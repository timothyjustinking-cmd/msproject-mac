# core/viewstate -- read-only decoder for the MPP `214/` view-state streams.
#
# The top-level CMakeLists.txt already lists `viewstate` in its
# stream-subdirectory foreach, so this file is picked up automatically when it
# exists.  It links core/props (found earlier in the same foreach list) because
# `214/CEdl`'s block payload and `214/Props` are Props-format documents.
#
# Standalone use for this directory alone:
#   cmake -S core/viewstate -B build/viewstate -G "Visual Studio 17 2022" -A x64
#   cmake --build build/viewstate --config Release
#
# Set VIEWSTATE_BUILD_TESTS=OFF to build only the library and the report CLI.

cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(core_viewstate_standalone LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

if(NOT COMMAND viewstate_set_warnings)
    function(viewstate_set_warnings target)
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

get_filename_component(VIEWSTATE_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

# Top-level builds add core/props first.  A standalone configure of this
# directory pulls it in so the target exists.
if(NOT TARGET core_props)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../props"
                     "${CMAKE_CURRENT_BINARY_DIR}/props")
endif()

add_library(core_viewstate STATIC
    viewstate.cpp
)
add_library(msproject::viewstate ALIAS core_viewstate)

target_compile_features(core_viewstate PUBLIC cxx_std_20)
set_target_properties(core_viewstate PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(core_viewstate PUBLIC core_props)
target_include_directories(core_viewstate PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
viewstate_set_warnings(core_viewstate)

add_executable(viewstate_report report_main.cpp)
target_link_libraries(viewstate_report PRIVATE core_viewstate)
target_compile_features(viewstate_report PRIVATE cxx_std_20)
set_target_properties(viewstate_report PROPERTIES CXX_EXTENSIONS OFF)
viewstate_set_warnings(viewstate_report)

option(VIEWSTATE_BUILD_TESTS "Build the core/viewstate tests" ON)

if(VIEWSTATE_BUILD_TESTS)
    enable_testing()

    add_executable(viewstate_tests "${VIEWSTATE_REPO_ROOT}/tests/viewstate_tests.cpp")
    target_link_libraries(viewstate_tests PRIVATE core_viewstate)
    target_compile_features(viewstate_tests PRIVATE cxx_std_20)
    set_target_properties(viewstate_tests PROPERTIES CXX_EXTENSIONS OFF)
    target_compile_definitions(viewstate_tests PRIVATE
        VIEWSTATE_TEST_DATA="${VIEWSTATE_REPO_ROOT}/tests/data"
    )
    viewstate_set_warnings(viewstate_tests)

    add_test(NAME viewstate COMMAND viewstate_tests)
    set_tests_properties(viewstate PROPERTIES
        WORKING_DIRECTORY "${VIEWSTATE_REPO_ROOT}"
    )
endif()
