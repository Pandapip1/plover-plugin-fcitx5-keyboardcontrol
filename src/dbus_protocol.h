#ifndef _PLOVER_FCITX5_DBUS_PROTOCOL_H_
#define _PLOVER_FCITX5_DBUS_PROTOCOL_H_

// D-Bus names/paths/interfaces for both halves of the bridge between the
// fcitx5 addon (ploverengine.h/cpp) and the Plover-side native extension
// (bridge.h/cpp) -- single source of truth for both, and for
// org.openstenoproject.plover.fcitx5.xml, which must keep matching this by
// hand since it's consumed by the D-Bus introspection machinery, not C++.

namespace ploverfcitx5 {

inline constexpr char kPloverServiceName[] = "org.openstenoproject.Plover";
inline constexpr char kPloverBridgePath[] =
    "/org/openstenoproject/Plover/Fcitx5Bridge";
inline constexpr char kPloverBridgeInterface[] =
    "org.openstenoproject.Plover.Fcitx5Bridge1";
inline constexpr char kEngineServiceName[] = "org.fcitx.Fcitx5";
inline constexpr char kEngineObjectPath[] =
    "/org/openstenoproject/PloverFcitx5/Engine";
inline constexpr char kEngineInterface[] =
    "org.openstenoproject.PloverFcitx5.Engine1";

} // namespace ploverfcitx5

#endif // _PLOVER_FCITX5_DBUS_PROTOCOL_H_
