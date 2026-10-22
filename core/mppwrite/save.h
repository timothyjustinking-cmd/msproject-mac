/*
 * save.h -- the save flow: apply edits to an in-memory document, write it to
 * disk, and prove from the bytes on disk that the edit is there.
 *
 * WHY THIS EXISTS.  `row_writer.h` can write a row, `cfb_writer.h` can emit the
 * container, but a caller that wants "change these dates and save" still has to
 * get five things right on its own: resolve UniqueIDs to rows, refuse the
 * project row, validate EVERY edit before touching the file, write, and then
 * re-open the written file to prove the write landed.  This file is that
 * sequence, once.
 *
 * ORACLE VERDICT FOR A DATE EDIT (measured 2026-10-22, MS Project via the
 * coordinator; do not soften this):
 *   SNET on an auto-scheduled task:      HONOURED -- `w29-snet.mpp` reopened
 *     with uid 1 on 2026-10-21 08:00-17:00 and Project recalculated around it;
 *   SNET on a Manual=True task:          STORED BUT IGNORED -- `w37-snet-save.mpp`
 *     reopened with ConstraintType 4 and the exact date and Start unchanged
 *     (2026-01-04 13:00), so a manual row reopens at its own start;
 *   SNET + clear the manual-mode bit:    HONOURED, row becomes auto-scheduled --
 *     `w37-snet-auto.mpp` reopened with Manual=False, Start = the SNET date
 *     (2026-10-21 08:00) and Finish recalculated through the calendar.
 * So `save_date_edits` is "pin with SNET, and for a row stored as manual also
 * clear Fixed2Meta's per-row flags bit 0x80" (the reader's `manually_scheduled`,
 * now measured as the mode bit).  That converts the row to auto-scheduled: the
 * date sticks, manual scheduling is NOT preserved.  A row whose flag byte cannot
 * be read is pinned without conversion and warned about, never silently.
 *
 * HONEST LIMIT (inference, unmeasured).  SNET means "start no earlier than", so
 * on a row with an FS predecessor Project places it at max(predecessor finish,
 * pin).  If links push the row later than the pinned date, opening the file will
 * not reproduce the exact date.  Exact-date writes for linked rows are
 * UNMEASURED; do not claim them.  A fixture that pins a successor before its
 * predecessor's finish (`w37-snet-link.mpp`, not built this wave) would settle
 * it.
 *
 * Earlier measured history that led here: writing a task's date words does not
 * make Project show the date (spec/mpp-writer.md §8.1, W26/W29).  For a
 * Manual=True row Project recomputes the effective dates on load and ignores
 * the date copies, and even Project's own manual-save word set (FixedData
 * +104/+108, +112/+116, +174, Fixed2Data +50/+54, with Fixed2Meta +88 and the
 * StartText/FinishText VarMeta entries dropped) reopens with Start unchanged.
 * A real SNET constraint at FixedData +64 (ConstraintType, u16) and +66
 * (ConstraintDate, packed date) is the only measured date store Project acts
 * on; manual rows need the mode bit cleared as well.
 *
 * WHAT "SAVE" MEANS HERE.  `CfbDocument::save` writes `to_bytes()`; for a
 * document parsed and then edited, that is the parsed container with the edited
 * streams replaced, valid by construction (tests/roundtrip_cfb.cpp proves the
 * round trip is byte-identical for an unedited file).  This flow adds the
 * end-to-end part: after writing, it re-opens the file from disk with
 * `CfbDocument::from_file`, requires the re-read bytes to equal what was
 * written, runs the CFB self-check and `check_table`, and re-reads every
 * touched row through `read_task_constraint` and `read_task_values`.  A save
 * that cannot prove those throws; it never returns "probably saved".
 *
 * ALL-OR-NOTHING.  Every edit / planned row is validated against the CURRENT
 * document before the output path is opened.  The edits are then applied to a
 * COPY of the document, so a refusal leaves both the caller's document and the
 * file untouched.  On success the caller's `doc` is replaced by the written
 * document, i.e. `doc` and the file agree when the call returns.
 *
 * ADDITIVE.  Nothing in this header changes an existing signature or an
 * existing behaviour; `row_writer.h`, `table.h` and `cfb_writer.h` are used as
 * they are.  A caller that wants the lower-level steps keeps using them.
 *
 * WHAT THIS FILE DOES NOT DO.  It does not recalculate a schedule (that is
 * `save_plan`, which writes rows the caller's engine produced -- see
 * `recalculate_write_path`), does not create streams or rows (the writer cannot
 * add a directory entry or a table row; spec/mpp-writer.md §5, §10.4), and does
 * not regenerate `214/` (decision in spec §10.4).
 */

#ifndef MPPWRITE_SAVE_H
#define MPPWRITE_SAVE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cfb_writer.h"
#include "row_writer.h"
#include "table.h"

namespace mppwrite {

/* One date edit, addressed the way a caller knows a task: by UniqueID. */
struct DateEdit {
    std::uint32_t uid = 0; /* task UniqueID (FixedData +4), NOT the row index */
    std::uint32_t start = kInvalidDate; /* packed date32, see table.h */
};

/*
 * `rows_written` is the number of edits applied (one per edit) or plan rows
 * written.  `warnings` carries the writer's own warnings plus, on success, one
 * informational receipt line per date edit / one summary line for a plan
 * recording the post-save re-read from disk.  It also carries the manual-row
 * conversion warning described under save_date_edits; that warning is how a
 * caller learns that the edit changed the row's scheduling mode.  A refusal is
 * always an exception, never a warning.
 */
struct SaveResult {
    std::size_t rows_written = 0;
    std::vector<std::string> warnings;
};

/*
 * Apply each uid's start date as an SNET pin (pin_task_start) plus, for a row
 * stored as manually scheduled, a clear of its manual-mode bit, and write the
 * package to `path`.
 *
 * Validated before anything is written, each with a message naming the uid and
 * the value:
 *   - the edit list must not be empty (nothing to save is a caller error);
 *   - uid 0 and row 0 (the project row) are refused;
 *   - every uid must exist in the table AND have a FixedMeta entry (a stored
 *     row no entry names is treated as deleted by Project, so the pin would be
 *     dead);
 *   - no uid may appear twice (one start per task);
 *   - every date must be a valid packed date (not 0, not 0xFFFFFFFF);
 *   - `path` must name a file in an existing directory.
 * The edits are applied to a copy of `doc`: the SNET constraint words
 * (FixedData +64/+66) of the edited rows, and -- for a row stored as manual --
 * its Fixed2Meta per-row flags byte with bit 0x80 cleared.  Every other byte of
 * every stream is checked to be unchanged.  After saving, the file is re-opened
 * from disk: the bytes must equal what was written, the CFB self-check and
 * `check_table` must be clean, `read_task_constraint` must be SNET with the
 * exact date, `read_task_values` must equal the written row, and a converted
 * row's flag byte must read back clear.  Any failure throws Error; in that case
 * neither `doc` nor a new output file has been left behind (a pre-existing file
 * at `path` is only replaced by `save` itself).
 *
 * Warnings this entry point appends to SaveResult::warnings, per edited row:
 *   - a row that was manual: "uid N: was manual; written as SNET and converted
 *     to auto-scheduled so Project honours the date (manual scheduling is not
 *     preserved, measured 2026-10-22)";
 *   - a row whose Fixed2Meta flag byte is unreadable: "scheduling mode unknown
 *     ...; SNET is honoured only for an auto-scheduled row" (the pin is written
 *     but not converted);
 *   - a replaced non-SNET constraint, and the "re-read from disk" receipt.
 * It never silently reports success for a manual row.
 */
SaveResult save_date_edits(CfbDocument& doc, std::vector<DateEdit> const& edits,
                           std::string const& path,
                           std::string_view table = "114/TBkndTask");

/*
 * Write a whole plan result (recalculate_write_path) and save.
 *
 * Each planned row is resolved with `resolve_task_values` against the current
 * document first, so every refusal happens before the file is touched; uids
 * must exist and must not repeat (row 0 -- uid 0, the project row -- is legal
 * here, because a plan result contains it and `write_task_row` handles it).
 * The rows are then written with `recalculate_write_path` on a copy, which also
 * runs `check_table`.  As with `save_date_edits`, `path` is validated, the file
 * is re-opened from disk and required to hold exactly the written bytes, the
 * CFB self-check and `check_table` must pass, and every row is re-read
 * (`read_task_values` and `read_task_constraint`) and must equal the resolved
 * values.  Only FixedData and Fixed2Data may differ, and only inside the
 * planned rows.
 *
 * An empty row list is legal and writes the document unchanged with
 * rows_written == 0.
 */
SaveResult save_plan(CfbDocument& doc, std::vector<PlannedRow> const& rows,
                     std::string const& path,
                     std::string_view table = "114/TBkndTask");

} /* namespace mppwrite */

#endif /* MPPWRITE_SAVE_H */
