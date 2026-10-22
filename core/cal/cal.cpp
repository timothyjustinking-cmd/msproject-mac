// core/cal/cal.cpp -- see cal.h for the layout and the evidence tags.
//
// Every offset used here is cited in cal.h and re-derived in core/cal/README.md.
// The cross-check that the reading is right is tests/cal_tests.cpp: it rebuilds
// the three task dates of corpus/probe/holiday1.mpp that MS Project calculated
// itself (docs/analysis/oracle/holiday-probe.json).

#include "cal.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <utility>

namespace core::cal {

namespace {

// --- format constants (all measured; see cal.h) -----------------------------

constexpr std::uint32_t kMetaMagic = 0xFADFADBAu;
constexpr std::uint32_t kMetaVersion = 4;
constexpr std::size_t kMetaHeaderBytes = 16;
constexpr std::size_t kMetaEntryBytes = 10;

// 114/TBkndCal/FixedData starts with four 16-byte descriptors whose FixedMeta
// entries point at 0, 16, 32 and 48; the first calendar row is at 64.
constexpr std::size_t kFixedHeaderBytes = 64;
constexpr std::size_t kCalendarRowBytes = 12;

constexpr std::size_t kVarMetaHeaderBytes = 24;
constexpr std::size_t kVarMetaEntryBytes = 12;

constexpr std::size_t kWeekAreaBytes = kWeekBlockBytes;  // 420
constexpr std::size_t kExceptionFixedBytes = 92;
constexpr std::size_t kExceptionNameOffset = 88;             // u32 name byte length
constexpr std::size_t kExceptionRecurrenceOffset = 72;       // u16 PjExceptionType
constexpr std::size_t kExceptionOccurrencesOffset = 4;       // u16, recurring only
constexpr std::size_t kExceptionPeriodCountOffset = 14;      // u16
constexpr std::size_t kExceptionStartOffset = 20;            // u16 start[i]
constexpr std::size_t kExceptionLengthOffset = 32;           // u32 length[i]
constexpr std::size_t kWorkWeekTailBytes = 2 + 2 + 8 + 4;    // 16

constexpr std::int32_t kMaxStoredIntervals = 8;              // spec §1.1
constexpr std::int32_t kMaxDayTicks = 14400;                 // 24 h in tenths

// 114/Props keys (PropsKey.DEFAULT_CALENDAR_HOURS / .DEFAULT_CALENDAR_NAME).
constexpr std::uint32_t kPropsDefaultCalendarHours = 37753736u;  // 0x02401388
constexpr std::uint32_t kPropsDefaultCalendarName = 37748750u;   // 0x0240000E

// Var2Data keys, compared on the low 16 bits (the full stored keys are
// 0x0D400001 and 0x0D400008; the high half is the field-type family).
constexpr std::uint16_t kVarKeyName = 1;
constexpr std::uint16_t kVarKeyData = 8;

// --- little-endian readers --------------------------------------------------

void require(std::vector<std::uint8_t> const& data, std::size_t offset, std::size_t bytes,
             char const* what) {
    if (offset > data.size() || data.size() - offset < bytes) {
        throw mpp::Error(std::string("core/cal: truncated ") + what);
    }
}

std::uint16_t read_u16(std::vector<std::uint8_t> const& data, std::size_t offset,
                       char const* what) {
    require(data, offset, 2, what);
    return static_cast<std::uint16_t>(data[offset]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[offset + 1]) << 8);
}

std::uint32_t read_u32(std::vector<std::uint8_t> const& data, std::size_t offset,
                       char const* what) {
    require(data, offset, 4, what);
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

// Var2Data block: u32 byte length followed by that many payload bytes.
std::vector<std::uint8_t> read_block(std::vector<std::uint8_t> const& var2, std::uint64_t offset) {
    if (offset > var2.size()) {
        throw mpp::Error("core/cal: Var2Data block offset past the stream");
    }
    const auto start = static_cast<std::size_t>(offset);
    const std::uint32_t length = read_u32(var2, start, "Var2Data block length");
    require(var2, start + 4, length, "Var2Data block payload");
    return std::vector<std::uint8_t>(var2.begin() + static_cast<std::ptrdiff_t>(start + 4),
                                     var2.begin() + static_cast<std::ptrdiff_t>(start + 4 + length));
}

std::size_t align4(std::uint32_t value) {
    return (static_cast<std::size_t>(value) + 3u) & ~static_cast<std::size_t>(3u);
}

// UTF-16LE, NUL-terminated; the stored byte length includes the terminator.
std::string decode_utf16(std::vector<std::uint8_t> const& bytes, std::size_t max_bytes) {
    std::string out;
    const std::size_t usable = std::min(bytes.size(), max_bytes);
    out.reserve(usable / 2);
    for (std::size_t i = 0; i + 1 < usable; i += 2) {
        const std::uint32_t unit = static_cast<std::uint32_t>(bytes[i]) |
                                   (static_cast<std::uint32_t>(bytes[i + 1]) << 8);
        if (unit == 0) {
            break;
        }
        // Names are ASCII in every file measured; anything else becomes '?'.
        out.push_back(unit < 0x80 ? static_cast<char>(unit) : '?');
    }
    return out;
}

std::string ascii_lower(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return value;
}

// --- 114/Props --------------------------------------------------------------

// Props14 header: 16 bytes, record count as u16 at +12.  Each record is
// (u32 data byte length, u32 key, u32 unused, data), padded to an even length.
std::map<std::uint32_t, std::vector<std::uint8_t>> parse_props(
    std::vector<std::uint8_t> const& props) {
    std::map<std::uint32_t, std::vector<std::uint8_t>> out;
    if (props.size() < kMetaHeaderBytes) {
        return out;
    }
    const std::size_t count = read_u16(props, 12, "Props header");
    std::size_t offset = kMetaHeaderBytes;
    for (std::size_t i = 0; i < count; ++i) {
        if (offset + 12 > props.size()) {
            break;
        }
        const std::uint32_t length = read_u32(props, offset, "Props record length");
        const std::uint32_t key = read_u32(props, offset + 4, "Props record key");
        offset += 12;
        if (length == 0 || length > props.size() - offset) {
            break;
        }
        out.emplace(key, std::vector<std::uint8_t>(
                             props.begin() + static_cast<std::ptrdiff_t>(offset),
                             props.begin() + static_cast<std::ptrdiff_t>(offset + length)));
        offset += length;
        if ((length & 1u) != 0) {
            ++offset;  // 2-byte alignment
        }
    }
    return out;
}

// --- 114/TBkndCal/VarMeta ---------------------------------------------------

struct VarEntry {
    std::uint32_t uid = 0;
    std::uint32_t offset = 0;
    std::uint16_t key = 0;
};

std::vector<VarEntry> parse_var_meta(std::vector<std::uint8_t> const& var_meta,
                                     std::uint64_t var2_size, CalendarTable& table) {
    std::vector<VarEntry> out;
    if (var_meta.size() < kVarMetaHeaderBytes) {
        ++table.metadata_anomalies;
        return out;
    }
    const std::uint32_t magic = read_u32(var_meta, 0, "VarMeta header");
    const std::uint32_t declared = read_u32(var_meta, 8, "VarMeta entry count");
    const std::uint64_t declared_var2 = read_u32(var_meta, 0x14, "VarMeta Var2Data size");
    if (magic != kMetaMagic || declared_var2 != var2_size) {
        ++table.metadata_anomalies;
    }
    const std::size_t entries = (var_meta.size() - kVarMetaHeaderBytes) / kVarMetaEntryBytes;
    if (kVarMetaHeaderBytes + entries * kVarMetaEntryBytes != var_meta.size() ||
        entries != declared) {
        ++table.metadata_anomalies;
    }
    out.reserve(entries);
    for (std::size_t i = 0; i < entries; ++i) {
        const std::size_t at = kVarMetaHeaderBytes + i * kVarMetaEntryBytes;
        VarEntry entry;
        entry.uid = read_u32(var_meta, at, "VarMeta entry");
        entry.offset = read_u32(var_meta, at + 4, "VarMeta entry");
        entry.key = static_cast<std::uint16_t>(read_u32(var_meta, at + 8, "VarMeta entry") & 0xFFFFu);
        out.push_back(entry);
    }
    return out;
}

// --- week resolution --------------------------------------------------------

WeekSpec stock_week_impl() {
    WeekSpec week{};
    const std::vector<WorkInterval> day = {{4800, 2400}, {7800, 2400}};
    for (std::size_t i = 1; i <= 5; ++i) {  // Monday..Friday
        week[i].use_base = false;
        week[i].intervals = day;
    }
    // Sunday and Saturday stay non-working custom days (use_base false, no intervals).
    week[0].use_base = false;
    week[6].use_base = false;
    return week;
}

// Resolve a calendar's seven days through its base chain.  A "use base" day in a
// base calendar falls back to 114/Props DEFAULT_CALENDAR_HOURS (which is how the
// Standard calendar is stored); in a derived calendar it falls back to the
// parent's resolved day.
WeekSpec resolve_week(CalendarTable const& table, CalendarDefinition const& calendar,
                      int depth) {
    if (depth > 8) {
        return table.default_week;
    }
    WeekSpec out{};
    if (!calendar.has_week_data) {
        if (calendar.base_uid > 0) {
            const CalendarDefinition* parent = find_calendar_by_uid(table, calendar.base_uid);
            return parent != nullptr ? resolve_week(table, *parent, depth + 1) : table.default_week;
        }
        return table.default_week;
    }
    for (std::size_t i = 0; i < out.size(); ++i) {
        if (!calendar.week[i].use_base) {
            out[i] = calendar.week[i];
            continue;
        }
        if (calendar.base_uid > 0) {
            const CalendarDefinition* parent = find_calendar_by_uid(table, calendar.base_uid);
            const WeekSpec parent_week =
                parent != nullptr ? resolve_week(table, *parent, depth + 1) : table.default_week;
            out[i] = parent_week[i];
        } else {
            out[i] = table.default_week[i];
        }
    }
    return out;
}

// Base calendars first, so a child's exceptions are applied last and win.
void collect_chain(CalendarTable const& table, CalendarDefinition const& calendar,
                   std::vector<CalendarDefinition const*>& chain, int depth) {
    if (depth > 8) {
        return;
    }
    if (calendar.base_uid > 0) {
        const CalendarDefinition* parent = find_calendar_by_uid(table, calendar.base_uid);
        if (parent != nullptr) {
            collect_chain(table, *parent, chain, depth + 1);
        }
    }
    chain.push_back(&calendar);
}

// --- recurring-exception expansion helpers (see expand_recurring_exception) --

constexpr bool is_leap_year(std::int32_t year) noexcept {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

std::int32_t days_in_month(std::int32_t year, std::int32_t month) noexcept {
    constexpr std::int32_t kMonthDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) {
        return 0;
    }
    if (month == 2 && is_leap_year(year)) {
        return 29;
    }
    return kMonthDays[static_cast<std::size_t>(month - 1)];
}

core::sched::CivilDate civil_of_day(std::int32_t day) noexcept {
    return core::sched::to_civil(static_cast<core::sched::date32>(day) << 16);
}

// Day number of a civil date, or 0 when the month has no such day.
std::int32_t day_of_civil(std::int32_t year, std::int32_t month, std::int32_t day) noexcept {
    if (day < 1 || day > days_in_month(year, month)) {
        return 0;
    }
    core::sched::CivilDate civil;
    civil.year = year;
    civil.month = month;
    civil.day = day;
    return core::sched::day_of(core::sched::from_civil(civil));
}

// Day-of-month of the `position`-th `weekday` in (year, month); position 0 means
// "the last one".  0 when the month has no such day.
std::int32_t nth_weekday_of_month(std::int32_t year, std::int32_t month, std::int32_t weekday,
                                 std::int32_t position) noexcept {
    const std::int32_t length = days_in_month(year, month);
    if (length == 0 || weekday < 0 || weekday > 6 || position < 0 || position > 5) {
        return 0;
    }
    if (position == 0) {
        for (std::int32_t day = length; day >= 1; --day) {
            if (core::sched::weekday_of_day(day_of_civil(year, month, day)) == weekday) {
                return day;
            }
        }
        return 0;
    }
    std::int32_t seen = 0;
    for (std::int32_t day = 1; day <= length; ++day) {
        if (core::sched::weekday_of_day(day_of_civil(year, month, day)) == weekday) {
            if (++seen == position) {
                return day;
            }
        }
    }
    return 0;
}

}  // namespace

// --- public decoders --------------------------------------------------------

WeekSpec stock_default_week() { return stock_week_impl(); }

std::size_t CalendarTable::base_calendar_count() const noexcept {
    std::size_t count = 0;
    for (CalendarDefinition const& calendar : calendars) {
        if (calendar.uid > 0 && calendar.base_uid <= 0) {
            ++count;
        }
    }
    return count;
}

std::size_t CalendarTable::derived_calendar_count() const noexcept {
    std::size_t count = 0;
    for (CalendarDefinition const& calendar : calendars) {
        if (calendar.uid > 0 && calendar.base_uid > 0) {
            ++count;
        }
    }
    return count;
}

std::size_t CalendarTable::exception_count() const noexcept {
    std::size_t count = 0;
    for (CalendarDefinition const& calendar : calendars) {
        count += calendar.exceptions.size();
    }
    return count;
}

std::size_t CalendarTable::work_week_count() const noexcept {
    std::size_t count = 0;
    for (CalendarDefinition const& calendar : calendars) {
        count += calendar.work_weeks.size();
    }
    return count;
}

WeekSpec decode_day_blocks(std::vector<std::uint8_t> const& data, std::size_t offset) {
    WeekSpec week{};
    for (std::size_t day = 0; day < week.size(); ++day) {
        const std::size_t at = offset + day * kDayBlockBytes;
        require(data, at, kDayBlockBytes, "calendar day block");
        const std::uint16_t day_type = read_u16(data, at, "calendar day type");
        if (day_type == 1) {
            week[day] = DaySpec{};  // use the inherited times
            continue;
        }
        DaySpec spec;
        spec.use_base = false;
        std::int32_t count = read_u16(data, at + 2, "calendar day interval count");
        count = std::min(count, kMaxStoredIntervals);
        spec.intervals.reserve(static_cast<std::size_t>(count));
        for (std::int32_t i = 0; i < count; ++i) {
            const std::int32_t start = read_u16(data, at + 8 + static_cast<std::size_t>(i) * 2,
                                                "calendar interval start");
            const std::int32_t length = static_cast<std::int32_t>(
                read_u32(data, at + 20 + static_cast<std::size_t>(i) * 4,
                         "calendar interval length"));
            // Mirrors mpxj's guard: a zero or longer-than-a-day interval is junk.
            if (length < 1 || length > kMaxDayTicks) {
                continue;
            }
            spec.intervals.push_back(WorkInterval{start, length});
        }
        week[day] = std::move(spec);
    }
    return week;
}

CalendarData decode_calendar_data(std::vector<std::uint8_t> const& data) {
    CalendarData out;
    if (data.size() < kWeekAreaBytes) {
        throw mpp::Error("core/cal: calendar data block is shorter than 420 bytes");
    }
    out.days = decode_day_blocks(data, 0);
    if (data.size() == kWeekAreaBytes) {
        return out;  // week only, no exception count
    }

    std::size_t offset = kWeekAreaBytes;
    const std::uint16_t exception_count = read_u16(data, offset, "calendar exception count");
    if (exception_count == 0) {
        offset += 4;  // count + padding, aligned to 8 bytes
    } else {
        offset += 4;  // skip the count word and its padding
        for (std::uint16_t i = 0; i < exception_count; ++i) {
            if (offset + kExceptionFixedBytes > data.size()) {
                out.truncated = true;
                return out;
            }
            DatedException exception;
            exception.start_day = read_u16(data, offset, "calendar exception start");
            exception.end_day = read_u16(data, offset + 2, "calendar exception end");
            exception.recurrence_type =
                read_u16(data, offset + kExceptionRecurrenceOffset, "calendar exception recurrence");
            exception.occurrences =
                read_u16(data, offset + kExceptionOccurrencesOffset, "calendar exception occurrences");
            std::int32_t periods =
                read_u16(data, offset + kExceptionPeriodCountOffset, "calendar exception periods");
            periods = std::min(periods, kMaxStoredIntervals);
            exception.working = periods > 0;
            for (std::int32_t p = 0; p < periods; ++p) {
                const std::int32_t start =
                    read_u16(data, offset + kExceptionStartOffset + static_cast<std::size_t>(p) * 2,
                             "calendar exception interval start");
                const std::int32_t length = static_cast<std::int32_t>(
                    read_u32(data, offset + kExceptionLengthOffset + static_cast<std::size_t>(p) * 4,
                             "calendar exception interval length"));
                if (length < 1 || length > kMaxDayTicks) {
                    continue;
                }
                exception.intervals.push_back(WorkInterval{start, length});
            }
            const std::uint32_t name_length =
                read_u32(data, offset + kExceptionNameOffset, "calendar exception name length");
            if (name_length > data.size()) {
                out.truncated = true;
                return out;
            }
            const std::size_t name_bytes = align4(name_length);
            if (offset + kExceptionFixedBytes + name_bytes > data.size()) {
                out.truncated = true;
                return out;
            }
            // The name itself is skipped on purpose: client-data rule (cal.h).
            offset += kExceptionFixedBytes + name_bytes;
            out.exceptions.push_back(std::move(exception));
        }
    }

    offset += 4;  // work-week section header
    while (offset + kWeekAreaBytes + kWorkWeekTailBytes <= data.size()) {
        DatedWorkWeek week;
        week.days = decode_day_blocks(data, offset);
        offset += kWeekAreaBytes;
        week.start_day = read_u16(data, offset, "calendar work week start");
        week.end_day = read_u16(data, offset + 2, "calendar work week end");
        offset += kWorkWeekTailBytes;
        const std::uint32_t name_length = read_u32(data, offset - 4, "calendar work week name length");
        if (name_length > data.size()) {
            out.truncated = true;
            break;
        }
        const std::size_t name_bytes = align4(name_length);
        if (offset + name_bytes > data.size()) {
            out.truncated = true;
            break;
        }
        offset += name_bytes;
        out.work_weeks.push_back(std::move(week));
    }
    // Trailing bytes that are not a whole work week are the 4-byte section
    // header of an empty section; anything else is a damaged tail.
    if (offset < data.size() && data.size() - offset > 4) {
        out.truncated = true;
    }
    return out;
}

CalendarTable decode_compound_file(mpp::CompoundFile const& file) {
    if (!file.has_stream("114/TBkndCal/FixedData")) {
        throw mpp::Error("core/cal: no 114/TBkndCal/FixedData stream");
    }
    const std::vector<std::uint8_t> fixed_data = file.read_stream("114/TBkndCal/FixedData");
    const std::vector<std::uint8_t> fixed_meta =
        file.has_stream("114/TBkndCal/FixedMeta") ? file.read_stream("114/TBkndCal/FixedMeta")
                                                  : std::vector<std::uint8_t>{};
    const std::vector<std::uint8_t> var_meta =
        file.has_stream("114/TBkndCal/VarMeta") ? file.read_stream("114/TBkndCal/VarMeta")
                                                : std::vector<std::uint8_t>{};
    const std::vector<std::uint8_t> var2 =
        file.has_stream("114/TBkndCal/Var2Data") ? file.read_stream("114/TBkndCal/Var2Data")
                                                 : std::vector<std::uint8_t>{};

    CalendarTable table;
    table.default_week = stock_week_impl();

    if (file.has_stream("114/Props")) {
        const std::map<std::uint32_t, std::vector<std::uint8_t>> props =
            parse_props(file.read_stream("114/Props"));
        const auto hours = props.find(kPropsDefaultCalendarHours);
        if (hours != props.end()) {
            if (hours->second.size() >= kWeekAreaBytes) {
                table.default_week = decode_day_blocks(hours->second, 0);
            } else {
                ++table.metadata_anomalies;
            }
        }
        const auto name = props.find(kPropsDefaultCalendarName);
        if (name != props.end()) {
            table.default_calendar_name = decode_utf16(name->second, name->second.size());
        }
    }

    if (fixed_data.size() < kFixedHeaderBytes ||
        (fixed_data.size() - kFixedHeaderBytes) % kCalendarRowBytes != 0) {
        throw mpp::Error("core/cal: 114/TBkndCal/FixedData is not 64 + 12*n bytes");
    }
    const std::size_t rows = (fixed_data.size() - kFixedHeaderBytes) / kCalendarRowBytes;

    // FixedMeta is only cross-checked: rows are read from FixedData's own size,
    // so a damaged metadata stream cannot shift them.
    if (fixed_meta.size() < kMetaHeaderBytes ||
        (fixed_meta.size() - kMetaHeaderBytes) % kMetaEntryBytes != 0) {
        ++table.metadata_anomalies;
    } else {
        const std::uint32_t magic = read_u32(fixed_meta, 0, "FixedMeta header");
        const std::uint32_t version = read_u32(fixed_meta, 4, "FixedMeta header");
        const std::uint32_t entries = read_u32(fixed_meta, 8, "FixedMeta header");
        const std::uint32_t declared_size = read_u32(fixed_meta, 12, "FixedMeta header");
        if (magic != kMetaMagic || version != kMetaVersion || declared_size != fixed_data.size() ||
            entries != 4 + rows || kMetaHeaderBytes + entries * kMetaEntryBytes != fixed_meta.size()) {
            ++table.metadata_anomalies;
        }
    }

    const std::vector<VarEntry> var_entries =
        parse_var_meta(var_meta, static_cast<std::uint64_t>(var2.size()), table);

    std::map<std::uint32_t, std::vector<VarEntry const*>> by_uid;
    for (VarEntry const& entry : var_entries) {
        by_uid[entry.uid].push_back(&entry);
    }

    table.calendars.reserve(rows);
    for (std::size_t row = 0; row < rows; ++row) {
        const std::size_t at = kFixedHeaderBytes + row * kCalendarRowBytes;
        CalendarDefinition calendar;
        calendar.base_uid = static_cast<std::int32_t>(read_u32(fixed_data, at, "calendar row"));
        calendar.resource_uid =
            static_cast<std::int32_t>(read_u32(fixed_data, at + 4, "calendar row"));
        calendar.uid = static_cast<std::int32_t>(read_u32(fixed_data, at + 8, "calendar row"));
        if (calendar.uid <= 0) {
            ++table.metadata_anomalies;  // mpxj skips these too
            continue;
        }
        const auto entries = by_uid.find(static_cast<std::uint32_t>(calendar.uid));
        if (entries != by_uid.end()) {
            for (VarEntry const* entry : entries->second) {
                if (entry->key == kVarKeyData && !calendar.has_week_data) {
                    const std::vector<std::uint8_t> block = read_block(var2, entry->offset);
                    CalendarData data = decode_calendar_data(block);
                    calendar.has_week_data = true;
                    calendar.week = std::move(data.days);
                    calendar.exceptions = std::move(data.exceptions);
                    calendar.work_weeks = std::move(data.work_weeks);
                    if (data.truncated) {
                        ++table.metadata_anomalies;
                    }
                    for (DatedException const& exception : calendar.exceptions) {
                        if (exception.recurrence_type > 1) {
                            ++table.recurring_exceptions;
                        }
                    }
                } else if (entry->key == kVarKeyName && calendar.name.empty()) {
                    calendar.name = decode_utf16(read_block(var2, entry->offset), 4096);
                }
            }
        }
        table.calendars.push_back(std::move(calendar));
    }

    // Report intervals that do not survive the minutes-only DayPattern.
    auto count_sub_minute = [&table](WeekSpec const& week) {
        for (DaySpec const& day : week) {
            for (WorkInterval const& interval : day.intervals) {
                if (interval.start_ticks % 10 != 0 || interval.length_ticks % 10 != 0) {
                    ++table.sub_minute_intervals;
                }
            }
        }
    };
    count_sub_minute(table.default_week);
    for (CalendarDefinition const& calendar : table.calendars) {
        count_sub_minute(calendar.week);
        for (DatedException const& exception : calendar.exceptions) {
            for (WorkInterval const& interval : exception.intervals) {
                if (interval.start_ticks % 10 != 0 || interval.length_ticks % 10 != 0) {
                    ++table.sub_minute_intervals;
                }
            }
        }
    }
    return table;
}

CalendarTable decode_file(std::string const& mpp_path) {
    return decode_compound_file(mpp::CompoundFile::from_file(mpp_path));
}

// --- builder ----------------------------------------------------------------

core::sched::DayPattern to_day_pattern(DaySpec const& day) {
    core::sched::DayPattern pattern{};
    if (day.use_base) {
        return pattern;  // unresolved "use base": callers resolve first
    }
    for (WorkInterval const& interval : day.intervals) {
        if (pattern.count >= core::sched::kMaxDayIntervals) {
            break;
        }
        const std::int32_t start_minute = interval.start_ticks / 10;
        const std::int32_t length_minute = (interval.length_ticks + 5) / 10;
        if (start_minute < 0 || start_minute > core::sched::kMinutesPerDay || length_minute < 1) {
            continue;
        }
        const std::size_t index = pattern.count;
        pattern.start_minute[index] = static_cast<std::uint16_t>(start_minute);
        pattern.length_minutes[index] = static_cast<std::uint32_t>(length_minute);
        ++pattern.count;
    }
    return pattern;
}

CalendarDefinition const* find_calendar(CalendarTable const& table, std::string_view name) {
    if (name.empty()) {
        return nullptr;
    }
    for (CalendarDefinition const& calendar : table.calendars) {
        if (calendar.name == name) {
            return &calendar;
        }
    }
    const std::string wanted = ascii_lower(std::string(name));
    for (CalendarDefinition const& calendar : table.calendars) {
        if (ascii_lower(calendar.name) == wanted) {
            return &calendar;
        }
    }
    return nullptr;
}

CalendarDefinition const* find_calendar_by_uid(CalendarTable const& table, std::int32_t uid) {
    for (CalendarDefinition const& calendar : table.calendars) {
        if (calendar.uid == uid) {
            return &calendar;
        }
    }
    return nullptr;
}

RecurrenceExpansion expand_recurring_exception(DatedException const& exception,
                                               std::int32_t max_days) {
    RecurrenceExpansion out;
    const std::int32_t type = exception.recurrence_type;
    if (type <= 1) {
        out.why = "not a recurring exception (PjExceptionType 0/1 is a plain range)";
        return out;
    }
    if (type > 7) {
        out.why = "PjExceptionType is outside 0..7";
        return out;
    }
    if (type == 7) {
        out.why = "day-count recurrence: the 'every N days' period is not decoded";
        return out;
    }
    if (exception.start_day <= 0) {
        out.why = "no recurrence start day";
        return out;
    }

    const std::int32_t first = exception.start_day;
    const std::int32_t last = std::max(first, exception.end_day);
    const core::sched::CivilDate start = civil_of_day(first);
    const core::sched::CivilDate end = civil_of_day(last);
    const std::int32_t weekday = core::sched::weekday_of_day(first);
    // The start day is a matching instance, so it carries the rule: its
    // day-of-month for types 2/4, and its position inside its own month for the
    // positional types 3/5 (1..4, or "last" when a further same-weekday day
    // would leave the month).
    const std::int32_t position =
        (start.day + core::sched::kDaysPerWeek > days_in_month(start.year, start.month))
            ? 0
            : ((start.day - 1) / core::sched::kDaysPerWeek + 1);

    const auto add = [&out, first, last](std::int32_t day) {
        if (day >= first && day <= last) {
            out.days.push_back(day);
        }
    };

    switch (type) {
        case 2:  // yearly: month + day-of-month
            for (std::int32_t year = start.year; year <= end.year; ++year) {
                add(day_of_civil(year, start.month, start.day));
            }
            break;
        case 3:  // yearly: month + position + weekday
            for (std::int32_t year = start.year; year <= end.year; ++year) {
                add(day_of_civil(year, start.month,
                                 nth_weekday_of_month(year, start.month, weekday, position)));
            }
            break;
        case 4:  // monthly: day-of-month
        case 5:  // monthly: position + weekday
            for (std::int32_t year = start.year; year <= end.year; ++year) {
                const std::int32_t first_month = year == start.year ? start.month : 1;
                const std::int32_t last_month = year == end.year ? end.month : 12;
                for (std::int32_t month = first_month; month <= last_month; ++month) {
                    const std::int32_t day =
                        type == 4 ? day_of_civil(year, month, start.day)
                                  : day_of_civil(year, month,
                                                 nth_weekday_of_month(year, month, weekday, position));
                    add(day);
                }
            }
            break;
        case 6:  // weekly: the start day's weekday, every week
            for (std::int32_t day = first; day <= last; day += core::sched::kDaysPerWeek) {
                add(day);
            }
            break;
        default:
            break;
    }

    if (out.days.empty()) {
        out.days.clear();
        out.why = "the expansion produced no day inside the recurrence range";
        return out;
    }
    if (max_days > 0 && static_cast<std::int32_t>(out.days.size()) > max_days) {
        out.days.clear();
        out.why = "the expansion is longer than the caller's day cap";
        return out;
    }
    // The occurrence word (+4) is a check, never a source of truth: it must agree
    // with the expansion or the expansion is refused (this is what catches a
    // period other than one week/month/year, which the decoded record cannot
    // express yet).
    if (exception.occurrences > 0 &&
        out.days.size() != static_cast<std::size_t>(exception.occurrences)) {
        out.days.clear();
        out.why = "the expansion disagrees with the stored occurrence word (+4)";
        return out;
    }
    out.applied = true;
    return out;
}

core::sched::Calendar build_calendar(CalendarTable const& table,
                                     CalendarDefinition const& calendar,
                                     RecurrenceReport* report) {
    const WeekSpec resolved = resolve_week(table, calendar, 0);
    core::sched::Week base_week{};
    for (std::size_t day = 0; day < base_week.size(); ++day) {
        base_week[day] = to_day_pattern(resolved[day]);
    }
    core::sched::Calendar result(base_week);

    std::vector<CalendarDefinition const*> chain;
    collect_chain(table, calendar, chain, 0);

    // Week start day -> the seven day patterns that week gets.  core::sched only
    // has a whole-week override (no dated-exception API), so every dated change
    // is merged into its week here.
    std::map<std::int32_t, core::sched::Week> overrides;
    auto week_for = [&overrides, &base_week](std::int32_t week_start_day) -> core::sched::Week& {
        const auto found = overrides.find(week_start_day);
        if (found != overrides.end()) {
            return found->second;
        }
        return overrides.emplace(week_start_day, base_week).first->second;
    };
    auto apply_day = [&week_for](std::int32_t day, core::sched::DayPattern const& pattern) {
        const std::int32_t weekday = core::sched::weekday_of_day(day);
        const std::int32_t week_start = day - weekday;
        core::sched::Week& week = week_for(week_start);
        week[static_cast<std::size_t>(weekday)] = pattern;
    };

    // Dated work weeks first, then exceptions: an exception for the same day
    // wins over the week's pattern, which is MS Project's precedence.
    for (CalendarDefinition const* link : chain) {
        for (DatedWorkWeek const& week : link->work_weeks) {
            const std::int32_t last = std::max(week.start_day, week.end_day);
            for (std::int32_t day = week.start_day; day <= last; ++day) {
                const std::int32_t weekday = core::sched::weekday_of_day(day);
                apply_day(day, to_day_pattern(week.days[static_cast<std::size_t>(weekday)]));
                if (day == last) {
                    break;  // guard against INT32_MAX
                }
            }
        }
    }
    const core::sched::DayPattern non_working{};
    for (CalendarDefinition const* link : chain) {
        for (DatedException const& exception : link->exceptions) {
            core::sched::DayPattern pattern{};
            if (exception.working) {
                DaySpec spec;
                spec.use_base = false;
                spec.intervals = exception.intervals;
                pattern = to_day_pattern(spec);
            } else {
                pattern = non_working;
            }
            std::vector<std::int32_t> days;
            if (exception.recurrence_type > 1) {
                // Recurring: expand into concrete days (see cal.h for exactly
                // which parameters are measured and which are [I]), then apply
                // them exactly like a plain range.  An expansion that cannot be
                // justified is skipped and counted, never guessed.
                const RecurrenceExpansion expansion = expand_recurring_exception(exception);
                if (!expansion.applied) {
                    if (report != nullptr) {
                        ++report->not_applied;
                    }
                    continue;
                }
                if (report != nullptr) {
                    ++report->applied;
                }
                days = expansion.days;
            } else {
                const std::int32_t last = std::max(exception.start_day, exception.end_day);
                for (std::int32_t day = exception.start_day; day <= last; ++day) {
                    days.push_back(day);
                }
            }
            for (const std::int32_t day : days) {
                apply_day(day, pattern);
            }
        }
    }

    for (auto const& override_entry : overrides) {
        result.set_work_week(override_entry.first, override_entry.second);
    }
    return result;
}

core::sched::Calendar build_project_calendar(CalendarTable const& table,
                                             std::string const& calendar_name,
                                             RecurrenceReport* report) {
    const std::string wanted = calendar_name.empty() ? table.default_calendar_name : calendar_name;
    if (CalendarDefinition const* calendar = find_calendar(table, wanted)) {
        return build_calendar(table, *calendar, report);
    }
    if (!calendar_name.empty()) {
        throw mpp::Error("core/cal: no calendar named \"" + calendar_name + "\"");
    }
    // The project's default name is missing from the table: only unambiguous
    // when the file has exactly one base calendar.
    CalendarDefinition const* only = nullptr;
    std::size_t bases = 0;
    for (CalendarDefinition const& calendar : table.calendars) {
        if (calendar.uid > 0 && calendar.base_uid <= 0) {
            only = &calendar;
            ++bases;
        }
    }
    if (bases == 1 && only != nullptr) {
        return build_calendar(table, *only, report);
    }
    throw mpp::Error("core/cal: the project's default calendar is not in 114/TBkndCal");
}

core::sched::Calendar load_project_calendar(std::string const& mpp_path,
                                            std::string const& calendar_name) {
    return build_project_calendar(decode_file(mpp_path), calendar_name);
}

core::sched::Calendar load_project_calendar(mpp::CompoundFile const& file,
                                            std::string const& calendar_name) {
    return build_project_calendar(decode_compound_file(file), calendar_name);
}

}  // namespace core::cal
