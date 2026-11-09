// tests/cfb_stream_removal.h -- a copy of a CFB document that lacks one stream.
//
// CfbDocument has no remove operation (a writer never drops a stream), but the
// view-state writers refuse a document that lacks a stream they need, and that
// guard has to be reached by a test.  The directory entry of the stream is found
// by renaming each stream entry in turn (its first UTF-16 unit) and keeping the
// rename that makes the wanted path disappear (the stream is still there under its
// new name, which no writer looks for).  The document must be
// unmodified, since the directory is read from the bytes it was parsed from.  Test-only.
#ifndef TESTS_CFB_STREAM_REMOVAL_H
#define TESTS_CFB_STREAM_REMOVAL_H

#include <cstddef>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cfb_writer.h"

namespace cfb_test {

inline mppwrite::CfbDocument without_stream(mppwrite::CfbDocument const& doc,
                                            std::string_view path) {
    constexpr std::size_t kEntryBytes = 128;
    constexpr std::size_t kDirectoryStartAt = 0x30;
    constexpr std::size_t kTypeAt = 66;
    constexpr std::uint8_t kStreamType = 2;
    if (!doc.unmodified()) {
        throw std::runtime_error("cfb_test::without_stream: the document was edited");
    }
    std::vector<std::uint8_t> const& bytes = doc.source_bytes();
    auto word = [&](std::size_t at) {
        return static_cast<std::uint32_t>(bytes[at]) |
               (static_cast<std::uint32_t>(bytes[at + 1]) << 8) |
               (static_cast<std::uint32_t>(bytes[at + 2]) << 16) |
               (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
    };
    std::vector<std::uint32_t> const& fat = doc.fat();
    std::vector<std::size_t> entries;
    std::uint32_t sector = word(kDirectoryStartAt);
    while (sector < fat.size() && entries.size() < 100000) {
        std::size_t const base = static_cast<std::size_t>(doc.sector_offset(sector));
        for (std::size_t at = base; at + kEntryBytes <= base + doc.sector_size();
             at += kEntryBytes) {
            entries.push_back(at);
        }
        sector = fat[sector];
    }
    for (std::size_t const at : entries) {
        if (at + kEntryBytes > bytes.size() || bytes[at + kTypeAt] != kStreamType) {
            continue;
        }
        std::vector<std::uint8_t> renamed = bytes;
        renamed[at] = static_cast<std::uint8_t>(renamed[at] ^ 0x01);
        try {
            mppwrite::CfbDocument candidate =
                mppwrite::CfbDocument::from_bytes(std::move(renamed));
            if (!candidate.has_stream(path) && candidate.stream_count() == doc.stream_count()) {
                return candidate;
            }
        } catch (std::exception const&) {
            continue;
        }
    }
    throw std::runtime_error("cfb_test::without_stream: no directory entry gives up " +
                             std::string(path));
}

}  // namespace cfb_test

#endif  // TESTS_CFB_STREAM_REMOVAL_H
