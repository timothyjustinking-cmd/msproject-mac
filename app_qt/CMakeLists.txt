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

# WAVE 19: PrintSupport is a separate Qt module (qtbase's PrintSupport target) and
# File > Print... uses QPrinter/QPrintDialog from it.  Distro packaging note for
# the Linux CI job: Ubuntu's `qt6-base-dev` ships Qt6PrintSupport, but a minimal
# image needs `libqt6printsupport6`/the matching -dev package as well.
find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets PrintSupport)

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
    entry_columns.cpp
    ribbon_bar.cpp
    sheet_views.cpp
    timeline_strip.cpp
    dialogs.cpp
    print_page.cpp
    preferences_store.cpp
    shell_icons.cpp
    view_label.cpp
    backstage.cpp
    theme.cpp
)
target_compile_features(qt_shell PUBLIC cxx_std_20)
set_target_properties(qt_shell PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(qt_shell PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(qt_shell PUBLIC Qt6::Widgets Qt6::PrintSupport app_layout)
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

# Wave 18 Q3: the parity render.  Same window, the REFERENCE capture's own
# client size (1920x1032, docs/parity.md section 2), so all nine Entry columns
# fit and P1's harness can compare the column widths (at 900 it reports them
# N/A).  This is the image the CI artifact carries as qt-render-parity.png:
#   python scripts/parity_report.py corpus/bench/msproject-gantt.png \
#          build-qt/test-out/qt-render-parity.png
# The default window remains 900x560; --parity changes the render size only.
add_test(NAME qt_render_parity
         COMMAND ${CMAKE_COMMAND} -E env QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=1
                 $<TARGET_FILE:msproject_qt>
                 --open "${QT_TEST_DATA_DIR_CMAKE}/synthetic-base.mpp"
                 --render "${QT_TEST_OUT_DIR_CMAKE}/qt-render-parity.png"
                 --parity)
set_tests_properties(qt_render_parity PROPERTIES
                     PASS_REGULAR_EXPRESSION "rendered 1920x1032")

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

# Wave 17 Q2 / wave 18 Q3: the numeric parity gate.  Asserts the reference-
# measured geometry and typography (row/header heights, the frozen 60 px gutter,
# the nine Entry column widths, the 0.459 splitter, 24 px/day, the two timescale
# tiers), the pinned colours, the ribbon (no clipped labels) / timeline / status
# chrome, and that all nine columns fit at the reference's 1920x1032.  No PNG is
# written; the PNGs stay with the render tests above and the Q4 golden harness.
add_executable(qt_parity_tests tests/parity_test.cpp)
target_link_libraries(qt_parity_tests PRIVATE qt_shell)
qt_set_warnings(qt_parity_tests)

add_test(NAME qt_parity_geometry
         COMMAND ${CMAKE_COMMAND} -E env QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=1
                 $<TARGET_FILE:qt_parity_tests>
                 "${QT_TEST_DATA_DIR_CMAKE}/synthetic-base.mpp")

# WAVE 19: the command surface's acceptance test.  Copies the committed fixture
# into test-out/ (never opens it in place), loads the copy into the real
# MainWindow, edits a cell through SheetTableModel -> SheetModel, exercises
# Undo/Redo through the menu actions, saves through app::Document to a new file,
# reopens it and asserts the edit persisted as the writer's SNET constraint, then
# proves that a save with nothing pending is byte-identical.  Every write is
# under ${CMAKE_CURRENT_BINARY_DIR}/test-out.
add_executable(qt_save_reopen_tests tests/save_reopen_test.cpp)
target_link_libraries(qt_save_reopen_tests PRIVATE qt_shell)
qt_set_warnings(qt_save_reopen_tests)

add_test(NAME qt_save_reopen
         COMMAND ${CMAKE_COMMAND} -E env QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=1
                 $<TARGET_FILE:qt_save_reopen_tests>
                 "${QT_TEST_DATA_DIR_CMAKE}/synthetic-base.mpp"
                 "${QT_TEST_OUT_DIR_CMAKE}")

# ===========================================================================
# WAVE 20: per-screen structural gates.  The 9/9 parity number measured only
# the loaded Task Sheet's geometry; these gate the empty project, the chrome,
# each dialog and the two grid view families.  All offscreen, fixed scale.
# ===========================================================================

# The empty project at 1920x1032 AND 900x560: current-week timescale (never the
# 1984 epoch), the Entry header band, the timeline's Start/Finish block, the
# status bar's four view buttons + zoom slider/percentage, the quick-access
# toolbar, the rotated view caption, and ribbon groups/buttons with icons and
# no clipped labels.  Writes qt-empty-1920.png / qt-empty-900.png under test-out.
add_executable(qt_empty_state_tests tests/empty_state_test.cpp)
target_link_libraries(qt_empty_state_tests PRIVATE qt_shell)
qt_set_warnings(qt_empty_state_tests)

add_test(NAME qt_empty_state
         COMMAND ${CMAKE_COMMAND} -E env QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=1
                 $<TARGET_FILE:qt_empty_state_tests> "${QT_TEST_OUT_DIR_CMAKE}")

# The loaded project's chrome: document name in the title, painted row numbers,
# quick-access toolbar, status-bar icons/percentage, rotated view caption, and
# the callout hidden once a file is open.
add_executable(qt_chrome_gate_tests tests/chrome_gate_test.cpp)
target_link_libraries(qt_chrome_gate_tests PRIVATE qt_shell)
qt_set_warnings(qt_chrome_gate_tests)

add_test(NAME qt_chrome_gate
         COMMAND ${CMAKE_COMMAND} -E env QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=1
                 $<TARGET_FILE:qt_chrome_gate_tests>
                 "${QT_TEST_DATA_DIR_CMAKE}/synthetic-base.mpp"
                 "${QT_TEST_OUT_DIR_CMAKE}")

# Resource Sheet + Task/Resource Usage: gutter, 42 px header, 22 px row pitch,
# column set and measured column widths, period Day/Week/Month widths, and the
# chart pane hidden in every grid view.
add_executable(qt_view_gate_tests tests/view_gate_test.cpp)
target_link_libraries(qt_view_gate_tests PRIVATE qt_shell)
qt_set_warnings(qt_view_gate_tests)

add_test(NAME qt_view_gate
         COMMAND ${CMAKE_COMMAND} -E env QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=1
                 $<TARGET_FILE:qt_view_gate_tests>
                 "${QT_TEST_DATA_DIR_CMAKE}/synthetic-base.mpp")

# Each dialog (Preferences, Table/Date Format, Custom Formula, Task Fields,
# Predecessors, Filter): fixed size, margin/spacing contract, measured control
# geometry, the OK/result path over app/, and Preferences persistence through
# an explicit ini store under test-out (never the user's real settings).
add_executable(qt_dialog_gate_tests tests/dialog_gate_test.cpp)
target_link_libraries(qt_dialog_gate_tests PRIVATE qt_shell)
qt_set_warnings(qt_dialog_gate_tests)

add_test(NAME qt_dialog_gate
         COMMAND ${CMAKE_COMMAND} -E env QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=1
                 $<TARGET_FILE:qt_dialog_gate_tests>
                 "${QT_TEST_DATA_DIR_CMAKE}/synthetic-base.mpp"
                 "${QT_TEST_OUT_DIR_CMAKE}")

# Copy/Cut/Paste through SheetModel: menu enablement, range paste as one undo
# step, Cut as copy + a clearing transaction, single-cell fallback.
add_executable(qt_clipboard_tests tests/clipboard_test.cpp)
target_link_libraries(qt_clipboard_tests PRIVATE qt_shell)
qt_set_warnings(qt_clipboard_tests)

add_test(NAME qt_clipboard
         COMMAND ${CMAKE_COMMAND} -E env QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=1
                 $<TARGET_FILE:qt_clipboard_tests>
                 "${QT_TEST_DATA_DIR_CMAKE}/synthetic-base.mpp")

# ===========================================================================
# WAVE 21 (operator decision 7): a loaded file's own `214/CTable` column set.
#   * the committed fixture's stored table IS the Entry table, so the window must
#     still show the nine measured columns (the parity screen cannot move);
#   * a synthetic NON-Entry table is written under test-out (the fixture copy's
#     CTable records are rewritten in place) and loaded through open_file();
#   * an optional third argument reads a corpus file read-only -- its stored
#     Entry table is not the stock one.  The corpus is git-ignored, so that entry
#     is only registered when the file is present on the machine.
# ===========================================================================
add_executable(qt_stored_table_tests tests/stored_table_test.cpp)
target_link_libraries(qt_stored_table_tests PRIVATE qt_shell)
qt_set_warnings(qt_stored_table_tests)

add_test(NAME qt_stored_table
         COMMAND ${CMAKE_COMMAND} -E env QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=1
                 $<TARGET_FILE:qt_stored_table_tests>
                 "${QT_TEST_DATA_DIR_CMAKE}/synthetic-base.mpp"
                 "${QT_TEST_OUT_DIR_CMAKE}")

if(EXISTS "${QT_REPO_ROOT}/corpus/project_a.mpp")
    add_test(NAME qt_stored_table_corpus
             COMMAND ${CMAKE_COMMAND} -E env QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=1
                     $<TARGET_FILE:qt_stored_table_tests>
                     "${QT_TEST_DATA_DIR_CMAKE}/synthetic-base.mpp"
                     "${QT_TEST_OUT_DIR_CMAKE}"
                     "${QT_REPO_ROOT}/corpus/project_a.mpp")
    message(STATUS "app_qt: corpus/project_a.mpp found -- qt_stored_table_corpus enabled")
else()
    message(STATUS "app_qt: no corpus/project_a.mpp -- qt_stored_table_corpus not registered")
endif()

# ===========================================================================
# WAVE 23 (B-1/A-3): the loaded file's rows are Project's rows -- ID order with
# the outline and the project-summary row 0.  The committed fixtures run in CI;
# the corpus case pins the clean capture's first thirteen rows (ID, indent
# level, name HASH only) and is registered only where the corpus exists.
# ===========================================================================
add_executable(qt_task_order_tests tests/task_order_test.cpp)
target_link_libraries(qt_task_order_tests PRIVATE qt_shell)
qt_set_warnings(qt_task_order_tests)

add_test(NAME qt_task_order
         COMMAND ${CMAKE_COMMAND} -E env QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=1
                 $<TARGET_FILE:qt_task_order_tests>
                 "${QT_TEST_DATA_DIR_CMAKE}/wave23-outline.mpp"
                 "${QT_TEST_DATA_DIR_CMAKE}/synthetic-base.mpp"
                 "${QT_TEST_DATA_DIR_CMAKE}/project_a_preds_golden.sha256")

if(EXISTS "${QT_REPO_ROOT}/corpus/project_a.mpp")
    add_test(NAME qt_task_order_corpus
             COMMAND ${CMAKE_COMMAND} -E env QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=1
                     $<TARGET_FILE:qt_task_order_tests>
                     "${QT_TEST_DATA_DIR_CMAKE}/wave23-outline.mpp"
                     "${QT_TEST_DATA_DIR_CMAKE}/synthetic-base.mpp"
                     "${QT_TEST_DATA_DIR_CMAKE}/project_a_preds_golden.sha256"
                     "${QT_REPO_ROOT}/corpus/project_a.mpp")
endif()

# ===========================================================================
# WAVE 21 operator review: the menu bar hidden, the backstage back arrow, the
# double-click Task Information dialog, the measured bar/selection palette, the
# header caption layout and the status bar's Ready.  Offscreen; writes one
# painted header band under test-out for human review.
# ===========================================================================
add_executable(qt_ux_gate_tests tests/ux_gate_test.cpp)
target_link_libraries(qt_ux_gate_tests PRIVATE qt_shell)
qt_set_warnings(qt_ux_gate_tests)

add_test(NAME qt_ux_gate
         COMMAND ${CMAKE_COMMAND} -E env QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=1
                 $<TARGET_FILE:qt_ux_gate_tests>
                 "${QT_TEST_DATA_DIR_CMAKE}/synthetic-base.mpp"
                 "${QT_TEST_OUT_DIR_CMAKE}")

# ===========================================================================
# WAVE 22 (operator decision 1): the seven ribbon tabs carry MS Project 2021's
# measured group/button inventory.  The gate pins tab -> group -> button order
# for EVERY tab (against the local clean captures, recorded in
# docs/handoff/r6.md), proves the decision-9 enabled/tooltip rule on every
# button, the prefix/fit contract and the >> chevron at 1920 and 900, the
# seven-tab count, and that the contextual Gantt Chart Format tab is shown only
# in the Gantt view (through the real view QActions).
# ===========================================================================
add_executable(qt_ribbon_inventory_tests tests/ribbon_inventory_test.cpp)
target_link_libraries(qt_ribbon_inventory_tests PRIVATE qt_shell)
qt_set_warnings(qt_ribbon_inventory_tests)

add_test(NAME qt_ribbon_inventory
         COMMAND ${CMAKE_COMMAND} -E env QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=1
                 $<TARGET_FILE:qt_ribbon_inventory_tests>
                 "${QT_TEST_DATA_DIR_CMAKE}/synthetic-base.mpp"
                 "${CMAKE_CURRENT_SOURCE_DIR}/ribbon_pin.tsv")

# ===========================================================================
# WAVE 20 (operator decision 13): Carlito is BUNDLED on every platform.  The
# four OFL faces live in app_qt/fonts/ and are compiled into each executable as
# Qt resources, so the shell never depends on the host having the font and the
# offscreen CI runners (Qt 6 ships no fonts) measure the same text metrics as a
# desktop.  Resources are added per executable because a resource in a static
# library can be dropped by the linker.
# ===========================================================================
set(QT_CARLITO_FONT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fonts")
set(QT_CARLITO_FONTS
    "${QT_CARLITO_FONT_DIR}/Carlito-Regular.ttf"
    "${QT_CARLITO_FONT_DIR}/Carlito-Bold.ttf"
    "${QT_CARLITO_FONT_DIR}/Carlito-Italic.ttf"
    "${QT_CARLITO_FONT_DIR}/Carlito-BoldItalic.ttf")
function(qt_bundle_carlito target)
    qt_add_resources(${target} "carlito_${target}"
        PREFIX "/fonts"
        BASE "${QT_CARLITO_FONT_DIR}"
        FILES ${QT_CARLITO_FONTS})
endfunction()
qt_bundle_carlito(msproject_qt)
foreach(carlito_target
        qt_shell_tests qt_parity_tests qt_save_reopen_tests
        qt_empty_state_tests qt_chrome_gate_tests qt_view_gate_tests
        qt_dialog_gate_tests qt_clipboard_tests qt_stored_table_tests
        qt_ux_gate_tests qt_ribbon_inventory_tests)
    qt_bundle_carlito(${carlito_target})
endforeach()
