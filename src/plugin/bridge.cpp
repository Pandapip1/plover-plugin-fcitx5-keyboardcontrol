#include "bridge.h"

#include <algorithm>
#include <future>
#include <utility>

#include <fcitx-utils/dbus/message.h>
#include <fcitx-utils/log.h>

#include "dbus_protocol.h"
#include "keysyms.h"

namespace fcitx5plover {

namespace {
constexpr uint64_t kCallTimeoutUsec = 2000000; // 2s, matching the old Python.
} // namespace

std::vector<std::string> probeAddon() {
    // Its own throwaway connection -- deliberately untangled from the
    // shared Bridge singleton below, so this stays safe to call any time
    // (e.g. every time the config GUI opens) without side effects on a
    // possibly-already-running Capture/Emulation session.
    try {
        fcitx::dbus::Bus bus(fcitx::dbus::BusType::Session);
        auto msg = bus.createMethodCall(kEngineServiceName, kEngineObjectPath,
                                        "org.freedesktop.DBus.Introspectable",
                                        "Introspect");
        auto reply = msg.call(kCallTimeoutUsec);
        if (reply.isError()) {
            return {"the fcitx5-plover addon does not appear to be loaded "
                    "(is fcitx5 running, with it installed and enabled?): " +
                    reply.errorMessage()};
        }
        std::string introspectionXml;
        reply >> introspectionXml;
        if (introspectionXml.find(kEngineInterface) == std::string::npos) {
            return {"the fcitx5-plover addon does not appear to be loaded"};
        }
        return {};
    } catch (const std::exception &e) {
        return {std::string("failed to connect to the D-Bus session bus: ") +
                e.what()};
    }
}

// Exposes Fcitx5Bridge1 (KeyDown/KeyUp/Activate/Deactivate/Reset), called by
// the addon. Every method here returns nothing and is fire-and-forget from
// the addon's point of view -- see org.openstenoproject.plover.fcitx5.xml.
class Bridge::Service : public fcitx::dbus::ObjectVTable<Bridge::Service> {
public:
    explicit Service(Bridge *bridge) : bridge_(bridge) {}

private:
    void keyDown(const std::string &contextId, uint32_t keysym) {
        bridge_->handleKeyDown(contextId, keysym);
    }
    void keyUp(const std::string &contextId, uint32_t keysym) {
        bridge_->handleKeyUp(contextId, keysym);
    }
    void activate(const std::string &contextId) {
        bridge_->handleActivate(contextId);
    }
    void deactivate(const std::string &contextId) {
        bridge_->handleDeactivate(contextId);
    }
    void reset(const std::string &contextId) { bridge_->handleReset(contextId); }

    FCITX_OBJECT_VTABLE_METHOD(keyDown, "KeyDown", "su", "");
    FCITX_OBJECT_VTABLE_METHOD(keyUp, "KeyUp", "su", "");
    FCITX_OBJECT_VTABLE_METHOD(activate, "Activate", "s", "");
    FCITX_OBJECT_VTABLE_METHOD(deactivate, "Deactivate", "s", "");
    FCITX_OBJECT_VTABLE_METHOD(reset, "Reset", "s", "");

    Bridge *bridge_;
};

Bridge &Bridge::instance() {
    static Bridge bridge;
    return bridge;
}

Bridge::Bridge() {
    std::promise<void> ready;
    auto readyFuture = ready.get_future();
    thread_ = std::thread([this, &ready] {
        try {
            loop_ = std::make_unique<fcitx::EventLoop>();
            dispatcher_ = std::make_unique<fcitx::EventDispatcher>();
            dispatcher_->attach(loop_.get());
            bus_ = std::make_unique<fcitx::dbus::Bus>(fcitx::dbus::BusType::Session);
            bus_->attachEventLoop(loop_.get());
            service_ = std::make_unique<Service>(this);
            bus_->addObjectVTable(kPloverBridgePath, kPloverBridgeInterface,
                                  *service_);
            bus_->requestName(kPloverServiceName,
                              fcitx::Flags<fcitx::dbus::RequestNameFlag>());
        } catch (...) {
            ready.set_exception(std::current_exception());
            return;
        }
        ready.set_value();
        loop_->exec();
    });
    // Propagates the exception (rethrown here, on the constructing/Python
    // thread) if setup above failed -- matches the old _ensure_connected()
    // raising synchronously out of KeyboardCapture()/KeyboardEmulation().
    // Must join first: the failed thread already returned (it never reaches
    // loop_->exec()), and a still-joinable std::thread whose destructor runs
    // during this constructor's exception unwinding would call
    // std::terminate() instead of letting the exception propagate.
    try {
        readyFuture.get();
    } catch (...) {
        thread_.join();
        throw;
    }
}

Bridge::~Bridge() {
    if (thread_.joinable()) {
        dispatcher_->schedule([this] { loop_->exit(); });
        thread_.join();
    }
}

void Bridge::runOnEventLoop(const std::function<void()> &fn) {
    std::promise<void> done;
    auto doneFuture = done.get_future();
    dispatcher_->schedule([&fn, &done] {
        fn();
        done.set_value();
    });
    doneFuture.get();
}

void Bridge::setKeyCallback(KeyCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    keyCallback_ = std::move(callback);
    pressedKeys_.clear();
}

void Bridge::clearKeyCallback() {
    releaseAllPressedKeys();
    std::lock_guard<std::mutex> lock(mutex_);
    keyCallback_ = nullptr;
}

void Bridge::releaseAllPressedKeys() {
    // A context we were reporting keys for lost focus, was told to reset,
    // or Capture.cancel() ran: its eventual key-up may never reach us (it'll
    // go to whatever the user focuses next, if anything), so release
    // everything now rather than risk a stuck key -- same role
    // do_focus_out(release_held=True) plays for the IBus backend.
    //
    // Copies the callback and the pressed-key list out and drops the lock
    // *before* invoking the callback: it ends up calling into Python (see
    // native.cpp), which needs the GIL, and the calling Python thread may
    // itself be blocked (elsewhere) waiting on this same mutex_ -- holding
    // both at once would be a lock-order inversion waiting to deadlock.
    KeyCallback callback;
    std::vector<std::string> keys;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callback = keyCallback_;
        keys = std::move(pressedKeys_);
        pressedKeys_.clear();
    }
    if (callback) {
        for (const auto &key : keys) {
            callback(key, false);
        }
    }
}

std::optional<std::string> Bridge::focusedContextId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return focusedContextId_;
}

void Bridge::pushSuppressedKeys(const std::vector<uint32_t> &keysyms) {
    runOnEventLoop([this, &keysyms] {
        auto msg = bus_->createMethodCall(kEngineServiceName, kEngineObjectPath,
                                          kEngineInterface, "SetSuppressedKeys");
        msg << keysyms;
        auto reply = msg.call(kCallTimeoutUsec);
        if (reply.isError()) {
            FCITX_WARN() << "fcitx5 SetSuppressedKeys call failed: "
                         << reply.errorMessage();
        }
    });
}

void Bridge::commitText(const std::string &contextId, const std::string &text) {
    runOnEventLoop([this, &contextId, &text] {
        auto msg = bus_->createMethodCall(kEngineServiceName, kEngineObjectPath,
                                          kEngineInterface, "CommitText");
        msg << contextId << text;
        auto reply = msg.call(kCallTimeoutUsec);
        if (reply.isError()) {
            FCITX_WARN() << "fcitx5 CommitText call failed: "
                         << reply.errorMessage();
        }
    });
}

void Bridge::forwardKey(const std::string &contextId, uint32_t keysym,
                        uint32_t keycode, uint32_t state, bool isRelease) {
    runOnEventLoop([this, &contextId, keysym, keycode, state, isRelease] {
        auto msg = bus_->createMethodCall(kEngineServiceName, kEngineObjectPath,
                                          kEngineInterface, "ForwardKey");
        msg << contextId << keysym << keycode << state << isRelease;
        auto reply = msg.call(kCallTimeoutUsec);
        if (reply.isError()) {
            FCITX_WARN() << "fcitx5 ForwardKey call failed: "
                         << reply.errorMessage();
        }
    });
}

void Bridge::handleKeyDown(const std::string &contextId, uint32_t keysym) {
    // As in releaseAllPressedKeys(): the callback (eventually calling into
    // Python) is invoked with mutex_ *not* held.
    KeyCallback callback;
    std::optional<std::string> key;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        focusedContextId_ = contextId;
        key = ploverNameForKeysym(keysym);
        if (key && keyCallback_) {
            pressedKeys_.push_back(*key);
            callback = keyCallback_;
        }
    }
    if (callback) {
        callback(*key, true);
    }
}

void Bridge::handleKeyUp(const std::string &contextId, uint32_t keysym) {
    KeyCallback callback;
    std::optional<std::string> key;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        focusedContextId_ = contextId;
        key = ploverNameForKeysym(keysym);
        if (key && keyCallback_) {
            auto it = std::find(pressedKeys_.begin(), pressedKeys_.end(), *key);
            if (it != pressedKeys_.end()) {
                pressedKeys_.erase(it);
            }
            callback = keyCallback_;
        }
    }
    if (callback) {
        callback(*key, false);
    }
}

void Bridge::handleActivate(const std::string &contextId) {
    std::lock_guard<std::mutex> lock(mutex_);
    focusedContextId_ = contextId;
}

void Bridge::handleDeactivate(const std::string &contextId) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (focusedContextId_ == contextId) {
            focusedContextId_ = std::nullopt;
        }
    }
    releaseAllPressedKeys();
}

void Bridge::handleReset(const std::string &contextId) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        focusedContextId_ = contextId;
    }
    releaseAllPressedKeys();
}

} // namespace fcitx5plover
