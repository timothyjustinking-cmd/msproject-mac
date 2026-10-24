/*
 * edit_marker.h -- the Props14 flag Project records on every editor save
 * (wave 29, W29-1.0b).
 *
 * WHY THIS FILE EXISTS
 *
 * Until wave 29 the save-pair tests treated Props14 as an unowned stream
 * Project rewrites, so "the port does not reproduce it" was a whole-stream
 * expected failure.  W29-1.0b measured a null save (Project opens the pair's
 * control and saves it with NO edit, twice) for every committed pair and
 * normalised the one variable-length record (the save path).  After that
 * normalisation every control, null save and edit save in all 23 pairs differs
 * at exactly ONE byte of interest: record 0x3540000a's payload.
 *
 *   control / null save   payload 00 00
 *   Project's edit save   payload 01 00
 *
 * The payload also stays 01 00 on a control that an earlier edit produced, so
 * the write is "set to 1", not "toggle": an edit save always stores 01 00.
 * That makes it a pure EDIT EFFECT, and the port must reproduce it.  The rest
 * of the stream (the 0x3540000b / 0x35400008 records, the "Fixture Builder"
 * marker Project's null save drops) is per-save noise, masked by the pair test.
 *
 * WHERE THE RECORD IS
 *
 * The record is NOT in the stream's top-level record array: the walk that
 * frames Props14 records breaks at the 256-byte 0x40000100 record, and
 * 0x3540000a sits inside that record's payload (measured: raw record start 480
 * in wave28-percent-auto0-control.mpp, whose stream is 574 bytes; the found id
 * is at 484).  This writer therefore locates the id bytes `0a 00 40 35`
 * anywhere in the stream, requires them to occur exactly once, and validates
 * the framing around them (u32 length = 2, u32 id = 0x3540000a, u32 kind = 2).
 *
 * NOT EVERY FILE HAS ONE
 *
 * Every file Project 2021 has saved carries the record; the hand-built
 * fixtures (tests/data/synthetic-base.mpp, wave23-outline.mpp) have a Props14
 * stream without it, and Project never wrote them.  A missing record is
 * therefore "no measured marker to reproduce" and the writer leaves the stream
 * untouched -- it does NOT invent a record, because no measurement shows where
 * Project would put one in such a file.  A record that is present but framed
 * differently, occurs more than once, or holds a payload other than 0 or 1 is
 * a shape this writer refuses (throw Error, caller writes nothing).
 */

#ifndef MPPWRITE_EDIT_MARKER_H
#define MPPWRITE_EDIT_MARKER_H

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "cfb_writer.h"

namespace mppwrite {

/* The stream that carries Project's per-edit marker. */
inline constexpr const char* kEditMarkerStream = "Props14";
/* Record id, and the id bytes the writer searches for. */
inline constexpr std::uint32_t kEditMarkerRecord = 0x3540000Au;
inline constexpr std::uint16_t kEditMarkerAfterSave = 1;

struct EditMarker {
    bool stream_present = false; /* Props14 exists in the document */
    bool record_present = false; /* the 0x3540000a record was found */
    bool changed = false;        /* the payload byte pair was rewritten */
    std::uint16_t before = 0;
    std::uint16_t after = 0;
    std::size_t record_offset = 0; /* record start in the stream as read */
};

/*
 * Set the edit marker to 01 00 (Project's edit-save value).  Returns what was
 * found and whether the stream was replaced.  Refuses a present-but-damaged
 * marker; a missing Props14 stream or a missing record is a no-op.
 */
EditMarker write_edit_marker(CfbDocument& doc,
                             std::string_view path = kEditMarkerStream);

} /* namespace mppwrite */

#endif /* MPPWRITE_EDIT_MARKER_H */
