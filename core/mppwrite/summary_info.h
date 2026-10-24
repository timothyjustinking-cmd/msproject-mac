/*
 * summary_info.h -- the DocumentSummaryInformation stream Project rewrites on
 * every editor save.
 *
 * WHY THIS FILE EXISTS (W29-1.0b, the R28-1 residual)
 *
 * Until wave 29 the save-pair tests treated DocumentSummaryInformation as an
 * unowned stream Project rewrites, so "the port does not reproduce it" was a
 * named expected failure.  A whole-stream expected failure cannot tell a
 * per-save byte from a change the edit caused, so it would also hide a real
 * one.  W29-1.0b measured a null save for every committed pair: Project opened
 * the control, saved it with NO edit, reopened it and saved once more.  The
 * zero-edit save leaves DocumentSummaryInformation byte for byte identical to
 * the control in all 23 pairs, so this stream has NO per-save noise: every byte
 * that differs between the null save and Project's edit save is the edit's
 * effect.  Eighteen of the 23 pairs change it, and every changed value is one
 * of the project summary's display fields.
 *
 * WHAT THE STREAM CARRIES (OLEPS property set; measured layout)
 *
 *   header            28 bytes: byte order 0xfffe, version, system id, 16-byte
 *                     CLSID, then u32 section count = 2
 *   section descriptor 20 bytes each: 16-byte FMTID + u32 offset
 *   section 0         FMTID d5cdd502-2e9c-101b-9397-08002b2cf9ae
 *                     a property table of (pid, offset) pairs, then the values.
 *                     pid 0x0D TitlesOfParts is a VT_VECTOR|VT_LPSTR of the
 *                     seven summary titles; pid 0x0C HeadingPairs names them.
 *   section 1         FMTID d5cdd505-2e9c-101b-9397-08002b2cf9ae
 *                     a dictionary (pid 0) and the typed display values:
 *                     pid 2 % complete, 3 cost, 4 duration, 5 finish FILETIME,
 *                     6 start FILETIME, 7 work, 8 % work complete; pids
 *                     0x1000002..0x1000008 are their labels.
 *   A simple LPSTR value is `u32 30, u32 length` with the length rounded up to
 *   four and NUL-padded.  A FILETIME is `u32 64, u64`.  Sections are padded to
 *   a four-byte size; the section offsets are recomputed from the sizes.
 *
 * The seven display values, MEASURED on 23 pairs and reproduced byte for byte
 * from the edited project row (uid 0) by build/w29/dsi_derive.py:
 *
 *   titles[0] / typed pid 6   start   fixed date text of row +96
 *   titles[1] / typed pid 5   finish  fixed date text of row +100
 *   titles[2] / typed pid 4   duration  +84 ticks, "<days> days" with "?"
 *                             when DurationFormat +164 has bit 0x20
 *   titles[5] / typed pid 2   percent  +92 as "<n>%"
 *   titles[6] / typed pid 8   % work   +94 as "<n>%"
 *   title [3] / typed pid 7   work    UNCHANGED (no measured pair changes the
 *                                     project row's total work, +8)
 *   title [4] / typed pid 3   cost    UNCHANGED
 *
 * The date text is "Ddd D/MM/YY" (weekday, not-zero-padded day, two-digit
 * month and year).  Duration is ticks / 4800 (one 8-hour day), at most two
 * decimals with trailing zeros trimmed.  The two measured format words are 21
 * and 53; 53 is 21 | 0x20 and the 0x20 bit is Project's "?" suffix.  Anything
 * else is refused rather than guessed.
 *
 * The FILETIMEs are the project row's date with a FIXED local time (measured
 * 08:00 for the start, 17:00 for the finish on the fixture machine, UTC+11 in
 * AEDT).  The writer keeps the stored time of day and adds the row's DAY delta
 * (after - before) in seconds, so it needs no timezone database and reproduces
 * Project for a date move that stays inside one UTC offset -- which every
 * measured pair does.  A move across a DST change is unmeasured and would need
 * the other offset.
 *
 * WHAT IS REFUSED (throw mppwrite::Error, so the caller writes nothing)
 *
 *   - a stream whose layout is not the one above (checked by re-serialising it
 *     unchanged and requiring the bytes to match),
 *   - a titles vector that is not seven VT_VECTOR|VT_LPSTR strings,
 *   - typed pids 2..8 missing or of an unexpected type,
 *   - a project-row DurationFormat other than 21 or 53,
 *   - an invalid project-row start or finish,
 *   - a change to the project row's total work (+8..+15).
 *
 * This is the measured-save-path rule: for a shape not measured here the
 * command must be refused, never written with a guessed value.
 */

#ifndef MPPWRITE_SUMMARY_INFO_H
#define MPPWRITE_SUMMARY_INFO_H

#include "cfb_writer.h"
#include "table.h"

namespace mppwrite {

/* The OLEPS stream Project uses for the project summary's display values. */
inline constexpr const char* kDocumentSummaryStream =
    "DocumentSummaryInformation";

/*
 * Rewrite DocumentSummaryInformation from the edited project row.
 *
 * `before` is the task table as the opened file stores it; `after` is the table
 * the caller is about to store.  Both must be this document's table.  Every
 * measured pair has the two tables differing only in the project row's
 * duration, start/finish, percent and percent-work words (and, for a date
 * update, its stored actuals); any other change that would make a display
 * value unmeasurable is refused above.
 *
 * The stream is replaced only after the whole rebuild has been validated, so a
 * refusal leaves the document untouched.
 */
void write_project_summary(CfbDocument& doc, TaskTable const& before,
                           TaskTable const& after);

} /* namespace mppwrite */

#endif /* MPPWRITE_SUMMARY_INFO_H */
