/*
 * viewstate_outline.h -- the Task Sheet outline level persisted in the file.
 * W35-2.3 (9 November 2026).
 *
 * WHY THIS EXISTS.  Project's View > Outline > Show > Level N collapses every
 * summary at level N - 1 and saves that state.  Until wave 35 the port applied
 * Show Level for the session only.  This is the writer for the shapes the five
 * `wave35-viewstate-outline-*` pairs measure (spec/writers/view-state.md section
 * 3; each pair carries a null save, so per-save noise is masked and only the
 * edit's effects remain).
 *
 * THE MEASURED RULES.  Rows are the task rows 1..N in file order; level is the
 * 0-based outline level.  Show Level L (1-based) makes
 *   collapsed  every summary row at level L - 1          FixedMeta flag 0x03
 *   hidden     every row at level >= L (under a collapsed summary)
 *                                                        FixedMeta flag 0x00
 *   shown      every other row                           FixedMeta flag 0x01
 * and writes, besides the Props14 edit marker (edit_marker.h):
 *   114/TBkndTask/FixedMeta   byte +15 of the 47-byte entry of each row (entry
 *                             index row + 3, after the 16-byte header); the
 *                             control's flag is 0x01 on every row.
 *   214/CEdl/Var2Data         the displayed rows.  Record 0x26400035 holds the
 *                             VISIBLE UniqueIDs, then 0x000ffdc0 - hidden, 0, 0
 *                             (its payload shrinks by 4 bytes per hidden row);
 *                             0x26400024 holds the visible UniqueIDs then
 *                             0xffffffff fill to 64 bytes; 0x26400036 (the first
 *                             displayed UniqueID) is unchanged.  Where the
 *                             control carries the row-layout record 0x2640003d
 *                             (four of the five controls do), it holds the
 *                             displayed row count N, the row offsets 0, 22, ...
 *                             22 (N - 1), the end offset 22 N, the row height 22
 *                             and two zero bytes, so it shrinks by 4 bytes per
 *                             hidden row as well.  The stream's three header
 *                             words follow its length (len - 4, len - 8,
 *                             len - 8).
 *   214/CEdl/VarMeta          the Var2Data size word (+0x14).
 *   214/Props                 the CEdl size, the u32 value of the scalar record
 *                             whose big-endian tag is 0x07000100.
 * The save-path record 0x26400031 of the CEdl stream is noise (it carries the
 * saving file's path) and is left as the control's; the two size words are the
 * written stream's own length, which Project's differ from only by the
 * path-length difference between its save and the control (the cause of the
 * wave-33 "VarMeta +21 / Props +11403" open gap).  No task stream, UniqueID or
 * ID moves.  Pairs: Level 1 on the five-task base hides UniqueIDs 2 and 3 and
 * collapses 1; Level 1 on the six-task three-level outline hides 2 to 5 and
 * collapses 1 (the summary 2 under it is hidden, flag 0x00, not collapsed);
 * Level 2 on that outline hides 3 and 4 and collapses 2.  Level 2 (and Show All)
 * on the base hides nothing and Project's save differs from the control by the
 * marker only; the writer reproduces that, and the edit model refuses it as
 * "nothing to persist".
 *
 * THE CAPABILITY MATRIX (brief rule 1; every other shape is refused up front,
 * in the edit model and here, each with its own test):
 *
 *   view            uid 2                      refused otherwise
 *   level           1 or 2                     refused: 3 and deeper (unmeasured)
 *   rows            the outline of the pairs   {0,1,1,0,0} (five tasks) or
 *                   (0-based levels, in file   {0,1,2,2,1,0} (six tasks); any
 *                   order)                     other count or shape is refused
 *   control state   every FixedMeta flag 0x01, a file that already hides or
 *                   the CEdl lists rows 1..N   collapses a row is refused
 *                   in order                   (replacing a state is unmeasured)
 *
 * Refusals run before `doc` or the output path is touched; the document is
 * edited on a copy, written, re-opened and proven from disk: only {114/TBkndTask/
 * FixedMeta, 214/CEdl/Var2Data, 214/CEdl/VarMeta, 214/Props, Props14} may differ
 * from the control, and each owned byte must hold exactly the requested state.
 * On success `doc` is replaced by the re-read document.
 */

#ifndef MPPWRITE_VIEWSTATE_OUTLINE_H
#define MPPWRITE_VIEWSTATE_OUTLINE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "cfb_writer.h"

namespace mppwrite {

/* FixedMeta: 16-byte header, 47-byte entries, entry index = row + 3. */
inline constexpr std::size_t kOutlineMetaHeader = 16;
inline constexpr std::size_t kOutlineMetaEntry = 47;
inline constexpr std::size_t kOutlineMetaFlag = 15;
inline constexpr std::uint8_t kOutlineFlagShown = 0x01;
inline constexpr std::uint8_t kOutlineFlagCollapsed = 0x03;
inline constexpr std::uint8_t kOutlineFlagHidden = 0x00;
/* The CEdl order record's third-from-last dword in the fully expanded control. */
inline constexpr std::uint32_t kOutlineOrderTail = 0x000FFDC0u;
/* 214/Props: the scalar record that mirrors the CEdl Var2Data size. */
inline constexpr std::uint32_t kOutlineEdlSizeTag = 0x07000100u;
/* The measured levels. */
inline constexpr int kOutlineMeasuredMinLevel = 1;
inline constexpr int kOutlineMeasuredMaxLevel = 2;

/* One task row: its UniqueID and 0-based outline level, in file order. */
struct OutlineViewRow {
    std::uint32_t uid = 0;
    std::uint32_t level = 0;
};

struct ViewOutlineEdit {
    std::uint32_t view_uid = 0; /* measured 2 */
    int show_level = 0;         /* 1-based, measured 1 and 2 */
    std::vector<OutlineViewRow> rows;
};

/* What the write proved from the file on disk. */
struct ViewOutlineWrite {
    int show_level = 0;
    std::size_t rows = 0;
    std::size_t hidden_rows = 0;
    std::size_t collapsed_rows = 0;
    std::size_t flags_written = 0;       /* FixedMeta bytes that changed */
    std::size_t edl_bytes_before = 0;
    std::size_t edl_bytes_after = 0;
};

/* The rows a Show Level hides and collapses, from the outline alone.  Shared
 * with the edit model so both sides compute the same sets.  Throws nothing;
 * `hidden[i]` / `collapsed[i]` are per row index. */
void outline_show_level_sets(std::vector<OutlineViewRow> const& rows, int show_level,
                             std::vector<bool>& hidden, std::vector<bool>& collapsed);

/* True when the rows' levels are one of the two outlines the pairs measure
 * (0-based 0,1,1,0,0 over five tasks or 0,1,2,2,1,0 over six).  The writer
 * checks it on every request; the edit model checks it in push() (W35b-3.1). */
bool outline_shape_measured(std::vector<OutlineViewRow> const& rows);

/*
 * Apply `edit` to `doc`, write the package to `path`, re-open it and prove the
 * state from the bytes.  Throws Error on a refusal or a mismatch; then neither
 * `doc` nor a new output file has been left behind.  `dry_run` runs every
 * check and builds and re-checks the staged streams but writes nothing (`path`
 * may be empty) and leaves `doc` as it was.
 */
ViewOutlineWrite save_view_outline(CfbDocument& doc, ViewOutlineEdit const& edit,
                                   std::string const& path, bool dry_run = false);

} /* namespace mppwrite */

#endif /* MPPWRITE_VIEWSTATE_OUTLINE_H */
