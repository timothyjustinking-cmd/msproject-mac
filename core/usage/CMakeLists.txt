# core/usage -- time-phased work buckets for the usage views (W43).
#
# SELF-CONTAINED, two modes like core/cal and core/plan:
#
#   * added from the top level (add_subdirectory(core/usage)): it uses the
#     parent's core_sched/core_mpp/core_cal targets, which the root stream list
#     adds before this directory.
#   * standalone:
#       cmake -S core/usage -B <build> -G "Visual Studio 17 2022"
#     It then adds core/sched, core/mpp and core/cal itself with their tests
#     off, because usage.h includes core/sched/calendar.h and the test links
#     the reader and the calendar decoder.
#
# Builds core_usage, usage_tests and registers `usage` with ctest.  Set
# USAGE_BUILD_TESTS=OFF to build just the library.

cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(core_usage_standalone LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

get_filename_component(CORE_USAGE_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

# Local copy of the project warning policy so this directory builds the same
# way whether or not the parent defined core_set_warnings.
function(core_usage_apply_warnings target)
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

# usage.h includes core/sched/calendar.h and date.h.  The top-level build adds
# core/sched first; a standalone configure adds it here with tests off.
if(NOT TARGET core_sched)
    set(CORE_SCHED_BUILD_TESTS OFF CACHE BOOL "core/sched tests (off for usage standalone)" FORCE)
    add_subdirectory("${CORE_USAGE_REPO_ROOT}/core/sched"
                     "${CMAKE_CURRENT_BINARY_DIR}/core_sched")
endif()

add_library(core_usage STATIC
    usage.cpp
)
add_library(msproject::usage ALIAS core_usage)

target_compile_features(core_usage PUBLIC cxx_std_20)
set_target_properties(core_usage PROPERTIES
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)
target_include_directories(core_usage PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_link_libraries(core_usage PUBLIC core_sched)
core_usage_apply_warnings(core_usage)

option(USAGE_BUILD_TESTS "Build the core/usage tests" ON)

if(USAGE_BUILD_TESTS)
    enable_testing()

    # The test links the reader (usage_mpp.h) and the calendar decoder for the
    # corpus case.  The top-level build has both; a standalone configure adds
    # them here with their tests off.
    if(NOT TARGET core_mpp)
        set(MPP_BUILD_TESTS OFF CACHE BOOL "core/mpp tests (off for usage standalone)" FORCE)
        add_subdirectory("${CORE_USAGE_REPO_ROOT}/core/mpp"
                         "${CMAKE_CURRENT_BINARY_DIR}/core_mpp")
    endif()
    if(NOT TARGET core_cal)
        set(CAL_BUILD_TESTS OFF CACHE BOOL "core/cal tests (off for usage standalone)" FORCE)
        add_subdirectory("${CORE_USAGE_REPO_ROOT}/core/cal"
                         "${CMAKE_CURRENT_BINARY_DIR}/core_cal")
    endif()

    set(USAGE_PROJECT_A_PATH "${CORE_USAGE_REPO_ROOT}/corpus/project_a.mpp")
    file(TO_CMAKE_PATH "${USAGE_PROJECT_A_PATH}" USAGE_PROJECT_A_PATH)

    add_executable(usage_tests "${CORE_USAGE_REPO_ROOT}/tests/usage_tests.cpp")
    target_link_libraries(usage_tests PRIVATE core_usage core_mpp core_cal)
    target_compile_features(usage_tests PRIVATE cxx_std_20)
    set_target_properties(usage_tests PROPERTIES CXX_EXTENSIONS OFF)
    core_usage_apply_warnings(usage_tests)
    target_compile_definitions(usage_tests PRIVATE
        USAGE_PROJECT_A_PATH="${USAGE_PROJECT_A_PATH}"
    )

    add_test(NAME usage COMMAND usage_tests)
    set_tests_properties(usage PROPERTIES
        WORKING_DIRECTORY "${CORE_USAGE_REPO_ROOT}"
    )
endif()
