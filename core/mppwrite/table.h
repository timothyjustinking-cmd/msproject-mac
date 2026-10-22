/*
 * table.h -- the record layer the writer needs: Task fixed rows and the
 * variable-value store, with the consistency rules that go with them.
 *
 * This is stage 3's foundation.  Stage 2 (inplace.h) can change a value that
 * already has a home of the same size.  Changing what a row *means* means
 * replacing a whole stream (114/TBkndTask/FixedData), and changing a string
 * means growing 114/TBkndTask/Var2Data.  Both are only safe if the bookkeeping
 * around them is updated, and the bookkeeping is what this file encodes:
 *
 *   FixedData  = 48-byte header + 202 * rows, row 0 = project row
 *   FixedMeta  = 16-byte header + 47 * (rows + 3) entries
 *                header +0x0c must equal the FixedData byte size
 *                entry[i].+4 must equal 48 + 202 * (i - 3) for i >= 3
 *   Var2Data   = concatenation of u32-length-prefixed blocks
 *   VarMeta    = 24-byte header + 12 * N entries, nothing after them
 *                header +0x14 must equal the Var2Data byte size
 *                entry[k].offset must point at a block start
 *
 * CORRECTION (2026-10-22, stream W21).  This file used to read VarMeta as a
 * 32-byte header followed by (field, mid, offset) entries and a 4-byte trailer.
 * That is wrong, and it worked only by accident: the 8-byte shift pairs entry
 * i's key with entry i+1's uid/offset, which for base.mpp happens to address the
 * right block when renaming uid 1 or uid 2.  The measured layout is 24 bytes of
 * header and (uid, offset, key) entries -- `core/mpp/reader.cpp` parses exactly
 * that and reproduces COM's task names on the real files.  Arithmetic on the
 * three files we have: base 528 = 24 + 42*12, PROJECT_A 12,996 = 24 + 1,081*12,
 * PROJECT_B 17,268 = 24 + 1,437*12; with a 32-byte header and a 4-byte trailer
 * none of those is an integer number of entries.  Consequence: the name key is
 * 0x0b40000e (the project row's block decodes to the project title), not
 * 0x0b400564 (a 4-byte field every row also has).
 *
 * The cheap way to grow a string is APPEND-ONLY: put the new block at the end
 * of Var2Data, repoint the one VarMeta entry, and update the Var2Data size in
 * the VarMeta header.  Every other block keeps its offset, so the other rows'
 * records do not have to be touched at all.  The old bytes stay behind as
 * garbage, which real files already contain (spec/mpp-records.md §5.3), so
 * readers must tolerate it — and they do, because offsets are what address the
 * stream.
 *
 * Adding a *new* VarMeta entry is not offered here.  `replace_var_block` keeps
 * the entry count fixed; growing the entry array belongs to the structural
 * writer, which must keep VarMeta's header count and every FixedMeta
 * field-count word in step.  That is `add_delete.h` (W45): one appended task row
 * adds 8 entries at the END of the stream, so no existing entry moves.
 *
 * Offsets are from spec/mpp-records.md §2, re-verified by
 * tests/roundtrip_cfb.cpp (check_base_fixture) and the one-change fixtures.
 */

#ifndef MPPWRITE_TABLE_H
#define MPPWRITE_TABLE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cfb_writer.h"

namespace mppwrite {

/* --- packed date/time (spec/mpp-records.md §3) --------------------------- */

inline constexpr std::uint32_t kTickSeconds = 6;
inline constexpr std::uint32_t kTicksPerDay = 4800; /* one 8-hour working day */
inline constexpr std::uint32_t kInvalidDate = 0xFFFFFFFFu;

/*
 * A date is one u32: (day << 16) | (seconds_since_midnight / 6), where day 0 is
 * 1983-12-31.  Verified examples: 0x3bf212c0 = day 15346, 4800 ticks =
 * 2026-01-05 08:00; 0x3d141482 = day 15636, 5250 ticks = 2026-10-22 08:45.
 */
struct PackedDate {
    int day = 0;            /* days since 1983-12-31 */
    int seconds_of_day = 0; /* 0 .. 86399, quantised to 6 seconds */

    bool operator==(PackedDate const&) const = default;
};

bool is_date(std::uint32_t raw);
PackedDate unpack_date(std::uint32_t raw);
std::uint32_t pack_date(PackedDate const& date);
/* Calendar convenience for tests and fixtures; month 1..12, day 1..31. */
std::uint32_t pack_date_ymd(int year, int month, int day,
                            int seconds_of_day = 0);
/* "2026-01-05 08:00", or "-" for the invalid value. */
std::string format_date(std::uint32_t raw);

/* --- TBkndTask fixed row (spec/mpp-records.md §1, §2) --------------------- */

namespace task_row {

inline constexpr std::size_t kHeaderSize = 48;
inline constexpr std::size_t kStride = 202;

/* Offsets inside one 202-byte row. */
inline constexpr std::size_t kRowId = 0;              /* u32, table order */
inline constexpr std::size_t kUniqueId = 4;           /* u32 */
inline constexpr std::size_t kConstraintType = 64;    /* u16, PjConstraint */
inline constexpr std::size_t kConstraintDate = 66;    /* u32 packed date */
inline constexpr std::size_t kTotalSlack = 74;        /* i32 ticks (W18) */
inline constexpr std::size_t kPriority = 78;          /* u16, default 500 */
inline constexpr std::size_t kActualDuration = 80;    /* u32 ticks */
inline constexpr std::size_t kDuration = 84;          /* u32 ticks */
inline constexpr std::size_t kRemainingDuration = 88; /* u32 ticks */
inline constexpr std::size_t kPercentComplete = 92;   /* u16, 0..100 */
inline constexpr std::size_t kStart = 96;             /* packed date */
inline constexpr std::size_t kFinish = 100;           /* packed date */
inline constexpr std::size_t kStart2 = 104;           /* second date copy */
inline constexpr std::size_t kFinish2 = 108;
inline constexpr std::size_t kStart3 = 112;           /* third date copy */
inline constexpr std::size_t kFinish3 = 116;
inline constexpr std::size_t kStartSlack = 186;       /* i32 ticks, = COM StartSlack */
inline constexpr std::size_t kTotalSlackCopy = 190;   /* i32 ticks, == +74 */
/*
 * +174 is the row's packed-date "start anchor" (spec/mpp-records.md §2).  W29
 * measured what Project does with it on its own save of a manual task: after
 * setting a Manual=True task's Start it wrote the new date here as well as into
 * +104/+108, +112/+116 and Fixed2Data +50/+54 (corpus/probe/
 * manual-start-edited.mpp vs base.mpp).  On an auto-scheduled row it holds the
 * unsnapped predecessor-finish bound, or 0xFFFFFFFF (w3-o1.mpp).
 */
inline constexpr std::size_t kStartAnchor = 174;      /* u32 packed date */

} /* namespace task_row */

/* Number of rows in a FixedData stream; throws when the size is not
 * 48 + 202 * n. */
std::size_t task_row_count(std::size_t fixed_data_bytes);
/* File-independent byte offset of a row inside FixedData. */
std::size_t task_row_offset(std::size_t row);

/*
 * A mutable view over one table's FixedData.  Default table path is
 * "114/TBkndTask"; the same layout applies to no other table.
 */
class TaskTable {
  public:
    static TaskTable from_document(CfbDocument const& doc,
                                   std::string_view table = "114/TBkndTask");

    std::vector<std::uint8_t> const& fixed_data() const { return fixed_; }
    std::size_t rows() const { return task_row_count(fixed_.size()); }
    std::size_t byte_size() const { return fixed_.size(); }

    std::uint32_t row_id(std::size_t row) const;
    std::uint32_t uid(std::size_t row) const;
    /* Index of the row with this UniqueID; throws Error when absent.  Row 0 is
     * the project row and is matched like any other (uid 0). */
    std::size_t row_for_uid(std::uint32_t uid) const;

    std::uint16_t u16(std::size_t row, std::size_t offset) const;
    std::uint32_t u32(std::size_t row, std::size_t offset) const;

    /*
     * Raw row bytes.  These are the only writers TaskTable offers, and they are
     * deliberately primitive: they know nothing about the copies a logical value
     * has elsewhere (114/TBkndTask/Fixed2Data, +88, +74/+186/+190), so a caller
     * using them owns the cross-stream consistency.  For a date or a duration
     * that is almost never what you want -- use `mppwrite::write_task_row`
     * (row_writer.h), which writes every copy.  The named single-copy setters
     * that used to live here (set_start, set_finish, set_duration,
     * set_remaining_duration) were removed for exactly that reason.
     */
    void set_u16(std::size_t row, std::size_t offset, std::uint16_t value);
    void set_u32(std::size_t row, std::size_t offset, std::uint32_t value);

    /* Fields with exactly one stored copy, so they need no coordination. */
    void set_priority(std::size_t row, std::uint16_t priority);
    void set_percent_complete(std::size_t row, std::uint16_t percent);

    /*
     * Write FixedData back into the container and keep FixedMeta consistent.
     * The row count must not change (FixedMeta would need entries added or
     * removed, which is not implemented); a same-size write only touches
     * FixedData.
     */
    void store_into(CfbDocument& doc,
                    std::string_view table = "114/TBkndTask") const;

  private:
    std::vector<std::uint8_t> fixed_;
};

/* --- variable values (spec/mpp-records.md §5) ---------------------------- */

inline constexpr std::size_t kVarMetaHeaderSize = 24;
inline constexpr std::size_t kVarMetaEntrySize = 12;
inline constexpr std::size_t kVarMetaVar2SizeOffset = 0x14;
/* Task.Name in MS Project's own files is field key 0x0b40000e: base.mpp's
 * project row points it at the block that decodes to "base", and task rows 1..3
 * at "Task A"/"Task B"/"Task C".  0x0b400564 -- used here until 2026-10-22 -- is
 * a different, 4-byte field that every row also has; see the correction note at
 * the top of this header. */
inline constexpr std::uint32_t kTaskNameFieldId = 0x0b40000eu;

/* One VarMeta entry, as stored: 24-byte header then 12 bytes per entry,
 * (uid, Var2Data offset, field key).  `field_id` is the key, `mid` is the uid
 * (spec/mpp-records.md §5.2: mid = row UniqueID, plus the 0x80000000|uid
 * family in some real files). */
struct VarEntry {
    std::size_t index = 0;      /* entry index, 0-based */
    std::uint32_t field_id = 0; /* key, e.g. 0x0b40000e = Name */
    std::uint32_t mid = 0;      /* uid */
    std::uint32_t offset = 0;   /* into Var2Data */
};

std::vector<VarEntry> read_var_entries(std::vector<std::uint8_t> const& var_meta);
/* Offsets of every entry matching (field key, uid), in file order. */
std::vector<std::uint32_t> find_var_offsets(std::vector<std::uint8_t> const& var_meta,
                                            std::uint32_t field_id,
                                            std::uint32_t mid);
/* Payload of the block at `offset` in Var2Data (length prefix stripped). */
std::vector<std::uint8_t> read_var_block(std::vector<std::uint8_t> const& var2_data,
                                         std::uint32_t offset);

/* UTF-16LE, NUL-terminated: the encoding Task.Name uses in Var2Data. */
std::vector<std::uint8_t> encode_utf16z(std::string_view utf8);
std::string decode_utf16z(std::vector<std::uint8_t> const& payload);
std::vector<std::uint8_t> encode_block(std::vector<std::uint8_t> const& payload);

/*
 * Append `payload` as a new Var2Data block and repoint every VarMeta entry for
 * (field_id, mid) at it.  Updates the Var2Data size in the VarMeta header.
 * Throws Error when the table has no such entry: a new field would need a new
 * entry, which changes the entry count (the entries end the stream; there is no
 * trailer) and is a later milestone.
 *
 * Everything else in both streams keeps its byte offset, so no other record has
 * to be rewritten; the old block is left in place as unreferenced bytes.
 */
void replace_var_block(CfbDocument& doc, std::string_view table,
                       std::uint32_t field_id, std::uint32_t mid,
                       std::vector<std::uint8_t> const& payload);

void replace_task_name(CfbDocument& doc, std::uint32_t uid,
                       std::string_view utf8,
                       std::string_view table = "114/TBkndTask");

std::string read_task_name(CfbDocument const& doc, std::uint32_t uid,
                           std::string_view table = "114/TBkndTask");

/* Replace a whole FixedData stream and keep the FixedMeta size in step.  This
 * is the "stream replacement" entry point for callers that build the row bytes
 * themselves. */
void store_task_fixed_data(CfbDocument& doc, std::vector<std::uint8_t> fixed_data,
                           std::string_view table = "114/TBkndTask");

/*
 * Cross-stream consistency report for one table: empty when
 *   - FixedMeta's +0x0c word equals the FixedData byte size,
 *   - FixedMeta's entry count equals the number of 47-byte entries present,
 *   - Fixed2Data is 64 * (rows + 3) bytes,
 *   - Fixed2Meta's +0x0c word equals the Fixed2Data byte size,
 *   - VarMeta is a 24-byte header plus a whole number of 12-byte entries,
 *   - VarMeta's +0x14 word equals the Var2Data byte size,
 *   - every VarMeta entry addresses a block that fits inside Var2Data.
 * A non-empty result names the first disagreement.  This is the executable form
 * of "what must stay consistent" in spec/mpp-writer.md.
 * (FixedMeta's and Fixed2Meta's declared ENTRY COUNTS are deliberately not
 * checked against the row count: PROJECT_A declares 87 and has 88 in both, and the
 * writer carries that damage through rather than repairing it.) */
std::string check_table(CfbDocument const& doc,
                        std::string_view table = "114/TBkndTask");

} /* namespace mppwrite */

#endif /* MPPWRITE_TABLE_H */
