# The fcitx5 addon, loaded into the fcitx5 process, bridging its keystrokes
# to Plover over D-Bus. Shares ../keysyms.h and ../dbus_protocol.h with
# ../plugin, the Plover-side half of this plugin.
add_fcitx5_addon(fcitx5plover ploverengine.cpp)
target_link_libraries(fcitx5plover Fcitx5::Core Fcitx5::Module::DBus)
target_include_directories(fcitx5plover PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
install(TARGETS fcitx5plover DESTINATION "${FCITX_INSTALL_ADDONDIR}")

configure_file(fcitx5plover.conf.in fcitx5plover.conf @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/fcitx5plover.conf"
       DESTINATION "${FCITX_INSTALL_PKGDATADIR}/addon")
