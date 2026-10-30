# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the inkypaint contributors
# The launcher is a separate CMake project; a source build still needs its runtime.
set(INKYPAINT_RUNTIME_FILE "" CACHE FILEPATH "Compiled scarletnx to stage beside the launcher")
set(inkypaint_stage_runtime "${INKYPAINT_RUNTIME_FILE}")
if(NOT inkypaint_stage_runtime AND NOT CMAKE_CROSSCOMPILING AND NOT WIN32)
    get_filename_component(inkypaint_repo "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
    if(APPLE)
        set(inkypaint_runtime_primary macos)
    else()
        set(inkypaint_runtime_primary linux)
    endif()
    foreach(candidate "${inkypaint_repo}/build/${inkypaint_runtime_primary}/scarletnx"
                      "${inkypaint_repo}/build/default/scarletnx" "${inkypaint_repo}/build/scarletnx")
        if(EXISTS "${candidate}")
            set(inkypaint_stage_runtime "${candidate}")
            break()
        endif()
    endforeach()
endif()
if(inkypaint_stage_runtime)
    if(NOT EXISTS "${inkypaint_stage_runtime}" OR IS_DIRECTORY "${inkypaint_stage_runtime}")
        message(FATAL_ERROR "INKYPAINT_RUNTIME_FILE is not a compiled scarletnx executable: ${inkypaint_stage_runtime}")
    endif()
    if(WIN32)
        set(inkypaint_runtime_name scarletnx.exe)
    else()
        set(inkypaint_runtime_name scarletnx)
    endif()
    add_custom_target(inkypaint-runtime-files ALL
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:inkypaint>/runtime"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${inkypaint_stage_runtime}"
            "$<TARGET_FILE_DIR:inkypaint>/runtime/${inkypaint_runtime_name}"
        VERBATIM)
    add_dependencies(inkypaint inkypaint-runtime-files)
    install(PROGRAMS "${inkypaint_stage_runtime}" DESTINATION runtime RENAME "${inkypaint_runtime_name}")
    message(STATUS "Launcher runtime: ${inkypaint_stage_runtime}")
else()
    message(STATUS "Launcher runtime not staged. Build scarletnx first, set INKYPAINT_RUNTIME_FILE, or use INKYPAINT_RUNTIME when launching.")
endif()
