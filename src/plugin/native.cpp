// pybind11 bindings exposing Bridge (bridge.h/cpp) to Python as
// plover_fcitx5._native -- KeyboardCapture/KeyboardEmulation in
// keyboardcontrol_fcitx5.py are thin Capture/Output subclasses that just
// delegate to Capture/Emulation below, so nearly everything that used to be
// in _keyboardcontrol_fcitx5_impl.py (the D-Bus bridge, the keysym tables,
// modifier-state tracking) now lives in C++, here and in bridge.cpp/
// keysyms.h.
//
// GIL discipline: every call into Bridge that can block on the event loop
// thread (anything using Bridge::runOnEventLoop() under the hood --
// pushSuppressedKeys/commitText/forwardKey) releases the GIL first and
// doesn't touch any py::object until it's reacquired. This isn't just a
// throughput nicety: the event loop thread acquires the GIL itself to
// invoke the key-event callback below, and if a Python thread were to block
// on the event loop *while holding the GIL*, and the event loop thread
// happened to be trying to acquire that same GIL first (to deliver a
// KeyDown it received first), the two would deadlock on each other.

#include <algorithm>
#include <chrono>
#include <cctype>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <fcitx-utils/key.h>
#include <fcitx-utils/keysymgen.h>

#include "bridge.h"
#include "keysyms.h"

namespace py = pybind11;

namespace {

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

void warnNotFocused(const std::string &action) {
    py::module_::import("plover.log")
        .attr("warning")(
            "Cannot %s - Plover is not the active/focused fcitx5 input "
            "method. Select \"Plover\" in your input source switcher and "
            "focus a text field.",
            action);
}

std::string keysymPortableName(uint32_t sym) {
    return fcitx::Key::keySymToString(static_cast<fcitx::KeySym>(sym),
                                      fcitx::KeyStringFormat::Portable);
}

// dict.get, ready to hand to plover.key_combo.parse_key_combo as its
// key_name_to_key_code lookup -- built once from kHandledKeys (see
// keysyms.h) plus the modifier keysyms, keyed by each one's *X11 keysym*
// name (e.g. "comma", "grave"), matching the vocabulary the X11/uinput
// backends already give combo strings in existing dictionaries -- not
// HandledKey::ploverName, which is Plover's own (different, for
// punctuation) vocabulary used by key_down()/key_up()/suppress() instead;
// see keysyms.h. Then run through Plover's own
// plover.key_combo.add_modifiers_aliases() so "shift"/"command"/"option"/etc
// resolve exactly the way Plover documents them, without hand-copying that
// alias list here too. Deliberately leaked (never destructed): it's a
// process-lifetime cache, and destroying a py::object after the
// interpreter starts tearing down is its own hazard.
py::object &keyNameLookup() {
    static py::object *lookup = [] {
        py::dict table;
        for (const auto &key : fcitx5plover::kHandledKeys) {
            table[py::str(toLower(keysymPortableName(key.keysym)))] =
                py::int_(key.keysym);
        }
        for (uint32_t sym : fcitx5plover::kModifierKeysyms) {
            table[py::str(toLower(keysymPortableName(sym)))] = py::int_(sym);
        }
        py::module_::import("plover.key_combo")
            .attr("add_modifiers_aliases")(table);
        return new py::object(table.attr("get"));
    }();
    return *lookup;
}

class Capture {
public:
    void start(py::function onKeyEvent) {
        onKeyEvent_ = std::move(onKeyEvent);
        py::gil_scoped_release release;
        fcitx5plover::Bridge::instance().setKeyCallback(
            [this](const std::string &key, bool pressed) {
                py::gil_scoped_acquire acquire;
                try {
                    onKeyEvent_(key, pressed);
                } catch (const py::error_already_set &) {
                    // Nothing upstream of the fcitx-utils event loop can
                    // usefully handle a Python exception -- log it (mirrors
                    // an uncaught exception in any other Plover callback)
                    // and keep the event loop alive rather than let it
                    // unwind into non-Python-aware C++ stack frames.
                    PyErr_Print();
                }
            });
        fcitx5plover::Bridge::instance().pushSuppressedKeys(suppressedKeysyms_);
    }

    void cancel() { fcitx5plover::Bridge::instance().clearKeyCallback(); }

    void suppress(const std::vector<std::string> &keys) {
        suppressedKeysyms_.clear();
        for (const auto &key : keys) {
            if (auto sym = fcitx5plover::keysymForPloverName(key)) {
                suppressedKeysyms_.push_back(*sym);
            }
        }
        fcitx5plover::Bridge::instance().pushSuppressedKeys(suppressedKeysyms_);
    }

private:
    py::function onKeyEvent_;
    std::vector<uint32_t> suppressedKeysyms_;
};

class Emulation {
public:
    void send_string(const std::string &s) {
        auto contextId = focusedContextIdReleased();
        if (!contextId) {
            warnNotFocused("send text");
            return;
        }
        py::gil_scoped_release release;
        fcitx5plover::Bridge::instance().commitText(*contextId, s);
    }

    void send_backspaces(int count) {
        auto contextId = focusedContextIdReleased();
        if (!contextId) {
            warnNotFocused("send backspaces");
            return;
        }
        py::gil_scoped_release release;
        auto &bridge = fcitx5plover::Bridge::instance();
        for (int i = 0; i < count; ++i) {
            bridge.forwardKey(*contextId, FcitxKey_BackSpace, 0, 0, false);
            bridge.forwardKey(*contextId, FcitxKey_BackSpace, 0, 0, true);
            sleepForDelay();
        }
    }

    void send_key_combination(const std::string &combo) {
        auto contextId = focusedContextIdReleased();
        if (!contextId) {
            warnNotFocused("send key combination");
            return;
        }

        // https://plover.readthedocs.io/en/latest/api/key_combo.html
        // Parsed with the GIL held (it calls back into Python), and the
        // whole result materialized into plain C++ data before releasing
        // the GIL below -- nothing past this point touches a py::object.
        std::vector<std::pair<uint32_t, bool>> events;
        {
            py::object result = py::module_::import("plover.key_combo")
                                    .attr("parse_key_combo")(combo, keyNameLookup());
            for (auto item : result) {
                auto pair = item.cast<py::tuple>();
                events.emplace_back(pair[0].cast<uint32_t>(), pair[1].cast<bool>());
            }
        }

        py::gil_scoped_release release;
        auto &bridge = fcitx5plover::Bridge::instance();
        uint32_t state = 0;
        for (const auto &[keysym, pressed] : events) {
            bridge.forwardKey(*contextId, keysym, 0, state, !pressed);
            sleepForDelay();
            if (uint32_t mask = fcitx5plover::modifierMaskFor(keysym)) {
                if (pressed) {
                    state |= mask;
                } else {
                    state &= ~mask;
                }
            }
        }
    }

    void set_key_press_delay(int delayMs) { keyPressDelayMs_ = delayMs; }

private:
    // Reads Bridge's focused-context with the GIL released, for the same
    // deadlock-avoidance reason as everything else in this file -- see the
    // top-of-file comment.
    std::optional<std::string> focusedContextIdReleased() {
        py::gil_scoped_release release;
        return fcitx5plover::Bridge::instance().focusedContextId();
    }

    void sleepForDelay() const {
        if (keyPressDelayMs_ > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(keyPressDelayMs_));
        }
    }

    int keyPressDelayMs_ = 0;
};

} // namespace

PYBIND11_MODULE(_native, m) {
    m.def("probe_addon", &fcitx5plover::probeAddon,
         py::call_guard<py::gil_scoped_release>());

    py::class_<Capture>(m, "Capture")
        .def(py::init<>())
        .def("start", &Capture::start)
        .def("cancel", &Capture::cancel, py::call_guard<py::gil_scoped_release>())
        .def("suppress", &Capture::suppress,
             py::call_guard<py::gil_scoped_release>());

    py::class_<Emulation>(m, "Emulation")
        .def(py::init<>())
        .def("send_string", &Emulation::send_string)
        .def("send_backspaces", &Emulation::send_backspaces)
        .def("send_key_combination", &Emulation::send_key_combination)
        .def("set_key_press_delay", &Emulation::set_key_press_delay);
}
