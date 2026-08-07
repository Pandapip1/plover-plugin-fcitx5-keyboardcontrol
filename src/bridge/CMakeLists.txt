# The fcitx5 addon, loaded into the fcitx5 process, bridging its keystrokes
# to Plover over D-Bus. Shares ../keysyms.h and ../dbus_protocol.h with
# ../plugin, the Plover-side half of this plugin.
add_fcitx5_addon(ploverfcitx5 ploverengine.cpp)
target_link_libraries(ploverfcitx5 Fcitx5::Core Fcitx5::Module::DBus)
target_include_directories(ploverfcitx5 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
install(TARGETS ploverfcitx5 DESTINATION "${FCITX_INSTALL_ADDONDIR}")

configure_file(ploverfcitx5.conf.in ploverfcitx5.conf @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/ploverfcitx5.conf"
       DESTINATION "${FCITX_INSTALL_PKGDATADIR}/addon")
