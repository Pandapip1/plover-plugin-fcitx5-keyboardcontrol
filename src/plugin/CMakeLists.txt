# The Plover-side plugin: a native extension, loaded into Plover's Python
# process as plover_fcitx5._native. Built on Fcitx5::Utils (fcitx-utils' own
# dbus::Bus/EventLoop/Key, already a dependency of ../bridge, the fcitx5
# addon this talks to) instead of PyGObject/GLib, so this plugin no longer
# needs PyGObject or the standalone xkbcommon Python package as runtime
# dependencies at all.
find_package(pybind11 CONFIG REQUIRED)
pybind11_add_module(_native bridge.cpp native.cpp)
target_link_libraries(_native PRIVATE Fcitx5::Utils Python3::Python)
target_include_directories(_native PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")

# Where pip (see the install(CODE) hook in the top-level CMakeLists.txt)
# puts plover_fcitx5's pure-Python files under CMAKE_INSTALL_PREFIX --
# computed the same way pip itself resolves `--prefix`, so _native ends up
# installed into the very same plover_fcitx5/ package directory.
execute_process(
   COMMAND "${Python3_EXECUTABLE}" -c
       "import sysconfig; print(sysconfig.get_path('platlib', vars={'base': '', 'platbase': ''}))"
   OUTPUT_VARIABLE FCITX5_PLOVER_PYTHON_SITE_PACKAGES
   OUTPUT_STRIP_TRAILING_WHITESPACE
)
install(TARGETS _native LIBRARY DESTINATION
       "${CMAKE_INSTALL_PREFIX}${FCITX5_PLOVER_PYTHON_SITE_PACKAGES}/plover_fcitx5")
