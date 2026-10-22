/*
 * table.cpp -- Task fixed rows, packed dates, and the append-only variable
 * store.  See table.h for the layout and the consistency rules.
 */

#include "table.h"

#include <algorithm>

namespace mppwrite {
namespace {

/* Howard Hinnant's civil calendar arithmetic, days since 1970-01-01. */
int days_from_civil(int y, unsigned m, unsigned d) {
    y -= m <= 2 ? 1 : 0;
    int const era = (y >= 0 ? y : y - 399) / 400;
    unsigned const yoe = static_cast<unsigned>(y - era * 400);
    unsigned const doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned const doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int>(doe) - 719468;
}

void civil_from_days(int z, int& y, unsigned& m, unsigned& d) {
    z += 719468;
    int const era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned const doe = static_cast<unsigned>(z - era * 146097);
    unsigned const yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int const yy = static_cast<int>(yoe) + era * 400;
    unsigned const doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned const mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y = yy + (m <= 2 ? 1 : 0);
}

/* 1983-12-31 is day 0 of the packed date. */
int const kEpochSerial = days_from_civil(1983, 12, 31);

void put_u16(std::vector<std::uint8_t>& b, std::size_t o, std::uint16_t v) {
    b[o] = static_cast<std::uint8_t>(v & 0xFFu);
    b[o + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
}

void put_u32(std::vector<std::uint8_t>& b, std::size_t o, std::uint32_t v) {
    b[o] = static_cast<std::uint8_t>(v & 0xFFu);
    b[o + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
    b[o + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
    b[o + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
}

std::uint16_t get_u16(std::vector<std::uint8_t> const& b, std::size_t o) {
    return static_cast<std::uint16_t>(b[o] | (b[o + 1] << 8));
}

std::uint32_t get_u32(std::vector<std::uint8_t> const& b, std::size_t o) {
    return static_cast<std::uint32_t>(b[o]) |
           (static_cast<std::uint32_t>(b[o + 1]) << 8) |
           (static_cast<std::uint32_t>(b[o + 2]) << 16) |
           (static_cast<std::uint32_t>(b[o + 3]) << 24);
}

std::uint8_t const kMetaMagic[4] = {0xBA, 0xAD, 0xDF, 0xFA};

bool has_meta_magic(std::vector<std::uint8_t> const& b) {
    return b.size() >= 4 && std::equal(std::begin(kMetaMagic), std::end(kMetaMagic),
                                       b.begin());
}

std::string table_stream(std::string_view table, std::string_view name) {
    return std::string(table) + "/" + std::string(name);
}

/* Zero-padded decimal for a non-negative value: printf's "%0<width>u" field
 * without a fixed buffer, so a wider value is printed in full rather than
 * truncated. */
std::string zero_padded(unsigned value, std::size_t width) {
    std::string text = std::to_string(value);
    if (text.size() < width) {
        text.insert(0, width - text.size(), '0');
    }
    return text;
}

} /* namespace */

/* --- packed date --------------------------------------------------------- */

bool is_date(std::uint32_t raw) { return raw != 0 && raw != kInvalidDate; }

PackedDate unpack_date(std::uint32_t raw) {
    PackedDate out;
    out.day = static_cast<int>(raw >> 16);
    out.seconds_of_day = static_cast<int>((raw & 0xFFFFu) * kTickSeconds);
    return out;
}

std::uint32_t pack_date(PackedDate const& date) {
    if (date.day < 0 || date.day > 0xFFFF) {
        throw Error("packed date: day " + std::to_string(date.day) +
                    " does not fit in 16 bits");
    }
    if (date.seconds_of_day < 0 || date.seconds_of_day >= 86400) {
        throw Error("packed date: seconds of day out of range");
    }
    std::uint32_t const ticks =
        static_cast<std::uint32_t>(date.seconds_of_day) / kTickSeconds;
    return (static_cast<std::uint32_t>(date.day) << 16) | (ticks & 0xFFFFu);
}

std::uint32_t pack_date_ymd(int year, int month, int day, int seconds_of_day) {
    PackedDate date;
    date.day = days_from_civil(year, static_cast<unsigned>(month),
                               static_cast<unsigned>(day)) -
               kEpochSerial;
    date.seconds_of_day = seconds_of_day;
    return pack_date(date);
}

std::string format_date(std::uint32_t raw) {
    if (!is_date(raw)) {
        return "-";
    }
    PackedDate const date = unpack_date(raw);
    int y = 0;
    unsigned m = 0;
    unsigned d = 0;
    civil_from_days(date.day + kEpochSerial, y, m, d);
    /* A packed date's day is 16 bits, so the civil year is always positive
     * (1984..2163) and every component is non-negative.  Building the string
     * from zero-padded fields means there is no fixed buffer and therefore no
     * possible truncation -- which is what GCC's -Wformat-truncation= was
     * rightly pointing out about the old char[32] + snprintf version. */
    int const seconds = date.seconds_of_day;
    return zero_padded(static_cast<unsigned>(y), 4) + "-" + zero_padded(m, 2) +
           "-" + zero_padded(d, 2) + " " +
           zero_padded(static_cast<unsigned>(seconds / 3600), 2) + ":" +
           zero_padded(static_cast<unsigned>(seconds % 3600 / 60), 2);
}

/* --- fixed rows ---------------------------------------------------------- */

std::size_t task_row_count(std::size_t fixed_data_bytes) {
    if (fixed_data_bytes < task_row::kHeaderSize ||
        (fixed_data_bytes - task_row::kHeaderSize) % task_row::kStride != 0) {
        throw Error("FixedData is " + std::to_string(fixed_data_bytes) +
                    " bytes, not 48 + 202*n");
    }
    return (fixed_data_bytes - task_row::kHeaderSize) / task_row::kStride;
}

std::size_t task_row_offset(std::size_t row) {
    return task_row::kHeaderSize + row * task_row::kStride;
}

TaskTable TaskTable::from_document(CfbDocument const& doc,
                                   std::string_view table) {
    TaskTable out;
    out.fixed_ = doc.read_stream(table_stream(table, "FixedData"));
    (void)task_row_count(out.fixed_.size()); /* validates the size */
    return out;
}

std::uint32_t TaskTable::row_id(std::size_t row) const {
    return u32(row, task_row::kRowId);
}

std::uint32_t TaskTable::uid(std::size_t row) const {
    return u32(row, task_row::kUniqueId);
}

std::size_t TaskTable::row_for_uid(std::uint32_t wanted) const {
    for (std::size_t row = 0; row < rows(); ++row) {
        if (uid(row) == wanted) {
            return row;
        }
    }
    throw Error("no task row with UniqueID " + std::to_string(wanted));
}

std::uint16_t TaskTable::u16(std::size_t row, std::size_t offset) const {
    std::size_t const p = task_row_offset(row) + offset;
    if (p + 2 > fixed_.size()) {
        throw Error("u16 read past the end of FixedData");
    }
    return get_u16(fixed_, p);
}

std::uint32_t TaskTable::u32(std::size_t row, std::size_t offset) const {
    std::size_t const p = task_row_offset(row) + offset;
    if (p + 4 > fixed_.size()) {
        throw Error("u32 read past the end of FixedData");
    }
    return get_u32(fixed_, p);
}

void TaskTable::set_u16(std::size_t row, std::size_t offset, std::uint16_t value) {
    std::size_t const p = task_row_offset(row) + offset;
    if (p + 2 > fixed_.size()) {
        throw Error("u16 write past the end of FixedData");
    }
    put_u16(fixed_, p, value);
}

void TaskTable::set_u32(std::size_t row, std::size_t offset, std::uint32_t value) {
    std::size_t const p = task_row_offset(row) + offset;
    if (p + 4 > fixed_.size()) {
        throw Error("u32 write past the end of FixedData");
    }
    put_u32(fixed_, p, value);
}

void TaskTable::set_priority(std::size_t row, std::uint16_t priority) {
    set_u16(row, task_row::kPriority, priority);
}

void TaskTable::set_percent_complete(std::size_t row, std::uint16_t percent) {
    if (percent > 100) {
        throw Error("percent complete " + std::to_string(percent) + " > 100");
    }
    set_u16(row, task_row::kPercentComplete, percent);
}

/* set_duration / set_remaining_duration / set_start / set_finish are gone on
 * purpose: each of those values lives in more than one stream (Fixed2Data for
 * duration/start/finish, +88 for the duration copy, +74/+186/+190 for slack).
 * Writing one of them here is the single-copy patch spec/mpp-writer.md §8.1
 * measured MS Project ignoring.  Use write_task_row (row_writer.h). */

void TaskTable::store_into(CfbDocument& doc, std::string_view table) const {
    store_task_fixed_data(doc, fixed_, table);
}

void store_task_fixed_data(CfbDocument& doc, std::vector<std::uint8_t> fixed_data,
                           std::string_view table) {
    std::string const path = table_stream(table, "FixedData");
    std::string const meta_path = table_stream(table, "FixedMeta");
    std::vector<std::uint8_t> const old = doc.read_stream(path);
    std::size_t const old_rows = task_row_count(old.size());
    std::size_t const new_rows = task_row_count(fixed_data.size());
    if (old_rows != new_rows) {
        /*
         * Adding or removing a row would also require:
         *   FixedMeta   one 47-byte entry per row (offset 48 + 202*row, a
         *               16-byte identity blob, the u16 field count) and a
         *               header entry count bump;
         *   Fixed2Data  64-byte identity record per row (row GUID, level double,
         *               parent/previous id) -- 64 * (rows + 3);
         *   Fixed2Meta  one 96-byte entry per row -- 16 + 96 * (rows + 3);
         *   VarMeta     a mid group for the new UniqueID;
         *   FixedData   the new row itself, with row ids renumbered.
         * None of that is implemented; failing loudly is the honest option.
         */
        throw Error("row count change (" + std::to_string(old_rows) + " -> " +
                    std::to_string(new_rows) +
                    ") is not supported: FixedMeta, Fixed2Data and Fixed2Meta "
                    "all carry one record per row and would have to be "
                    "extended too");
    }
    std::vector<std::uint8_t> const meta = doc.read_stream(meta_path);
    if (has_meta_magic(meta) && meta.size() >= 16) {
        std::uint32_t const declared = get_u32(meta, 12);
        if (declared != old.size()) {
            throw Error("FixedMeta declares a FixedData size of " +
                        std::to_string(declared) + " but the stream is " +
                        std::to_string(old.size()) + " bytes");
        }
    }
    doc.replace_stream(path, fixed_data);
}

/* --- variable values ----------------------------------------------------- */

std::vector<VarEntry> read_var_entries(std::vector<std::uint8_t> const& var_meta) {
    std::vector<VarEntry> out;
    if (!has_meta_magic(var_meta) || var_meta.size() < kVarMetaHeaderSize) {
        throw Error("VarMeta has no 0xBAADFADF header");
    }
    /* 24-byte header, then (uid, offset, key) triples; the entries end the
     * stream.  Measured: base 528 = 24 + 42*12, PROJECT_A 12,996 = 24 + 1,081*12,
     * PROJECT_B 17,268 = 24 + 1,437*12. */
    if ((var_meta.size() - kVarMetaHeaderSize) % kVarMetaEntrySize != 0) {
        throw Error("VarMeta is " + std::to_string(var_meta.size()) +
                    " bytes, not 24 + 12*n");
    }
    std::size_t const n =
        (var_meta.size() - kVarMetaHeaderSize) / kVarMetaEntrySize;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        std::size_t const p = kVarMetaHeaderSize + i * kVarMetaEntrySize;
        VarEntry e;
        e.index = i;
        e.mid = get_u32(var_meta, p);         /* uid */
        e.offset = get_u32(var_meta, p + 4);  /* into Var2Data */
        e.field_id = get_u32(var_meta, p + 8); /* key */
        out.push_back(e);
    }
    return out;
}

std::vector<std::uint32_t> find_var_offsets(
    std::vector<std::uint8_t> const& var_meta, std::uint32_t field_id,
    std::uint32_t mid) {
    std::vector<std::uint32_t> out;
    for (VarEntry const& e : read_var_entries(var_meta)) {
        if (e.field_id == field_id && e.mid == mid) {
            out.push_back(e.offset);
        }
    }
    return out;
}

std::vector<std::uint8_t> read_var_block(
    std::vector<std::uint8_t> const& var2_data, std::uint32_t offset) {
    if (static_cast<std::size_t>(offset) + 4 > var2_data.size()) {
        throw Error("Var2Data block offset " + std::to_string(offset) +
                    " is past the stream");
    }
    std::uint32_t const length = get_u32(var2_data, offset);
    if (static_cast<std::size_t>(offset) + 4 + length > var2_data.size()) {
        throw Error("Var2Data block at " + std::to_string(offset) +
                    " runs past the stream");
    }
    return std::vector<std::uint8_t>(
        var2_data.begin() + static_cast<std::ptrdiff_t>(offset) + 4,
        var2_data.begin() + static_cast<std::ptrdiff_t>(offset) + 4 + length);
}

std::vector<std::uint8_t> encode_utf16z(std::string_view utf8) {
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; i < utf8.size();) {
        unsigned char const c = static_cast<unsigned char>(utf8[i]);
        std::uint32_t cp = 0;
        std::size_t extra = 0;
        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xE0) == 0xC0) {
            cp = c & 0x1Fu;
            extra = 1;
        } else if ((c & 0xF0) == 0xE0) {
            cp = c & 0x0Fu;
            extra = 2;
        } else {
            cp = c & 0x07u;
            extra = 3;
        }
        ++i;
        for (std::size_t k = 0; k < extra && i < utf8.size(); ++k, ++i) {
            cp = (cp << 6) | (static_cast<unsigned char>(utf8[i]) & 0x3Fu);
        }
        if (cp < 0x10000) {
            out.push_back(static_cast<std::uint8_t>(cp & 0xFFu));
            out.push_back(static_cast<std::uint8_t>(cp >> 8));
        } else {
            cp -= 0x10000;
            std::uint32_t const high = 0xD800 + (cp >> 10);
            std::uint32_t const low = 0xDC00 + (cp & 0x3FF);
            out.push_back(static_cast<std::uint8_t>(high & 0xFFu));
            out.push_back(static_cast<std::uint8_t>(high >> 8));
            out.push_back(static_cast<std::uint8_t>(low & 0xFFu));
            out.push_back(static_cast<std::uint8_t>(low >> 8));
        }
    }
    out.push_back(0);
    out.push_back(0);
    return out;
}

namespace {

/* The uid a VarMeta `mid` belongs to: real files use `uid` and the
 * `0x80000000|uid` family, which Project keeps together for one row. */
std::uint32_t base_uid(std::uint32_t mid) { return mid & 0x7FFFFFFFu; }
bool is_secondary_uid(std::uint32_t mid) { return (mid & 0x80000000u) != 0; }

} /* namespace */

std::size_t var_entry_insert_offset(std::vector<std::uint8_t> const& var_meta,
                                    std::uint32_t mid, std::uint32_t field_id) {
    std::vector<VarEntry> const entries = read_var_entries(var_meta);
    std::uint32_t const wanted_uid = base_uid(mid);
    bool const wanted_secondary = is_secondary_uid(mid);
    /* The array is sorted by (uid, secondary family, field key) in Project's own
     * files, so the new entry goes before the first entry that sorts after it.
     * That keeps a uid's entries contiguous (the property Project needs) and
     * key-ascending inside the group. */
    for (std::size_t i = 0; i < entries.size(); ++i) {
        VarEntry const& entry = entries[i];
        std::uint32_t const uid = base_uid(entry.mid);
        bool const secondary = is_secondary_uid(entry.mid);
        bool const after = uid > wanted_uid ||
                           (uid == wanted_uid && secondary > wanted_secondary) ||
                           (uid == wanted_uid && secondary == wanted_secondary &&
                            entry.field_id > field_id);
        if (after) {
            return kVarMetaHeaderSize + i * kVarMetaEntrySize;
        }
    }
    return var_meta.size();
}

void insert_var_entry(std::vector<std::uint8_t>& var_meta, std::uint32_t mid,
                      std::uint32_t field_id, std::uint32_t offset) {
    std::size_t const at = var_entry_insert_offset(var_meta, mid, field_id);
    std::vector<std::uint8_t> entry(kVarMetaEntrySize, 0);
    entry[0] = static_cast<std::uint8_t>(mid & 0xFFu);
    entry[1] = static_cast<std::uint8_t>((mid >> 8) & 0xFFu);
    entry[2] = static_cast<std::uint8_t>((mid >> 16) & 0xFFu);
    entry[3] = static_cast<std::uint8_t>((mid >> 24) & 0xFFu);
    entry[4] = static_cast<std::uint8_t>(offset & 0xFFu);
    entry[5] = static_cast<std::uint8_t>((offset >> 8) & 0xFFu);
    entry[6] = static_cast<std::uint8_t>((offset >> 16) & 0xFFu);
    entry[7] = static_cast<std::uint8_t>((offset >> 24) & 0xFFu);
    entry[8] = static_cast<std::uint8_t>(field_id & 0xFFu);
    entry[9] = static_cast<std::uint8_t>((field_id >> 8) & 0xFFu);
    entry[10] = static_cast<std::uint8_t>((field_id >> 16) & 0xFFu);
    entry[11] = static_cast<std::uint8_t>((field_id >> 24) & 0xFFu);
    var_meta.insert(var_meta.begin() + static_cast<std::ptrdiff_t>(at), entry.begin(),
                    entry.end());
}

std::string decode_utf16z(std::vector<std::uint8_t> const& payload) {
    std::string out;
    for (std::size_t i = 0; i + 1 < payload.size(); i += 2) {
        std::uint32_t unit = static_cast<std::uint32_t>(payload[i]) |
                             (static_cast<std::uint32_t>(payload[i + 1]) << 8);
        if (unit == 0) {
            break;
        }
        if (unit < 0x80) {
            out.push_back(static_cast<char>(unit));
        } else if (unit < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (unit >> 6)));
            out.push_back(static_cast<char>(0x80 | (unit & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (unit >> 12)));
            out.push_back(static_cast<char>(0x80 | ((unit >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (unit & 0x3F)));
        }
    }
    return out;
}

std::vector<std::uint8_t> encode_block(std::vector<std::uint8_t> const& payload) {
    std::vector<std::uint8_t> out(4 + payload.size(), 0);
    put_u32(out, 0, static_cast<std::uint32_t>(payload.size()));
    std::copy(payload.begin(), payload.end(), out.begin() + 4);
    return out;
}

void replace_var_block(CfbDocument& doc, std::string_view table,
                       std::uint32_t field_id, std::uint32_t mid,
                       std::vector<std::uint8_t> const& payload) {
    std::string const var2_path = table_stream(table, "Var2Data");
    std::string const meta_path = table_stream(table, "VarMeta");
    std::vector<std::uint8_t> var2 = doc.read_stream(var2_path);
    std::vector<std::uint8_t> var_meta = doc.read_stream(meta_path);
    if (!has_meta_magic(var_meta) ||
        var_meta.size() < kVarMetaHeaderSize + kVarMetaEntrySize) {
        throw Error("VarMeta is too small or has no header");
    }
    std::vector<VarEntry> const entries = read_var_entries(var_meta);
    std::vector<std::size_t> matches;
    for (VarEntry const& e : entries) {
        if (e.field_id == field_id && e.mid == mid) {
            matches.push_back(e.index);
        }
    }
    if (matches.empty()) {
        throw Error("no VarMeta entry for field 0x" +
                    std::to_string(field_id) + " / mid " + std::to_string(mid) +
                    ": adding an entry is not implemented (the entries end the "
                    "stream, so a new one would have to be inserted before the "
                    "last entry and every other offset re-based)");
    }
    std::uint32_t const offset = static_cast<std::uint32_t>(var2.size());
    std::vector<std::uint8_t> const block = encode_block(payload);
    var2.insert(var2.end(), block.begin(), block.end());
    for (std::size_t const index : matches) {
        /* entry = 12 bytes: uid (+0), offset (+4), key (+8) */
        put_u32(var_meta, kVarMetaHeaderSize + index * kVarMetaEntrySize + 4,
                offset);
    }
    put_u32(var_meta, kVarMetaVar2SizeOffset,
            static_cast<std::uint32_t>(var2.size()));
    doc.replace_stream(var2_path, var2);
    doc.replace_stream(meta_path, var_meta);
}

void replace_task_name(CfbDocument& doc, std::uint32_t uid,
                       std::string_view utf8, std::string_view table) {
    replace_var_block(doc, table, kTaskNameFieldId, uid,
                      encode_utf16z(utf8));
}

std::string check_table(CfbDocument const& doc, std::string_view table) {
    try {
        std::string const fixed_path = table_stream(table, "FixedData");
        if (!doc.has_stream(fixed_path)) {
            return {}; /* table not present in this file */
        }
        std::vector<std::uint8_t> const fixed = doc.read_stream(fixed_path);
        std::vector<std::uint8_t> const meta =
            doc.read_stream(table_stream(table, "FixedMeta"));
        std::vector<std::uint8_t> const var2 =
            doc.read_stream(table_stream(table, "Var2Data"));
        std::vector<std::uint8_t> const var_meta =
            doc.read_stream(table_stream(table, "VarMeta"));
        if (!has_meta_magic(meta) || meta.size() < 16) {
            return "FixedMeta has no BAADFADF header";
        }
        if (get_u32(meta, 12) != fixed.size()) {
            return "FixedMeta +0x0c says " + std::to_string(get_u32(meta, 12)) +
                   " bytes but FixedData is " + std::to_string(fixed.size());
        }
        std::size_t const meta_entries = (meta.size() - 16) / 47;
        if (get_u32(meta, 8) != meta_entries) {
            return "FixedMeta header says " + std::to_string(get_u32(meta, 8)) +
                   " entries but " + std::to_string(meta_entries) + " are present";
        }
        std::size_t const rows = task_row_count(fixed.size());
        if (meta_entries != rows + 3) {
            return "FixedMeta has " + std::to_string(meta_entries) +
                   " entries for " + std::to_string(rows) + " rows (expected " +
                   std::to_string(rows + 3) + ")";
        }
        /* Fixed2Data/Fixed2Meta: the parallel identity table, 64-byte record
         * per row (record index = row + 3) and 96-byte entries.  The writer
         * writes Fixed2Data, so its size words must stay in step too. */
        std::string const fixed2_path = table_stream(table, "Fixed2Data");
        if (doc.has_stream(fixed2_path)) {
            std::vector<std::uint8_t> const fixed2 = doc.read_stream(fixed2_path);
            if (fixed2.size() != 64 * (rows + 3)) {
                return "Fixed2Data is " + std::to_string(fixed2.size()) +
                       " bytes for " + std::to_string(rows) + " rows (expected " +
                       std::to_string(64 * (rows + 3)) + ")";
            }
            std::string const fixed2_meta_path =
                table_stream(table, "Fixed2Meta");
            if (doc.has_stream(fixed2_meta_path)) {
                std::vector<std::uint8_t> const fixed2_meta =
                    doc.read_stream(fixed2_meta_path);
                if (!has_meta_magic(fixed2_meta) || fixed2_meta.size() < 16) {
                    return "Fixed2Meta has no BAADFADF header";
                }
                if (get_u32(fixed2_meta, 12) != fixed2.size()) {
                    return "Fixed2Meta +0x0c says " +
                           std::to_string(get_u32(fixed2_meta, 12)) +
                           " bytes but Fixed2Data is " +
                           std::to_string(fixed2.size());
                }
            }
        }
        if (!has_meta_magic(var_meta) ||
            var_meta.size() < kVarMetaHeaderSize) {
            return "VarMeta has no BAADFADF header";
        }
        if ((var_meta.size() - kVarMetaHeaderSize) % kVarMetaEntrySize != 0) {
            return "VarMeta is " + std::to_string(var_meta.size()) +
                   " bytes, not 24 + 12*n";
        }
        std::uint32_t const declared =
            get_u32(var_meta, kVarMetaVar2SizeOffset);
        if (declared != var2.size()) {
            return "VarMeta +0x14 says " + std::to_string(declared) +
                   " bytes but Var2Data is " + std::to_string(var2.size());
        }
        for (VarEntry const& e : read_var_entries(var_meta)) {
            if (static_cast<std::size_t>(e.offset) + 4 > var2.size()) {
                return "VarMeta entry " + std::to_string(e.index) +
                       " offset " + std::to_string(e.offset) +
                       " is past the end of Var2Data";
            }
            std::uint32_t const length = get_u32(var2, e.offset);
            if (static_cast<std::size_t>(e.offset) + 4 + length > var2.size()) {
                return "VarMeta entry " + std::to_string(e.index) +
                       " block runs past the end of Var2Data";
            }
        }
        return {};
    } catch (Error const& e) {
        return e.what();
    }
}

std::string read_task_name(CfbDocument const& doc, std::uint32_t uid,
                           std::string_view table) {
    std::vector<std::uint8_t> const var_meta =
        doc.read_stream(table_stream(table, "VarMeta"));
    std::vector<std::uint8_t> const var2 =
        doc.read_stream(table_stream(table, "Var2Data"));
    std::vector<std::uint32_t> const offsets =
        find_var_offsets(var_meta, kTaskNameFieldId, uid);
    if (offsets.empty()) {
        return {};
    }
    return decode_utf16z(read_var_block(var2, offsets.front()));
}

} /* namespace mppwrite */
