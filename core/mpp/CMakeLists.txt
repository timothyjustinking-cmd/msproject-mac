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
    # Forward slashes only: these become C string literals.
    foreach(path_var MPP_PROJECT_A_PATH MPP_PROJECT_B_PATH MPP_PROBE_DIR MPP_GOLDEN_PATH)
        file(TO_CMAKE_PATH "${${path_var}}" ${path_var})
    endforeach()

    add_executable(mpp_reader_tests "${MPP_REPO_ROOT}/tests/mpp_reader_tests.cpp")
    target_link_libraries(mpp_reader_tests PRIVATE core_mpp)
    target_compile_features(mpp_reader_tests PRIVATE cxx_std_20)
    set_target_properties(mpp_reader_tests PROPERTIES CXX_EXTENSIONS OFF)
    mpp_set_warnings(mpp_reader_tests)
    target_compile_definitions(mpp_reader_tests PRIVATE
        MPP_PROJECT_A_PATH="${MPP_PROJECT_A_PATH}"
        MPP_PROJECT_B_PATH="${MPP_PROJECT_B_PATH}"
        MPP_PROBE_DIR="${MPP_PROBE_DIR}"
        MPP_GOLDEN_PATH="${MPP_GOLDEN_PATH}"
    )

    add_test(NAME mpp_reader COMMAND mpp_reader_tests)
    set_tests_properties(mpp_reader PROPERTIES
        WORKING_DIRECTORY "${MPP_REPO_ROOT}"
    )
endif()
