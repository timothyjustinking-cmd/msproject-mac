# core/mpp -- the .mpp reader (W6).
#
# SELF-CONTAINED.  The top-level CMakeLists.txt is owned by W8; this directory
# is added with `add_subdirectory(core/mpp)` and depends only on the C++20
# standard library (no core_model, no third-party code), so the reader can be
# linked into tools, tests and the future writer independently.
#
# It also defines the mpp_reader_tests target; see docs/handoff/reader.md.
# Configure with -DMPP_BUILD_TESTS=OFF to build just the library.

add_library(core_mpp STATIC
    cfb.cpp
    reader.cpp
)
add_library(msproject::mpp ALIAS core_mpp)

target_compile_features(core_mpp PUBLIC cxx_std_20)
set_target_properties(core_mpp PROPERTIES
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)
target_include_directories(core_mpp PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

# Local copy of the project's warning policy so this directory builds the same
# way whether or not the parent defined core_set_warnings.
if(NOT COMMAND mpp_set_warnings)
    function(mpp_set_warnings target)
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

mpp_set_warnings(core_mpp)

option(MPP_BUILD_TESTS "Build the .mpp reader tests" ON)

if(MPP_BUILD_TESTS)
    enable_testing()

    get_filename_component(MPP_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
    set(MPP_PROJECT_A_PATH "${MPP_REPO_ROOT}/corpus/project_a.mpp")
    set(MPP_PROJECT_B_PATH "${MPP_REPO_ROOT}/corpus/project_b.mpp")
    set(MPP_PROBE_DIR "${MPP_REPO_ROOT}/corpus/probe")
    # MS Project's own export (83 tasks, hashed names); committed, unlike the corpus.
    set(MPP_GOLDEN_PATH "${MPP_REPO_ROOT}/tests/data/project_a_golden.tsv")
    # Project's assignment dates, uids/dates only (W40 oracle artifact).
    set(MPP_ASSIGNMENT_GOLDEN_PATH
        "${MPP_REPO_ROOT}/tests/mpp_assignment_dates_golden.tsv")
    # Wave 23 (W23-13): the derived project-property name table (index -> name)
    # generated from the local-only decompiled binary by
    # scripts/gen_project_property_names.py.  The reader test checks its record
    # id arithmetic against this committed TSV.
    set(MPP_PROPERTY_NAMES_PATH
        "${MPP_REPO_ROOT}/analysis/project_property_names.tsv")
    # Forward slashes only: these become C string literals.
    foreach(path_var MPP_PROJECT_A_PATH MPP_PROJECT_B_PATH MPP_PROBE_DIR MPP_GOLDEN_PATH
                     MPP_ASSIGNMENT_GOLDEN_PATH MPP_PROPERTY_NAMES_PATH)
        file(TO_CMAKE_PATH "${${path_var}}" ${path_var})
    endforeach()

    add_executable(mpp_reader_tests
        "${MPP_REPO_ROOT}/tests/mpp_reader_tests.cpp"
        # G1: the MPP12 container round-trip test (test_mpp12_container) uses
        # the writer's CFB read/write model.  cfb_writer.cpp depends only on the
        # C++20 standard library, so the reader test binary stays self-contained
        # and no order-dependent link to core_mppwrite is needed.
        "${MPP_REPO_ROOT}/core/mppwrite/cfb_writer.cpp")
    target_link_libraries(mpp_reader_tests PRIVATE core_mpp)
    target_include_directories(mpp_reader_tests PRIVATE
        "${MPP_REPO_ROOT}/core/mppwrite")
    target_compile_features(mpp_reader_tests PRIVATE cxx_std_20)
    set_target_properties(mpp_reader_tests PROPERTIES CXX_EXTENSIONS OFF)
    mpp_set_warnings(mpp_reader_tests)
    target_compile_definitions(mpp_reader_tests PRIVATE
        MPP_PROJECT_A_PATH="${MPP_PROJECT_A_PATH}"
        MPP_PROJECT_B_PATH="${MPP_PROJECT_B_PATH}"
        MPP_PROBE_DIR="${MPP_PROBE_DIR}"
        MPP_GOLDEN_PATH="${MPP_GOLDEN_PATH}"
        MPP_ASSIGNMENT_GOLDEN_PATH="${MPP_ASSIGNMENT_GOLDEN_PATH}"
        MPP_PROPERTY_NAMES_PATH="${MPP_PROPERTY_NAMES_PATH}"
    )

    add_test(NAME mpp_reader COMMAND mpp_reader_tests)
    set_tests_properties(mpp_reader PROPERTIES
        WORKING_DIRECTORY "${MPP_REPO_ROOT}"
    )

    # ---- W45: a tombstoned Task row (FixedMeta entry +0 = 0x02) is not a live
    # task, even when its VarMeta group survives -- which is exactly what
    # Project's own add-then-delete file leaves behind.  The committed synthetic
    # fixture is the no-tombstone control, so a clean checkout still runs a real
    # assertion; the corpus probe files and the writer's emitted fixtures are
    # used when present and skipped when not.  Additive: this test reads files
    # and writes nothing.
    set(MPP_TEST_DATA_DIR "${MPP_REPO_ROOT}/tests/data")
    set(MPP_W45_TEST_OUT_TOP "${CMAKE_BINARY_DIR}/core/mppwrite/test-out")
    set(MPP_W45_TEST_OUT_MIRROR
        "${MPP_REPO_ROOT}/core/mppwrite/build/vs-w45/test-out")
    foreach(w45_path_var MPP_TEST_DATA_DIR MPP_W45_TEST_OUT_TOP
                          MPP_W45_TEST_OUT_MIRROR)
        file(TO_CMAKE_PATH "${${w45_path_var}}" ${w45_path_var})
    endforeach()
    add_executable(mpp_tombstone_tests "${MPP_REPO_ROOT}/tests/mpp_tombstone_tests.cpp")
    target_link_libraries(mpp_tombstone_tests PRIVATE core_mpp)
    target_compile_features(mpp_tombstone_tests PRIVATE cxx_std_20)
    set_target_properties(mpp_tombstone_tests PROPERTIES CXX_EXTENSIONS OFF)
    mpp_set_warnings(mpp_tombstone_tests)
    target_compile_definitions(mpp_tombstone_tests PRIVATE
        MPP_PROBE_DIR="${MPP_PROBE_DIR}"
        MPP_TEST_DATA_DIR="${MPP_TEST_DATA_DIR}"
        MPP_W45_TEST_OUT_TOP="${MPP_W45_TEST_OUT_TOP}"
        MPP_W45_TEST_OUT_MIRROR="${MPP_W45_TEST_OUT_MIRROR}"
    )
    add_test(NAME mpp_tombstone COMMAND mpp_tombstone_tests)
    set_tests_properties(mpp_tombstone PROPERTIES
        WORKING_DIRECTORY "${MPP_REPO_ROOT}"
    )

    # ---- W40: malformed-input coverage.  The fixtures are generated in memory,
    # so this test needs no corpus.  Each case must throw mpp::Error or produce a
    # warning -- never crash, read past a buffer, or allocate from an unchecked
    # size word.
    add_executable(mpp_error_paths "${MPP_REPO_ROOT}/tests/mpp_error_paths.cpp")
    target_link_libraries(mpp_error_paths PRIVATE core_mpp)
    target_compile_features(mpp_error_paths PRIVATE cxx_std_20)
    set_target_properties(mpp_error_paths PROPERTIES CXX_EXTENSIONS OFF)
    mpp_set_warnings(mpp_error_paths)
    add_test(NAME mpp_error_paths COMMAND mpp_error_paths)
    set_tests_properties(mpp_error_paths PROPERTIES
        WORKING_DIRECTORY "${MPP_REPO_ROOT}"
    )

    # ---- W48: the custom-field read API (CustomFieldValue, field_value.h).
    # Project's own w10-fieldvalues.mpp / w10-flag-on.mpp / w10-flag-off.mpp are
    # the measured sources and are skipped when corpus/ is absent; the field-id
    # range checks and the "no custom fields" control run from the committed
    # synthetic fixture on a clean checkout.  Additive: reads files, writes none.
    add_executable(mpp_field_value_tests
        "${MPP_REPO_ROOT}/tests/mpp_field_value_tests.cpp")
    target_link_libraries(mpp_field_value_tests PRIVATE core_mpp)
    target_compile_features(mpp_field_value_tests PRIVATE cxx_std_20)
    set_target_properties(mpp_field_value_tests PROPERTIES CXX_EXTENSIONS OFF)
    mpp_set_warnings(mpp_field_value_tests)
    target_compile_definitions(mpp_field_value_tests PRIVATE
        MPP_PROBE_DIR="${MPP_PROBE_DIR}"
        MPP_TEST_DATA_DIR="${MPP_TEST_DATA_DIR}"
    )
    add_test(NAME mpp_field_value COMMAND mpp_field_value_tests)
    set_tests_properties(mpp_field_value PROPERTIES
        WORKING_DIRECTORY "${MPP_REPO_ROOT}"
    )

    # ---- W40: performance harness on a synthetic 4,000-task programme.  It
    # links the later stream libraries (core/sched, core/plan, core/view,
    # core/io, core/sheet, core/gantt), which only the repository's top-level
    # build creates.  A standalone core/mpp configure or the reader_dump tool's
    # configure must not add it (no core/gantt include path there), so the guard
    # is "the source root is this repository root", not "this directory is not
    # the source root".
    if(CMAKE_SOURCE_DIR STREQUAL MPP_REPO_ROOT)
        add_executable(mpp_perf_large "${MPP_REPO_ROOT}/tests/mpp_perf_large.cpp")
        target_link_libraries(mpp_perf_large PRIVATE
            core_mpp core_sched core_plan core_view core_io core_sheet core_gantt)
        target_compile_features(mpp_perf_large PRIVATE cxx_std_20)
        set_target_properties(mpp_perf_large PROPERTIES CXX_EXTENSIONS OFF)
        mpp_set_warnings(mpp_perf_large)
        add_test(NAME mpp_perf_large COMMAND mpp_perf_large)
        set_tests_properties(mpp_perf_large PROPERTIES
            WORKING_DIRECTORY "${MPP_REPO_ROOT}"
        )
    endif()
endif()
