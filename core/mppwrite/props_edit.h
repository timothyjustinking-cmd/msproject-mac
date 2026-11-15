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
 *   derived  W36-2.1: a table's size word is the length of its Var2Data and
 *            its count word the u32 at +8 of its FixedMeta, as the save lays
 *            the tables out (all 8 words, 624/624 committed files; 214/Props
 *            carries only the CV_iew size, which viewstream_edit derives the
 *            same way).  props_table_words() computes them from the document
 *            the save has laid out, so the growth writers no longer carry a
 *            measured copy of each word.  The Task freed counter and the
 *            Assignment dirty flag are in no table and stay the caller's.
 *   insert   W36-2.2: an id absent from the stream is appended AT THE END of
 *            the stream, after the typed-property section, as
 *            [u32 length][u32 id][u32 kind-and-flags = 0][payload]; the two
 *            leading size words become stream_size - 4 and the record count
 *            word at +12 goes up by one per record (it IS the full record
 *            count: a walk of that many records from +16, 12 + length each,
 *            ends exactly at the stream end in 624/624 committed files, with
 *            no repeated id and no odd length).  Derived from the baseline
 *            trace (trace-props-framing-r4.md: FUN_140253fe4, not-found
 *            branch) and the wave30-baseline pair (id 0x02401393, the 98-byte
 *            "Used for Microsoft Project 98 Baseline Calendar" text, 110 B
 *            appended, count 262 -> 263); held out, never used to derive it:
 *            12 more pairs append that same record (wave35-g3-baseline-set-
 *            nested and the wave36-r7-w37-b-* pairs, all 110 B) and two
 *            append id 0x02400005, the summary note text (64 and 514 B
 *            payloads, wave36-r7-w37-c-note-summary-*).  The odd-length pad of
 *            the disassembly (+1) was never exercised by any file, so an odd
 *            payload is refused.  Only those two ids insert.
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
 *   insert      114/Props: ids 0x02401393 and 0x02400005 only (see "insert"
 *               above; W36-2.2 decoded the +110 B growth wave30-baseline
 *               showed as an appended record).  Every other id, a kind
 *               other than 0, an odd payload and 214/Props are refused.  The
 *               custom-field SET pair's 88-byte insert is in the per-table
 *               Props streams (114/TBkndTask/Props 86 -> 174), not in
 *               114/Props.
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
 *   - a record edit on 214/Props; a target id absent that is not one of the
 *     two inserted ids (W36-2.2), or an insert whose kind, length or text no
 *     pair measured, a stream that does not walk to its end by its count
 *     word, an id already after the typed-property section; a target id
 *     present twice,
 *     or outside the six a pair updated (0x0240138b, 0x024013be from
 *     wave30-wbs-code; 0x02401392, 0x024013c5 from wave34-field-set;
 *     0x02400003, 0x02400024 from wave35-calculate-unlinked; W35-0.4,
 *     RC-W7), with another kind, with an equal payload, with a smaller
 *     payload, or with a payload other than the measured one (RW35-W7: the
 *     value, or for the project-finish date its 4-byte length); a duplicate
 *     target id in the request;
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
inline constexpr std::uint32_t kPropsTaskCountTag = 0x01000001u;
inline constexpr std::uint32_t kPropsCalendarSizeTag = 0x03000100u;
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

/* W36-2.1: the four 114 tables whose size and count words the save derives.
 * The value is the table number N of the tags 0x0N000100 (size) and
 * 0x0N000001 (count). */
enum class PropsTable : std::uint8_t {
    Task = 1,
    Resource = 2,
    Calendar = 3,
    Assignment = 4,
};

/* The 114/Props size and count words of `tables`, computed from the streams
 * `laid_out` holds (the save has laid the tables out): size = the length of
 * <table>/Var2Data, count = the u32 at +8 of <table>/FixedMeta.  Measured on
 * all 8 words of the 624 committed files (every word equal, 624/624).  Only
 * the words that differ from the Props stream's stored value are returned, so
 * the result goes straight into PropsEdit::scalars; a table whose streams are
 * missing or whose Props word is absent throws Error.  The caller names the
 * tables its edit lays out: a table it does not touch keeps its stored words. */
std::vector<PropsScalarEdit> props_table_words(
    CfbDocument const& laid_out, std::vector<PropsTable> const& tables);

/* What the write proved from the file on disk. */
struct PropsRecordWrite {
    std::uint32_t id = 0;
    std::uint32_t kind = 0;
    std::size_t old_offset = 0;   /* the record header */
    std::size_t new_offset = 0;
    std::size_t old_payload_bytes = 0;
    std::size_t new_payload_bytes = 0;
    bool inserted = false;        /* W36-2.2: appended, so old_* are 0 */
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
    std::size_t records_inserted = 0; /* W36-2.2: appended at the stream end */
    std::size_t appended_bytes = 0;   /* 12 + payload of every appended record */
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
