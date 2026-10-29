/*
 * fixture_guard.h -- committed fixtures fail when missing; corpus/ skips.
 *
 * W33-3.3 (audit C-6).  The suite used to print "SKIP" and return 0 wherever a
 * fixture was missing, so a renamed, un-checked-out or never-added committed
 * fixture turned the checks that used it green without running.  The rule is
 * now:
 *
 *   tests/data/, tests/*.tsv, analysis/*.tsv
 *       committed: a missing file is a FAILURE naming the path;
 *   corpus/
 *       git-ignored client data: absence is a supported SKIP, and the words
 *       "corpus absent" are printed so a log says which kind of skip it was.
 *
 * require_committed() exits nonzero rather than returning: most call sites sit
 * in the middle of a test that has already been selected, and continuing with
 * an empty document would report a different failure.  The path is the whole
 * diagnosis, so the message always carries it.
 */
#ifndef FIXTURE_GUARD_H
#define FIXTURE_GUARD_H

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace fixture {

inline bool present(std::filesystem::path const& path) {
    std::error_code error;
    return std::filesystem::exists(path, error);
}

/* A committed fixture is part of the repository: missing is a FAILURE. */
inline void require_committed(std::filesystem::path const& path) {
    if (present(path)) {
        return;
    }
    std::printf("FAIL: committed fixture missing: %s\n", path.string().c_str());
    std::fflush(stdout);
    std::exit(1);
}

/* corpus/ is git-ignored client data: absence is a supported SKIP.  `why`
 * records the forced-absent (MPP_NO_CORPUS) or unusable-data case. */
inline void corpus_absent(std::filesystem::path const& path,
                          char const* why = nullptr) {
    if (why != nullptr) {
        std::printf("SKIP: corpus absent (%s): %s\n", path.string().c_str(),
                    why);
    } else {
        std::printf("SKIP: corpus absent (%s)\n", path.string().c_str());
    }
}

inline bool corpus_present(std::filesystem::path const& path,
                           char const* why = nullptr) {
    if (present(path)) {
        return true;
    }
    corpus_absent(path, why);
    return false;
}

} /* namespace fixture */

#endif /* FIXTURE_GUARD_H */
