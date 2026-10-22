# core/formula -- custom-field registry, formula parser/evaluator, computed
# columns.
#
# SELF-CONTAINED, like core/sheet.  The top-level CMakeLists.txt is owned by the
# build stream and already has `formula` in its stream-subdirectory list (after
# `sheet`), so this file is picked up automatically when it exists:
#
#   * added with add_subdirectory(core/formula) from the top level: core_model,
#     core_view and core_sheet already exist, so only this library and its test
#     are added.
#   * standalone (for verification of this directory alone):
#       cmake -S core/formula -B <build> -G "Visual Studio 17 2022"
#     It adds core/sheet, which adds core/view and core/model -- the same
#     layering core/sheet uses for core/view.
#
# Set FORMULA_BUILD_TESTS=OFF to build just the library.

cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(core_formula_standalone LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

if(NOT COMMAND formula_set_warnings)
    function(formula_set_warnings target)
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

if(NOT TARGET core_sheet)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../sheet/CMakeLists.txt")
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../sheet"
                         "${CMAKE_CURRENT_BINARY_DIR}/core_sheet")
    else()
        message(FATAL_ERROR
            "core/formula needs core/sheet: ${CMAKE_CURRENT_SOURCE_DIR}/../sheet/CMakeLists.txt "
            "is missing")
    endif()
endif()

add_library(core_formula STATIC
    expr.cpp
    eval.cpp
    fields.cpp
    columns.cpp
)
add_library(msproject::formula ALIAS core_formula)

target_compile_features(core_formula PUBLIC cxx_std_20)
set_target_properties(core_formula PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(core_formula PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_link_libraries(core_formula PUBLIC core_sheet)
formula_set_warnings(core_formula)

option(FORMULA_BUILD_TESTS "Build the core/formula tests" ON)

if(FORMULA_BUILD_TESTS)
    enable_testing()

    get_filename_component(FORMULA_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

    add_executable(formula_tests "${FORMULA_REPO_ROOT}/tests/formula_tests.cpp")
    target_link_libraries(formula_tests PRIVATE core_formula)
    target_compile_features(formula_tests PRIVATE cxx_std_20)
    set_target_properties(formula_tests PROPERTIES CXX_EXTENSIONS OFF)
    formula_set_warnings(formula_tests)

    add_test(NAME formula COMMAND formula_tests)
    set_tests_properties(formula PROPERTIES
        WORKING_DIRECTORY "${FORMULA_REPO_ROOT}"
    )
endif()
