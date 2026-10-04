# Glance for KWin (KDE Plasma's compositor): effect.
# Builds kwin/build/bin/kwin/effects/plugins/glance.so. `cmake --install`
# puts it in KWin's effects plugin folder (kcoreaddons_add_plugin below).
# For development it can instead be loaded from the build folder via
# QT_PLUGIN_PATH (run-nested.sh, use-in-session.sh).
cmake_minimum_required(VERSION 3.20)
project(glance LANGUAGES CXX)

# KWin's headers need C++20 or later (KWin itself is built as C++23).
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ECM 6.0 REQUIRED NO_MODULE)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets DBus Quick Test)
find_package(KF6CoreAddons REQUIRED)
find_package(KF6GlobalAccel REQUIRED)
find_package(KWin REQUIRED)

# The parts with no KWin in them (geometry.cpp), a library of their own so
# the unit tests (tests/) can use them without KWin.
add_library(glance-core STATIC geometry.cpp)
target_link_libraries(glance-core PUBLIC Qt6::Core Qt6::Gui)
target_include_directories(glance-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(glance-core PROPERTIES POSITION_INDEPENDENT_CODE ON)

kcoreaddons_add_plugin(glance SOURCES main.cpp parked.cpp INSTALL_NAMESPACE "kwin/effects/plugins")
target_link_libraries(glance PRIVATE glance-core KWin::kwin KF6::GlobalAccel)
# KWin headers missing from kwin-devel (see the note in each).
target_include_directories(glance SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/kwin-private)
# Let moc see the Q_OBJECT inside KWin's effect factory macro.
set_property(TARGET glance APPEND PROPERTY AUTOMOC_MACRO_NAMES "KWIN_EFFECT_FACTORY")

# glance-clip: the clip window app Glance starts (kwin/clip/main.cpp).
# Built next to the plugin's folder tree (kwin/build/bin/glance-clip), where
# Glance looks for it first; installed into the normal bin folder.
add_executable(glance-clip clip/main.cpp)
target_link_libraries(glance-clip PRIVATE Qt6::Widgets)
set_target_properties(glance-clip PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
install(TARGETS glance-clip ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
# Its .desktop file: KWin takes the window icon from it (hidden from menus).
install(FILES clip/org.glance.Clip.desktop DESTINATION ${KDE_INSTALL_APPDIR})

# Unit tests: `ctest --test-dir kwin/build` (no KWin needed).
enable_testing()
add_subdirectory(tests)
