# app/ -- the macOS shell (v4): a native AppKit application over the portable
# core -- menu bar, Preferences panel, filter dialog, PDF export, printing (wave
# 14), dirty prompts, resource/usage views and computed columns.
#
# STANDALONE PROJECT.  This file is NOT added to the repository's top-level
# CMakeLists.txt (that file is owned by the build stream) and it does not edit
# any core/ CMakeLists.txt.  It adds the core stream subdirectories it needs with
# their own tests turned OFF, so one app build compiles the libraries the shell
# actually links and nothing else.
#
#   macOS:   cmake -S app -B build-app -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
#            cmake --build build-app
#            ctest --test-dir build-app -C Release
#            -> msproject_app.app (main.mm: NSApplication + PlanView + menus +
#               Preferences + export + print + `--smoke`) and the fifteen headless
#               tests
#
#   Windows: cmake -S app -B build\vs-w47 -G "Visual Studio 17 2022" -A x64
#            cmake --build build\vs-w47 --config Release
#            ctest --test-dir build\vs-w47 -C Release
#            -> layout_model_tests.exe, selection_model_tests.exe,
#               document_tests.exe, prefs_tests.exe, menu_tests.exe,
#               filter_tests.exe, export_tests.exe, views_tests.exe,
#               app_edit_tests.exe, app_fields_tests.exe,
#               app_edits_fields_tests.exe, app_edits_links_tests.exe,
#               page_layout_tests.exe, start_edit_warning_tests.exe,
#               smoke_tests.exe; ObjC++/AppKit does not exist here, so the AppKit
#               target is skipped and the headless tests are the gate.
#
#   Linux:   same as Windows (headless tests only).
#
# Options:
#   APP_BUILD_TESTS         build + register the fifteen headless tests (default ON)
#   CORE_WARNINGS_AS_ERRORS same meaning as the top-level build (default OFF;
#                           the macOS workflow passes ON).  It also reaches the
#                           core subdirectories, which read this variable.
#
# See app/README.md for what the window does and does not do.

cmake_minimum_required(VERSION 3.20)

project(msproject_app VERSION 0.4.0 LANGUAGES CXX)

option(APP_BUILD_TESTS "Build the headless app tests" ON)
option(CORE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

enable_testing()

get_filename_component(APP_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

# ---------------------------------------------------------------------------
# Warning policy (the app's own targets; the core subdirectories have their own
# copy of the same policy and read CORE_WARNINGS_AS_ERRORS from here).
# ---------------------------------------------------------------------------
function(app_set_warnings target)
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

# ---------------------------------------------------------------------------
# core/model -- it has no CMakeLists.txt of its own (the top-level build defines
# it), so this project defines it the same way core/view's standalone mode does.
# Every target below links it by name.
# ---------------------------------------------------------------------------
if(NOT TARGET core_model)
    add_library(core_model STATIC
        "${APP_REPO_ROOT}/core/model/field_table.gen.cpp"
        "${APP_REPO_ROOT}/core/model/field_table.cpp"
        "${APP_REPO_ROOT}/core/model/core_c.cpp"
    )
    add_library(msproject::core ALIAS core_model)
    target_compile_features(core_model PUBLIC cxx_std_20)
    set_target_properties(core_model PROPERTIES CXX_EXTENSIONS OFF)
    target_include_directories(core_model PUBLIC
        "${APP_REPO_ROOT}/core/include"
        "${APP_REPO_ROOT}/core/model"
    )
    app_set_warnings(core_model)
endif()

# ---------------------------------------------------------------------------
# The core libraries the shell links, with every stream test switched off.  The
# options are cache entries because the stream subdirectories read them; FORCE
# beats their own `option()` defaults inside a single app configure.
# ---------------------------------------------------------------------------
set(MPP_BUILD_TESTS OFF CACHE BOOL "core/mpp tests (off: app configure)" FORCE)
set(CORE_SCHED_BUILD_TESTS OFF CACHE BOOL "core/sched tests (off: app configure)" FORCE)
set(VIEW_BUILD_TESTS OFF CACHE BOOL "core/view tests (off: app configure)" FORCE)
set(SHEET_BUILD_TESTS OFF CACHE BOOL "core/sheet tests (off: app configure)" FORCE)
set(GANTT_BUILD_TESTS OFF CACHE BOOL "core/gantt tests (off: app configure)" FORCE)
# core/render (W39) is what File > Export PDF writes through.  Its test binary
# and the corpus-render fixture are stream tests, so they stay off here.
set(RENDER_BUILD_TESTS OFF CACHE BOOL "core/render tests (off: app configure)" FORCE)
# Wave 11: the resource sheet, the usage grids and the computed columns link
# core/view's resource tables and W43/W44's usage/formula libraries.
set(USAGE_BUILD_TESTS OFF CACHE BOOL "core/usage tests (off: app configure)" FORCE)
set(FORMULA_BUILD_TESTS OFF CACHE BOOL "core/formula tests (off: app configure)" FORCE)
# Not added below, but pinned OFF so a future edit that adds them cannot drag a
# stream test (or the mspdi_cmp tool chain) into the app build.
set(CORE_PLAN_BUILD_TESTS OFF CACHE BOOL "core/plan tests (off: app configure)" FORCE)
set(CAL_BUILD_TESTS OFF CACHE BOOL "core/cal tests (off: app configure)" FORCE)
set(IO_BUILD_TESTS OFF CACHE BOOL "core/io tests (off: app configure)" FORCE)
set(MPPWRITE_BUILD_TESTS OFF CACHE BOOL "core/mppwrite tests (off: app configure)" FORCE)
# Wave 12: core/props decodes 114/Props -- the stored formula text the task sheet
# shows as computed columns.  Its test binary is a stream test, so it stays off.
set(PROPS_BUILD_TESTS OFF CACHE BOOL "core/props tests (off: app configure)" FORCE)
set(VIEWSTATE_BUILD_TESTS OFF CACHE BOOL "core/viewstate tests (off: app configure)" FORCE)

# Order matters: view needs model, sheet needs view, gantt needs sched, render
# needs sheet + gantt, usage needs sched, formula needs sheet.  props (W50) has no
# core dependency of its own: it parses an already-extracted stream blob.
foreach(app_core_dep mpp sched view sheet gantt usage formula mppwrite render props viewstate)
    if(NOT EXISTS "${APP_REPO_ROOT}/core/${app_core_dep}/CMakeLists.txt")
        message(FATAL_ERROR
            "app: core/${app_core_dep}/CMakeLists.txt is missing. The shell links the "
            "portable core directly; a clean checkout of the repository is required.")
    endif()
    add_subdirectory("${APP_REPO_ROOT}/core/${app_core_dep}"
                     "${CMAKE_CURRENT_BINARY_DIR}/core/${app_core_dep}")
endforeach()

# ---------------------------------------------------------------------------
# Which save path app/document.cpp compiles (see that file's header comment).
# Stream W37 owns core/mppwrite/save.h; it is not on disk yet, so the default
# body is pin_task_start + CfbDocument::save.  When the header lands this flips
# to the single mppwrite::save_date_edits call -- on the next configure, and the
# status line says which one ran.  Nothing else in app/ changes.
# ---------------------------------------------------------------------------
if(EXISTS "${APP_REPO_ROOT}/core/mppwrite/save.h")
    set(APP_MPPWRITE_SAVE_HEADER 1)
    message(STATUS "app: core/mppwrite/save.h found -- saves use mppwrite::save_date_edits")
else()
    set(APP_MPPWRITE_SAVE_HEADER 0)
    message(STATUS
        "app: core/mppwrite/save.h absent -- saves use pin_task_start + CfbDocument::save")
endif()

# ---------------------------------------------------------------------------
# app_layout -- the portable half of the shell: date32 conversions, RecordSet
# mapping, sheet row strings, GanttSource/GanttStyle, the sheet<->Gantt selection
# mapping (selection_model), the save helper (document), the preferences
# (prefs), the menu-bar data (menu_model), the filter dialog model
# (filter_model), the PDF export (export_pdf) and the wave-11 views
# (view_model: resource sheet, task/resource usage, computed columns), plus the
# wave-15 self-smoke sequence (smoke).  No AppKit, so it builds and is tested on
# every platform.
# ---------------------------------------------------------------------------
add_library(app_layout STATIC
    layout_model.cpp
    selection_model.cpp
    document.cpp
    prefs.cpp
    menu_model.cpp
    filter_model.cpp
    export_pdf.cpp
    view_model.cpp
    fields_model.cpp
    edit_model.cpp
    field_link_edits.cpp
    page_layout.cpp
    start_edit_warning.cpp
    smoke.cpp
)
add_library(msproject::app_layout ALIAS app_layout)
target_compile_features(app_layout PUBLIC cxx_std_20)
set_target_properties(app_layout PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(app_layout PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
# core_mpp is needed by reader_adapter.h's include of "reader.h"; the layout
# model itself never includes it.  core_mppwrite is the save path in
# document.cpp; core_render is the PDF writer behind export_pdf.cpp; core_usage
# and core_formula are the wave-11 views (view_model.cpp) and also carry
# core/usage's own use of core_sched.
target_link_libraries(app_layout PUBLIC core_sheet core_gantt core_mpp core_mppwrite
                                     core_render core_usage core_formula core_props
                                     core_viewstate)
# PUBLIC so the tests see the same branch of app/document.cpp the shell compiles.
target_compile_definitions(app_layout PUBLIC
    APP_MPPWRITE_SAVE_HEADER=${APP_MPPWRITE_SAVE_HEADER}
)
app_set_warnings(app_layout)

# ---------------------------------------------------------------------------
# Headless tests -- the Windows/Linux gate and the macOS CI test step.
# ---------------------------------------------------------------------------
if(APP_BUILD_TESTS)
    # Overridable so a machine can prove the no-corpus path (a CI runner) without
    # touching corpus/: -DAPP_CORPUS_PROBE_DIR=<a path that does not exist>.
    set(APP_CORPUS_PROBE_DIR "${APP_REPO_ROOT}/corpus/probe" CACHE PATH
        "corpus/probe directory for the headless probes (may be absent)")
    file(TO_CMAKE_PATH "${APP_CORPUS_PROBE_DIR}" APP_CORPUS_PROBE_DIR_CMAKE)

    # The committed synthetic fixture (tests/data/README.md) is what the save
    # round trip reads; written files go under the build tree, never into
    # tests/data/ or corpus/.
    set(APP_TEST_DATA_DIR "${APP_REPO_ROOT}/tests/data")
    file(TO_CMAKE_PATH "${APP_TEST_DATA_DIR}" APP_TEST_DATA_DIR_CMAKE)
    set(APP_TEST_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/test-out")
    file(TO_CMAKE_PATH "${APP_TEST_OUT_DIR}" APP_TEST_OUT_DIR_CMAKE)
    file(MAKE_DIRECTORY "${APP_TEST_OUT_DIR}")
    # Wave 15 (W63): app_smoke's own directory, so a smoke run's artifacts (the
    # fixture copy, the edited file, smoke.json) are never mixed with the other
    # tests' outputs and can be reviewed or deleted as a set.
    set(APP_SMOKE_OUT_DIR "${APP_TEST_OUT_DIR}/smoke")
    file(TO_CMAKE_PATH "${APP_SMOKE_OUT_DIR}" APP_SMOKE_OUT_DIR_CMAKE)

    add_executable(layout_model_tests layout_model_tests.cpp)
    target_compile_features(layout_model_tests PRIVATE cxx_std_20)
    set_target_properties(layout_model_tests PROPERTIES CXX_EXTENSIONS OFF)
    target_link_libraries(layout_model_tests PRIVATE app_layout)
    target_compile_definitions(layout_model_tests PRIVATE
        APP_CORPUS_PROBE_DIR="${APP_CORPUS_PROBE_DIR_CMAKE}"
    )
    app_set_warnings(layout_model_tests)

    add_test(NAME app_layout COMMAND layout_model_tests)
    set_tests_properties(app_layout PROPERTIES WORKING_DIRECTORY "${APP_REPO_ROOT}")

    # The sheet <-> Gantt selection mapping (row ordinals, collapse, hit testing,
    # scroll targets).  Synthetic rows only: no fixture, no corpus.
    add_executable(selection_model_tests selection_model_tests.cpp)
    target_compile_features(selection_model_tests PRIVATE cxx_std_20)
    set_target_properties(selection_model_tests PROPERTIES CXX_EXTENSIONS OFF)
    target_link_libraries(selection_model_tests PRIVATE app_layout)
    app_set_warnings(selection_model_tests)

    add_test(NAME app_selection COMMAND selection_model_tests)
    set_tests_properties(app_selection PROPERTIES WORKING_DIRECTORY "${APP_REPO_ROOT}")

    # The save helper: diff the RecordSet against the loaded plan, write the
    # edited starts through core/mppwrite, reload and check.  Runs on the
    # committed synthetic fixture and writes only under APP_TEST_OUT_DIR.
    add_executable(document_tests document_tests.cpp)
    target_compile_features(document_tests PRIVATE cxx_std_20)
    set_target_properties(document_tests PROPERTIES CXX_EXTENSIONS OFF)
    target_link_libraries(document_tests PRIVATE app_layout)
    target_compile_definitions(document_tests PRIVATE
        APP_TEST_DATA_DIR="${APP_TEST_DATA_DIR_CMAKE}"
        APP_TEST_OUT_DIR="${APP_TEST_OUT_DIR_CMAKE}"
    )
    app_set_warnings(document_tests)

    add_test(NAME app_document COMMAND document_tests)
    set_tests_properties(app_document PROPERTIES WORKING_DIRECTORY "${APP_REPO_ROOT}")

    # The preferences: identifiers, the tolerant persisted-string parser, the
    # date/duration renderings, and cell_text() choosing the app rendering.
    # Synthetic rows only.
    add_executable(prefs_tests prefs_tests.cpp)
    target_compile_features(prefs_tests PRIVATE cxx_std_20)
    set_target_properties(prefs_tests PROPERTIES CXX_EXTENSIONS OFF)
    target_link_libraries(prefs_tests PRIVATE app_layout)
    app_set_warnings(prefs_tests)

    add_test(NAME app_prefs COMMAND prefs_tests)
    set_tests_properties(app_prefs PROPERTIES WORKING_DIRECTORY "${APP_REPO_ROOT}")

    # The menu bar as data: every menu and action present, key equivalents
    # unique per menu, and the unsupported items permanently greyed out.
    add_executable(menu_tests menu_tests.cpp)
    target_compile_features(menu_tests PRIVATE cxx_std_20)
    set_target_properties(menu_tests PROPERTIES CXX_EXTENSIONS OFF)
    target_link_libraries(menu_tests PRIVATE app_layout)
    app_set_warnings(menu_tests)

    add_test(NAME app_menu COMMAND menu_tests)
    set_tests_properties(app_menu PROPERTIES WORKING_DIRECTORY "${APP_REPO_ROOT}")

    # The filter dialog model: field/operator/value -> FilterDefinition, error
    # paths, matching rows, the row-list restriction the shell draws through,
    # and the filtered RecordSet used for print.
    add_executable(filter_tests filter_tests.cpp)
    target_compile_features(filter_tests PRIVATE cxx_std_20)
    set_target_properties(filter_tests PROPERTIES CXX_EXTENSIONS OFF)
    target_link_libraries(filter_tests PRIVATE app_layout)
    app_set_warnings(filter_tests)

    add_test(NAME app_filter COMMAND filter_tests)
    set_tests_properties(app_filter PROPERTIES WORKING_DIRECTORY "${APP_REPO_ROOT}")

    # The PDF export: a real PDF written under APP_TEST_OUT_DIR from synthetic
    # rows, sheet and Gantt, filtered and unfiltered, plus the shading-off
    # calendar and the name/subtitle/footer helpers.
    add_executable(export_tests export_tests.cpp)
    target_compile_features(export_tests PRIVATE cxx_std_20)
    set_target_properties(export_tests PROPERTIES CXX_EXTENSIONS OFF)
    target_link_libraries(export_tests PRIVATE app_layout)
    target_compile_definitions(export_tests PRIVATE
        APP_TEST_OUT_DIR="${APP_TEST_OUT_DIR_CMAKE}"
    )
    app_set_warnings(export_tests)

    add_test(NAME app_export COMMAND export_tests)
    set_tests_properties(app_export PROPERTIES WORKING_DIRECTORY "${APP_REPO_ROOT}")

    # Wave 11: the resource sheet's rows/fields, the usage buckets (day/week),
    # the computed formula columns and their read-only rule, the view-mode and
    # preference round trip, and the view-mode menu gating.  Synthetic rows only.
    add_executable(views_tests views_tests.cpp)
    target_compile_features(views_tests PRIVATE cxx_std_20)
    set_target_properties(views_tests PROPERTIES CXX_EXTENSIONS OFF)
    target_link_libraries(views_tests PRIVATE app_layout)
    app_set_warnings(views_tests)

    add_test(NAME app_views COMMAND views_tests)
    set_tests_properties(app_views PROPERTIES WORKING_DIRECTORY "${APP_REPO_ROOT}")

    # Wave 12: Insert/Delete Task.  The pending-edit model, its undo/redo, the
    # display plan, and the real writer sequence on a copy of the committed
    # synthetic fixture under APP_TEST_OUT_DIR (counts, uids, tombstoned
    # dependants, refusal with no target).  Never writes tests/data or corpus/.
    add_executable(app_edit_tests app_edit_tests.cpp)
    target_compile_features(app_edit_tests PRIVATE cxx_std_20)
    set_target_properties(app_edit_tests PROPERTIES CXX_EXTENSIONS OFF)
    target_link_libraries(app_edit_tests PRIVATE app_layout)
    target_compile_definitions(app_edit_tests PRIVATE
        APP_TEST_DATA_DIR="${APP_TEST_DATA_DIR_CMAKE}"
        APP_TEST_OUT_DIR="${APP_TEST_OUT_DIR_CMAKE}"
    )
    app_set_warnings(app_edit_tests)

    add_test(NAME app_edit COMMAND app_edit_tests)
    set_tests_properties(app_edit PROPERTIES WORKING_DIRECTORY "${APP_REPO_ROOT}")

    # Wave 12: stored custom-field values and the file's own formulas.  Reads the
    # W48 storage values from a synthetic row set, the column set/formatting, and
    # the committed 114/Props blob (tests/data/synthetic-props.bin) through
    # core::props -> core::formula.
    add_executable(app_fields_tests app_fields_tests.cpp)
    target_compile_features(app_fields_tests PRIVATE cxx_std_20)
    set_target_properties(app_fields_tests PROPERTIES CXX_EXTENSIONS OFF)
    target_link_libraries(app_fields_tests PRIVATE app_layout)
    target_compile_definitions(app_fields_tests PRIVATE
        APP_TEST_DATA_DIR="${APP_TEST_DATA_DIR_CMAKE}"
    )
    app_set_warnings(app_fields_tests)

    add_test(NAME app_fields COMMAND app_fields_tests)
    set_tests_properties(app_fields PROPERTIES WORKING_DIRECTORY "${APP_REPO_ROOT}")

    # Wave 13: writing custom fields through the Task Fields dialog model plus the
    # real writer round trip (save_field_values / clear_field_value) on a copy of
    # the committed synthetic fixture.  Writes only under APP_TEST_OUT_DIR.
    add_executable(app_edits_fields_tests app_edits_fields_tests.cpp)
    target_compile_features(app_edits_fields_tests PRIVATE cxx_std_20)
    set_target_properties(app_edits_fields_tests PROPERTIES CXX_EXTENSIONS OFF)
    target_link_libraries(app_edits_fields_tests PRIVATE app_layout)
    target_compile_definitions(app_edits_fields_tests PRIVATE
        APP_TEST_DATA_DIR="${APP_TEST_DATA_DIR_CMAKE}"
        APP_TEST_OUT_DIR="${APP_TEST_OUT_DIR_CMAKE}"
    )
    app_set_warnings(app_edits_fields_tests)

    add_test(NAME app_edits_fields COMMAND app_edits_fields_tests)
    set_tests_properties(app_edits_fields PROPERTIES WORKING_DIRECTORY "${APP_REPO_ROOT}")

    # Wave 13: link editing through the Predecessors dialog model plus the real
    # writer round trip (add_task_link / delete_task_link) on a copy of the
    # committed synthetic fixture.  Writes only under APP_TEST_OUT_DIR.
    add_executable(app_edits_links_tests app_edits_links_tests.cpp)
    target_compile_features(app_edits_links_tests PRIVATE cxx_std_20)
    set_target_properties(app_edits_links_tests PROPERTIES CXX_EXTENSIONS OFF)
    target_link_libraries(app_edits_links_tests PRIVATE app_layout)
    target_compile_definitions(app_edits_links_tests PRIVATE
        APP_TEST_DATA_DIR="${APP_TEST_DATA_DIR_CMAKE}"
        APP_TEST_OUT_DIR="${APP_TEST_OUT_DIR_CMAKE}"
    )
    app_set_warnings(app_edits_links_tests)

    add_test(NAME app_edits_links COMMAND app_edits_links_tests)
    set_tests_properties(app_edits_links PROPERTIES WORKING_DIRECTORY "${APP_REPO_ROOT}")

    # Wave 14: printing.  The page arithmetic (rows per page for a 3-row and an
    # 83-row plan, page count, header/footer text) and the seam: the printer and
    # the PDF routes produce identical pages, and core/render's Export PDF page
    # count agrees with the same layout.  Writes its two PDFs under
    # APP_TEST_OUT_DIR, never into tests/data/ or corpus/.
    add_executable(page_layout_tests page_layout_tests.cpp)
    target_compile_features(page_layout_tests PRIVATE cxx_std_20)
    set_target_properties(page_layout_tests PROPERTIES CXX_EXTENSIONS OFF)
    target_link_libraries(page_layout_tests PRIVATE app_layout)
    target_compile_definitions(page_layout_tests PRIVATE
        APP_TEST_OUT_DIR="${APP_TEST_OUT_DIR_CMAKE}"
    )
    app_set_warnings(page_layout_tests)

    add_test(NAME app_print COMMAND page_layout_tests)
    set_tests_properties(app_print PROPERTIES WORKING_DIRECTORY "${APP_REPO_ROOT}")

    # Wave 14: the start-edit warning (linked pin clamp / manual reschedule /
    # no effect).  Pure PlanInput rows, no fixture and no writer.
    add_executable(start_edit_warning_tests start_edit_warning_tests.cpp)
    target_compile_features(start_edit_warning_tests PRIVATE cxx_std_20)
    set_target_properties(start_edit_warning_tests PROPERTIES CXX_EXTENSIONS OFF)
    target_link_libraries(start_edit_warning_tests PRIVATE app_layout)
    app_set_warnings(start_edit_warning_tests)

    add_test(NAME app_start_warning COMMAND start_edit_warning_tests)
    set_tests_properties(app_start_warning PROPERTIES WORKING_DIRECTORY "${APP_REPO_ROOT}")

    # Wave 15 (W63): the portable half of `msproject_app --smoke`.  Loads a copy of
    # the committed synthetic fixture (never the fixture itself), drives the filter
    # dialog model, the Task Fields / Predecessors dialog models, a Start edit
    # through core::sheet, the save-time warning, the real writer, a reopen and the
    # shared print layout, then writes smoke.json under APP_SMOKE_OUT_DIR.  The
    # AppKit half (window, print-to-PDF, window snapshot) is app/main.mm and is
    # compiled only by the macOS workflow; this test is what proves the sequence on
    # the development box.
    add_executable(smoke_tests smoke_tests.cpp)
    target_compile_features(smoke_tests PRIVATE cxx_std_20)
    set_target_properties(smoke_tests PROPERTIES CXX_EXTENSIONS OFF)
    target_link_libraries(smoke_tests PRIVATE app_layout)
    target_compile_definitions(smoke_tests PRIVATE
        APP_TEST_DATA_DIR="${APP_TEST_DATA_DIR_CMAKE}"
        APP_SMOKE_OUT_DIR="${APP_SMOKE_OUT_DIR_CMAKE}"
    )
    app_set_warnings(smoke_tests)

    add_test(NAME app_smoke COMMAND smoke_tests)
    set_tests_properties(app_smoke PROPERTIES WORKING_DIRECTORY "${APP_REPO_ROOT}")
endif()

# ---------------------------------------------------------------------------
# The AppKit application.  macOS only: main.mm is Objective-C++ and links AppKit,
# and there is no way to compile that on Windows/Linux.  A non-Apple configure
# must still succeed (the headless test above is what those platforms build).
# ---------------------------------------------------------------------------
if(APPLE)
    enable_language(OBJCXX)

    find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
    # main.mm filters the open panel with UTType (NSOpenPanel's non-deprecated
    # content-type API); UTType lives in UniformTypeIdentifiers.framework, which
    # is not part of Cocoa.
    find_library(UNIFORM_TYPE_IDENTIFIERS_FRAMEWORK UniformTypeIdentifiers REQUIRED)

    add_executable(msproject_app MACOSX_BUNDLE main.mm)
    target_compile_features(msproject_app PRIVATE cxx_std_20)
    set_target_properties(msproject_app PROPERTIES
        OBJCXX_STANDARD 20
        OBJCXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        MACOSX_BUNDLE_BUNDLE_NAME "MS Project Shell"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.msprojectdecompile.shell"
        MACOSX_BUNDLE_BUNDLE_VERSION "0.4"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "0.4"
    )
    # ARC for the one Objective-C++ translation unit; the core and app_layout stay
    # plain C++.
    target_compile_options(msproject_app PRIVATE
        "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>"
    )
    target_link_libraries(msproject_app PRIVATE app_layout ${COCOA_FRAMEWORK}
                          ${UNIFORM_TYPE_IDENTIFIERS_FRAMEWORK})

    # Fixture search paths compiled in so the app opens something with no argv.
    file(TO_CMAKE_PATH "${APP_REPO_ROOT}/corpus/probe" APP_FIXTURE_DIR_CMAKE)
    file(TO_CMAKE_PATH "${APP_REPO_ROOT}/tests/data" APP_TEST_DATA_DIR_CMAKE)
    target_compile_definitions(msproject_app PRIVATE
        APP_FIXTURE_DIR="${APP_FIXTURE_DIR_CMAKE}"
        APP_TEST_DATA_DIR="${APP_TEST_DATA_DIR_CMAKE}"
    )
    app_set_warnings(msproject_app)
    message(STATUS "app: AppKit target msproject_app enabled (APPLE)")
else()
    message(STATUS "app: not APPLE -- AppKit target skipped, headless tests only "
                   "(APP_BUILD_TESTS=${APP_BUILD_TESTS})")
endif()
