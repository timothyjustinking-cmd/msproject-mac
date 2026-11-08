/*
 * props_edit.h -- update records and typed-property scalars in `114/Props`
 * or `214/Props`.  W34-2.2 (30 October 2026).
 *
 * WHY THIS EXISTS.  Project's saves keep the document's directory properties in
 * the `114/Props` stream (back-end document) and `214/Props` (front-end
 * document): a header, a sequence of (length, id, kind, payload) records, and
 * an opaque tail that carries, per table, 16-byte typed-property scalars
 * `[LE u32 4][tag, BIG-endian][LE u32 4][LE u32 value]`.  Several measured
 * edits change only that stream: the WBS code definition updates two records
 * and the Task size scalar, Add Resources moves three count/size words, and
 * the Bold cell changes the CV_iew size scalar.  Until now nothing could write
 * a record; this writer can, for the measured shapes.
 *
 * THE MEASURED RULES (full derivation in spec/writers/props-records.md; every
 * pair has a null save, so per-save noise is masked and only the edit's effects
 * are compared):
 *
 *   header   16 B: +0 and +4 = stream_size - 4 (measured on both streams),
 *            +8 = 0x00010000, +12 = the undecoded "declared entries" word,
 *            preserved verbatim (it is NOT the record count: the reference
 *            114/Props has 161 records and 262 there, and stays 262 through a
 *            record update; wave30-baseline moves it to 263, which is one more
 *            reason insertion is not this writer's shape).
 *   records  from +16, back to back, no padding: u32 payload_length, u32 id
 *            (byte 2 is 0x40 on every measured id), u32 kind, payload.  The
 *            record area ends at the first offset that is not a record
 *            header; the rest of the stream is the tail.  A record update
 *            keeps the id, kind and position, and the new payload may be
 *            equal to or LARGER than the old one; every later record and the
 *            tail then move by the delta (no padding absorbs it) and both
 *            header size words become new_size - 4.  Pair: wave30-wbs-code
 *            (114/Props 88,724 -> 88,748, records 0x0240138b 4 -> 24 and
 *            0x024013be 2 -> 6, record area 3,316 -> 3,340, tail 85,408 B on
 *            both sides - moved, not grown) and wave34-field-set (0x02401392
 *            equal-size 2 -> 2, 0x024013c5 8 -> 16).
 *   scalars  a whitelisted 16-byte scalar in the tail has only its value word
 *            rewritten; the stream length, every record and every other tail
 *            byte stay.  Pairs: wave30-wbs-code (Task size tag BE 0x01000100
 *            512 -> 552), wave34-field-set (530 -> 574), Add Resources
 *            (Resource count 0x02000001 5 -> 6, Resource size 0x02000100
 *            86 -> 140, Calendar count 0x03000001 7 -> 8), textstyle
 *            (214/Props CV_iew size 0x01000100 100,178 -> 100,234).  The
 *            Task freed word (BE 0x01000002, 0 -> payload bytes on a var
 *            removal) is measured through W34-1.2's pairs.
 *   marker   The Props14 edit marker (edit_marker.h) is written on every save,
 *            as every measured editor save writes it.
 *
 * WHAT IS NOT TOUCHED.  The tail's field-reference map, the per-table Props
 * streams, every table, 214/CEdl and every other stream.  The writer proves
 * after saving that only {the edited Props stream, Props14} moved.
 *
 * THE CAPABILITY MATRIX (brief rule 1: a command is enabled only for a shape a
 * pair measures end to end, with the null save separating noise from the edit,
 * and every other shape refused up front):
 *
 *   stream     records                          scalars
 *   114/Props  update, existing id, kind kept,  BE 0x01000100, 0x01000002,
 *              payload equal or larger          0x02000001, 0x02000100,
 *                                               0x03000001, 0x04000001,
 *                                               0x04000002, 0x04008000,
 *                                               0x04000100
 *   214/Props  refused (no pair)               BE 0x01000100 only
 *   any other  refused                          refused
 *
 *   insert      refused on both streams.  No measured pair inserts a directory
 *               record: wave30-wbs-code and wave34-field-set UPDATE records
 *               that exist in the control (161 records on every side), and
 *               wave30-baseline, the one pair whose 114/Props grows (+110 B),
 *               keeps its 161 records and grows only the tail, in a section
 *               the decode note cannot yet rebuild (its +12 word 262 -> 263
 *               and an undecoded +110 B tail growth).  The custom-field SET
 *               pair's 88-byte insert is in the per-table Props streams
 *               (114/TBkndTask/Props 86 -> 174), not in 114/Props.
 *   shrink      refused: no pair shrinks a record payload (wbs and field-set
 *               grow or keep the size); a shrink would be a different rule
 *               until a pair measures it.
 *   kind change refused: every measured update keeps the kind.
 *   no change   refused: an equal payload or an equal scalar value is not an
 *               edit, and writing it would only mask a caller's mistake.
 *
 * Refusals (all before `doc` or the output path is touched; every one has a
 * test in tests/roundtrip_props.cpp):
 *   - an empty or unusable output path; no record and no scalar edit; a
 *     stream other than kPropsBackEnd/kPropsFrontEnd;
 *   - a stream shorter than its 16-byte header, size words that are not
 *     stream_size - 4, a format word that is not 0x00010000, or a record whose
 *     length runs past the stream end (the 0x00400000 id family is how the
 *     record area ends and the tail begins);
 *   - a record edit on 214/Props; a target id absent (insert), present twice,
 *     or outside the four a pair updated (0x0240138b, 0x024013be from
 *     wave30-wbs-code; 0x02401392, 0x024013c5 from wave34-field-set; W35-0.4,
 *     RC-W7), with another kind, with an equal payload, or with a smaller
 *     payload; a duplicate target id in the request;
 *   - a scalar tag outside the stream's whitelist, a duplicate tag in the
 *     request, no scalar with the tag, the tag twice in the stream, a hit
 *     outside the tail, or a value equal to the current one.
 *
 * ALL-OR-NOTHING AND PROVEN FROM DISK.  The edit is applied to a copy of the
 * document; the package is written to `path`, re-opened and required to hold
 * exactly the bytes built, re-emit byte-identically, pass CfbDocument::check,
 * contain exactly the requested state (each edited record's new offset and
 * payload, every other record byte-identical, the tail moved with only the
 * edited value words changed, both size words, the +8/+12 words preserved),
 * the Props14 marker, and no changed stream outside {the edited stream,
 * Props14}.  On success `doc` is replaced by the re-read document.
 */

#ifndef MPPWRITE_PROPS_EDIT_H
#define MPPWRITE_PROPS_EDIT_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "cfb_writer.h"

namespace mppwrite {

/* The two directory property streams. */
inline constexpr char const* kPropsBackEnd = "114/Props";
inline constexpr char const* kPropsFrontEnd = "214/Props";

/* The measured 16-byte scalar tags, read as the BIG-endian u32 the bytes
 * spell on disk (the same convention varmeta_edit.h uses).  The decode note
 * writes them little-endian (0x00010001 etc.); it is the same four bytes. */
inline constexpr std::uint32_t kPropsTaskSizeTag = 0x01000100u;
inline constexpr std::uint32_t kPropsTaskFreedTag = 0x01000002u;
inline constexpr std::uint32_t kPropsResourceCountTag = 0x02000001u;
inline constexpr std::uint32_t kPropsResourceSizeTag = 0x02000100u;
inline constexpr std::uint32_t kPropsCalendarCountTag = 0x03000001u;
/* The assignment table's four words, measured through W34-3.1's pairs: the
 * FixedMeta header count, the cumulative removed-payload ("freed") counter,
 * the dirty flag and the Var2Data size. */
inline constexpr std::uint32_t kPropsAssignmentCountTag = 0x04000001u;
inline constexpr std::uint32_t kPropsAssignmentFreedTag = 0x04000002u;
inline constexpr std::uint32_t kPropsAssignmentDirtyTag = 0x04008000u;
inline constexpr std::uint32_t kPropsAssignmentSizeTag = 0x04000100u;

/* One record update: the id must already exist with this kind, and the new
 * payload must be equal to or larger than the stored one. */
struct PropsRecordEdit {
    std::uint32_t id = 0;
    std::uint32_t kind = 0;
    std::vector<std::uint8_t> payload;
};

/* One typed-property scalar update: `tag` is the BIG-endian u32, `value` the
 * new little-endian value word. */
struct PropsScalarEdit {
    std::uint32_t tag = 0;
    std::uint32_t value = 0;
};

/* One save's edits on one stream.  At least one record or scalar edit is
 * required; record edits apply in the stream's order (the request order is
 * not a re-ordering request and is ignored except for duplicates). */
struct PropsEdit {
    std::string stream = kPropsBackEnd;
    std::vector<PropsRecordEdit> records;
    std::vector<PropsScalarEdit> scalars;
};

/* What the write proved from the file on disk. */
struct PropsRecordWrite {
    std::uint32_t id = 0;
    std::uint32_t kind = 0;
    std::size_t old_offset = 0;   /* the record header */
    std::size_t new_offset = 0;
    std::size_t old_payload_bytes = 0;
    std::size_t new_payload_bytes = 0;
};

struct PropsEditWrite {
    std::string stream;
    std::size_t records_before = 0;
    std::size_t records_after = 0;
    std::size_t stream_bytes_before = 0;
    std::size_t stream_bytes_after = 0;
    std::size_t tail_offset_before = 0;
    std::size_t tail_offset_after = 0;
    std::size_t scalars_updated = 0;
    std::vector<PropsRecordWrite> record_writes;
};

/*
 * Apply `edit` to `doc`, write the package to `path`, re-open it and prove
 * every fact above from the bytes.  Throws Error on a refusal or a mismatch;
 * then neither `doc` nor a new output file has been left behind.
 */
PropsEditWrite save_props_edit(CfbDocument& doc, PropsEdit const& edit,
                               std::string const& path);

} /* namespace mppwrite */

#endif /* MPPWRITE_PROPS_EDIT_H */
