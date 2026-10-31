# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the inkypaint contributors
#
# The inkypaint launcher for Windows (and Linux later), Qt 6 Quick: a project of its own, so the runtime's build never
# needs Qt. scripts/package-windows.sh cross-builds it from macOS or Linux with the windows preset's toolchain and
# MSYS2's Qt (scripts/windows-deps.py), QT_HOST_PATH naming a Qt of the same version for the build machine (its moc,
# rcc and QML tools). A native build needs only Qt 6.5 or later
cmake_minimum_required(VERSION 3.21)
project(inkypaint_launcher VERSION 1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
find_package(Qt6 6.5 REQUIRED COMPONENTS Gui Network Qml Quick QuickControls2)
qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(inkypaint WIN32
    main.cpp launchersettings.cpp launchersettings.h game.cpp game.h inifile.cpp inifile.h gamesettings.cpp gamesettings.h preparer.cpp preparer.h
    grid0.cpp grid0.h grid0patches.cpp grid0patches.h onlinemodel.cpp onlinemodel.h keynames.cpp keynames.h modfiles.cpp modfiles.h modsmodel.cpp modsmodel.h
    appmodel.cpp appmodel.h discordipc.h discordpresence.cpp discordpresence.h presencescene.cpp presencescene.h
    presencemodel.cpp presencemodel.h)
# Discord's rich presence: the application's ID from discord.com/developers (empty, the feature does nothing). The
# environment's INKYPAINT_DISCORD_APP_ID overrides it at run time
set(INKYPAINT_DISCORD_APP_ID "1566012059571982356" CACHE STRING "Discord application ID for the launcher's rich presence (empty: off)")
target_compile_definitions(inkypaint PRIVATE INKYPAINT_DISCORD_APP_ID="${INKYPAINT_DISCORD_APP_ID}")
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(inkypaint
    URI Inkypaint
    VERSION 1.0
    QML_FILES
        qml/Main.qml qml/Theme.qml qml/PageScroll.qml qml/Hero.qml qml/PlayPage.qml qml/FilesPage.qml
        qml/SettingsPage.qml qml/OnlinePage.qml qml/SplatMark.qml qml/GlassCard.qml qml/InkButton.qml
        qml/InkText.qml qml/InkSwitch.qml qml/InkField.qml qml/InkChoice.qml qml/StepBadge.qml qml/Banner.qml
        qml/Avatar.qml qml/InkBackdrop.qml qml/FileRow.qml qml/PlayerRow.qml qml/SignInCard.qml qml/ModsPage.qml qml/KeyCapture.qml qml/ControllerMap.qml)
# the splats (white masks, tinted where they are drawn) and the icon, from dist/launcher
set(dist "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/launcher")
set_source_files_properties("${dist}/AppIcon-1024.png" PROPERTIES QT_RESOURCE_ALIAS AppIcon.png)
qt_add_resources(inkypaint assets PREFIX /inkypaint/assets BASE "${dist}"
    FILES "${dist}/splat-burst.png" "${dist}/splat-drip.png" "${dist}/splat-streak.png" "${dist}/AppIcon-1024.png")
target_link_libraries(inkypaint PRIVATE Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick Qt6::QuickControls2)
if(WIN32)
    target_sources(inkypaint PRIVATE inkypaint.rc)
    target_link_libraries(inkypaint PRIVATE crypt32) # DPAPI, the GRID0+ password
    target_compile_definitions(inkypaint PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()
install(TARGETS inkypaint RUNTIME DESTINATION .)
include(runtime.cmake)

include(CTest)
if(BUILD_TESTING)
    add_executable(inkypaint-test-state EXCLUDE_FROM_ALL
        ../../tests/launcher_qt_state.cpp launchersettings.cpp game.cpp)
    target_include_directories(inkypaint-test-state PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(inkypaint-test-state PRIVATE Qt6::Gui)
    add_test(NAME launcher_state_relaunch COMMAND inkypaint-test-state)
endif()
if(BUILD_TESTING)
    add_executable(inkypaint-test-dialogs EXCLUDE_FROM_ALL ../../tests/launcher_qt_dialogs.cpp)
    target_link_libraries(inkypaint-test-dialogs PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
endif()

if(BUILD_TESTING)
    add_executable(inkypaint-test-graphics EXCLUDE_FROM_ALL
        ../../tests/launcher_graphics.cpp gamesettings.cpp gamesettings.h inifile.cpp keynames.cpp)
    target_include_directories(inkypaint-test-graphics PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(inkypaint-test-graphics PRIVATE Qt6::Gui Qt6::Network)
    add_test(NAME launcher_graphics_presets COMMAND inkypaint-test-graphics)
endif()

if(BUILD_TESTING)
    add_executable(inkypaint-test-switch EXCLUDE_FROM_ALL ../../tests/launcher_switch.cpp)
    target_compile_definitions(inkypaint-test-switch PRIVATE INKY_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml")
    target_link_libraries(inkypaint-test-switch PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
    add_test(NAME launcher_switch_hit_area COMMAND inkypaint-test-switch)
    set_tests_properties(launcher_switch_hit_area PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

if(BUILD_TESTING)
    add_executable(inkypaint-test-input-pages EXCLUDE_FROM_ALL
        ../../tests/launcher_input_pages.cpp gamesettings.cpp gamesettings.h inifile.cpp keynames.cpp)
    target_include_directories(inkypaint-test-input-pages PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(inkypaint-test-input-pages PRIVATE INKY_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml")
    target_link_libraries(inkypaint-test-input-pages PRIVATE Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick Qt6::QuickControls2)
    add_test(NAME launcher_input_pages COMMAND inkypaint-test-input-pages)
    set_tests_properties(launcher_input_pages PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

if(BUILD_TESTING)
    add_executable(inkypaint-test-mod-paths EXCLUDE_FROM_ALL ../../tests/launcher_mod_paths.cpp modfiles.cpp)
    target_include_directories(inkypaint-test-mod-paths PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(inkypaint-test-mod-paths PRIVATE Qt6::Core)
    add_test(NAME launcher_mod_paths COMMAND inkypaint-test-mod-paths)
endif()

if(BUILD_TESTING)
    add_executable(inkypaint-test-presence EXCLUDE_FROM_ALL ../../tests/launcher_presence.cpp presencescene.cpp)
    target_include_directories(inkypaint-test-presence PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(inkypaint-test-presence PRIVATE Qt6::Core)
    add_test(NAME launcher_presence COMMAND inkypaint-test-presence)
endif()

if(BUILD_TESTING)
    add_executable(inkypaint-test-runtime EXCLUDE_FROM_ALL ../../tests/launcher_runtime.cpp game.cpp)
    target_include_directories(inkypaint-test-runtime PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(inkypaint-test-runtime PRIVATE Qt6::Gui)
    add_test(NAME launcher_runtime_discovery COMMAND inkypaint-test-runtime)
endif()
