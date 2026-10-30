/*
 * save_pair_compare.h -- W34-0.5: the whole-file save-pair comparison for the
 * paths whose Project evidence was corpus-only (Insert Task, Delete Task, link
 * ADD, custom-field SET and CLEAR).
 *
 * WHAT IT DOES.  Given the committed pair -- the control, Project's no-edit
 * save of it (the null save) and Project's edit save -- and the port's save of
 * the same edit from the same control, every stream in the union of the four
 * documents goes through save_pair::compare (tests/save_pair_noise.h).  A
 * stream whose null save and edit save differ is DECLARED to carry the edit's
 * effect, so its effect set must be non-empty; every unmasked byte must equal
 * Project's edit save.  The stream's noise, effects and (where it applies) the
 * appended tail are printed, exactly as the outline pair test does.
 *
 * A MEASURED GAP.  Where the port's save differs from Project's, the caller
 * records the difference in a KnownGap table with its EXACT signature: the
 * mismatch count, the first mismatching offset and the port/Project bytes
 * there.  The entry is a NAMED expected failure while it is open, so the gap
 * is recorded rather than patched blind and CI stays green.  A mismatch whose
 * signature is not in the table is a HARD failure, and a table entry whose
 * stream now matches Project is a hard failure too ("remove the entry"), so a
 * fixed gap cannot rot in the list.
 */

#ifndef SAVE_PAIR_COMPARE_H
#define SAVE_PAIR_COMPARE_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "cfb_writer.h"
#include "save_pair_noise.h"

namespace save_pair {

inline char const* kind_name(Kind kind) {
    switch (kind) {
        case Kind::fixed:
            return "fixed";
        case Kind::exact:
            return "exact";
        case Kind::props14:
            return "props14";
        case Kind::cedl:
            return "cedl";
    }
    return "?";
}

/* The kind for a stream path.  OLE control streams carry a leading 0x05 in
 * their path; the measured zero-noise stream is the name below it. */
inline Kind kind_for_path(std::string const& path) {
    std::string name = path;
    if (!name.empty() && name.front() == '\x05') {
        name.erase(name.begin());
    }
    if (name == "DocumentSummaryInformation") {
        return Kind::exact;
    }
    if (name == "Props14") {
        return Kind::props14;
    }
    if (name == "214/CEdl/Var2Data") {
        return Kind::cedl;
    }
    return Kind::fixed;
}

inline std::vector<std::uint8_t> stream_or_empty(
    mppwrite::CfbDocument const& doc, std::string const& path) {
    return doc.has_stream(path) ? doc.read_stream(path)
                                : std::vector<std::uint8_t>{};
}

inline std::string hex_byte(std::uint8_t byte) {
    char buffer[8];
    std::snprintf(buffer, sizeof buffer, "0x%02x",
                  static_cast<unsigned>(byte));
    return buffer;
}

inline std::string mismatch_message(std::string const& path,
                                    Verdict const& verdict) {
    std::string message = path + " equals Project's edit save outside the mask";
    if (verdict.mismatches != 0) {
        message += " (first of " + std::to_string(verdict.mismatches) +
                   " at byte " + std::to_string(verdict.first_mismatch) +
                   ": port " + hex_byte(verdict.output_byte) + ", Project " +
                   hex_byte(verdict.result_byte) + ")";
    }
    return message;
}

/* One recorded gap: a stream the port's save does not reproduce. */
struct KnownGap {
    char const* path;
    std::size_t mismatches;
    std::size_t first_mismatch;
    std::uint8_t port_byte;
    std::uint8_t project_byte;
};

inline KnownGap const* find_known_gap(KnownGap const* gaps, std::size_t count,
                                      std::string const& path) {
    for (std::size_t i = 0; i < count; ++i) {
        if (path == gaps[i].path) {
            return &gaps[i];
        }
    }
    return nullptr;
}

/* The streams the pair test DECLARES the edit changes.  The list is the
 * measured "Stream summary" of the committed pair anatomy
 * (spec/writers/evidence/wave34-*.md): a stream with a non-zero effect or
 * tail-effect count.  It is deliberately external to save_pair::compare: a
 * mask that widened over an effect would drop `effects` to zero, and this
 * check must then FAIL rather than shrink the declaration. */
inline bool declared_path(char const* const* paths, std::size_t count,
                          std::string const& path) {
    for (std::size_t i = 0; i < count; ++i) {
        if (path == paths[i]) {
            return true;
        }
    }
    return false;
}

/* One stream's declared normalised tail ranges (W34-3.1).  The ranges apply
 * only to `path`; every other stream compares as before. */
struct TailNormalisation {
    std::string path;
    std::vector<TailRange> ranges;
};

inline std::vector<TailRange> const& normalisation_for(
    std::vector<TailNormalisation> const& normalisations,
    std::string const& path) {
    static std::vector<TailRange> const kNone;
    for (TailNormalisation const& entry : normalisations) {
        if (path == entry.path) {
            return entry.ranges;
        }
    }
    return kNone;
}

inline void report_stream(std::string const& label, std::string const& path,
                          Kind kind, Verdict const& verdict) {
    std::printf("   %-18s %-34s %-7s noise=%-4llu effects=%-4llu",
                label.c_str(), path.c_str(), kind_name(kind),
                static_cast<unsigned long long>(verdict.noise),
                static_cast<unsigned long long>(verdict.effects));
    if (verdict.tail_bytes != 0) {
        std::printf(" tail=%llu tail-effects=%llu",
                    static_cast<unsigned long long>(verdict.tail_bytes),
                    static_cast<unsigned long long>(verdict.tail_effects));
    }
    std::printf("\n");
}

/*
 * Compare every stream of the four documents.  Returns true when every stream
 * either matches Project or matches its recorded KnownGap signature (an
 * EXPECTED FAIL); a mismatch the table does not describe, or a table entry the
 * port now matches, is reported through `ctx` as a hard failure.
 */
template <typename Context>
void compare_all(Context& ctx, std::string const& label,
                 mppwrite::CfbDocument const& control,
                 mppwrite::CfbDocument const& null_save,
                 mppwrite::CfbDocument const& project,
                 mppwrite::CfbDocument const& port, KnownGap const* gaps,
                 std::size_t gap_count, char const* const* declared_paths,
                 std::size_t declared_count,
                 std::vector<TailNormalisation> const& normalisations = {}) {
    std::vector<std::string> paths = control.stream_paths();
    for (mppwrite::CfbDocument const* doc : {&null_save, &project, &port}) {
        for (std::string const& path : doc->stream_paths()) {
            paths.push_back(path);
        }
    }
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());

    std::size_t total_noise = 0;
    std::size_t total_effects = 0;
    for (std::string const& path : paths) {
        Kind const kind = kind_for_path(path);
        std::vector<std::uint8_t> const control_stream =
            stream_or_empty(control, path);
        std::vector<std::uint8_t> const null_stream =
            stream_or_empty(null_save, path);
        std::vector<std::uint8_t> const project_stream =
            stream_or_empty(project, path);
        std::vector<std::uint8_t> const port_stream = stream_or_empty(port, path);
        bool const declared = declared_path(declared_paths, declared_count, path);
        Verdict const verdict = compare(kind, control_stream, null_stream,
                                        project_stream, port_stream, declared,
                                        normalisation_for(normalisations,
                                                          path));
        report_stream(label, path, kind, verdict);
        total_noise += verdict.noise;
        total_effects += verdict.effects + verdict.tail_effects;

        if (kind == Kind::exact) {
            ctx.check(verdict.noise_free,
                      label + ": " + path +
                          ": the null save leaves it byte for byte equal to "
                          "the control");
        } else if (kind == Kind::cedl) {
            /* Project's own pair may change this stream's length (the save
             * path record and the edit's own block), so control and result
             * lengths are not required to agree; every mismatching position,
             * including one side running out, is in `mismatches`. */
        } else {
            ctx.check(verdict.lengths_ok,
                      label + ": " + path +
                          ": control and null save have the measured lengths");
        }
        if (declared) {
            ctx.check(verdict.effects_ok,
                      label + ": " + path +
                          ": the edit's declared effect set is non-empty");
        }

        KnownGap const* const gap = find_known_gap(gaps, gap_count, path);
        if (gap == nullptr) {
            ctx.check(verdict.mismatches == 0,
                      label + ": " + mismatch_message(path, verdict));
            continue;
        }
        bool const same_gap = verdict.mismatches == gap->mismatches &&
                              verdict.first_mismatch == gap->first_mismatch &&
                              verdict.output_byte == gap->port_byte &&
                              verdict.result_byte == gap->project_byte;
        if (verdict.mismatches == 0) {
            ctx.expected_failure(
                true, label + ": " + path +
                          ": the recorded gap is gone -- delete the KnownGap "
                          "entry and make the check a normal one");
        } else if (same_gap) {
            ctx.expected_failure(false,
                                 label + ": " + mismatch_message(path, verdict));
        } else {
            ctx.check(false, label + ": " + mismatch_message(path, verdict) +
                                 " (a DIFFERENT mismatch from the recorded gap: "
                                 "expected " +
                                 std::to_string(gap->mismatches) + " at byte " +
                                 std::to_string(gap->first_mismatch) + ", port " +
                                 hex_byte(gap->port_byte) + ", Project " +
                                 hex_byte(gap->project_byte) + ")");
        }
    }
    std::cout << "   " << label << " pair totals: noise=" << total_noise
              << " effects=" << total_effects << "\n";
}

} /* namespace save_pair */

#endif /* SAVE_PAIR_COMPARE_H */
