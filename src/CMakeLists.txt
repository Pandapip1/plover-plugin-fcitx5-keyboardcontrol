# keysyms.h and dbus_protocol.h here are the shared single source of truth
# between plugin/ (the Plover-side plugin: the native extension Plover's
# KeyboardCapture/KeyboardEmulation delegate to) and bridge/ (the fcitx5
# addon that bridges fcitx5 keystrokes to it over D-Bus) -- see those files
# for why. Neither subdirectory builds a target of its own for them; each
# just adds this directory to its include path
# (target_include_directories(... "${CMAKE_CURRENT_SOURCE_DIR}/..")).
add_subdirectory(plugin)
add_subdirectory(bridge)
