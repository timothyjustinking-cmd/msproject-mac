# core/session -- the document/session model: a .mpp on disk becomes the rows,
# the sheet model, the Gantt source and a status report, with no AppKit/ObjC.
#
# SELF-CONTAINED, like core/sheet and core/cal.  The top-level CMakeLists.txt
# already has `session` in its stream-subdirectory foreach (coordinator commit
# f35a5ab), so this file is picked up automatically.  Two modes work:
#
#   * added with add_subdirectory(core/session) from the top level: core_mpp,
#     core_sched, core_cal, core_plan, core_view, core_sheet and core_gantt are
#     added before this directory, so this adds only its own library and test.
#   * standalone (verification of this directory alone):
#       cmake -S core/session -B <build> -G "Visual Studio 17 2022"
#     It then adds core/plan (which brings core/mpp, core/sched and core/cal),
#     core/sheet (which brings core/view and core/model) and core/gantt, all
#     with their own tests off.
#
# Set SESSION_BUILD_TESTS=OFF to build just the library.

cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(core_session_standalone LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

if(NOT COMMAND session_set_warnings)
    function(session_set_warnings target)
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

get_filename_component(SESSION_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

# core/view (added by core/sheet below) expects the parent build to define
# core_model.  The root build always does; a standalone session tree must build
# the descriptor table itself, exactly as core/sheet and core/view do.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND NOT TARGET core_model)
    add_library(core_model STATIC
        ${SESSION_REPO_ROOT}/core/model/field_table.gen.cpp
        ${SESSION_REPO_ROOT}/core/model/field_table.cpp
        ${SESSION_REPO_ROOT}/core/model/core_c.cpp
    )
    target_compile_features(core_model PUBLIC cxx_std_20)
    set_target_properties(core_model PROPERTIES CXX_EXTENSIONS OFF)
    target_include_directories(core_model PUBLIC
        ${SESSION_REPO_ROOT}/core/include
        ${SESSION_REPO_ROOT}/core/model
    )
    session_set_warnings(core_model)
endif()

# ---- dependencies ---------------------------------------------------------
# The root build adds every one of these before `session`; a standalone
# configure adds them here with their tests/tools off.
if(NOT TARGET core_plan)
    set(CORE_PLAN_BUILD_TESTS OFF CACHE BOOL "core/plan tests (off for session standalone)" FORCE)
    # Keep a session-only tree from also building tools/recalc.
    set(CORE_PLAN_FROM_TOOL ON)
    add_subdirectory("${SESSION_REPO_ROOT}/core/plan"
                     "${CMAKE_CURRENT_BINARY_DIR}/core_plan")
endif()
if(NOT TARGET core_sheet)
    set(SHEET_BUILD_TESTS OFF CACHE BOOL "core/sheet tests (off for session standalone)" FORCE)
    set(VIEW_BUILD_TESTS OFF CACHE BOOL "core/view tests (off for session standalone)" FORCE)
    add_subdirectory("${SESSION_REPO_ROOT}/core/sheet"
                     "${CMAKE_CURRENT_BINARY_DIR}/core_sheet")
endif()
if(NOT TARGET core_gantt)
    set(GANTT_BUILD_TESTS OFF CACHE BOOL "core/gantt tests (off for session standalone)" FORCE)
    add_subdirectory("${SESSION_REPO_ROOT}/core/gantt"
                     "${CMAKE_CURRENT_BINARY_DIR}/core_gantt")
endif()

add_library(core_session STATIC
    session.cpp
)
add_library(msproject::session ALIAS core_session)

target_compile_features(core_session PUBLIC cxx_std_20)
set_target_properties(core_session PROPERTIES
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)
target_include_directories(core_session PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
# PUBLIC: session.h exposes date32/RecordSet/SheetModel/GanttSource, and
# session.cpp consumes the reader, the calendar decoder and the plan bridge.
target_link_libraries(core_session PUBLIC core_plan core_cal core_sheet core_gantt)
session_set_warnings(core_session)

option(SESSION_BUILD_TESTS "Build the core/session tests" ON)

if(SESSION_BUILD_TESTS)
    enable_testing()

    # Absolute paths as C string literals (forward slashes) so the binary works
    # from any working directory and on every generator.  The corpus is
    # git-ignored client data; tests/data/synthetic-base.mpp is committed.
    set(SESSION_BASE_PATH "${SESSION_REPO_ROOT}/tests/data/synthetic-base.mpp")
    set(SESSION_PROJECT_A_PATH "${SESSION_REPO_ROOT}/corpus/project_a.mpp")
    set(SESSION_NOT_MPP_PATH "${SESSION_REPO_ROOT}/tests/data/README.md")
    foreach(path_var SESSION_BASE_PATH SESSION_PROJECT_A_PATH SESSION_NOT_MPP_PATH)
        file(TO_CMAKE_PATH "${${path_var}}" ${path_var})
    endforeach()

    add_executable(session_tests "${SESSION_REPO_ROOT}/tests/session_tests.cpp")
    target_link_libraries(session_tests PRIVATE core_session)
    target_compile_features(session_tests PRIVATE cxx_std_20)
    set_target_properties(session_tests PROPERTIES CXX_EXTENSIONS OFF)
    session_set_warnings(session_tests)
    target_compile_definitions(session_tests PRIVATE
        SESSION_BASE_PATH="${SESSION_BASE_PATH}"
        SESSION_PROJECT_A_PATH="${SESSION_PROJECT_A_PATH}"
        SESSION_NOT_MPP_PATH="${SESSION_NOT_MPP_PATH}"
    )

    add_test(NAME session COMMAND session_tests)
    set_tests_properties(session PROPERTIES
        WORKING_DIRECTORY "${SESSION_REPO_ROOT}"
    )
endif()
