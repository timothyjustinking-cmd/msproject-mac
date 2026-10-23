// app_qt/dialogs.h -- the wave-19 command dialogs, as Qt dialogs over the
// portable app/ models.
//
// Every decision these dialogs make lives in app/ and is headless-tested there
// (app/field_link_edits.h, app/filter_model.h); this file only builds controls,
// reads their strings and calls those functions.  That is the fidelity contract
// (docs/ui-qt-port.md section 2.4/2.5): one model, one behaviour, three shells.
//
//   * Task Fields...   -- app::task_field_specs / field_value_text /
//                         make_task_field_edits (the six measured custom fields)
//   * Predecessors...  -- app::predecessors_of / make_link_add / make_link_delete
//   * Filter...        -- app::filter_fields / filter_operators / build_filter
//
// All are plain QDialog/QMessageBox: no native panels, no Q_OBJECT (connections
// are lambdas on Qt signals), so the target stays moc-free.
#pragma once

#include <QString>

#include <cstdint>
#include <string>
#include <vector>

#include "edit_model.h"
#include "filter.h"
#include "layout_model.h"
#include "sheet.h"

class QWidget;

namespace app {
struct FilterField;
}

namespace msqt {

// Edit > Task Fields...  Prefilled from `row`'s stored values.  On OK, `edits`
// holds only the fields whose value changed (make_task_field_edits).  A parse
// error keeps the dialog open and shows the field's message.
bool edit_task_fields(QWidget* parent, const app::TaskRow& row,
                      const core::sheet::SheetFormat& format,
                      std::vector<app::StructuralEdit>& edits, QString* error);

// Edit > Predecessors...  `edits` is the current pending stack; the dialog works
// on a copy and assigns it to `out_edits` only when the user accepts, so a
// cancelled dialog queues nothing.  `loaded` is the file baseline the display
// plan is rebuilt from after each pending add/delete.
bool edit_predecessors(QWidget* parent, const app::PlanInput& loaded,
                       const app::StructuralEdits& edits, std::uint32_t successor_uid,
                       app::StructuralEdits& out_edits, QString* error);

// View > Filter...  One comparison rule (field, operator, value); Apply returns
// the built definition, Clear sets `cleared` and writes nothing.
bool edit_filter(QWidget* parent, const std::vector<app::FilterField>& fields,
                 const core::sheet::SheetFormat& format,
                 core::view::FilterDefinition* definition, std::string* description,
                 bool* cleared, QString* error);

// Insert > Task...: the name to add.  QInputDialog is a Qt dialog (never the
// platform's), so it matches the rest of the shell.
bool ask_task_name(QWidget* parent, QString* name);

}  // namespace msqt
