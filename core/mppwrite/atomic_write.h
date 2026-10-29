/*
 * atomic_write.h -- W33-0.1 (A-2 / D-1 / D-4): never truncate a target.
 *
 * WHY THIS EXISTS.  Every pre-W33 .mpp writer opened the FINAL path with
 * ios::trunc and wrote directly into it (CfbDocument::save,
 * InPlacePatcher::save).  After the first structural save both shells reload
 * the target and adopt it as the open document, so every later Save overwrites
 * the only copy in place: a crash or an I/O failure after the truncate but
 * before the last byte destroys the user's file, and the shell's alert ("The
 * opened file was not modified") is then false.
 *
 * THE RULE.  write_file_atomically() writes the bytes to
 * `<target>.tmp-<pid>` in the TARGET'S DIRECTORY (same volume, so the rename is
 * a rename and not a copy), flushes, re-reads the temporary file and compares
 * every byte with the bytes it was given, runs an optional caller self-check on
 * the re-read (CfbDocument::save passes the CFB parse + re-emit + check), and
 * only then renames the temporary file over the target.  On ANY failure the
 * temporary file is removed and the target is left exactly as it was.  An
 * existing target is never opened for writing, so it is never truncated.
 *
 * WHAT IT DOES NOT DO.  It does not fsync: std::ofstream has no portable
 * handle for that, and the guarantee this helper makes is about a failed write
 * or a failed self-check, not about power loss after a successful rename.  It
 * does not create the target's directory, and it does not follow a path that is
 * a directory: both fail cleanly with the target untouched.
 */

#ifndef MPPWRITE_ATOMIC_WRITE_H
#define MPPWRITE_ATOMIC_WRITE_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mppwrite {

/* Returns the empty string when the re-read file passes the caller's
 * self-check, and a non-empty reason when it does not. */
using AtomicWriteVerifier =
    std::function<std::string(std::vector<std::uint8_t> const&)>;

/* `<path>.tmp-<pid>`, beside the target.  Exposed so a caller (and the W33
 * tests) can name the file the helper would use; nothing else should write
 * there. */
[[nodiscard]] std::string atomic_temp_path(std::string const& path);

/* Writes `bytes` to `path` through atomic_temp_path(path).  Throws Error with
 * the reason on any failure; on a throw the target is unchanged and the
 * temporary file is gone. */
void write_file_atomically(std::string const& path,
                           std::vector<std::uint8_t> const& bytes,
                           AtomicWriteVerifier const& verify = AtomicWriteVerifier());

}  // namespace mppwrite

#endif /* MPPWRITE_ATOMIC_WRITE_H */
