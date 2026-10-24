/*
 * summary_info.cpp -- rewrite DocumentSummaryInformation from the edited
 * project row.  The measurement and the rules are stated in summary_info.h;
 * build/w29/dsi_rebuild.py and build/w29/dsi_derive.py are the prototypes that
 * validated the serialiser and the value derivation on all 23 save pairs.
 *
 * The serialiser is deliberately strict: before anything is written, the
 * stream is parsed and re-serialised UNCHANGED and the bytes must match the
 * opened stream.  That proves this code understands the layout (section order,
 * property-table order, value encoding, padding) instead of merely accepting
 * it.  A shape it does not understand is refused, not rewritten.
 */

#include "summary_info.h"

#include "table.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace mppwrite {
namespace {

using Bytes = std::vector<std::uint8_t>;
using FmtId = std::array<std::uint8_t, 16>;

constexpr std::uint32_t kTypeI2 = 2;
constexpr std::uint32_t kTypeI4 = 3;
constexpr std::uint32_t kTypeBool = 11;
constexpr std::uint32_t kTypeLpstr = 30;
constexpr std::uint32_t kTypeFiletime = 64;
constexpr std::uint32_t kTypeVectorLpstr = 0x101Eu;
constexpr std::uint32_t kTypeVectorVariant = 0x100Cu;

constexpr std::size_t kPropertySetHeader = 28;
constexpr std::size_t kDescriptorSize = 20;

/* The two measured section format identifiers, as stored (little-endian
 * GUIDs d5cdd502-2e9c-101b-9397-08002b2cf9ae and d5cdd505-...). */
constexpr FmtId kSummaryFmt = {0x02, 0xd5, 0xcd, 0xd5, 0x9c, 0x2e, 0x1b, 0x10,
                               0x93, 0x97, 0x08, 0x00, 0x2b, 0x2c, 0xf9, 0xae};
constexpr FmtId kUserFmt = {0x05, 0xd5, 0xcd, 0xd5, 0x9c, 0x2e, 0x1b, 0x10,
                            0x93, 0x97, 0x08, 0x00, 0x2b, 0x2c, 0xf9, 0xae};

/* Section 0: TitlesOfParts, the seven summary titles. */
constexpr std::uint32_t kPidTitles = 0x0D;
constexpr std::size_t kTitleCount = 7;
/* Section 1: the typed display values this writer derives. */
constexpr std::uint32_t kPidPercent = 2;
constexpr std::uint32_t kPidCost = 3;
constexpr std::uint32_t kPidDuration = 4;
constexpr std::uint32_t kPidFinish = 5;
constexpr std::uint32_t kPidStart = 6;
constexpr std::uint32_t kPidWork = 7;
constexpr std::uint32_t kPidPercentWork = 8;

/* The project row's total-work double (table.h: +8 work, +16 actual work,
 * +24 remaining work) and one FILETIME day in 100 ns units. */
constexpr std::size_t kRowWork = 8;
constexpr std::int64_t kFiletimePerDay = 86400LL * 10000000LL;

[[noreturn]] void fail(std::string const& what) {
    throw Error("DocumentSummaryInformation: " + what);
}

std::uint16_t get_u16(Bytes const& b, std::size_t at, char const* what) {
    if (at > b.size() || b.size() - at < 2) {
        fail(std::string(what) + " is truncated");
    }
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(b[at]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(b[at + 1]) << 8));
}

std::uint32_t get_u32(Bytes const& b, std::size_t at, char const* what) {
    if (at > b.size() || b.size() - at < 4) {
        fail(std::string(what) + " is truncated");
    }
    return static_cast<std::uint32_t>(b[at]) |
           (static_cast<std::uint32_t>(b[at + 1]) << 8) |
           (static_cast<std::uint32_t>(b[at + 2]) << 16) |
           (static_cast<std::uint32_t>(b[at + 3]) << 24);
}

std::uint64_t get_u64(Bytes const& b, std::size_t at, char const* what) {
    if (at > b.size() || b.size() - at < 8) {
        fail(std::string(what) + " is truncated");
    }
    std::uint64_t value = 0;
    for (int i = 7; i >= 0; --i) {
        value = (value << 8) | b[at + static_cast<std::size_t>(i)];
    }
    return value;
}

void set_u32(Bytes& b, std::size_t at, std::uint32_t v) {
    b[at] = static_cast<std::uint8_t>(v & 0xFFu);
    b[at + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
    b[at + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
    b[at + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
}

void append_u32(Bytes& b, std::uint32_t v) {
    b.push_back(static_cast<std::uint8_t>(v & 0xFFu));
    b.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
    b.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
    b.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
}

void append_u64(Bytes& b, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        b.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu));
    }
}

std::size_t round_up_4(std::size_t n) { return (n + 3u) & ~std::size_t{3u}; }

struct Property {
    std::uint32_t pid = 0;
    Bytes raw; /* the complete encoded value, type word included */
};

struct Section {
    FmtId fmt{};
    std::vector<Property> props;
};

/* A dictionary (section 1 pid 0): u32 count then (u32 id, u32 length,
 * length bytes) per entry. */
Bytes parse_dictionary(Bytes const& data, std::size_t at) {
    std::uint32_t const count = get_u32(data, at, "a dictionary count");
    std::size_t pos = at + 4;
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint32_t const length =
            get_u32(data, pos + 4, "a dictionary entry length");
        if (length > data.size() || pos + 8 > data.size() - length) {
            fail("a dictionary entry runs past the stream");
        }
        pos += 8 + length;
    }
    return Bytes(data.begin() + static_cast<std::ptrdiff_t>(at),
                 data.begin() + static_cast<std::ptrdiff_t>(pos));
}

/* The complete encoded value at `at`, with its length taken from the type. */
Bytes parse_prop(Bytes const& data, std::size_t at) {
    std::uint32_t const type = get_u32(data, at, "a property type");
    std::size_t end = at;
    if (type == kTypeVectorLpstr) {
        std::uint32_t const count =
            get_u32(data, at + 4, "a string-vector count");
        end = at + 8;
        for (std::uint32_t i = 0; i < count; ++i) {
            std::uint32_t const length =
                get_u32(data, end, "a string-vector length");
            if (length > data.size() || end + 4 > data.size() - length) {
                fail("a string-vector entry runs past the stream");
            }
            end += 4 + length;
        }
    } else if (type == kTypeVectorVariant) {
        std::uint32_t const count =
            get_u32(data, at + 4, "a variant-vector count");
        end = at + 8;
        for (std::uint32_t i = 0; i < count; ++i) {
            std::uint32_t const variant =
                get_u32(data, end, "a variant type");
            end += 4;
            if (variant == kTypeLpstr) {
                std::uint32_t const length =
                    get_u32(data, end, "a variant string length");
                if (length > data.size() || end + 4 > data.size() - length) {
                    fail("a variant string runs past the stream");
                }
                end += 4 + length;
            } else if (variant == kTypeI2 || variant == kTypeI4 ||
                       variant == kTypeBool) {
                end += 4;
            } else {
                fail("an unmeasured variant type " + std::to_string(variant));
            }
        }
    } else if (type == kTypeLpstr) {
        std::uint32_t const length =
            get_u32(data, at + 4, "a string length");
        if (length > data.size() || at + 8 > data.size() - length) {
            fail("a string value runs past the stream");
        }
        end = at + 8 + length;
    } else if (type == kTypeI2 || type == kTypeI4 || type == kTypeBool) {
        end = at + 8;
    } else if (type == kTypeFiletime) {
        end = at + 12;
    } else {
        fail("an unmeasured property type " + std::to_string(type));
    }
    if (end > data.size()) {
        fail("a property value runs past the stream");
    }
    return Bytes(data.begin() + static_cast<std::ptrdiff_t>(at),
                 data.begin() + static_cast<std::ptrdiff_t>(end));
}

Section parse_section(Bytes const& data, std::size_t off, std::size_t index) {
    Section section;
    std::uint32_t const size = get_u32(data, off, "a section size");
    std::uint32_t const count =
        get_u32(data, off + 4, "a section property count");
    std::size_t const table_end = off + 8 + 8 * static_cast<std::size_t>(count);
    if (off > data.size() || size > data.size() - off) {
        fail("section " + std::to_string(index) + " runs past the stream");
    }
    if (table_end > off + size) {
        fail("section " + std::to_string(index) +
             " is smaller than its property table");
    }
    std::vector<std::pair<std::uint32_t, std::size_t>> entries;
    entries.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint32_t const pid =
            get_u32(data, off + 8 + 8 * static_cast<std::size_t>(i),
                    "a property id");
        std::uint32_t const relative =
            get_u32(data, off + 12 + 8 * static_cast<std::size_t>(i),
                    "a property offset");
        if (relative > size || off + relative > data.size()) {
            fail("a property offset in section " + std::to_string(index) +
                 " points past the section");
        }
        for (auto const& seen : entries) {
            if (seen.first == pid) {
                fail("section " + std::to_string(index) + " repeats property " +
                     std::to_string(pid));
            }
        }
        entries.emplace_back(pid, off + relative);
    }
    section.props.reserve(entries.size());
    for (auto const& entry : entries) {
        Property prop;
        prop.pid = entry.first;
        prop.raw = entry.first == 0 ? parse_dictionary(data, entry.second)
                                    : parse_prop(data, entry.second);
        section.props.push_back(std::move(prop));
    }
    return section;
}

/* Re-serialise the sections after `header`.  Measured canonical layout: the
 * values follow the property table in table order, each section padded to a
 * four-byte size, descriptors rewritten with the new offsets. */
Bytes serialise(Bytes const& header, std::vector<Section> const& sections) {
    Bytes out = header;
    out.resize(header.size() + kDescriptorSize * sections.size(), 0);
    std::vector<std::size_t> offsets;
    offsets.reserve(sections.size());
    std::size_t cursor = out.size();
    for (Section const& section : sections) {
        std::size_t values = 0;
        for (Property const& prop : section.props) {
            values += prop.raw.size();
        }
        std::size_t const length =
            round_up_4(8 + 8 * section.props.size() + values);
        if (length > std::numeric_limits<std::uint32_t>::max()) {
            fail("a rebuilt section is larger than 32 bits");
        }
        offsets.push_back(cursor);
        cursor += length;
    }
    out.resize(cursor, 0);
    for (std::size_t i = 0; i < sections.size(); ++i) {
        std::copy(sections[i].fmt.begin(), sections[i].fmt.end(),
                  out.begin() + static_cast<std::ptrdiff_t>(28 + 20 * i));
        set_u32(out, 44 + 20 * i, static_cast<std::uint32_t>(offsets[i]));
    }
    for (std::size_t i = 0; i < sections.size(); ++i) {
        Section const& section = sections[i];
        std::size_t const off = offsets[i];
        std::size_t values = 0;
        for (Property const& prop : section.props) {
            values += prop.raw.size();
        }
        std::size_t const length =
            round_up_4(8 + 8 * section.props.size() + values);
        set_u32(out, off, static_cast<std::uint32_t>(length));
        set_u32(out, off + 4,
                static_cast<std::uint32_t>(section.props.size()));
        std::size_t pos = off + 8 + 8 * section.props.size();
        for (std::size_t j = 0; j < section.props.size(); ++j) {
            Property const& prop = section.props[j];
            std::size_t const relative = pos - off;
            if (relative > std::numeric_limits<std::uint32_t>::max()) {
                fail("a rebuilt property offset is larger than 32 bits");
            }
            set_u32(out, off + 8 + 8 * j, prop.pid);
            set_u32(out, off + 12 + 8 * j,
                    static_cast<std::uint32_t>(relative));
            std::copy(prop.raw.begin(), prop.raw.end(),
                      out.begin() + static_cast<std::ptrdiff_t>(pos));
            pos += prop.raw.size();
        }
    }
    return out;
}

Property* find_prop(Section& section, std::uint32_t pid) {
    for (Property& prop : section.props) {
        if (prop.pid == pid) {
            return &prop;
        }
    }
    return nullptr;
}

Property const* find_prop(Section const& section, std::uint32_t pid) {
    for (Property const& prop : section.props) {
        if (prop.pid == pid) {
            return &prop;
        }
    }
    return nullptr;
}

std::uint32_t prop_type(Property const& prop) {
    return get_u32(prop.raw, 0, "a property type");
}

/* VT_LPSTR with the length (NUL included) rounded up to four and NUL-padded,
 * the measured encoding of every string this writer replaces. */
Bytes lpstr(std::string const& text) {
    std::size_t const length = text.size() + 1;
    std::size_t const padded = round_up_4(length);
    Bytes raw;
    append_u32(raw, kTypeLpstr);
    append_u32(raw, static_cast<std::uint32_t>(padded));
    raw.insert(raw.end(), text.begin(), text.end());
    raw.resize(8 + padded, 0);
    return raw;
}

/* The stored FILETIME moved by a whole number of days.  Keeping the control's
 * time of day is the measured rule (see summary_info.h). */
Bytes filetime(std::int64_t delta_days, Bytes const& control) {
    if (control.size() != 12) {
        fail("a FILETIME value is not 12 bytes");
    }
    std::uint64_t const stored = get_u64(control, 4, "a FILETIME");
    std::int64_t const moved = delta_days * kFiletimePerDay;
    std::int64_t const updated = static_cast<std::int64_t>(stored) + moved;
    if (updated < 0) {
        fail("a derived FILETIME would be before 1601");
    }
    Bytes raw;
    append_u32(raw, kTypeFiletime);
    append_u64(raw, static_cast<std::uint64_t>(updated));
    return raw;
}

/* "5 days", "4.8 days", "6 days?" -- the measured summary-duration display. */
std::string duration_text(std::uint32_t ticks, std::uint16_t format) {
    if (format != 21 && format != 53) {
        fail("project DurationFormat " + std::to_string(format) +
             " has no measured summary-duration display (measured: 21 and"
             " 53)");
    }
    double const days = static_cast<double>(ticks) / 4800.0;
    long long const hundredths =
        static_cast<long long>(std::nearbyint(days * 100.0));
    long long const whole = hundredths / 100;
    long long const rest = hundredths % 100;
    std::string number = std::to_string(whole);
    if (rest != 0) {
        number += '.';
        number += static_cast<char>('0' + rest / 10);
        if (rest % 10 != 0) {
            number += static_cast<char>('0' + rest % 10);
        }
    }
    number += " days";
    if ((format & 0x20u) != 0) {
        number += '?';
    }
    return number;
}

struct CivilDate {
    int year = 0;
    int month = 0;
    int day = 0;
};

/* Day 0 (1983-12-31) as days since 1970-01-01. */
constexpr std::int64_t kDay0SinceEpoch = 5112;

/* Howard Hinnant's civil_from_days, shifted to the packed-date epoch. */
CivilDate civil_from_days(int day) {
    std::int64_t const z =
        static_cast<std::int64_t>(day) + kDay0SinceEpoch + 719468;
    std::int64_t const era = (z >= 0 ? z : z - 146096) / 146097;
    std::uint64_t const doe = static_cast<std::uint64_t>(z - era * 146097);
    std::uint64_t const yoe =
        (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    std::int64_t const y = static_cast<std::int64_t>(yoe) + era * 400;
    std::uint64_t const doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    std::uint64_t const mp = (5 * doy + 2) / 153;
    std::uint64_t const d = doy - (153 * mp + 2) / 5 + 1;
    int const month = static_cast<int>(mp) + (mp < 10 ? 3 : -9);
    CivilDate out;
    out.year = static_cast<int>(y + (month <= 2 ? 1 : 0));
    out.month = month;
    out.day = static_cast<int>(d);
    return out;
}

std::string two_digits(int value) {
    std::string out;
    if (value < 10) {
        out += '0';
    }
    out += std::to_string(value);
    return out;
}

/* "Mon 5/01/26" -- weekday (day 0 was a Saturday), unpadded day, two-digit
 * month and year, measured. */
std::string date_text(std::uint32_t packed, char const* what) {
    if (!is_date(packed)) {
        fail(std::string(what) + " is not a valid packed date");
    }
    PackedDate const date = unpack_date(packed);
    static char const* const kWeekday[7] = {"Sat", "Sun", "Mon", "Tue",
                                            "Wed", "Thu", "Fri"};
    CivilDate const civil = civil_from_days(date.day);
    std::string out = kWeekday[static_cast<std::size_t>(date.day) % 7];
    out += ' ';
    out += std::to_string(civil.day);
    out += '/';
    out += two_digits(civil.month);
    out += '/';
    out += two_digits(civil.year % 100);
    return out;
}

std::int64_t row_day(TaskTable const& table, std::size_t offset,
                     char const* what) {
    std::uint32_t const raw = table.u32(0, offset);
    if (!is_date(raw)) {
        fail(std::string(what) + " is not a valid packed date");
    }
    return unpack_date(raw).day;
}

/* MS stores the two property sets under a root-level name whose first byte is
 * the 0x05 control prefix; CfbDocument keeps that byte (it only trims spaces),
 * and `stream_paths()` reports it.  Find the stream by its leaf name so the
 * writer does not depend on how the prefix is stored. */
std::string summary_stream_path(CfbDocument const& doc) {
    for (std::string const& path : doc.stream_paths()) {
        std::size_t const slash = path.find_last_of('/');
        std::string_view const leaf =
            slash == std::string::npos
                ? std::string_view(path)
                : std::string_view(path).substr(slash + 1);
        std::size_t at = 0;
        while (at < leaf.size() &&
               (leaf[at] == ' ' ||
                static_cast<unsigned char>(leaf[at]) < 0x20)) {
            ++at;
        }
        if (leaf.substr(at) == kDocumentSummaryStream) {
            return path;
        }
    }
    return {};
}

Property const& require_prop(Section const& section, std::uint32_t pid,
                             std::uint32_t type, char const* what) {
    Property const* const prop = find_prop(section, pid);
    if (prop == nullptr) {
        fail(std::string("typed value ") + what + " is missing");
    }
    if (prop_type(*prop) != type) {
        fail(std::string("typed value ") + what + " is not the measured type " +
             std::to_string(type));
    }
    return *prop;
}

void set_prop(Section& section, std::uint32_t pid, Bytes raw) {
    Property* prop = find_prop(section, pid);
    if (prop == nullptr) {
        fail("section property " + std::to_string(pid) + " is missing");
    }
    prop->raw = std::move(raw);
}

} /* namespace */

void write_project_summary(CfbDocument& doc, TaskTable const& before,
                           TaskTable const& after) {
    if (before.rows() < 1 || after.rows() < 1) {
        fail("the task table has no project row");
    }
    /* Work is the one display value that is not derived here: no measured pair
     * changes the project row's total work, and the work string's rounding for
     * a changed total is unmeasured.  A change is a new shape and must be
     * refused rather than carried through with the old string. */
    if (before.u32(0, kRowWork) != after.u32(0, kRowWork)) {
        fail("the project row's total work changed; the summary work string"
             " has no measured update rule");
    }
    std::string const stream_path = summary_stream_path(doc);
    if (stream_path.empty()) {
        fail("the stream is absent from the document");
    }
    Bytes const data = doc.read_stream(stream_path);
    if (data.size() < kPropertySetHeader) {
        fail("the stream is shorter than its 28-byte header");
    }
    if (get_u16(data, 0, "the byte-order word") != 0xFFFEu) {
        fail("the byte-order word is not 0xFFFE");
    }
    std::uint32_t const section_count =
        get_u32(data, 24, "the section count");
    if (section_count != 2) {
        fail("the stream has " + std::to_string(section_count) +
             " sections; the measured layout has 2");
    }
    std::vector<Section> sections;
    sections.reserve(2);
    for (std::size_t i = 0; i < 2; ++i) {
        std::uint32_t const off =
            get_u32(data, 44 + 20 * i, "a section offset");
        Section section = parse_section(data, off, i);
        for (std::size_t b = 0; b < 16; ++b) {
            section.fmt[b] = data[28 + 20 * i + b];
        }
        sections.push_back(std::move(section));
    }
    if (sections[0].fmt != kSummaryFmt || sections[1].fmt != kUserFmt) {
        fail("the two section format identifiers are not the measured pair");
    }
    /* Prove the layout is the one this code re-creates before changing it. */
    Bytes const header(data.begin(),
                       data.begin() +
                           static_cast<std::ptrdiff_t>(kPropertySetHeader));
    if (serialise(header, sections) != data) {
        fail("the stream layout is not the measured canonical layout");
    }

    /* --- section 0: the seven titles ------------------------------------ */
    Property const* const titles_prop = find_prop(sections[0], kPidTitles);
    if (titles_prop == nullptr ||
        prop_type(*titles_prop) != kTypeVectorLpstr) {
        fail("TitlesOfParts is missing or is not a string vector");
    }
    Bytes const& titles_raw = titles_prop->raw;
    std::uint32_t const title_count =
        get_u32(titles_raw, 4, "the title count");
    if (title_count != kTitleCount) {
        fail("TitlesOfParts has " + std::to_string(title_count) +
             " titles; the measured display has 7");
    }
    std::vector<Bytes> titles;
    titles.reserve(kTitleCount);
    std::size_t titles_at = 8;
    for (std::uint32_t i = 0; i < title_count; ++i) {
        std::uint32_t const length =
            get_u32(titles_raw, titles_at, "a title length");
        if (length > titles_raw.size() ||
            titles_at + 4 > titles_raw.size() - length) {
            fail("a title runs past TitlesOfParts");
        }
        titles.emplace_back(
            titles_raw.begin() + static_cast<std::ptrdiff_t>(titles_at + 4),
            titles_raw.begin() +
                static_cast<std::ptrdiff_t>(titles_at + 4 + length));
        titles_at += 4 + length;
    }
    if (titles_at != titles_raw.size()) {
        fail("TitlesOfParts has bytes after its seven strings");
    }

    /* --- section 1: the typed values ------------------------------------ */
    Property const& finish_prop =
        require_prop(sections[1], kPidFinish, kTypeFiletime, "finish");
    Property const& start_prop =
        require_prop(sections[1], kPidStart, kTypeFiletime, "start");
    (void)require_prop(sections[1], kPidPercent, kTypeLpstr, "% complete");
    (void)require_prop(sections[1], kPidDuration, kTypeLpstr, "duration");
    (void)require_prop(sections[1], kPidPercentWork, kTypeLpstr,
                       "% work complete");
    (void)require_prop(sections[1], kPidCost, kTypeLpstr, "cost");
    (void)require_prop(sections[1], kPidWork, kTypeLpstr, "work");

    std::uint16_t const format = after.u16(0, task_row::kDurationFormat);
    std::string const new_duration =
        duration_text(after.u32(0, task_row::kDuration), format);
    std::string const new_percent =
        std::to_string(after.u16(0, task_row::kPercentComplete)) + "%";
    std::string const new_percent_work =
        std::to_string(after.u16(0, task_row::kPercentWorkComplete)) + "%";
    std::string const new_start =
        date_text(after.u32(0, task_row::kStart), "the project row's start");
    std::string const new_finish =
        date_text(after.u32(0, task_row::kFinish), "the project row's finish");
    std::int64_t const start_days =
        row_day(after, task_row::kStart, "the project row's start") -
        row_day(before, task_row::kStart, "the opened project row's start");
    std::int64_t const finish_days =
        row_day(after, task_row::kFinish, "the project row's finish") -
        row_day(before, task_row::kFinish, "the opened project row's finish");

    /* Titles [3] (work) and [4] (cost) keep their bytes. */
    titles[0] = Bytes(new_start.begin(), new_start.end());
    titles[0].push_back(0);
    titles[1] = Bytes(new_finish.begin(), new_finish.end());
    titles[1].push_back(0);
    titles[2] = Bytes(new_duration.begin(), new_duration.end());
    titles[2].push_back(0);
    titles[5] = Bytes(new_percent.begin(), new_percent.end());
    titles[5].push_back(0);
    titles[6] = Bytes(new_percent_work.begin(), new_percent_work.end());
    titles[6].push_back(0);
    Bytes titles_vector;
    append_u32(titles_vector, kTypeVectorLpstr);
    append_u32(titles_vector, static_cast<std::uint32_t>(titles.size()));
    for (Bytes const& title : titles) {
        append_u32(titles_vector, static_cast<std::uint32_t>(title.size()));
        titles_vector.insert(titles_vector.end(), title.begin(), title.end());
    }
    set_prop(sections[0], kPidTitles, std::move(titles_vector));
    set_prop(sections[1], kPidPercent, lpstr(new_percent));
    set_prop(sections[1], kPidDuration, lpstr(new_duration));
    set_prop(sections[1], kPidFinish, filetime(finish_days, finish_prop.raw));
    set_prop(sections[1], kPidStart, filetime(start_days, start_prop.raw));
    set_prop(sections[1], kPidPercentWork, lpstr(new_percent_work));

    doc.replace_stream(stream_path, serialise(header, sections));
}

} /* namespace mppwrite */
