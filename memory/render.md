# memory: render stream (W39, wave 9)

_2026-10-22. Owner: `core/render/` (new), `tests/render_*.cpp` (new),
`memory/render.md` (this file). No git commands were run; everything is on disk
in the main checkout for the coordinator to commit. No file outside those paths
was edited._

Read this, then `core/render/pdf_writer.h` and `core/render/report.h` (the
contracts are in their comments), and `memory/sheet.md` / `memory/gantt.md` for
the two models a report consumes.

## State: implemented, built with MSVC /W4 /WX, tested

| File | Contents |
|---|---|
| `core/render/pdf_writer.h` | `PdfWriter` (pages, text, lines, rects, polygons, clipping, finish/save), `PdfPageFormat` (A4 landscape default + portrait), `PdfFont` (Helvetica, Helvetica-Bold), `PdfInfo`, `winansi_encode()`, `escape_pdf_string()`, `pdf_font_name()` |
| `core/render/pdf_writer.cpp` | PDF 1.7 serialisation: `%PDF-1.7`, 5 fixed objects (Catalog, Pages, Info, two Type1 fonts), one /Page + one content stream per page, classic xref with absolute byte offsets, trailer, startxref, `%%EOF`. Hand-written locale-free fixed-point formatting, deliberate UTF-8 -> WinAnsi encoding, glyph-width tables for the two fonts |
| `core/render/report.h` | `ReportStyle`, `ReportRequestBase`, `SheetReportRequest`, `GanttReportRequest`, `task_report_columns()`, `render_sheet_report()`, `render_gantt_report()` |
| `core/render/report.cpp` | column planning/truncation, title block, repeated header, pagination, grid, legend, page numbers; Gantt timescale choice, bars/milestones/summaries, dependency polylines + arrowheads, whole-day non-working shading, status line |
| `core/render/CMakeLists.txt` | `core_render` library, alias `msproject::render`, `render_tests`, ctest `render`; subtree **and** standalone modes (`-S core/render`) |
| `core/render/verify_pdf.py` | OPTIONAL external check; stdlib structural parser always, `pypdf`/`PyPDF2` when installed (nothing is installed by this repo) |
| `tests/render_test.h` | check counter + an **independent PDF parser** (follows startxref, parses xref entries, /Root -> /Pages -> /Kids, content streams, literal strings) shared by both suites |
| `tests/render_pdf_tests.cpp` | (a) structure, (d) escaping/WinAnsi, clipping, determinism, coordinate conversion |
| `tests/render_report_tests.cpp` | (b) 200-row sheet, (c) empty project, (d) long names, Gantt suite, (e) corpus-gated real file |
| `tests/render_test_main.cpp` | one binary, prints `render_tests: N checks passed` |

No third-party library is linked, no zlib, no image or font asset. The only
dependencies are the C++20 standard library plus this project's `core::view`,
`core::sheet` and `core::gantt` (which brings `core::sched`).

## Verification (last run 2026-10-22, MSVC 19.44, `/W4 /permissive- /utf-8 /WX`)

Build dir is `build\vs-w39` (this stream only). Never `--parallel`.

```powershell
$vc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "call `"$vc`" >nul && cmake -S . -B build\vs-w39 -G `"Visual Studio 17 2022`" -A x64 -DCMAKE_BUILD_TYPE=Release -DCORE_WARNINGS_AS_ERRORS=ON"
cmd /c "call `"$vc`" >nul && cmake --build build\vs-w39 --config Release --target render_tests"
.\build\vs-w39\core\render\Release\render_tests.exe
# -> render_pdf: 97 checks, 0 failed
# -> render_report: 72 checks, 0 failed
# -> render_tests: 169 checks passed
# -> [render_report] corpus sheet: rows=83 pages=3 bytes=51286
# -> [render_report] corpus gantt: rows=83 links=88 pages=3 bytes=140592

$ctest='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
& $ctest --test-dir build\vs-w39 -C Release --output-on-failure -R "^render$"
# -> 1/1 Test #30: render ... Passed; 100% tests passed
```

Standalone mode (proves the second CMake path; no `core_mpp`, so the corpus test
prints SKIP and 8 corpus checks do not run):

```powershell
cmd /c "call `"$vc`" >nul && cmake -S core\render -B build\vs-w39\standalone -G `"Visual Studio 17 2022`" -A x64 -DCORE_WARNINGS_AS_ERRORS=ON"
cmd /c "call `"$vc`" >nul && cmake --build build\vs-w39\standalone --config Release --target render_tests"
.\build\vs-w39\standalone\Release\render_tests.exe
# -> [render_report] no reader in this build: corpus test SKIP
# -> render_tests: 161 checks passed
```

External reader (the C++ test does **not** depend on it; pypdf happens to be
installed on this machine). The test writes four SYNTHETIC samples to
`build\vs-w39\core\render\render-output\` (inside the build tree; never the
source tree, and never a corpus document):

```powershell
python core\render\verify_pdf.py build\vs-w39\core\render\render-output\*.pdf
# sheet-synthetic-200.pdf: ok bytes=116850 pages=6 xref=18 version=1.7 | pypdf pages=6 text_chars=11700
# gantt-synthetic-60.pdf:  ok bytes=85475  pages=2 xref=10 version=1.7 | pypdf pages=2 text_chars=4320
# sheet-empty.pdf:         ok bytes=2693   pages=1 xref=8  version=1.7 | pypdf pages=1 text_chars=153
# sheet-long-names.pdf:    ok bytes=3194   pages=1 xref=8  version=1.7 | pypdf pages=1 text_chars=244
# all files passed
```

pypdf text extraction of `sheet-long-names.pdf` contains `Café` (WinAnsi 0xE9)
and does **not** contain the UTF-8 byte pair `C3 A9`: the encoding is right, not
merely structurally valid.

## Measured vs decided

### Reused (not re-derived) -- this is the point of the stream

| Data | Source |
|---|---|
| every cell string, date/duration/percent/cost/bool text | `core::sheet::SheetModel::cell_text()` (so `format_value_text`/`format_date_text`/`format_duration_text`) |
| row visibility (collapsed summaries), summary bold, critical/overdue red, indent | `SheetModel::visible_rows/cell_style/indent_points` |
| column titles | `SheetModel::column_title()` (authored title, else descriptor name) |
| bar rectangles, milestone diamonds, summary bars, percent fill, labels | `core::gantt::build_gantt()` / `Bar` |
| dependency polylines and arrowheads | `GanttLayout::links` (`LinkRoute::points`, `ArrowHead::tip/wing`) |
| non-working shading bands | `GanttLayout::nonworking_spans()` (whole-day spans only) |
| timescale header labels | `Timescale::header(major/minor, x0, x1)` |
| date -> x | `Timescale` (the report never maps a date itself) |

### Verified externally

- The produced bytes parse as a PDF: startxref lands on `xref`, every in-use
  entry's offset lands on `<n> 0 obj`, `/Size` == entry count, `/Root` is the
  catalog, `/Pages /Count` == `/Kids` == `/Type /Page` objects, `%%EOF` last
  (both in the C++ parser and in `verify_pdf.py`).
- pypdf reads all four synthetic samples and agrees on page count.
- WinAnsi round trip: `Café – 50 €` extracts as the same characters.
- Byte determinism: the same request rendered twice is byte-identical (FNV-1a
  compared in the tests).
- 200 synthetic tasks appear exactly once each across 6 pages; the column header
  appears once per page; `Page i of n` on every page.
- Gantt: 60 tasks over 2 pages, 59 links drawn, legend on every page.
- Corpus (real 83-task file): 83 rows, 3 sheet pages / 51,286 bytes, Gantt 3
  pages / 140,592 bytes / 88 live links. **Counts only; no name is printed and
  no corpus PDF is written.**

### Decisions (ours; no oracle, no product screenshot)

1. **A4 landscape 842 x 595 pt is the default**; portrait is 595 x 842; any size
   is accepted. Margins 24 pt, title 14 pt, body 8 pt, rows 13 pt, header 16 pt.
2. **Coordinates are points, y DOWN from the top-left**, matching sheet/Gantt;
   `PdfWriter` flips to PDF user space internally. A report never flips a y.
3. **Column widths** start at max(header, widest cell), capped by an authored
   `SheetColumn::width` (characters x `metrics.char_width`) and by 60 % of the
   page; when the total overflows, widths are scaled then the widest are shaved.
4. **Truncation** appends `...` and cuts only between UTF-8 code points. The full
   value stays in the RecordSet; the printed text is short.
5. **Pagination**: `floor((body_bottom - body_top - header_height) / row_height)`,
   minimum 1; the title block, header, legend and `Page i of n` repeat.
6. **Gantt timescale**: auto unit = day for a span <= 45 days, week <= 260, month
   beyond; `pixels_per_unit` chosen once so the project fits the chart width, so
   bars line up across pages. With `auto_timescale = false` the caller's
   `Timescale` is used verbatim.
7. **Gantt label** is always drawn to the right of the bar (a printed chart has
   no hover). The geometry supplies the text; its inside/outside placement is
   not used.
8. **Summary bars** get decorative end ticks (two short vertical lines) because
   `core::gantt` deliberately leaves the real end-cap shape unmodelled.
9. **Only whole-day non-working spans are shaded**; `partial == true` spans are
   skipped, per `memory/gantt.md` decision 1 (day-granularity renderer).
10. **Legend text** ("Task", "Critical", "Summary", "Milestone", "Dependency",
    "Critical / overdue", "Bold = summary") is presentation text invented here.
11. **Default columns** are ID, Name, Duration, Start, Finish, Slack
    (`TotalSlack`), Critical. `Slack` is the only authored title.
12. **Gantt milestone detection** is the stored `Milestone` flag OR a zero
    duration, matching the reader's effective-milestone fallback.
13. **/Info** defaults: creator "MS Project Decompile (core/render)",
    creation date `D:20260101000000Z`. A caller overrides via `PdfInfo`; the
    report applies `request.info` only when `info.title` is non-empty.
14. **Non-representable code points become `?`** (counted by
    `winansi_encode(..., &n)`); nothing is silently dropped. Latin-1 and the
    CP1252 punctuation block encode exactly.

## Coordinate and pagination conventions (short form)

- Page: `(0,0)` top-left, x right, y down, points. Rect `(x, y, w, h)` extends
  right and DOWN. Text `(x, baseline_y)`.
- Header band: `body_top .. body_top + header_height`; rows start at
  `body_top + header_height`; footer rule at `page.height - margin - footer_height`.
- Row `i` on a page: `top + i * row_height`; baseline is
  `top + (row_height + 0.70 * font_size) / 2`.
- Gantt: left table `[margin, margin + table_width]`, chart
  `[margin + table_width + 6, page.width - margin]`; timescale band 26 pt
  (major 11 + minor 15); bars are translated by
  `content_top - first_row * row_height` and clipped to the chart box.
- Page count is computed before drawing, so the footer can print the total.

## What is open / next three actions

1. **No visual oracle.** The layout numbers (margins, row height, font sizes,
   legend wording, column caps) are inventions. A product screenshot of a
   printed Gantt/sheet is the only way to replace them; nothing here blocks on
   it, because they are all in `ReportStyle`.
2. **Month timescale bars** are placed by the Gantt's piecewise-linear month
   mapping and clipped; the report never validates that a 200-day project's
   month labels are readable at A4 width. A test with a >260-day span would pin
   the month path (currently only the day path is exercised by the synthetic
   tests; the corpus is day-scale too).
3. **CJK / other non-WinAnsi text** becomes `?`. If the app must print such
   names, the writer needs a real TrueType subset (out of scope: no font assets).
   A documented `?` is the honest current behaviour.
4. **No wrapped text, no repeated table header inside a single tall row, no
   multi-line titles.** A long title is truncated like any other text.
5. Optional: wire the report into the app's print/export command when W38's shell
   lands (the API is ready: build a request, `PdfWriter`, `save`).

## Traps hit (do not relearn)

1. **`finish()` must serialise the LOCAL page copy.** An empty writer gets a
   blank page added in `finish()`; object bodies built from `pages_` would read
   out of range. `page_object_body`/`content_object_body` take the format/content
   directly.
2. **Fit-text equality needs 0.001 pt of tolerance.** A column's width is
   `content + 2 * padding`; subtracting the padding again is one floating-point
   rounding step, and without tolerance a name that exactly fits became
   `A(B)C...`. The test pins the round trip.
3. **`/Type /Page` is a prefix of `/Type /Pages`.** Count page objects with a
   trailing space or the parser helper; a naive substring count returns one too
   many.
4. **The xref table has `object_count + 1` entries** (entry 0 is the free head);
   `/Size` is that count. A test that expects `7` for a 1-page document is wrong
   (it is 8).
5. **`/Kids [n 0 R ...]` parsing must skip the generation number and `R`**, or
   each page contributes a phantom kid `0`.
6. **MSVC `/W4 /WX` rejects `fopen` (C4996).** `PdfWriter::save` uses
   `std::ofstream` in binary mode; a `FILE*` probe in a test should be
   `std::ifstream`.
7. **`RCHECK` is a macro, not a namespaced symbol.** `using
   render_test::RCHECK;` fails with C2039.
8. **size_t vs long long comparisons warn (C4389) under `/W4`.** Cast the
   `size_t` count before comparing to `xref_count`.
9. **`core::gantt::LinkType` members are `finish_to_start`, `start_to_start`,
   `finish_to_finish`, `start_to_finish`** (not `finish_finish`), and the reader's
   raw values are 0=FF, 1=FS, 2=SF, 3=SS.
10. **Declared `units` in `report.cpp` must be initialised on every branch**;
    clang/GCC warn about a possibly-uninitialised local under `-Werror`.
11. **Dropbox rewrites mtimes on this tree.** `edit` often reports "file changed
    since it was read"; re-read immediately before each edit and retry.
12. **Gantt-style `measure_text` captures `pdf` by reference.** Build the layout
    and draw it inside the same call; do not store the layout past `pdf`.
13. **A corpus document is never written to disk.** `save_sample()` is only
    called from synthetic tests; the corpus test keeps the bytes in memory.
14. **`report.cpp` draws only visible rows** (`SheetModel::visible_rows()`); a
    collapsed summary hides its children from the report exactly as it does on
    screen. If a report should always show the full outline, call
    `model.expand_all()` on a model the caller owns first (not done here: the
    sheet's current view state is the truth).
