#include "atomic_write.h"

#include "cfb_writer.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace mppwrite {

namespace {

long process_id() {
#if defined(_WIN32)
    return static_cast<long>(_getpid());
#else
    return static_cast<long>(::getpid());
#endif
}

std::vector<std::uint8_t> read_bytes(std::string const& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw Error("cannot re-read '" + path + "'");
    }
    std::vector<std::uint8_t> bytes;
    bytes.assign(std::istreambuf_iterator<char>(input),
                 std::istreambuf_iterator<char>());
    if (input.bad()) {
        throw Error("cannot re-read '" + path + "'");
    }
    return bytes;
}

}  // namespace

std::string atomic_temp_path(std::string const& path) {
    return path + ".tmp-" + std::to_string(process_id());
}

void write_file_atomically(std::string const& path,
                           std::vector<std::uint8_t> const& bytes,
                           AtomicWriteVerifier const& verify) {
    if (path.empty()) {
        throw Error("cannot write an empty path");
    }
    const std::string temp = atomic_temp_path(path);
    /* Remove only a file this call created: the temporary name can already be
     * occupied by something the caller owns (a directory in the W33 test), and
     * deleting that would itself be a data-loss bug. */
    bool created = false;
    auto cleanup = [&temp, &created]() {
        if (!created) {
            return;
        }
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
    };

    try {
        {
            std::ofstream output(temp, std::ios::binary | std::ios::trunc);
            if (!output) {
                throw Error("cannot write '" + path + "': the temporary file '" +
                            temp + "' cannot be created; the target is unchanged");
            }
            created = true;
            if (!bytes.empty()) {
                output.write(reinterpret_cast<char const*>(bytes.data()),
                             static_cast<std::streamsize>(bytes.size()));
            }
            output.flush();
            if (!output) {
                throw Error("short write to '" + temp +
                            "'; the target '" + path + "' is unchanged");
            }
            output.close();
            if (!output) {
                throw Error("cannot close '" + temp + "'; the target '" + path +
                            "' is unchanged");
            }
        }

        std::vector<std::uint8_t> const reread = read_bytes(temp);
        if (reread != bytes) {
            throw Error("the temporary file '" + temp + "' re-reads as " +
                        std::to_string(reread.size()) + " bytes, not the " +
                        std::to_string(bytes.size()) +
                        " written; the target '" + path + "' is unchanged");
        }
        if (verify) {
            const std::string reason = verify(reread);
            if (!reason.empty()) {
                throw Error("the written file failed its self-check (" + reason +
                            "); the target '" + path + "' is unchanged");
            }
        }

        std::error_code rename_error;
        std::filesystem::rename(temp, path, rename_error);
        if (rename_error) {
            throw Error("cannot replace '" + path + "' with '" + temp + "': " +
                        rename_error.message() + "; the previous file is unchanged");
        }
    } catch (...) {
        cleanup();
        throw;
    }
}

}  // namespace mppwrite
