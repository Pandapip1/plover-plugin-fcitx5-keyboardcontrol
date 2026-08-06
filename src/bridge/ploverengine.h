#ifndef _FCITX5_PLOVER_PLOVERENGINE_H_
#define _FCITX5_PLOVER_PLOVERENGINE_H_

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <fcitx-utils/dbus/bus.h>
#include <fcitx-utils/dbus/objectvtable.h>
#include <fcitx-utils/dbus/servicewatcher.h>
#include <fcitx-utils/trackableobject.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>

#include <dbus_public.h>

#include "dbus_protocol.h"

namespace fcitx {

// D-Bus names/paths -- see dbus_protocol.h, shared with bridge.h/cpp (the
// Plover-side half of this bridge) and org.openstenoproject.plover.fcitx5.xml.
using fcitx5plover::kEngineInterface;
using fcitx5plover::kEngineObjectPath;
using fcitx5plover::kPloverBridgeInterface;
using fcitx5plover::kPloverBridgePath;
using fcitx5plover::kPloverServiceName;

class PloverEngine;

// Exposes Engine1 (CommitText/ForwardKey/SetSuppressedKeys) on fcitx5's own
// bus connection, under whatever name fcitx5 itself owns -- no separate bus
// name needed for the addon. Calls straight through to the owning
// PloverEngine.
class PloverEngineService : public dbus::ObjectVTable<PloverEngineService> {
public:
    explicit PloverEngineService(PloverEngine *engine) : engine_(engine) {}

private:
    void commitText(const std::string &contextId, const std::string &text);
    void forwardKey(const std::string &contextId, uint32_t keysym,
                    uint32_t keycode, uint32_t state, bool isRelease);
    void setSuppressedKeys(const std::vector<uint32_t> &keysyms);

    FCITX_OBJECT_VTABLE_METHOD(commitText, "CommitText", "ss", "");
    FCITX_OBJECT_VTABLE_METHOD(forwardKey, "ForwardKey", "suuub", "");
    FCITX_OBJECT_VTABLE_METHOD(setSuppressedKeys, "SetSuppressedKeys", "au", "");

    PloverEngine *engine_;
};

// Plover, as far as fcitx5 is concerned, is an InputMethodEngine like any
// other -- one that happens to do its actual stroke translation in a
// separate process instead of in this address space.
//
// The accept/filter decision in keyEvent() is made entirely locally, with
// no D-Bus round trip: suppression is pure set-membership against whatever
// keysym set Plover last pushed via SetSuppressedKeys, and which keysyms
// are "recognized"/"modifiers" at all come from keysyms.h's shared tables,
// carried here rather than asked of Python per key. Once a key is accepted
// or passed through, telling Plover what happened (so its stroke-assembly
// state can follow along) is a fire-and-forget notification off fcitx5's
// critical path -- see org.openstenoproject.plover.fcitx5.xml for the full
// rationale.
class PloverEngine : public InputMethodEngine {
public:
    explicit PloverEngine(Instance *instance);
    ~PloverEngine() override;

    Instance *instance() { return instance_; }

    std::vector<InputMethodEntry> listInputMethods() override;
    void keyEvent(const InputMethodEntry &entry, KeyEvent &keyEvent) override;
    void activate(const InputMethodEntry &entry,
                  InputContextEvent &event) override;
    void deactivate(const InputMethodEntry &entry,
                     InputContextEvent &event) override;
    void reset(const InputMethodEntry &entry,
               InputContextEvent &event) override;

    // Called by PloverEngineService once Plover has decided what to type,
    // or to update the local suppression set.
    void commitText(const std::string &contextId, const std::string &text);
    void forwardKey(const std::string &contextId, uint32_t keysym,
                    uint32_t keycode, uint32_t state, bool isRelease);
    void setSuppressedKeys(const std::vector<uint32_t> &keysyms);

    FCITX_ADDON_DEPENDENCY_LOADER(dbus, instance_->addonManager());

private:
    dbus::Bus *bus();
    // Mints/looks up the opaque id contexts_ and the Fcitx5Bridge1 calls
    // key on, tracked with a weak reference so a destroyed InputContext
    // can never be dereferenced through it.
    std::string idFor(InputContext *ic);
    InputContext *contextFor(const std::string &contextId);
    void forgetContext(const std::string &contextId);

    // Returns whether to consume (filter) the key.
    bool handleKey(InputContext *ic, uint32_t keysym, bool isRelease);
    void clearModifierState();

    // Fire-and-forget notifications to Plover; no-ops if it isn't
    // currently running.
    void notifyKeyState(InputContext *ic, uint32_t keysym, bool pressed);
    void notifyActivate(const std::string &contextId);
    void notifyDeactivate(const std::string &contextId);
    void notifyReset(const std::string &contextId);

    Instance *instance_;
    PloverEngineService service_;
    std::unique_ptr<dbus::ServiceWatcher> watcher_;
    std::unique_ptr<dbus::ServiceWatcherEntry> watchHandle_;
    bool ploverPresent_ = false;

    std::unordered_set<uint32_t> suppressedKeysyms_;
    std::unordered_set<uint32_t> downModifierKeysyms_;
    std::unordered_set<uint32_t> keysPressedWithModifier_;

    std::unordered_map<std::string, TrackableObjectReference<InputContext>>
        contexts_;
};

class PloverEngineFactory : public AddonFactory {
public:
    AddonInstance *create(AddonManager *manager) override {
        return new PloverEngine(manager->instance());
    }
};

} // namespace fcitx

#endif // _FCITX5_PLOVER_PLOVERENGINE_H_
