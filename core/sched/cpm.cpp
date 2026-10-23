// core/sched/cpm.cpp -- forward pass, backward pass, slack and criticality.
//
// The measured contract (docs/analysis/oracle/w3-o1.json, w3-o3.json,
// w3-o3b.json, w3-o4.json, w3-o4b.json, milestone-probe.json, lagff-probe.json)
// is repeated at each decision below.  The rules the measurements forced and that
// the spec did not have are marked "MEASURED RULE".
#include "cpm.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <queue>
#include <unordered_map>
#include <utility>
#include <vector>

namespace core::sched {
namespace {

constexpr int kMaxPasses = 50;  // §3.8: a hard cap is mandatory

bool is_set(date32 d) noexcept { return !is_na(d) && !is_overflow(d); }

// Signed working-time difference (§3.5 slack is in working ticks).
ticks work_delta(const Calendar& calendar, date32 from, date32 to) {
    if (!is_set(from) || !is_set(to)) {
        return 0;
    }
    if (to >= from) {
        return calendar.work_time_between(from, to);
    }
    return -calendar.work_time_between(to, from);
}

// Signed WALL-CLOCK difference in the same ticks (tenths of a minute).  Used for
// an elapsed-duration task, whose duration and slack count all time, not just
// working time (MEASURED, wave 11: w9-elapsedlag.mpp T8 stores 106200 ticks of
// TotalSlack = the wall-clock gap Wed 10-21 08:00 -> Wed 10-28 17:00).
ticks elapsed_delta(date32 from, date32 to) {
    if (!is_set(from) || !is_set(to)) {
        return 0;
    }
    return (to_seconds(to) - to_seconds(from)) / kSecondsPerTenth;
}

// §4.2 forward lag: working lag on the successor's calendar; negative = lead.
date32 add_lag(const Calendar& calendar, date32 t, ticks lag) {
    if (!is_set(t)) {
        return kDateNa;
    }
    if (lag >= 0) {
        return calendar.add_work(t, lag);
    }
    return calendar.sub_work(t, -lag);
}

// §4.2 backward lag: inverse of add_lag.
date32 sub_lag(const Calendar& calendar, date32 t, ticks lag) {
    if (!is_set(t)) {
        return kDateNa;
    }
    if (lag >= 0) {
        return calendar.sub_work(t, lag);
    }
    return calendar.add_work(t, -lag);
}

// MEASURED RULE (docs/analysis/oracle/lagff-probe.json, W3-O3b "lagff"): the FF
// forward bound is the predecessor's finish INSTANT with the signed lag applied,
// and a lead lands on the previous interval END (a finish), not on an interval
// start.  From a 5 d predecessor finishing Fri 2026-10-23 17:00:
//   FF-1d -> Thu 22 17:00, FF-3d -> Tue 20 17:00, FF-6d -> Thu 10/15 17:00,
// while FF+0 -> Fri 17:00, FF+1d -> Monday 17:00 (= AddWork from 17:00), FF+2h
// -> Monday 10:00.  A plain SubWork would give Fri 08:00 for FF-1d, which is
// neither measured row; the subtraction is therefore followed by align_back().
// A non-negative lag is plain AddWork from the RAW finish: no AlignForward in
// front of it (FF+0 must reproduce pred.EF even when that is an interval start).
date32 add_finish_lag(const Calendar& calendar, date32 t, ticks lag) {
    if (!is_set(t)) {
        return kDateNa;
    }
    if (lag >= 0) {
        return calendar.add_work(t, lag);
    }
    return calendar.align_back(calendar.sub_work(t, -lag));
}

// MEASURED RULE (wave 9b; corpus/probe/w9-elapsedlag.mpp and
// docs/analysis/oracle/w9-elapsedlag.json): an ELAPSED lag is plain wall-clock
// arithmetic -- the calendar is not walked at all.  The stored lag value is
// already in ticks of elapsed time ("2 edays" = 28800 ticks = 48 h,
// "3 edays" = 43200 = 72 h), so only the addition differs.  Project's own save
// stores the link's LagFormat word (row +18) as 8 for both "2 edays"/"3 edays"
// links and 7 for the matching "2 days" links, in two single-variable pairs
// (same predecessor, same 2-day spelling).
//
// The measured placements: T1 finishes Wed 2026-10-21 17:00; FS+2 edays ->
// Fri 10-23 17:00 -> successor Start Mon 10-26 08:00; FS+3 edays ->
// Sat 10-24 17:00 -> Start Mon 10-26 08:00.  A 3 WORKING-day lag from the same
// finish would start Tue 10-27 08:00, so the scheduled dates do separate the
// two readings (and the stored LagFormat word settles it independently).
date32 add_elapsed(date32 t, ticks amount) {
    if (!is_set(t)) {
        return kDateNa;
    }
    return pack(day_of(t) + static_cast<std::int32_t>(amount / kTenthsPerDay),
                time_of(t) + static_cast<std::int32_t>(amount % kTenthsPerDay));
}

// One link's forward lag, choosing the working-time or the elapsed rule from the
// link itself.
date32 add_link_lag(const Calendar& calendar, date32 t, const CpmLink& link) {
    return link.elapsed_lag ? add_elapsed(t, link.lag) : add_lag(calendar, t, link.lag);
}

date32 add_finish_link_lag(const Calendar& calendar, date32 t, const CpmLink& link) {
    return link.elapsed_lag ? add_elapsed(t, link.lag) : add_finish_lag(calendar, t, link.lag);
}

// The backward inverse: the same rule with the sign flipped, so an elapsed lag
// subtracts wall-clock time too.
date32 sub_link_lag(const Calendar& calendar, date32 t, const CpmLink& link) {
    return link.elapsed_lag ? add_elapsed(t, -link.lag) : sub_lag(calendar, t, link.lag);
}

// MEASURED RULE (docs/analysis/oracle/w3-o3.json, W3-O3): a displayed Start of a
// task that CONSUMES working time is always a working instant.  When an FS/SS
// lower bound lands on an interval end, MSP shows the next working instant:
// FS+1d gives the bound Tue 20 17:00 but Start/EarlyStart Wed 21 08:00, and
// SS+1d gives Mon 19 17:00 but Start Tue 20 08:00.  A bound that already falls
// inside working time is left alone (FS+2h starts Tue 20 10:00).
//
// MEASURED RULE (docs/analysis/oracle/milestone-probe.json): a 0-duration task
// does NOT snap.  An FS+0 bound that lands on an interval end starts AND finishes
// at 17:00 (IDs 2 and 7), and FS+50d keeps its 17:00 instant (ID 3); the very
// same FS+0 bound on a 1 d successor is displayed at 08:00 (w3-o3).  So the
// caller passes snap = (duration > 0).
//
// Finish bounds are NOT snapped: MSP displays a finish at an interval end
// (FS+1d finishes Wed 21 17:00) and at a mid-day instant (FS+2h finishes
// Wed 21 10:00), so only the start side goes through this helper.
date32 align_start_bound(const Calendar& calendar, date32 t, bool snap) {
    if (!is_set(t) || !snap) {
        return t;
    }
    return calendar.is_working_instant(t) ? t : calendar.next_start(t);
}

bool constraint_needs_date(ConstraintType type) noexcept {
    switch (type) {
        case ConstraintType::must_start_on:
        case ConstraintType::must_finish_on:
        case ConstraintType::start_no_earlier_than:
        case ConstraintType::start_no_later_than:
        case ConstraintType::finish_no_earlier_than:
        case ConstraintType::finish_no_later_than:
            return true;
        case ConstraintType::as_soon_as_possible:
        case ConstraintType::as_late_as_possible:
            return false;
    }
    return false;
}

struct Node {
    std::int32_t id = 0;
    ticks duration = 0;
    bool elapsed_duration = false;
    const Calendar* calendar = nullptr;
    ConstraintType constraint = ConstraintType::as_soon_as_possible;
    date32 constraint_date = kDateNa;
    date32 deadline = kDateNa;
    date32 actual_start = kDateNa;   // a started task's fact (see CpmTask)
    date32 actual_finish = kDateNa;  // a completed task's fact (see CpmTask)
    date32 manual_start = kDateNa;   // a manual row's stored Start (see CpmTask)
    date32 manual_finish = kDateNa;  // a manual row's planned Finish (see CpmTask)
    date32 link_anchor_start = kDateNa;   // what a successor reads (see CpmTask)
    date32 link_anchor_finish = kDateNa;  // what a successor reads (see CpmTask)

    std::vector<std::size_t> incoming;   // indices into links
    std::vector<std::size_t> outgoing;   // indices into links

    // Results.
    date32 es = kDateNa;
    date32 ef = kDateNa;
    date32 ls = kDateNa;
    date32 lf = kDateNa;
    date32 link_es = kDateNa;  // link-driven early start (constraint not applied)
    date32 link_ef = kDateNa;  // link-driven early finish
    ticks start_slack = 0;
    ticks finish_slack = 0;
    ticks free_slack = 0;
    ticks total_slack = 0;
    bool critical = false;
};

// Task-duration arithmetic.  A working duration walks the task's calendar; an
// elapsed duration is plain wall-clock arithmetic (the same add_elapsed() the
// measured elapsed lag uses).  MEASURED for the elapsed case on w9-elapsedlag.mpp
// T8 ("2 edays" = 28800 ticks): Mon 10-19 08:00 -> Wed 10-21 08:00, and its
// late dates/slack are wall-clock too.
date32 add_duration(const Calendar& calendar, date32 t, const Node& node) {
    if (node.elapsed_duration) {
        return add_elapsed(t, node.duration);
    }
    return calendar.add_work(t, node.duration);
}

date32 sub_duration(const Calendar& calendar, date32 t, const Node& node) {
    if (node.elapsed_duration) {
        return add_elapsed(t, -node.duration);
    }
    return calendar.sub_work(t, node.duration);
}

ticks duration_delta(const Calendar& calendar, date32 from, date32 to,
                     const Node& node) {
    return node.elapsed_duration ? elapsed_delta(from, to)
                                 : work_delta(calendar, from, to);
}

CpmResult make_error(CpmError error, std::string message) {
    CpmResult result;
    result.error = error;
    result.message = std::move(message);
    return result;
}

}  // namespace

const char* to_string(CpmError error) noexcept {
    switch (error) {
        case CpmError::none:
            return "none";
        case CpmError::no_tasks:
            return "no tasks";
        case CpmError::missing_project_start:
            return "missing project start";
        case CpmError::missing_project_finish:
            return "missing project finish";
        case CpmError::duplicate_task_id:
            return "duplicate task id";
        case CpmError::unknown_task:
            return "unknown task in a link";
        case CpmError::self_link:
            return "self link";
        case CpmError::negative_duration:
            return "negative duration";
        case CpmError::constraint_date_missing:
            return "constraint date missing";
        case CpmError::overflow:
            return "calendar overflow";
        case CpmError::cycle:
            return "cycle";
        case CpmError::did_not_converge:
            return "did not converge";
        case CpmError::invalid_option:
            return "invalid option";
    }
    return "unknown";
}

// MEASURED RULE (w3-o4b.json "asap_constraintdate_probe"): assigning
// ConstraintDate to an ASAP task silently changes its type to SNET(4); the date
// then behaves as a floor (w3-o1.json T2/T3/T4 are all stored as type 4).
// The same migration is applied to ALAP: the w3-o4.json ALAP run came back with
// ConstraintType 4 after a date was assigned, which is the same signature.  ALAP
// itself takes no date in MS Project, so this only affects an input that pairs
// them.  [I] until a clean ALAP re-test.
ConstraintType normalise_constraint(ConstraintType type, date32 constraint_date) noexcept {
    if ((type == ConstraintType::as_soon_as_possible || type == ConstraintType::as_late_as_possible) &&
        is_set(constraint_date)) {
        return ConstraintType::start_no_earlier_than;
    }
    return type;
}

CpmResult schedule_tasks(const std::vector<CpmTask>& tasks, const std::vector<CpmLink>& links,
                         const Calendar& project_calendar, const CpmOptions& options) {
    CpmResult result;
    result.project_start = options.project_start;

    if (tasks.empty()) {
        return make_error(CpmError::no_tasks, "schedule_tasks: the task list is empty");
    }
    if (!is_set(options.project_start)) {
        return make_error(CpmError::missing_project_start,
                          "schedule_tasks: options.project_start is NA");
    }
    if (!options.schedule_from_start && !is_set(options.project_finish)) {
        return make_error(CpmError::missing_project_finish,
                          "schedule_tasks: schedule_from_start is false but project_finish is NA");
    }
    if (options.critical_slack_limit_days < 0) {
        return make_error(CpmError::invalid_option,
                          "schedule_tasks: CriticalSlackLimit is negative");
    }

    // --- validate and build the nodes ---------------------------------------
    std::vector<Node> nodes;
    nodes.reserve(tasks.size());
    std::unordered_map<std::int32_t, std::size_t> index_of;
    index_of.reserve(tasks.size() * 2);
    for (const CpmTask& task : tasks) {
        if (task.duration < 0) {
            return make_error(CpmError::negative_duration,
                              "schedule_tasks: task " + std::to_string(task.id) +
                                  " has a negative duration");
        }
        const auto inserted = index_of.emplace(task.id, nodes.size());
        if (!inserted.second) {
            return make_error(CpmError::duplicate_task_id,
                              "schedule_tasks: duplicate task id " + std::to_string(task.id));
        }
        Node node;
        node.id = task.id;
        node.duration = task.duration;
        node.elapsed_duration = task.elapsed_duration;
        node.calendar = task.calendar != nullptr ? task.calendar : &project_calendar;
        node.constraint = normalise_constraint(task.constraint, task.constraint_date);
        node.constraint_date = task.constraint_date;
        node.deadline = task.deadline;
        node.actual_start = task.actual_start;
        node.actual_finish = task.actual_finish;
        node.manual_start = task.manual_start;
        node.manual_finish = task.manual_finish;
        node.link_anchor_start = task.link_anchor_start;
        node.link_anchor_finish = task.link_anchor_finish;
        if (constraint_needs_date(node.constraint) && !is_set(node.constraint_date)) {
            return make_error(CpmError::constraint_date_missing,
                              "schedule_tasks: task " + std::to_string(task.id) +
                                  " uses constraint " + std::to_string(static_cast<int>(node.constraint)) +
                                  " without a constraint date");
        }
        nodes.push_back(std::move(node));
    }

    for (const CpmLink& link : links) {
        if (link.predecessor == link.successor) {
            return make_error(CpmError::self_link,
                              "schedule_tasks: task " + std::to_string(link.predecessor) +
                                  " is linked to itself");
        }
        const auto pred = index_of.find(link.predecessor);
        if (pred == index_of.end()) {
            return make_error(CpmError::unknown_task,
                              "schedule_tasks: link names unknown predecessor " +
                                  std::to_string(link.predecessor));
        }
        const auto succ = index_of.find(link.successor);
        if (succ == index_of.end()) {
            return make_error(CpmError::unknown_task,
                              "schedule_tasks: link names unknown successor " +
                                  std::to_string(link.successor));
        }
    }
    std::vector<std::size_t> pred_index(links.size());
    std::vector<std::size_t> succ_index(links.size());
    for (std::size_t i = 0; i < links.size(); ++i) {
        pred_index[i] = index_of.at(links[i].predecessor);
        succ_index[i] = index_of.at(links[i].successor);
        nodes[pred_index[i]].outgoing.push_back(i);
        nodes[succ_index[i]].incoming.push_back(i);
    }

    // --- deterministic topological order (§3.1/§3.2), by task id ------------
    std::vector<std::size_t> indegree(nodes.size(), 0);
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        indegree[i] = nodes[i].incoming.size();
    }
    auto greater_id = [&nodes](std::size_t a, std::size_t b) {
        return nodes[a].id > nodes[b].id;
    };
    std::priority_queue<std::size_t, std::vector<std::size_t>, decltype(greater_id)> ready(
        greater_id);
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (indegree[i] == 0) {
            ready.push(i);
        }
    }
    std::vector<std::size_t> order;
    order.reserve(nodes.size());
    while (!ready.empty()) {
        const std::size_t index = ready.top();
        ready.pop();
        order.push_back(index);
        for (const std::size_t link_index : nodes[index].outgoing) {
            const std::size_t next = succ_index[link_index];
            if (--indegree[next] == 0) {
                ready.push(next);
            }
        }
    }
    if (order.size() != nodes.size()) {
        CpmResult cycle = make_error(CpmError::cycle,
                                     "schedule_tasks: the links form a cycle; MSP reports a "
                                     "circular relationship instead of iterating");
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            if (indegree[i] != 0) {
                cycle.cycle_tasks.push_back(nodes[i].id);
            }
        }
        std::sort(cycle.cycle_tasks.begin(), cycle.cycle_tasks.end());
        return cycle;
    }
    for (const std::size_t index : order) {
        result.order.push_back(nodes[index].id);
    }

    // --- the passes ---------------------------------------------------------
    const auto fail_overflow = [](std::int32_t task_id) {
        return make_error(CpmError::overflow, "schedule_tasks: task " + std::to_string(task_id) +
                                                  " leaves the calendar domain");
    };

    date32 anchor_finish = options.project_finish;
    struct Snapshot {
        date32 es = kDateNa;
        date32 ef = kDateNa;
        date32 ls = kDateNa;
        date32 lf = kDateNa;
    };
    bool converged = false;
    for (int pass = 0; pass < kMaxPasses; ++pass) {
        std::vector<Snapshot> previous(nodes.size());
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            previous[i] = Snapshot{nodes[i].es, nodes[i].ef, nodes[i].ls, nodes[i].lf};
        }
        // ---- forward pass: early dates (§3.3) ------------------------------
        for (const std::size_t index : order) {
            Node& node = nodes[index];
            const Calendar& cal = *node.calendar;
            const date32 floor = cal.next_start(options.project_start);
            if (!is_set(floor)) {
                return fail_overflow(node.id);
            }
            // MEASURED RULE (w3-o3): a link bound is either a START bound (FS,
            // SS) or a FINISH bound (FF, SF), and the two sides align differently.
            // Keeping them apart is what fixes FF and SF:
            //   * FS/SS bounds are aligned like a start (a bound on an interval
            //     end snaps to the next working instant);
            //   * FF/SF bounds are PLACED on the finish instant and never snapped
            //     (FF+0 finishes Mon 19 17:00; SF+0 finishes Mon 19 08:00, a
            //     mid-day instant).
            // MEASURED RULE (milestone-probe): a task that consumes no working
            // time keeps a start bound's instant exactly, even at 17:00.
            const bool consumes_time = node.duration > 0;
            date32 start_bound = kDateNa;
            date32 finish_bound = kDateNa;
            bool has_ff_link = false;
            for (const std::size_t link_index : node.incoming) {
                const CpmLink& link = links[link_index];
                const Node& pred = nodes[pred_index[link_index]];
                const Calendar& pred_cal = *pred.calendar;
                /* The dates the successor's bound reads (see CpmTask::
                 * link_anchor_*): a manual predecessor contributes its KEPT
                 * dates even when the pass computes its own early dates. */
                const date32 pred_es =
                    is_set(pred.link_anchor_start) ? pred.link_anchor_start : pred.es;
                const date32 pred_ef =
                    is_set(pred.link_anchor_finish) ? pred.link_anchor_finish : pred.ef;
                date32 candidate = kDateNa;
                switch (link.type) {
                    case LinkType::finish_to_start: {
                        // §4.2: the anchor is the next working instant after the
                        // predecessor's finish, which is why FS+0 starts the
                        // successor the next morning (measured: w3-o4 baseline,
                        // w3-o3).  The result is then snapped like every start
                        // bound: FS+1d lands on Tue 17:00 but starts Wed 08:00.
                        //
                        // MEASURED (milestone-probe): when the successor is a
                        // milestone the anchor is the predecessor's finish instant
                        // ITSELF and no snap happens: FS+0 after a 1 d task gives
                        // start == finish 17:00 (IDs 2 and 7), and FS+50d keeps the
                        // 17:00 instant (ID 3).  Using next_start here would move
                        // the milestone to the next morning.
                        // MEASURED (wave 9b, w9-elapsedlag.mpp): an ELAPSED lag
                        // anchors on the predecessor's finish instant, not on its
                        // aligned next start -- 2 edays from Fri 17:00 is Sun
                        // 17:00 -> Start Mon 10-26 08:00, while aligning first
                        // (Mon 08:00 + 48 h = Wed 08:00) gives Project's
                        // WORKING-day answer for the same spellings.  The
                        // working-day lag keeps the next_start anchor (measured
                        // w3-o3/w3-o4).
                        const date32 fs_anchor =
                            link.elapsed_lag ? pred_ef : pred_cal.next_start(pred_ef);
                        candidate =
                            consumes_time
                                ? align_start_bound(cal, add_link_lag(cal, fs_anchor, link), true)
                                : add_link_lag(cal, pred_ef, link);
                        break;
                    }
                    case LinkType::start_to_start:
                        // MEASURED (w3-o3): SS+0 starts at the predecessor's
                        // start; SS+1d lands on Mon 17:00 and starts Tue 08:00
                        // after the start-bound alignment.  A milestone keeps the
                        // bound instant (milestone-probe ID 5: SS+0 -> Mon 08:00;
                        // that bound is already a working instant, so the shape of
                        // the rule, not a second row, is what is measured here).
                        candidate = align_start_bound(cal, add_link_lag(cal, pred_es, link),
                                                      consumes_time);
                        break;
                    case LinkType::finish_to_finish:
                        // MEASURED (w3-o3 FF+0): the anchor is the predecessor's
                        // FINISH INSTANT (Mon 17:00), not the aligned next start
                        // (Tue 08:00).  Using next_start here is what made the
                        // engine finish T2 on Tue 20 08:00 for FF+0.
                        //
                        // MEASURED (lagff-probe): a LEAD is applied, and lands on
                        // the previous interval end: FF-1d from Fri 23 17:00
                        // finishes Thu 22 17:00, FF-3d Tue 20 17:00, FF-6d Thu
                        // 10/15 17:00 (see add_finish_lag above).
                        has_ff_link = true;
                        candidate = add_finish_link_lag(cal, pred_ef, link);
                        break;
                    case LinkType::start_to_finish:
                        // MEASURED (w3-o3 SF): the anchor is the predecessor's
                        // start instant; the result is a placed finish.  A lead
                        // is plain calendar subtraction here (SF-1d finishes Fri
                        // 16 08:00, an interval START, not the previous finish),
                        // so this side does NOT go through add_finish_lag.  SF+1d
                        // is not anomalous: AddWork(Mon 08:00, 1 d) = Mon 17:00,
                        // so the placed finish is Mon 08:00-17:00.
                        candidate = add_link_lag(cal, pred_es, link);
                        break;
                }
                if (!is_set(candidate)) {
                    return fail_overflow(node.id);
                }
                if (link.type == LinkType::finish_to_finish ||
                    link.type == LinkType::start_to_finish) {
                    finish_bound = is_set(finish_bound) ? std::max(finish_bound, candidate)
                                                        : candidate;
                } else {
                    start_bound = is_set(start_bound) ? std::max(start_bound, candidate) : candidate;
                }
            }
            // MEASURED RULE (lagff-probe FF-6d): an FF finish bound below the
            // project start does not place the successor outside the project.
            // FF-6d computes Thu 2026-10-15 17:00, before the project start, and
            // MSP schedules the successor Mon 19 08:00-17:00: the placement is
            // floored at the project start and the finish then follows the start.
            // Written on the finish bound, the floor is the earliest finish of a
            // task that starts at the project start.
            // This also reproduces the old w3-o3 "FF-1d behaves as if the lag
            // were dropped" row: the applied bound is Fri 16 17:00, the floor
            // lifts it to Mon 17:00, so that row coincides with zero lag.
            // The floor is FF-only: a START-side link is not floored (SS-1d
            // starts Fri 16 08:00) and neither is SF (SF+0 starts Fri 16 08:00).
            //
            // ASSUMPTION, not measured: the fixture's successors are all 1 d, so
            // a 0-duration (milestone) FF successor under this floor -- where
            // add_work(floor, 0) is the floor instant itself -- has no oracle row.
            // Do not claim it; it is listed as open in core/sched/README.md.
            if (has_ff_link) {
                const date32 earliest_finish = add_duration(cal, floor, node);
                if (!is_set(earliest_finish)) {
                    return fail_overflow(node.id);
                }
                finish_bound = std::max(finish_bound, earliest_finish);
            }

            // MEASURED RULE (w3-o3 SS-1d): a start-side link PLACES the start and
            // is not floored at the project start -- Project starts T2 on Fri 16
            // 08:00, before the Mon 19 project start.  The floor still governs the
            // ASAP/no-predecessor case; a finish-side bound back-computes the
            // start with SubWork (also without the floor: SF+0 starts Fri 16).
            date32 es = floor;
            if (is_set(start_bound)) {
                es = start_bound;
            } else if (is_set(finish_bound)) {
                es = sub_duration(cal, finish_bound, node);
                if (!is_set(es)) {
                    return fail_overflow(node.id);
                }
            }
            node.link_es = es;

            date32 ef = kDateNa;
            // MEASURED (docs/engine-parity.md "First recording"): a manual row's
            // own constraint is stored but does not move it.  constraint-snet
            // stores SNET 2026-02-02 08:00 on uid 2 and the task keeps
            // 2026-01-04 13:00 / 2026-01-07 12:00 while the CALCULATED pair
            // (+96/+100) jumps to 02-02/02-04; the same for MSO.  So a manual pin
            // suppresses the constraint here and in the backward pass; the
            // caller runs a second, pin-free pass to obtain the calculated pair.
            const bool manual = is_set(node.manual_start);
            if (!manual) {
                switch (node.constraint) {
                    case ConstraintType::must_start_on:
                        // MEASURED (§3.6, w3-o4 MSO rows): pin the start exactly, even
                        // when the constraint date is mid-day and even when the links
                        // want a later date.  A pin on a non-working instant snaps
                        // forward like any start bound (no fixture yet; [I]).
                        es = align_start_bound(cal, node.constraint_date, true);
                        break;
                    case ConstraintType::start_no_earlier_than:
                        // MEASURED: a floor; it never pulls a task earlier.
                        //
                        // The milestone no-snap rule above is a LINK-bound rule here:
                        // the one measured milestone constraint (ID 4) stores a date
                        // that MSP had already normalised (an assigned start of Wed
                        // 17:00 was rewritten to Thu 08:00 and stored as SNET 08:00),
                        // so aligning a raw 17:00 SNET date and keeping it are
                        // indistinguishable from the oracle.  Keep the alignment; the
                        // raw case is listed as open in core/sched/README.md.
                        es = align_start_bound(cal, std::max(es, node.constraint_date), true);
                        break;
                    case ConstraintType::start_no_later_than:
                        // MEASURED (w3-o4/10d/SNLT): the ceiling is applied in the
                        // forward pass -- the task is placed at the constraint when the
                        // links want a later date, and the violation shows as negative
                        // slack.
                        if (node.constraint_date < es) {
                            es = align_start_bound(cal, node.constraint_date, true);
                        }
                        break;
                    case ConstraintType::must_finish_on:
                        ef = node.constraint_date;
                        es = sub_duration(cal, ef, node);
                        break;
                    case ConstraintType::finish_no_earlier_than: {
                        const date32 natural = add_duration(cal, es, node);
                        if (!is_set(natural)) {
                            return fail_overflow(node.id);
                        }
                        if (natural < node.constraint_date) {
                            ef = node.constraint_date;
                            es = sub_duration(cal, ef, node);
                        }
                        break;
                    }
                    case ConstraintType::finish_no_later_than: {
                        const date32 natural = add_duration(cal, es, node);
                        if (!is_set(natural)) {
                            return fail_overflow(node.id);
                        }
                        // MEASURED (w3-o4/10d/FNLT): a hard ceiling applied in the
                        // forward pass, exactly like SNLT on the finish side.
                        if (node.constraint_date < natural) {
                            ef = node.constraint_date;
                            es = sub_duration(cal, ef, node);
                        }
                        break;
                    }
                    case ConstraintType::as_soon_as_possible:
                    case ConstraintType::as_late_as_possible:
                        break;
                }
            }
            if (!is_set(ef)) {
                // MEASURED (w3-o3 SF+0/SF+2h): a finish-side bound is a PLACED
                // finish, not "start + duration".  SF+0 finishes Mon 19 08:00
                // even though T2 starts Fri 16 08:00, so the bound must not be
                // max'ed with the natural early finish (which is Mon 19 17:00
                // from the project-start floor and used to win).
                ef = is_set(finish_bound) ? finish_bound : add_duration(cal, es, node);
            } else if (is_set(finish_bound) && finish_bound > ef) {
                ef = finish_bound;
                es = sub_duration(cal, ef, node);
                if (!is_set(es)) {
                    return fail_overflow(node.id);
                }
            }
            if (is_set(finish_bound)) {
                // A finish bound is a floor: when a start bound or a constraint
                // pushed the start later, the finish follows the start.
                const date32 from_start = add_duration(cal, es, node);
                if (!is_set(from_start)) {
                    return fail_overflow(node.id);
                }
                if (from_start > ef) {
                    ef = from_start;
                }
            }
            if (!is_set(ef) || !is_set(es)) {
                return fail_overflow(node.id);
            }
            // MEASURED RULE (Project's save corpus/probe/project_a-recalc.mpp, uids
            // 10849/10851; see CpmTask::actual_start): a started task's early
            // start is its ACTUAL start, even when a link or a constraint wants a
            // different date.  The finish is then the actual start + duration (the
            // same forward shape as MSO), while the late finish stays
            // successor-derived -- the backward pin below touches only the start.
            if (is_set(node.actual_start)) {
                es = node.actual_start;
                ef = add_duration(cal, es, node);
                if (!is_set(ef)) {
                    return fail_overflow(node.id);
                }
            }
            // MEASURED (docs/engine-parity.md "First recording"; every recorded
            // oracle): a MANUAL row's early dates are its stored Start and the
            // Finish the caller derived from it.  Neither the project-start floor
            // (the stored Start is 2026-01-04 13:00, a Sunday, BEFORE the aligned
            // Monday 08:00) nor a link nor its own constraint moves them, so this
            // override comes last and uses the manual pair verbatim.  The CALLER
            // computes manual_finish, because it is not plain AddWork in the one
            // measured boundary case (plan.h "MANUAL SCHEDULING").
            if (manual) {
                es = node.manual_start;
                ef = is_set(node.manual_finish) ? node.manual_finish
                                                : add_duration(cal, es, node);
                if (!is_set(ef)) {
                    return fail_overflow(node.id);
                }
            }
            node.es = es;
            node.ef = ef;
            node.link_ef = std::max(node.link_es, add_duration(cal, node.link_es, node));
            if (!is_set(node.link_ef)) {
                return fail_overflow(node.id);
            }
        }

        // Project finish = the latest early finish (§3.4).
        date32 project_finish = options.project_start;
        for (const Node& node : nodes) {
            if (node.ef > project_finish) {
                project_finish = node.ef;
            }
        }
        anchor_finish = options.schedule_from_start ? project_finish : options.project_finish;
        result.project_finish = anchor_finish;

        // ---- backward pass: late dates (§3.4) ------------------------------
        for (auto it = order.rbegin(); it != order.rend(); ++it) {
            Node& node = nodes[*it];
            const Calendar& cal = *node.calendar;
            date32 lf = kDateNa;
            date32 ls_bound = kDateNa;
            // True when an outgoing FS/FF link set a bound on this task's
            // FINISH.  An SS/SF link bounds the successor side of this task's
            // start and does not lift the late finish past the project finish.
            bool has_finish_link = false;
            const auto take_min_finish = [&lf](date32 bound) {
                if (!is_set(bound)) {
                    return;
                }
                lf = is_set(lf) ? std::min(lf, bound) : bound;
            };
            const auto take_min_start = [&ls_bound](date32 bound) {
                if (!is_set(bound)) {
                    return;
                }
                ls_bound = is_set(ls_bound) ? std::min(ls_bound, bound) : bound;
            };
            for (const std::size_t link_index : node.outgoing) {
                const CpmLink& link = links[link_index];
                const Node& succ = nodes[succ_index[link_index]];
                // MEASURED (corpus/probe/project_a-recalc.mpp uid 10851): a link whose
                // SUCCESSOR is 100 % complete does not bound this task's late
                // finish -- the successor has no remaining work, so it cannot be
                // delayed.  Encoded here as "no bound at all"; see
                // CpmTask::actual_finish for the row and for the one [I]
                // extension (the measured row is FS).
                if (is_set(succ.actual_finish)) {
                    continue;
                }
                switch (link.type) {
                    case LinkType::finish_to_start:
                        // MEASURED (w3-o3 FS+1d, w3-o4 MFO row): the predecessor's
                        // late finish is a FINISH bound.  Subtract the lag from the
                        // successor's late start first, then align BACK to a finish
                        // instant.  Aligning the bound like a start (the old
                        // align_back-then-sub_lag order) gave Tue 20 08:00 for
                        // FS+1d where Project reports Mon 19 17:00.
                        has_finish_link = true;
                        take_min_finish(cal.align_back(sub_link_lag(cal, succ.ls, link)));
                        break;
                    case LinkType::start_to_start:
                        take_min_start(sub_link_lag(cal, succ.ls, link));
                        break;
                    case LinkType::finish_to_finish:
                        // MEASURED (w3-o3 FF+1d): the same finish-side rule --
                        // subtract the lag, then align back (T2 LF Tue 17:00 gives
                        // T1 LF Mon 17:00, not Tue 08:00).
                        //
                        // MEASURED (w3-o3 FF-1d, lagff-probe): a LEAD does NOT
                        // push the predecessor's late finish past the successor's
                        // late finish.  The plain inverse of FF-1d would be
                        // T2 LF + 1 d = Tue 17:00, giving T1 480 of slack; MSP
                        // reports T1 LF Mon 17:00, slack 0 (w3-o3 FF-1d) and
                        // T1 LF Fri 10/23 17:00 with five FF successors, slack 0
                        // (lagff-probe).  In both measured lead rows the bound is
                        // exactly succ.LF, so it is clamped there.  The clamp is
                        // inert for a non-negative lag (sub_lag already returns an
                        // earlier instant and align_back only moves it earlier).
                        has_finish_link = true;
                        take_min_finish(std::min(
                            succ.lf, cal.align_back(sub_link_lag(cal, succ.lf, link))));
                        break;
                    case LinkType::start_to_finish:
                        take_min_start(sub_link_lag(cal, succ.lf, link));
                        break;
                }
            }
            if (is_set(ls_bound)) {
                take_min_finish(add_duration(cal, ls_bound, node));
            }

            bool pinned = false;
            // MEASURED (docs/engine-parity.md "First recording"): a manual row's
            // stored constraint does not define its late dates either -- uid 3 of
            // the constraint-mso oracle keeps ConstraintType 2 and TotalSlack
            // 14400 (the unconstrained value).  Treat it as ASAP here; the
            // backward pass then bounds it like any other row.
            const bool manual = is_set(node.manual_start);
            switch (manual ? ConstraintType::as_soon_as_possible : node.constraint) {
                case ConstraintType::must_start_on:
                case ConstraintType::must_finish_on:
                    // §3.4: pinned tasks have late == early.
                    lf = node.ef;
                    pinned = true;
                    break;
                case ConstraintType::start_no_later_than: {
                    // MEASURED (w3-o4/1d/SNLT): the constraint defines the late
                    // dates -- LF = AddWork(date, duration) = Mon 26 17:00, even
                    // when that is beyond the project finish.  A successor bound,
                    // where present, still wins.
                    const date32 bound = add_duration(cal, node.constraint_date, node);
                    if (!is_set(bound)) {
                        return fail_overflow(node.id);
                    }
                    lf = is_set(lf) ? std::min(lf, bound) : bound;
                    break;
                }
                case ConstraintType::finish_no_later_than:
                    // MEASURED (w3-o4/1d/FNLT): a ceiling on the late finish.
                    if (!is_set(lf)) {
                        lf = anchor_finish;
                    }
                    lf = std::min(lf, node.constraint_date);
                    break;
                case ConstraintType::as_late_as_possible:
                    lf = is_set(lf) ? std::min(lf, anchor_finish) : anchor_finish;
                    break;
                case ConstraintType::as_soon_as_possible:
                case ConstraintType::start_no_earlier_than:
                case ConstraintType::finish_no_earlier_than:
                    if (!is_set(lf)) {
                        lf = anchor_finish;
                    } else if (!has_finish_link) {
                        // MEASURED (w3-o3 SS and SF rows): an SS/SF link caps the
                        // predecessor's late START, not its late finish.  The
                        // converted start bound must therefore stay inside the
                        // project finish -- without this SS-1d's converted bound
                        // (Tue 17:00) lifted T1's late finish past it and gave T1
                        // 480 of slack where Project reports 0.
                        lf = std::min(lf, anchor_finish);
                    }
                    break;
            }
            if (!manual && is_set(node.deadline)) {
                // MEASURED (w3-o4b deadlines): the deadline is an upper bound on
                // the late finish, aligned like a successor start -- deadline
                // Mon 19 17:00 clamps LF to Mon 19 17:00 and the slack is -480.
                // A manual row's deadline is a status field, not a bound: the row
                // keeps its dates whatever the deadline says ([I], no fixture).
                const date32 bound = cal.align_back(node.deadline);
                if (!is_set(bound)) {
                    return fail_overflow(node.id);
                }
                lf = std::min(lf, bound);
            }
            if (!is_set(lf)) {
                lf = anchor_finish;
            }
            date32 ls = kDateNa;
            if (pinned && lf == node.ef) {
                ls = node.es;
            } else {
                ls = sub_duration(cal, lf, node);
            }
            if (!is_set(ls)) {
                return fail_overflow(node.id);
            }
            node.lf = lf;
            node.ls = ls;
            // MEASURED (docs/engine-parity.md "First recording"): a manual row's
            // start is a user fact, so its LATE START is its own start and the
            // whole row moves with the slack.  Every manual row of every oracle
            // stores the same number at +74/+186/+190 (0/0/0, 9600, 14400,
            // 24000, 33600 ...), which is exactly this: StartSlack is reported
            // equal to TotalSlack, and TotalSlack is LF - EF.  Doing it here
            // (rather than LS = LF - duration, which can land before a manual
            // start that sits in non-working time) is what reproduces the rows.
            if (manual) {
                node.ls = node.es;
            }
            // MEASURED RULE (project_a-recalc.mpp uids 10849/10851): a started task's
            // LATE START is its actual start -- StartSlack is 0 even though the
            // successor-derived late finish is later.  Only the start is pinned;
            // the late finish above is NOT forced back to the early finish the
            // way an MSO constraint forces it.
            if (is_set(node.actual_start)) {
                node.ls = node.actual_start;
            }
        }

        // ---- convergence (§3.8) -------------------------------------------
        // The two passes above are one-directional (the forward pass reads only
        // predecessor early dates; the backward pass reads only successor late
        // dates), so a second pass should change nothing.  Comparing the dates
        // proves that rather than assuming it, and the cap turns a non-converging
        // input into an error instead of a hang.
        bool changed = false;
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            if (nodes[i].es != previous[i].es || nodes[i].ef != previous[i].ef ||
                nodes[i].ls != previous[i].ls || nodes[i].lf != previous[i].lf) {
                changed = true;
                break;
            }
        }
        if (!changed) {
            converged = true;
            break;
        }
    }
    if (!converged) {
        return make_error(CpmError::did_not_converge,
                          "schedule_tasks: the pass loop did not converge");
    }

    // --- slack, free slack, criticality (§3.5) ------------------------------
    const ticks critical_limit =
        static_cast<ticks>(options.critical_slack_limit_days) *
        options.duration.minutes_per_day() * kTicksPerMinute;
    for (Node& node : nodes) {
        const Calendar& cal = *node.calendar;
        const bool manual = is_set(node.manual_start);
        const ticks start_raw = duration_delta(cal, node.es, node.ls, node);
        const ticks finish_raw = duration_delta(cal, node.ef, node.lf, node);
        // MEASURED RULE (project_a-recalc.mpp rows +190, uids 10849/10851; summary
        // 8141/10720/10761): TotalSlack is the FINISH slack LF - EF.  It is not
        // min(LS-ES, LF-EF): a started task stores StartSlack 0 (LS pinned to the
        // actual start) while TotalSlack is 16320 / 39360 min = LF - EF, and a
        // summary's TotalSlack is its rolled-up LF - EF even when its StartSlack
        // is 0.  A start-side number only lowers it when it is NEGATIVE, which is
        // the measured hard-constraint conflict (w3-o4 MSO/SNLT: TotalSlack -2400
        // while LF - EF is 0); the same is true of the constraint slack below.
        ticks total = finish_raw;
        if (start_raw < 0) {
            total = std::min(total, start_raw);
        }
        // Constraint slacks.  MEASURED (w3-o4 infeasible rows): when a hard
        // constraint places the task away from where its links want it, the
        // difference is the total slack -- -2400 for MSO/SNLT, -2880 for
        // MFO/FNLT -- and the start slack carries it while the finish slack stays
        // at LF-EF.  A manual row ignores its own constraint (see the forward
        // pass), so no constraint slack is counted for it.
        if (!manual && is_set(node.constraint_date)) {
            ticks constraint_slack = 0;
            bool have = false;
            switch (node.constraint) {
                case ConstraintType::must_start_on:
                case ConstraintType::start_no_later_than:
                    constraint_slack = duration_delta(cal, node.link_es, node.constraint_date, node);
                    have = true;
                    break;
                case ConstraintType::must_finish_on:
                case ConstraintType::finish_no_later_than:
                    constraint_slack = duration_delta(cal, node.link_ef, node.constraint_date, node);
                    have = true;
                    break;
                default:
                    break;
            }
            if (have) {
                // Only a NEGATIVE (conflicting) constraint slack lowers the total;
                // a positive one is the constraint relaxing the late dates and is
                // already part of LF - EF (w3-o4 SNLT feasible rows stay 1920).
                if (constraint_slack < 0) {
                    total = std::min(total, constraint_slack);
                }
            }
        }
        node.total_slack = total;
        // MEASURED (every manual row of every recorded oracle: recalculate 0 /
        // 9600 / 14400, constraint-snet the same, constraint-mso the same,
        // link-fs-lag 0 / 0 / 33600, delete 24000 / 0): the three slack words are
        // ONE number on a manual row, because the whole row moves -- the start is
        // pinned (LS = ES) and the finish carries the slack.  So StartSlack is
        // reported equal to TotalSlack, not LS - ES.
        node.start_slack = manual ? total : std::min(start_raw, total);
        node.finish_slack = finish_raw;
        // MEASURED (wave 9b; corpus/probe/schedin.mpp uid 2, the ALAP row, storage
        // words only): an ALAP task is PLACED at its late start, so it has no
        // start slack and no free slack left -- row +186 (StartSlack) and +74
        // (FreeSlack) are both 0 there -- while TotalSlack stays LF - EF on the
        // unconstrained early dates (row +190 = 52800 ticks = 5280 min, the value
        // this engine reports).  That is why the three words differ on this one
        // row and nowhere else in the corpus.
        // [I] scope: one measured row; the task has no successors and no links,
        // and the rule is applied to every ALAP task.  A manual row's own
        // constraint is ignored (forward pass), so it is never ALAP-placed here.
        const bool alap =
            !manual && node.constraint == ConstraintType::as_late_as_possible;
        if (node.outgoing.empty()) {
            // MEASURED (w3-o1): a task with no successors reports free slack equal
            // to its total slack, clamped at zero (w3-o4 pinned rows report 0).
            // TODO(open: W3-P3): negative free slack is not directly measured.
            node.free_slack = std::max<ticks>(total, 0);
        } else {
            bool first = true;
            bool any_counted = false;
            ticks free = 0;
            for (const std::size_t link_index : node.outgoing) {
                const CpmLink& link = links[link_index];
                const Node& succ = nodes[succ_index[link_index]];
                // MEASURED (project_a-recalc.mpp uid 10851): a completed successor is
                // excluded from free slack too -- Project stores 28800 min for
                // 10851, the gap to its not-started successor 10870, not the
                // 2400 min gap to the completed 10757.  See CpmTask::actual_finish.
                if (is_set(succ.actual_finish)) {
                    continue;
                }
                ticks slip = 0;
                switch (link.type) {
                    case LinkType::finish_to_start:
                        slip = duration_delta(cal, node.ef, succ.es, node) - link.lag;
                        break;
                    case LinkType::start_to_start:
                        slip = duration_delta(cal, node.es, succ.es, node) - link.lag;
                        break;
                    case LinkType::finish_to_finish:
                        slip = duration_delta(cal, node.ef, succ.ef, node) - link.lag;
                        break;
                    case LinkType::start_to_finish:
                        slip = duration_delta(cal, node.es, succ.ef, node) - link.lag;
                        break;
                }
                free = first ? slip : std::min(free, slip);
                first = false;
                any_counted = true;
            }
            // TODO(open: W3-P3): whether MSP clamps negative free slack to zero is
            // open; every measured free slack is >= 0, so the clamp is safe here.
            //
            // MEASURED (w3-o3 FF-1d): the raw slip formula counts a LEAD twice --
            // FF-1d's predecessor can slip one day before the (floored) successor
            // moves, so the formula returns +480 while Project reports FreeSlack 0
            // and TotalSlack 0.  Free slack can never exceed total slack, so the
            // measured row is reproduced by taking the smaller of the two; every
            // other measured free-slack row already satisfies free <= total.
            // When every successor is complete, no link is counted and the
            // no-successor rule applies (free == total).
            node.free_slack =
                any_counted ? std::max<ticks>(std::min(free, total), 0) : std::max<ticks>(total, 0);
        }
        if (alap) {
            // See the ALAP block above: placed at the late start, nothing left to
            // slip.  A negative total still wins, hence the min with 0.
            node.start_slack = std::min<ticks>(0, total);
            node.free_slack = std::min<ticks>(0, node.free_slack);
        }
        // MEASURED (every manual row of every oracle; core/mpp derives Critical
        // the same way and cites base.mpp Task A): a manually scheduled row is
        // never Critical, even at zero slack.
        node.critical = !manual && node.total_slack <= critical_limit;
    }

    // --- results, in input order -------------------------------------------
    const bool from_finish = !options.schedule_from_start;
    result.tasks.reserve(nodes.size());
    for (const CpmTask& task : tasks) {
        const Node& node = nodes[index_of.at(task.id)];
        CpmTaskResult out;
        out.id = node.id;
        const bool display_late =
            from_finish || node.constraint == ConstraintType::as_late_as_possible;
        out.start = display_late ? node.ls : node.es;
        out.finish = display_late ? node.lf : node.ef;
        out.early_start = node.es;
        out.early_finish = node.ef;
        out.late_start = node.ls;
        out.late_finish = node.lf;
        out.start_slack = node.start_slack;
        out.finish_slack = node.finish_slack;
        out.free_slack = node.free_slack;
        out.total_slack = node.total_slack;
        out.critical = node.critical;
        out.elapsed_duration = task.elapsed_duration;
        // A manual row's stored constraint is never applied (see the forward
        // pass), so report the constraint that WAS applied: ASAP.
        out.constraint = is_set(node.manual_start) ? ConstraintType::as_soon_as_possible
                                                   : node.constraint;
        result.tasks.push_back(out);
    }
    result.error = CpmError::none;
    return result;
}

}  // namespace core::sched
