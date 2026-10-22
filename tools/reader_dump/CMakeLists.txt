# tools/reader_dump -- TSV dump of the tasks in an .mpp file (W6).
#
# Self-contained: `add_subdirectory(tools/reader_dump)` after core/mpp.  If the
# parent has not added core/mpp yet, this file adds it itself.

if(NOT TARGET core_mpp)
    add_subdirectory(
        "${CMAKE_CURRENT_SOURCE_DIR}/../../core/mpp"
        "${CMAKE_CURRENT_BINARY_DIR}/core_mpp"
    )
endif()

add_executable(reader_dump main.cpp)
target_link_libraries(reader_dump PRIVATE core_mpp)
target_compile_features(reader_dump PRIVATE cxx_std_20)
set_target_properties(reader_dump PROPERTIES CXX_EXTENSIONS OFF)

if(COMMAND mpp_set_warnings)
    mpp_set_warnings(reader_dump)
elseif(MSVC)
    target_compile_options(reader_dump PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(reader_dump PRIVATE -Wall -Wextra -Wpedantic)
endif()
