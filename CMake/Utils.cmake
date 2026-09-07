# SPDX-FileCopyrightText: 2026 Arman2122
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

cmake_minimum_required(VERSION 3.20)

macro(get_all_targets_recursive targets dir)
    get_property(subdirectories DIRECTORY ${dir} PROPERTY SUBDIRECTORIES)
    foreach(subdir ${subdirectories})
        get_all_targets_recursive(${targets} ${subdir})
    endforeach()

    get_property(current_targets DIRECTORY ${dir} PROPERTY BUILDSYSTEM_TARGETS)
    list(APPEND ${targets} ${current_targets})
endmacro()

function(get_all_targets var)
    set(targets)
    get_all_targets_recursive(targets ${CMAKE_CURRENT_SOURCE_DIR})
    set(${var} ${targets} PARENT_SCOPE)
endfunction()

# See https://github.com/boostorg/stacktrace/issues/55#issuecomment-984782636
if (MSVC)
    function(apply_pdbaltpath_pdb_for_all_targets)
        get_all_targets(ALL_TARGETS)
        foreach(TARGET_NAME ${ALL_TARGETS})
            get_target_property(OUTPUT_NAME ${TARGET_NAME} OUTPUT_NAME)
            if ("${OUTPUT_NAME}" STREQUAL "OUTPUT_NAME-NOTFOUND" OR "${OUTPUT_NAME}" STREQUAL "")
                set(OUTPUT_NAME ${TARGET_NAME})
            endif()
            set_target_properties(${TARGET_NAME} PROPERTIES LINK_FLAGS "/PDBALTPATH:${OUTPUT_NAME}.pdb")
        endforeach()
    endfunction()
endif()
