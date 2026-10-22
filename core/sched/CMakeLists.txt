# core/sched -- date32, duration ticks, and the working-time calendar.
#
# Self-contained subdirectory: add it with
#     add_subdirectory(core/sched)
# from the project root.  This file deliberately does not touch the top-level
# CMakeLists.txt (the coordinator owns that); it only assumes the tests live in
# <root>/tests/sched_*.cpp, which is this stream's lane.
cmake_minimum_required(VERSION 3.20)

option(CORE_SCHED_BUILD_TESTS "Build the core/sched tests" ON)

add_library(core_sched STATIC
    date.cpp
    duration.cpp
    calendar.cpp
    cpm.cpp
    units.cpp
    rollup.cpp
)
add_library(core::sched ALIAS core_sched)

target_compile_features(core_sched PUBLIC cxx_std_20)
set_target_properties(core_sched PROPERTIES
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)
target_include_directories(core_sched PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

# Reuse the top-level warning helper when this directory is added to that build;
# otherwise apply the same policy locally so the directory also stands alone.
if(COMMAND core_set_warnings)
    set(CORE_SCHED_HAVE_WARNING_HELPER ON)
else()
    set(CORE_SCHED_HAVE_WARNING_HELPER OFF)
    function(core_sched_set_warnings target)
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

function(core_sched_apply_warnings target)
    if(CORE_SCHED_HAVE_WARNING_HELPER)
        core_set_warnings(${target})
    else()
        core_sched_set_warnings(${target})
    endif()
endfunction()

core_sched_apply_warnings(core_sched)

if(CORE_SCHED_BUILD_TESTS)
    # tests/ lives at the repository root: this stream owns tests/sched_*.
    set(CORE_SCHED_TESTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../tests")
    foreach(module IN ITEMS date duration calendar cpm units rollup)
        set(test_target "core_sched_${module}_tests")
        add_executable(${test_target}
            "${CORE_SCHED_TESTS_DIR}/sched_${module}_tests.cpp"
        )
        target_link_libraries(${test_target} PRIVATE core_sched)
        target_compile_features(${test_target} PRIVATE cxx_std_20)
        set_target_properties(${test_target} PROPERTIES CXX_EXTENSIONS OFF)
        core_sched_apply_warnings(${test_target})
        # add_test() is only collected by ctest when the including project has
        # called enable_testing(); the top-level CMakeLists.txt does that under
        # CORE_BUILD_TESTS, and any other parent must do the same.
        add_test(NAME core_sched_${module} COMMAND ${test_target})
    endforeach()
endif()
