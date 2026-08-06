cmake_minimum_required(VERSION 3.16)

project(fcitx5-plover VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Minimum fcitx5 version providing the InputMethodEngine/ObjectVTable API
# this addon relies on, not necessarily the exact version it was built
# against -- deliberately a floor, not a pin.
set(FCITX5_PLOVER_REQUIRED_FCITX_VERSION 5.1.0)

find_package(Fcitx5Core ${FCITX5_PLOVER_REQUIRED_FCITX_VERSION} REQUIRED)
find_package(Fcitx5Module REQUIRED COMPONENTS DBus)
find_package(Fcitx5Utils REQUIRED)
# Development.Module (not the heavier Development.Embed/full Development)
# is all pybind11_add_module (src/plugin) needs.
find_package(Python3 REQUIRED COMPONENTS Interpreter Development.Module)

include(GNUInstallDirs)
include("${FCITX_INSTALL_CMAKECONFIG_DIR}/Fcitx5Utils/Fcitx5CompilerSettings.cmake")

add_subdirectory(src)

# The `plover_fcitx5` Plover plugin (pyproject.toml/setup.cfg, at the top of
# this source tree) is installed via pip alongside the addon built above, so
# a single `cmake --install` -- i.e. a single Nix derivation -- produces one
# output tree containing both halves, instead of this project needing a
# separate `pip install` step wired up by whoever packages it. `--no-index`
# and `--no-build-isolation` keep this offline: everything it needs
# (setuptools, and at runtime Plover itself) is expected to already be
# available to Python3_EXECUTABLE, the same way a Nix build's sandbox
# provides them as inputs rather than letting pip fetch them.
#
# `\$ENV{DESTDIR}` is intentionally left escaped so it's expanded by the
# generated cmake_install.cmake at install time (i.e. respects a `DESTDIR=`
# passed to `cmake --install`/`make install`), not baked in now at configure
# time the way `${CMAKE_INSTALL_PREFIX}` below is.
install(CODE "
    execute_process(
        COMMAND \"${Python3_EXECUTABLE}\" -m pip install
                --no-build-isolation --no-index --no-deps
                --prefix \"\$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}\"
                \"${CMAKE_SOURCE_DIR}\"
        RESULT_VARIABLE _fcitx5_plover_pip_result
    )
    if(NOT _fcitx5_plover_pip_result EQUAL 0)
        message(FATAL_ERROR \"pip install of the plover_fcitx5 Python package failed\")
    endif()
")
