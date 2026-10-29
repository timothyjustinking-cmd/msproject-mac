/*
 * atomic_write.cpp -- W33-0.1 (A-2 / D-1 / D-4): every .mpp write is atomic.
 *
 * CLAIM UNDER TEST.  A writer never truncates its target.  It writes the bytes
 * to a temporary file beside the target, flushes it, re-reads it and compares
 * every byte, and only then renames it over the target.  When anything fails --
 * the temporary file cannot even be created, the re-read differs, a caller's
 * extra self-check fails -- the target is byte-identical to what it was before
 * the call and no temporary file is left behind.
 *
 * The first section uses only CfbDocument::save, so it is the check that can
 * fail on the pre-W33 baseline: that writer opened the final path with
 * ios::trunc, so a save whose temporary name was blocked still rewrote the
 * target.  The later sections drive the W33 helper directly with an injected
 * re-read mismatch and prove the ordinary write, the overwrite and the
 * no-edit round trip.
 *
 * Nothing under tests/data is written: every output lands in the build tree's
 * test-out directory.
 */

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include "cfb_writer.h"
#include "atomic_write.h"

#ifndef MPPWRITE_TEST_DATA
#define MPPWRITE_TEST_DATA "tests/data"
#endif
#ifndef MPPWRITE_TEST_OUT
#define MPPWRITE_TEST_OUT "core/mppwrite/build/test-out"
#endif

namespace {

std::string join(std::string const& a, std::string const& b) { return a + "/" + b; }

long test_pid() {
#if defined(_WIN32)
    return static_cast<long>(_getpid());
#else
    return static_cast<long>(::getpid());
#endif
}

std::vector<std::uint8_t> file_bytes(std::filesystem::path const& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot read '" + path.string() + "'");
    }
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input),
                                     std::istreambuf_iterator<char>());
}

void write_raw(std::filesystem::path const& path, std::string const& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot write '" + path.string() + "'");
    }
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.close();
    if (!output) {
        throw std::runtime_error("short write on '" + path.string() + "'");
    }
}

/* The suffix the W33 helper documents; kept here so the pre-helper baseline can
 * be exercised: the helper's own atomic_temp_path() is checked against it below. */
std::filesystem::path expected_temp_path(std::filesystem::path const& target) {
    return std::filesystem::path(target.string() + ".tmp-" + std::to_string(test_pid()));
}

class TestContext {
  public:
    void check(bool condition, std::string const& what) {
        ++checks_;
        if (!condition) {
            ++failures_;
            std::cout << "FAIL: " << what << "\n";
        }
    }
    int report() const {
        std::cout << checks_ << " checks, " << failures_ << " failures\n";
        return failures_ == 0 ? 0 : 1;
    }

  private:
    int checks_ = 0;
    int failures_ = 0;
};

}  // namespace

int main() {
    TestContext t;
    try {
        std::filesystem::path const out = MPPWRITE_TEST_OUT;
        std::filesystem::create_directories(out);
        std::filesystem::path const source =
            std::filesystem::path(MPPWRITE_TEST_DATA) / "synthetic-base.mpp";
        if (!std::filesystem::is_regular_file(source)) {
            std::cout << "FAIL: the committed fixture is missing: " << source.string()
                      << "\n";
            return 1;
        }

        /* --- 1. a save whose temporary file cannot be created ---------------
         * A directory occupies <target>.tmp-<pid>.  The pre-W33 writer ignored
         * that name and truncated the target; the W33 writer must refuse and
         * leave the target exactly as it was. */
        {
            std::filesystem::path const target = out / "atomic-blocked-temp.mpp";
            write_raw(target, "the previous contents");
            std::vector<std::uint8_t> const before = file_bytes(target);
            std::filesystem::path const temp = expected_temp_path(target);
            std::filesystem::remove_all(temp);
            std::filesystem::create_directory(temp);

            bool threw = false;
            std::string message;
            try {
                mppwrite::CfbDocument document =
                    mppwrite::CfbDocument::from_file(source.string());
                document.save(target.string());
            } catch (std::exception const& error) {
                threw = true;
                message = error.what();
            }
            t.check(threw,
                    "a save whose temporary file cannot be created is refused");
            t.check(threw && !message.empty(),
                    "the refusal names a reason: " + message);
            t.check(file_bytes(target) == before,
                    "the target is byte-identical after the refused save");
            t.check(std::filesystem::is_directory(temp),
                    "the refusal removed nothing it did not create");
            std::filesystem::remove_all(temp);
        }

        /* --- 2. an ordinary save and an overwrite ---------------------------- */
        {
            std::filesystem::path const target = out / "atomic-plain.mpp";
            std::filesystem::remove(target);
            mppwrite::CfbDocument document =
                mppwrite::CfbDocument::from_file(source.string());
            document.save(target.string());
            t.check(std::filesystem::is_regular_file(target),
                    "an ordinary save writes the target");
            t.check(file_bytes(target) == file_bytes(source),
                    "a no-edit save is byte-identical to the source");
            t.check(!std::filesystem::exists(expected_temp_path(target)),
                    "the ordinary save leaves no temporary file");
        }

        /* --- 3. the helper's documented temporary name ----------------------- */
        {
            std::filesystem::path const target = out / "atomic-name.mpp";
            t.check(mppwrite::atomic_temp_path(target.string()) ==
                        expected_temp_path(target).string(),
                    "atomic_temp_path is <target>.tmp-<pid> beside the target");
        }

        /* --- 4. an injected re-read mismatch -------------------------------- */
        {
            std::filesystem::path const target = out / "atomic-mismatch.mpp";
            write_raw(target, "the previous contents");
            std::vector<std::uint8_t> const before = file_bytes(target);
            std::vector<std::uint8_t> const wanted{1, 2, 3, 4, 5};
            bool threw = false;
            std::string message;
            try {
                mppwrite::write_file_atomically(
                    target.string(), wanted,
                    [](std::vector<std::uint8_t> const&) {
                        return std::string("forced re-read mismatch");
                    });
            } catch (std::exception const& error) {
                threw = true;
                message = error.what();
            }
            t.check(threw, "a failing self-check refuses the write");
            t.check(message.find("forced re-read mismatch") != std::string::npos,
                    "the refusal names the self-check: " + message);
            t.check(file_bytes(target) == before,
                    "the target is byte-identical after the failed self-check");
            t.check(!std::filesystem::exists(expected_temp_path(target)),
                    "the failed self-check leaves no temporary file");
        }

        /* --- 5. the helper writes a new file and replaces an existing one --- */
        {
            std::filesystem::path const fresh = out / "atomic-new.bin";
            std::filesystem::remove(fresh);
            std::vector<std::uint8_t> const bytes{9, 8, 7};
            mppwrite::write_file_atomically(fresh.string(), bytes);
            t.check(file_bytes(fresh) == bytes, "the helper writes a new file");
            t.check(!std::filesystem::exists(expected_temp_path(fresh)),
                    "the helper leaves no temporary file beside a new file");

            std::vector<std::uint8_t> const replacement{6, 5};
            mppwrite::write_file_atomically(fresh.string(), replacement);
            t.check(file_bytes(fresh) == replacement,
                    "the helper replaces an existing file");
            t.check(!std::filesystem::exists(expected_temp_path(fresh)),
                    "the helper leaves no temporary file after a replacement");
        }
    } catch (std::exception const& error) {
        std::cout << "fatal: " << error.what() << "\n";
        return 1;
    }
    return t.report();
}
