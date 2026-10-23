# Top-level build for the portable core.
#
# OWNERSHIP: the build/CI stream (W8) owns this file.  Every other stream adds a
# self-contained CMakeLists.txt inside its own subdirectory (core/mpp,
# core/sched, ...) and is wired in by the "stream subdirectories" block below.
# Streams must not edit this file; if something here is wrong for a stream,
# message the coordinator.
#
# The build is also the portability check: the same sources must compile with
# MSVC on Windows, AppleClang on macOS and GCC on Linux, and `ctest` must pass
# on all three.  See docs/build.md and .github/workflows/ci.yml.

cmake_minimum_required(VERSION 3.20)

project(msproject_core VERSION 0.1.0 LANGUAGES CXX)

option(CORE_BUILD_TESTS "Build the core test executables" ON)
option(CORE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

# Defaults inherited by every target, including stream subdirectories, so that
# a subdirectory CMakeLists.txt cannot accidentally build under different rules.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# The core will be linked into a macOS dylib / framework eventually, so build
# static libraries as position independent on every platform from day one.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Called here, before add_subdirectory(), so that tests registered inside stream
# subdirectories are collected as well.
enable_testing()

# ---------------------------------------------------------------------------
# Warnings
#
# Applied directory-wide, i.e. to every target created here and in every
# subdirectory.  That is deliberate: CI passes -DCORE_WARNINGS_AS_ERRORS=ON, and
# the check is only worth something if it covers the stream subdirectories too.
# MSVC's /W4 plus /permissive- is the Windows equivalent of -Wall -Wextra
# -Wpedantic; /utf-8 makes the compiler read sources as UTF-8 regardless of the
# machine's ANSI code page (the source tree is UTF-8 with LF line endings).
# ---------------------------------------------------------------------------
if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
    if(CORE_WARNINGS_AS_ERRORS)
        add_compile_options(/WX)
    endif()
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(CORE_WARNINGS_AS_ERRORS)
        add_compile_options(-Werror)
    endif()
endif()

# Per-target form, kept for stream subdirectories that call it (the current
# top-level targets do).  The directory-wide block above already covers every
# target; calling this as well is redundant but harmless.
function(core_set_warnings target)
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

# ---------------------------------------------------------------------------
# core/model -- generated descriptor table + C++ API + C ABI
# ---------------------------------------------------------------------------
add_library(core_model STATIC
    core/model/field_table.gen.cpp
    core/model/field_table.cpp
    core/model/core_c.cpp
)
add_library(msproject::core ALIAS core_model)

target_compile_features(core_model PUBLIC cxx_std_20)
set_target_properties(core_model PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(core_model PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/core/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/core/model>
)
core_set_warnings(core_model)

find_package(Python3 COMPONENTS Interpreter QUIET)

if(Python3_Interpreter_FOUND)
    add_custom_target(regenerate_field_table
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen_field_tables.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Regenerating core/model/field_table.gen.{h,cpp} from analysis/*.tsv"
        VERBATIM
    )
endif()

# ---------------------------------------------------------------------------
# Stream subdirectories
#
# Other streams own core/mpp/ and core/sched/ and add a self-contained
# CMakeLists.txt there.  A missing subdirectory must NOT break the build: the
# streams land asynchronously, and a tree with the reader but not the scheduler
# (or neither) still has to configure, build and pass its tests.  Hence the
# if(EXISTS) guard -- add a directory to the list only when it has its own
# CMakeLists.txt and assume it may be absent at any time.
#
# These are added after core_model so that a subdirectory may link
# `core_model` (or the `msproject::core` alias) by target name.
# ---------------------------------------------------------------------------
foreach(stream_subdir mpp sched mppwrite view io cal plan sheet gantt session render usage formula props viewstate)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/core/${stream_subdir}/CMakeLists.txt")
        add_subdirectory("core/${stream_subdir}")
        message(STATUS "core/${stream_subdir}: CMakeLists.txt found, included")
    else()
        message(STATUS "core/${stream_subdir}: no CMakeLists.txt, skipped")
    endif()
endforeach()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(CORE_BUILD_TESTS)
    add_executable(core_field_table_tests tests/field_table_tests.cpp)
    target_link_libraries(core_field_table_tests PRIVATE core_model)
    target_compile_features(core_field_table_tests PRIVATE cxx_std_20)
    set_target_properties(core_field_table_tests PROPERTIES CXX_EXTENSIONS OFF)
    core_set_warnings(core_field_table_tests)

    set(CORE_METADATA_TSV "${CMAKE_CURRENT_SOURCE_DIR}/analysis/field_metadata_all.tsv")
    set(CORE_DEFINITIVE_TSV "${CMAKE_CURRENT_SOURCE_DIR}/analysis/field_definitive_map.tsv")
    configure_file(
        tests/test_paths.h.in
        "${CMAKE_CURRENT_BINARY_DIR}/generated/core_test_paths.h"
        @ONLY
    )
    target_include_directories(core_field_table_tests PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
    )

    add_test(NAME core_field_table COMMAND core_field_table_tests)
    set_tests_properties(core_field_table PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )

    # The generator check is registered on every platform so the test list does
    # not depend on which optional interpreters a machine happens to have.  The
    # interpreter is located at configure time (find_package above, which tries
    # python3 then python); when none is found the test reports SKIP and passes,
    # because a missing optional tool is an environment difference, not a red
    # build.  WORKING_DIRECTORY is set explicitly either way: the script derives
    # its paths from its own location, but a fixed cwd makes the test
    # reproducible from any build tree.
    if(Python3_Interpreter_FOUND AND EXISTS "${Python3_EXECUTABLE}")
        add_test(
            NAME core_field_table_generator_check
            COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen_field_tables.py"
                    --check
        )
        set_tests_properties(core_field_table_generator_check PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        )
    else()
        message(STATUS "Python3 not found: generator check test will report SKIP")
        add_test(
            NAME core_field_table_generator_check
            COMMAND ${CMAKE_COMMAND} -E echo
                    "SKIP: no Python 3 interpreter found; core_field_table_generator_check not run"
        )
        set_tests_properties(core_field_table_generator_check PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        )
    endif()

    # -----------------------------------------------------------------------
    # E1 (wave 17): the differential engine/backend harness.
    #
    # tests/engine_diff_tests.cpp is both the ctest self-test and the C++ half
    # of scripts/engine_diff.py (--apply / --dump).  The self-test applies a
    # built-in operation list to a COPY of tests/data/synthetic-base.mpp and
    # asserts the documented values; scripts/engine_diff.py --selftest then
    # proves the canonical dump and the field-by-field diff end to end without
    # MS Project (a self-recorded oracle passes, a tampered one fails naming
    # the field).  No COM runs here: recording an oracle is coordinator-only.
    # -----------------------------------------------------------------------
    if(TARGET core_mpp AND TARGET core_mppwrite AND TARGET core_plan AND
       TARGET core_cal)
        add_executable(engine_diff_tests tests/engine_diff_tests.cpp)
        target_link_libraries(engine_diff_tests PRIVATE
            core_mpp core_mppwrite core_plan core_cal)
        target_compile_features(engine_diff_tests PRIVATE cxx_std_20)
        set_target_properties(engine_diff_tests PROPERTIES CXX_EXTENSIONS OFF)
        core_set_warnings(engine_diff_tests)

        set(ENGINE_DIFF_TEST_DATA "${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
        set(ENGINE_DIFF_TEST_OUT "${CMAKE_CURRENT_BINARY_DIR}/engine-diff")
        file(TO_CMAKE_PATH "${ENGINE_DIFF_TEST_DATA}" ENGINE_DIFF_TEST_DATA)
        file(TO_CMAKE_PATH "${ENGINE_DIFF_TEST_OUT}" ENGINE_DIFF_TEST_OUT)
        target_compile_definitions(engine_diff_tests PRIVATE
            ENGINE_DIFF_TEST_DATA="${ENGINE_DIFF_TEST_DATA}"
            ENGINE_DIFF_TEST_OUT="${ENGINE_DIFF_TEST_OUT}"
        )

        add_test(NAME engine_diff_apply COMMAND engine_diff_tests)
        set_tests_properties(engine_diff_apply PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        )

        if(Python3_Interpreter_FOUND AND EXISTS "${Python3_EXECUTABLE}")
            add_test(NAME engine_diff_harness
                COMMAND "${Python3_EXECUTABLE}"
                        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/engine_diff.py"
                        --selftest
                        --apply-bin "$<TARGET_FILE:engine_diff_tests>"
                        --workdir "${CMAKE_CURRENT_BINARY_DIR}/engine-diff/harness"
            )
            set_tests_properties(engine_diff_harness PROPERTIES
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            )
            # Wave 23 (W23-9a, audit A-4): the scenario suite as a ctest lane,
            # with the CURRENT build tree's applier instead of the stale
            # build/e1 default.  Wave 24 (W24-7/R10/V): the lane pins the
            # FAILING SET BY NAME -- constraint-mso is deliberately LEFT
            # FAILING until a held-out Project measurement earns 8/8, and every
            # other scenario must pass.  A count-only expectation would stay
            # green if constraint-mso started passing while another scenario
            # broke.
            add_test(NAME engine_diff_suite
                COMMAND "${Python3_EXECUTABLE}"
                        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/engine_diff.py"
                        --suite --expect-fail constraint-mso
                        --apply-bin "$<TARGET_FILE:engine_diff_tests>"
                        --workdir "${CMAKE_CURRENT_BINARY_DIR}/engine-diff/suite"
            )
            set_tests_properties(engine_diff_suite PROPERTIES
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            )
        else()
            message(STATUS
                "Python3 not found: engine_diff_harness test not registered")
        endif()
    endif()
endif()

# The adversarial/fuzz lane (W57) is self-contained but wired here so the root
# configure/build can run it like any other stream: `--target fuzz_reader` etc.
if(CORE_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/CMakeLists.txt")
    add_subdirectory(tests/fuzz)

# Coordinator tools.  tools/oracle_gate writes our own 214/CTable append and a
# formula into a real .mpp so MS Project can be asked to re-save it (the wave-15
# oracle gate); it is opt-in so a plain build stays as fast as before.
option(BUILD_ORACLE_GATE "Build tools/oracle_gate (oracle gate fixture writer)" OFF)
if(BUILD_ORACLE_GATE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/oracle_gate/CMakeLists.txt")
    add_subdirectory(tools/oracle_gate)
endif()
endif()
