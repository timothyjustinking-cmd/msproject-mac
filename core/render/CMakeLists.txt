# core/render -- dependency-free PDF output for sheet and Gantt reports.
#
# SELF-CONTAINED, like core/sheet and core/gantt.  Two modes:
#
#   * added from the top level (add_subdirectory(core/render)): core_model,
#     core_view, core_sched, core_sheet and core_gantt already exist, so this
#     adds only its own library and test.
#   * standalone (verification of this directory alone):
#       cmake -S core/render -B <build> -G "Visual Studio 17 2022"
#     It builds core_model itself and adds view/sched/sheet/gantt as
#     subdirectories with their own tests off.
#
# No third-party library, no zlib, no image asset, no font file: the only
# dependency is the C++20 standard library plus this project's own core::view,
# core::sheet and core::gantt models.
#
# Optionally set RENDER_BUILD_TESTS=OFF to build just the library.

cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(core_render_standalone LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

if(NOT COMMAND render_set_warnings)
    function(render_set_warnings target)
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

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND NOT TARGET core_model)
    add_library(core_model STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../model/field_table.gen.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../model/field_table.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../model/core_c.cpp
    )
    target_compile_features(core_model PUBLIC cxx_std_20)
    set_target_properties(core_model PROPERTIES CXX_EXTENSIONS OFF)
    target_include_directories(core_model PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../model
    )
    render_set_warnings(core_model)
endif()

# core/render formats through core::sheet and draws core::gantt geometry, so both
# must exist.  The top-level build adds them before this directory; a standalone
# configure adds them here with their own tests disabled.
if(NOT TARGET core_view)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../view/CMakeLists.txt")
        set(VIEW_BUILD_TESTS OFF CACHE BOOL "core/view tests (off for render standalone)" FORCE)
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../view"
                         "${CMAKE_CURRENT_BINARY_DIR}/core_view")
    else()
        message(FATAL_ERROR "core/render needs core/view: ../view/CMakeLists.txt is missing")
    endif()
endif()

if(NOT TARGET core_sched)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../sched/CMakeLists.txt")
        set(CORE_SCHED_BUILD_TESTS OFF CACHE BOOL "core/sched tests (off for render standalone)" FORCE)
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../sched"
                         "${CMAKE_CURRENT_BINARY_DIR}/core_sched")
    else()
        message(FATAL_ERROR "core/render needs core/sched: ../sched/CMakeLists.txt is missing")
    endif()
endif()

if(NOT TARGET core_sheet)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../sheet/CMakeLists.txt")
        set(SHEET_BUILD_TESTS OFF CACHE BOOL "core/sheet tests (off for render standalone)" FORCE)
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../sheet"
                         "${CMAKE_CURRENT_BINARY_DIR}/core_sheet")
    else()
        message(FATAL_ERROR "core/render needs core/sheet: ../sheet/CMakeLists.txt is missing")
    endif()
endif()

if(NOT TARGET core_gantt)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../gantt/CMakeLists.txt")
        set(GANTT_BUILD_TESTS OFF CACHE BOOL "core/gantt tests (off for render standalone)" FORCE)
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../gantt"
                         "${CMAKE_CURRENT_BINARY_DIR}/core_gantt")
    else()
        message(FATAL_ERROR "core/render needs core/gantt: ../gantt/CMakeLists.txt is missing")
    endif()
endif()

add_library(core_render STATIC
    pdf_writer.cpp
    report.cpp
)
add_library(msproject::render ALIAS core_render)

target_compile_features(core_render PUBLIC cxx_std_20)
set_target_properties(core_render PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(core_render PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
# core_gantt brings core_sched; core_sheet brings core_view.
target_link_libraries(core_render PUBLIC core_sheet core_gantt)
render_set_warnings(core_render)

option(RENDER_BUILD_TESTS "Build the core/render tests" ON)

if(RENDER_BUILD_TESTS)
    enable_testing()

    get_filename_component(RENDER_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
    set(RENDER_PROJECT_A_MPP "${RENDER_REPO_ROOT}/corpus/project_a.mpp")
    file(TO_CMAKE_PATH "${RENDER_PROJECT_A_MPP}" RENDER_PROJECT_A_MPP)
    # Synthetic sample PDFs are written here (inside the build tree, never the
    # source tree) so core/render/verify_pdf.py or any PDF reader can inspect
    # them.  Corpus-derived documents are never written.
    set(RENDER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/render-output")
    file(TO_CMAKE_PATH "${RENDER_OUTPUT_DIR}" RENDER_OUTPUT_DIR)
    file(MAKE_DIRECTORY "${RENDER_OUTPUT_DIR}")

    add_executable(render_tests
        "${RENDER_REPO_ROOT}/tests/render_test_main.cpp"
        "${RENDER_REPO_ROOT}/tests/render_pdf_tests.cpp"
        "${RENDER_REPO_ROOT}/tests/render_report_tests.cpp"
    )
    target_link_libraries(render_tests PRIVATE core_render)
    target_compile_features(render_tests PRIVATE cxx_std_20)
    set_target_properties(render_tests PROPERTIES CXX_EXTENSIONS OFF)
    render_set_warnings(render_tests)
    target_compile_definitions(render_tests PRIVATE
        RENDER_PROJECT_A_MPP="${RENDER_PROJECT_A_MPP}"
        RENDER_OUTPUT_DIR="${RENDER_OUTPUT_DIR}"
    )

    # Test (e): render the real 83-task file.  The reader is another stream's
    # target; when it is part of this build the test decodes the corpus .mpp,
    # otherwise it prints SKIP and the rest of the suite still runs.  The file
    # itself is checked at run time, so a corpus-less checkout still passes.
    if(TARGET core_mpp)
        target_link_libraries(render_tests PRIVATE core_mpp)
        target_include_directories(render_tests PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../io"
            "${CMAKE_CURRENT_SOURCE_DIR}/../mpp"
        )
        target_compile_definitions(render_tests PRIVATE RENDER_HAVE_MPP_READER=1)
        message(STATUS "core/render: core_mpp found, corpus render test enabled")
    else()
        message(STATUS "core/render: no core_mpp target, corpus render test skipped")
    endif()

    add_test(NAME render COMMAND render_tests)
    set_tests_properties(render PROPERTIES
        WORKING_DIRECTORY "${RENDER_REPO_ROOT}"
    )
endif()
