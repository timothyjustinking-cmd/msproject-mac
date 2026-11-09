// tests/edl_control_path.h -- Project's saves of a view-state pair, given the CEdl size the
// control's save-path record would give them.  Test-only.
//
// The two CEdl size words (214/CEdl/VarMeta at 0x14 and the 0x07000100 scalar in 214/Props)
// hold the CEdl Var2Data length, which includes the save-path record 0x26400031 (the full path
// of the saving file).  Project's null and edit saves are made under other file names than the
// control, so their size words carry the path-length difference as noise the byte mask only
// partly covers.  The port keeps the control's path record, so Project's saves are given the
// size their CEdl would have with the control's path record (the same idea as the normalisation
// save_pair_noise.h applies to the path record inside the stream).  See
// core/mppwrite/viewstate_outline.h.
#ifndef TESTS_EDL_CONTROL_PATH_H
#define TESTS_EDL_CONTROL_PATH_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "cfb_writer.h"

namespace edl_control_path {

using Bytes = std::vector<std::uint8_t>;

constexpr char const* kEdlVar2 = "214/CEdl/Var2Data";
constexpr char const* kEdlMeta = "214/CEdl/VarMeta";
constexpr char const* kProps = "214/Props";
constexpr std::size_t kEdlSizeAt = 11402;  // the 0x07000100 scalar's value in 214/Props

inline std::uint32_t u32_at(Bytes const& b, std::size_t o) {
    return static_cast<std::uint32_t>(b[o]) | (static_cast<std::uint32_t>(b[o + 1]) << 8) |
           (static_cast<std::uint32_t>(b[o + 2]) << 16) |
           (static_cast<std::uint32_t>(b[o + 3]) << 24);
}

inline void put_u32_at(Bytes& b, std::size_t o, std::uint32_t v) {
    for (std::size_t i = 0; i < 4; ++i) {
        b[o + i] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu);
    }
}

/* The length of the save-path record in a CEdl Var2Data stream (0 when there is none). */
inline std::size_t path_record_bytes(Bytes const& edl) {
    std::size_t const count = u32_at(edl, 16);
    std::size_t at = 20;
    for (std::size_t i = 0; i < count && at + 12 <= edl.size(); ++i) {
        std::size_t const length = u32_at(edl, at);
        if (u32_at(edl, at + 4) == 0x26400031u) {
            return length;
        }
        at += 12 + length + (length % 2);
    }
    return 0;
}

inline mppwrite::CfbDocument with_control_path_size(std::filesystem::path const& file,
                                                    std::size_t control_path) {
    mppwrite::CfbDocument document = mppwrite::CfbDocument::from_file(file.string());
    Bytes const edl = document.read_stream(kEdlVar2);
    std::size_t const size = edl.size() - path_record_bytes(edl) + control_path;
    Bytes meta = document.read_stream(kEdlMeta);
    put_u32_at(meta, 0x14, static_cast<std::uint32_t>(size));
    document.replace_stream(kEdlMeta, meta);
    Bytes props = document.read_stream(kProps);
    put_u32_at(props, kEdlSizeAt, static_cast<std::uint32_t>(size));
    document.replace_stream(kProps, props);
    return document;
}

}  // namespace edl_control_path

#endif  // TESTS_EDL_CONTROL_PATH_H
