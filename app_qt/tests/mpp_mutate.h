// app_qt/tests/mpp_mutate.h -- wave 35 (W35-0.2, RW34b-3).
//
// A Qt gate test that needs a file shape no committed fixture has (a resource
// calendar with a stored working-time block, a resource row whose calendar word
// is moved) makes it here: a copy of a committed Project save with one stream
// edited through the same CfbDocument the writers use.  The edit is the
// smallest one that creates the shape, so the file is otherwise Project's own.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include "cfb_writer.h"

namespace mppmutate {

using Bytes = std::vector<std::uint8_t>;

inline std::uint32_t get_u32(const Bytes& b, std::size_t at) {
    return static_cast<std::uint32_t>(b[at]) | (static_cast<std::uint32_t>(b[at + 1]) << 8) |
           (static_cast<std::uint32_t>(b[at + 2]) << 16) |
           (static_cast<std::uint32_t>(b[at + 3]) << 24);
}

inline void put_u32(Bytes& b, std::size_t at, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        b[at + i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu);
    }
}

// Copies `source` to `target` with `edit` applied to the compound file.
// Returns false (and leaves no file) when the source cannot be read.
inline bool write_edited_copy(const std::string& source, const std::string& target,
                              const std::function<void(mppwrite::CfbDocument&)>& edit) {
    try {
        mppwrite::CfbDocument doc = mppwrite::CfbDocument::from_file(source);
        edit(doc);
        const Bytes bytes = doc.to_bytes();
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        return static_cast<bool>(out);
    } catch (const std::exception&) {
        std::error_code ignored;
        std::filesystem::remove(target, ignored);
        return false;
    }
}

// Adds one stored block (`length` payload bytes in Var2Data and a VarMeta entry
// at its (uid, key) position) to the calendar table for calendar `uid`.
inline void add_calendar_block(mppwrite::CfbDocument& doc, std::uint32_t uid, std::uint32_t key,
                               std::size_t length) {
    const char* const var2_path = "114/TBkndCal/Var2Data";
    const char* const meta_path = "114/TBkndCal/VarMeta";
    Bytes var2 = doc.read_stream(var2_path);
    Bytes meta = doc.read_stream(meta_path);
    const std::uint32_t offset = static_cast<std::uint32_t>(var2.size());
    Bytes block(4 + length, 0);
    put_u32(block, 0, static_cast<std::uint32_t>(length));
    var2.insert(var2.end(), block.begin(), block.end());
    const std::size_t count = (meta.size() - 24) / 12;
    std::size_t at = count;
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint32_t entry_uid = get_u32(meta, 24 + 12 * i);
        const std::uint32_t entry_key = get_u32(meta, 24 + 12 * i + 8);
        if (entry_uid > uid || (entry_uid == uid && entry_key > key)) {
            at = i;
            break;
        }
    }
    Bytes entry(12, 0);
    put_u32(entry, 0, uid);
    put_u32(entry, 4, offset);
    put_u32(entry, 8, key);
    meta.insert(meta.begin() + static_cast<std::ptrdiff_t>(24 + 12 * at), entry.begin(),
                entry.end());
    put_u32(meta, 8, static_cast<std::uint32_t>(count + 1));
    put_u32(meta, 0x14, static_cast<std::uint32_t>(var2.size()));
    doc.replace_stream(var2_path, var2);
    doc.replace_stream(meta_path, meta);
}

}  // namespace mppmutate
