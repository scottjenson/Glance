# edge-shrink for KWin (KDE Plasma's compositor): feasibility test plugin.
# Builds kwin/build/bin/kwin/plugins/edgeshrinktest.so. It is not installed:
# a nested KWin loads it from the build folder via QT_PLUGIN_PATH.
cmake_minimum_required(VERSION 3.20)
project(edgeshrink-kwin LANGUAGES CXX)

find_package(ECM REQUIRED NO_MODULE)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

find_package(Qt6 REQUIRED COMPONENTS Core Gui)
find_package(KF6CoreAddons REQUIRED)
find_package(KWin REQUIRED)

kcoreaddons_add_plugin(edgeshrinktest SOURCES main.cpp INSTALL_NAMESPACE "kwin/plugins")
target_link_libraries(edgeshrinktest PRIVATE KWin::kwin)
