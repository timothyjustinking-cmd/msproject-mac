# tools/mspdi_cmp -- cross-check the .mpp reader against an MSPDI export.
#
# The tool needs both code paths: core_mpp (binary reader) and core_io (MSPDI
# importer).  In the main build core/io/CMakeLists.txt adds this directory,
# because core/io owns the MSPDI path and core/mpp is already a target by then.
# Standalone (for a fast loop while editing this tool):
#
#   cmake -S tools/mspdi_cmp -B <build> -G "Visual Studio 17 2022" -A x64
#   cmake --build <build> --config Release --target mspdi_cmp
#
# It defines the targets it needs (core_model, core_view, core_mpp, core_io)
# the same way those directories' own standalone configures do.
#
# crosscheck.cpp is a static library so tests/io_mspdi_crosscheck.cpp runs
# exactly the comparison code the tool runs.

cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(mspdi_cmp_standalone LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

if(NOT COMMAND mspdi_cmp_set_warnings)
    function(mspdi_cmp_set_warnings target)
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

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    if(NOT TARGET core_model)
        add_library(core_model STATIC
            ${CMAKE_CURRENT_SOURCE_DIR}/../../core/model/field_table.gen.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/../../core/model/field_table.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/../../core/model/core_c.cpp
        )
        target_compile_features(core_model PUBLIC cxx_std_20)
        set_target_properties(core_model PROPERTIES CXX_EXTENSIONS OFF)
        target_include_directories(core_model PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/../../core/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../../core/model
        )
        mspdi_cmp_set_warnings(core_model)
    endif()
    if(NOT TARGET core_view)
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../core/view"
                         "${CMAKE_CURRENT_BINARY_DIR}/core_view")
    endif()
    if(NOT TARGET core_mpp)
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../core/mpp"
                         "${CMAKE_CURRENT_BINARY_DIR}/core_mpp")
    endif()
    if(NOT TARGET core_io)
        # Stop core/io from adding this directory back (recursive add_subdirectory).
        set(MSPDI_CMP_FROM_TOOL ON)
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../core/io"
                         "${CMAKE_CURRENT_BINARY_DIR}/core_io")
        unset(MSPDI_CMP_FROM_TOOL)
    endif()
endif()

add_library(mspdi_crosscheck STATIC crosscheck.cpp)
target_compile_features(mspdi_crosscheck PUBLIC cxx_std_20)
set_target_properties(mspdi_crosscheck PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(mspdi_crosscheck PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_link_libraries(mspdi_crosscheck PUBLIC core_io core_mpp)
mspdi_cmp_set_warnings(mspdi_crosscheck)

add_executable(mspdi_cmp main.cpp)
target_compile_features(mspdi_cmp PRIVATE cxx_std_20)
set_target_properties(mspdi_cmp PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(mspdi_cmp PRIVATE mspdi_crosscheck)
mspdi_cmp_set_warnings(mspdi_cmp)
