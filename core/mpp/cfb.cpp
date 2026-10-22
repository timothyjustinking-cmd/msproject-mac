/*
 * cfb.cpp -- read-only Compound File Binary (OLE2) container.
 *
 * Layout references: [MS-CFB].  Everything below was verified against the two
 * corpus files (sector size 512, mini sector 64, cutoff 4096) and against the
 * synthetic fixture built by tests/mpp_synthetic_fixture.py, which exercises
 * both the FAT and the mini FAT path.
 */

#include "cfb.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <utility>

#include "utf16.h"

namespace mpp {
namespace {

constexpr std::uint8_t kSignature[8] = {0xD0, 0xCF, 0x11, 0xE0,
                                        0xA1, 0xB1, 0x1A, 0xE1};
constexpr std::uint32_t kMaxRegularSector = 0xFFFFFFFAu;  /* ids below are special */
constexpr std::uint32_t kEndOfChain = 0xFFFFFFFEu;
constexpr std::uint32_t kFreeSector = 0xFFFFFFFFu;
constexpr std::uint32_t kNoStream = 0xFFFFFFFFu;
constexpr std::size_t kDirectoryEntrySize = 128;
constexpr std::size_t kHeaderSize = 512;
constexpr std::size_t kDifatHeaderSlots = 109;

std::uint16_t read_u16(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return static_cast<std::uint16_t>(data[offset]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[offset + 1]) << 8);
}

std::uint32_t read_u32(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

std::uint64_t read_u64(const std::vector<std::uint8_t>& data, std::size_t offset) {
    const std::uint64_t low = read_u32(data, offset);
    const std::uint64_t high = read_u32(data, offset + 4);
    return low | (high << 32);
}

std::string trim_ascii_space(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t')) {
        ++begin;
    }
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t')) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

}  // namespace

bool cfb_path_matches(std::string_view lhs, std::string_view rhs) {
    std::size_t li = 0;
    std::size_t ri = 0;
    while (true) {
        const std::size_t lnext = lhs.find('/', li);
        const std::size_t rnext = rhs.find('/', ri);
        const std::string_view lcomp =
            lhs.substr(li, lnext == std::string_view::npos ? std::string_view::npos
                                                           : lnext - li);
        const std::string_view rcomp =
            rhs.substr(ri, rnext == std::string_view::npos ? std::string_view::npos
                                                           : rnext - ri);
        if (trim_ascii_space(lcomp) != trim_ascii_space(rcomp)) {
            return false;
        }
        const bool ldone = lnext == std::string_view::npos;
        const bool rdone = rnext == std::string_view::npos;
        if (ldone || rdone) {
            return ldone && rdone;
        }
        li = lnext + 1;
        ri = rnext + 1;
    }
}

CompoundFile CompoundFile::from_file(std::string const& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw Error("cannot open '" + path + "'");
    }
    input.seekg(0, std::ios::end);
    const std::streamoff length = input.tellg();
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
    try {
        return from_bytes(std::move(bytes));
    } catch (Error const& error) {
        throw Error("'" + path + "': " + error.what());
    }
}

CompoundFile CompoundFile::from_bytes(std::vector<std::uint8_t> bytes) {
    CompoundFile file;
    file.data_ = std::move(bytes);
    file.parse();
    return file;
}

std::size_t CompoundFile::sector_offset(std::uint32_t sector) const {
    /* Sector 0 begins one header (== one sector) into the file. */
    const std::uint64_t offset =
        (static_cast<std::uint64_t>(sector) + 1u) * sector_size_;
    if (offset >= data_.size()) {
        throw Error("sector " + std::to_string(sector) + " lies past the end of the file");
    }
    return static_cast<std::size_t>(offset);
}

std::uint32_t CompoundFile::fat_next(std::uint32_t sector) const {
    if (sector >= fat_.size()) {
        throw Error("FAT lookup for sector " + std::to_string(sector) +
                    " is out of range (FAT holds " + std::to_string(fat_.size()) +
                    " entries)");
    }
    return fat_[sector];
}

std::uint32_t CompoundFile::mini_fat_next(std::uint32_t sector) const {
    if (sector >= mini_fat_.size()) {
        throw Error("mini FAT lookup for sector " + std::to_string(sector) +
                    " is out of range (mini FAT holds " +
                    std::to_string(mini_fat_.size()) + " entries)");
    }
    return mini_fat_[sector];
}

std::vector<std::uint8_t> CompoundFile::read_regular_chain(std::uint32_t start,
                                                           std::uint64_t size) const {
    std::vector<std::uint8_t> out;
    if (size == 0 || start == kEndOfChain || start == kFreeSector) {
        return out;
    }
    if (size > data_.size()) {
        throw Error("stream claims " + std::to_string(size) +
                    " bytes, more than the whole file");
    }
    out.reserve(static_cast<std::size_t>(size));
    std::uint32_t current = start;
    /* One FAT entry per sector is the hard upper bound on the chain length. */
    const std::size_t limit = fat_.size() + 8;
    for (std::size_t step = 0; step < limit && out.size() < size; ++step) {
        if (current >= kMaxRegularSector) {
            break;
        }
        const std::size_t offset = sector_offset(current);
        const std::uint64_t remaining = size - out.size();
        const std::size_t take = static_cast<std::size_t>(
            std::min<std::uint64_t>(remaining, sector_size_));
        if (offset + take > data_.size()) {
            break;
        }
        out.insert(out.end(), data_.begin() + static_cast<std::ptrdiff_t>(offset),
                   data_.begin() + static_cast<std::ptrdiff_t>(offset + take));
        if (out.size() >= size) {
            break;
        }
        current = fat_next(current);
    }
    if (out.size() != size) {
        throw Error("sector chain is truncated: read " + std::to_string(out.size()) +
                    " of " + std::to_string(size) + " bytes");
    }
    return out;
}

std::vector<std::uint8_t> CompoundFile::read_mini_chain(std::uint32_t start,
                                                        std::uint64_t size) const {
    std::vector<std::uint8_t> out;
    if (size == 0 || start == kEndOfChain || start == kFreeSector) {
        return out;
    }
    if (size > mini_stream_.size()) {
        throw Error("mini stream claims " + std::to_string(size) +
                    " bytes, more than the " + std::to_string(mini_stream_.size()) +
                    "-byte mini stream");
    }
    out.reserve(static_cast<std::size_t>(size));
    std::uint32_t current = start;
    const std::size_t limit = mini_fat_.size() + 8;
    for (std::size_t step = 0; step < limit && out.size() < size; ++step) {
        if (current >= kMaxRegularSector) {
            break;
        }
        const std::uint64_t offset =
            static_cast<std::uint64_t>(current) * mini_sector_size_;
        if (offset >= mini_stream_.size()) {
            break;
        }
        const std::uint64_t remaining = size - out.size();
        const std::size_t take = static_cast<std::size_t>(
            std::min<std::uint64_t>(remaining, mini_sector_size_));
        const std::size_t offset_size = static_cast<std::size_t>(offset);
        const std::size_t available = mini_stream_.size() - offset_size;
        const std::size_t copy = std::min(take, available);
        out.insert(out.end(),
                   mini_stream_.begin() + static_cast<std::ptrdiff_t>(offset_size),
                   mini_stream_.begin() +
                       static_cast<std::ptrdiff_t>(offset_size + copy));
        if (out.size() < size) {
            current = mini_fat_next(current);
        }
    }
    if (out.size() != size) {
        throw Error("mini sector chain is truncated: read " +
                    std::to_string(out.size()) + " of " + std::to_string(size) +
                    " bytes");
    }
    return out;
}

void CompoundFile::parse() {
    if (data_.size() < kHeaderSize) {
        throw Error("file is " + std::to_string(data_.size()) +
                    " bytes, too short for a compound file header");
    }
    if (!std::equal(std::begin(kSignature), std::end(kSignature), data_.begin())) {
        throw Error("not a compound file (bad signature)");
    }

    const std::uint16_t major_version = read_u16(data_, 0x1A);
    const std::uint16_t sector_shift = read_u16(data_, 0x1E);
    const std::uint16_t mini_sector_shift = read_u16(data_, 0x20);
    if (major_version != 3 && major_version != 4) {
        throw Error("unsupported compound file version " +
                    std::to_string(major_version) + " (only 3 and 4 exist)");
    }
    if (sector_shift < 9 || sector_shift > 16) {
        throw Error("implausible sector shift " + std::to_string(sector_shift));
    }
    sector_size_ = static_cast<std::size_t>(1) << sector_shift;
    if (mini_sector_shift != 6 && mini_sector_shift != 5) {
        throw Error("unsupported mini sector shift " + std::to_string(mini_sector_shift));
    }
    mini_sector_size_ = static_cast<std::size_t>(1) << mini_sector_shift;
    mini_cutoff_ = read_u32(data_, 0x38);
    if (mini_cutoff_ == 0) {
        mini_cutoff_ = 4096;
    }
    if (data_.size() < sector_size_) {
        throw Error("file is smaller than one sector");
    }

    /* [MS-CFB] 2.2: 0x2C FAT sector count, 0x30 first directory sector,
     * 0x38 mini stream cutoff, 0x3C first mini FAT sector, 0x40 mini FAT sector
     * count, 0x44 first DIFAT sector, 0x48 DIFAT sector count.  Getting 0x3C
     * and 0x40 wrong reads the DIFAT terminator (0xfffffffe) as a sector count
     * and asks for 2.2 TB of mini FAT. */
    fat_sectors_ = read_u32(data_, 0x2C);
    dir_start_ = read_u32(data_, 0x30);
    mini_fat_start_ = read_u32(data_, 0x3C);
    mini_fat_sectors_ = read_u32(data_, 0x40);
    const std::uint32_t difat_start = read_u32(data_, 0x44);
    const std::uint32_t difat_sectors = read_u32(data_, 0x48);

    /* ---- FAT sector ids: 109 in the header, then a DIFAT chain if needed. */
    std::vector<std::uint32_t> fat_sector_ids;
    for (std::size_t i = 0; i < kDifatHeaderSlots; ++i) {
        const std::uint32_t id = read_u32(data_, 0x4C + i * 4);
        if (id < kMaxRegularSector) {
            fat_sector_ids.push_back(id);
        }
    }
    if (difat_start < kMaxRegularSector && difat_sectors > 0) {
        std::uint32_t current = difat_start;
        const std::size_t per_sector = sector_size_ / 4;
        for (std::uint32_t step = 0; step < difat_sectors && current < kMaxRegularSector;
             ++step) {
            const std::size_t offset = sector_offset(current);
            if (offset + per_sector * 4 > data_.size()) {
                break;
            }
            for (std::size_t i = 0; i + 1 < per_sector; ++i) {
                const std::uint32_t id = read_u32(data_, offset + i * 4);
                if (id < kMaxRegularSector) {
                    fat_sector_ids.push_back(id);
                }
            }
            current = read_u32(data_, offset + (per_sector - 1) * 4);
        }
    }
    if (fat_sectors_ != 0 && fat_sectors_ < fat_sector_ids.size()) {
        fat_sector_ids.resize(fat_sectors_);
    }
    if (fat_sector_ids.empty()) {
        throw Error("compound file has no FAT sectors");
    }

    fat_.reserve(fat_sector_ids.size() * (sector_size_ / 4));
    for (std::uint32_t id : fat_sector_ids) {
        const std::size_t offset = sector_offset(id);
        if (offset + sector_size_ > data_.size()) {
            throw Error("FAT sector " + std::to_string(id) + " lies past the end of the file");
        }
        for (std::size_t i = 0; i < sector_size_ / 4; ++i) {
            fat_.push_back(read_u32(data_, offset + i * 4));
        }
    }

    /* ---- Directory: a regular sector chain starting at dir_start_. */
    std::vector<std::uint8_t> directory;
    {
        std::uint32_t current = dir_start_;
        const std::size_t limit = fat_.size() + 8;
        for (std::size_t step = 0; step < limit; ++step) {
            if (current >= kMaxRegularSector) {
                break;
            }
            const std::size_t offset = sector_offset(current);
            if (offset + sector_size_ > data_.size()) {
                throw Error("directory sector lies past the end of the file");
            }
            directory.insert(directory.end(),
                             data_.begin() + static_cast<std::ptrdiff_t>(offset),
                             data_.begin() +
                                 static_cast<std::ptrdiff_t>(offset + sector_size_));
            current = fat_next(current);
        }
    }
    if (directory.size() < kDirectoryEntrySize) {
        throw Error("compound file directory is empty");
    }

    struct RawEntry {
        std::string name;
        std::uint8_t type = 0;
        std::uint32_t left = kNoStream;
        std::uint32_t right = kNoStream;
        std::uint32_t child = kNoStream;
        std::uint32_t start = kEndOfChain;
        std::uint64_t size = 0;
    };
    std::vector<RawEntry> raw;
    {
        const std::size_t count = directory.size() / kDirectoryEntrySize;
        raw.resize(count);
        for (std::size_t i = 0; i < count; ++i) {
            const std::size_t base = i * kDirectoryEntrySize;
            RawEntry& entry = raw[i];
            const std::uint16_t name_bytes = read_u16(directory, base + 0x40);
            if (name_bytes >= 2 && name_bytes <= 64) {
                entry.name = utf16le_to_utf8(directory.data() + base,
                                            static_cast<std::size_t>(name_bytes) - 2);
            }
            entry.type = directory[base + 0x42];
            entry.left = read_u32(directory, base + 0x44);
            entry.right = read_u32(directory, base + 0x48);
            entry.child = read_u32(directory, base + 0x4C);
            entry.start = read_u32(directory, base + 0x74);
            entry.size = major_version == 3
                             ? static_cast<std::uint64_t>(read_u32(directory, base + 0x78))
                             : read_u64(directory, base + 0x78);
        }
    }

    std::size_t root_index = raw.size();
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i].type == 5) {
            root_index = i;
            break;
        }
    }
    if (root_index == raw.size()) {
        throw Error("compound file has no root directory entry");
    }

    /* ---- Flatten the directory tree into "/"-separated paths. */
    std::vector<bool> seen(raw.size(), false);
    std::vector<std::pair<std::uint32_t, std::string>> pending;
    if (raw[root_index].child < raw.size()) {
        pending.emplace_back(raw[root_index].child, std::string());
    }
    while (!pending.empty()) {
        const std::pair<std::uint32_t, std::string> item = pending.back();
        pending.pop_back();

        std::vector<std::uint32_t> siblings;
        std::vector<std::uint32_t> stack;
        stack.push_back(item.first);
        while (!stack.empty()) {
            const std::uint32_t index = stack.back();
            stack.pop_back();
            if (index >= raw.size() || seen[index]) {
                continue;
            }
            if (raw[index].type != 1 && raw[index].type != 2) {
                continue;
            }
            seen[index] = true;
            siblings.push_back(index);
            if (raw[index].left < raw.size()) {
                stack.push_back(raw[index].left);
            }
            if (raw[index].right < raw.size()) {
                stack.push_back(raw[index].right);
            }
        }
        for (std::uint32_t index : siblings) {
            const RawEntry& source = raw[index];
            std::string path = item.second;
            if (!path.empty()) {
                path.push_back('/');
            }
            path += source.name;
            Entry entry;
            entry.path = path;
            entry.type = source.type;
            entry.start = source.start;
            entry.size = source.size;
            entries_.push_back(std::move(entry));
            if (source.type == 1 && source.child < raw.size()) {
                pending.emplace_back(source.child, entries_.back().path);
            }
        }
    }

    /* ---- Mini FAT: regular chain, read as a u32 array. */
    if (mini_fat_sectors_ > 0 && mini_fat_start_ < kMaxRegularSector) {
        const std::uint64_t bytes =
            static_cast<std::uint64_t>(mini_fat_sectors_) * sector_size_;
        const std::vector<std::uint8_t> chain =
            read_regular_chain(mini_fat_start_, bytes);
        mini_fat_.reserve(chain.size() / 4);
        for (std::size_t i = 0; i + 3 < chain.size(); i += 4) {
            mini_fat_.push_back(static_cast<std::uint32_t>(chain[i]) |
                                (static_cast<std::uint32_t>(chain[i + 1]) << 8) |
                                (static_cast<std::uint32_t>(chain[i + 2]) << 16) |
                                (static_cast<std::uint32_t>(chain[i + 3]) << 24));
        }
    }

    /* ---- The mini stream itself is the root entry's chain. */
    const RawEntry& root = raw[root_index];
    if (root.size > 0 && root.start < kMaxRegularSector) {
        mini_stream_ = read_regular_chain(root.start, root.size);
    }
}

bool CompoundFile::has_stream(std::string_view path) const {
    return find(path) != nullptr;
}

std::uint64_t CompoundFile::stream_size(std::string_view path) const {
    const Entry* entry = find(path);
    if (entry == nullptr) {
        throw Error("no stream '" + std::string(path) + "' in compound file");
    }
    return entry->size;
}

std::vector<std::uint8_t> CompoundFile::read_stream(std::string_view path) const {
    const Entry* entry = find(path);
    if (entry == nullptr) {
        throw Error("no stream '" + std::string(path) + "' in compound file");
    }
    if (entry->size == 0) {
        return {};
    }
    try {
        if (entry->size < mini_cutoff_) {
            return read_mini_chain(entry->start, entry->size);
        }
        return read_regular_chain(entry->start, entry->size);
    } catch (Error const& error) {
        throw Error("stream '" + entry->path + "' is unreadable: " + error.what());
    }
}

std::vector<std::string> CompoundFile::stream_paths() const {
    std::vector<std::string> paths;
    for (const Entry& entry : entries_) {
        if (entry.type == 2) {
            paths.push_back(entry.path);
        }
    }
    return paths;
}

const CompoundFile::Entry* CompoundFile::find(std::string_view path) const {
    for (const Entry& entry : entries_) {
        if (entry.type == 2 && cfb_path_matches(entry.path, path)) {
            return &entry;
        }
    }
    return nullptr;
}

}  // namespace mpp
