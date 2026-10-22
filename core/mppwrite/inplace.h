/*
 * inplace.h -- change bytes in a container without re-serialising it.
 *
 * WHY THIS IS THE SECOND MILESTONE.  Rebuilding a container to change one
 * field is the expensive answer, and it is the answer that breaks things: every
 * byte you rewrite is a byte you can get wrong, and a full save has to get
 * allocation, directory, FAT and mini FAT all right at once.  Project itself
 * does not do that for a single edit.  So the cheapest useful writer is:
 *
 *     find the byte's physical location in the file, change it, leave
 *     everything else alone.
 *
 * This class does exactly that.  It keeps the file bytes and a parsed container
 * side by side; `write()` maps the stream offset through
 *
 *     stream -> (mini chain -> mini stream -> root chain)  or  (chain -> FAT)
 *            -> sector -> file offset
 *
 * and touches only the bytes that carry the value.  No sector is moved, no
 * table is rebuilt, the file length does not change, and `changed_ranges()`
 * reports the exact byte spans that differ from the input so a test can assert
 * "only these four bytes changed".
 *
 * WHAT IT CANNOT DO.  It cannot change a value's size, so it cannot grow a
 * string, add a record or change a stream's length.  That is stage 3
 * (`replace_stream` in cfb_writer.h, and the table layer in table.h).
 */

#ifndef MPPWRITE_INPLACE_H
#define MPPWRITE_INPLACE_H

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cfb_writer.h"

namespace mppwrite {

/* A half-open byte range in the file: [offset, offset + length). */
struct ByteRange {
    std::uint64_t offset = 0;
    std::size_t length = 0;

    std::uint64_t end() const { return offset + length; }
};

class InPlacePatcher {
  public:
    static InPlacePatcher from_file(std::string const& path);
    static InPlacePatcher from_bytes(std::vector<std::uint8_t> bytes);

    std::uint64_t file_size() const { return bytes_.size(); }
    std::size_t stream_size(std::string_view stream) const;

    /* Read through the container, so a value written by `write()` reads back. */
    std::vector<std::uint8_t> read(std::string_view stream, std::uint64_t offset,
                                   std::size_t length) const;
    std::uint16_t read_u16(std::string_view stream, std::uint64_t offset) const;
    std::uint32_t read_u32(std::string_view stream, std::uint64_t offset) const;
    std::uint32_t read_u32_be(std::string_view stream, std::uint64_t offset) const;

    /*
     * Write a value at `offset` inside `stream`.  Throws Error when the range
     * is outside the stream.  A value may cross a sector boundary (the mapping
     * splits it); it may not cross the end of the stream.
     */
    void write(std::string_view stream, std::uint64_t offset,
               std::span<std::uint8_t const> value);
    void write_u16(std::string_view stream, std::uint64_t offset,
                   std::uint16_t value);
    void write_u32(std::string_view stream, std::uint64_t offset,
                   std::uint32_t value);

    /* Everything written so far, merged and in file order. */
    std::vector<ByteRange> changed_ranges() const;
    std::size_t changed_byte_count() const;

    /* True when the bytes at `offset` differ from the bytes this patcher was
     * built from. */
    bool differs_at(std::uint64_t offset) const;

    std::vector<std::uint8_t> const& bytes() const { return bytes_; }
    CfbDocument const& container() const { return doc_; }
    void save(std::string const& path) const;

  private:
    InPlacePatcher() = default;

    /* Map a stream range to the list of file ranges that carry it. */
    std::vector<ByteRange> map(std::string_view stream, std::uint64_t offset,
                               std::size_t length) const;

    CfbDocument doc_;
    std::vector<std::uint8_t> bytes_;
    std::vector<std::uint8_t> original_;
    std::vector<std::uint64_t> touched_; /* sorted, unique file offsets */
};

} /* namespace mppwrite */

#endif /* MPPWRITE_INPLACE_H */
