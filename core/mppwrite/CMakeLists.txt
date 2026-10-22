# core/mppwrite -- the .mpp writer (W9).
#
# SELF-CONTAINED.  The top-level CMakeLists.txt is owned by W8 and currently
# adds only core/mpp and core/sched, so this directory is written to be usable
# two ways:
#
#   * standalone:   cmake -S core/mppwrite -B core/mppwrite/build
#   * as a subtree: add `mppwrite` to the stream-subdirectory list in the
#     top-level CMakeLists.txt and it is picked up like the other streams.
#
# Either way it depends only on the C++20 standard library: the writer must not
# link the reader, because a writer that can only be tested against our own
# reader proves less than one that also round-trips real files byte for byte.

cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(mppwrite_standalone LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    enable_testing()
    option(CORE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
endif()

add_library(core_mppwrite STATIC
    cfb_writer.cpp
    inplace.cpp
    table.cpp
    row_writer.cpp
    save.cpp
)
add_library(msproject::mppwrite ALIAS core_mppwrite)

target_compile_features(core_mppwrite PUBLIC cxx_std_20)
set_target_properties(core_mppwrite PROPERTIES
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)
target_include_directories(core_mppwrite PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

# Local copy of the project's warning policy, so this directory builds the same
# way whether or not the parent defined core_set_warnings.
if(NOT COMMAND mppwrite_set_warnings)
    function(mppwrite_set_warnings target)
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

mppwrite_set_warnings(core_mppwrite)

option(MPPWRITE_BUILD_TESTS "Build the .mpp writer tests" ON)

if(MPPWRITE_BUILD_TESTS)
    enable_testing()

    get_filename_component(MPPWRITE_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
    # A caller may override the probe directory, e.g. to point at a path that
    # does not exist and exercise the writer tests' no-corpus path on a machine
    # that has corpus/.  The default is the real probe fixture directory.
    if(NOT DEFINED MPPWRITE_PROBE_DIR)
        set(MPPWRITE_PROBE_DIR "${MPPWRITE_REPO_ROOT}/corpus/probe")
    endif()
    set(MPPWRITE_PROJECT_A_PATH "${MPPWRITE_REPO_ROOT}/corpus/project_a.mpp")
    set(MPPWRITE_PROJECT_B_PATH "${MPPWRITE_REPO_ROOT}/corpus/project_b.mpp")
    # Committed, non-client fixture (tests/data/README.md): the writer's
    # no-corpus test path needs one .mpp that exists on a clean checkout.
    set(MPPWRITE_TEST_DATA "${MPPWRITE_REPO_ROOT}/tests/data")
    # Tests never write into corpus/ (it is read-only for every stream); all
    # written fixtures go to the build tree.
    set(MPPWRITE_TEST_OUT "${CMAKE_CURRENT_BINARY_DIR}/test-out")
    foreach(path_var
            MPPWRITE_PROBE_DIR MPPWRITE_PROJECT_A_PATH MPPWRITE_PROJECT_B_PATH
            MPPWRITE_TEST_DATA MPPWRITE_TEST_OUT)
        file(TO_CMAKE_PATH "${${path_var}}" ${path_var})
    endforeach()

    function(mppwrite_add_test name source)
        add_executable(${name} "${MPPWRITE_REPO_ROOT}/tests/${source}")
        target_link_libraries(${name} PRIVATE core_mppwrite)
        target_compile_features(${name} PRIVATE cxx_std_20)
        set_target_properties(${name} PROPERTIES CXX_EXTENSIONS OFF)
        mppwrite_set_warnings(${name})
        target_compile_definitions(${name} PRIVATE
            MPPWRITE_PROBE_DIR="${MPPWRITE_PROBE_DIR}"
            MPPWRITE_PROJECT_A_PATH="${MPPWRITE_PROJECT_A_PATH}"
            MPPWRITE_PROJECT_B_PATH="${MPPWRITE_PROJECT_B_PATH}"
            MPPWRITE_TEST_DATA="${MPPWRITE_TEST_DATA}"
            MPPWRITE_TEST_OUT="${MPPWRITE_TEST_OUT}"
        )
        add_test(NAME ${name} COMMAND ${name})
        set_tests_properties(${name} PROPERTIES
            WORKING_DIRECTORY "${MPPWRITE_REPO_ROOT}"
        )
    endfunction()

    mppwrite_add_test(mppwrite_roundtrip_cfb roundtrip_cfb.cpp)
    mppwrite_add_test(mppwrite_inplace_patch inplace_patch.cpp)
    mppwrite_add_test(mppwrite_roundtrip_stream roundtrip_stream_replace.cpp)
    mppwrite_add_test(mppwrite_roundtrip_row roundtrip_row_write.cpp)
    mppwrite_add_test(mppwrite_roundtrip_authority roundtrip_authority.cpp)
    mppwrite_add_test(mppwrite_roundtrip_manual roundtrip_manual_start.cpp)
    mppwrite_add_test(mppwrite_save_flow roundtrip_save_flow.cpp)
endif()
