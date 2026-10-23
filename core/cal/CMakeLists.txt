# core/cal -- base calendars from <root>/TBkndCal (MPP14 114/, MPP12 112/),
# built into core::sched::Calendar.
#
# SELF-CONTAINED.  Two modes, exactly like core/gantt and core/view:
#
#   * added from the top level (add_subdirectory(core/cal)): it uses the
#     parent's core_sched/core_mpp targets, which the top-level stream list adds
#     before this directory.
#   * standalone (for verification):
#       cmake -S core/cal -B <build> -G "Visual Studio 17 2022"
#     It then adds core/sched and core/mpp itself (tests off), because cal.h
#     includes core/sched/calendar.h and core/mpp/cfb.h.
#
# Builds core_cal, cal_tests and registers `cal` with ctest.  Set
# CAL_BUILD_TESTS=OFF to build just the library.

cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(core_cal_standalone LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

# Local copy of the project's warning policy so the directory builds the same
# way whether or not the parent defined core_set_warnings.
if(NOT COMMAND cal_set_warnings)
    function(cal_set_warnings target)
        if(MSVC)
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
endif()

# cal.h includes core/mpp/cfb.h and core/sched/calendar.h, so both targets must
# exist.  The top-level build adds core/mpp and core/sched first; a standalone
# build adds them here with their tests off.
if(NOT TARGET core_mpp)
    set(MPP_BUILD_TESTS OFF CACHE BOOL "core/mpp tests (off for cal standalone)" FORCE)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../mpp"
                     "${CMAKE_CURRENT_BINARY_DIR}/mpp")
endif()
if(NOT TARGET core_sched)
    set(CORE_SCHED_BUILD_TESTS OFF CACHE BOOL "core/sched tests (off for cal standalone)" FORCE)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../sched"
                     "${CMAKE_CURRENT_BINARY_DIR}/sched")
endif()

add_library(core_cal STATIC
    cal.cpp
)
add_library(msproject::cal ALIAS core_cal)

target_compile_features(core_cal PUBLIC cxx_std_20)
set_target_properties(core_cal PROPERTIES
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)
target_include_directories(core_cal PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
# PUBLIC: cal.h names mpp::CompoundFile in its API, so consumers need cfb.h too.
target_link_libraries(core_cal PUBLIC core_sched core_mpp)
cal_set_warnings(core_cal)

option(CAL_BUILD_TESTS "Build the core/cal tests" ON)

if(CAL_BUILD_TESTS)
    enable_testing()

    # The corpus is git-ignored client data; the probe fixtures are synthetic
    # (built over COM by the coordinator).  Absolute paths, forward slashes, so
    # they can become C string literals.
    get_filename_component(CAL_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
    set(CAL_PROBE_DIR "${CAL_REPO_ROOT}/corpus/probe")
    set(CAL_CORPUS_DIR "${CAL_REPO_ROOT}/corpus")
    foreach(path_var CAL_PROBE_DIR CAL_CORPUS_DIR)
        file(TO_CMAKE_PATH "${${path_var}}" ${path_var})
    endforeach()

    add_executable(cal_tests "${CAL_REPO_ROOT}/tests/cal_tests.cpp")
    target_link_libraries(cal_tests PRIVATE core_cal)
    target_compile_features(cal_tests PRIVATE cxx_std_20)
    set_target_properties(cal_tests PROPERTIES CXX_EXTENSIONS OFF)
    cal_set_warnings(cal_tests)
    target_compile_definitions(cal_tests PRIVATE
        CAL_PROBE_DIR="${CAL_PROBE_DIR}"
        CAL_CORPUS_DIR="${CAL_CORPUS_DIR}"
    )

    add_test(NAME cal COMMAND cal_tests)
    set_tests_properties(cal PROPERTIES
        WORKING_DIRECTORY "${CAL_REPO_ROOT}"
    )
endif()
