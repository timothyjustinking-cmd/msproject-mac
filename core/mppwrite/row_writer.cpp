/*
 * row_writer.cpp -- one consistent task row, in both tables.  See row_writer.h
 * for the layout, the measured evidence and the reasoning.
 */

#include "row_writer.h"

#include <string>

namespace mppwrite {
namespace {

std::string table_stream(std::string_view table, std::string_view name) {
    return std::string(table) + "/" + std::string(name);
}

std::uint32_t get_u32(std::vector<std::uint8_t> const& b, std::size_t o) {
    return static_cast<std::uint32_t>(b[o]) |
           (static_cast<std::uint32_t>(b[o + 1]) << 8) |
           (static_cast<std::uint32_t>(b[o + 2]) << 16) |
           (static_cast<std::uint32_t>(b[o + 3]) << 24);
}

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

void write_slack(std::vector<std::uint8_t>& b, std::size_t o, std::int32_t v) {
    put_u32(b, o, static_cast<std::uint32_t>(v));
}

std::int32_t read_slack(std::vector<std::uint8_t> const& b, std::size_t o) {
    return static_cast<std::int32_t>(get_u32(b, o));
}

bool is_valid_date(std::uint32_t raw) {
    return raw != 0 && raw != kInvalidDate;
}

std::string row_label(std::size_t row, std::uint32_t uid) {
    return "row " + std::to_string(row) + " (uid " + std::to_string(uid) + ")";
}

/*
 * The refusal that gives this file its point.  Each of start/finish/duration has
 * several stored copies; if exactly one copy would change, the caller is asking
 * for the edit spec/mpp-writer.md §8.1 measured MS Project recalculating away.
 * Several copies changing is fine; nothing changing is fine.
 */
void refuse_single_copy(std::string const& what, std::size_t row,
                        std::uint32_t uid,
                        std::vector<std::uint32_t> const& before,
                        std::vector<std::uint32_t> const& after,
                        std::vector<std::string> const& copy_names) {
    std::size_t changed = 0;
    std::size_t which = 0;
    for (std::size_t i = 0; i < before.size(); ++i) {
        if (before[i] != after[i]) {
            ++changed;
            which = i;
        }
    }
    if (changed != 1) {
        return;
    }
    throw Error("write_task_row: " + row_label(row, uid) + ": single-copy " +
                what + " write refused -- only " + copy_names[which] +
                " would change.  A row whose copies disagree is exactly the "
                "state spec/mpp-writer.md §8.1 measured MS Project ignoring; "
                "supply every copy, or set the copies you want re-derived from "
                "the value to kInherit (remaining_duration derives from "
                "duration - actual_duration, fixed2_duration from duration). "
                "TaskTable::set_u32 remains available for raw bytes.");
}

} /* namespace */

/* --- Fixed2Data ---------------------------------------------------------- */

std::size_t fixed2_record_offset(std::size_t row) {
    return fixed2_row::kStride * (row + fixed2_row::kHeaderRecords);
}

std::size_t fixed2_row_count(std::size_t fixed2_bytes) {
    if (fixed2_bytes < fixed2_row::kStride * fixed2_row::kHeaderRecords ||
        fixed2_bytes % fixed2_row::kStride != 0) {
        throw Error("Fixed2Data is " + std::to_string(fixed2_bytes) +
                    " bytes, not 64 * (n + 3)");
    }
    return fixed2_bytes / fixed2_row::kStride - fixed2_row::kHeaderRecords;
}

std::vector<std::uint8_t> read_fixed2_data(CfbDocument const& doc,
                                           std::string_view table) {
    std::string const path = table_stream(table, "Fixed2Data");
    if (!doc.has_stream(path)) {
        throw Error("no " + path + " stream");
    }
    return doc.read_stream(path);
}

/* --- values -------------------------------------------------------------- */

bool operator==(TaskValues const& a, TaskValues const& b) {
    return a.start == b.start && a.finish == b.finish &&
           a.project_start == b.project_start &&
           a.project_finish == b.project_finish &&
           a.late_start == b.late_start && a.late_finish == b.late_finish &&
           a.duration == b.duration &&
           a.remaining_duration == b.remaining_duration &&
           a.actual_duration == b.actual_duration &&
           a.total_slack == b.total_slack && a.start_slack == b.start_slack &&
           a.priority == b.priority &&
           a.percent_complete == b.percent_complete &&
           a.constraint_type == b.constraint_type &&
           a.constraint_date == b.constraint_date &&
           a.start_anchor == b.start_anchor &&
           a.fixed2_start == b.fixed2_start &&
           a.fixed2_finish == b.fixed2_finish &&
           a.fixed2_duration == b.fixed2_duration;
}

TaskValues read_task_values(CfbDocument const& doc, std::size_t row,
                            std::string_view table) {
    TaskTable const t = TaskTable::from_document(doc, table);
    if (row >= t.rows()) {
        throw Error("read_task_values: " + row_label(row, 0) + " but the table has " +
                    std::to_string(t.rows()) + " rows");
    }
    TaskValues v;
    v.start = t.u32(row, task_row::kStart);
    v.finish = t.u32(row, task_row::kFinish);
    v.project_start = t.u32(row, task_row::kStart2);
    v.project_finish = t.u32(row, task_row::kFinish2);
    v.late_start = t.u32(row, task_row::kStart3);
    v.late_finish = t.u32(row, task_row::kFinish3);
    v.duration = t.u32(row, task_row::kDuration);
    v.remaining_duration = t.u32(row, task_row::kRemainingDuration);
    v.actual_duration = t.u32(row, task_row::kActualDuration);
    v.total_slack = read_slack(t.fixed_data(), task_row_offset(row) +
                                                    task_row::kTotalSlack);
    v.start_slack = read_slack(t.fixed_data(), task_row_offset(row) +
                                                    task_row::kStartSlack);
    v.priority = t.u16(row, task_row::kPriority);
    v.percent_complete = t.u16(row, task_row::kPercentComplete);
    v.constraint_type = t.u16(row, task_row::kConstraintType);
    v.constraint_date = t.u32(row, task_row::kConstraintDate);
    v.start_anchor = t.u32(row, task_row::kStartAnchor);
    if (row == 0) {
        /* The project row's Fixed2Data date/duration words are 0xFFFFFFFF and
         * are never written; report them as "not stored". */
        v.fixed2_start = kInvalidDate;
        v.fixed2_finish = kInvalidDate;
        v.fixed2_duration = kInvalidDate;
    } else {
        std::vector<std::uint8_t> const f2 = read_fixed2_data(doc, table);
        if (fixed2_row_count(f2.size()) != t.rows()) {
            throw Error("read_task_values: Fixed2Data holds " +
                        std::to_string(fixed2_row_count(f2.size())) +
                        " rows, FixedData holds " + std::to_string(t.rows()));
        }
        std::size_t const p = fixed2_record_offset(row);
        v.fixed2_start = get_u32(f2, p + fixed2_row::kStart);
        v.fixed2_finish = get_u32(f2, p + fixed2_row::kFinish);
        v.fixed2_duration = get_u32(f2, p + fixed2_row::kDuration);
    }
    return v;
}

TaskValues resolve_task_values(CfbDocument const& doc, std::size_t row,
                               TaskValues const& values,
                               std::string_view table) {
    TaskTable const t = TaskTable::from_document(doc, table);
    if (row >= t.rows()) {
        throw Error("write_task_row: " + row_label(row, 0) +
                    " but the table has " + std::to_string(t.rows()) + " rows");
    }
    std::uint32_t const uid = t.uid(row);
    TaskValues const old = read_task_values(doc, row, table);
    TaskValues v = values;

    /* 1. Every date copy must be supplied.  This is the API-level refusal of a
     *    single-copy patch: there is no way to say "just +96". */
    std::string missing;
    auto need = [&missing](std::uint32_t value, char const* label) {
        if (!is_valid_date(value)) {
            if (!missing.empty()) {
                missing += ", ";
            }
            missing += label;
        }
    };
    need(v.start, "start (+96)");
    need(v.finish, "finish (+100)");
    need(v.project_start, "project_start (+104)");
    need(v.project_finish, "project_finish (+108)");
    need(v.late_start, "late_start (+112)");
    need(v.late_finish, "late_finish (+116)");
    if (!missing.empty()) {
        throw Error("write_task_row: " + row_label(row, uid) +
                    ": incomplete date copies, missing " + missing +
                    ".  A single-copy write is refused (spec/mpp-writer.md "
                    "§8.1: Project recalculates a row whose copies disagree). "
                    "Read the row first with read_task_values and change every "
                    "copy that belongs to the value.");
    }

    /* 2. Duration family: actual + remaining == duration, and remaining
     *    defaults to duration - actual (var-pct.mpp, Project's own save). */
    v.duration = (v.duration == kInherit) ? old.duration : v.duration;
    v.actual_duration = (v.actual_duration == kInherit) ? old.actual_duration
                                                       : v.actual_duration;
    if (v.actual_duration > v.duration) {
        throw Error("write_task_row: " + row_label(row, uid) +
                    ": actual duration " + std::to_string(v.actual_duration) +
                    " exceeds duration " + std::to_string(v.duration));
    }
    if (v.remaining_duration == kInherit) {
        v.remaining_duration = v.duration - v.actual_duration;
    }
    if (v.actual_duration + v.remaining_duration != v.duration) {
        throw Error("write_task_row: " + row_label(row, uid) +
                    ": duration family disagrees: actual " +
                    std::to_string(v.actual_duration) + " + remaining " +
                    std::to_string(v.remaining_duration) + " != duration " +
                    std::to_string(v.duration));
    }

    /* 3. Slack: +74 and +190 carry TotalSlack, +186 StartSlack.
     *
     * Same "keep a copy a copy" rule as Fixed2Data below: +186 equals +74 on
     * every measured TASK row (base/w3-o1/outline1/resaved-base) but differs on
     * w3-o1's project row (0 vs 4800), so it follows TotalSlack only when it
     * already did and the caller has not overridden it. */
    std::int32_t const total =
        (values.total_slack == kInheritSlack) ? old.total_slack : values.total_slack;
    v.total_slack = total;
    bool const start_was_a_copy = old.start_slack == old.total_slack;
    std::int32_t const wanted_start =
        (values.start_slack == kInheritSlack) ? old.start_slack
                                              : values.start_slack;
    /* "Explicit" means "different from what is stored"; a value carried over
     * from read_task_values is not an instruction to keep the old number. */
    bool const overridden = wanted_start != old.start_slack;
    v.start_slack = (overridden || !start_was_a_copy) ? wanted_start : total;

    /* 4. Single-copy fields. */
    if (v.priority == static_cast<std::uint16_t>(kInherit)) {
        v.priority = old.priority;
    }
    if (v.percent_complete == static_cast<std::uint16_t>(kInherit)) {
        v.percent_complete = old.percent_complete;
    }
    if (v.constraint_type == static_cast<std::uint16_t>(kInherit)) {
        v.constraint_type = old.constraint_type;
    }
    if (v.constraint_date == kInherit) {
        v.constraint_date = old.constraint_date;
    }
    /* The start anchor is inherited only: write_task_row has no opinion on
     * +174.  `write_manual_task_row` is what sets it. */
    if (v.start_anchor == kInherit) {
        v.start_anchor = old.start_anchor;
    }
    if (v.percent_complete > 100) {
        throw Error("write_task_row: " + row_label(row, uid) +
                    ": percent complete " + std::to_string(v.percent_complete) +
                    " > 100");
    }

    /* 5. The Fixed2Data copies.
     *
     * "Keep a copy a copy": if a Fixed2Data word equalled its FixedData source
     * in the stored row and the caller has not overridden it, it follows the new
     * source value.  That is what makes read -> change one date -> write land
     * every copy without the caller having to restate what is already a copy.
     * An explicit override is written as given (w3-o1.mpp stores 0 in +54/+58,
     * so the derivation cannot be unconditional). */
    if (row == 0) {
        v.fixed2_start = kInvalidDate;
        v.fixed2_finish = kInvalidDate;
        v.fixed2_duration = kInvalidDate;
    } else {
        auto keep_copy = [](std::uint32_t value, std::uint32_t old_copy,
                            std::uint32_t old_source, std::uint32_t new_source) {
            if (value == kInherit) {
                return new_source;
            }
            if (value == old_copy && old_copy == old_source) {
                return new_source;
            }
            return value;
        };
        v.fixed2_start = keep_copy(v.fixed2_start, old.fixed2_start,
                                   old.project_start, v.project_start);
        v.fixed2_finish = keep_copy(v.fixed2_finish, old.fixed2_finish,
                                    old.project_finish, v.project_finish);
        v.fixed2_duration = keep_copy(v.fixed2_duration, old.fixed2_duration,
                                      old.duration, v.duration);
        if (!is_valid_date(v.fixed2_start) || !is_valid_date(v.fixed2_finish)) {
            throw Error("write_task_row: " + row_label(row, uid) +
                        ": Fixed2Data date copy is missing (every measured task "
                        "row stores +46/+50 and +54)");
        }
    }

    /* 6. The single-copy refusal, against what is already stored.  The
     *    Fixed2Data words are members of their families: a change that moves
     *    +104 but leaves +46/+50 behind is the same hazard as one that moves
     *    +96 alone. */
    refuse_single_copy("start", row, uid,
                       {old.start, old.project_start, old.late_start,
                        old.fixed2_start},
                       {v.start, v.project_start, v.late_start,
                        v.fixed2_start},
                       {"+96 (task start)", "+104 (project-frame start)",
                        "+112 (late start)", "Fixed2Data +46/+50"});
    refuse_single_copy("finish", row, uid,
                       {old.finish, old.project_finish, old.late_finish,
                        old.fixed2_finish},
                       {v.finish, v.project_finish, v.late_finish,
                        v.fixed2_finish},
                       {"+100 (task finish)", "+108 (project-frame finish)",
                        "+116 (late finish)", "Fixed2Data +54"});
    if (row != 0) {
        refuse_single_copy("duration", row, uid,
                           {old.duration, old.fixed2_duration},
                           {v.duration, v.fixed2_duration},
                           {"FixedData +84", "Fixed2Data +58"});
    }
    return v;
}

/* --- writing ------------------------------------------------------------- */

RowWriteReport write_task_row(CfbDocument& doc, std::size_t row,
                              TaskValues const& values,
                              std::string_view table) {
    TaskTable const t = TaskTable::from_document(doc, table);
    if (row >= t.rows()) {
        throw Error("write_task_row: " + row_label(row, 0) +
                    " but the table has " + std::to_string(t.rows()) + " rows");
    }
    std::uint32_t const uid = t.uid(row);
    std::vector<std::uint8_t> fixed2 = read_fixed2_data(doc, table);
    if (fixed2_row_count(fixed2.size()) != t.rows()) {
        throw Error("write_task_row: Fixed2Data holds " +
                    std::to_string(fixed2_row_count(fixed2.size())) +
                    " rows but FixedData holds " + std::to_string(t.rows()));
    }

    RowWriteReport report;
    report.written = resolve_task_values(doc, row, values, table);
    TaskValues const& v = report.written;

    std::vector<std::uint8_t> fixed = t.fixed_data();
    std::size_t const p = task_row_offset(row);
    put_u32(fixed, p + task_row::kActualDuration, v.actual_duration);
    put_u32(fixed, p + task_row::kDuration, v.duration);
    put_u32(fixed, p + task_row::kRemainingDuration, v.remaining_duration);
    put_u32(fixed, p + task_row::kStart, v.start);
    put_u32(fixed, p + task_row::kFinish, v.finish);
    put_u32(fixed, p + task_row::kStart2, v.project_start);
    put_u32(fixed, p + task_row::kFinish2, v.project_finish);
    put_u32(fixed, p + task_row::kStart3, v.late_start);
    put_u32(fixed, p + task_row::kFinish3, v.late_finish);
    write_slack(fixed, p + task_row::kTotalSlack, v.total_slack);
    write_slack(fixed, p + task_row::kStartSlack, v.start_slack);
    write_slack(fixed, p + task_row::kTotalSlackCopy, v.total_slack);
    put_u16(fixed, p + task_row::kPriority, v.priority);
    put_u16(fixed, p + task_row::kPercentComplete, v.percent_complete);
    put_u16(fixed, p + task_row::kConstraintType, v.constraint_type);
    put_u32(fixed, p + task_row::kConstraintDate, v.constraint_date);
    put_u32(fixed, p + task_row::kStartAnchor, v.start_anchor);

    if (row != 0) {
        std::size_t const r = fixed2_record_offset(row);
        put_u32(fixed2, r + fixed2_row::kStart, v.fixed2_start);
        put_u32(fixed2, r + fixed2_row::kStartCopy, v.fixed2_start);
        put_u32(fixed2, r + fixed2_row::kFinish, v.fixed2_finish);
        put_u32(fixed2, r + fixed2_row::kDuration, v.fixed2_duration);
    }

    store_task_fixed_data(doc, fixed, table);
    if (row != 0) {
        doc.replace_stream(table_stream(table, "Fixed2Data"), fixed2);
    }

    /* Read it back through the container: if a byte landed anywhere but where
     * the reader looks, this is where it shows. */
    TaskValues const after = read_task_values(doc, row, table);
    if (after != v) {
        throw Error("write_task_row: " + row_label(row, uid) +
                    ": the row does not read back as written (" +
                    "start " + format_date(after.start) + " vs " +
                    format_date(v.start) + ", duration " +
                    std::to_string(after.duration) + " vs " +
                    std::to_string(v.duration) + ")");
    }

    /* Legal but suspicious: warn rather than refuse, because these can be a
     * real schedule's answer (a negative-duration row cannot, a late start after
     * the late finish can). */
    auto warn_if = [&report, row, uid](bool condition, std::string const& text) {
        if (condition) {
            report.warnings.push_back(row_label(row, uid) + ": " + text);
        }
    };
    warn_if(v.start > v.finish,
            "start " + format_date(v.start) + " is after finish " +
                format_date(v.finish));
    warn_if(v.project_start > v.project_finish,
            "project-frame start " + format_date(v.project_start) +
                " is after project-frame finish " +
                format_date(v.project_finish));
    warn_if(v.late_start > v.late_finish,
            "late start " + format_date(v.late_start) + " is after late finish " +
                format_date(v.late_finish));
    if (row != 0) {
        warn_if(v.fixed2_duration != v.duration,
                "Fixed2Data +58 (" + std::to_string(v.fixed2_duration) +
                    ") disagrees with FixedData +84 (" +
                    std::to_string(v.duration) + ")");
    }
    warn_if(v.percent_complete > 0 && v.actual_duration == 0,
            "percent complete " + std::to_string(v.percent_complete) +
                " with ActualDuration 0");
    return report;
}

/* --- W29: the manual-task write path ------------------------------------- */

ManualWriteReport write_manual_task_row(CfbDocument& doc, std::size_t row,
                                        ManualTaskWrite const& values,
                                        std::string_view table) {
    if (row == 0) {
        throw Error("write_manual_task_row: row 0 is the project row; it has no "
                    "Fixed2Data record to write");
    }
    TaskTable const t = TaskTable::from_document(doc, table);
    if (row >= t.rows()) {
        throw Error("write_manual_task_row: " + row_label(row, 0) +
                    " but the table has " + std::to_string(t.rows()) + " rows");
    }
    std::uint32_t const uid = t.uid(row);
    if (!is_valid_date(values.start) || !is_valid_date(values.finish)) {
        throw Error("write_manual_task_row: " + row_label(row, uid) + ": start (" +
                    format_date(values.start) + ") and finish (" +
                    format_date(values.finish) + ") must both be valid dates");
    }
    if (values.late_start != kInherit && !is_valid_date(values.late_start)) {
        throw Error("write_manual_task_row: " + row_label(row, uid) +
                    ": late_start is set but is not a valid date");
    }
    if (values.late_finish != kInherit && !is_valid_date(values.late_finish)) {
        throw Error("write_manual_task_row: " + row_label(row, uid) +
                    ": late_finish is set but is not a valid date");
    }

    ManualWriteReport report;
    report.before = read_task_values(doc, row, table);
    std::uint32_t const late_start =
        values.late_start == kInherit ? values.start : values.late_start;
    std::uint32_t const late_finish =
        values.late_finish == kInherit ? values.finish : values.late_finish;

    /* FixedData: exactly the word set Project wrote.  +96/+100, the duration
     * family and everything else are left as stored. */
    std::vector<std::uint8_t> fixed = t.fixed_data();
    std::size_t const p = task_row_offset(row);
    put_u32(fixed, p + task_row::kStart2, values.start);
    put_u32(fixed, p + task_row::kFinish2, values.finish);
    put_u32(fixed, p + task_row::kStart3, late_start);
    put_u32(fixed, p + task_row::kFinish3, late_finish);
    put_u32(fixed, p + task_row::kStartAnchor, values.start);
    store_task_fixed_data(doc, fixed, table);

    /* Fixed2Data: +50 follows the effective Start and +54 the effective Finish;
     * +46 and +58 are deliberately left at their stored values. */
    std::vector<std::uint8_t> fixed2 = read_fixed2_data(doc, table);
    if (fixed2_row_count(fixed2.size()) != t.rows()) {
        throw Error("write_manual_task_row: Fixed2Data holds " +
                    std::to_string(fixed2_row_count(fixed2.size())) +
                    " rows but FixedData holds " + std::to_string(t.rows()));
    }
    std::size_t const r = fixed2_record_offset(row);
    put_u32(fixed2, r + fixed2_row::kStartCopy, values.start);
    put_u32(fixed2, r + fixed2_row::kFinish, values.finish);
    doc.replace_stream(table_stream(table, "Fixed2Data"), fixed2);

    /* Read it back and prove the split: the effective words moved, the
     * calculated/duration words did not. */
    report.after = read_task_values(doc, row, table);
    TaskValues const& before = report.before;
    TaskValues const& after = report.after;
    auto unchanged = [row, uid](bool condition, std::string const& text) {
        if (!condition) {
            throw Error("write_manual_task_row: " + row_label(row, uid) +
                        ": " + text);
        }
    };
    unchanged(after.project_start == values.start,
              "effective start (+104) did not take");
    unchanged(after.project_finish == values.finish,
              "effective finish (+108) did not take");
    unchanged(after.late_start == late_start, "late start (+112) did not take");
    unchanged(after.late_finish == late_finish,
              "late finish (+116) did not take");
    unchanged(after.start_anchor == values.start,
              "start anchor (+174) did not take");
    unchanged(after.fixed2_finish == values.finish,
              "Fixed2Data +54 did not take");
    unchanged(after.start == before.start, "calculated start (+96) moved");
    unchanged(after.finish == before.finish, "calculated finish (+100) moved");
    unchanged(after.duration == before.duration, "duration (+84) moved");
    unchanged(after.remaining_duration == before.remaining_duration,
              "remaining duration (+88) moved");
    unchanged(after.actual_duration == before.actual_duration,
              "actual duration (+80) moved");
    unchanged(after.fixed2_start == before.fixed2_start,
              "Fixed2Data +46 moved");
    unchanged(after.fixed2_duration == before.fixed2_duration,
              "Fixed2Data +58 moved");
    unchanged(after.constraint_type == before.constraint_type &&
                  after.constraint_date == before.constraint_date,
              "the constraint (+64/+66) moved");

    auto warn_if = [&report, row, uid](bool condition, std::string const& text) {
        if (condition) {
            report.warnings.push_back(row_label(row, uid) + ": " + text);
        }
    };
    warn_if(values.start > values.finish,
            "start " + format_date(values.start) + " is after finish " +
                format_date(values.finish));
    warn_if(late_start > late_finish,
            "late start " + format_date(late_start) + " is after late finish " +
                format_date(late_finish));
    warn_if(after.fixed2_duration != after.duration,
            "Fixed2Data +58 (" + std::to_string(after.fixed2_duration) +
                ") disagrees with FixedData +84 (" +
                std::to_string(after.duration) + ")");
    return report;
}

/* --- W29: the constraint write path -------------------------------------- */

TaskConstraint read_task_constraint(CfbDocument const& doc, std::size_t row,
                                    std::string_view table) {
    TaskTable const t = TaskTable::from_document(doc, table);
    if (row >= t.rows()) {
        throw Error("read_task_constraint: " + row_label(row, 0) +
                    " but the table has " + std::to_string(t.rows()) + " rows");
    }
    TaskConstraint c;
    c.type = t.u16(row, task_row::kConstraintType);
    c.date = t.u32(row, task_row::kConstraintDate);
    return c;
}

TaskConstraint write_task_constraint(CfbDocument& doc, std::size_t row,
                                     TaskConstraint const& values,
                                     std::string_view table) {
    TaskTable t = TaskTable::from_document(doc, table);
    if (row >= t.rows()) {
        throw Error("write_task_constraint: " + row_label(row, 0) +
                    " but the table has " + std::to_string(t.rows()) + " rows");
    }
    std::uint32_t const uid = t.uid(row);
    if (values.type != 0 && !is_valid_date(values.date)) {
        throw Error("write_task_constraint: " + row_label(row, uid) +
                    ": constraint type " + std::to_string(values.type) +
                    " needs a valid packed date, not " + format_date(values.date));
    }
    if (values.type == 0 && is_valid_date(values.date)) {
        throw Error("write_task_constraint: " + row_label(row, uid) +
                    ": a constraint DATE (" + format_date(values.date) +
                    ") without a constraint TYPE is meaningless -- ASAP stores "
                    "no date");
    }
    TaskValues const before = read_task_values(doc, row, table);
    t.set_u16(row, task_row::kConstraintType, values.type);
    t.set_u32(row, task_row::kConstraintDate, values.date);
    t.store_into(doc, table);

    TaskConstraint const after = read_task_constraint(doc, row, table);
    TaskValues const after_values = read_task_values(doc, row, table);
    if (after.type != values.type || after.date != values.date) {
        throw Error("write_task_constraint: " + row_label(row, uid) +
                    ": the constraint does not read back as written");
    }
    /* A constraint is not a date edit: nothing else in the row may move. */
    if (after_values.start != before.start ||
        after_values.finish != before.finish ||
        after_values.project_start != before.project_start ||
        after_values.project_finish != before.project_finish ||
        after_values.late_start != before.late_start ||
        after_values.late_finish != before.late_finish ||
        after_values.duration != before.duration ||
        after_values.remaining_duration != before.remaining_duration ||
        after_values.actual_duration != before.actual_duration ||
        after_values.total_slack != before.total_slack ||
        after_values.start_slack != before.start_slack ||
        after_values.start_anchor != before.start_anchor ||
        after_values.fixed2_start != before.fixed2_start ||
        after_values.fixed2_finish != before.fixed2_finish ||
        after_values.fixed2_duration != before.fixed2_duration) {
        throw Error("write_task_constraint: " + row_label(row, uid) +
                    ": writing the constraint moved another field of the row");
    }
    return after;
}

TaskConstraint pin_task_start(CfbDocument& doc, std::size_t row,
                              std::uint32_t date, std::string_view table) {
    TaskConstraint constraint;
    constraint.type = kConstraintSnet;
    constraint.date = date;
    return write_task_constraint(doc, row, constraint, table);
}

std::size_t recalculate_write_path(CfbDocument& doc,
                                   std::vector<PlannedRow> const& rows,
                                   std::string_view table,
                                   std::vector<std::string>* warnings) {
    TaskTable const table_view = TaskTable::from_document(doc, table);
    for (PlannedRow const& planned : rows) {
        std::size_t const row = table_view.row_for_uid(planned.uid);
        RowWriteReport const report = write_task_row(doc, row, planned.values,
                                                     table);
        if (warnings != nullptr) {
            warnings->insert(warnings->end(), report.warnings.begin(),
                             report.warnings.end());
        }
    }
    std::string const problem = check_table(doc, table);
    if (!problem.empty()) {
        throw Error("recalculate_write_path: the table is not consistent after "
                    "writing: " + problem);
    }
    return rows.size();
}

} /* namespace mppwrite */
