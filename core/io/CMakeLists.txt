# core/io -- MSPDI XML import/export and CSV export.
#
# SELF-CONTAINED, like core/view: the top-level CMakeLists.txt is owned by W8 and
# does not add this directory yet, so standalone verification is:
#
#   cmake -S core/io -B <build> -G "Visual Studio 17 2022"
#   cmake --build <build> --config Debug
#   ctest --test-dir <build> -C Debug --output-on-failure
#
# In standalone mode it compiles core/model and adds core/view itself; when the
# coordinator wires `view io` into the top-level stream list, both targets
# already exist and only this directory's own library/tests are added.

cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(core_io_standalone LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

if(NOT COMMAND io_set_warnings)
    function(io_set_warnings target)
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
        io_set_warnings(core_model)
    endif()
    if(NOT TARGET core_view)
        add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../view view)
    endif()
endif()

add_library(core_io STATIC
    xml.cpp
    mspdi.cpp
    mspdi_writer.cpp
    csv.cpp
)
add_library(msproject::io ALIAS core_io)

target_compile_features(core_io PUBLIC cxx_std_20)
set_target_properties(core_io PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(core_io PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
# Optional bridge to the .mpp reader's model.  Present only when that stream's
# header is on disk; core/io then compiles the guarded adapter and its test.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../mpp/reader.h")
    target_include_directories(core_io PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../mpp>
    )
    message(STATUS "core/io: core/mpp/reader.h found, reader bridge enabled")
else()
    message(STATUS "core/io: no core/mpp/reader.h, reader bridge skipped")
endif()
target_link_libraries(core_io PUBLIC core_view)
io_set_warnings(core_io)

get_filename_component(IO_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

# ---------------------------------------------------------------------------
# tools/mspdi_cmp -- the independent cross-check against Project's MSPDI export.
#
# It needs both code paths (core_mpp + core_io), so it is only added when the
# reader is part of this build: the top-level build adds core/mpp before
# core/io.  A standalone core/io configure skips it; tools/mspdi_cmp can then be
# configured on its own (its CMakeLists.txt adds core/mpp and core/io itself).
# MSPDI_CMP_FROM_TOOL stops the recursion when that standalone configure adds
# core/io, which would otherwise add this directory back.
# ---------------------------------------------------------------------------
set(IO_MSPDI_CMP_DIR "${IO_REPO_ROOT}/tools/mspdi_cmp")
if(EXISTS "${IO_MSPDI_CMP_DIR}/CMakeLists.txt" AND TARGET core_mpp AND
   NOT MSPDI_CMP_FROM_TOOL)
    add_subdirectory("${IO_MSPDI_CMP_DIR}" "${CMAKE_BINARY_DIR}/tools/mspdi_cmp")
elseif(NOT MSPDI_CMP_FROM_TOOL)
    message(STATUS "core/io: mspdi_cmp tool skipped (needs the top-level build "
                   "with core/mpp, or its own standalone configure)")
endif()

option(IO_BUILD_TESTS "Build the core/io tests" ON)

if(IO_BUILD_TESTS)
    enable_testing()

    file(TO_CMAKE_PATH "${IO_REPO_ROOT}" IO_REPO_ROOT_CMAKE)
    file(TO_CMAKE_PATH "${IO_REPO_ROOT}/tests" IO_SAMPLE_DIR_CMAKE)
    file(TO_CMAKE_PATH "${IO_REPO_ROOT}/corpus/project_a.mpp"
         IO_CROSSCHECK_PROJECT_A_MPP_CMAKE)
    file(TO_CMAKE_PATH "${IO_REPO_ROOT}/corpus/probe/project_a-export.xml"
         IO_CROSSCHECK_PROJECT_A_XML_CMAKE)
    file(TO_CMAKE_PATH "${IO_REPO_ROOT}/corpus/probe/lagff1.mpp"
         IO_CROSSCHECK_LAG_MPP_CMAKE)
    file(TO_CMAKE_PATH "${IO_REPO_ROOT}/corpus/probe/lagff1.xml"
         IO_CROSSCHECK_LAG_XML_CMAKE)
    file(TO_CMAKE_PATH "${IO_REPO_ROOT}/corpus/project_b.mpp"
         IO_CROSSCHECK_PROJECT_B_MPP_CMAKE)
    file(TO_CMAKE_PATH "${IO_REPO_ROOT}/corpus/probe/project_b-export.xml"
         IO_CROSSCHECK_PROJECT_B_XML_CMAKE)
    file(TO_CMAKE_PATH "${IO_REPO_ROOT}/corpus/probe/assign1.mpp"
         IO_CROSSCHECK_ASSIGN_MPP_CMAKE)
    file(TO_CMAKE_PATH "${IO_REPO_ROOT}/corpus/probe/assign1.xml"
         IO_CROSSCHECK_ASSIGN_XML_CMAKE)

    add_executable(io_mspdi_tests "${IO_REPO_ROOT}/tests/io_mspdi_tests.cpp")
    target_link_libraries(io_mspdi_tests PRIVATE core_io)
    target_compile_features(io_mspdi_tests PRIVATE cxx_std_20)
    set_target_properties(io_mspdi_tests PROPERTIES CXX_EXTENSIONS OFF)
    io_set_warnings(io_mspdi_tests)
    target_compile_definitions(io_mspdi_tests PRIVATE
        IO_SAMPLE_DIR="${IO_SAMPLE_DIR_CMAKE}"
    )

    add_executable(io_csv_tests "${IO_REPO_ROOT}/tests/io_csv_tests.cpp")
    target_link_libraries(io_csv_tests PRIVATE core_io)
    target_compile_features(io_csv_tests PRIVATE cxx_std_20)
    set_target_properties(io_csv_tests PROPERTIES CXX_EXTENSIONS OFF)
    io_set_warnings(io_csv_tests)

    # W40: malformed MSPDI inputs.  No fixture files: every case is a string.
    add_executable(io_error_paths "${IO_REPO_ROOT}/tests/io_error_paths.cpp")
    target_link_libraries(io_error_paths PRIVATE core_io)
    target_compile_features(io_error_paths PRIVATE cxx_std_20)
    set_target_properties(io_error_paths PROPERTIES CXX_EXTENSIONS OFF)
    io_set_warnings(io_error_paths)
    add_test(NAME io_error_paths COMMAND io_error_paths)
    set_tests_properties(io_error_paths PROPERTIES WORKING_DIRECTORY "${IO_REPO_ROOT}")

    add_executable(io_mspdi_writer_tests "${IO_REPO_ROOT}/tests/io_mspdi_writer_tests.cpp")
    target_link_libraries(io_mspdi_writer_tests PRIVATE core_io)
    target_compile_features(io_mspdi_writer_tests PRIVATE cxx_std_20)
    set_target_properties(io_mspdi_writer_tests PROPERTIES CXX_EXTENSIONS OFF)
    io_set_warnings(io_mspdi_writer_tests)
    target_compile_definitions(io_mspdi_writer_tests PRIVATE
        IO_SAMPLE_DIR="${IO_SAMPLE_DIR_CMAKE}"
    )

    add_test(NAME io_mspdi COMMAND io_mspdi_tests)
    set_tests_properties(io_mspdi PROPERTIES WORKING_DIRECTORY "${IO_REPO_ROOT}")
    add_test(NAME io_csv COMMAND io_csv_tests)
    set_tests_properties(io_csv PROPERTIES WORKING_DIRECTORY "${IO_REPO_ROOT}")
    add_test(NAME io_mspdi_writer COMMAND io_mspdi_writer_tests)
    set_tests_properties(io_mspdi_writer PROPERTIES WORKING_DIRECTORY "${IO_REPO_ROOT}")

    # Cross-check through the other code path.  Needs the tool's comparison
    # library, which exists only when the top-level build included core_mpp.
    # Corpus-gated at run time: a missing corpus/ prints SKIP and passes.
    if(TARGET mspdi_crosscheck)
        add_executable(io_mspdi_crosscheck "${IO_REPO_ROOT}/tests/io_mspdi_crosscheck.cpp")
        target_link_libraries(io_mspdi_crosscheck PRIVATE mspdi_crosscheck)
        target_compile_features(io_mspdi_crosscheck PRIVATE cxx_std_20)
        set_target_properties(io_mspdi_crosscheck PROPERTIES CXX_EXTENSIONS OFF)
        io_set_warnings(io_mspdi_crosscheck)
        target_compile_definitions(io_mspdi_crosscheck PRIVATE
            IO_CROSSCHECK_PROJECT_A_MPP="${IO_CROSSCHECK_PROJECT_A_MPP_CMAKE}"
            IO_CROSSCHECK_PROJECT_A_XML="${IO_CROSSCHECK_PROJECT_A_XML_CMAKE}"
            IO_CROSSCHECK_LAG_MPP="${IO_CROSSCHECK_LAG_MPP_CMAKE}"
            IO_CROSSCHECK_LAG_XML="${IO_CROSSCHECK_LAG_XML_CMAKE}"
            IO_CROSSCHECK_PROJECT_B_MPP="${IO_CROSSCHECK_PROJECT_B_MPP_CMAKE}"
            IO_CROSSCHECK_PROJECT_B_XML="${IO_CROSSCHECK_PROJECT_B_XML_CMAKE}"
            IO_CROSSCHECK_ASSIGN_MPP="${IO_CROSSCHECK_ASSIGN_MPP_CMAKE}"
            IO_CROSSCHECK_ASSIGN_XML="${IO_CROSSCHECK_ASSIGN_XML_CMAKE}"
        )
        add_test(NAME io_mspdi_crosscheck COMMAND io_mspdi_crosscheck)
        set_tests_properties(io_mspdi_crosscheck PROPERTIES
            WORKING_DIRECTORY "${IO_REPO_ROOT}")
    else()
        message(STATUS "core/io: io_mspdi_crosscheck test skipped (no mspdi_crosscheck "
                       "target: core_mpp is not part of this build)")
    endif()
endif()
