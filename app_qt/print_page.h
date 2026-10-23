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
app::PrintJob make_print_job(const app::PlanInput& plan,
                             const std::vector<std::size_t>& rows,
                             const std::string& filter_description,
                             const std::string& date_text, app::ZoomLevel zoom);

// The rows a print/export request covers: the filtered display list when a
// filter is active (an empty active list is a legitimate "nothing matches"),
// else every RecordSet row in order.
std::vector<std::size_t> print_rows(std::size_t record_row_count,
                                    const std::vector<std::size_t>& display_rows,
                                    bool filter_active);

// Opens the print dialog for a QPrinter configured from `job.spec` and paints
// every page.  False when the user cancels or the device cannot be painted.
bool print_job(QWidget* parent, const app::PrintJob& job,
               const core::sheet::SheetModel& sheet, const app::PlanInput& plan,
               const app::Preferences& preferences, QString* error);

}  // namespace msqt
