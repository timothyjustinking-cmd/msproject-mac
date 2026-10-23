/*
 * cfb.h -- read-only Compound File Binary (OLE2) container access.
 *
 * An .mpp file is a CFB v3 document.  This is the smallest reader that can
 * serve the MPP record layer and nothing more:
 *
 *   - version 3 (512-byte sectors) and version 4 (4096-byte sectors);
 *   - regular FAT sector chains;
 *   - mini FAT chains (streams smaller than the 4096-byte cutoff), which real
 *     .mpp files use heavily: e.g. PROJECT_A's TBkndCal/FixedData is 460 bytes;
 *   - DIFAT continuation sectors (files above ~7 MB of FAT);
 *   - the directory tree, flattened to "/"-separated paths, with entries
 *     returned in traversal order.
 *
 * No writing, no CLSIDs, no property sets, no locking.
 *
 * NAMES.  MS Project pads storage names to a 6-character field, so the real
 * path in every corpus file is "   114/TBkndTask/FixedData" (three leading
 * spaces).  Every lookup here strips ASCII spaces from each path component
 * before comparing, so callers write "114/TBkndTask/FixedData".
 */

#ifndef MPP_CFB_H
#define MPP_CFB_H

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace mpp {

/* Every failure this library reports is one of these. */
class Error : public std::runtime_error {
  public:
    explicit Error(std::string const& message) : std::runtime_error(message) {}
};

class CompoundFile {
  public:
    /* Read the whole file; throws Error if it is not a readable CFB file. */
    static CompoundFile from_file(std::string const& path);
    static CompoundFile from_bytes(std::vector<std::uint8_t> bytes);

    /* True when a stream with this path exists (space-insensitive compare). */
    bool has_stream(std::string_view path) const;

    /* Byte size of a stream.  Throws Error when it does not exist. */
    std::uint64_t stream_size(std::string_view path) const;

    /* Full contents of a stream.  Throws Error when it does not exist or the
     * sector chain is short.  An empty stream returns an empty vector. */
    std::vector<std::uint8_t> read_stream(std::string_view path) const;

    /* Every stream path in the file, traversal order, as stored (spaces and
     * all).  Useful for diagnostics when a lookup fails. */
    std::vector<std::string> stream_paths() const;

    /*
     * Container anomalies that are recoverable, in the order they were found.
     * The one this reader reports is FAT slack: a file may carry sectors that no
     * chain references (MS Project writes such files; corpus/imported/sf248.mpp
     * has 959 sectors and a 768-entry FAT).  That is not damage -- every chain
     * still ends inside the FAT -- so it becomes "N unreferenced sectors" here
     * and the read continues.  A chain that actually points past the FAT is still
     * fatal and throws Error.  The reader copies these into
     * Document::warnings so a caller sees them.
     */
    std::vector<std::string> const& warnings() const { return warnings_; }

    /* Number of directory entries (storages + streams + root). */
    std::size_t entry_count() const { return entries_.size(); }

  private:
    struct Entry {
        std::string path;
        std::uint8_t type = 0;      /* 1 storage, 2 stream, 5 root */
        std::uint32_t start = 0;
        std::uint64_t size = 0;
    };

    CompoundFile() = default;
    void parse();

    std::size_t sector_offset(std::uint32_t sector) const;
    std::vector<std::uint8_t> read_regular_chain(std::uint32_t start,
                                                 std::uint64_t size) const;
    std::vector<std::uint8_t> read_mini_chain(std::uint32_t start,
                                              std::uint64_t size) const;
    std::uint32_t fat_next(std::uint32_t sector) const;
    std::uint32_t mini_fat_next(std::uint32_t sector) const;
    const Entry* find(std::string_view path) const;

    std::vector<std::uint8_t> data_;
    std::size_t sector_size_ = 512;
    std::size_t mini_sector_size_ = 64;
    std::uint32_t mini_cutoff_ = 4096;
    std::uint32_t dir_start_ = 0xFFFFFFFFu;
    std::uint32_t mini_fat_start_ = 0xFFFFFFFFu;
    std::uint32_t mini_fat_sectors_ = 0;
    std::uint32_t fat_sectors_ = 0;

    std::vector<std::uint32_t> fat_;
    std::vector<std::uint32_t> mini_fat_;
    std::vector<std::uint8_t> mini_stream_;
    std::vector<Entry> entries_;
    std::vector<std::string> warnings_;
};

/* Strip ASCII spaces from both sides and compare, component by component. */
bool cfb_path_matches(std::string_view lhs, std::string_view rhs);

}  // namespace mpp

#endif /* MPP_CFB_H */
