/*
 * project_props.cpp -- the measured new-task-mode writer (W28-3.1).
 *
 * The record layout and the measured bytes are stated with their evidence in
 * project_props.h.  This file walks the same Props framing the status-date
 * writer (actuals.cpp) uses: a 16-byte stream header, then records of
 * `u32 length, u32 id, u32 kind, length payload bytes`, ending at the first id
 * whose bits 16..23 are not 0x40.  The target record is located by id alone and
 * its shape is checked before a byte moves.
 */

#include "project_props.h"

#include "table.h"

#include <string>
#include <vector>

namespace mppwrite {
namespace {

constexpr std::uint32_t kNewTaskModeRecord = 0x024013C8;
constexpr std::uint32_t kNewTaskModeKind = 2;
constexpr std::size_t kNewTaskModeLength = 2;
constexpr std::size_t kPropsHeaderSize = 16;
constexpr std::size_t kRecordHeaderSize = 12;

constexpr std::uint16_t kManualBytes = 0x00FF; /* Project writes ff 00 */
constexpr std::uint16_t kAutoBytes = 0x0000;   /* Project writes 00 00 */

std::uint16_t get_u16(std::vector<std::uint8_t> const& bytes,
                      std::size_t offset) {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bytes[offset]) |
        (static_cast<std::uint16_t>(bytes[offset + 1]) << 8));
}

void put_u16(std::vector<std::uint8_t>& bytes, std::size_t offset,
             std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFFu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
}

std::uint32_t get_u32(std::vector<std::uint8_t> const& bytes,
                      std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

}  // namespace

std::uint16_t write_new_task_mode(CfbDocument& doc, bool manual,
                                  std::string_view root) {
    std::string const path = std::string(root) + "/Props";
    if (!doc.has_stream(path)) {
        throw Error(path + " is absent, so the new-task scheduling mode cannot "
                    "be written");
    }
    std::vector<std::uint8_t> props = doc.read_stream(path);
    if (props.size() < kPropsHeaderSize) {
        throw Error(path + " is " + std::to_string(props.size()) +
                    " bytes, shorter than the 16-byte props header");
    }
    std::size_t offset = kPropsHeaderSize;
    while (offset + kRecordHeaderSize <= props.size()) {
        const std::uint32_t length = get_u32(props, offset);
        const std::uint32_t id = get_u32(props, offset + 4);
        const std::uint32_t kind = get_u32(props, offset + 8);
        if ((id & 0x00FF0000u) != 0x00400000u) {
            break; /* the record framing has ended */
        }
        if (static_cast<std::uint64_t>(offset) + kRecordHeaderSize + length >
            props.size()) {
            throw Error(path + " has a truncated record 0x" +
                        std::to_string(id) + " (declared " +
                        std::to_string(length) + " bytes)");
        }
        if (id == kNewTaskModeRecord) {
            if (kind != kNewTaskModeKind || length != kNewTaskModeLength) {
                throw Error(path + " stores the new-task mode as kind " +
                            std::to_string(kind) + " / " +
                            std::to_string(length) +
                            " bytes, not the measured kind 2 / 2 bytes");
            }
            const std::size_t payload = offset + kRecordHeaderSize;
            const std::uint16_t previous = get_u16(props, payload);
            put_u16(props, payload, manual ? kManualBytes : kAutoBytes);
            doc.replace_stream(path, props);
            return previous;
        }
        offset += kRecordHeaderSize + length;
    }
    throw Error(path + " has no new-task-mode record (0x024013c8), so the "
                "new-task scheduling mode cannot be written");
}

}  // namespace mppwrite
