# tools/oracle_gate -- write our own 214/CTable append + a formula into a real
# .mpp so the coordinator can have MS Project open and re-save it (wave 15's
# oracle gate).  Standalone: `cmake -S tools/oracle_gate -B <build>`.

if(NOT TARGET core_viewstate)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../core/viewstate"
                     "${CMAKE_CURRENT_BINARY_DIR}/core_viewstate")
endif()
if(NOT TARGET core_mppwrite)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../core/mppwrite"
                     "${CMAKE_CURRENT_BINARY_DIR}/core_mppwrite")
endif()
if(NOT TARGET core_props)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../core/props"
                     "${CMAKE_CURRENT_BINARY_DIR}/core_props")
endif()

add_executable(oracle_gate main.cpp)
target_link_libraries(oracle_gate PRIVATE core_viewstate core_mppwrite core_props)
target_compile_features(oracle_gate PRIVATE cxx_std_20)
set_target_properties(oracle_gate PROPERTIES CXX_EXTENSIONS OFF)

if(COMMAND core_set_warnings)
    core_set_warnings(oracle_gate)
elseif(MSVC)
    target_compile_options(oracle_gate PRIVATE /W4 /permissive- /utf-8)
    if(CORE_WARNINGS_AS_ERRORS)
        target_compile_options(oracle_gate PRIVATE /WX)
    endif()
else()
    target_compile_options(oracle_gate PRIVATE -Wall -Wextra -Wpedantic)
    if(CORE_WARNINGS_AS_ERRORS)
        target_compile_options(oracle_gate PRIVATE -Werror)
    endif()
endif()
