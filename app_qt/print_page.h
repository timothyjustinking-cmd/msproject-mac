// app_qt/print_page.h -- File > Print... through QPrinter, on the SHARED page
// geometry.
//
// THE CONTRACT (docs/ui-qt-port.md section 2.6)
//   Page size, margins and every vertical band come from app::PageSpec through
//   app::build_print_job() (app/page_layout.h), the same function the macOS
//   shell prints with and the same one app/export_pdf.cpp gives core/render.
//   There is no second page arithmetic here: this file draws rows into the
//   bands page_layout() computed.
//
// WHAT IS DRAWN
//   The Gantt report the reference's Print command produces: the five printed
//   table columns (ID, Name, Start, Finish, Duration) and, for the Gantt kind,
//   the timescale header, weekend shading, links and bars from core::gantt.
//   The pagination and the header/footer strings are app::PrintJob's.
//
// EXPORT PDF is a different route on purpose: it writes core/render's bytes
// (app::export_report), so an exported PDF is byte-identical to the shared
// renderer on all three platforms.  File > Print... cannot be byte-identical to
// a PDF writer (the printer dialog chooses a device), but it uses the same
// PageSpec, and app/page_layout_tests.cpp pins that both routes paginate
// identically.
#pragma once

#include <QImage>
#include <QSize>
#include <QString>

#include <cstddef>
#include <string>
#include <vector>

#include "layout_model.h"
#include "page_layout.h"
#include "prefs.h"
#include "sheet.h"

class QWidget;

namespace msqt {

// The print request for the window's current state: the rows to print, the
// project title and the caller-formatted date text.  The zoom is the Gantt
// timescale the window is showing, so the printed chart matches the screen.
// `printed_task_count` is how many of `rows` are real tasks (the synthetic
// project row, uid 0, is a row but not a task), so the subtitle never reads
// "N+1 of N" when the file shows the project row (wave 24, W24-1).
// W26-4.5: `spec` is the paper/orientation the Print page's controls chose; the
// default is the shared A4 landscape page.
app::PrintJob make_print_job(const app::PlanInput& plan,
                             const std::vector<std::size_t>& rows,
                             std::size_t printed_task_count,
                             const std::string& filter_description,
                             const std::string& date_text, app::ZoomLevel zoom,
                             const app::PageSpec& spec = app::PageSpec::a4_landscape());

// The rows a print/export request covers: the filtered display list when a
// filter is active (an empty active list is a legitimate "nothing matches"),
// else every RecordSet row in order.  Wave 26 (W26-0.1): a thin wrapper over
// app::print_row_list(), the one implementation both shells call.
std::vector<std::size_t> print_rows(std::size_t record_row_count,
                                    const std::vector<std::size_t>& display_rows,
                                    bool filter_active);

// Wave 24 (W24-1/R6): the printed chart for `rows`, built from the RecordSet
// rows' uids.  Row N of the result is sheet row `rows[N]` (uid 0 draws the
// project-summary bar), so the sheet and chart halves cannot drift by one when
// the file shows the project row.  Public so the gate can compare the two
// without a printer.
core::gantt::GanttLayout print_gantt(const app::PlanInput& plan,
                                     const core::sheet::SheetModel& sheet,
                                     const std::vector<std::size_t>& rows,
                                     const app::PageSpec& spec, app::ZoomLevel zoom);

// Opens the print dialog for a QPrinter configured from `job.spec` and paints
// every page.  False when the user cancels or the device cannot be painted.
// W26-4.5: `copies` and `printer_name` come from the backstage Print page's
// controls; the page geometry is always job.spec's.
bool print_job(QWidget* parent, const app::PrintJob& job,
               const core::sheet::SheetModel& sheet, const app::PlanInput& plan,
               const app::Preferences& preferences, QString* error,
               int copies = 1, const QString& printer_name = QString());

// W26-4.5: one page of `job` rendered into an image of `target_size` -- the
// SHARED painter the printer path uses, so the backstage preview cannot drift
// from what is printed.  The image is the measured preview surface (a neutral
// #d9d9d9 backdrop with the white page rectangle fitted and centred); the gate
// compares the live preview pane with this function's output and the page
// rectangle's aspect with job.spec.
QImage render_print_page(const app::PrintJob& job, std::size_t page_index,
                         const core::sheet::SheetModel& sheet,
                         const app::PlanInput& plan,
                         const app::Preferences& preferences,
                         const QSize& target_size);

}  // namespace msqt
