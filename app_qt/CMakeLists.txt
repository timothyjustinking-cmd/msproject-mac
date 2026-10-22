# app_qt -- the three-platform shell (Qt 6 Widgets).
#
# Built by the `qt-linux` workflow (and, later, the Windows/macOS jobs).  The
# fidelity contract in docs/ui-qt-port.md is what the code and the render tests
# enforce: Fusion style, fixed metrics, non-native dialogs, one action table.
#
# Standalone: cmake -S app_qt -B <build> -DCMAKE_PREFIX_PATH=<Qt6>
#
# The shell links the existing portable `app/` model library unchanged
# (docs/ui-qt-port.md section 4.1): `app/` is added here with its own headless
# tests OFF, because the Qt shell's tests are the gate for this directory and
# the app/ tests already have their own jobs.  Nothing under app/ is modified.

cmake_minimum_required(VERSION 3.21)
project(msproject_qt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(QT_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)

# The app/ and core/ targets must be configured before the warning function is
# used, because CORE_WARNINGS_AS_ERRORS is an app/ option (the coordinator's CI
# passes it on the command line either way).
if(NOT TARGET app_layout)
    set(APP_BUILD_TESTS OFF CACHE BOOL
        "app/ headless tests (off: app_qt builds app/ as a library)" FORCE)
    add_subdirectory("${QT_REPO_ROOT}/app" "${CMAKE_CURRENT_BINARY_DIR}/app-models")
endif()

# Warnings are errors here too: the shell must not rot on one platform only.
function(qt_set_warnings target)
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

# The shell as a library so the executable and the offscreen test build the
# same window, not a copy of it.  No Q_OBJECT anywhere -> no AUTOMOC.
add_library(qt_shell STATIC
    main_window.cpp
    menus.cpp
    plan_document.cpp
    sheet_table_model.cpp
    gantt_canvas.cpp
)
target_compile_features(qt_shell PUBLIC cxx_std_20)
set_target_properties(qt_shell PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(qt_shell PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(qt_shell PUBLIC Qt6::Widgets app_layout)
qt_set_warnings(qt_shell)

add_executable(msproject_qt main.cpp)
target_link_libraries(msproject_qt PRIVATE qt_shell)
qt_set_warnings(msproject_qt)

enable_testing()

set(QT_TEST_DATA_DIR "${QT_REPO_ROOT}/tests/data")
file(TO_CMAKE_PATH "${QT_TEST_DATA_DIR}" QT_TEST_DATA_DIR_CMAKE)
set(QT_TEST_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/test-out")
file(TO_CMAKE_PATH "${QT_TEST_OUT_DIR}" QT_TEST_OUT_DIR_CMAKE)
file(MAKE_DIRECTORY "${QT_TEST_OUT_DIR}")

# The render smoke: offscreen, fixed size, PNG out.  Kept from the skeleton; the
# golden-image harness (docs/ui-qt-port.md section 3) grows from this.
add_test(NAME qt_render_smoke
         COMMAND ${CMAKE_COMMAND} -E env QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=1
                 $<TARGET_FILE:msproject_qt> --render
                 ${CMAKE_CURRENT_BINARY_DIR}/qt-render-smoke.png)
set_tests_properties(qt_render_smoke PROPERTIES
                     PASS_REGULAR_EXPRESSION "rendered 900x560")

# The same render with a real plan loaded: proves --open reaches the window and
# that a document does not change the fixed window size.
add_test(NAME qt_render_document
         COMMAND ${CMAKE_COMMAND} -E env QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=1
                 $<TARGET_FILE:msproject_qt>
                 --open "${QT_TEST_DATA_DIR_CMAKE}/synthetic-base.mpp"
                 --render "${QT_TEST_OUT_DIR_CMAKE}/qt-render-document.png")
set_tests_properties(qt_render_document PROPERTIES
                     PASS_REGULAR_EXPRESSION "rendered 900x560")

# The headless shell test: loads the committed fixture, asserts the row count and
# the whole menu command list against app::menu_model, then renders and reads the
# PNG back.  The fixture is opened read-only; nothing here writes tests/data/.
add_executable(qt_shell_tests tests/shell_test.cpp)
target_link_libraries(qt_shell_tests PRIVATE qt_shell)
qt_set_warnings(qt_shell_tests)

add_test(NAME qt_shell_offscreen
         COMMAND ${CMAKE_COMMAND} -E env QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=1
                 $<TARGET_FILE:qt_shell_tests>
                 "${QT_TEST_DATA_DIR_CMAKE}/synthetic-base.mpp"
                 "${QT_TEST_OUT_DIR_CMAKE}/qt-shell-window.png")
