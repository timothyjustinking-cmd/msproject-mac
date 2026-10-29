/*
 * inplace.cpp -- see inplace.h.
 *
 * The only non-obvious part is the mapping.  A stream's bytes are not
 * contiguous in the file: they follow the stream's sector chain (regular
 * streams) or the mini chain into the mini stream, whose bytes in turn follow
 * the root entry's sector chain.  `map()` resolves both cases to file ranges
 * and the caller writes through them, so the container is never rebuilt and no
 * other byte of the file can move.
 */

#include "inplace.h"

#include "atomic_write.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace mppwrite {

InPlacePatcher InPlacePatcher::from_file(std::string const& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw Error("cannot open '" + path + "'");
    }
    input.seekg(0, std::ios::end);
    std::streamoff const length = input.tellg();
    if (length < 0) {
        throw Error("cannot determine the size of '" + path + "'");
    }
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    if (!bytes.empty() &&
        !input.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()))) {
        throw Error("short read on '" + path + "'");
    }
    return from_bytes(std::move(bytes));
}

InPlacePatcher InPlacePatcher::from_bytes(std::vector<std::uint8_t> bytes) {
    InPlacePatcher patcher;
    patcher.original_ = bytes;
    patcher.bytes_ = std::move(bytes);
    patcher.doc_ = CfbDocument::from_bytes(patcher.bytes_);
    return patcher;
}

std::size_t InPlacePatcher::stream_size(std::string_view stream) const {
    return static_cast<std::size_t>(doc_.stream_size(stream));
}

std::vector<ByteRange> InPlacePatcher::map(std::string_view stream,
                                           std::uint64_t offset,
                                           std::size_t length) const {
    std::vector<std::pair<std::uint64_t, std::size_t>> const ranges =
        doc_.stream_file_ranges(stream);
    std::vector<ByteRange> out;
    std::uint64_t position = 0; /* stream offset of the current range */
    std::size_t remaining = length;
    for (auto const& range : ranges) {
        if (remaining == 0) {
            break;
        }
        std::uint64_t const range_end = position + range.second;
        if (offset + remaining <= position) {
            break;
        }
        if (offset >= range_end) {
            position = range_end;
            continue;
        }
        std::size_t const skip = static_cast<std::size_t>(
            offset > position ? offset - position : 0);
        std::size_t const take =
            std::min<std::size_t>(range.second - skip, remaining);
        out.push_back(ByteRange{range.first + skip, take});
        remaining -= take;
        offset += take;
        position = range_end;
    }
    if (remaining != 0) {
        throw Error("stream '" + std::string(stream) +
                    "' does not hold the requested range");
    }
    return out;
}

std::vector<std::uint8_t> InPlacePatcher::read(std::string_view stream,
                                               std::uint64_t offset,
                                               std::size_t length) const {
    if (offset + length > stream_size(stream)) {
        throw Error("read past the end of stream '" + std::string(stream) + "'");
    }
    std::vector<std::uint8_t> out;
    out.reserve(length);
    for (ByteRange const& r : map(stream, offset, length)) {
        out.insert(out.end(),
                   bytes_.begin() + static_cast<std::ptrdiff_t>(r.offset),
                   bytes_.begin() + static_cast<std::ptrdiff_t>(r.end()));
    }
    return out;
}

std::uint16_t InPlacePatcher::read_u16(std::string_view stream,
                                       std::uint64_t offset) const {
    std::vector<std::uint8_t> const b = read(stream, offset, 2);
    return static_cast<std::uint16_t>(b[0] | (b[1] << 8));
}

std::uint32_t InPlacePatcher::read_u32(std::string_view stream,
                                       std::uint64_t offset) const {
    std::vector<std::uint8_t> const b = read(stream, offset, 4);
    return static_cast<std::uint32_t>(b[0]) |
           (static_cast<std::uint32_t>(b[1]) << 8) |
           (static_cast<std::uint32_t>(b[2]) << 16) |
           (static_cast<std::uint32_t>(b[3]) << 24);
}

std::uint32_t InPlacePatcher::read_u32_be(std::string_view stream,
                                          std::uint64_t offset) const {
    std::vector<std::uint8_t> const b = read(stream, offset, 4);
    return (static_cast<std::uint32_t>(b[0]) << 24) |
           (static_cast<std::uint32_t>(b[1]) << 16) |
           (static_cast<std::uint32_t>(b[2]) << 8) |
           static_cast<std::uint32_t>(b[3]);
}

void InPlacePatcher::write(std::string_view stream, std::uint64_t offset,
                           std::span<std::uint8_t const> value) {
    if (offset + value.size() > stream_size(stream)) {
        throw Error("write past the end of stream '" + std::string(stream) +
                    "' (" + std::to_string(offset) + " + " +
                    std::to_string(value.size()) + " > " +
                    std::to_string(stream_size(stream)) + ")");
    }
    std::vector<ByteRange> const targets = map(stream, offset, value.size());
    std::size_t done = 0;
    for (ByteRange const& r : targets) {
        std::memcpy(bytes_.data() + r.offset, value.data() + done, r.length);
        for (std::size_t i = 0; i < r.length; ++i) {
            touched_.push_back(r.offset + i);
        }
        done += r.length;
    }
    if (done != value.size()) {
        throw Error("internal: mapped " + std::to_string(done) + " of " +
                    std::to_string(value.size()) + " bytes");
    }
    std::sort(touched_.begin(), touched_.end());
    touched_.erase(std::unique(touched_.begin(), touched_.end()), touched_.end());
}

void InPlacePatcher::write_u16(std::string_view stream, std::uint64_t offset,
                               std::uint16_t value) {
    std::uint8_t raw[2];
    raw[0] = static_cast<std::uint8_t>(value & 0xFFu);
    raw[1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
    write(stream, offset, std::span<std::uint8_t const>(raw, 2));
}

void InPlacePatcher::write_u32(std::string_view stream, std::uint64_t offset,
                               std::uint32_t value) {
    std::uint8_t raw[4];
    raw[0] = static_cast<std::uint8_t>(value & 0xFFu);
    raw[1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
    raw[2] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
    raw[3] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
    write(stream, offset, std::span<std::uint8_t const>(raw, 4));
}

std::vector<ByteRange> InPlacePatcher::changed_ranges() const {
    std::vector<ByteRange> out;
    for (std::uint64_t const o : touched_) {
        if (original_[o] == bytes_[o]) {
            continue;
        }
        if (!out.empty() && out.back().end() == o) {
            ++out.back().length;
        } else {
            out.push_back(ByteRange{o, 1});
        }
    }
    return out;
}

std::size_t InPlacePatcher::changed_byte_count() const {
    std::size_t n = 0;
    for (std::uint64_t const o : touched_) {
        if (original_[o] != bytes_[o]) {
            ++n;
        }
    }
    return n;
}

bool InPlacePatcher::differs_at(std::uint64_t offset) const {
    if (offset >= bytes_.size()) {
        return false;
    }
    return original_[offset] != bytes_[offset];
}

void InPlacePatcher::save(std::string const& path) const {
    /* W33-0.1 (A-2 / D-1 / D-4): the same write-beside-and-rename rule as
     * CfbDocument::save.  The bytes are compared on re-read; a failure leaves
     * the target exactly as it was. */
    write_file_atomically(path, bytes_);
}

} /* namespace mppwrite */
