# tests/fuzz -- wave-13 (W57) adversarial fuzzers.
#
# THREE TARGETS, ALL DETERMINISTIC:
#   fuzz_reader   seeded mutations of tests/data/synthetic-base.mpp through
#                 mpp::load_bytes (contract: mpp::Error or a self-consistent
#                 Document, never a crash/hang/overread)
#   fuzz_writer   seeded edit sequences over the public core/mppwrite APIs;
#                 every operation is proved from the bytes on disk, every refusal
#                 must leave the file untouched
#   fuzz_parsers  formula tokenize/parse/evaluate and Props blob parsing
#
# WIRING.  Two modes, like the other stream subdirectories in this tree:
#
#   * standalone (how W57 verified it):
#       cmake -S tests/fuzz -B build\vs-w57 -G "Visual Studio 17 2022" -A x64 \
#             -DCORE_WARNINGS_AS_ERRORS=ON
#     This directory builds the libraries it needs (core/mpp, core/mppwrite,
#     core/formula + core/sheet/view/model, core/props) with those
#     subdirectories' own tests switched off.
#
#   * added with add_subdirectory(tests/fuzz) from the top-level build: the
#     libraries already exist and are linked by target name.  The top-level
#     CMakeLists.txt is owned by the build stream, so W57 did not add that line;
#     see docs/handoff/w57.md for the one-line change.
#
# ctest:  ctest --test-dir build\vs-w57 -C Release --output-on-failure -R fuzz
#         (each test also runs standalone; see --help)

cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(core_fuzz_standalone LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    enable_testing()
    option(CORE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
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
endif()

if(NOT COMMAND fuzz_set_warnings)
    function(fuzz_set_warnings target)
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

get_filename_component(FUZZ_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

# Standalone verification can also build the product tests the fuzzers share a
# tree with (mpp_reader, mppwrite_roundtrip_row, formula, props), e.g. to run
# them under the same flags.  Off by default so the fuzz gate builds quickly.
option(FUZZ_PRODUCT_TESTS "Also build the linked subdirectories' own tests" OFF)

# ---------------------------------------------------------------------------
# Dependencies.  In the top-level build these targets already exist; standalone
# they are added here with their own tests off (a normal variable is enough:
# option() keeps an inherited value under CMP0077 NEW, so this does not leak
# into any other build).
# ---------------------------------------------------------------------------
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND NOT TARGET core_model)
    add_library(core_model STATIC
        "${FUZZ_REPO_ROOT}/core/model/field_table.gen.cpp"
        "${FUZZ_REPO_ROOT}/core/model/field_table.cpp"
        "${FUZZ_REPO_ROOT}/core/model/core_c.cpp"
    )
    target_compile_features(core_model PUBLIC cxx_std_20)
    set_target_properties(core_model PROPERTIES CXX_EXTENSIONS OFF)
    target_include_directories(core_model PUBLIC
        "${FUZZ_REPO_ROOT}/core/include"
        "${FUZZ_REPO_ROOT}/core/model"
    )
    fuzz_set_warnings(core_model)
endif()

if(NOT TARGET core_mpp)
    if(NOT FUZZ_PRODUCT_TESTS)
        set(MPP_BUILD_TESTS OFF)
    endif()
    add_subdirectory("${FUZZ_REPO_ROOT}/core/mpp"
                     "${CMAKE_CURRENT_BINARY_DIR}/core_mpp")
endif()

if(NOT TARGET core_mppwrite)
    if(NOT FUZZ_PRODUCT_TESTS)
        set(MPPWRITE_BUILD_TESTS OFF)
    endif()
    add_subdirectory("${FUZZ_REPO_ROOT}/core/mppwrite"
                     "${CMAKE_CURRENT_BINARY_DIR}/core_mppwrite")
endif()

if(NOT TARGET core_props)
    if(NOT FUZZ_PRODUCT_TESTS)
        set(PROPS_BUILD_TESTS OFF)
    endif()
    add_subdirectory("${FUZZ_REPO_ROOT}/core/props"
                     "${CMAKE_CURRENT_BINARY_DIR}/core_props")
endif()

if(NOT TARGET core_formula)
    if(NOT FUZZ_PRODUCT_TESTS)
        set(FORMULA_BUILD_TESTS OFF)
        set(SHEET_BUILD_TESTS OFF)
        set(VIEW_BUILD_TESTS OFF)
    endif()
    add_subdirectory("${FUZZ_REPO_ROOT}/core/formula"
                     "${CMAKE_CURRENT_BINARY_DIR}/core_formula")
endif()

# ---------------------------------------------------------------------------
# Paths injected into every fuzz target.  Forward slashes: they become C string
# literals.  FUZZ_OUT_DIR is where fuzz_writer writes its .mpp copies and where
# the watchdog progress files live; it is always inside the build tree.
# ---------------------------------------------------------------------------
set(FUZZ_TEST_DATA_DIR "${FUZZ_REPO_ROOT}/tests/data")
set(FUZZ_CORPUS_DIR "${FUZZ_REPO_ROOT}/corpus")
set(FUZZ_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/out")
foreach(path_var FUZZ_TEST_DATA_DIR FUZZ_CORPUS_DIR FUZZ_OUT_DIR)
    file(TO_CMAKE_PATH "${${path_var}}" ${path_var})
endforeach()

function(fuzz_add_target name source libs)
    add_executable(${name} "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
    target_link_libraries(${name} PRIVATE ${libs})
    target_compile_features(${name} PRIVATE cxx_std_20)
    set_target_properties(${name} PROPERTIES CXX_EXTENSIONS OFF)
    fuzz_set_warnings(${name})
    target_compile_definitions(${name} PRIVATE
        FUZZ_TEST_DATA_DIR="${FUZZ_TEST_DATA_DIR}"
        FUZZ_CORPUS_DIR="${FUZZ_CORPUS_DIR}"
        FUZZ_OUT_DIR="${FUZZ_OUT_DIR}"
    )
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        WORKING_DIRECTORY "${FUZZ_REPO_ROOT}"
        TIMEOUT 180
        LABELS fuzz
    )
endfunction()

# Link only what each target needs.  The order also keeps a generic include
# name from resolving into the wrong library: core/mppwrite has its own
# `table.h`, so it must not appear on core/formula's include path (sheet.h
# includes "table.h" from core/view).
fuzz_add_target(fuzz_reader fuzz_reader_main.cpp "core_mpp;core_mppwrite")
fuzz_add_target(fuzz_writer fuzz_writer_main.cpp "core_mpp;core_mppwrite")
fuzz_add_target(fuzz_parsers fuzz_parsers_main.cpp
                "core_formula;core_props;core_mpp")

# ---------------------------------------------------------------------------
# RECURSION-GUARD REGRESSION TESTS.
#
# History: core/formula's recursive-descent parser and evaluator had no nesting
# guard, so 256 nested IIf calls / 384 nested parentheses overflowed the stack
# (0xC00000FD on MSVC Release; 128 nested parentheses under ASan Debug).
# FIXED 2026-10-22 in core/formula, residual closed in wave 23:
# kMaxFormulaDepth = 64, enforced in both parse() (expr.cpp) and evaluate()
# (eval.cpp) with a positioned error.  These two tests pin the refusal at 256
# and 1024 levels.  The wave-23 audit (A-8) found this comment and README still
# said 160; the constant has been 64 since W57 F1 was closed, and at 64 the
# guard is inside the MSVC ASan Debug stack limit, so these tests pass both in
# Release and in build\vs-w57-asan.  Do not re-disable them.
# ---------------------------------------------------------------------------
foreach(depth 256 1024)
    add_test(NAME fuzz_deep_nesting_guard_${depth}
             COMMAND fuzz_parsers --deep-guard ${depth} --cases 1)
    set_tests_properties(fuzz_deep_nesting_guard_${depth} PROPERTIES
        TIMEOUT 120
        LABELS "fuzz;regression"
        WORKING_DIRECTORY "${FUZZ_REPO_ROOT}"
    )
endforeach()
