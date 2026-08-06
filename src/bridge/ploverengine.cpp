#include "ploverengine.h"

#include <fcitx-utils/log.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>

#include "keysyms.h"

namespace fcitx {

namespace {

// This side only needs to know whether a keysym is recognized/a modifier at
// all, not the Plover key name it maps to (bridge.cpp, the Plover-side half
// of this bridge, needs that instead, for its key_down()/key_up() calls) --
// see keysyms.h for the shared table both sides draw from.
const std::unordered_set<uint32_t> &kHandledKeysyms =
    fcitx5plover::handledKeysymSet();
const std::unordered_set<uint32_t> &kModifierKeysyms =
    fcitx5plover::modifierKeysymSet();

} // namespace

void PloverEngineService::commitText(const std::string &contextId,
                                     const std::string &text) {
    engine_->commitText(contextId, text);
}

void PloverEngineService::forwardKey(const std::string &contextId,
                                     uint32_t keysym, uint32_t keycode,
                                     uint32_t state, bool isRelease) {
    engine_->forwardKey(contextId, keysym, keycode, state, isRelease);
}

void PloverEngineService::setSuppressedKeys(
    const std::vector<uint32_t> &keysyms) {
    engine_->setSuppressedKeys(keysyms);
}

PloverEngine::PloverEngine(Instance *instance)
    : instance_(instance), service_(this) {
    bus()->addObjectVTable(kEngineObjectPath, kEngineInterface, service_);
    watcher_ = std::make_unique<dbus::ServiceWatcher>(*bus());
    watchHandle_ = watcher_->watchService(
        kPloverServiceName,
        [this](const std::string &, const std::string &,
               const std::string &newOwner) {
            ploverPresent_ = !newOwner.empty();
            if (!ploverPresent_) {
                // Plover is gone: never get stuck suppressing keys nobody
                // is listening to translate anymore.
                clearModifierState();
            }
        });
}

PloverEngine::~PloverEngine() = default;

dbus::Bus *PloverEngine::bus() { return dbus()->call<IDBusModule::bus>(); }

std::vector<InputMethodEntry> PloverEngine::listInputMethods() {
    std::vector<InputMethodEntry> result;
    result.push_back(InputMethodEntry("plover", "Plover", "en", "fcitx5plover"));
    return result;
}

std::string PloverEngine::idFor(InputContext *ic) {
    auto id = std::to_string(reinterpret_cast<uintptr_t>(ic));
    contexts_[id] = ic->watch();
    return id;
}

InputContext *PloverEngine::contextFor(const std::string &contextId) {
    auto it = contexts_.find(contextId);
    if (it == contexts_.end()) {
        return nullptr;
    }
    auto *ic = it->second.get();
    if (!ic) {
        // The context was destroyed without deactivate() telling us --
        // shouldn't normally happen, but don't hold a stale entry either
        // way.
        contexts_.erase(it);
    }
    return ic;
}

void PloverEngine::forgetContext(const std::string &contextId) {
    contexts_.erase(contextId);
}

void PloverEngine::clearModifierState() {
    suppressedKeysyms_.clear();
    downModifierKeysyms_.clear();
    keysPressedWithModifier_.clear();
}

bool PloverEngine::handleKey(InputContext *ic, uint32_t keysym,
                             bool isRelease) {
    if (suppressedKeysyms_.empty()) {
        // Nothing suppressed: still tell Plover about recognized keys (it
        // needs this for global shortcuts like PLOVER_TOGGLE/PHROLG), but
        // never consume anything.
        if (kHandledKeysyms.count(keysym)) {
            notifyKeyState(ic, keysym, !isRelease);
        }
        return false;
    }

    if (kModifierKeysyms.count(keysym)) {
        if (isRelease) {
            downModifierKeysyms_.erase(keysym);
        } else {
            downModifierKeysyms_.insert(keysym);
        }
        return false;
    }

    if (!kHandledKeysyms.count(keysym)) {
        // Not a supported key. Passthrough.
        return false;
    }

    if (!isRelease && !downModifierKeysyms_.empty()) {
        // Part of an app shortcut (e.g. Ctrl+C), not a stroke: passthrough
        // untouched, and don't report it to Plover at all.
        keysPressedWithModifier_.insert(keysym);
        return false;
    }

    if (isRelease && keysPressedWithModifier_.count(keysym)) {
        // Matching release for the case above: also passthrough untouched,
        // even if the modifier was already released.
        keysPressedWithModifier_.erase(keysym);
        return false;
    }

    notifyKeyState(ic, keysym, !isRelease);
    return suppressedKeysyms_.count(keysym) > 0;
}

void PloverEngine::keyEvent(const InputMethodEntry &, KeyEvent &keyEvent) {
    const auto keysym = static_cast<uint32_t>(keyEvent.key().sym());
    if (handleKey(keyEvent.inputContext(), keysym, keyEvent.isRelease())) {
        keyEvent.filterAndAccept();
    }
}

void PloverEngine::activate(const InputMethodEntry &,
                            InputContextEvent &event) {
    notifyActivate(idFor(event.inputContext()));
}

void PloverEngine::deactivate(const InputMethodEntry &,
                              InputContextEvent &event) {
    const auto contextId = idFor(event.inputContext());
    notifyDeactivate(contextId);
    // Nothing can call KeyEvent/CommitText/ForwardKey for this context
    // again once it's deactivated; stop tracking it rather than leak the
    // entry for the lifetime of the addon.
    forgetContext(contextId);
}

void PloverEngine::reset(const InputMethodEntry &, InputContextEvent &event) {
    clearModifierState();
    notifyReset(idFor(event.inputContext()));
}

void PloverEngine::commitText(const std::string &contextId,
                              const std::string &text) {
    auto *ic = contextFor(contextId);
    if (!ic) {
        return;
    }
    ic->commitString(text);
}

void PloverEngine::forwardKey(const std::string &contextId, uint32_t keysym,
                              uint32_t keycode, uint32_t state,
                              bool isRelease) {
    auto *ic = contextFor(contextId);
    if (!ic) {
        return;
    }
    ic->forwardKey(Key(static_cast<KeySym>(keysym), KeyStates(state),
                       static_cast<int>(keycode)),
                  isRelease);
}

void PloverEngine::setSuppressedKeys(const std::vector<uint32_t> &keysyms) {
    suppressedKeysyms_ =
        std::unordered_set<uint32_t>(keysyms.begin(), keysyms.end());
}

void PloverEngine::notifyKeyState(InputContext *ic, uint32_t keysym,
                                  bool pressed) {
    if (!ploverPresent_) {
        return;
    }
    auto msg = bus()->createMethodCall(kPloverServiceName, kPloverBridgePath,
                                       kPloverBridgeInterface,
                                       pressed ? "KeyDown" : "KeyUp");
    msg << idFor(ic) << keysym;
    msg.send();
}

void PloverEngine::notifyActivate(const std::string &contextId) {
    if (!ploverPresent_) {
        return;
    }
    auto msg = bus()->createMethodCall(kPloverServiceName, kPloverBridgePath,
                                       kPloverBridgeInterface, "Activate");
    msg << contextId;
    msg.send();
}

void PloverEngine::notifyDeactivate(const std::string &contextId) {
    if (!ploverPresent_) {
        return;
    }
    auto msg = bus()->createMethodCall(kPloverServiceName, kPloverBridgePath,
                                       kPloverBridgeInterface, "Deactivate");
    msg << contextId;
    msg.send();
}

void PloverEngine::notifyReset(const std::string &contextId) {
    if (!ploverPresent_) {
        return;
    }
    auto msg = bus()->createMethodCall(kPloverServiceName, kPloverBridgePath,
                                       kPloverBridgeInterface, "Reset");
    msg << contextId;
    msg.send();
}

} // namespace fcitx

FCITX_ADDON_FACTORY_V2(fcitx5plover, fcitx::PloverEngineFactory)
