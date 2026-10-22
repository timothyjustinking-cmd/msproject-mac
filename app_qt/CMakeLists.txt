# app_qt -- the three-platform shell (Qt 6 Widgets).
#
# Built by the `qt-linux` workflow (and, later, the Windows/macOS jobs).  The
# fidelity contract in docs/ui-qt-port.md is what the code and the golden-image
# test enforce: Fusion style, fixed metrics, non-native dialogs, one action table.
#
# Standalone: cmake -S app_qt -B <build> -DCMAKE_PREFIX_PATH=<Qt6>

cmake_minimum_required(VERSION 3.21)
project(msproject_qt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)

add_executable(msproject_qt main.cpp)
target_link_libraries(msproject_qt PRIVATE Qt6::Widgets)

# Warnings are errors here too: the shell must not rot on one platform only.
if(MSVC)
    target_compile_options(msproject_qt PRIVATE /W4 /permissive- /utf-8)
    if(CORE_WARNINGS_AS_ERRORS)
        target_compile_options(msproject_qt PRIVATE /WX)
    endif()
else()
    target_compile_options(msproject_qt PRIVATE -Wall -Wextra -Wpedantic)
    if(CORE_WARNINGS_AS_ERRORS)
        target_compile_options(msproject_qt PRIVATE -Werror)
    endif()
endif()

# The render smoke: offscreen, fixed size, PNG out.  The golden-image harness
# (docs/ui-qt-port.md §3) grows from this.
enable_testing()
add_test(NAME qt_render_smoke
         COMMAND ${CMAKE_COMMAND} -E env QT_QPA_PLATFORM=offscreen
                 $<TARGET_FILE:msproject_qt> --render
                 ${CMAKE_CURRENT_BINARY_DIR}/qt-render-smoke.png)
set_tests_properties(qt_render_smoke PROPERTIES
                     PASS_REGULAR_EXPRESSION "rendered 900x560")
