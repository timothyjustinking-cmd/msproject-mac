#include "edit_marker.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mppwrite {
namespace {

std::uint32_t get_u32(std::vector<std::uint8_t> const& bytes,
                      std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

std::uint16_t get_u16(std::vector<std::uint8_t> const& bytes,
                      std::size_t offset) {
    return static_cast<std::uint16_t>(bytes[offset] |
                                      (bytes[offset + 1] << 8));
}

void put_u16(std::vector<std::uint8_t>& bytes, std::size_t offset,
             std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFFu);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
}

} /* namespace */

EditMarker write_edit_marker(CfbDocument& doc, std::string_view path) {
    EditMarker marker;
    std::string const stream_path(path);
    if (!doc.has_stream(stream_path)) {
        return marker;
    }
    marker.stream_present = true;
    std::vector<std::uint8_t> const data = doc.read_stream(stream_path);

    /* The id is 0x3540000a little-endian; the record layout is
     * u32 length / u32 id / u32 kind / payload. */
    constexpr std::array<std::uint8_t, 4> kMarkerId = {0x0A, 0x00, 0x40, 0x35};
    std::vector<std::size_t> found;
    for (std::size_t at = 0; at + kMarkerId.size() <= data.size(); ++at) {
        if (data[at] == kMarkerId[0] && data[at + 1] == kMarkerId[1] &&
            data[at + 2] == kMarkerId[2] && data[at + 3] == kMarkerId[3]) {
            found.push_back(at);
        }
    }
    if (found.empty()) {
        /* synthetic-base.mpp / wave23-outline.mpp: no Project save wrote this
         * file, so there is no measured record to reproduce. */
        return marker;
    }
    if (found.size() != 1) {
        throw Error(stream_path + " holds " + std::to_string(found.size()) +
                    " copies of the edit-marker id 0x3540000a; only the one "
                    "Project's own saves carry has been measured, so the save is "
                    "refused");
    }
    std::size_t const at = found.front();
    if (at < 4) {
        throw Error(stream_path + " has the edit-marker id at byte " +
                    std::to_string(at) + ", too early to be a record; refusing");
    }
    std::size_t const record = at - 4;
    if (record + 14 > data.size()) {
        throw Error(stream_path + " has the edit-marker id at byte " +
                    std::to_string(at) +
                    " but no complete record around it; refusing");
    }
    std::uint32_t const length = get_u32(data, record);
    std::uint32_t const ident = get_u32(data, record + 4);
    std::uint32_t const kind = get_u32(data, record + 8);
    if (length != 2 || ident != kEditMarkerRecord || kind != 2) {
        throw Error(stream_path + " edit-marker record at byte " +
                    std::to_string(record) + " is len=" +
                    std::to_string(length) + " id=0x" +
                    std::to_string(ident) + " kind=" + std::to_string(kind) +
                    "; only len=2/id=0x3540000a/kind=2 has been measured, so "
                    "the save is refused");
    }
    marker.record_present = true;
    marker.record_offset = record;
    marker.before = get_u16(data, record + 12);
    if (marker.before != 0 && marker.before != kEditMarkerAfterSave) {
        throw Error(stream_path + " edit-marker payload is " +
                    std::to_string(marker.before) +
                    "; Project's saves store only 0 or 1, so the save is refused");
    }
    marker.after = kEditMarkerAfterSave;
    if (marker.before == kEditMarkerAfterSave) {
        return marker; /* already Project's edit-save value */
    }
    std::vector<std::uint8_t> updated = data;
    put_u16(updated, record + 12, kEditMarkerAfterSave);
    doc.replace_stream(stream_path, updated);
    marker.changed = true;
    return marker;
}

} /* namespace mppwrite */
