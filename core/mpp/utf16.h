/*
 * utf16.h -- UTF-16LE -> UTF-8, the one text primitive both layers need.
 *
 * .mpp stores task names and CFB stores directory entry names as UTF-16LE.
 * Both are the same conversion, so it lives here rather than being copied.
 * Unpaired surrogates become U+FFFD; nothing throws.
 */

#ifndef MPP_UTF16_H
#define MPP_UTF16_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace mpp {

inline void append_utf8(std::string& out, std::uint32_t code_point) {
    if (code_point > 0x10FFFFu) {
        code_point = 0xFFFDu;
    }
    if (code_point < 0x80u) {
        out.push_back(static_cast<char>(code_point));
    } else if (code_point < 0x800u) {
        out.push_back(static_cast<char>(0xC0u | (code_point >> 6)));
        out.push_back(static_cast<char>(0x80u | (code_point & 0x3Fu)));
    } else if (code_point < 0x10000u) {
        out.push_back(static_cast<char>(0xE0u | (code_point >> 12)));
        out.push_back(static_cast<char>(0x80u | ((code_point >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (code_point & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xF0u | (code_point >> 18)));
        out.push_back(static_cast<char>(0x80u | ((code_point >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((code_point >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (code_point & 0x3Fu)));
    }
}

/*
 * Decode `bytes` of UTF-16LE.  A trailing NUL (and everything after it) is
 * dropped when stop_at_nul is set, which is how .mpp terminates strings.  An
 * odd trailing byte is ignored.
 */
inline std::string utf16le_to_utf8(const std::uint8_t* data, std::size_t bytes,
                                   bool stop_at_nul = true) {
    std::string out;
    out.reserve(bytes / 2);
    std::size_t i = 0;
    while (i + 1 < bytes) {
        const std::uint32_t unit = static_cast<std::uint32_t>(data[i]) |
                                   (static_cast<std::uint32_t>(data[i + 1]) << 8);
        i += 2;
        if (stop_at_nul && unit == 0) {
            break;
        }
        if (unit >= 0xD800u && unit <= 0xDBFFu && i + 1 < bytes) {
            const std::uint32_t low = static_cast<std::uint32_t>(data[i]) |
                                      (static_cast<std::uint32_t>(data[i + 1]) << 8);
            if (low >= 0xDC00u && low <= 0xDFFFu) {
                i += 2;
                append_utf8(out, 0x10000u + ((unit - 0xD800u) << 10) +
                                     (low - 0xDC00u));
                continue;
            }
        }
        if (unit >= 0xD800u && unit <= 0xDFFFu) {
            append_utf8(out, 0xFFFDu);
            continue;
        }
        append_utf8(out, unit);
    }
    return out;
}

}  // namespace mpp

#endif /* MPP_UTF16_H */
