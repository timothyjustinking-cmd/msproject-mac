# core/sheet -- the task sheet's view model and geometry (no drawing).
#
# SELF-CONTAINED, like core/view.  The top-level CMakeLists.txt is owned by the
# build stream and already has `sheet` in its stream-subdirectory list, so this
# file is picked up automatically when it exists.  Two modes work:
#
#   * added with add_subdirectory(core/sheet) from the top level: `core_model`
#     and `core_view` already exist (view is added before sheet), so this adds
#     only its own library and test.
#   * standalone (for verification of this directory alone):
#       cmake -S core/sheet -B <build> -G "Visual Studio 17 2022"
#     It builds core_model itself and adds core/view as a subdirectory, which is
#     the same pattern core/view uses for core_model.
#
# Set SHEET_BUILD_TESTS=OFF to build just the library.

cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(core_sheet_standalone LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

if(NOT COMMAND sheet_set_warnings)
    function(sheet_set_warnings target)
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
    sheet_set_warnings(core_model)
endif()

if(NOT TARGET core_view)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../view/CMakeLists.txt")
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../view"
                         "${CMAKE_CURRENT_BINARY_DIR}/core_view")
    else()
        message(FATAL_ERROR
            "core/sheet needs core/view: ${CMAKE_CURRENT_SOURCE_DIR}/../view/CMakeLists.txt "
            "is missing")
    endif()
endif()

add_library(core_sheet STATIC
    sheet.cpp
)
add_library(msproject::sheet ALIAS core_sheet)

target_compile_features(core_sheet PUBLIC cxx_std_20)
set_target_properties(core_sheet PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(core_sheet PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_link_libraries(core_sheet PUBLIC core_view)
sheet_set_warnings(core_sheet)

option(SHEET_BUILD_TESTS "Build the core/sheet tests" ON)

if(SHEET_BUILD_TESTS)
    enable_testing()

    get_filename_component(SHEET_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

    add_executable(sheet_tests "${SHEET_REPO_ROOT}/tests/sheet_tests.cpp")
    target_link_libraries(sheet_tests PRIVATE core_sheet)
    target_compile_features(sheet_tests PRIVATE cxx_std_20)
    set_target_properties(sheet_tests PROPERTIES CXX_EXTENSIONS OFF)
    sheet_set_warnings(sheet_tests)

    add_test(NAME sheet COMMAND sheet_tests)
    set_tests_properties(sheet PROPERTIES
        WORKING_DIRECTORY "${SHEET_REPO_ROOT}"
    )
endif()
