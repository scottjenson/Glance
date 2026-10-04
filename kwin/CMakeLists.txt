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

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets DBus Quick Test Concurrent)
find_package(KF6CoreAddons REQUIRED)
find_package(KF6GlobalAccel REQUIRED)
find_package(KWin REQUIRED)

# kwin-private/ holds KWin headers copied from this KWin version (see the
# note in each). Another Plasma release may have changed them, and a build
# against stale copies could crash KWin: stop. A bug-fix release only warns.
# After updating the copies, set this to the new version.
set(GLANCE_KWIN_PRIVATE_VERSION 6.7.5)
string(REGEX MATCH "^[0-9]+\\.[0-9]+" kwinRelease "${KWin_VERSION}")
string(REGEX MATCH "^[0-9]+\\.[0-9]+" privateRelease "${GLANCE_KWIN_PRIVATE_VERSION}")
if(NOT kwinRelease VERSION_EQUAL privateRelease)
    message(FATAL_ERROR "\n"
        "  Glance doesn't support this Plasma release yet.\n"
        "  This KWin is ${KWin_VERSION}; Glance was made for KWin ${GLANCE_KWIN_PRIVATE_VERSION}\n"
        "  (its copies of some KWin headers, kwin/kwin-private, come from that version).\n"
        "  Look for a newer Glance at https://github.com/scottjenson/Glance, or report it there.\n")
elseif(NOT KWin_VERSION VERSION_EQUAL GLANCE_KWIN_PRIVATE_VERSION)
    message(WARNING "KWin is ${KWin_VERSION}, Glance's kwin/kwin-private headers come from "
        "${GLANCE_KWIN_PRIVATE_VERSION}: a bug-fix release, most likely fine.")
endif()

# The parts with no KWin in them (geometry.cpp), a library of their own so
# the unit tests (tests/) can use them without KWin.
add_library(glance-core STATIC geometry.cpp)
target_link_libraries(glance-core PUBLIC Qt6::Core Qt6::Gui)
target_include_directories(glance-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(glance-core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# glance.cpp is the effect (the coordinator); the rest are its components.
kcoreaddons_add_plugin(glance
    SOURCES glance.cpp parked.cpp parkedinput.cpp drag.cpp keyboard.cpp alttab.cpp focusring.cpp
            previews.cpp wheel.cpp declutter.cpp clips.cpp kde.cpp
    INSTALL_NAMESPACE "kwin/effects/plugins")
target_link_libraries(glance PRIVATE glance-core KWin::kwin KF6::GlobalAccel Qt6::Concurrent)
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

# glance-check: at login, says so in a dialog when KWin didn't load Glance
# (kwin/check/glance-check.in). Built to kwin/build/bin/glance-check (which
# use-in-session.sh starts at login); installed with an autostart entry.
get_filename_component(GLANCE_REPO_DIR ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)
configure_file(check/glance-check.in ${CMAKE_BINARY_DIR}/bin/glance-check @ONLY
    FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
install(PROGRAMS ${CMAKE_BINARY_DIR}/bin/glance-check DESTINATION ${KDE_INSTALL_BINDIR})
set(GLANCE_CHECK_EXEC ${KDE_INSTALL_FULL_BINDIR}/glance-check)
configure_file(check/org.glance.Check.desktop.in ${CMAKE_BINARY_DIR}/org.glance.Check.desktop @ONLY)
install(FILES ${CMAKE_BINARY_DIR}/org.glance.Check.desktop DESTINATION ${KDE_INSTALL_AUTOSTARTDIR})

# Unit tests: `ctest --test-dir kwin/build` (no KWin needed).
enable_testing()
add_subdirectory(tests)
