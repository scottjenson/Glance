# edge-shrink for KWin (KDE Plasma's compositor): feasibility test plugin.
# Builds kwin/build/bin/kwin/effects/plugins/edgeshrinktest.so. It is not installed:
# a nested KWin loads it from the build folder via QT_PLUGIN_PATH.
cmake_minimum_required(VERSION 3.20)
project(edgeshrink-kwin LANGUAGES CXX)

# KWin's headers need C++20 or later (KWin itself is built as C++23).
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ECM 6.0 REQUIRED NO_MODULE)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets DBus Quick)
find_package(KF6CoreAddons REQUIRED)
find_package(KWin REQUIRED)

kcoreaddons_add_plugin(edgeshrinktest SOURCES main.cpp INSTALL_NAMESPACE "kwin/effects/plugins")
target_link_libraries(edgeshrinktest PRIVATE KWin::kwin)
# Let moc see the Q_OBJECT inside KWin's effect factory macro.
set_property(TARGET edgeshrinktest APPEND PROPERTY AUTOMOC_MACRO_NAMES "KWIN_EFFECT_FACTORY")
