# core/gantt -- the Gantt chart's geometry and state, no drawing.
#
# SELF-CONTAINED.  Two modes, exactly like core/view:
#
#   * added from the top level (add_subdirectory(core/gantt)): it uses the
#     parent's core_model/core_sched targets if they exist.  core_sched provides
#     date32 and the civil conversions; gantt adds no other dependency.
#   * standalone (for verification):
#       cmake -S core/gantt -B <build> -G "Visual Studio 17 2022"
#     It then adds core/sched itself (tests off) so gantt.h's "date.h" include
#     and date32 helpers resolve without touching the top-level build.
#
# Optionally set GANTT_BUILD_TESTS=OFF to build just the library.

cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(core_gantt_standalone LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

if(NOT COMMAND gantt_set_warnings)
    function(gantt_set_warnings target)
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

# gantt.h includes core/sched/date.h, so core_sched must exist.  The top-level
# build adds core/sched before core/gantt; a standalone build adds it here.
if(NOT TARGET core_sched)
    set(CORE_SCHED_BUILD_TESTS OFF CACHE BOOL "core/sched tests (off for gantt standalone)" FORCE)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../sched"
                     "${CMAKE_CURRENT_BINARY_DIR}/sched")
endif()

add_library(core_gantt STATIC
    gantt.cpp
)
add_library(msproject::gantt ALIAS core_gantt)

target_compile_features(core_gantt PUBLIC cxx_std_20)
set_target_properties(core_gantt PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(core_gantt PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_link_libraries(core_gantt PUBLIC core_sched)
gantt_set_warnings(core_gantt)

option(GANTT_BUILD_TESTS "Build the core/gantt tests" ON)

if(GANTT_BUILD_TESTS)
    enable_testing()

    add_executable(gantt_tests "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/gantt_tests.cpp")
    target_link_libraries(gantt_tests PRIVATE core_gantt)
    target_compile_features(gantt_tests PRIVATE cxx_std_20)
    set_target_properties(gantt_tests PROPERTIES CXX_EXTENSIONS OFF)
    gantt_set_warnings(gantt_tests)

    # The holiday-probe test decodes corpus/probe/holiday1.mpp through
    # core::cal::load_project_calendar(), so it needs core_cal.  The top-level
    # build adds core/cal (and core/mpp) before core/gantt; a standalone gantt
    # tree does not, and the test is then compiled without that section (it
    # prints a SKIP line instead of failing).  The file itself is checked at
    # runtime, so a checkout without corpus/ still passes.
    if(TARGET core_cal)
        get_filename_component(GANTT_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
        set(GANTT_PROBE_DIR "${GANTT_REPO_ROOT}/corpus/probe")
        file(TO_CMAKE_PATH "${GANTT_PROBE_DIR}" GANTT_PROBE_DIR)
        target_link_libraries(gantt_tests PRIVATE core_cal)
        target_compile_definitions(gantt_tests PRIVATE
            GANTT_HAS_CAL=1
            GANTT_PROBE_DIR="${GANTT_PROBE_DIR}"
        )
    endif()

    add_test(NAME gantt COMMAND gantt_tests)
endif()
