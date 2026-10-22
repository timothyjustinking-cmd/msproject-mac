/*
 * mpp_fixture_builder.h -- build a small, valid CFB (OLE2) container in memory.
 *
 * W40 needs malformed .mpp inputs without committing binary fixtures, so the
 * tests synthesise a container byte by byte.  The builder writes the regular
 * FAT path only (header mini-stream cutoff = 1, so every non-empty stream is a
 * regular sector chain); the reader supports that, and the real corpus covers
 * the mini-FAT path.
 *
 * Everything is little-endian and version 3 (512-byte sectors).  A directory
 * tree is built from the stream paths: "114/TBkndTask/FixedData" becomes the
 * storages 114 / TBkndTask and the stream FixedData, exactly the shape the
 * reader flattens.
 *
 * This is test infrastructure, not production code: it asserts nothing and is
 * only used by tests/mpp_*.cpp.
 */

#ifndef MPP_FIXTURE_BUILDER_H
#define MPP_FIXTURE_BUILDER_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace mpp_fixture {

constexpr std::size_t kSector = 512;
constexpr std::uint32_t kEndOfChain = 0xFFFFFFFEu;
constexpr std::uint32_t kFreeSector = 0xFFFFFFFFu;
constexpr std::uint32_t kFatSector = 0xFFFFFFFDu;
constexpr std::uint32_t kNoStream = 0xFFFFFFFFu;

struct Stream {
    std::string path;                 /* 'A/B/name' */
    std::vector<std::uint8_t> data;
    /* When non-zero, the directory entry claims this size while `data` holds the
     * bytes actually written.  Used to prove a lying size word cannot allocate. */
    std::uint64_t declared_size = 0;
};

inline void put_u16(std::vector<std::uint8_t>& out, std::size_t offset,
                    std::uint16_t value) {
    out[offset] = static_cast<std::uint8_t>(value & 0xFFu);
    out[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
}

inline void put_u32(std::vector<std::uint8_t>& out, std::size_t offset,
                    std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        out[offset + i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu);
    }
}

inline void put_u64(std::vector<std::uint8_t>& out, std::size_t offset,
                    std::uint64_t value) {
    for (std::size_t i = 0; i < 8; ++i) {
        out[offset + i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu);
    }
}

inline void put_f64(std::vector<std::uint8_t>& out, std::size_t offset,
                    double value) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    put_u64(out, offset, bits);
}

/* ---------------------------------------------------------------- geometry.
 * The reader's own numbers (core/mpp/reader.cpp): a Task FixedData row is 202
 * bytes after a 48-byte header; Task FixedMeta is 16 + 47*(rows+3). */
constexpr std::size_t kTaskHeader = 48;
constexpr std::size_t kTaskRow = 202;
constexpr std::size_t kTaskMetaHeader = 16;
constexpr std::size_t kTaskMetaEntry = 47;
constexpr std::size_t kTaskMetaDescriptors = 3;
constexpr std::size_t kVarMetaHeader = 24;
constexpr std::size_t kVarMetaEntry = 12;
constexpr std::uint32_t kMetaMagic = 0xFADFADBAu;

constexpr std::size_t task_row_offset(std::size_t row) {
    return kTaskHeader + row * kTaskRow;
}

/* A FixedData stream with `rows` zero rows (the caller fills fields). */
inline std::vector<std::uint8_t> task_fixed_data(std::size_t rows) {
    return std::vector<std::uint8_t>(kTaskHeader + rows * kTaskRow, 0);
}

/* A FixedMeta stream.  `declared` is the header's count word; the body always
 * holds `entries` entries of `entry` bytes.  Entry i's +4 word is
 * `first_offset + i*stride` for i >= descriptors (the reader ignores the three
 * 16-byte descriptor slots but still expects them for Task/Resource). */
inline std::vector<std::uint8_t> fixed_meta(std::size_t entries,
                                            std::size_t entry_size,
                                            std::size_t descriptors,
                                            std::uint32_t declared,
                                            std::uint32_t first_offset,
                                            std::uint32_t stride) {
    std::vector<std::uint8_t> meta(kTaskMetaHeader + entries * entry_size, 0);
    put_u32(meta, 0, kMetaMagic);
    put_u32(meta, 4, 0);
    put_u32(meta, 8, declared);
    put_u32(meta, 12, 0);
    for (std::size_t i = descriptors; i < entries; ++i) {
        const std::size_t base = kTaskMetaHeader + i * entry_size;
        put_u32(meta, base + 4, first_offset + static_cast<std::uint32_t>(i - descriptors) *
                                                     stride);
    }
    return meta;
}

/* Task FixedMeta for `rows` rows (descriptors include row 0, the project row). */
inline std::vector<std::uint8_t> task_meta(std::size_t rows,
                                           std::uint32_t declared) {
    return fixed_meta(rows + kTaskMetaDescriptors, kTaskMetaEntry,
                      kTaskMetaDescriptors, declared,
                      static_cast<std::uint32_t>(kTaskHeader),
                      static_cast<std::uint32_t>(kTaskRow));
}

/* VarMeta: 24-byte header + n 12-byte entries (uid, Var2Data offset, key). */
inline std::vector<std::uint8_t> var_meta(
    const std::vector<std::uint32_t>& entries /* uid, offset, key triples */,
    std::uint32_t declared_entries, std::uint32_t declared_var2_size) {
    std::vector<std::uint8_t> meta(kVarMetaHeader + entries.size() * 4, 0);
    put_u32(meta, 0, kMetaMagic);
    put_u32(meta, 8, declared_entries);
    put_u32(meta, 0x14, declared_var2_size);
    for (std::size_t i = 0; i + 2 < entries.size(); i += 3) {
        const std::size_t base = kVarMetaHeader + i / 3 * kVarMetaEntry;
        put_u32(meta, base, entries[i]);
        put_u32(meta, base + 4, entries[i + 1]);
        put_u32(meta, base + 8, entries[i + 2]);
    }
    return meta;
}

/* A Var2Data block: u32 byte length then payload. */
inline void append_block(std::vector<std::uint8_t>& var2,
                         const std::vector<std::uint8_t>& payload) {
    const std::uint32_t offset = static_cast<std::uint32_t>(var2.size());
    var2.resize(offset + 4 + payload.size());
    put_u32(var2, offset, static_cast<std::uint32_t>(payload.size()));
    std::copy(payload.begin(), payload.end(), var2.begin() + offset + 4);
}

/* --------------------------------------------------------------- CFB writer. */

namespace detail {

struct Node {
    std::string name;
    bool stream = false;
    std::vector<std::uint8_t> data;
    std::uint64_t declared_size = 0;
    std::vector<std::size_t> children;
};

inline void add_path(std::vector<Node>& nodes, const Stream& stream) {
    std::size_t parent = 0;
    std::size_t begin = 0;
    while (true) {
        const std::size_t slash = stream.path.find('/', begin);
        if (slash == std::string::npos) {
            Node leaf;
            leaf.name = stream.path.substr(begin);
            leaf.stream = true;
            leaf.data = stream.data;
            leaf.declared_size = stream.declared_size != 0
                                     ? stream.declared_size
                                     : stream.data.size();
            nodes.push_back(std::move(leaf));
            nodes[parent].children.push_back(nodes.size() - 1);
            return;
        }
        const std::string component = stream.path.substr(begin, slash - begin);
        std::size_t found = 0;
        for (const std::size_t child : nodes[parent].children) {
            if (!nodes[child].stream && nodes[child].name == component) {
                found = child;
                break;
            }
        }
        if (found == 0) {
            Node storage;
            storage.name = component;
            nodes.push_back(std::move(storage));
            found = nodes.size() - 1;
            nodes[parent].children.push_back(found);
        }
        parent = found;
        begin = slash + 1;
    }
}

inline void write_dir_entry(std::vector<std::uint8_t>& directory,
                            std::size_t entry, const std::string& name,
                            std::uint8_t type, std::uint32_t start,
                            std::uint64_t size, std::uint32_t child,
                            std::uint32_t right) {
    const std::size_t base = entry * 128;
    const std::size_t chars = std::min<std::size_t>(name.size(), 31);
    for (std::size_t i = 0; i < chars; ++i) {
        directory[base + i * 2] = static_cast<std::uint8_t>(name[i]);
        directory[base + i * 2 + 1] = 0;
    }
    put_u16(directory, base + 0x40,
            static_cast<std::uint16_t>((chars + 1) * 2));
    directory[base + 0x42] = type;
    directory[base + 0x43] = 1; /* black */
    put_u32(directory, base + 0x44, kNoStream);
    put_u32(directory, base + 0x48, right);
    put_u32(directory, base + 0x4C, child);
    put_u32(directory, base + 0x74, start);
    put_u64(directory, base + 0x78, size);
}

}  // namespace detail

/* Builds the whole container.  The first stream in `streams` becomes the first
 * child of the root; sibling order within a storage is insertion order. */
inline std::vector<std::uint8_t> make_cfb(const std::vector<Stream>& streams) {
    std::vector<detail::Node> nodes(1);
    nodes[0].name = "Root Entry";
    for (const Stream& stream : streams) {
        detail::add_path(nodes, stream);
    }

    const std::size_t dir_entries = nodes.size();
    const std::size_t dir_sectors = (dir_entries * 128 + kSector - 1) / kSector;

    struct Chain {
        std::uint32_t start = 0;
        std::uint32_t sectors = 0;
    };
    std::vector<Chain> chain(nodes.size());
    std::uint32_t next_sector = static_cast<std::uint32_t>(dir_sectors);
    std::vector<std::uint32_t> fat_entries;
    auto set_fat = [&fat_entries](std::size_t index, std::uint32_t value) {
        if (fat_entries.size() <= index) {
            fat_entries.resize(index + 1, kFreeSector);
        }
        fat_entries[index] = value;
    };

    for (std::size_t index = 0; index < nodes.size(); ++index) {
        if (!nodes[index].stream || nodes[index].data.empty()) {
            continue;
        }
        /* The chain follows the bytes actually written, never the declared
         * size: a test may make the directory claim more than the file holds. */
        const std::uint64_t sectors =
            (nodes[index].data.size() + kSector - 1) / kSector;
        chain[index].start = next_sector;
        chain[index].sectors = static_cast<std::uint32_t>(sectors);
        for (std::uint32_t i = 0; i < sectors; ++i) {
            set_fat(next_sector + i,
                    i + 1 == sectors ? kEndOfChain : next_sector + i + 1);
        }
        next_sector += static_cast<std::uint32_t>(sectors);
    }

    /* Directory chain. */
    for (std::size_t i = 0; i < dir_sectors; ++i) {
        set_fat(static_cast<std::size_t>(i),
                i + 1 == dir_sectors ? kEndOfChain
                                     : static_cast<std::uint32_t>(i + 1));
    }

    /* FAT sectors at the end.  Adding them can push the total past one FAT's
     * capacity, so iterate to a fixed point. */
    std::uint32_t total = next_sector;
    while (true) {
        const std::uint32_t fat_sectors =
            (total + static_cast<std::uint32_t>(kSector / 4) - 1) /
            static_cast<std::uint32_t>(kSector / 4);
        if (next_sector + fat_sectors == total) {
            break;
        }
        total = next_sector + fat_sectors;
    }
    const std::uint32_t fat_sectors = total - next_sector;
    if (fat_sectors > 109) {
        return {}; /* the header has 109 DIFAT slots; not needed by these tests */
    }
    for (std::uint32_t i = 0; i < fat_sectors; ++i) {
        set_fat(next_sector + i, kFatSector);
    }
    fat_entries.resize(static_cast<std::size_t>(total) * 4, kFreeSector);

    /* Directory bytes. */
    std::vector<std::uint8_t> directory(dir_sectors * kSector, 0);
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        const detail::Node& node = nodes[index];
        const std::uint32_t child =
            node.children.empty() ? kNoStream
                                  : static_cast<std::uint32_t>(node.children.front());
        const std::uint8_t type = index == 0 ? 5 : (node.stream ? 2 : 1);
        const std::uint32_t start =
            node.stream && !node.data.empty() ? chain[index].start : kEndOfChain;
        const std::uint64_t size = node.stream ? node.declared_size : 0;
        detail::write_dir_entry(directory, index, node.name, type, start, size,
                                child, kNoStream);
    }
    /* Fill each child's right pointer to the next sibling. */
    for (const detail::Node& node : nodes) {
        for (std::size_t i = 0; i + 1 < node.children.size(); ++i) {
            const std::size_t child = node.children[i];
            const std::uint32_t next =
                static_cast<std::uint32_t>(node.children[i + 1]);
            put_u32(directory, child * 128 + 0x48, next);
        }
    }

    /* Header. */
    std::vector<std::uint8_t> file(kSector, 0);
    const std::uint8_t signature[8] = {0xD0, 0xCF, 0x11, 0xE0,
                                       0xA1, 0xB1, 0x1A, 0xE1};
    std::copy(signature, signature + 8, file.begin());
    put_u16(file, 0x18, 0x003E);       /* minor version */
    put_u16(file, 0x1A, 3);            /* major version */
    put_u16(file, 0x1C, 0xFFFE);       /* byte order */
    put_u16(file, 0x1E, 9);            /* sector shift: 512 */
    put_u16(file, 0x20, 6);            /* mini sector shift */
    put_u32(file, 0x28, 0);            /* directory sectors (v3) */
    put_u32(file, 0x2C, fat_sectors);
    put_u32(file, 0x30, 0);            /* first directory sector */
    put_u32(file, 0x38, 1);            /* mini stream cutoff: force regular */
    put_u32(file, 0x3C, kEndOfChain);  /* first mini FAT */
    put_u32(file, 0x40, 0);            /* mini FAT sectors */
    put_u32(file, 0x44, kEndOfChain);  /* first DIFAT */
    put_u32(file, 0x48, 0);            /* DIFAT sectors */
    for (std::size_t i = 0; i < 109; ++i) {
        put_u32(file, 0x4C + i * 4,
                i < fat_sectors ? next_sector + static_cast<std::uint32_t>(i)
                                : kFreeSector);
    }

    file.resize(kSector + static_cast<std::size_t>(total) * kSector, 0);
    std::copy(directory.begin(), directory.end(), file.begin() + kSector);
    for (std::size_t index = 1; index < nodes.size(); ++index) {
        if (!nodes[index].stream || nodes[index].declared_size == 0) {
            continue;
        }
        const std::size_t offset =
            kSector + static_cast<std::size_t>(chain[index].start) * kSector;
        std::copy(nodes[index].data.begin(), nodes[index].data.end(),
                  file.begin() + static_cast<std::ptrdiff_t>(offset));
    }
    for (std::uint32_t i = 0; i < fat_sectors; ++i) {
        const std::size_t offset =
            kSector + static_cast<std::size_t>(next_sector + i) * kSector;
        for (std::size_t j = 0; j < kSector / 4; ++j) {
            put_u32(file, offset + j * 4,
                    fat_entries[static_cast<std::size_t>(i) * (kSector / 4) + j]);
        }
    }
    return file;
}

}  // namespace mpp_fixture

#endif /* MPP_FIXTURE_BUILDER_H */
