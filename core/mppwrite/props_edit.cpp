/*
 * props_edit.cpp -- record updates and typed-property scalars in `114/Props`
 * and `214/Props`.  W34-2.2 (30 October 2026).  The measured rules and the
 * capability matrix are in props_edit.h; the pair derivation is in
 * spec/writers/props-records.md.
 */

#include "props_edit.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <utility>

#include "edit_marker.h"

namespace mppwrite {
namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr std::size_t kPropsHeaderSize = 16;
constexpr std::uint32_t kPropsFormatWord = 0x00010000u;
constexpr std::uint32_t kRecordIdFamily = 0x00400000u;
constexpr std::size_t kRecordHeaderSize = 12;
constexpr std::size_t kScalarBytes = 16;
constexpr std::size_t kNoIndex = static_cast<std::size_t>(-1);

std::uint32_t get_u32(Bytes const& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

void put_u32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFFu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
    bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
}

void append_u32(Bytes& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
}

std::uint32_t get_big_u32(Bytes const& bytes, std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
           static_cast<std::uint32_t>(bytes[offset + 3]);
}

std::string hex_u32(std::uint32_t value) {
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "0x%08x", value);
    return buffer;
}

/* Same policy as the other writers. */
void check_output_path(std::string const& path, std::string const& function) {
    if (path.empty()) {
        throw Error(function + ": the output path is empty");
    }
    std::filesystem::path const out(path);
    if (out.filename().empty()) {
        throw Error(function + ": " + path +
                    " names a directory, not a file to save to");
    }
    std::error_code error;
    if (std::filesystem::is_directory(out, error)) {
        throw Error(function + ": " + path + " is a directory");
    }
    std::filesystem::path const parent = out.parent_path();
    if (parent.empty()) {
        return;
    }
    error.clear();
    bool const parent_exists = std::filesystem::exists(parent, error);
    if (error || !parent_exists) {
        throw Error(function + ": cannot save to " + path + ": the directory " +
                    parent.string() + " does not exist");
    }
}

Bytes read_file_bytes(std::string const& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw Error("props edit: cannot re-open '" + path + "'");
    }
    input.seekg(0, std::ios::end);
    std::streamoff const length = input.tellg();
    input.seekg(0, std::ios::beg);
    Bytes bytes(length > 0 ? static_cast<std::size_t>(length) : 0);
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    return bytes;
}

/* The six directory records a committed pair updated (W35-0.4, RC-W7):
 * wave30-wbs-code 0x0240138b (4 -> 24 B) and 0x024013be (2 -> 6 B),
 * wave34-field-set 0x02401392 (equal size) and 0x024013c5 (8 -> 16 B),
 * wave35-calculate-unlinked 0x02400003 (the project finish, 4 B in place) and
 * 0x02400024 (a 2 B flag, in place).  Any other id that happens to exist would
 * be rewritten at a position and size no pair measured. */
bool record_measured(std::uint32_t id) {
    return id == 0x0240138Bu || id == 0x024013BEu || id == 0x02401392u ||
           id == 0x024013C5u || id == 0x02400003u || id == 0x02400024u;
}

/* W35b-4.G0-10 (R35G0-10): the size each measured record had BEFORE its pair's update (the
 * pairs' "from" side: 4 -> 24 B, 2 -> 6 B, equal 2 B, 8 -> 16 B, 4 B and 2 B in place).  The
 * payload pin below fixes the size AFTER; without this a record of any other old size grew
 * (or was rewritten) to the measured one, a transition no pair saw. */
std::size_t measured_old_length(std::uint32_t id) {
    switch (id) {
    case 0x0240138Bu: return 4;
    case 0x024013BEu: return 2;
    case 0x02401392u: return 2;
    case 0x024013C5u: return 8;
    case 0x02400003u: return 4;
    case 0x02400024u: return 2;
    default: return 0;
    }
}

/* RW35-W7: the payload each measured record takes.  A pair measured one value
 * per record (the project finish is a date, so only its 4-byte length is
 * measured); any other payload for a measured id is a shape no pair saw.
 * Returns the refusal clause, or an empty string when the payload is the
 * measured one. */
std::string payload_pin_clause(std::uint32_t id, Bytes const& payload) {
    static Bytes const mask = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,
                               0x2E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                               0x07, 0x00, 0x00, 0x00, 0x57, 0x00, 0x00, 0x00};
    static Bytes const prefix = {0x57, 0x00, 0x00, 0x00, 0x00, 0x00};
    static Bytes const field_flag = {0x01, 0x00};
    static Bytes const field_data = {0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
                                     0x00, 0x00, 0x33, 0x00, 0x40, 0x0B,
                                     0x64, 0x40, 0x40, 0x0F};
    static Bytes const pending_clear = {0x00, 0x00};
    if (id == 0x0240138Bu && payload != mask) {
        return "0x0240138b is measured as the 24-byte WBS mask ('.' separator, "
               "'W' prefix, wave30-wbs-code)";
    }
    if (id == 0x024013BEu && payload != prefix) {
        return "0x024013be is measured as the 6-byte WBS prefix ('W' as "
               "UTF-16LE and a NUL, wave30-wbs-code)";
    }
    if (id == 0x02401392u && payload != field_flag) {
        return "0x02401392 is measured as 01 00 (wave34-field-set)";
    }
    if (id == 0x024013C5u && payload != field_data) {
        return "0x024013c5 is measured as the 16-byte custom-field entry of "
               "wave34-field-set";
    }
    if (id == 0x02400003u && payload.size() != 4) {
        return "0x02400003 is measured as a 4-byte date "
               "(wave35-calculate-unlinked)";
    }
    if (id == 0x02400024u && payload != pending_clear) {
        return "0x02400024 is measured as 00 00 "
               "(wave35-calculate-unlinked)";
    }
    return std::string();
}

bool scalar_allowed(std::string const& stream, std::uint32_t tag) {
    if (stream == kPropsBackEnd) {
        return tag == kPropsTaskSizeTag || tag == kPropsTaskFreedTag ||
               tag == kPropsResourceCountTag ||
               tag == kPropsResourceSizeTag ||
               tag == kPropsCalendarCountTag ||
               tag == kPropsAssignmentCountTag ||
               tag == kPropsAssignmentFreedTag ||
               tag == kPropsAssignmentDirtyTag ||
               tag == kPropsAssignmentSizeTag;
    }
    return tag == kPropsTaskSizeTag; /* 214/Props: the CV_iew size only */
}

std::string scalar_whitelist(std::string const& stream) {
    if (stream == kPropsBackEnd) {
        return "0x01000100 (Task size), 0x01000002 (Task freed), "
               "0x02000001 (Resource count), 0x02000100 (Resource size), "
               "0x03000001 (Calendar count), 0x04000001 (Assignment count), "
               "0x04000002 (Assignment freed), 0x04008000 (Assignment dirty) "
               "and 0x04000100 (Assignment size)";
    }
    return "0x01000100 (the CV_iew Var2Data size)";
}

/* Byte offset of the VALUE of one 16-byte scalar property, located by its
 * BIG-endian tag.  Exactly one occurrence is the measured shape. */
std::size_t props_value_offset(Bytes const& props, std::uint32_t tag,
                               std::string const& function,
                               std::string const& stream) {
    std::size_t found = kNoIndex;
    for (std::size_t at = 0; at + kScalarBytes <= props.size(); ++at) {
        if (get_u32(props, at) != 4 || get_u32(props, at + 8) != 4) {
            continue;
        }
        if (get_big_u32(props, at + 4) != tag) {
            continue;
        }
        if (found != kNoIndex) {
            throw Error(function + ": " + stream +
                        " holds more than one scalar record for tag " +
                        hex_u32(tag) + "; the value is ambiguous, so it cannot "
                        "be kept");
        }
        found = at + 12;
    }
    if (found == kNoIndex) {
        throw Error(function + ": " + stream +
                    " has no scalar record for tag " + hex_u32(tag));
    }
    return found;
}

struct Record {
    std::size_t index = 0;
    std::size_t offset = 0; /* the record header */
    std::size_t length = 0; /* payload bytes */
    std::uint32_t id = 0;
    std::uint32_t kind = 0;
};

struct PropsState {
    Bytes bytes;
    std::size_t tail_offset = 0;
    std::vector<Record> records;
};

/* Read and validate one stream: header identity, the record walk, and the
 * tail start the walk finds.  Every measured record id is in the
 * 0x00400000 family, which is what ends the record area. */
PropsState load_props(CfbDocument const& doc, std::string const& stream,
                      std::string const& function) {
    if (!doc.has_stream(stream)) {
        throw Error(function + ": the document has no " + stream + " stream");
    }
    PropsState state;
    state.bytes = doc.read_stream(stream);
    Bytes const& bytes = state.bytes;
    if (bytes.size() < kPropsHeaderSize) {
        throw Error(function + ": " + stream + " is " +
                    std::to_string(bytes.size()) +
                    " bytes, shorter than its 16-byte header");
    }
    std::uint32_t const size_word =
        static_cast<std::uint32_t>(bytes.size() - 4);
    if (get_u32(bytes, 0) != size_word || get_u32(bytes, 4) != size_word) {
        throw Error(function + ": " + stream + " size words are " +
                    hex_u32(get_u32(bytes, 0)) + " and " +
                    hex_u32(get_u32(bytes, 4)) + ", not stream size - 4 (" +
                    hex_u32(size_word) + "); refusing to edit a damaged stream");
    }
    if (get_u32(bytes, 8) != kPropsFormatWord) {
        throw Error(function + ": " + stream + " format word is not " +
                    hex_u32(kPropsFormatWord));
    }
    std::size_t pos = kPropsHeaderSize;
    while (pos + kRecordHeaderSize <= bytes.size()) {
        std::uint32_t const id = get_u32(bytes, pos + 4);
        if ((id & 0x00FF0000u) != kRecordIdFamily) {
            break;
        }
        std::uint32_t const length = get_u32(bytes, pos);
        if (length > bytes.size() - pos - kRecordHeaderSize) {
            throw Error(function + ": " + stream + " record " +
                        std::to_string(state.records.size()) + " at " +
                        std::to_string(pos) + " declares " +
                        std::to_string(length) +
                        " payload bytes and runs past the stream end");
        }
        Record record;
        record.index = state.records.size();
        record.offset = pos;
        record.length = length;
        record.id = id;
        record.kind = get_u32(bytes, pos + 8);
        state.records.push_back(record);
        pos += kRecordHeaderSize + length;
    }
    state.tail_offset = pos;
    return state;
}

PropsRecordEdit const* find_record_edit(PropsEdit const& edit,
                                        std::uint32_t id) {
    for (PropsRecordEdit const& candidate : edit.records) {
        if (candidate.id == id) {
            return &candidate;
        }
    }
    return nullptr;
}

bool record_payload_matches(PropsState const& state, Record const& record,
                            std::vector<std::uint8_t> const& payload) {
    return std::equal(payload.begin(), payload.end(),
                      state.bytes.begin() +
                          static_cast<std::ptrdiff_t>(record.offset + 12));
}

void prove_only_owned_streams_changed(CfbDocument const& opened,
                                      CfbDocument const& reread,
                                      std::string const& stream,
                                      std::string const& function) {
    for (std::string const& path : opened.stream_paths()) {
        if (!reread.has_stream(path)) {
            throw Error(function + ": the written file lost the " + path +
                        " stream");
        }
    }
    for (std::string const& path : reread.stream_paths()) {
        if (!opened.has_stream(path)) {
            throw Error(function + ": the written file gained the " + path +
                        " stream");
        }
        if (path == stream || path == kEditMarkerStream) {
            continue;
        }
        if (opened.read_stream(path) != reread.read_stream(path)) {
            throw Error(function + ": " + path +
                        " changed; this edit owns only " + stream +
                        " and the Props14 marker");
        }
    }
}

} /* namespace */

PropsEditWrite save_props_edit(CfbDocument& doc, PropsEdit const& edit,
                               std::string const& path) {
    std::string const function = "props edit";
    std::string const stream = normalise_path(edit.stream);
    if (stream != kPropsBackEnd && stream != kPropsFrontEnd) {
        throw Error(function + ": '" + edit.stream +
                    "' is not a stream this writer has measured: only " +
                    kPropsBackEnd + " and " + kPropsFrontEnd +
                    " are enabled (the per-table Props registries and every "
                    "other stream are refused)");
    }
    check_output_path(path, function);
    if (edit.records.empty() && edit.scalars.empty()) {
        throw Error(function + ": no record and no scalar edit was requested");
    }
    if (stream == kPropsFrontEnd && !edit.records.empty()) {
        throw Error(function + ": no measured pair changes a 214/Props "
                    "record; the stream's only measured edit is the CV_iew "
                    "Var2Data size scalar, so a record edit is refused");
    }

    /* Validate the request before the document is read: duplicates, a missing
     * record (the unmeasured insert), a kind change, a shrink and a no-op are
     * all refused up front. */
    for (std::size_t i = 0; i < edit.records.size(); ++i) {
        for (std::size_t j = i + 1; j < edit.records.size(); ++j) {
            if (edit.records[i].id == edit.records[j].id) {
                throw Error(function + ": the request edits record " +
                            hex_u32(edit.records[i].id) +
                            " twice; one update per record per save");
            }
        }
    }
    for (std::size_t i = 0; i < edit.scalars.size(); ++i) {
        if (!scalar_allowed(stream, edit.scalars[i].tag)) {
            throw Error(function + ": scalar tag " +
                        hex_u32(edit.scalars[i].tag) + " is not measured on " +
                        stream + "; the measured tags are " +
                        scalar_whitelist(stream));
        }
        for (std::size_t j = i + 1; j < edit.scalars.size(); ++j) {
            if (edit.scalars[i].tag == edit.scalars[j].tag) {
                throw Error(function + ": the request edits scalar tag " +
                            hex_u32(edit.scalars[i].tag) +
                            " twice; one update per tag per save");
            }
        }
    }

    CfbDocument const opened = doc;
    PropsState const state = load_props(doc, stream, function);

    std::vector<PropsRecordWrite> planned;
    for (PropsRecordEdit const& edit_record : edit.records) {
        std::size_t found = kNoIndex;
        std::size_t count = 0;
        for (Record const& record : state.records) {
            if (record.id == edit_record.id) {
                found = record.index;
                ++count;
            }
        }
        if (count == 0) {
            throw Error(function + ": record " + hex_u32(edit_record.id) +
                        " is not in " + stream + "; inserting a record is not "
                        "a measured shape (wave30-baseline keeps its 161 "
                        "records and grows only an undecoded +110-byte tail), "
                        "so the insert is refused");
        }
        if (count > 1) {
            throw Error(function + ": " + stream + " holds " +
                        std::to_string(count) + " records with id " +
                        hex_u32(edit_record.id) + "; refusing to choose");
        }
        if (!record_measured(edit_record.id)) {
            throw Error(function + ": record " + hex_u32(edit_record.id) +
                        " is not one a committed pair updated; the measured "
                        "ids are 0x0240138b and 0x024013be (wave30-wbs-code) "
                        "and 0x02401392 and 0x024013c5 (wave34-field-set), "
                        "and 0x02400003 and 0x02400024 "
                        "(wave35-calculate-unlinked), so this update is "
                        "refused");
        }
        Record const& record = state.records[found];
        if (record.kind != edit_record.kind) {
            throw Error(function + ": record " + hex_u32(edit_record.id) +
                        " is kind " + std::to_string(record.kind) + ", not " +
                        std::to_string(edit_record.kind) +
                        "; every measured update keeps the kind, so a kind "
                        "change is refused");
        }
        if (edit_record.payload.size() < record.length) {
            throw Error(function + ": record " + hex_u32(edit_record.id) +
                        " would shrink from " +
                        std::to_string(record.length) + " to " +
                        std::to_string(edit_record.payload.size()) +
                        " payload bytes; no measured pair shrinks a record");
        }
        if (edit_record.payload.size() == record.length &&
            record_payload_matches(state, record, edit_record.payload)) {
            throw Error(function + ": record " + hex_u32(edit_record.id) +
                        " would keep its payload; that is not an edit");
        }
        if (record.length != measured_old_length(edit_record.id)) {
            throw Error(function + ": record " + hex_u32(edit_record.id) + " holds " +
                        std::to_string(record.length) + " payload bytes, not the " +
                        std::to_string(measured_old_length(edit_record.id)) +
                        " its pair updated from; that transition is no pair's, so this "
                        "update is refused");
        }
        std::string const pin =
            payload_pin_clause(edit_record.id, edit_record.payload);
        if (!pin.empty()) {
            throw Error(function + ": record " + hex_u32(edit_record.id) +
                        " is given a payload no pair measured: " + pin +
                        ", so this update is refused");
        }
        PropsRecordWrite write;
        write.id = edit_record.id;
        write.kind = edit_record.kind;
        write.old_offset = record.offset;
        write.old_payload_bytes = record.length;
        write.new_payload_bytes = edit_record.payload.size();
        planned.push_back(write);
    }

    std::vector<std::uint32_t> scalar_tags;
    std::vector<std::uint32_t> scalar_values;
    std::vector<std::size_t> scalar_value_at;
    for (PropsScalarEdit const& scalar : edit.scalars) {
        std::size_t const value_at =
            props_value_offset(state.bytes, scalar.tag, function, stream);
        if (value_at < 12 || value_at - 12 < state.tail_offset) {
            throw Error(function + ": scalar tag " + hex_u32(scalar.tag) +
                        " sits at " + std::to_string(value_at - 12) +
                        ", inside the record area; the measured scalars live "
                        "in the tail");
        }
        if (get_u32(state.bytes, value_at) == scalar.value) {
            throw Error(function + ": scalar tag " + hex_u32(scalar.tag) +
                        " would keep the value " +
                        std::to_string(scalar.value) + "; that is not an edit");
        }
        scalar_tags.push_back(scalar.tag);
        scalar_values.push_back(scalar.value);
        scalar_value_at.push_back(value_at);
    }

    /* Build the new stream: the header, the records in their existing order
     * with the edited payloads, and the tail moved whole with only the edited
     * value words rewritten. */
    std::size_t new_size = kPropsHeaderSize + (state.bytes.size() - state.tail_offset);
    for (Record const& record : state.records) {
        PropsRecordEdit const* const replacement =
            find_record_edit(edit, record.id);
        new_size += kRecordHeaderSize +
                    (replacement ? replacement->payload.size() : record.length);
    }
    if (new_size > 0xFFFFFFFFu) {
        throw Error(function + ": the stream would pass the 32-bit size word");
    }
    Bytes out;
    out.reserve(new_size);
    out.insert(out.end(), state.bytes.begin(),
               state.bytes.begin() + kPropsHeaderSize);
    for (Record const& record : state.records) {
        PropsRecordEdit const* const replacement =
            find_record_edit(edit, record.id);
        std::size_t const length =
            replacement ? replacement->payload.size() : record.length;
        std::size_t const new_offset = out.size();
        append_u32(out, static_cast<std::uint32_t>(length));
        append_u32(out, record.id);
        append_u32(out, record.kind);
        if (replacement) {
            out.insert(out.end(), replacement->payload.begin(),
                       replacement->payload.end());
            for (PropsRecordWrite& write : planned) {
                if (write.id == record.id) {
                    write.new_offset = new_offset;
                }
            }
        } else {
            out.insert(out.end(),
                       state.bytes.begin() +
                           static_cast<std::ptrdiff_t>(record.offset + 12),
                       state.bytes.begin() + static_cast<std::ptrdiff_t>(
                                                 record.offset + 12 +
                                                 record.length));
        }
    }
    Bytes tail(state.bytes.begin() +
                   static_cast<std::ptrdiff_t>(state.tail_offset),
               state.bytes.end());
    for (std::size_t i = 0; i < scalar_value_at.size(); ++i) {
        put_u32(tail, scalar_value_at[i] - state.tail_offset,
                scalar_values[i]);
    }
    out.insert(out.end(), tail.begin(), tail.end());
    put_u32(out, 0, static_cast<std::uint32_t>(out.size() - 4));
    put_u32(out, 4, static_cast<std::uint32_t>(out.size() - 4));
    if (out.size() != new_size) {
        throw Error(function + ": the built stream is " +
                    std::to_string(out.size()) + " bytes, expected " +
                    std::to_string(new_size));
    }

    CfbDocument work = doc;
    work.replace_stream(stream, out);
    EditMarker const marker = write_edit_marker(work);
    if (marker.record_present && marker.after != kEditMarkerAfterSave) {
        throw Error(function + ": the staged document's Props14 marker is " +
                    std::to_string(marker.after) + ", not 1");
    }

    /* The in-memory stream must satisfy the same geometry. */
    PropsState const staged = load_props(work, stream, function);
    if (staged.bytes != out || staged.records.size() != state.records.size()) {
        throw Error(function + ": the staged stream does not hold the built "
                    "bytes");
    }

    Bytes const intended = work.to_bytes();
    work.save(path);
    Bytes const on_disk = read_file_bytes(path);
    if (on_disk != intended) {
        throw Error(function + ": " + path + " holds " +
                    std::to_string(on_disk.size()) + " bytes, but the document "
                    "built " + std::to_string(intended.size()) +
                    "; the write did not land");
    }
    CfbDocument reread = CfbDocument::from_file(path);
    if (reread.to_bytes() != on_disk) {
        throw Error(function + ": the written file does not re-emit "
                    "byte-identically");
    }
    if (!reread.check().empty()) {
        throw Error(function + ": the written container is inconsistent: " +
                    reread.check());
    }
    PropsState const after = load_props(reread, stream, function);

    /* Header, record count, order, offsets, payloads. */
    if (after.bytes.size() != out.size()) {
        throw Error(function + ": the written stream is " +
                    std::to_string(after.bytes.size()) + " bytes, expected " +
                    std::to_string(out.size()));
    }
    if (get_u32(after.bytes, 0) != after.bytes.size() - 4 ||
        get_u32(after.bytes, 4) != after.bytes.size() - 4) {
        throw Error(function + ": the written size words are not stream "
                    "size - 4");
    }
    if (get_u32(after.bytes, 8) != get_u32(state.bytes, 8)) {
        throw Error(function + ": the written format word moved");
    }
    if (get_u32(after.bytes, 12) != get_u32(state.bytes, 12)) {
        throw Error(function + ": the word at header +12 moved; no measured "
                    "record edit changes it");
    }
    if (after.records.size() != state.records.size()) {
        throw Error(function + ": the written stream holds " +
                    std::to_string(after.records.size()) + " records, expected " +
                    std::to_string(state.records.size()) +
                    "; an update must not insert or remove one");
    }
    std::size_t shifted = 0;
    for (std::size_t i = 0; i < state.records.size(); ++i) {
        Record const& before = state.records[i];
        Record const& now = after.records[i];
        if (now.id != before.id || now.kind != before.kind) {
            throw Error(function + ": record " + std::to_string(i) +
                        " is not the control's id/kind (the order changed)");
        }
        if (now.offset != before.offset + shifted) {
            throw Error(function + ": record " + hex_u32(now.id) +
                        " sits at " + std::to_string(now.offset) + ", expected " +
                        std::to_string(before.offset + shifted));
        }
        PropsRecordEdit const* const replacement = find_record_edit(edit, now.id);
        if (replacement) {
            if (now.length != replacement->payload.size() ||
                !std::equal(replacement->payload.begin(),
                            replacement->payload.end(),
                            after.bytes.begin() + static_cast<std::ptrdiff_t>(
                                                      now.offset + 12))) {
                throw Error(function + ": the written record " +
                            hex_u32(now.id) +
                            " does not hold the requested payload");
            }
            shifted += replacement->payload.size() - before.length;
        } else if (now.length != before.length ||
                   !std::equal(state.bytes.begin() +
                                   static_cast<std::ptrdiff_t>(
                                       before.offset + 12),
                               state.bytes.begin() + static_cast<std::ptrdiff_t>(
                                                         before.offset + 12 +
                                                         before.length),
                               after.bytes.begin() + static_cast<std::ptrdiff_t>(
                                                         now.offset + 12))) {
            throw Error(function + ": the written record " + hex_u32(now.id) +
                        " changed but was not edited");
        }
    }

    /* The tail: the control's bytes moved whole, with only the edited value
     * words different. */
    std::size_t const old_tail_bytes = state.bytes.size() - state.tail_offset;
    if (after.bytes.size() - after.tail_offset != old_tail_bytes) {
        throw Error(function + ": the written tail is " +
                    std::to_string(after.bytes.size() - after.tail_offset) +
                    " bytes, expected " + std::to_string(old_tail_bytes) +
                    "; a scalar edit must not resize the tail");
    }
    for (std::size_t i = 0; i < old_tail_bytes; ++i) {
        bool owned = false;
        for (std::size_t s = 0; s < scalar_value_at.size(); ++s) {
            std::size_t const value = scalar_value_at[s] - state.tail_offset;
            if (i >= value && i < value + 4) {
                owned = true;
            }
        }
        if (!owned &&
            after.bytes[after.tail_offset + i] != state.bytes[state.tail_offset + i]) {
            throw Error(function + ": the written tail changed at byte " +
                        std::to_string(after.tail_offset + i) +
                        ", which no requested scalar owns");
        }
    }
    for (std::size_t s = 0; s < scalar_value_at.size(); ++s) {
        std::size_t const value =
            after.tail_offset + (scalar_value_at[s] - state.tail_offset);
        if (get_u32(after.bytes, value) != scalar_values[s]) {
            throw Error(function + ": the written scalar tag " +
                        hex_u32(scalar_tags[s]) + " is " +
                        std::to_string(get_u32(after.bytes, value)) +
                        ", expected " + std::to_string(scalar_values[s]));
        }
    }
    prove_only_owned_streams_changed(opened, reread, stream, function);

    PropsEditWrite written;
    written.stream = stream;
    written.records_before = state.records.size();
    written.records_after = after.records.size();
    written.stream_bytes_before = state.bytes.size();
    written.stream_bytes_after = after.bytes.size();
    written.tail_offset_before = state.tail_offset;
    written.tail_offset_after = after.tail_offset;
    written.scalars_updated = scalar_value_at.size();
    written.record_writes = planned;

    doc = std::move(reread);
    return written;
}

} /* namespace mppwrite */
