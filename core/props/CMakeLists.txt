# core/props -- read-only decoder for the MPP `Props` property streams.
#
# The top-level CMakeLists.txt already lists `props` in its stream-subdirectory
# foreach, so this file is picked up automatically when it exists.  It has no
# dependency on another core stream: the parser takes an already-extracted
# stream blob (114/Props, 214/Props or Props14).
#
# Standalone use for this directory alone:
#   cmake -S core/props -B build/props -G "Visual Studio 17 2022" -A x64
#   cmake --build build/props --config Release
#
# Set PROPS_BUILD_TESTS=OFF to build only the library.

cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(core_props_standalone LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

if(NOT COMMAND props_set_warnings)
    function(props_set_warnings target)
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

get_filename_component(PROPS_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

add_library(core_props STATIC
    props.cpp
)
add_library(msproject::props ALIAS core_props)

target_compile_features(core_props PUBLIC cxx_std_20)
set_target_properties(core_props PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(core_props PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
props_set_warnings(core_props)

add_executable(props_report report_main.cpp)
target_link_libraries(props_report PRIVATE core_props)
target_compile_features(props_report PRIVATE cxx_std_20)
set_target_properties(props_report PROPERTIES CXX_EXTENSIONS OFF)
props_set_warnings(props_report)

option(PROPS_BUILD_TESTS "Build the core/props tests" ON)

if(PROPS_BUILD_TESTS)
    enable_testing()

    add_executable(props_tests "${PROPS_REPO_ROOT}/tests/props_tests.cpp")
    target_link_libraries(props_tests PRIVATE core_props)
    target_compile_features(props_tests PRIVATE cxx_std_20)
    set_target_properties(props_tests PROPERTIES CXX_EXTENSIONS OFF)
    target_compile_definitions(props_tests PRIVATE
        PROPS_TEST_DATA="${PROPS_REPO_ROOT}/tests/data/synthetic-props.bin"
    )
    props_set_warnings(props_tests)

    add_test(NAME props COMMAND props_tests)
    set_tests_properties(props PROPERTIES
        WORKING_DIRECTORY "${PROPS_REPO_ROOT}"
    )
endif()
