# core/plan -- the bridge from the .mpp reader to the CPM engine (M2).
#
# SELF-CONTAINED.  The top-level CMakeLists.txt owns the build and adds this
# directory through its `foreach(stream_subdir ... plan ...)` guard; do not edit
# the root.  This file:
#
#   * builds core_plan from plan.cpp and links core_mpp + core_sched;
#   * builds tests/plan_recalc_tests.cpp (this stream owns tests/plan_*);
#   * adds tools/recalc at <build>/tools/recalc so the tool lands at
#     <build>/tools/recalc/<Config>/recalc(.exe) as the stream brief expects.
#
# The directory also configures standalone (`cmake -S core/plan -B <build>`),
# in which case it adds core/mpp and core/sched itself.

cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(core_plan_standalone LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

get_filename_component(CORE_PLAN_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

# Reuse the project-wide warning policy when this directory is part of the
# top-level build, and apply the identical policy when it stands alone.
function(core_plan_apply_warnings target)
    if(COMMAND core_set_warnings)
        core_set_warnings(${target})
    elseif(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
        if(CORE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(CORE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

# Dependencies.  The top-level build adds core/mpp and core/sched before this
# directory; a standalone configure has to add them itself.
if(NOT TARGET core_mpp)
    add_subdirectory("${CORE_PLAN_REPO_ROOT}/core/mpp"
                     "${CMAKE_CURRENT_BINARY_DIR}/core_mpp")
endif()
if(NOT TARGET core_sched)
    add_subdirectory("${CORE_PLAN_REPO_ROOT}/core/sched"
                     "${CMAKE_CURRENT_BINARY_DIR}/core_sched")
endif()
# The calendar decoder is the plan bridge's input for the file's own work week.
# The top-level stream list adds core/cal before this directory; a standalone
# configure adds it here with its tests off (CAL_BUILD_TESTS is a cache option,
# so a later top-level add_subdirectory(core/cal) must not happen — hence the
# order in the root foreach).
if(NOT TARGET core_cal)
    set(CAL_BUILD_TESTS OFF CACHE BOOL "core/cal tests (off for plan standalone)" FORCE)
    add_subdirectory("${CORE_PLAN_REPO_ROOT}/core/cal"
                     "${CMAKE_CURRENT_BINARY_DIR}/core_cal")
endif()

add_library(core_plan STATIC
    plan.cpp
    attribution.cpp
)
add_library(msproject::plan ALIAS core_plan)

target_compile_features(core_plan PUBLIC cxx_std_20)
set_target_properties(core_plan PROPERTIES
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)
target_include_directories(core_plan PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_link_libraries(core_plan PUBLIC core_sched core_mpp)
core_plan_apply_warnings(core_plan)

# ---------------------------------------------------------------------------
# Tests.  paths are configured as absolute C string literals (forward slashes)
# so the binary works from any working directory and on every generator.
# ---------------------------------------------------------------------------
option(CORE_PLAN_BUILD_TESTS "Build the core/plan tests" ON)

if(CORE_PLAN_BUILD_TESTS)
    enable_testing()

    set(PLAN_BASE_PATH "${CORE_PLAN_REPO_ROOT}/tests/data/synthetic-base.mpp")
    set(PLAN_PROJECT_A_PATH "${CORE_PLAN_REPO_ROOT}/corpus/project_a.mpp")
    set(PLAN_RECALC_PATH "${CORE_PLAN_REPO_ROOT}/corpus/probe/project_a-recalc.mpp")
    set(PLAN_SCHEDIN_PATH "${CORE_PLAN_REPO_ROOT}/corpus/probe/schedin.mpp")
    set(PLAN_ELAPSED_PATH "${CORE_PLAN_REPO_ROOT}/corpus/probe/w9-elapsedlag.mpp")
    set(PLAN_GOLDEN_PATH "${CORE_PLAN_REPO_ROOT}/tests/data/project_a_recalc_golden.tsv")
    foreach(path_var PLAN_BASE_PATH PLAN_PROJECT_A_PATH PLAN_RECALC_PATH PLAN_SCHEDIN_PATH PLAN_ELAPSED_PATH
                     PLAN_GOLDEN_PATH)
        file(TO_CMAKE_PATH "${${path_var}}" ${path_var})
    endforeach()

    add_executable(plan_recalc_tests "${CORE_PLAN_REPO_ROOT}/tests/plan_recalc_tests.cpp")
    target_link_libraries(plan_recalc_tests PRIVATE core_plan core_cal)
    target_compile_features(plan_recalc_tests PRIVATE cxx_std_20)
    set_target_properties(plan_recalc_tests PROPERTIES CXX_EXTENSIONS OFF)
    core_plan_apply_warnings(plan_recalc_tests)
    target_compile_definitions(plan_recalc_tests PRIVATE
        PLAN_BASE_PATH="${PLAN_BASE_PATH}"
        PLAN_PROJECT_A_PATH="${PLAN_PROJECT_A_PATH}"
        PLAN_RECALC_PATH="${PLAN_RECALC_PATH}"
        PLAN_SCHEDIN_PATH="${PLAN_SCHEDIN_PATH}"
        PLAN_ELAPSED_PATH="${PLAN_ELAPSED_PATH}"
        PLAN_GOLDEN_PATH="${PLAN_GOLDEN_PATH}"
    )

    add_test(NAME plan_recalc COMMAND plan_recalc_tests)
    set_tests_properties(plan_recalc PROPERTIES
        WORKING_DIRECTORY "${CORE_PLAN_REPO_ROOT}"
    )

    # W28: the mismatch classifier, on synthetic PlanTaskResult/StoredRow pairs.
    # It reads no corpus file and carries no pinned PROJECT_A counts.
    add_executable(plan_attribution_tests
        "${CORE_PLAN_REPO_ROOT}/tests/plan_attribution_tests.cpp")
    target_link_libraries(plan_attribution_tests PRIVATE core_plan)
    target_compile_features(plan_attribution_tests PRIVATE cxx_std_20)
    set_target_properties(plan_attribution_tests PROPERTIES CXX_EXTENSIONS OFF)
    core_plan_apply_warnings(plan_attribution_tests)

    add_test(NAME plan_attribution COMMAND plan_attribution_tests)
    set_tests_properties(plan_attribution PROPERTIES
        WORKING_DIRECTORY "${CORE_PLAN_REPO_ROOT}"
    )
endif()

# ---------------------------------------------------------------------------
# tools/recalc.  The root build does not add tools/, so this directory adds the
# tool at the exact location the brief names: <build>/tools/recalc/<Config>/.
# CORE_PLAN_FROM_TOOL guards the standalone case (tools/recalc adds this
# directory, so this directory must not add tools/recalc back).
# ---------------------------------------------------------------------------
if(NOT CORE_PLAN_FROM_TOOL AND NOT TARGET recalc AND
   EXISTS "${CORE_PLAN_REPO_ROOT}/tools/recalc/CMakeLists.txt")
    add_subdirectory("${CORE_PLAN_REPO_ROOT}/tools/recalc"
                     "${CMAKE_BINARY_DIR}/tools/recalc")
endif()
