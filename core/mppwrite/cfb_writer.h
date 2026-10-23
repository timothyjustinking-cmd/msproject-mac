/*
 * cfb_writer.h -- read *and* re-emit a Compound File Binary (OLE2) container.
 *
 * WHY THIS EXISTS.  An .mpp file is a CFB v3 document.  The first writer
 * milestone is not "produce a task" but "reproduce the container": parse every
 * byte of the wrapper and emit it again unchanged.  If that holds for every
 * file in corpus/probe (and the two real projects), the container layer is
 * understood well enough to stand under the record writers, and nothing in it
 * has to be carried through as an opaque blob.
 *
 * WHAT THE MODEL KEEPS, AND WHY.
 *
 *   - Header fields are kept verbatim, including the 6 reserved bytes and the
 *     16-byte CLSID.  Version 3 files write 0 for `num_dir_sectors`; that 0 is
 *     preserved rather than "fixed".
 *   - SECTOR PLACEMENT IS DATA, NOT A RESULT.  Every chain (directory, mini
 *     FAT, mini stream, each regular stream) is stored as the explicit list of
 *     sector ids it occupies.  The FAT, the mini FAT, the DIFAT and the
 *     directory's start/size fields are then *derived* from those chains.  A
 *     round trip therefore proves the derivation, not a copy: if the FAT
 *     computation were wrong the bytes would differ.  It also means the writer
 *     can append sectors for a stream that grows without disturbing any other
 *     sector.
 *   - The mini stream is a heap with an allocation map.  Mini sectors are NOT
 *     handed out in directory order (verified: base.mpp's \x01CompObj sits at
 *     mini sector 29 while Props14 sits at 0), so the map is preserved as-is.
 *   - Tail padding is preserved: the bytes after a stream's last byte inside
 *     its final sector, the slack at the end of the mini stream, and the bytes
 *     after the last directory entry.  PROJECT_A has 198 non-zero padding bytes after
 *     Fixed2Data, so zero-filling them would break byte equality.
 *
 * WHAT IT DOES NOT DO (deliberate, see spec/mpp-writer.md):
 *
 *   - It does not add or remove directory entries, so it cannot create or
 *     delete streams; it can only replace a stream's payload and grow/shrink
 *     its allocation.  Adding a stream needs a new directory entry, a new red
 *     black tree link and (past 4 stores) a new directory sector; that is the
 *     next writer milestone, not this one.
 *   - It does not repair damage.  PROJECT_A's duplicate FixedMeta offsets and
 *     declared-vs-actual entry counts are carried through unchanged.
 *
 * FAT SLACK (measured on corpus/imported/sf248.mpp, wave 20 M6).  A file may
 * carry trailing sectors that no chain references: sf248 has 959 sectors and a
 * 768-entry FAT.  That is not damage -- every chain ends inside the FAT -- so
 * parse() records "N unreferenced sectors: ..." in warnings() and continues; a
 * chain that does point past the FAT is still fatal (chain_from() refuses the
 * lookup).  Such a file is READABLE but not RE-EMITTABLE byte-identically:
 * to_bytes() derives the FAT from the chains the model knows (the header's
 * sector-placement claim), so the slack sectors cannot be carried through.
 * to_bytes() therefore refuses such a document and check() reports it as "not
 * round-trippable", rather than silently emitting a different file.  Every file
 * this writer round-trips covers all its sectors; the round-trip tests feed only
 * those.
 *
 * NAMES.  MS Project right-aligns storage names in a 6-character field, so the
 * real path is "   114/TBkndTask/FixedData" (three leading spaces).  Lookups
 * compare each component with ASCII spaces stripped, like the reader does, so
 * callers write "114/TBkndTask/FixedData".
 */

#ifndef MPPWRITE_CFB_WRITER_H
#define MPPWRITE_CFB_WRITER_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mppwrite {

/* Every failure in this library is one of these. */
class Error : public std::runtime_error {
  public:
    explicit Error(std::string const& message) : std::runtime_error(message) {}
};

/* CFB sector marks, [MS-CFB] 2.2. */
inline constexpr std::uint32_t kMaxRegularSector = 0xFFFFFFFAu;
inline constexpr std::uint32_t kFatSector = 0xFFFFFFFDu;
inline constexpr std::uint32_t kDifatSector = 0xFFFFFFFCu;
inline constexpr std::uint32_t kEndOfChain = 0xFFFFFFFEu;
inline constexpr std::uint32_t kFreeSector = 0xFFFFFFFFu;
/* "This entry has no child / no sibling". */
inline constexpr std::uint32_t kNoStream = 0xFFFFFFFFu;

inline constexpr std::size_t kDirectoryEntrySize = 128;
inline constexpr std::size_t kHeaderSize = 512;
inline constexpr std::size_t kDifatHeaderSlots = 109;

/* One 128-byte directory entry, fully modelled.
 *
 * Every one of the 128 bytes is covered by a field below: 64 name bytes, the
 * u16 name length, type/colour, three sibling links, CLSID, state, two
 * timestamps, start sector and size.  There is no "unknown tail", which is why
 * re-emitting an untouched entry reproduces it exactly. */
struct DirectoryEntry {
    /* Raw name field: UTF-16LE, NUL-terminated, right-padded.  The padding is
     * not necessarily zero (every entry in every corpus file has non-zero
     * padding), so the whole 64-byte field is kept and written back verbatim. */
    std::array<std::uint8_t, 64> name_bytes{};

    std::uint16_t name_length = 0; /* bytes including the terminating NUL */
    std::uint8_t type = 0;         /* 0 empty, 1 storage, 2 stream, 5 root */
    std::uint8_t color = 0;        /* 0 red, 1 black */
    std::uint32_t left = kNoStream;
    std::uint32_t right = kNoStream;
    std::uint32_t child = kNoStream;
    std::array<std::uint8_t, 16> clsid{};
    std::uint32_t state = 0;
    std::uint64_t creation_time = 0;
    std::uint64_t modified_time = 0;
    std::uint32_t start = kEndOfChain; /* sector, or mini sector below cutoff */
    std::uint64_t size = 0;            /* payload bytes, v3: low 32 bits used */

    /* Full "/"-separated path with spaces stripped from each component, e.g.
     * "114/TBkndTask/FixedData".  Empty for the root entry.  Derived from the
     * tree, not stored in the file. */
    std::string path;

    /* Directory-tree parent, and the path with spaces intact.  Both are
     * derived from the tree; neither is stored per entry. */
    std::uint32_t parent = kNoStream;
    std::string stored_path;

    /* Decoded name exactly as stored (leading padding spaces included). */
    std::string name() const;
};

/* One stream: its payload plus the exact sectors it occupies. */
struct Stream {
    std::string path;                     /* normalised, see DirectoryEntry */
    std::string stored_path;              /* as stored, spaces intact */
    std::size_t entry = 0;                /* index into the directory */
    std::vector<std::uint32_t> chain;      /* regular sectors, in order */
    std::vector<std::uint32_t> mini_chain; /* mini sectors, in order */
    std::vector<std::uint8_t> data;        /* the payload, entry.size bytes */
    std::vector<std::uint8_t> tail;        /* padding after data in last sector */
};

class CfbDocument {
  public:
    /* An empty document: no bytes, no directory.  Provided so a document can be
     * a member or an array element; every accessor that needs a parsed file
     * throws Error until the object is assigned from from_file/from_bytes. */
    CfbDocument() = default;

    /* Read a whole file.  Throws Error if it is not a CFB this writer accepts. */
    static CfbDocument from_file(std::string const& path);
    static CfbDocument from_bytes(std::vector<std::uint8_t> bytes);

    /* --- inspection ---------------------------------------------------- */
    std::size_t entry_count() const { return entries_.size(); }
    std::size_t stream_count() const { return streams_.size(); }
    std::vector<std::string> stream_paths() const;
    std::vector<std::string> stored_stream_paths() const;
    bool has_stream(std::string_view path) const;
    std::vector<std::uint8_t> read_stream(std::string_view path) const;
    std::uint64_t stream_size(std::string_view path) const;

    /* The allocation table as read: one u32 per sector, holding either a next
     * sector id, kFatSector, kDifatSector, kFreeSector or kEndOfChain. */
    std::vector<std::uint32_t> const& fat() const { return fat_; }

    /*
     * Container anomalies that are recoverable, in the order they were found.
     * The one this parser reports is FAT slack: a file may carry sectors that no
     * chain references (MS Project writes such files; corpus/imported/sf248.mpp
     * has 959 sectors and a 768-entry FAT).  Every chain still ends inside the
     * FAT, so the read continues; a chain that points past the FAT is fatal and
     * throws Error.  See FAT SLACK in the file comment: a document with
     * warnings is readable but not byte-identically re-emittable.
     */
    std::vector<std::string> const& warnings() const { return warnings_; }

    /* Every sector id the document claims, sorted and unique.  Used by the
     * round-trip test to prove that nothing in the file is unexplained. */
    std::vector<std::uint32_t> sectors_used() const;

    std::uint32_t sector_size() const { return sector_size_; }
    std::uint32_t mini_sector_size() const { return mini_sector_size_; }
    std::uint32_t mini_cutoff() const { return mini_cutoff_; }
    std::uint16_t major_version() const { return major_version_; }
    std::uint16_t minor_version() const { return minor_version_; }
    std::size_t sector_count() const { return sector_count_parsed(); }
    std::size_t entry_slots() const { return entries_.size(); }

    /* Full file offset of the first byte of `sector`. */
    std::uint64_t sector_offset(std::uint32_t sector) const {
        return kHeaderSize + static_cast<std::uint64_t>(sector) * sector_size_;
    }

    /* Where each byte of a stream lives in the file, in stream order.  Ranges
     * are maximal within one sector.  This is the bridge the in-place patcher
     * uses; it is also the cheapest possible proof that the container model is
     * right, because a patched byte must land where the reader looks for it. */
    std::vector<std::pair<std::uint64_t, std::size_t>> stream_file_ranges(
        std::string_view path) const;

    /* --- editing (container level) ------------------------------------- */
    /*
     * Replace a stream's payload.  Allocation rules:
     *   - same sector count as the current chain: the existing sectors are
     *     reused, so only the payload bytes change;
     *   - otherwise the old sectors are released (they become free sectors
     *     inside the file, which is legal) and the payload is placed in fresh
     *     sectors appended at the end of the sector space.
     * Everything else in the container is left where it is.  Throws Error when
     * the path does not exist, or when it names the root entry.
     */
    void replace_stream(std::string_view path, std::vector<std::uint8_t> data);

    /* --- emit ----------------------------------------------------------- */
    /* The serialised file.  For a document that was parsed and never edited
     * this is byte-identical to the input; tests/roundtrip_cfb.cpp asserts it
     * for every file in corpus/probe plus the two real projects. */
    std::vector<std::uint8_t> to_bytes() const;
    void save(std::string const& path) const;

    /* True when nothing has modified the document since it was parsed. */
    bool unmodified() const { return !modified_; }

    /* The bytes it was parsed from (empty for a document built from scratch). */
    std::vector<std::uint8_t> const& source_bytes() const { return source_; }

    /*
     * Self-check: rebuild the FAT from the stored chains and compare it with
     * the FAT that was read, then verify that every sector in the file has
     * exactly one owner and that every stream's payload length matches its
     * directory entry.  Returns an empty string when everything agrees,
     * otherwise a human-readable description of the first inconsistency.
     * This is the automated form of the claim "the FAT is derivable".
     */
    std::string check() const;

  private:
    struct Layout {
        std::vector<std::uint32_t> fat;        /* fat_count * entries_per_sector */
        std::vector<std::uint32_t> fat_sids;
        std::vector<std::uint32_t> difat_sids;
        std::vector<std::uint32_t> minifat_sids;
        std::vector<std::uint32_t> minifat; /* minifat_sids * entries_per_sector */
        std::vector<std::uint32_t> root_chain;
        std::size_t sector_count = 0;
        std::size_t entries_per_sector = 0;
    };

    void parse();
    void build_paths();
    void read_chains();
    Layout layout() const;
    Stream* find_stream(std::string_view path);
    Stream const* find_stream(std::string_view path) const;
    std::vector<std::uint32_t> chain_from(std::vector<std::uint32_t> const& table,
                                          std::uint32_t start,
                                          std::string_view what) const;
    std::vector<std::uint8_t> gather(std::vector<std::uint32_t> const& chain,
                                     std::size_t size) const;
    std::vector<std::uint32_t> dir_chain() const;
    std::size_t sector_count_parsed() const;
    std::uint32_t next_free_sector() const;
    std::vector<std::uint32_t> fat_sids_all() const;

    /* header */
    std::vector<std::uint8_t> source_;
    std::uint16_t minor_version_ = 0;
    std::uint16_t major_version_ = 0;
    std::uint16_t byte_order_ = 0xFFFE;
    std::uint16_t sector_shift_ = 9;
    std::uint16_t mini_shift_ = 6;
    std::array<std::uint8_t, 6> reserved_{};
    std::array<std::uint8_t, 16> clsid_{};
    std::uint32_t num_dir_sectors_ = 0;
    std::uint32_t transaction_ = 0;
    std::uint32_t mini_cutoff_ = 4096;
    std::uint32_t dir_start_ = kEndOfChain;
    std::uint32_t minifat_start_ = kEndOfChain;
    std::uint32_t difat_start_ = kEndOfChain;
    std::uint32_t sector_size_ = 512;
    std::uint32_t mini_sector_size_ = 64;

    /* parsed structures, kept for validation and for placement */
    std::vector<std::uint32_t> fat_;        /* as read */
    std::vector<std::uint32_t> difat_slots_; /* FAT sector ids, DIFAT order */
    std::vector<std::uint32_t> difat_extra_; /* DIFAT continuation sectors */
    std::vector<std::uint32_t> minifat_chain_; /* sectors holding the mini FAT */
    std::vector<std::uint32_t> minifat_;       /* as read */
    std::vector<std::uint32_t> root_chain_;    /* sectors holding the mini stream */
    std::vector<std::uint8_t> mini_stream_;    /* mini stream bytes, root size */
    std::vector<std::uint8_t> mini_stream_tail_; /* bytes past root size */
    std::vector<std::uint8_t> dir_bytes_;      /* directory sectors, verbatim */
    std::vector<DirectoryEntry> entries_;
    std::vector<Stream> streams_;
    /* Recoverable container anomalies (currently only FAT slack), see
     * warnings().  Never fatal; a chain past the FAT throws from chain_from(). */
    std::vector<std::string> warnings_;

    bool modified_ = false;
};

/* Strip ASCII spaces from both sides and compare, component by component. */
bool cfb_path_matches(std::string_view lhs, std::string_view rhs);
/* "   114/TBkndTask/FixedData" -> "114/TBkndTask/FixedData". */
std::string normalise_path(std::string_view path);

} /* namespace mppwrite */

#endif /* MPPWRITE_CFB_WRITER_H */
