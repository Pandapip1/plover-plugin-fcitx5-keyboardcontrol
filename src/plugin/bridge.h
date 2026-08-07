#ifndef _PLOVER_FCITX5_BRIDGE_H_
#define _PLOVER_FCITX5_BRIDGE_H_

// Plover-side half of the D-Bus bridge to the fcitx5 addon
// (../bridge/ploverengine.h/cpp) -- owns the connection and background
// event loop shared process-wide between Capture and Emulation
// (native.cpp), the same way keyboardcontrol_fcitx5.py's old module-level
// _ensure_connected()/_Bridge did, but built on fcitx-utils' own
// EventLoop/dbus::Bus (already a dependency of that addon too) instead of
// PyGObject/GLib, so this plugin no longer needs PyGObject or the
// standalone xkbcommon Python package as runtime dependencies at all.
//
// Deliberately has no pybind11/Python.h dependency: everything here is
// plain C++, callable from a unit test or another host with no Python
// involved. native.cpp is the only place that bridges these callbacks to
// Python (and the only place that has to think about the GIL).

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <fcitx-utils/dbus/bus.h>
#include <fcitx-utils/dbus/objectvtable.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/eventdispatcher.h>

namespace ploverfcitx5 {

// Reasons the plover-fcitx5 addon isn't reachable right now, or an empty
// vector if it is. Cheap, synchronous, side-effect-free (its own throwaway
// Bus connection, untangled from the shared Bridge below) -- safe to call
// any time, e.g. every time the config GUI opens.
std::vector<std::string> probeAddon();

// Fired for every recognized key the addon reports, on the event loop
// thread -- native.cpp's callers are responsible for acquiring the GIL
// before touching Python state from within one.
using KeyCallback = std::function<void(const std::string &ploverKey, bool pressed)>;

// Owns the D-Bus connection and its background event loop thread. There is
// only ever one plover-fcitx5 addon connection process-wide, regardless of
// how many of Capture/Emulation are in use, so this is a singleton rather
// than something native.cpp constructs per instance.
class Bridge {
public:
    static Bridge &instance();

    Bridge(const Bridge &) = delete;
    Bridge &operator=(const Bridge &) = delete;

    // Only one Capture is ever active at a time -- same contract as the old
    // _Bridge.capture. Registering a new callback starts a fresh "pressed
    // keys" tracking session; clearing one (passing nullptr) first
    // synthesizes a release for every key still down, the same role
    // KeyboardCapture.cancel()'s _release_all_pressed_keys() played before.
    void setKeyCallback(KeyCallback callback);
    void clearKeyCallback();

    void pushSuppressedKeys(const std::vector<uint32_t> &keysyms);
    void commitText(const std::string &contextId, const std::string &text);
    void forwardKey(const std::string &contextId, uint32_t keysym,
                    uint32_t keycode, uint32_t state, bool isRelease);

    // The context Emulation should currently type into, if any -- nullopt
    // means Plover isn't the focused/active fcitx5 input method right now.
    std::optional<std::string> focusedContextId() const;

private:
    class Service;

    Bridge();
    ~Bridge();

    // Synchronously runs `fn` on the event loop thread and waits for it to
    // finish, the same role _run_on_loop()/GLib.idle_add() played in the
    // old Python implementation.
    void runOnEventLoop(const std::function<void()> &fn);

    // Synthesizes a release (fires the callback with pressed=false) for
    // every key still down, then forgets them. Never invokes the callback
    // while mutex_ is held -- see the .cpp for why that matters.
    void releaseAllPressedKeys();

    // Handlers for incoming Fcitx5Bridge1 calls, invoked on the event loop
    // thread by Service. Mirrors _Bridge.handle_method_call exactly.
    void handleKeyDown(const std::string &contextId, uint32_t keysym);
    void handleKeyUp(const std::string &contextId, uint32_t keysym);
    void handleActivate(const std::string &contextId);
    void handleDeactivate(const std::string &contextId);
    void handleReset(const std::string &contextId);

    std::thread thread_;
    std::unique_ptr<fcitx::EventLoop> loop_;
    std::unique_ptr<fcitx::EventDispatcher> dispatcher_;
    std::unique_ptr<fcitx::dbus::Bus> bus_;
    std::unique_ptr<Service> service_;

    mutable std::mutex mutex_;
    KeyCallback keyCallback_;
    std::vector<std::string> pressedKeys_;
    std::optional<std::string> focusedContextId_;

    friend class Service;
};

} // namespace ploverfcitx5

#endif // _PLOVER_FCITX5_BRIDGE_H_
