# tools/recalc -- recalculate an .mpp with core::plan (M2).
#
# The root CMakeLists.txt does not add tools/, so core/plan/CMakeLists.txt adds
# this directory at <build>/tools/recalc.  Linking core_plan is enough: its
# PUBLIC dependencies carry core_mpp (reader.h) and core_sched (date/sched) to
# this target, which is what "ties to nothing but core_plan" means here.
#
# Standalone: `cmake -S tools/recalc -B <build>` also works; in that case this
# file adds core/plan itself (CORE_PLAN_FROM_TOOL stops the recursion).

if(NOT TARGET core_plan)
    set(CORE_PLAN_FROM_TOOL ON)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../core/plan"
                     "${CMAKE_CURRENT_BINARY_DIR}/core_plan")
endif()

add_executable(recalc main.cpp)
target_link_libraries(recalc PRIVATE core_plan core_cal)
target_compile_features(recalc PRIVATE cxx_std_20)
set_target_properties(recalc PROPERTIES CXX_EXTENSIONS OFF)

if(COMMAND core_set_warnings)
    core_set_warnings(recalc)
elseif(MSVC)
    target_compile_options(recalc PRIVATE /W4 /permissive- /utf-8)
    if(CORE_WARNINGS_AS_ERRORS)
        target_compile_options(recalc PRIVATE /WX)
    endif()
else()
    target_compile_options(recalc PRIVATE -Wall -Wextra -Wpedantic)
    if(CORE_WARNINGS_AS_ERRORS)
        target_compile_options(recalc PRIVATE -Werror)
    endif()
endif()
