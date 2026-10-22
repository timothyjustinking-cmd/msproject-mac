/*
 * fuzz_common.h -- shared plumbing for the wave-13 (W57) adversarial fuzzers.
 *
 * The three fuzz targets (fuzz_reader, fuzz_writer, fuzz_parsers) are
 * deterministic: every case is derived from an explicit seed, the seed is
 * printed on failure, and re-running the same command reproduces the same case
 * sequence.  Nothing here uses randomness from the clock or the OS.
 *
 * This header is test/tooling code.  It does not belong to the product.
 */

#ifndef W57_FUZZ_COMMON_H
#define W57_FUZZ_COMMON_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fuzz {

using Bytes = std::vector<std::uint8_t>;

/* splitmix64.  Chosen because it is tiny, deterministic and has no state
 * beyond one word, so a case can be reproduced from (seed, case index) alone. */
class Rng {
  public:
    explicit Rng(std::uint64_t seed) : state_(seed == 0 ? 0x9E3779B97F4A7C15ULL : seed) {}

    std::uint64_t next() {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    /* Uniform-enough value in [0, bound); bound == 0 returns 0. */
    std::uint32_t below(std::uint32_t bound) {
        return bound == 0 ? 0u : static_cast<std::uint32_t>(next() % bound);
    }

    std::uint8_t byte() { return static_cast<std::uint8_t>(next() & 0xFFu); }

    std::uint64_t word64() { return next(); }

  private:
    std::uint64_t state_;
};

inline std::string to_hex(std::uint64_t value, unsigned digits) {
    static char const* table = "0123456789abcdef";
    std::string out(digits, '0');
    for (unsigned i = 0; i < digits; ++i) {
        out[digits - 1 - i] = table[(value >> (4 * i)) & 0xFu];
    }
    return out;
}

inline std::string hex64(std::uint64_t value) { return "0x" + to_hex(value, 16); }
inline std::string hex32(std::uint32_t value) { return "0x" + to_hex(value, 8); }

inline Bytes read_file(std::string const& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open " + path);
    }
    input.seekg(0, std::ios::end);
    std::streamoff const length = input.tellg();
    if (length < 0) {
        throw std::runtime_error("cannot size " + path);
    }
    input.seekg(0, std::ios::beg);
    Bytes bytes(static_cast<std::size_t>(length));
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        if (!input) {
            throw std::runtime_error("short read on " + path);
        }
    }
    return bytes;
}

inline void write_file(std::string const& path, Bytes const& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot create " + path);
    }
    if (!bytes.empty()) {
        output.write(reinterpret_cast<char const*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }
    if (!output) {
        throw std::runtime_error("cannot write " + path);
    }
}

inline bool file_exists(std::string const& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}

inline std::uint64_t fnv1a64(Bytes const& bytes) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (std::uint8_t const byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

inline std::uint64_t fnv1a64(std::string_view text) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (char const ch : text) {
        hash ^= static_cast<std::uint8_t>(ch);
        hash *= 1099511628211ULL;
    }
    return hash;
}

/* Callers guarantee the range exists; these are mutation helpers, not a parser. */
inline std::uint16_t u16_at(Bytes const& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(bytes[offset] |
                                      (static_cast<std::uint16_t>(bytes[offset + 1]) << 8));
}

inline std::uint32_t u32_at(Bytes const& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

inline void put_u16_at(Bytes& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFFu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
}

inline void put_u32_at(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFFu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
    bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
}

inline bool has_range(Bytes const& bytes, std::size_t offset, std::size_t width) {
    return offset <= bytes.size() && width <= bytes.size() - offset;
}

/* "1234 ms" for the timing receipts. */
inline std::string ms_text(double millis) {
    return std::to_string(static_cast<long long>(millis + 0.5)) + " ms";
}

inline double elapsed_ms(std::chrono::steady_clock::time_point start) {
    auto const delta = std::chrono::steady_clock::now() - start;
    return std::chrono::duration<double, std::milli>(delta).count();
}

/* A check counter with the same output shape the other tests in this tree use. */
class Checker {
  public:
    void check(bool condition, std::string const& what) {
        ++checks_;
        if (!condition) {
            ++failures_;
            std::cout << "FAIL: " << what << "\n";
        }
    }

    void equal(std::size_t actual, std::size_t expected, std::string const& what) {
        check(actual == expected, what + " (actual " + std::to_string(actual) +
                                      ", expected " + std::to_string(expected) + ")");
    }

    void equal_u32(std::uint32_t actual, std::uint32_t expected, std::string const& what) {
        check(actual == expected, what + " (actual " + std::to_string(actual) +
                                      ", expected " + std::to_string(expected) + ")");
    }

    void fail(std::string const& what) { check(false, what); }

    int checks() const { return checks_; }
    int failures() const { return failures_; }

    int report(std::string const& name) const {
        std::cout << (failures_ == 0 ? "PASS " : "FAIL ") << name << ": " << checks_
                  << " checks, " << failures_ << " failures\n";
        return failures_ == 0 ? 0 : 1;
    }

  private:
    int checks_ = 0;
    int failures_ = 0;
};

/*
 * The parent process spawns the worker (see fuzz_watchdog.h).  Before every
 * case the worker rewrites this file with a one-line description, so a worker
 * that hangs or crashes can be attributed to the exact case that was running.
 */
class Progress {
  public:
    explicit Progress(std::string path) : path_(std::move(path)) {}

    void note(std::string const& text) const {
        if (path_.empty()) {
            return;
        }
        std::ofstream out(path_, std::ios::binary | std::ios::trunc);
        if (out) {
            out << text << "\n";
        }
    }

    std::string const& path() const { return path_; }

    bool last(std::string& text) const {
        if (path_.empty()) {
            return false;
        }
        std::ifstream in(path_, std::ios::binary);
        if (!in) {
            return false;
        }
        std::string line;
        if (!std::getline(in, line)) {
            return false;
        }
        text = line;
        return true;
    }

  private:
    std::string path_;
};

inline std::string find_first_existing(std::vector<std::string> const& candidates) {
    for (std::string const& candidate : candidates) {
        if (file_exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

inline std::string join_path(std::string const& directory, std::string const& name) {
    if (directory.empty()) {
        return name;
    }
    return (std::filesystem::path(directory) / name).string();
}

}  // namespace fuzz

#endif /* W57_FUZZ_COMMON_H */
