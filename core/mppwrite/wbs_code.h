/*
 * wbs_code.h -- define the WBS code "W" + two-digit ordered numbers.
 * W34-4.6 (8 November 2026).
 *
 * WHAT IT WRITES.  Project > Properties > WBS > Define Code, then Renumber,
 * with the scheme wave30-wbs-code measured: prefix "W", level 1, ordered
 * numbers, length 2, separator ".", generate codes, verify uniqueness, renumber
 * all.  Against Project's control the edit save differs in exactly these
 * places (spec/writers/evidence/wave30-wbs.md):
 *
 *   114/Props   record 0x0240138b 4 -> 24 B (the code-mask definition,
 *               `01000000 00000200 2e000000 00000000 07000000 57000000`) and
 *               record 0x024013be 2 -> 6 B (the prefix text, UTF-16 "W" and
 *               its NUL, then two zero bytes); the Task size scalar follows
 *               Var2Data (512 -> 552); later records and the tail move +24.
 *   114/TBkndTask/VarMeta, Var2Data   four 6-byte UTF-16 blocks appended in
 *               this order: the project row (uid 0, key 0x0B40019F) "03" and
 *               the three tasks (uids 1-3, key 0x0B40018D) "01" "02" "03";
 *               four VarMeta entries at their sorted slots, count 42 -> 46.
 *   114/TBkndTask/FixedMeta   each of the four rows' +2 field count +1; the
 *               project row's +40 |= 0x04; each task row's +20 |= 0x80.
 *   Props14     the editor marker.
 *
 * It is composed from the three measured writers (`save_var_record` four times,
 * `save_props_edit` once), each of which proves its own step from the bytes on
 * disk; this file adds the all-or-nothing wrapper, the population check and a
 * final proof that nothing outside the five owned streams moved.
 *
 * THE MEASURED POPULATION (brief rule 2: one data point is one shape).  The
 * control holds the project row and exactly three flat, unsummarised tasks
 * with UniqueIDs 1, 2 and 3, no stored WBS text and no code definition.  A
 * file with any other task population, a stored WBS block, or a definition
 * already present is REFUSED: another count, an outline with summaries or a
 * second definition each need their own pair (the generated text for a nested
 * outline, the per-level mask for a different prefix or separator, and the
 * `0x0240138b` growth for another length are all undecoded).  The plan side of
 * the command (app/edit_model) refuses the same shapes before this runs.
 *
 * `dry_run` runs the whole chain against a scratch file next to nothing the
 * caller owns (the system temporary directory), removes it, and reports what a
 * real write would do; `doc` and `path` are untouched.
 */

#ifndef MPPWRITE_WBS_CODE_H
#define MPPWRITE_WBS_CODE_H

#include <cstddef>
#include <cstdint>
#include <string>

#include "cfb_writer.h"

namespace mppwrite {

/* The measured control: the project row and tasks 1..3. */
inline constexpr std::uint32_t kWbsMeasuredTaskCount = 3;

struct WbsCodeWrite {
    std::size_t blocks_written = 0;       /* var blocks appended (4) */
    std::size_t var2_bytes_before = 0;    /* Task Var2Data */
    std::size_t var2_bytes_after = 0;
    std::size_t props_bytes_before = 0;   /* 114/Props */
    std::size_t props_bytes_after = 0;
};

/*
 * Define the measured WBS code on `doc`, write the package to `path`, and prove
 * it from disk.  The five measured writers run against a scratch file in the
 * system temporary directory; `path` is written ONCE, after the whole chain has
 * succeeded and been proven, so a refusal or a mismatch never touches an
 * existing `path`.  Throws Error on a refusal or a mismatch; then neither `doc`
 * nor a new output file has been left behind.  With `dry_run`, `path` is not
 * checked or created and `doc` is not modified.
 */
WbsCodeWrite save_wbs_code(CfbDocument& doc, std::string const& path,
                           bool dry_run = false);

} /* namespace mppwrite */

#endif /* MPPWRITE_WBS_CODE_H */
