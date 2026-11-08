/*
 * resource_growth.cpp -- W34-3.2: the measured Add Resources shape.  The rules,
 * the measured offsets and the refusals are in resource_growth.h.
 */

#include "resource_growth.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "edit_marker.h"
#include "props_edit.h"

namespace mppwrite {
namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr char const* kRscFixedData = "114/TBkndRsc/FixedData";
constexpr char const* kRscFixedMeta = "114/TBkndRsc/FixedMeta";
constexpr char const* kRscFixed2Data = "114/TBkndRsc/Fixed2Data";
constexpr char const* kRscFixed2Meta = "114/TBkndRsc/Fixed2Meta";
constexpr char const* kRscVarMeta = "114/TBkndRsc/VarMeta";
constexpr char const* kRscVar2Data = "114/TBkndRsc/Var2Data";
constexpr char const* kCalFixedData = "114/TBkndCal/FixedData";
constexpr char const* kCalFixedMeta = "114/TBkndCal/FixedMeta";
constexpr char const* kCalFixed2Data = "114/TBkndCal/Fixed2Data";
constexpr char const* kCalFixed2Meta = "114/TBkndCal/Fixed2Meta";

constexpr std::size_t kMetaHeaderBytes = 16;
constexpr std::uint32_t kTableMagic = 0xFADFADBAu;

constexpr std::size_t kRscHeaderBytes = 48;
constexpr std::size_t kRscRowBytes = 172;
constexpr std::size_t kRscFmEntryBytes = 37;
constexpr std::size_t kRscF2RecordBytes = 40;
constexpr std::size_t kRscF2mEntryBytes = 51;
constexpr std::size_t kRscDescriptors = 3;
constexpr std::size_t kRscRowId = 0;
constexpr std::size_t kRscRowUid = 4;
constexpr std::size_t kRscRowUnits = 8;
constexpr std::size_t kRscRowCounter = 154;

constexpr std::size_t kCalHeaderBytes = 64;
constexpr std::size_t kCalRowBytes = 12;
constexpr std::size_t kCalMetaEntryBytes = 10;
constexpr std::size_t kCalSlotBytes = 48;
constexpr std::size_t kCalSpareSlots = 4;

constexpr std::size_t kVarMetaHeaderBytes = 24;
constexpr std::size_t kVarMetaEntryBytes = 12;
constexpr std::size_t kVar2SizeOffset = 0x14;

constexpr std::uint32_t kNameKey = 0x0C400001u;
constexpr std::uint32_t kInitialsKey = 0x0C400002u;
constexpr std::uint32_t kSmallKey = 0x0C400055u;
constexpr std::uint32_t kCreatedKey = 0x0C4002D6u;
constexpr std::uint32_t kBlobKey = 0x0C4002E3u;

[[noreturn]] void fail(std::string const& message) { throw Error(message); }

std::uint16_t get_u16(Bytes const& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bytes[offset]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1])
                                   << 8));
}

std::uint32_t get_u32(Bytes const& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

std::uint32_t get_big_u32(Bytes const& bytes, std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
           static_cast<std::uint32_t>(bytes[offset + 3]);
}

double get_f64(Bytes const& bytes, std::size_t offset) {
    std::uint64_t bits = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        bits |= static_cast<std::uint64_t>(bytes[offset + i]) << (8 * i);
    }
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

void put_u16(Bytes& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFFu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
}

void put_u32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[offset + i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu);
    }
}

void append_u32(Bytes& bytes, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        bytes.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu));
    }
}

void append_f64(Bytes& bytes, double value) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    for (std::size_t i = 0; i < 8; ++i) {
        bytes.push_back(static_cast<std::uint8_t>((bits >> (8 * i)) & 0xFFu));
    }
}

Bytes slice(Bytes const& bytes, std::size_t offset, std::size_t length) {
    return Bytes(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                 bytes.begin() + static_cast<std::ptrdiff_t>(offset + length));
}

/* The template row, FixedMeta entry and Fixed2Meta entry of a resource the
 * Add Resources pairs measured (the last resource of the wave30 and wave34
 * controls: a work resource at 100 %, no work, no cost, no assignment, standard
 * calendar).  Both controls agree byte for byte outside the masked bytes, which
 * the writer rewrites for the new resource: the row's id and uid (0..7) and its
 * counter (154..157); the entry's session bit (0), file offset (4..7) and
 * state bytes (12..13); the Fixed2 entry's file offset and first state byte
 * (4..8).  Anything else the writer would clone from a different last resource
 * (work, cost, actual and remaining figures, accrual, type, calendar reference,
 * flags) is a shape no pair measured. */
constexpr char const* kTemplateRowHex =
    "0000000000000000000000000088c34000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000003000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000ffff"
    "ffffffffffff020002000000";

constexpr char const* kTemplateFixedMetaHex =
    "0000050000000000e3fffd3f0000000000000000000000000000000000000000"
    "0000000000";

constexpr char const* kTemplateFixed2MetaHex =
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000c000000000000000000000000000";

Bytes from_hex(std::string const& hex) {
    Bytes out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        out.push_back(
            static_cast<std::uint8_t>(std::stoi(hex.substr(i, 2), nullptr, 16)));
    }
    return out;
}

/* The first byte of `got` that differs from `want`, ignoring the masked ranges
 * [lo, hi), or `want.size()` if none does. */
std::size_t first_template_difference(
    Bytes const& got, Bytes const& want,
    std::initializer_list<std::pair<std::size_t, std::size_t>> masked) {
    for (std::size_t i = 0; i < want.size(); ++i) {
        bool skip = false;
        for (auto const& range : masked) {
            skip = skip || (i >= range.first && i < range.second);
        }
        if (!skip && got[i] != want[i]) return i;
    }
    return want.size();
}

std::string hex32(std::uint32_t value) {
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "%08x", value);
    return buffer;
}

/* Same policy as the other writers. */
void check_output_path(std::string const& path, std::string const& function) {
    if (path.empty()) {
        fail(function + ": the output path is empty");
    }
    std::filesystem::path const out(path);
    if (out.filename().empty()) {
        fail(function + ": " + path + " names a directory, not a file to save to");
    }
    std::error_code error;
    if (std::filesystem::is_directory(out, error)) {
        fail(function + ": " + path + " is a directory");
    }
    std::filesystem::path const parent = out.parent_path();
    if (parent.empty()) {
        return;
    }
    error.clear();
    bool const parent_exists = std::filesystem::exists(parent, error);
    if (error || !parent_exists) {
        fail(function + ": cannot save to " + path + ": the directory " +
             parent.string() + " does not exist");
    }
}

/* Stream components carry right-alignment spaces in some documents ("   114");
 * strip them so the changed-set proof names a stream the way this file does. */
std::string normalise_stream_path(std::string const& path) {
    std::string out;
    std::size_t start = 0;
    while (start <= path.size()) {
        std::size_t const slash = path.find('/', start);
        std::string const component =
            path.substr(start, slash == std::string::npos ? std::string::npos
                                                          : slash - start);
        std::size_t at = 0;
        while (at < component.size() &&
               (component[at] == ' ' ||
                static_cast<unsigned char>(component[at]) < 0x20)) {
            ++at;
        }
        if (!out.empty()) {
            out += '/';
        }
        out += component.substr(at);
        if (slash == std::string::npos) {
            break;
        }
        start = slash + 1;
    }
    return out;
}

Bytes read_required(CfbDocument const& doc, char const* path,
                    std::string const& function) {
    if (!doc.has_stream(path)) {
        fail(function + ": the document has no " + path + " stream");
    }
    return doc.read_stream(path);
}

/* A 16-byte table-meta header: magic, flag, count, data size.  `entries` is the
 * number of entries the stream length implies. */
std::size_t check_meta(Bytes const& meta, std::size_t entry_bytes,
                       std::size_t data_size, char const* what,
                       std::string const& function) {
    if (meta.size() < kMetaHeaderBytes ||
        (meta.size() - kMetaHeaderBytes) % entry_bytes != 0) {
        fail(function + ": " + what + " is not a 16-byte header plus " +
             std::to_string(entry_bytes) + "-byte entries");
    }
    if (get_u32(meta, 0) != kTableMagic) {
        fail(function + ": " + what + " does not start with the table magic");
    }
    std::size_t const entries = (meta.size() - kMetaHeaderBytes) / entry_bytes;
    if (get_u32(meta, 8) != entries || get_u32(meta, 12) != data_size) {
        fail(function + ": " + what + " header count/size disagree with its "
             "table");
    }
    return entries;
}

std::uint32_t props_scalar(Bytes const& props, std::uint32_t tag,
                           std::string const& function) {
    constexpr std::size_t kHeader = 16;
    std::optional<std::size_t> found;
    for (std::size_t at = kHeader; at + 16 <= props.size(); ++at) {
        if (get_u32(props, at) != 4 || get_u32(props, at + 8) != 4 ||
            get_big_u32(props, at + 4) != tag) {
            continue;
        }
        if (found) {
            fail(function + ": the Props scalar 0x" + hex32(tag) +
                 " appears more than once");
        }
        found = at;
    }
    if (!found) {
        fail(function + ": the Props stream has no 0x" + hex32(tag) + " scalar");
    }
    return get_u32(props, *found + 12);
}

struct VarEntry {
    std::uint32_t uid = 0;
    std::uint32_t offset = 0;
    std::uint32_t key = 0;
};

std::vector<VarEntry> load_var_meta(Bytes const& var_meta, Bytes const& var2,
                                    std::string const& function) {
    if (var_meta.size() < kVarMetaHeaderBytes ||
        (var_meta.size() - kVarMetaHeaderBytes) % kVarMetaEntryBytes != 0) {
        fail(function + ": the resource VarMeta is not 24 + 12 * entries bytes");
    }
    if (get_u32(var_meta, 0) != kTableMagic) {
        fail(function + ": the resource VarMeta does not start with the table "
             "magic");
    }
    std::size_t const count =
        (var_meta.size() - kVarMetaHeaderBytes) / kVarMetaEntryBytes;
    if (get_u32(var_meta, 8) != count ||
        get_u32(var_meta, kVar2SizeOffset) != var2.size()) {
        fail(function + ": the resource VarMeta header count/size disagree "
             "with its table");
    }
    std::vector<VarEntry> entries;
    for (std::size_t i = 0; i < count; ++i) {
        std::size_t const at = kVarMetaHeaderBytes + i * kVarMetaEntryBytes;
        VarEntry entry;
        entry.uid = get_u32(var_meta, at);
        entry.offset = get_u32(var_meta, at + 4);
        entry.key = get_u32(var_meta, at + 8);
        if (entry.offset > var2.size() || var2.size() - entry.offset < 4 ||
            get_u32(var2, entry.offset) > var2.size() - entry.offset - 4) {
            fail(function + ": resource VarMeta entry " + std::to_string(i) +
                 " points outside Var2Data");
        }
        if (!entries.empty() && entries.back().uid > entry.uid) {
            fail(function + ": the resource VarMeta is not in ascending uid "
                 "order");
        }
        entries.push_back(entry);
    }
    return entries;
}

std::optional<Bytes> var_payload(std::vector<VarEntry> const& entries,
                                 Bytes const& var2, std::uint32_t uid,
                                 std::uint32_t key) {
    std::optional<Bytes> found;
    for (VarEntry const& entry : entries) {
        if (entry.uid != uid || entry.key != key) {
            continue;
        }
        if (found) {
            return std::nullopt;
        }
        found = slice(var2, entry.offset + 4, get_u32(var2, entry.offset));
    }
    return found;
}

Bytes utf16z(std::string const& text) {
    Bytes out;
    for (char const c : text) {
        out.push_back(static_cast<std::uint8_t>(c));
        out.push_back(0);
    }
    out.push_back(0);
    out.push_back(0);
    return out;
}

bool printable_ascii(std::string const& text) {
    return std::all_of(text.begin(), text.end(), [](char c) {
        return c >= 0x20 && c <= 0x7E;
    });
}

using Identity = std::array<std::uint8_t, 16>;

Identity identity_at(Bytes const& bytes, std::size_t offset) {
    Identity out{};
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset), 16,
                out.begin());
    return out;
}

/* The identities of one family share bytes 0..1 and 4..15; bytes 2..3 are the
 * counter Project advances on save. */
bool same_family(Identity const& a, Identity const& b) {
    return a[0] == b[0] && a[1] == b[1] &&
           std::equal(a.begin() + 4, a.end(), b.begin() + 4);
}

std::uint16_t counter_of(Identity const& id) {
    return static_cast<std::uint16_t>(id[2] | (id[3] << 8));
}

struct Built {
    Bytes rsc_fixed;
    Bytes rsc_meta;
    Bytes rsc_f2;
    Bytes rsc_f2_meta;
    Bytes rsc_var_meta;
    Bytes rsc_var2;
    Bytes cal_fixed;
    Bytes cal_meta;
    Bytes cal_f2;
    Bytes cal_f2_meta;
    PropsEdit props;
};

void require_stream(CfbDocument const& work, char const* path,
                    Bytes const& expected, std::string const& function) {
    if (!work.has_stream(path) || work.read_stream(path) != expected) {
        fail(function + ": the written " + path +
             " does not hold the bytes the edit built");
    }
}

void check_changed_set(CfbDocument const& before, CfbDocument const& after,
                       std::vector<std::string> const& expected,
                       std::string const& function) {
    std::vector<std::string> before_paths = before.stream_paths();
    std::vector<std::string> after_paths = after.stream_paths();
    std::sort(before_paths.begin(), before_paths.end());
    std::sort(after_paths.begin(), after_paths.end());
    if (before_paths != after_paths) {
        fail(function + ": the written file's stream set is not the opened "
             "file's");
    }
    std::vector<bool> seen(expected.size(), false);
    for (std::string const& path : after_paths) {
        bool const changed = before.read_stream(path) != after.read_stream(path);
        std::size_t match = expected.size();
        for (std::size_t i = 0; i < expected.size(); ++i) {
            if (normalise_stream_path(path) == expected[i]) {
                match = i;
            }
        }
        if (match != expected.size()) {
            if (!changed) {
                fail(function + ": the edit did not change " + expected[match]);
            }
            seen[match] = true;
        } else if (changed) {
            fail(function + ": " + path + " changed; this edit owns only the "
                 "measured resource, calendar and Props streams");
        }
    }
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (!seen[i]) {
            fail(function + ": the expected stream " + expected[i] +
                 " is not in the written file");
        }
    }
}

} /* namespace */

ResourceGrowthWrite add_work_resource(CfbDocument& doc,
                                      ResourceGrowthSpec const& spec,
                                      std::string const& path) {
    std::string const function = "resource add";
    if (!spec.dry_run) {
        check_output_path(path, function);
    }
    /* W34b-1.4 (R34G4-6): the longest name a committed pair measures is the two
     * characters of wave30-resource-add.mpp ("R2") and
     * wave34-resource-add-third.mpp ("R3"); a longer name stores a longer
     * Var2Data block no pair measured.  ',' '[' and ']' are the list and
     * field delimiters Project's UI refuses in a resource name. */
    if (spec.name.empty() || spec.name.size() > 2 ||
        !printable_ascii(spec.name) ||
        spec.name.find_first_of(",[]") != std::string::npos) {
        fail(function + ": the name must be 1..2 printable ASCII characters, "
             "none of , [ ] (no other name shape is measured)");
    }
    std::string const initials =
        spec.initials.empty() ? spec.name.substr(0, 1) : spec.initials;
    if (initials.size() > 8 || !printable_ascii(initials)) {
        fail(function + ": the initials must be 1..8 printable ASCII "
             "characters");
    }
    if (spec.created == 0 || spec.created == 0xFFFFFFFFu) {
        fail(function + ": the Created stamp must be a packed date, not 0 or "
             "0xffffffff");
    }

    /* ---- read and validate the resource table ----------------------------- */
    Bytes const rsc_fixed = read_required(doc, kRscFixedData, function);
    Bytes const rsc_meta = read_required(doc, kRscFixedMeta, function);
    Bytes const rsc_f2 = read_required(doc, kRscFixed2Data, function);
    Bytes const rsc_f2_meta = read_required(doc, kRscFixed2Meta, function);
    Bytes const rsc_var_meta = read_required(doc, kRscVarMeta, function);
    Bytes const rsc_var2 = read_required(doc, kRscVar2Data, function);
    if (rsc_fixed.size() < kRscHeaderBytes ||
        (rsc_fixed.size() - kRscHeaderBytes) % kRscRowBytes != 0) {
        fail(function + ": the resource FixedData is not 48 + 172 * rows bytes");
    }
    std::size_t const rows = (rsc_fixed.size() - kRscHeaderBytes) / kRscRowBytes;
    if (rows != 2 && rows != 3) {
        fail(function + ": the resource table has " + std::to_string(rows) +
             " rows; the measured shapes add the second resource (2 rows) or "
             "the third (3 rows), and the per-count flag bytes of any other "
             "count are not measured");
    }
    std::size_t const rsc_entries = rows + kRscDescriptors;
    if (check_meta(rsc_meta, kRscFmEntryBytes, rsc_fixed.size(),
                   "the resource FixedMeta", function) != rsc_entries) {
        fail(function + ": the resource FixedMeta does not carry rows + 3 "
             "entries");
    }
    if (rsc_f2.size() != rsc_entries * kRscF2RecordBytes ||
        check_meta(rsc_f2_meta, kRscF2mEntryBytes, rsc_f2.size(),
                   "the resource Fixed2Meta", function) != rsc_entries) {
        fail(function + ": the resource Fixed2Data/Fixed2Meta do not carry "
             "rows + 3 records");
    }
    for (std::size_t row = 0; row < rows; ++row) {
        std::size_t const entry =
            kMetaHeaderBytes + (row + kRscDescriptors) * kRscFmEntryBytes;
        if (get_u32(rsc_meta, entry + 4) !=
            kRscHeaderBytes + row * kRscRowBytes) {
            fail(function + ": the resource FixedMeta entry for row " +
                 std::to_string(row) + " does not name its row");
        }
        if (row != 0 && rsc_meta[entry] != 0) {
            fail(function + ": resource row " + std::to_string(row) +
                 " is not live; a table with a tombstoned row is not measured");
        }
    }
    for (std::size_t index = 0; index < rsc_entries; ++index) {
        if (get_u32(rsc_f2_meta,
                    kMetaHeaderBytes + index * kRscF2mEntryBytes + 4) !=
            index * kRscF2RecordBytes) {
            fail(function + ": the resource Fixed2Meta entry " +
                 std::to_string(index) + " does not name its record");
        }
    }
    std::vector<VarEntry> const rsc_entries_var =
        load_var_meta(rsc_var_meta, rsc_var2, function);

    std::uint32_t max_uid = 0;
    std::uint32_t max_id = 0;
    for (std::size_t row = 1; row < rows; ++row) {
        std::size_t const base = kRscHeaderBytes + row * kRscRowBytes;
        max_uid = std::max(max_uid, get_u32(rsc_fixed, base + kRscRowUid));
        max_id = std::max(max_id, get_u32(rsc_fixed, base + kRscRowId));
    }
    for (VarEntry const& entry : rsc_entries_var) {
        if (entry.uid > max_uid) {
            fail(function + ": the resource VarMeta holds uid " +
                 std::to_string(entry.uid) + ", beyond the largest row uid " +
                 std::to_string(max_uid));
        }
    }
    if (max_uid == 0xFFFFFFFFu || max_id == 0xFFFFFFFFu) {
        fail(function + ": the resource uid or id would overflow");
    }
    std::uint32_t const uid = max_uid + 1;
    std::uint32_t const id = max_id + 1;

    std::size_t const template_row = rows - 1;
    std::size_t const template_base = kRscHeaderBytes + template_row * kRscRowBytes;
    std::uint32_t const template_uid = get_u32(rsc_fixed, template_base + kRscRowUid);
    if (get_f64(rsc_fixed, template_base + kRscRowUnits) != 10000.0) {
        fail(function + ": the last resource's MaxUnits is not 100 %; only "
             "default rates are measured");
    }
    std::optional<Bytes> const small =
        var_payload(rsc_entries_var, rsc_var2, template_uid, kSmallKey);
    std::optional<Bytes> const blob =
        var_payload(rsc_entries_var, rsc_var2, template_uid, kBlobKey);
    if (!small || *small != Bytes{2, 0, 0, 0} || !blob ||
        *blob != Bytes(16, 0)) {
        fail(function + ": the last resource's 0x0c400055 / 0x0c4002e3 blocks "
             "are not the measured 2 and 16 zero bytes");
    }

    /* ---- read and validate the calendar table ----------------------------- */
    Bytes const cal_fixed = read_required(doc, kCalFixedData, function);
    Bytes const cal_meta = read_required(doc, kCalFixedMeta, function);
    Bytes const cal_f2 = read_required(doc, kCalFixed2Data, function);
    Bytes const cal_f2_meta = read_required(doc, kCalFixed2Meta, function);
    if (cal_fixed.size() < kCalHeaderBytes ||
        (cal_fixed.size() - kCalHeaderBytes) % kCalRowBytes != 0) {
        fail(function + ": the calendar FixedData is not 64 + 12 * rows bytes");
    }
    std::size_t const cal_rows = (cal_fixed.size() - kCalHeaderBytes) / kCalRowBytes;
    if (cal_rows != rows + 1) {
        fail(function + ": the calendar table has " + std::to_string(cal_rows) +
             " rows; the measured shapes pair " + std::to_string(rows) +
             " resource rows with " + std::to_string(rows + 1) + " calendars");
    }
    std::size_t const cal_entries = cal_rows + kCalSpareSlots;
    if (check_meta(cal_meta, kCalMetaEntryBytes, cal_fixed.size(),
                   "the calendar FixedMeta", function) != cal_entries ||
        cal_f2.size() != cal_entries * kCalSlotBytes ||
        check_meta(cal_f2_meta, kCalMetaEntryBytes, cal_f2.size(),
                   "the calendar Fixed2Meta", function) != cal_entries) {
        fail(function + ": the calendar FixedMeta/Fixed2Data/Fixed2Meta do not "
             "carry rows + 4 entries");
    }
    std::uint32_t max_cal_uid = 0;
    for (std::size_t row = 0; row < cal_rows; ++row) {
        max_cal_uid = std::max(
            max_cal_uid, get_u32(cal_fixed, kCalHeaderBytes + row * kCalRowBytes + 8));
    }
    if (max_cal_uid == 0xFFFFFFFFu) {
        fail(function + ": the calendar uid would overflow");
    }
    std::uint32_t const cal_uid = max_cal_uid + 1;

    /* ---- the Props counts must agree with the tables ---------------------- */
    Bytes const props = read_required(doc, kPropsBackEnd, function);
    std::uint32_t const props_rsc_count =
        props_scalar(props, kPropsResourceCountTag, function);
    std::uint32_t const props_rsc_size =
        props_scalar(props, kPropsResourceSizeTag, function);
    std::uint32_t const props_cal_count =
        props_scalar(props, kPropsCalendarCountTag, function);
    if (props_rsc_count != rsc_entries || props_rsc_size != rsc_var2.size() ||
        props_cal_count != cal_entries) {
        fail(function + ": the 114/Props resource and calendar scalars do not "
             "agree with the tables");
    }

    /* ---- the identities --------------------------------------------------- */
    std::size_t const last_slot = (cal_entries - 1) * kCalSlotBytes;
    Identity const shared = identity_at(cal_f2, last_slot + 32);
    Identity const sibling = identity_at(cal_f2, last_slot + 16);
    if (std::all_of(shared.begin(), shared.end(),
                    [](std::uint8_t b) { return b == 0; }) ||
        !same_family(shared, sibling)) {
        fail(function + ": the last calendar slot does not carry the shared "
             "save-time identity in its third place; that slot shape is not "
             "measured");
    }
    std::uint16_t counter = counter_of(shared);
    for (std::size_t at = 0; at + 16 <= cal_f2.size(); at += 16) {
        Identity const other = identity_at(cal_f2, at);
        if (same_family(other, shared)) {
            counter = std::max(counter, counter_of(other));
        }
    }
    for (std::size_t record = 0; record < rsc_entries; ++record) {
        for (std::size_t inner : {std::size_t{0}, std::size_t{24}}) {
            Identity const other =
                identity_at(rsc_f2, record * kRscF2RecordBytes + inner);
            if (same_family(other, shared)) {
                counter = std::max(counter, counter_of(other));
            }
        }
    }
    if (counter == 0xFFFFu) {
        fail(function + ": the identity counter would overflow");
    }
    Identity fresh = shared;
    fresh[2] = static_cast<std::uint8_t>((counter + 1) & 0xFFu);
    fresh[3] = static_cast<std::uint8_t>(((counter + 1) >> 8) & 0xFFu);

    /* ---- the template resource must be the measured one ------------------- */
    {
        Bytes const want_row = from_hex(kTemplateRowHex);
        Bytes const row = slice(rsc_fixed, template_base, kRscRowBytes);
        std::size_t const at =
            first_template_difference(row, want_row, {{0, 8}, {154, 158}});
        if (at < want_row.size()) {
            fail(function + ": the last resource's row differs from the measured "
                 "template at row offset " + std::to_string(at) + " (the pairs "
                 "measured a work resource with no work, cost, accrual, calendar "
                 "or flag set); a resource that is not the measured shape is not "
                 "cloned");
        }
        Bytes const want_fm = from_hex(kTemplateFixedMetaHex);
        Bytes const fm = slice(
            rsc_meta,
            kMetaHeaderBytes + (template_row + kRscDescriptors) * kRscFmEntryBytes,
            kRscFmEntryBytes);
        std::size_t const fm_at =
            first_template_difference(fm, want_fm, {{0, 1}, {4, 8}, {12, 14}});
        if (fm_at < want_fm.size()) {
            fail(function + ": the last resource's FixedMeta entry differs from "
                 "the measured template at entry byte " + std::to_string(fm_at));
        }
        Bytes const want_f2m = from_hex(kTemplateFixed2MetaHex);
        Bytes const f2m = slice(
            rsc_f2_meta,
            kMetaHeaderBytes + (template_row + kRscDescriptors) * kRscF2mEntryBytes,
            kRscF2mEntryBytes);
        std::size_t const f2m_at =
            first_template_difference(f2m, want_f2m, {{4, 9}});
        if (f2m_at < want_f2m.size()) {
            fail(function + ": the last resource's Fixed2Meta entry differs from "
                 "the measured template at entry byte " + std::to_string(f2m_at));
        }
    }

    /* ---- build ------------------------------------------------------------ */
    Built built;
    std::size_t const new_offset = rsc_fixed.size();

    built.rsc_fixed = rsc_fixed;
    {
        Bytes row = slice(rsc_fixed, template_base, kRscRowBytes);
        put_u32(row, kRscRowId, id);
        put_u32(row, kRscRowUid, uid);
        std::uint16_t const old_counter = get_u16(row, kRscRowCounter);
        if (old_counter == 0xFFFFu) {
            fail(function + ": the resource row counter would overflow");
        }
        put_u16(row, kRscRowCounter, static_cast<std::uint16_t>(old_counter + 1));
        built.rsc_fixed.insert(built.rsc_fixed.end(), row.begin(), row.end());
    }

    built.rsc_meta = rsc_meta;
    {
        std::size_t const template_entry =
            kMetaHeaderBytes + (template_row + kRscDescriptors) * kRscFmEntryBytes;
        Bytes entry = slice(rsc_meta, template_entry, kRscFmEntryBytes);
        entry[0] = 0;
        put_u32(entry, 4, static_cast<std::uint32_t>(new_offset));
        entry[12] = 0x1C;
        entry[13] = static_cast<std::uint8_t>(0x44 + rows);
        built.rsc_meta.insert(built.rsc_meta.end(), entry.begin(), entry.end());
        put_u32(built.rsc_meta, 8, static_cast<std::uint32_t>(rsc_entries + 1));
        put_u32(built.rsc_meta, 12,
                static_cast<std::uint32_t>(built.rsc_fixed.size()));
    }

    built.rsc_f2 = rsc_f2;
    built.rsc_f2.insert(built.rsc_f2.end(), fresh.begin(), fresh.end());
    append_f64(built.rsc_f2, static_cast<double>(rows + 1));
    built.rsc_f2.insert(built.rsc_f2.end(), fresh.begin(), fresh.end());

    built.rsc_f2_meta = rsc_f2_meta;
    {
        std::size_t const template_entry =
            kMetaHeaderBytes + (template_row + kRscDescriptors) * kRscF2mEntryBytes;
        Bytes entry = slice(rsc_f2_meta, template_entry, kRscF2mEntryBytes);
        put_u32(entry, 4, static_cast<std::uint32_t>(rsc_f2.size()));
        entry[8] = rows == 2 ? 0x2B : 0x0B;
        built.rsc_f2_meta.insert(built.rsc_f2_meta.end(), entry.begin(),
                                 entry.end());
        put_u32(built.rsc_f2_meta, 8, static_cast<std::uint32_t>(rsc_entries + 1));
        put_u32(built.rsc_f2_meta, 12,
                static_cast<std::uint32_t>(built.rsc_f2.size()));
    }

    /* Blocks are written Created, blob, Name, Initials, small; the VarMeta
     * entries follow in ascending key order. */
    built.rsc_var2 = rsc_var2;
    built.rsc_var_meta = rsc_var_meta;
    {
        Bytes created;
        append_u32(created, spec.created);
        std::vector<std::pair<std::uint32_t, Bytes>> const blocks = {
            {kCreatedKey, created},
            {kBlobKey, Bytes(16, 0)},
            {kNameKey, utf16z(spec.name)},
            {kInitialsKey, utf16z(initials)},
            {kSmallKey, Bytes{2, 0, 0, 0}},
        };
        std::vector<std::pair<std::uint32_t, std::uint32_t>> offsets;
        for (auto const& block : blocks) {
            offsets.emplace_back(block.first,
                                 static_cast<std::uint32_t>(built.rsc_var2.size()));
            append_u32(built.rsc_var2,
                       static_cast<std::uint32_t>(block.second.size()));
            built.rsc_var2.insert(built.rsc_var2.end(), block.second.begin(),
                                  block.second.end());
        }
        std::sort(offsets.begin(), offsets.end());
        for (auto const& entry : offsets) {
            append_u32(built.rsc_var_meta, uid);
            append_u32(built.rsc_var_meta, entry.second);
            append_u32(built.rsc_var_meta, entry.first);
        }
        put_u32(built.rsc_var_meta, 8,
                static_cast<std::uint32_t>(
                    (built.rsc_var_meta.size() - kVarMetaHeaderBytes) /
                    kVarMetaEntryBytes));
        put_u32(built.rsc_var_meta, kVar2SizeOffset,
                static_cast<std::uint32_t>(built.rsc_var2.size()));
    }

    built.cal_fixed = cal_fixed;
    append_u32(built.cal_fixed, 1);
    append_u32(built.cal_fixed, uid);
    append_u32(built.cal_fixed, cal_uid);

    built.cal_meta = cal_meta;
    {
        Bytes entry(kCalMetaEntryBytes, 0);
        put_u32(entry, 4, static_cast<std::uint32_t>(cal_fixed.size()));
        put_u16(entry, 8, 0x000E);
        built.cal_meta.insert(built.cal_meta.end(), entry.begin(), entry.end());
        put_u32(built.cal_meta, 8, static_cast<std::uint32_t>(cal_entries + 1));
        put_u32(built.cal_meta, 12,
                static_cast<std::uint32_t>(built.cal_fixed.size()));
    }

    built.cal_f2 = cal_f2;
    built.cal_f2.insert(built.cal_f2.end(), fresh.begin(), fresh.end());
    built.cal_f2.insert(built.cal_f2.end(), fresh.begin(), fresh.end());
    built.cal_f2.insert(built.cal_f2.end(), shared.begin(), shared.end());

    built.cal_f2_meta = cal_f2_meta;
    {
        Bytes entry(kCalMetaEntryBytes, 0);
        put_u32(entry, 4, static_cast<std::uint32_t>(cal_f2.size()));
        put_u16(entry, 8, 0x000E);
        built.cal_f2_meta.insert(built.cal_f2_meta.end(), entry.begin(),
                                 entry.end());
        put_u32(built.cal_f2_meta, 8, static_cast<std::uint32_t>(cal_entries + 1));
        put_u32(built.cal_f2_meta, 12,
                static_cast<std::uint32_t>(built.cal_f2.size()));
    }

    built.props.stream = kPropsBackEnd;
    built.props.scalars = {
        {kPropsResourceCountTag, static_cast<std::uint32_t>(rsc_entries + 1)},
        {kPropsResourceSizeTag, static_cast<std::uint32_t>(built.rsc_var2.size())},
        {kPropsCalendarCountTag, static_cast<std::uint32_t>(cal_entries + 1)},
    };

    ResourceGrowthWrite written;
    written.uid = uid;
    written.id = id;
    written.calendar_uid = cal_uid;
    written.rows_before = rows;
    written.rows_after = rows + 1;
    written.resource_count_before = props_rsc_count;
    written.resource_count_after = static_cast<std::uint32_t>(rsc_entries + 1);
    written.resource_size_before = props_rsc_size;
    written.resource_size_after = static_cast<std::uint32_t>(built.rsc_var2.size());
    written.calendar_count_before = props_cal_count;
    written.calendar_count_after = static_cast<std::uint32_t>(cal_entries + 1);

    /* W34-4.2: a dry run has built and validated every stream; it writes
     * nothing and leaves `doc` as it was. */
    if (spec.dry_run) {
        return written;
    }

    /* ---- save and prove ---------------------------------------------------- */
    CfbDocument work = doc;
    work.replace_stream(kRscFixedData, built.rsc_fixed);
    work.replace_stream(kRscFixedMeta, built.rsc_meta);
    work.replace_stream(kRscFixed2Data, built.rsc_f2);
    work.replace_stream(kRscFixed2Meta, built.rsc_f2_meta);
    work.replace_stream(kRscVarMeta, built.rsc_var_meta);
    work.replace_stream(kRscVar2Data, built.rsc_var2);
    work.replace_stream(kCalFixedData, built.cal_fixed);
    work.replace_stream(kCalFixedMeta, built.cal_meta);
    work.replace_stream(kCalFixed2Data, built.cal_f2);
    work.replace_stream(kCalFixed2Meta, built.cal_f2_meta);

    (void)save_props_edit(work, built.props, path);

    /* The edit marker is the one stream whose change is conditional: the marker
     * writer stores 1 and leaves a file that already holds 1 untouched.  Drop it
     * from the changed set only in exactly that byte-identical case. */
    std::vector<std::string> expected = {
        kRscFixedData,  kRscFixedMeta,  kRscFixed2Data, kRscFixed2Meta,
        kRscVarMeta,    kRscVar2Data,   kCalFixedData,  kCalFixedMeta,
        kCalFixed2Data, kCalFixed2Meta, kPropsBackEnd,  kEditMarkerStream};
    if (doc.has_stream(kEditMarkerStream) && work.has_stream(kEditMarkerStream) &&
        doc.read_stream(kEditMarkerStream) == work.read_stream(kEditMarkerStream)) {
        expected.erase(std::find(expected.begin(), expected.end(),
                                 std::string(kEditMarkerStream)));
    }

    require_stream(work, kRscFixedData, built.rsc_fixed, function);
    require_stream(work, kRscFixedMeta, built.rsc_meta, function);
    require_stream(work, kRscFixed2Data, built.rsc_f2, function);
    require_stream(work, kRscFixed2Meta, built.rsc_f2_meta, function);
    require_stream(work, kRscVarMeta, built.rsc_var_meta, function);
    require_stream(work, kRscVar2Data, built.rsc_var2, function);
    require_stream(work, kCalFixedData, built.cal_fixed, function);
    require_stream(work, kCalFixedMeta, built.cal_meta, function);
    require_stream(work, kCalFixed2Data, built.cal_f2, function);
    require_stream(work, kCalFixed2Meta, built.cal_f2_meta, function);

    Bytes const props_now = work.read_stream(kPropsBackEnd);
    for (PropsScalarEdit const& edit : built.props.scalars) {
        if (props_scalar(props_now, edit.tag, function) != edit.value) {
            fail(function + ": the written 114/Props scalar 0x" + hex32(edit.tag) +
                 " is not " + std::to_string(edit.value));
        }
    }
    check_changed_set(doc, work, expected, function);

    doc = std::move(work);
    return written;
}

} /* namespace mppwrite */
