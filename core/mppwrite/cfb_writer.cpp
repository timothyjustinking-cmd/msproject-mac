/*
 * cfb_writer.cpp -- CFB parse + re-emit, and stream replacement.
 *
 * The layout claims this file relies on are measured, not assumed; each one is
 * re-checked by docs/analysis/writer/cfb_assumptions.py over the whole corpus
 * and by tests/roundtrip_cfb.cpp over every file in corpus/probe:
 *
 *   - every sector of a file this writer RE-EMITS is reachable from a chain (no
 *     orphans), so a sector never has to be invented.  Files that carry
 *     unreferenced trailing sectors exist (MS Project writes them: sf248 has 768
 *     FAT entries for 959 sectors); those are readable with a warning but are
 *     not round-tripped through to_bytes(), whose FAT is derived from the
 *     chains.  A chain that walks past the FAT is fatal;
 *   - the FAT is exactly the chain layout plus FATSECT/DIFSECT marks, so it can
 *     be derived instead of copied;
 *   - the mini FAT is exactly the mini chains;
 *   - directory sectors hold ceil(entries / 8) entries in index order;
 *   - free directory slots are 16 x 00 then 0xFFFFFFFF x3 and nothing else;
 *   - the bytes after a stream's last byte are zero EXCEPT in PROJECT_A's Fixed2Data
 *     (198 non-zero bytes), which is why padding is carried through;
 *   - there is no trailing data after the last sector.
 */

#include "cfb_writer.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>

namespace mppwrite {
namespace {

constexpr std::uint8_t kSignature[8] = {0xD0, 0xCF, 0x11, 0xE0,
                                        0xA1, 0xB1, 0x1A, 0xE1};

/* --- little-endian access ------------------------------------------------- */

std::uint16_t read_u16(std::vector<std::uint8_t> const& b, std::size_t o) {
    return static_cast<std::uint16_t>(b[o] | (b[o + 1] << 8));
}

std::uint32_t read_u32(std::vector<std::uint8_t> const& b, std::size_t o) {
    return static_cast<std::uint32_t>(b[o]) |
           (static_cast<std::uint32_t>(b[o + 1]) << 8) |
           (static_cast<std::uint32_t>(b[o + 2]) << 16) |
           (static_cast<std::uint32_t>(b[o + 3]) << 24);
}

std::uint64_t read_u64(std::vector<std::uint8_t> const& b, std::size_t o) {
    return static_cast<std::uint64_t>(read_u32(b, o)) |
           (static_cast<std::uint64_t>(read_u32(b, o + 4)) << 32);
}

void write_u16(std::uint8_t* p, std::uint16_t v) {
    p[0] = static_cast<std::uint8_t>(v & 0xFFu);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
}

void write_u32(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v & 0xFFu);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
    p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
    p[3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
}

void write_u64(std::uint8_t* p, std::uint64_t v) {
    write_u32(p, static_cast<std::uint32_t>(v & 0xFFFFFFFFu));
    write_u32(p + 4, static_cast<std::uint32_t>(v >> 32));
}

/* --- text ---------------------------------------------------------------- */

bool is_space(char c) { return c == ' ' || c == '\t'; }

std::string trim_spaces(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && is_space(text[begin])) {
        ++begin;
    }
    while (end > begin && is_space(text[end - 1])) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

/* UTF-16LE -> UTF-8.  Directory names are BMP-only in every file examined
 * (ASCII plus the 0x01/0x05 control prefixes MS uses), so surrogate pairs are
 * decoded only as far as the BMP conversion needs. */
std::string utf16le_to_utf8(std::vector<std::uint8_t> const& raw,
                            std::size_t max_bytes) {
    std::string out;
    std::size_t const limit = std::min(max_bytes, raw.size());
    for (std::size_t i = 0; i + 1 < limit; i += 2) {
        std::uint32_t unit = static_cast<std::uint32_t>(raw[i]) |
                             (static_cast<std::uint32_t>(raw[i + 1]) << 8);
        if (unit == 0) {
            break;
        }
        if (unit >= 0xD800 && unit <= 0xDBFF && i + 3 < limit) {
            std::uint32_t low = static_cast<std::uint32_t>(raw[i + 2]) |
                                (static_cast<std::uint32_t>(raw[i + 3]) << 8);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
                i += 2;
            }
        }
        if (unit < 0x80) {
            out.push_back(static_cast<char>(unit));
        } else if (unit < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (unit >> 6)));
            out.push_back(static_cast<char>(0x80 | (unit & 0x3F)));
        } else if (unit < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (unit >> 12)));
            out.push_back(static_cast<char>(0x80 | ((unit >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (unit & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (unit >> 18)));
            out.push_back(static_cast<char>(0x80 | ((unit >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((unit >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (unit & 0x3F)));
        }
    }
    return out;
}

/* UTF-8 -> UTF-16LE into a 64-byte name field; returns the name length in
 * bytes including the terminating NUL (as the field stores it). */
[[maybe_unused]] std::uint16_t utf8_to_utf16le(std::string_view text,
                              std::array<std::uint8_t, 64>& field) {
    std::vector<std::uint16_t> units;
    for (std::size_t i = 0; i < text.size();) {
        unsigned char const c = static_cast<unsigned char>(text[i]);
        std::uint32_t cp = 0;
        std::size_t extra = 0;
        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xE0) == 0xC0) {
            cp = c & 0x1Fu;
            extra = 1;
        } else if ((c & 0xF0) == 0xE0) {
            cp = c & 0x0Fu;
            extra = 2;
        } else {
            cp = c & 0x07u;
            extra = 3;
        }
        ++i;
        for (std::size_t k = 0; k < extra && i < text.size(); ++k, ++i) {
            cp = (cp << 6) | (static_cast<unsigned char>(text[i]) & 0x3Fu);
        }
        if (cp < 0x10000) {
            units.push_back(static_cast<std::uint16_t>(cp));
        } else {
            cp -= 0x10000;
            units.push_back(static_cast<std::uint16_t>(0xD800 + (cp >> 10)));
            units.push_back(static_cast<std::uint16_t>(0xDC00 + (cp & 0x3FF)));
        }
    }
    field.fill(0);
    std::size_t const max_units = 31; /* 64 bytes minus the NUL */
    std::size_t const n = std::min(units.size(), max_units);
    for (std::size_t i = 0; i < n; ++i) {
        field[i * 2] = static_cast<std::uint8_t>(units[i] & 0xFFu);
        field[i * 2 + 1] = static_cast<std::uint8_t>(units[i] >> 8);
    }
    return static_cast<std::uint16_t>((n + 1) * 2);
}

/* --- raw buffer helpers -------------------------------------------------- */

[[maybe_unused]] void put_u32(std::vector<std::uint8_t>& b, std::size_t o, std::uint32_t v) {
    b[o] = static_cast<std::uint8_t>(v & 0xFFu);
    b[o + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
    b[o + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
    b[o + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
}

std::string hex(std::uint64_t v) {
    static char const digits[] = "0123456789abcdef";
    std::string out;
    do {
        out.insert(out.begin(), digits[v & 0xFu]);
        v >>= 4;
    } while (v != 0);
    return "0x" + out;
}

} /* namespace */

/* --- free functions ------------------------------------------------------ */

bool cfb_path_matches(std::string_view lhs, std::string_view rhs) {
    std::size_t li = 0;
    std::size_t ri = 0;
    while (true) {
        std::size_t const lnext = lhs.find('/', li);
        std::size_t const rnext = rhs.find('/', ri);
        std::string_view const lcomp =
            lhs.substr(li, lnext == std::string_view::npos ? std::string_view::npos
                                                           : lnext - li);
        std::string_view const rcomp =
            rhs.substr(ri, rnext == std::string_view::npos ? std::string_view::npos
                                                           : rnext - ri);
        if (trim_spaces(lcomp) != trim_spaces(rcomp)) {
            return false;
        }
        bool const ldone = lnext == std::string_view::npos;
        bool const rdone = rnext == std::string_view::npos;
        if (ldone || rdone) {
            return ldone && rdone;
        }
        li = lnext + 1;
        ri = rnext + 1;
    }
}

std::string normalise_path(std::string_view path) {
    std::string out;
    std::size_t i = 0;
    bool first = true;
    while (i <= path.size()) {
        std::size_t const next = path.find('/', i);
        std::string_view const part =
            path.substr(i, next == std::string_view::npos ? std::string_view::npos
                                                          : next - i);
        if (!first) {
            out.push_back('/');
        }
        out += trim_spaces(part);
        first = false;
        if (next == std::string_view::npos) {
            break;
        }
        i = next + 1;
    }
    return out;
}

std::string DirectoryEntry::name() const {
    if (name_length == 0) {
        return {};
    }
    std::vector<std::uint8_t> const field(name_bytes.begin(), name_bytes.end());
    std::size_t const bytes =
        name_length <= 64 ? static_cast<std::size_t>(name_length) : 64;
    return utf16le_to_utf8(field, bytes >= 2 ? bytes - 2 : 0);
}

/* --- parse --------------------------------------------------------------- */

CfbDocument CfbDocument::from_file(std::string const& path) {
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
    try {
        return from_bytes(std::move(bytes));
    } catch (Error const& e) {
        throw Error("'" + path + "': " + e.what());
    }
}

CfbDocument CfbDocument::from_bytes(std::vector<std::uint8_t> bytes) {
    CfbDocument doc;
    doc.source_ = std::move(bytes);
    doc.parse();
    return doc;
}

void CfbDocument::parse() {
    std::vector<std::uint8_t> const& b = source_;
    warnings_.clear();
    if (b.size() < kHeaderSize) {
        throw Error("smaller than one CFB header");
    }
    if (!std::equal(std::begin(kSignature), std::end(kSignature), b.begin())) {
        throw Error("bad CFB signature");
    }
    minor_version_ = read_u16(b, 0x18);
    major_version_ = read_u16(b, 0x1A);
    byte_order_ = read_u16(b, 0x1C);
    sector_shift_ = read_u16(b, 0x1E);
    mini_shift_ = read_u16(b, 0x20);
    if (byte_order_ != 0xFFFEu) {
        throw Error("big-endian CFB is not supported");
    }
    if (major_version_ != 3 && major_version_ != 4) {
        throw Error("unsupported CFB major version " +
                    std::to_string(major_version_));
    }
    if (sector_shift_ != 9 && sector_shift_ != 12) {
        throw Error("unsupported sector shift " + std::to_string(sector_shift_));
    }
    if (mini_shift_ != 6) {
        throw Error("unsupported mini sector shift " + std::to_string(mini_shift_));
    }
    sector_size_ = 1u << sector_shift_;
    mini_sector_size_ = 1u << mini_shift_;
    std::copy_n(b.begin() + 8, 16, clsid_.begin());
    std::copy_n(b.begin() + 0x22, 6, reserved_.begin());

    num_dir_sectors_ = read_u32(b, 0x28);
    std::uint32_t const num_fat = read_u32(b, 0x2C);
    dir_start_ = read_u32(b, 0x30);
    transaction_ = read_u32(b, 0x34);
    mini_cutoff_ = read_u32(b, 0x38);
    minifat_start_ = read_u32(b, 0x3C);
    difat_start_ = read_u32(b, 0x44);
    std::uint32_t const num_difat = read_u32(b, 0x48);

    if ((b.size() - kHeaderSize) % sector_size_ != 0) {
        throw Error("file is not a whole number of sectors");
    }
    std::size_t const sector_total = (b.size() - kHeaderSize) / sector_size_;
    if (dir_start_ >= kMaxRegularSector) {
        throw Error("no directory start sector");
    }
    if (dir_start_ >= sector_total) {
        throw Error("directory start sector " + std::to_string(dir_start_) +
                    " is past the end of the file");
    }

    /* DIFAT: 109 slots in the header, then continuation sectors chained. */
    difat_slots_.clear();
    for (std::size_t i = 0; i < kDifatHeaderSlots; ++i) {
        std::uint32_t const sid = read_u32(b, 0x4C + 4 * i);
        if (sid < kMaxRegularSector) {
            difat_slots_.push_back(sid);
        }
    }
    difat_extra_.clear();
    std::uint32_t sid = difat_start_;
    std::size_t const per_difat = sector_size_ / 4 - 1;
    for (std::uint32_t n = 0; n < num_difat && sid < kMaxRegularSector; ++n) {
        if (sid >= sector_total) {
            throw Error("DIFAT continuation sector past the end of the file");
        }
        difat_extra_.push_back(sid);
        std::size_t const base = static_cast<std::size_t>(sector_offset(sid));
        for (std::size_t i = 0; i < per_difat; ++i) {
            std::uint32_t const fat_sid = read_u32(b, base + 4 * i);
            if (fat_sid < kMaxRegularSector) {
                difat_slots_.push_back(fat_sid);
            }
        }
        sid = read_u32(b, base + sector_size_ - 4);
    }
    if (difat_slots_.size() < num_fat) {
        throw Error("header declares " + std::to_string(num_fat) +
                    " FAT sectors but only " +
                    std::to_string(difat_slots_.size()) + " are listed");
    }
    difat_slots_.resize(num_fat);

    /* FAT */
    std::size_t const eps = sector_size_ / 4;
    fat_.clear();
    for (std::uint32_t const fat_sid : difat_slots_) {
        if (fat_sid >= sector_total) {
            throw Error("FAT sector " + std::to_string(fat_sid) + " past EOF");
        }
        std::size_t const base = static_cast<std::size_t>(sector_offset(fat_sid));
        for (std::size_t i = 0; i < eps; ++i) {
            fat_.push_back(read_u32(b, base + 4 * i));
        }
    }
    /* Unreferenced sectors.  A FAT shorter than the file is SLACK, not
     * truncation, when every chain ends inside the FAT: MS Project writes files
     * like that (corpus/imported/sf248.mpp has 959 sectors and a 768-entry FAT).
     * Report it and keep going; chain_from() below still refuses a chain that
     * points past the FAT, so real truncation stays fatal.  Mirrors
     * core/mpp/cfb.cpp's rule (wave 20 M6). */
    if (fat_.size() < sector_total) {
        warnings_.push_back(
            std::to_string(sector_total - fat_.size()) +
            " unreferenced sectors: the FAT describes " +
            std::to_string(fat_.size()) + " of the file's " +
            std::to_string(sector_total) +
            " sectors; the remainder is not part of any chain");
    }

    /* directory chain and entries */
    std::vector<std::uint32_t> const dir = chain_from(fat_, dir_start_, "directory");
    dir_bytes_ = gather(dir, dir.size() * sector_size_);
    if (dir_bytes_.size() % kDirectoryEntrySize != 0) {
        throw Error("directory byte count is not a multiple of 128");
    }
    std::size_t const entry_count = dir_bytes_.size() / kDirectoryEntrySize;
    entries_.assign(entry_count, DirectoryEntry{});
    for (std::size_t i = 0; i < entry_count; ++i) {
        std::size_t const o = i * kDirectoryEntrySize;
        DirectoryEntry& e = entries_[i];
        std::copy_n(dir_bytes_.begin() + static_cast<std::ptrdiff_t>(o), 64,
                    e.name_bytes.begin());
        e.name_length = read_u16(dir_bytes_, o + 64);
        e.type = dir_bytes_[o + 66];
        e.color = dir_bytes_[o + 67];
        e.left = read_u32(dir_bytes_, o + 68);
        e.right = read_u32(dir_bytes_, o + 72);
        e.child = read_u32(dir_bytes_, o + 76);
        std::copy_n(dir_bytes_.begin() + static_cast<std::ptrdiff_t>(o + 80), 16,
                    e.clsid.begin());
        e.state = read_u32(dir_bytes_, o + 96);
        e.creation_time = read_u64(dir_bytes_, o + 100);
        e.modified_time = read_u64(dir_bytes_, o + 108);
        e.start = read_u32(dir_bytes_, o + 116);
        e.size = read_u64(dir_bytes_, o + 120);
    }
    if (entries_.empty() || entries_[0].type != 5) {
        throw Error("first directory entry is not the root storage");
    }

    /* mini FAT */
    minifat_.clear();
    minifat_chain_.clear();
    if (minifat_start_ < kMaxRegularSector) {
        minifat_chain_ = chain_from(fat_, minifat_start_, "mini FAT");
        std::vector<std::uint8_t> const bytes =
            gather(minifat_chain_, minifat_chain_.size() * sector_size_);
        for (std::size_t i = 0; i + 3 < bytes.size(); i += 4) {
            minifat_.push_back(read_u32(bytes, i));
        }
    }

    /* mini stream (root entry) */
    root_chain_.clear();
    mini_stream_.clear();
    mini_stream_tail_.clear();
    DirectoryEntry const& root = entries_[0];
    if (root.size > 0) {
        root_chain_ = chain_from(fat_, root.start, "mini stream");
        std::size_t const capacity = root_chain_.size() * sector_size_;
        if (capacity < root.size) {
            throw Error("mini stream chain is shorter than the root entry size");
        }
        mini_stream_ = gather(root_chain_, static_cast<std::size_t>(root.size));
        std::vector<std::uint8_t> const all = gather(root_chain_, capacity);
        mini_stream_tail_.assign(all.begin() + static_cast<std::ptrdiff_t>(root.size),
                                 all.end());
    }

    build_paths();
    read_chains();
}

void CfbDocument::build_paths() {
    for (DirectoryEntry& e : entries_) {
        e.path.clear();
        e.stored_path.clear();
        e.parent = kNoStream;
    }
    struct Pending {
        std::uint32_t index;
        std::uint32_t parent;
    };
    std::vector<Pending> stack;
    if (entries_[0].child < kNoStream) {
        stack.push_back({entries_[0].child, kNoStream});
    }
    std::vector<bool> seen(entries_.size(), false);
    while (!stack.empty()) {
        Pending const item = stack.back();
        stack.pop_back();
        std::uint32_t const index = item.index;
        if (index == 0 || index >= entries_.size() || seen[index]) {
            continue;
        }
        seen[index] = true;
        DirectoryEntry& e = entries_[index];
        e.parent = item.parent;
        std::string const leaf = trim_spaces(e.name());
        std::string const stored = e.name();
        if (item.parent == kNoStream) {
            e.path = leaf;
            e.stored_path = stored;
        } else {
            e.path = entries_[item.parent].path + "/" + leaf;
            e.stored_path = entries_[item.parent].stored_path + "/" + stored;
        }
        if (e.left < kNoStream) {
            stack.push_back({e.left, item.parent});
        }
        if (e.right < kNoStream) {
            stack.push_back({e.right, item.parent});
        }
        if (e.child < kNoStream) {
            stack.push_back({e.child, index});
        }
    }
}

void CfbDocument::read_chains() {
    streams_.clear();
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        DirectoryEntry const& e = entries_[i];
        if (e.type != 2) {
            continue;
        }
        Stream s;
        s.entry = i;
        s.path = e.path;
        s.stored_path = e.stored_path;
        if (e.size > 0) {
            if (e.size < mini_cutoff_ && !minifat_.empty()) {
                s.mini_chain = chain_from(minifat_, e.start, e.path);
                std::size_t const capacity =
                    s.mini_chain.size() * mini_sector_size_;
                if (capacity < e.size) {
                    throw Error("stream '" + e.path + "' mini chain is too short");
                }
                s.data.reserve(static_cast<std::size_t>(e.size));
                for (std::size_t k = 0; k < s.mini_chain.size(); ++k) {
                    std::uint32_t const m = s.mini_chain[k];
                    std::size_t const base =
                        static_cast<std::size_t>(m) * mini_sector_size_;
                    if (base + mini_sector_size_ > mini_stream_.size()) {
                        throw Error("stream '" + e.path + "' mini sector " +
                                    std::to_string(m) + " is past the mini stream");
                    }
                    for (std::size_t j = 0; j < mini_sector_size_; ++j) {
                        if (s.data.size() < e.size) {
                            s.data.push_back(mini_stream_[base + j]);
                        }
                    }
                }
            } else {
                s.chain = chain_from(fat_, e.start, e.path);
                std::size_t const capacity = s.chain.size() * sector_size_;
                if (capacity < e.size) {
                    throw Error("stream '" + e.path + "' chain is too short");
                }
                std::vector<std::uint8_t> const all = gather(s.chain, capacity);
                s.data.assign(all.begin(),
                              all.begin() + static_cast<std::ptrdiff_t>(e.size));
                s.tail.assign(all.begin() + static_cast<std::ptrdiff_t>(e.size),
                              all.end());
            }
        }
        streams_.push_back(std::move(s));
    }
}

/* --- chain helpers ------------------------------------------------------- */

std::vector<std::uint32_t> CfbDocument::chain_from(
    std::vector<std::uint32_t> const& table, std::uint32_t start,
    std::string_view what) const {
    std::vector<std::uint32_t> out;
    std::vector<bool> seen(table.size(), false);
    std::uint32_t sid = start;
    while (sid < kMaxRegularSector) {
        if (sid >= table.size()) {
            throw Error(std::string(what) + ": sector " + std::to_string(sid) +
                        " is past the end of the allocation table");
        }
        if (seen[sid]) {
            throw Error(std::string(what) + ": sector chain loops at " +
                        std::to_string(sid));
        }
        seen[sid] = true;
        out.push_back(sid);
        sid = table[sid];
    }
    return out;
}

std::vector<std::uint8_t> CfbDocument::gather(
    std::vector<std::uint32_t> const& chain, std::size_t size) const {
    std::vector<std::uint8_t> out;
    out.reserve(std::min<std::size_t>(size, source_.size()));
    for (std::uint32_t const sid : chain) {
        std::size_t const base = static_cast<std::size_t>(sector_offset(sid));
        if (base + sector_size_ > source_.size() || out.size() >= size) {
            break;
        }
        std::size_t const take = std::min<std::size_t>(sector_size_, size - out.size());
        out.insert(out.end(), source_.begin() + static_cast<std::ptrdiff_t>(base),
                   source_.begin() + static_cast<std::ptrdiff_t>(base + take));
    }
    if (out.size() != size) {
        throw Error("chain is shorter than the requested " +
                    std::to_string(size) + " bytes");
    }
    return out;
}

std::vector<std::uint32_t> CfbDocument::sectors_used() const {
    std::vector<std::uint32_t> out;
    auto add_all = [&](std::vector<std::uint32_t> const& ids) {
        out.insert(out.end(), ids.begin(), ids.end());
    };
    add_all(fat_sids_all());
    add_all(dir_chain());
    add_all(minifat_chain_);
    add_all(root_chain_);
    for (Stream const& s : streams_) {
        add_all(s.chain);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::vector<std::uint32_t> CfbDocument::dir_chain() const {
    return chain_from(fat_, dir_start_, "directory");
}

/* --- accessors ----------------------------------------------------------- */

std::vector<std::string> CfbDocument::stream_paths() const {
    std::vector<std::string> out;
    out.reserve(streams_.size());
    for (Stream const& s : streams_) {
        out.push_back(s.path);
    }
    return out;
}

std::vector<std::string> CfbDocument::stored_stream_paths() const {
    std::vector<std::string> out;
    out.reserve(streams_.size());
    for (Stream const& s : streams_) {
        out.push_back(s.stored_path);
    }
    return out;
}

Stream* CfbDocument::find_stream(std::string_view path) {
    for (Stream& s : streams_) {
        if (cfb_path_matches(s.path, path)) {
            return &s;
        }
    }
    return nullptr;
}

Stream const* CfbDocument::find_stream(std::string_view path) const {
    for (Stream const& s : streams_) {
        if (cfb_path_matches(s.path, path)) {
            return &s;
        }
    }
    return nullptr;
}

bool CfbDocument::has_stream(std::string_view path) const {
    return find_stream(path) != nullptr;
}

std::vector<std::uint8_t> CfbDocument::read_stream(std::string_view path) const {
    Stream const* s = find_stream(path);
    if (s == nullptr) {
        throw Error("no stream named '" + std::string(path) + "'");
    }
    return s->data;
}

std::uint64_t CfbDocument::stream_size(std::string_view path) const {
    Stream const* s = find_stream(path);
    if (s == nullptr) {
        throw Error("no stream named '" + std::string(path) + "'");
    }
    return s->data.size();
}

std::vector<std::pair<std::uint64_t, std::size_t>>
CfbDocument::stream_file_ranges(std::string_view path) const {
    Stream const* s = find_stream(path);
    if (s == nullptr) {
        throw Error("no stream named '" + std::string(path) + "'");
    }
    std::vector<std::pair<std::uint64_t, std::size_t>> out;
    std::size_t remaining = s->data.size();
    if (!s->mini_chain.empty()) {
        std::size_t cursor = 0; /* byte offset inside the stream */
        for (std::uint32_t const m : s->mini_chain) {
            if (remaining == 0) {
                break;
            }
            std::size_t const take =
                std::min<std::size_t>(mini_sector_size_, remaining);
            std::size_t offset = static_cast<std::size_t>(m) * mini_sector_size_;
            std::size_t left = take;
            while (left > 0) {
                std::size_t const sector_index = offset / sector_size_;
                std::size_t const within = offset % sector_size_;
                std::size_t const chunk =
                    std::min<std::size_t>(left, sector_size_ - within);
                if (sector_index >= root_chain_.size()) {
                    throw Error("mini sector " + std::to_string(m) +
                                " maps outside the mini stream chain");
                }
                out.emplace_back(
                    sector_offset(root_chain_[sector_index]) + within, chunk);
                offset += chunk;
                left -= chunk;
                cursor += chunk;
            }
            remaining -= take;
        }
        if (remaining != 0) {
            throw Error("stream '" + std::string(path) +
                        "' mini chain is shorter than its payload");
        }
        (void)cursor;
        return out;
    }
    for (std::uint32_t const sid : s->chain) {
        if (remaining == 0) {
            break;
        }
        std::size_t const chunk = std::min<std::size_t>(sector_size_, remaining);
        out.emplace_back(sector_offset(sid), chunk);
        remaining -= chunk;
    }
    if (remaining != 0) {
        throw Error("stream '" + std::string(path) +
                    "' chain is shorter than its payload");
    }
    return out;
}

/* --- allocation ---------------------------------------------------------- */

namespace {

/* Bytes that occupy [new_size, capacity) given the previous padding, which
 * started at old_size.  Used when a stream is replaced inside the same
 * allocation: the untouched padding is carried, bytes that the payload now
 * covers are dropped, and bytes the payload no longer covers become zero. */
std::vector<std::uint8_t> carry_tail(std::vector<std::uint8_t> const& old_tail,
                                     std::size_t old_size, std::size_t new_size,
                                     std::size_t capacity) {
    std::vector<std::uint8_t> out(capacity > new_size ? capacity - new_size : 0, 0);
    if (new_size >= old_size) {
        std::size_t const skip = new_size - old_size;
        if (skip < old_tail.size()) {
            std::size_t const n = std::min(out.size(), old_tail.size() - skip);
            std::copy_n(old_tail.begin() + static_cast<std::ptrdiff_t>(skip), n,
                        out.begin());
        }
    } else {
        std::size_t const lead = old_size - new_size;
        if (lead < out.size()) {
            std::size_t const n = std::min(out.size() - lead, old_tail.size());
            std::copy_n(old_tail.begin(), n,
                        out.begin() + static_cast<std::ptrdiff_t>(lead));
        }
    }
    return out;
}

} /* namespace */

void CfbDocument::replace_stream(std::string_view path,
                                 std::vector<std::uint8_t> data) {
    Stream* s = find_stream(path);
    if (s == nullptr) {
        throw Error("no stream named '" + std::string(path) + "'");
    }
    if (s->entry == 0) {
        throw Error("refusing to replace the root entry");
    }
    DirectoryEntry& entry = entries_[s->entry];
    std::size_t const old_size = s->data.size();
    bool const stay_mini = data.size() < mini_cutoff_ && !minifat_.empty();

    if (!stay_mini) {
        /* regular sectors */
        std::size_t const need =
            (data.size() + sector_size_ - 1) / sector_size_;
        if (need == 0) {
            s->chain.clear();
            s->tail.clear();
        } else if (need == s->chain.size()) {
            s->tail = carry_tail(s->tail, old_size, data.size(),
                                 need * sector_size_);
        } else {
            std::uint32_t next = next_free_sector();
            std::vector<std::uint32_t> fresh;
            fresh.reserve(need);
            for (std::size_t i = 0; i < need; ++i) {
                fresh.push_back(next + static_cast<std::uint32_t>(i));
            }
            s->chain = std::move(fresh);
            s->mini_chain.clear();
            s->tail.assign(need * sector_size_ - data.size(), 0);
        }
        s->mini_chain.clear();
    } else {
        /* mini sectors: keep a same-size allocation, otherwise append. */
        std::size_t const need =
            (data.size() + mini_sector_size_ - 1) / mini_sector_size_;
        bool const fits_existing = need <= s->mini_chain.size();
        if (data.empty()) {
            s->mini_chain.clear();
        } else if (fits_existing && !s->mini_chain.empty()) {
            s->mini_chain.resize(need);
        } else {
            std::size_t const total = mini_stream_.size() / mini_sector_size_;
            std::vector<std::uint32_t> fresh;
            fresh.reserve(need);
            for (std::size_t i = 0; i < need; ++i) {
                fresh.push_back(static_cast<std::uint32_t>(total + i));
            }
            s->mini_chain = std::move(fresh);
            mini_stream_.resize((total + need) * mini_sector_size_, 0);
            /* The payload now covers the old padding; whatever is left over is
             * new and therefore zero. */
            mini_stream_tail_.clear();
        }
        s->chain.clear();
        s->tail.clear();
    }

    s->data = std::move(data);
    entry.size = s->data.size();
    if (!s->mini_chain.empty()) {
        entry.start = s->mini_chain.front();
    } else if (!s->chain.empty()) {
        entry.start = s->chain.front();
    } else {
        entry.start = kEndOfChain;
    }
    if (entry.size == 0) {
        entry.start = kEndOfChain;
    }
    /* The mini stream IS the root entry's payload, so growing it (above) has
     * to be reflected in the root directory entry or a re-read would truncate
     * the stream and lose the new mini sectors. */
    entries_[0].size = mini_stream_.size();
    modified_ = true;
}

std::uint32_t CfbDocument::next_free_sector() const {
    std::uint32_t highest = 0;
    bool any = false;
    auto note = [&](std::uint32_t sid) {
        highest = std::max(highest, sid);
        any = true;
    };
    for (std::uint32_t const sid : fat_sids_all()) {
        note(sid);
    }
    for (std::uint32_t const sid : dir_chain()) {
        note(sid);
    }
    for (std::uint32_t const sid : minifat_chain_) {
        note(sid);
    }
    for (std::uint32_t const sid : root_chain_) {
        note(sid);
    }
    for (Stream const& s : streams_) {
        for (std::uint32_t const sid : s.chain) {
            note(sid);
        }
    }
    return any ? highest + 1 : 0;
}

std::vector<std::uint32_t> CfbDocument::fat_sids_all() const {
    std::vector<std::uint32_t> out = difat_slots_;
    out.insert(out.end(), difat_extra_.begin(), difat_extra_.end());
    return out;
}

/* --- layout -------------------------------------------------------------- */

CfbDocument::Layout CfbDocument::layout() const {
    Layout L;
    std::size_t const eps = sector_size_ / 4;
    L.entries_per_sector = eps;

    std::vector<std::uint32_t> const dir = dir_chain();
    std::vector<std::uint32_t> fat_ids = difat_slots_;
    std::vector<std::uint32_t> difat_ids = difat_extra_;
    std::vector<std::uint32_t> minifat_ids = minifat_chain_;
    std::vector<std::uint32_t> root_ids = root_chain_;

    std::size_t highest = 0;
    bool any = false;
    auto note = [&](std::uint32_t sid) {
        highest = std::max<std::size_t>(highest, sid);
        any = true;
    };
    for (std::uint32_t const sid : dir) {
        note(sid);
    }
    for (std::uint32_t const sid : fat_ids) {
        note(sid);
    }
    for (std::uint32_t const sid : difat_ids) {
        note(sid);
    }
    for (std::uint32_t const sid : minifat_ids) {
        note(sid);
    }
    for (std::uint32_t const sid : root_ids) {
        note(sid);
    }
    for (Stream const& s : streams_) {
        for (std::uint32_t const sid : s.chain) {
            note(sid);
        }
    }
    std::size_t sector_count =
        std::max<std::size_t>(sector_count_parsed(), any ? highest + 1 : 0);

    std::size_t next = sector_count;
    auto reserve = [&](std::vector<std::uint32_t>& v, std::size_t n) {
        while (v.size() < n) {
            v.push_back(static_cast<std::uint32_t>(next));
            ++next;
        }
    };
    std::size_t mini_entries = 0;
    for (Stream const& s : streams_) {
        for (std::uint32_t const m : s.mini_chain) {
            mini_entries = std::max<std::size_t>(mini_entries, m + 1u);
        }
    }
    for (int guard = 0; guard < 16; ++guard) {
        std::size_t const before = next;
        std::size_t const sc = std::max<std::size_t>(sector_count, next);
        reserve(fat_ids, (sc + eps - 1) / eps);
        reserve(difat_ids, fat_ids.size() > kDifatHeaderSlots
                               ? (fat_ids.size() - kDifatHeaderSlots + eps - 2) /
                                     (eps - 1)
                               : 0);
        reserve(minifat_ids, (mini_entries + eps - 1) / eps);
        reserve(root_ids, (mini_stream_.size() + sector_size_ - 1) / sector_size_);
        if (next == before) {
            break;
        }
        sector_count = std::max<std::size_t>(sector_count, next);
    }
    sector_count = std::max<std::size_t>(sector_count, next);
    if (fat_ids.size() * eps < sector_count) {
        throw Error("internal: FAT of " + std::to_string(fat_ids.size()) +
                    " sectors cannot address " + std::to_string(sector_count) +
                    " sectors");
    }
    if (difat_ids.size() !=
        (fat_ids.size() > kDifatHeaderSlots
             ? (fat_ids.size() - kDifatHeaderSlots + eps - 2) / (eps - 1)
             : 0)) {
        throw Error("internal: DIFAT sector count mismatch");
    }

    L.fat.assign(fat_ids.size() * eps, kFreeSector);
    for (std::uint32_t const sid : fat_ids) {
        if (sid >= L.fat.size()) {
            throw Error("FAT sector id " + std::to_string(sid) + " out of range");
        }
        L.fat[sid] = kFatSector;
    }
    for (std::uint32_t const sid : difat_ids) {
        if (sid >= L.fat.size()) {
            throw Error("DIFAT sector id " + std::to_string(sid) + " out of range");
        }
        L.fat[sid] = kDifatSector;
    }
    auto link = [&](std::vector<std::uint32_t> const& chain,
                    std::string_view what) {
        for (std::size_t i = 0; i < chain.size(); ++i) {
            if (chain[i] >= L.fat.size()) {
                throw Error(std::string(what) + ": sector " +
                            std::to_string(chain[i]) + " out of FAT range");
            }
            L.fat[chain[i]] =
                i + 1 < chain.size() ? chain[i + 1] : kEndOfChain;
        }
    };
    link(dir, "directory");
    link(minifat_ids, "mini FAT");
    link(root_ids, "mini stream");
    for (Stream const& s : streams_) {
        link(s.chain, s.path);
    }

    L.minifat.assign(std::max<std::size_t>(minifat_ids.size() * eps, mini_entries),
                     kFreeSector);
    for (Stream const& s : streams_) {
        for (std::size_t i = 0; i < s.mini_chain.size(); ++i) {
            std::uint32_t const m = s.mini_chain[i];
            if (m >= L.minifat.size()) {
                throw Error("stream '" + s.path + "': mini sector " +
                            std::to_string(m) + " out of mini FAT range");
            }
            L.minifat[m] = i + 1 < s.mini_chain.size() ? s.mini_chain[i + 1]
                                                       : kEndOfChain;
        }
    }

    L.fat_sids = std::move(fat_ids);
    L.difat_sids = std::move(difat_ids);
    L.minifat_sids = std::move(minifat_ids);
    L.root_chain = std::move(root_ids);
    L.sector_count = sector_count;
    return L;
}

std::size_t CfbDocument::sector_count_parsed() const {
    if (source_.size() <= kHeaderSize) {
        return 0;
    }
    return (source_.size() - kHeaderSize) / sector_size_;
}

/* --- emit ---------------------------------------------------------------- */

namespace {

/* Every sector must have exactly one owner, or the file is ambiguous and a
 * reader would disagree with us. */
enum class OwnerKind {
    none,
    fat,
    difat,
    directory,
    minifat,
    mini_stream,
    stream,
};

struct Owner {
    OwnerKind kind = OwnerKind::none;
    std::size_t index = 0;
};

} /* namespace */

std::vector<std::uint8_t> CfbDocument::to_bytes() const {
    /* A document parsed with unreferenced sectors cannot be reproduced: the
     * unreferenced sectors are not part of any chain, and this writer derives
     * the FAT from the chains it knows (see FAT SLACK in the header).  Refuse
     * rather than silently emit a different file.  Reading, including every
     * stream, still works. */
    if (!warnings_.empty()) {
        throw Error(
            "refusing to re-emit a container with " +
            std::to_string(warnings_.size()) +
            " recoverable warning(s): " + warnings_.front() +
            ".  The unreferenced sectors are not part of any chain, so the "
            "derived FAT cannot reproduce this file; parse it for reading "
            "only");
    }
    Layout const L = layout();
    std::size_t const ss = sector_size_;
    std::size_t const eps = L.entries_per_sector;

    std::vector<std::uint8_t> out(kHeaderSize + L.sector_count * ss, 0);

    std::vector<std::uint32_t> const dir = dir_chain();
    std::vector<Owner> owners(L.sector_count);
    std::vector<std::size_t> stream_pos(L.sector_count, 0);
    auto claim = [&](std::uint32_t sid, OwnerKind kind, std::size_t index,
                     std::string_view who) {
        if (sid >= L.sector_count) {
            throw Error(std::string(who) + ": sector " + std::to_string(sid) +
                        " is past the end of the chosen sector count " +
                        std::to_string(L.sector_count));
        }
        if (owners[sid].kind != OwnerKind::none) {
            throw Error("sector " + std::to_string(sid) + " is claimed by " +
                        std::string(who) + " and by another structure");
        }
        owners[sid].kind = kind;
        owners[sid].index = index;
    };
    for (std::size_t i = 0; i < L.fat_sids.size(); ++i) {
        claim(L.fat_sids[i], OwnerKind::fat, i, "FAT");
    }
    for (std::size_t i = 0; i < L.difat_sids.size(); ++i) {
        claim(L.difat_sids[i], OwnerKind::difat, i, "DIFAT");
    }
    for (std::size_t i = 0; i < dir.size(); ++i) {
        claim(dir[i], OwnerKind::directory, i, "directory");
    }
    for (std::size_t i = 0; i < L.minifat_sids.size(); ++i) {
        claim(L.minifat_sids[i], OwnerKind::minifat, i, "mini FAT");
    }
    for (std::size_t i = 0; i < L.root_chain.size(); ++i) {
        claim(L.root_chain[i], OwnerKind::mini_stream, i, "mini stream");
    }
    for (std::size_t i = 0; i < streams_.size(); ++i) {
        for (std::size_t k = 0; k < streams_[i].chain.size(); ++k) {
            claim(streams_[i].chain[k], OwnerKind::stream, i,
                  streams_[i].path);
            stream_pos[streams_[i].chain[k]] = k;
        }
    }

    /* directory image: the parsed bytes with every entry re-serialised, so the
     * slack after the last entry is preserved. */
    std::vector<std::uint8_t> dir_image = dir_bytes_;
    if (dir_image.size() < dir.size() * ss) {
        dir_image.resize(dir.size() * ss, 0);
    }
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        DirectoryEntry const& e = entries_[i];
        std::uint8_t* p = dir_image.data() + i * kDirectoryEntrySize;
        std::copy(e.name_bytes.begin(), e.name_bytes.end(), p);
        write_u16(p + 64, e.name_length);
        p[66] = e.type;
        p[67] = e.color;
        write_u32(p + 68, e.left);
        write_u32(p + 72, e.right);
        write_u32(p + 76, e.child);
        std::copy(e.clsid.begin(), e.clsid.end(), p + 80);
        write_u32(p + 96, e.state);
        write_u64(p + 100, e.creation_time);
        write_u64(p + 108, e.modified_time);
        write_u32(p + 116, e.start);
        write_u64(p + 120, e.size);
    }

    /* mini stream image: the parsed bytes with every mini payload written back
     * at its own mini sector, so unallocated slack survives. */
    std::vector<std::uint8_t> mini = mini_stream_;
    mini.insert(mini.end(), mini_stream_tail_.begin(), mini_stream_tail_.end());
    mini.resize(L.root_chain.size() * ss, 0);
    for (Stream const& s : streams_) {
        for (std::size_t k = 0; k < s.mini_chain.size(); ++k) {
            std::size_t const base =
                static_cast<std::size_t>(s.mini_chain[k]) * mini_sector_size_;
            if (base + mini_sector_size_ > mini.size()) {
                throw Error("stream '" + s.path + "': mini sector beyond the mini stream");
            }
            std::size_t const from = k * mini_sector_size_;
            std::size_t const take =
                from < s.data.size()
                    ? std::min<std::size_t>(mini_sector_size_, s.data.size() - from)
                    : 0;
            std::copy_n(s.data.begin() + static_cast<std::ptrdiff_t>(from), take,
                        mini.begin() + static_cast<std::ptrdiff_t>(base));
        }
    }

    for (std::size_t sid = 0; sid < L.sector_count; ++sid) {
        std::uint8_t* dst = out.data() + sector_offset(static_cast<std::uint32_t>(sid));
        Owner const& owner = owners[sid];
        switch (owner.kind) {
            case OwnerKind::fat: {
                std::size_t const base = owner.index * eps;
                for (std::size_t j = 0; j < eps; ++j) {
                    write_u32(dst + j * 4, L.fat[base + j]);
                }
                break;
            }
            case OwnerKind::difat: {
                std::size_t const per = eps - 1;
                for (std::size_t j = 0; j < per; ++j) {
                    std::size_t const k =
                        kDifatHeaderSlots + owner.index * per + j;
                    write_u32(dst + j * 4,
                              k < L.fat_sids.size() ? L.fat_sids[k] : kFreeSector);
                }
                write_u32(dst + per * 4,
                          owner.index + 1 < L.difat_sids.size()
                              ? L.difat_sids[owner.index + 1]
                              : kEndOfChain);
                break;
            }
            case OwnerKind::directory: {
                std::size_t const base = owner.index * ss;
                std::copy_n(dir_image.begin() + static_cast<std::ptrdiff_t>(base),
                            ss, dst);
                break;
            }
            case OwnerKind::minifat: {
                std::size_t const base = owner.index * eps;
                for (std::size_t j = 0; j < eps; ++j) {
                    write_u32(dst + j * 4, L.minifat[base + j]);
                }
                break;
            }
            case OwnerKind::mini_stream: {
                std::size_t const base = owner.index * ss;
                std::copy_n(mini.begin() + static_cast<std::ptrdiff_t>(base), ss,
                            dst);
                break;
            }
            case OwnerKind::stream: {
                Stream const& s = streams_[owner.index];
                std::size_t const k = stream_pos[sid];
                std::size_t const from = k * ss;
                std::size_t const take =
                    from < s.data.size()
                        ? std::min<std::size_t>(ss, s.data.size() - from)
                        : 0;
                std::copy_n(s.data.begin() + static_cast<std::ptrdiff_t>(from),
                            take, dst);
                if (take < ss) {
                    std::size_t const pad_from = from + take - s.data.size();
                    std::vector<std::uint8_t> const& tail = s.tail;
                    std::size_t const n =
                        pad_from < tail.size()
                            ? std::min<std::size_t>(ss - take, tail.size() - pad_from)
                            : 0;
                    if (n > 0) {
                        std::copy_n(tail.begin() + static_cast<std::ptrdiff_t>(pad_from),
                                    n, dst + take);
                    }
                }
                break;
            }
            case OwnerKind::none:
            default:
                /* free sector: left zero, as in every corpus file */
                break;
        }
    }

    /* header */
    std::uint8_t* h = out.data();
    std::copy(std::begin(kSignature), std::end(kSignature), h);
    std::copy(clsid_.begin(), clsid_.end(), h + 8);
    write_u16(h + 0x18, minor_version_);
    write_u16(h + 0x1A, major_version_);
    write_u16(h + 0x1C, byte_order_);
    write_u16(h + 0x1E, sector_shift_);
    write_u16(h + 0x20, mini_shift_);
    std::copy(reserved_.begin(), reserved_.end(), h + 0x22);
    write_u32(h + 0x28, num_dir_sectors_);
    write_u32(h + 0x2C, static_cast<std::uint32_t>(L.fat_sids.size()));
    write_u32(h + 0x30, dir.front());
    write_u32(h + 0x34, transaction_);
    write_u32(h + 0x38, mini_cutoff_);
    write_u32(h + 0x3C, L.minifat_sids.empty() ? kEndOfChain
                                               : L.minifat_sids.front());
    write_u32(h + 0x40, static_cast<std::uint32_t>(L.minifat_sids.size()));
    write_u32(h + 0x44, L.difat_sids.empty() ? kEndOfChain : L.difat_sids.front());
    write_u32(h + 0x48, static_cast<std::uint32_t>(L.difat_sids.size()));
    for (std::size_t i = 0; i < kDifatHeaderSlots; ++i) {
        write_u32(h + 0x4C + 4 * i,
                  i < L.fat_sids.size() ? L.fat_sids[i] : kFreeSector);
    }
    return out;
}

void CfbDocument::save(std::string const& path) const {
    std::vector<std::uint8_t> const bytes = to_bytes();
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw Error("cannot write '" + path + "'");
    }
    if (!bytes.empty()) {
        output.write(reinterpret_cast<char const*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }
    if (!output) {
        throw Error("short write on '" + path + "'");
    }
}

/* --- self check ---------------------------------------------------------- */

std::string CfbDocument::check() const {
    try {
        if (!warnings_.empty()) {
            return "not round-trippable: " + warnings_.front();
        }
        if (!modified_) {
            Layout const L = layout();
            if (L.fat.size() != fat_.size()) {
                return "FAT has " + std::to_string(fat_.size()) +
                       " entries but the layout implies " +
                       std::to_string(L.fat.size());
            }
            for (std::size_t i = 0; i < fat_.size(); ++i) {
                if (L.fat[i] != fat_[i]) {
                    return "FAT[" + std::to_string(i) + "]: file says " +
                           hex(fat_[i]) + ", chains imply " + hex(L.fat[i]);
                }
            }
        }
        std::vector<std::uint8_t> const bytes = to_bytes();
        if (!modified_ && bytes.size() != source_.size()) {
            return "re-emitted size " + std::to_string(bytes.size()) +
                   " differs from input size " +
                   std::to_string(source_.size());
        }
        for (Stream const& s : streams_) {
            if (s.data.size() != entries_[s.entry].size) {
                return "stream '" + s.path + "' payload is " +
                       std::to_string(s.data.size()) + " bytes but its directory "
                       "entry says " + std::to_string(entries_[s.entry].size);
            }
        }
        return {};
    } catch (Error const& e) {
        return std::string("check failed: ") + e.what();
    }
}

} /* namespace mppwrite */
