cmake_minimum_required(VERSION 3.20)

project(kwin-effect-snow VERSION 0.2.0 LANGUAGES CXX)

set(SNOW_PACKAGE_VERSION "${PROJECT_VERSION}" CACHE STRING "Version used for distributable packages")
set(CPACK_PACKAGE_NAME "kwin-effect-snow")
set(CPACK_PACKAGE_VERSION "${SNOW_PACKAGE_VERSION}")
set(CPACK_PACKAGE_VENDOR "Snow contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Native KWin effect that renders falling snow")
set(CPACK_PACKAGE_DESCRIPTION "Snow is a native C++ KWin effect that renders falling snow and accumulation on windows and panels.")
set(CPACK_PACKAGE_CONTACT "Joep Admiraal <joep@groovytunes.nl>")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "kwin-effect-snow")
set(CPACK_PACKAGE_FILE_NAME "kwin-effect-snow-${SNOW_PACKAGE_VERSION}")

# These are runtime dependencies only. CPack never puts Qt, KDE Frameworks,
# or KWin libraries in the package; the effect must use the host installation.
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "kwin-common (>= 4:6.6), kwin-common (<< 4:6.7), kwin-wayland | kwin-x11, libqt6core6t64, libqt6gui6t64, libkf6configcore6, libkf6coreaddons6, libkf6configwidgets6, libkf6i18n6, libkf6kcmutils6")
set(CPACK_RPM_PACKAGE_ARCHITECTURE "x86_64")
set(CPACK_RPM_PACKAGE_REQUIRES "kwin >= 6.7, kwin < 6.8, qt6-qtbase, kf6-kconfig, kf6-kcoreaddons, kf6-kconfigwidgets, kf6-ki18n, kf6-kcmutils")
set(CPACK_RPM_PACKAGE_LICENSE "GPL-2.0-or-later")

include(CPack)

set(QT_MIN_VERSION 6.10.0)
set(KF_MIN_VERSION 6.22.0)

# Default to installing into the user's home rather than ECM's system prefix.
# KWin only finds effects there when QT_PLUGIN_PATH names it -- tools/dev.sh
# does that for the nested loop; see docs/development.md for the live session.
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/.local" CACHE PATH "Install prefix" FORCE)
endif()

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

# Deliberately not including KDEInstallDirs/KDECMakeSettings: they reset
# CMAKE_INSTALL_PREFIX to a system prefix, and this plugin only ever installs
# into one hand-picked directory.
include(KDECompilerSettings NO_POLICY_SCOPE)
include(FeatureSummary)

set(CMAKE_AUTOMOC ON)
# KWIN_EFFECT_FACTORY expands to a Q_OBJECT class, but AutoMoc only recognises a
# fixed list of macros; without this it warns that main.cpp includes main.moc
# "but does not contain a Q_OBJECT macro" on every clean build.
list(APPEND CMAKE_AUTOMOC_MACRO_NAMES "KWIN_EFFECT_FACTORY")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Gui DBus Quick Widgets)
# ConfigWidgets, I18n and KCMUtils are the config module's alone: the effect
# itself links none of them, and nothing KWin loads pulls in a widget toolkit.
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS
    Config ConfigWidgets CoreAddons I18n KCMUtils WindowSystem)

# KWin exports no stable ABI for out-of-tree binary effects, so the plugin is
# built against whatever kwin-dev is installed and rebuilt on each KWin bump.
# See docs/adr/0002-native-cpp-effect-accepting-abi-churn.md.
find_package(KWin REQUIRED)
message(STATUS "Building against KWin ${KWin_VERSION}")

# KWin's PluginEffectLoader scans the "kwin/effects/plugins" namespace under
# every Qt library path, giving:
#   ~/.local/lib/x86_64-linux-gnu/qt6/plugins/kwin/effects/plugins/
if(CMAKE_LIBRARY_ARCHITECTURE)
    set(_snow_libdir "lib/${CMAKE_LIBRARY_ARCHITECTURE}")
else()
    set(_snow_libdir "lib")
endif()
set(SNOW_QT_PLUGIN_DIR "${CMAKE_INSTALL_PREFIX}/${_snow_libdir}/qt6/plugins"
    CACHE PATH "Qt plugin root the effect installs under")
set(SNOW_PLUGIN_INSTALL_DIR "${SNOW_QT_PLUGIN_DIR}/kwin/effects/plugins")

# The sibling namespace System Settings resolves an effect's
# X-KDE-ConfigModule against, giving:
#   ~/.local/lib/x86_64-linux-gnu/qt6/plugins/kwin/effects/configs/
set(SNOW_CONFIG_INSTALL_DIR "${SNOW_QT_PLUGIN_DIR}/kwin/effects/configs")

add_subdirectory(src)

# KWin 6.7 removed presentTime from the pre-paint virtuals. Tell the effect
# which overload is the actual virtual for this build so it can mark that one
# override while keeping source compatibility with both API generations.
if(KWin_VERSION VERSION_LESS 6.7.0)
    target_compile_definitions(snow PRIVATE SNOW_KWIN_PREPAINT_HAS_PRESENT_TIME)
endif()

# CTest defines BUILD_TESTING, defaulting to ON. Qt6::Test is only looked for
# when it is actually going to be used, so building the effect never needs it.
include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()

message(STATUS "Effect installs to ${SNOW_PLUGIN_INSTALL_DIR}")
message(STATUS "Config module installs to ${SNOW_CONFIG_INSTALL_DIR}")

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
