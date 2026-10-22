# core/view -- tables, filters, groups, sorts as data.
#
# SELF-CONTAINED.  The top-level CMakeLists.txt is owned by W8 and wires only
# core/mpp and core/sched today; this directory therefore builds in two modes:
#
#   * standalone (for verification and for the coordinator before wiring):
#       cmake -S core/view -B <build> -G "Visual Studio 17 2022"
#     It compiles the three core/model sources itself so the descriptor table is
#     available without touching the top-level build.
#   * added with add_subdirectory(core/view): it expects the parent's
#     core_model target and adds only its own library and tests.
#
# Optionally set VIEW_BUILD_TESTS=OFF to build just the library.

cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(core_view_standalone LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

if(NOT COMMAND view_set_warnings)
    function(view_set_warnings target)
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
    view_set_warnings(core_model)
endif()

add_library(core_view STATIC
    value.cpp
    record_set.cpp
    json.cpp
    field_ref.cpp
    table.cpp
    filter.cpp
    group.cpp
    sort.cpp
    view_set.cpp
    defaults.cpp
    table_registry.cpp
)
add_library(msproject::view ALIAS core_view)

target_compile_features(core_view PUBLIC cxx_std_20)
set_target_properties(core_view PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(core_view PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_link_libraries(core_view PUBLIC core_model)
view_set_warnings(core_view)

option(VIEW_BUILD_TESTS "Build the core/view tests" ON)

if(VIEW_BUILD_TESTS)
    enable_testing()

    get_filename_component(VIEW_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
    file(TO_CMAKE_PATH "${VIEW_REPO_ROOT}" VIEW_REPO_ROOT_CMAKE)

    add_executable(view_model_tests "${VIEW_REPO_ROOT}/tests/view_model_tests.cpp")
    target_link_libraries(view_model_tests PRIVATE core_view)
    target_compile_features(view_model_tests PRIVATE cxx_std_20)
    set_target_properties(view_model_tests PROPERTIES CXX_EXTENSIONS OFF)
    view_set_warnings(view_model_tests)
    target_compile_definitions(view_model_tests PRIVATE
        VIEW_REPO_ROOT="${VIEW_REPO_ROOT_CMAKE}"
    )

    add_test(NAME view_model COMMAND view_model_tests)
    set_tests_properties(view_model PROPERTIES
        WORKING_DIRECTORY "${VIEW_REPO_ROOT}"
    )
endif()
