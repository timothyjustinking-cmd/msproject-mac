/*
 * viewstate_sort.h -- the Task Sheet sort persisted in the file.
 * W35-2.1 (9 November 2026).
 *
 * WHY THIS EXISTS.  Project stores the applied sort (Project > Sort) in the
 * view stream and the order it produced in the edit list.  Until wave 35 the
 * port applied a sort for the session only.  This is the writer for the shapes
 * the five `wave35-viewstate-sort-*` pairs measure (spec/writers/view-state.md
 * has the derivation; the pairs carry a null save, so per-save noise is masked
 * and only the edit's effects remain).
 *
 * THE MEASURED RULES.
 *   spec     `214/CV_iew/Var2Data`, the block of view uid 2 / key 0x20400006
 *            (offset 42,576, payload 42,692 B in every control), holds the sort
 *            specification TWICE: at block + 40 and at block + 1192, each three
 *            6-byte slots of `u32 field id, u16 ascending flag`.  The default
 *            is `17 00 40 0b 01 00 | ff ff ff ff 01 00 | ff ff ff ff 01 00`
 *            (the ID field, ascending).  Pair effects: Name descending
 *            rewrites slot 0 to `0e 00 40 0b 00 00` and Duration descending to
 *            `07 05 40 0b 00 00`; Name ascending is `0e 00 40 0b 01 00`; both
 *            copies change the same way.  Descending is flag 0, ascending 1.
 *   order    `214/CEdl/Var2Data` caches the displayed row order as UniqueIDs:
 *              0x26400035  the first N dwords of its 32-byte payload
 *                          (the control's `1 2 3 4 5`), then the dwords
 *                          `0x000ffdc0, 0, 0` that the pair leaves alone;
 *              0x26400036  its 4-byte payload is the first displayed UniqueID;
 *              0x26400024  a 64-byte payload: N UniqueIDs, then 0xffffffff
 *                          fill (every measured control carries it; the
 *                          0x2640003d record before it in four of them is
 *                          untouched by a sort).
 *            Pair: the control's `1 2 3 4 5` becomes `5 4 1 3 2` for Name
 *            descending (Task E, Task D, Task A, then A's children C, B) and
 *            `4 1 2 3 5` for Duration descending; the 0x26400036 word becomes
 *            the first displayed UniqueID (5, 4).  UniqueIDs, IDs and every
 *            task stream are untouched (Renumber = False), as is the
 *            Outline-versus-flat option (Outline = False writes the same bytes
 *            as Outline = True on the measured rows, so only Outline = True is
 *            claimed).
 *   marker   The Props14 edit marker (`edit_marker.h`) is written, as on every
 *            measured editor save.
 *
 * THE CAPABILITY MATRIX (brief rule 1; every other shape is refused up front,
 * in the edit model and here, each with its own test):
 *
 *   view            uid 2                      refused otherwise
 *   key count       exactly one                the two-key pair stays measured
 *                                              evidence only (its secondary key
 *                                              never decided a row)
 *   key field       Name or Duration           refused: ID, Start, Finish, ...
 *   direction       Name: ascending or         refused: Duration ascending (no
 *                   descending; Duration:      pair; the writer's "Duration
 *                   descending only            ascending is unmeasured")
 *   Outline         true                       Outline = False refused
 *   Renumber        false                      Renumber = True refused
 *   row count       5                          the pairs' row count; the 0x35
 *                                              payload length (N + 3 dwords) is
 *                                              measured at N = 5 only
 *   control state   default spec in both       a file that already carries a
 *                   copies, order 1..N         sort or a different order
 *   block           digest b2a54e044baa30a2    the measured uid-2 block (FNV-1a
 *                   (spec bytes masked)        64), so the text style around
 *                                              the spec is the measured one
 *
 * Refusals run before `doc` or the output path is touched; the document is
 * edited on a copy, the package is written, re-opened and proven from disk:
 * only {214/CV_iew/Var2Data, 214/CEdl/Var2Data, Props14} may differ from the
 * control, and the spec copies, the order records and the Props14 marker must
 * hold exactly the requested state.  On success `doc` is replaced by the
 * re-read document.
 */

#ifndef MPPWRITE_VIEWSTATE_SORT_H
#define MPPWRITE_VIEWSTATE_SORT_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "cfb_writer.h"

namespace mppwrite {

/* The measured sort fields: field ids in the spec slots. */
inline constexpr std::uint32_t kSortFieldId = 0x0B400017u;
inline constexpr std::uint32_t kSortFieldName = 0x0B40000Eu;
inline constexpr std::uint32_t kSortFieldDuration = 0x0B400507u;
inline constexpr std::uint32_t kSortSlotNone = 0xFFFFFFFFu;
/* The two spec copies, as offsets from the start of the uid-2 block. */
inline constexpr std::size_t kSortSpecFirst = 40;
inline constexpr std::size_t kSortSpecSecond = 1192;
inline constexpr std::size_t kSortSpecBytes = 18;
/* The pairs' row count (the CEdl order records are measured at 5 only). */
inline constexpr std::size_t kSortMeasuredRows = 5;
/* The CEdl records that hold the displayed order. */
inline constexpr std::uint32_t kEdlOrderId = 0x26400035u;
inline constexpr std::uint32_t kEdlFirstRowId = 0x26400036u;
inline constexpr std::uint32_t kEdlOrderCopyId = 0x26400024u;

enum class SortField : std::uint8_t {
    name = 0,
    duration = 1,
    id = 2,     /* refused: the default, unmeasured as an explicit key */
    start = 3,  /* refused */
    finish = 4, /* refused */
    other = 5,  /* refused: any other field */
};

struct SortKey {
    SortField field = SortField::name;
    bool ascending = true;
};

/* One measured sort.  `base_order` is the rows' UniqueIDs in their file order
 * (the control's displayed order) and `sorted_order` the same rows in the order
 * the sort produces; the edit model computes the latter (app/sort_model.h, sort_display_rows) under
 * its own pinned rules.  The writer checks both are permutations of each other
 * and that `base_order` is the control's. */
struct ViewSortEdit {
    std::uint32_t view_uid = 0; /* measured 2 */
    std::vector<SortKey> keys;  /* exactly one */
    bool outline = true;        /* measured true only */
    bool renumber = false;      /* measured false only */
    std::vector<std::uint32_t> base_order;
    std::vector<std::uint32_t> sorted_order;
};

/* What the write proved from the file on disk. */
struct ViewSortWrite {
    std::uint32_t view_uid = 0;
    std::uint32_t field_id = 0;
    bool ascending = true;
    std::size_t block_offset = 0;     /* the uid-2 block in CV_iew/Var2Data */
    std::size_t spec_bytes_changed = 0; /* bytes that differ in both copies */
    std::size_t order_rows = 0;
    std::size_t order_records_written = 0; /* the three CEdl records */
    std::uint32_t first_row_before = 0;
    std::uint32_t first_row_after = 0;
};

/* The pair-measured digest of the uid-2 block with both spec copies zeroed. */
std::uint64_t sort_block_digest(std::vector<std::uint8_t> const& var2,
                                std::size_t block_offset);

/* One task row as a sort sees it, in file order. */
struct SortRow {
    std::uint32_t uid = 0;
    int level = 0;                /* 0-based outline level */
    std::string name;             /* UTF-8 */
    std::uint32_t duration = 0;   /* ticks (FixedData +84) */
};

/* W35b-1.2: whether Name comparison of two sibling names is one the pairs
 * measure.  Project's collation of case, punctuation, digits and accents is not
 * measured, so two names are ordered only where the first differing position
 * holds two ASCII letters of the same case (neither name a prefix of the other). */
bool sort_name_pinned(std::string const& left, std::string const& right);

/* W35b-1.2: the displayed order a sort of `rows` (file order) produces under the
 * pinned rules, computed from the rows alone: siblings are ordered by the key
 * (stable), each subtree follows its parent.  Shared by the edit model (rows from
 * the plan) and by `save_view_sort` (rows from the file), so a caller's order is
 * checked against the file's own names, durations and outline.  Returns an empty
 * string and fills `sorted`, or the reason the order is unmeasured. */
std::string sorted_order_of(std::vector<SortRow> const& rows, std::uint32_t field_id,
                            bool ascending, std::vector<std::uint32_t>& sorted);

/* W35b-1.1: the uid-2 view-state block must be one of the two measured
 * controls' (digest with both sort-spec copies zeroed) and carry the default
 * spec in both copies.  Shared by the other view-state writers; `refused` names
 * what is refused in the message ("this outline level").  Throws Error. */
void require_measured_view_block(CfbDocument const& doc, std::string const& function,
                                 std::string const& refused);

/*
 * Apply `edit` to `doc`, write the package to `path`, re-open it and prove the
 * state from the bytes.  Throws Error on a refusal or a mismatch.  A refusal, or a
 * mismatch in the staged streams, throws before anything is written: `doc` is as it
 * was and `path` is untouched.  `dry_run` runs exactly those checks (`path` may be
 * empty).  The write itself is atomic (a temporary file, then a rename), but the
 * proofs on the re-read file come after it: a mismatch they find throws with `doc`
 * as it was and `path` already holding the new bytes (R35G2-10), so a throw from
 * this function does not promise that `path` was left alone.
 */
ViewSortWrite save_view_sort(CfbDocument& doc, ViewSortEdit const& edit,
                             std::string const& path, bool dry_run = false);

} /* namespace mppwrite */

#endif /* MPPWRITE_VIEWSTATE_SORT_H */
