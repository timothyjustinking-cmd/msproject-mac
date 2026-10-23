#include "viewstate.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <sstream>
#include <utility>

namespace msproject::viewstate {
namespace {

std::uint16_t read_u16(std::span<const std::uint8_t> data, std::size_t offset) {
    if (offset + 2 > data.size()) {
        return 0;
    }
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[offset]) |
                                      (static_cast<std::uint16_t>(data[offset + 1]) << 8u));
}

std::uint32_t read_u32(std::span<const std::uint8_t> data, std::size_t offset) {
    if (offset + 4 > data.size()) {
        return 0;
    }
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8u) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16u) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24u);
}

std::string hex32(std::uint32_t value) {
    char buffer[11] = {};
    std::snprintf(buffer, sizeof(buffer), "0x%08x", value);
    return std::string(buffer);
}

std::string byte_hex(const std::uint8_t* data, std::size_t count) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(count * 2);
    for (std::size_t i = 0; i < count; ++i) {
        out.push_back(digits[(data[i] >> 4u) & 0x0fu]);
        out.push_back(digits[data[i] & 0x0fu]);
    }
    return out;
}

bool meta_header_ok(std::span<const std::uint8_t> data,
                    std::uint32_t& magic,
                    ParseError* error,
                    std::string_view what) {
    if (data.size() < kFixedMetaHeaderSize) {
        if (error != nullptr) {
            error->message = std::string(what) + " is shorter than 16 bytes";
            error->offset = data.size();
        }
        return false;
    }
    magic = read_u32(data, 0);
    if (magic != kMetaMagic) {
        if (error != nullptr) {
            error->message = std::string(what) + " has magic " + hex32(magic) +
                             ", expected 0xfadfadba";
            error->offset = 0;
        }
        return false;
    }
    return true;
}

bool write_fail(WriteError* error, std::string message) {
    if (error != nullptr) {
        error->message = std::move(message);
    }
    return false;
}

void put_u16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
}

void put_u32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16u) & 0xffu);
    bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24u) & 0xffu);
}

void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16u) & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 24u) & 0xffu));
}

// ---------------------------------------------------------------------------
// Wave 17 (F1) helpers.
// ---------------------------------------------------------------------------

// True only for the printable ASCII this module can read back: the decoder
// stops at the first non-printable UTF-16 unit, so writing anything else would
// not round-trip.
bool printable_ascii(std::string_view text, std::string& why) {
    if (text.empty()) {
        why = "name is empty";
        return false;
    }
    for (const char ch : text) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        if (byte < 0x20 || byte > 0x7e) {
            why = "name must be printable ASCII (the decoder reads ASCII names only)";
            return false;
        }
    }
    return true;
}

// UTF-16LE bytes for `text`, optionally with a terminating NUL unit.
std::vector<std::uint8_t> utf16_bytes(std::string_view text, bool nul_terminated) {
    std::vector<std::uint8_t> out(text.size() * 2 + (nul_terminated ? 2u : 0u), 0);
    for (std::size_t i = 0; i < text.size(); ++i) {
        out[i * 2] = static_cast<std::uint8_t>(text[i]);
    }
    return out;
}

std::string strip_accelerators(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char ch : text) {
        if (ch != '&') {
            out.push_back(ch);
        }
    }
    return out;
}

// Inference, not a stored flag (wave 15): a combination view stores other view
// names in 0x2240000a/0x2240000b.  Mark a view when its table-or-top matches the
// name or display name of another view in the same collection.
void recompute_combination_flags(ViewState& state) {
    for (auto& view : state.views) {
        view.looks_like_combination = false;
    }
    for (auto& view : state.views) {
        if (view.table_or_top.empty()) {
            continue;
        }
        const std::string needle = strip_accelerators(view.table_or_top);
        for (const auto& other : state.views) {
            if (&other == &view) {
                continue;
            }
            if (strip_accelerators(other.name) == needle ||
                (!other.display_name.empty() &&
                 strip_accelerators(other.display_name) == needle)) {
                view.looks_like_combination = true;
                break;
            }
        }
    }
}

std::size_t pad_to_even(std::size_t value) {
    return (value & 1u) != 0 ? value + 1 : value;
}

// The name a view/table list shows for a table: the measured second-caption
// (display_caption) when it decodes, else the row caption.  CEdl stores this
// form ("Entry" for a "&Entry" caption; "W14 Entry C" when the caption has no
// accelerator).
std::string table_selector_name(const TableDef& table) {
    return table.display_caption.empty() ? table.caption : table.display_caption;
}

// Value 0 when the entry cannot be read.
std::size_t block_index_for_offset(const ViewState& state, std::uint32_t offset) {
    for (std::size_t i = 0; i < state.blocks.size(); ++i) {
        if (state.blocks[i].offset == offset) {
            return i;
        }
    }
    return state.blocks.size();
}

// Replaces the payload of the block whose length word is at `block_offset`,
// fixes the length word, shifts every later block / VarMeta offset / decoded
// block offset by the size delta and refreshes the parsed stream sizes.  The
// caller has already validated `payload` and any decoded view of it.
bool replace_block_payload(ViewState& state,
                           std::uint32_t block_offset,
                           std::vector<std::uint8_t> payload,
                           WriteError* error) {
    const std::size_t index = block_index_for_offset(state, block_offset);
    if (index == state.blocks.size()) {
        return write_fail(error, "block at offset " + std::to_string(block_offset) +
                                     " is not in the Var2Data walk");
    }
    const std::size_t start = static_cast<std::size_t>(block_offset) + 4;
    const std::size_t old_length = state.blocks[index].payload_length;
    if (start > state.var2_data.size() || old_length > state.var2_data.size() - start) {
        return write_fail(error, "block at offset " + std::to_string(block_offset) +
                                     " runs past Var2Data");
    }
    if (payload.size() > 0xffffffffu) {
        return write_fail(error, "new block payload does not fit a u32 length word");
    }
    const std::int64_t delta = static_cast<std::int64_t>(payload.size()) -
                               static_cast<std::int64_t>(old_length);
    state.var2_data.erase(state.var2_data.begin() + static_cast<std::ptrdiff_t>(start),
                          state.var2_data.begin() +
                              static_cast<std::ptrdiff_t>(start + old_length));
    state.var2_data.insert(state.var2_data.begin() + static_cast<std::ptrdiff_t>(start),
                           payload.begin(), payload.end());
    put_u32(state.var2_data, block_offset, static_cast<std::uint32_t>(payload.size()));
    state.blocks[index].payload_length = static_cast<std::uint32_t>(payload.size());
    state.block_refs[index].payload_length = state.blocks[index].payload_length;
    if (delta != 0) {
        for (auto& block : state.blocks) {
            if (block.offset > block_offset) {
                block.offset = static_cast<std::uint32_t>(
                    static_cast<std::int64_t>(block.offset) + delta);
            }
        }
        for (auto& entry : state.var_meta.entries) {
            if (entry.offset > block_offset) {
                entry.offset = static_cast<std::uint32_t>(
                    static_cast<std::int64_t>(entry.offset) + delta);
            }
        }
        for (auto& table : state.tables) {
            if (table.block_offset > block_offset) {
                table.block_offset = static_cast<std::uint32_t>(
                    static_cast<std::int64_t>(table.block_offset) + delta);
            }
        }
        for (auto& view : state.views) {
            if (view.main_block_offset > block_offset) {
                view.main_block_offset = static_cast<std::uint32_t>(
                    static_cast<std::int64_t>(view.main_block_offset) + delta);
            }
        }
        for (auto& embedded : state.embedded_props) {
            if (embedded.block_offset > block_offset) {
                embedded.block_offset = static_cast<std::uint32_t>(
                    static_cast<std::int64_t>(embedded.block_offset) + delta);
            }
        }
        for (auto& gap : state.var2.gaps) {
            if (gap.offset > block_offset) {
                gap.offset = static_cast<std::uint32_t>(
                    static_cast<std::int64_t>(gap.offset) + delta);
            }
        }
    }
    // block_refs are index-aligned with the block walk (built that way by the
    // parser); keep their offsets in step after the shift.
    for (std::size_t i = 0; i < state.block_refs.size() && i < state.blocks.size(); ++i) {
        state.block_refs[i].offset = state.blocks[i].offset;
    }
    state.var2.blocks = state.blocks;
    state.var2.size = static_cast<std::uint32_t>(state.var2_data.size());
    state.var_meta.var2_size = state.var2.size;
    return true;
}

// Decodes the CV_iew main block named by `view.main_block_offset` into
// view.records / names / options.  Leaves view untouched and returns false when
// the item walk does not validate (the caller then keeps the block opaque).
bool decode_view_main(const ViewState& state, ViewDef& view) {
    const std::size_t index = block_index_for_offset(state, view.main_block_offset);
    if (index == state.blocks.size()) {
        return false;
    }
    const std::span<const std::uint8_t> payload = state.block_bytes(state.blocks[index]);
    std::vector<ViewRecord> records;
    if (!parse_view_records(payload, records, nullptr)) {
        return false;
    }
    view.main_decoded = true;
    view.main_payload_length = state.blocks[index].payload_length;
    view.records = std::move(records);
    view.display_name.clear();
    view.table_or_top.clear();
    view.filter_or_bottom.clear();
    view.group_name.clear();
    view.options_decoded = false;
    view.page_margins_thousandths = {};
    view.page_margin_extra = 0;
    for (const auto& record : view.records) {
        const std::size_t base = record.record_offset + kViewItemHeaderSize;
        if (record.id == kViewNameKey) {
            view.display_name = utf16_ascii(payload, base, record.payload_length / 2);
        } else if (record.id == kViewTableKey) {
            view.table_or_top = utf16_ascii(payload, base, record.payload_length / 2);
        } else if (record.id == kViewFilterKey) {
            view.filter_or_bottom = utf16_ascii(payload, base, record.payload_length / 2);
        } else if (record.id == kViewGroupKey) {
            view.group_name = utf16_ascii(payload, base, record.payload_length / 2);
        } else if (record.id == kViewOptionsKey && record.payload_length == kViewOptionsSize) {
            view.options_decoded = true;
            for (std::size_t margin = 0; margin < view.page_margins_thousandths.size();
                 ++margin) {
                view.page_margins_thousandths[margin] =
                    read_u32(payload, base + margin * 4);
            }
            view.page_margin_extra = read_u32(payload, base + 16);
        }
    }
    return true;
}

// Decodes one CEdl Props block into its typed form.  Returns false (with a
// warning posted by the caller) when the payload is not a Props document.
bool decode_edl_block(const ViewState& state, const VarBlock& block, EmbeddedProps& out) {
    out = EmbeddedProps{};
    out.block_offset = block.offset;
    out.payload_length = block.payload_length;
    props::PropsDocument document;
    props::ParseError props_error;
    if (!props::parse_props(state.block_bytes(block), document, &props_error)) {
        return false;
    }
    out.document = std::move(document);
    for (const auto& record : out.document.records) {
        EdlRecord typed;
        typed.offset = record.offset;
        typed.id = record.id;
        typed.kind = record.kind;
        typed.payload_length = record.payload_length;
        typed.preview = preview_bytes(out.document.payload(record));
        out.records.push_back(std::move(typed));
    }
    const auto decode_name = [&out](std::uint32_t id, std::string& target) {
        const props::PropsRecord* record = out.document.find(id);
        if (record == nullptr || record->kind != 1u) {
            return false;
        }
        target = utf16_ascii(out.document.payload(*record), 0, record->payload_length / 2);
        return !target.empty();
    };
    out.view_name_decoded = decode_name(kEdlViewNameKey, out.view_name);
    out.table_name_decoded = decode_name(kEdlTableNameKey, out.table_name);
    const props::PropsRecord* field_list = out.document.find(kEdlFieldListKey);
    if (field_list != nullptr && field_list->payload_length % 4u == 0) {
        const std::span<const std::uint8_t> data = out.document.payload(*field_list);
        std::vector<std::uint32_t> ids;
        bool valid = true;
        for (std::size_t at = 0; at + 4 <= data.size(); at += 4) {
            ids.push_back(read_u32(data, at));
        }
        if (!ids.empty() && ids.back() == kNoFieldId) {
            out.field_list_terminated = true;
            ids.pop_back();
        }
        for (const std::uint32_t id : ids) {
            const std::uint32_t kind = id & 0xffff0000u;
            if (kind != 0x0b400000u && kind != 0x0c400000u) {
                valid = false;
            }
        }
        if (valid) {
            out.field_list_decoded = true;
            out.field_ids = std::move(ids);
        }
    }
    return true;
}

}  // namespace

std::span<const std::uint8_t> ViewState::block_bytes(const VarBlock& block) const {
    if (block.offset + 4 > var2_data.size()) {
        return {};
    }
    const std::size_t start = static_cast<std::size_t>(block.offset) + 4;
    const std::size_t length = std::min<std::size_t>(block.payload_length,
                                                     var2_data.size() - start);
    return std::span<const std::uint8_t>(var2_data.data() + start, length);
}

const ViewBlockRef* ViewState::find_block_ref(std::uint32_t uid, std::uint32_t key) const {
    for (const auto& ref : block_refs) {
        if (ref.uid == uid && ref.key == key) {
            return &ref;
        }
    }
    return nullptr;
}

const RowRecord* RowRecordSet::find(std::uint32_t id) const {
    for (const auto& record : records) {
        if (record.id == id) {
            return &record;
        }
    }
    return nullptr;
}

const EdlRecord* EmbeddedProps::find(std::uint32_t id) const {
    for (const auto& record : records) {
        if (record.id == id) {
            return &record;
        }
    }
    return nullptr;
}

ViewState* ViewState::find_related(std::string_view wanted_klass) {
    for (ViewState* candidate : related) {
        if (candidate != nullptr && candidate->klass == wanted_klass) {
            return candidate;
        }
    }
    return nullptr;
}

const ViewState* ViewState::find_related(std::string_view wanted_klass) const {
    for (const ViewState* candidate : related) {
        if (candidate != nullptr && candidate->klass == wanted_klass) {
            return candidate;
        }
    }
    return nullptr;
}

bool is_known_class(std::string_view klass) {
    return klass == "CTable" || klass == "CV_iew" || klass == "CEdl" ||
           klass == "CFilter" || klass == "CGrouping" || klass == "CDrawing" ||
           klass == "CMap" || klass == "CReport";
}

std::string_view block_kind(std::string_view klass, std::uint32_t key) {
    if (key == kColumnBlockKey) {
        if (klass == "CTable") {
            return "table-columns";
        }
        if (klass == "CV_iew") {
            return "view-state";
        }
        if (klass == "CEdl") {
            return "props-document";
        }
        if (klass == "CFilter") {
            return "filter-definition";
        }
        if (klass == "CGrouping") {
            return "grouping-definition";
        }
        return "opaque";
    }
    if (key == kViewNotesKey) {
        return "view-notes";
    }
    if (key == kTimelineViewKey) {
        return "timeline-view";
    }
    return "opaque";
}

std::string_view view_item_kind(std::uint32_t id) {
    switch (id) {
        case kViewNameKey:
            return "view-name";
        case kViewTableKey:
            return "table-or-top-pane";
        case kViewFilterKey:
            return "filter-or-bottom-pane";
        case kViewGroupKey:
            return "grouping-name";
        case kViewOptionsKey:
            return "view-options-970";
        case kViewFormatKey:
            return "gantt-bar-styles";
        case kTimelineViewKey:
            return "timeline-xml";
        case kViewNotesKey:
            return "view-notes";
        case kViewFlagsKey:
            return "view-flags-37";
        case kViewCalendarAKey:
            return "calendar-6";
        case kViewCalendarBKey:
            return "calendar-4";
        case kViewWindowStateKey:
            return "window-state-20";
        case kViewWindowState2Key:
            return "window-state-20b";
        case kViewAppliedGroupKey:
            return "applied-grouping-cache";
        case kViewCriteriaWordKey:
            return "criteria-word-2";
        case kViewAppliedStateKey:
            return "applied-state-cache-12";
        default:
            return "opaque";
    }
}

std::string utf16_ascii(std::span<const std::uint8_t> data,
                        std::size_t offset,
                        std::size_t max_units) {
    std::string out;
    for (std::size_t unit = 0; unit < max_units; ++unit) {
        const std::size_t at = offset + unit * 2;
        if (at + 2 > data.size()) {
            break;
        }
        const std::uint16_t value = read_u16(data, at);
        if (value == 0) {
            break;
        }
        if (value < 0x20 || value > 0x7e) {
            break;
        }
        out.push_back(static_cast<char>(value));
    }
    return out;
}

std::string preview_bytes(std::span<const std::uint8_t> data, std::size_t limit) {
    std::size_t ascii_start = 0;
    std::size_t ascii_len = 0;
    for (std::size_t i = 0; i < data.size(); ++i) {
        const std::uint8_t value = data[i];
        if (value >= 0x20 && value <= 0x7e) {
            if (ascii_len == 0) {
                ascii_start = i;
            }
            ++ascii_len;
            if (ascii_len >= limit) {
                break;
            }
        } else {
            ascii_len = 0;
        }
    }
    if (ascii_len >= 4) {
        const std::size_t count = std::min<std::size_t>(ascii_len, limit);
        return std::string(reinterpret_cast<const char*>(data.data() + ascii_start), count);
    }

    std::size_t utf16_start = 0;
    std::size_t utf16_len = 0;
    for (std::size_t i = 0; i + 1 < data.size(); i += 2) {
        const bool printable = data[i] >= 0x20 && data[i] <= 0x7e && data[i + 1] == 0;
        if (printable) {
            if (utf16_len == 0) {
                utf16_start = i;
            }
            ++utf16_len;
            if (utf16_len >= limit) {
                break;
            }
        } else {
            utf16_len = 0;
        }
    }
    if (utf16_len >= 4) {
        return utf16_ascii(data, utf16_start, std::min<std::size_t>(utf16_len, limit));
    }

    const std::size_t count = std::min<std::size_t>(data.size(), 12);
    return byte_hex(data.data(), count);
}

bool parse_fixed_meta(std::span<const std::uint8_t> data,
                      FixedMetaStream& out,
                      ParseError* error) {
    out = FixedMetaStream{};
    std::uint32_t magic = 0;
    if (!meta_header_ok(data, magic, error, "FixedMeta")) {
        return false;
    }
    out.magic = magic;
    out.flags = read_u32(data, 4);
    out.declared_count = read_u32(data, 8);
    out.fixed_data_size = read_u32(data, 12);

    const std::size_t available = data.size() - kFixedMetaHeaderSize;
    if (out.declared_count == 0) {
        out.entry_size = 0;
        if (available != 0) {
            if (error != nullptr) {
                error->message = "FixedMeta declares 0 entries but holds " +
                                 std::to_string(available) + " trailing bytes";
                error->offset = kFixedMetaHeaderSize;
            }
            return false;
        }
        return true;
    }
    if (available % out.declared_count != 0) {
        if (error != nullptr) {
            error->message = "FixedMeta size is not 16 + declared_count * entry_size (" +
                             std::to_string(out.declared_count) + " entries, " +
                             std::to_string(available) + " bytes)";
            error->offset = kFixedMetaHeaderSize;
        }
        return false;
    }
    out.entry_size = static_cast<std::uint32_t>(available / out.declared_count);
    if (out.entry_size < 8) {
        if (error != nullptr) {
            error->message = "FixedMeta entry size " + std::to_string(out.entry_size) +
                             " is too small to hold state/var_count/offset";
            error->offset = kFixedMetaHeaderSize;
        }
        return false;
    }
    out.entries.reserve(out.declared_count);
    for (std::uint32_t i = 0; i < out.declared_count; ++i) {
        FixedMetaEntry entry;
        entry.entry_offset = static_cast<std::uint32_t>(kFixedMetaHeaderSize) +
                             i * out.entry_size;
        const std::size_t base = entry.entry_offset;
        entry.state = read_u16(data, base);
        entry.var_count = read_u16(data, base + 2);
        entry.fixed_offset = read_u32(data, base + 4);
        entry.opaque_tail.assign(data.begin() + static_cast<std::ptrdiff_t>(base + 8),
                                 data.begin() + static_cast<std::ptrdiff_t>(
                                                     base + out.entry_size));
        out.entries.push_back(std::move(entry));
    }
    return true;
}

bool parse_var_meta(std::span<const std::uint8_t> data,
                    VarMetaStream& out,
                    ParseError* error) {
    out = VarMetaStream{};
    std::uint32_t magic = 0;
    if (!meta_header_ok(data, magic, error, "VarMeta")) {
        return false;
    }
    out.magic = magic;
    out.declared_count = read_u32(data, 8);
    out.var2_size = read_u32(data, 20);

    const std::size_t available = data.size() - kVarMetaHeaderSize;
    if (out.declared_count > available / 12) {
        if (error != nullptr) {
            error->message = "VarMeta declares " + std::to_string(out.declared_count) +
                             " entries but holds only " + std::to_string(available) + " bytes";
            error->offset = kVarMetaHeaderSize;
        }
        return false;
    }
    const std::size_t used = static_cast<std::size_t>(out.declared_count) * 12;
    out.trailing_bytes = static_cast<std::uint32_t>(available - used);
    out.entries.reserve(out.declared_count);
    for (std::uint32_t i = 0; i < out.declared_count; ++i) {
        const std::size_t base = kVarMetaHeaderSize + static_cast<std::size_t>(i) * 12;
        VarMetaEntry entry;
        entry.uid = read_u32(data, base);
        entry.offset = read_u32(data, base + 4);
        entry.key = read_u32(data, base + 8);
        out.entries.push_back(entry);
    }
    return true;
}

bool parse_var2(std::span<const std::uint8_t> data,
                Var2Stream& out,
                ParseError* error) {
    out = Var2Stream{};
    out.size = static_cast<std::uint32_t>(data.size());
    std::size_t offset = 0;
    while (offset < data.size()) {
        if (offset + 4 > data.size()) {
            if (error != nullptr) {
                error->message = "Var2Data has " + std::to_string(data.size() - offset) +
                                 " trailing byte(s) where a block length was expected";
                error->offset = offset;
            }
            return false;
        }
        const std::uint32_t length = read_u32(data, offset);
        if (length > data.size() - offset - 4) {
            if (error != nullptr) {
                error->message = "Var2Data block at " + std::to_string(offset) + " declares " +
                                 std::to_string(length) + " payload bytes but only " +
                                 std::to_string(data.size() - offset - 4) + " remain";
                error->offset = offset;
            }
            return false;
        }
        VarBlock block;
        block.offset = static_cast<std::uint32_t>(offset);
        block.payload_length = length;
        out.blocks.push_back(block);
        offset += 4 + static_cast<std::size_t>(length);
    }
    return true;
}

// Cache-tolerant `214/CEdl` walk (wave 18, F2).  See the header comment: a
// document may be followed by up to kEdlCacheMaxResidue stale bytes before the
// next document, and every candidate payload must parse as a Props document.
bool parse_var2_edl_cache(std::span<const std::uint8_t> data,
                          std::span<const VarMetaEntry> var_meta_entries,
                          Var2Stream& out,
                          ParseError* error) {
    out = Var2Stream{};
    out.size = static_cast<std::uint32_t>(data.size());
    out.cache_tolerant = true;
    const auto is_document = [data](std::size_t payload_offset, std::uint32_t length) {
        props::PropsDocument document;
        props::ParseError ignored;
        return props::parse_props(data.subspan(payload_offset, length), document, &ignored);
    };
    std::size_t offset = 0;
    while (offset < data.size()) {
        bool have_block = false;
        for (std::size_t skip = 0; skip <= kEdlCacheMaxResidue;
             skip += kEdlCacheResyncStep) {
            const std::size_t candidate = offset + skip;
            if (candidate + 4 > data.size()) {
                break;
            }
            const std::uint32_t length = read_u32(data, candidate);
            if (static_cast<std::size_t>(length) > data.size() - candidate - 4) {
                continue;
            }
            if (!is_document(candidate + 4, length)) {
                continue;
            }
            if (skip != 0) {
                Var2Gap gap;
                gap.offset = static_cast<std::uint32_t>(offset);
                gap.bytes.assign(data.begin() + static_cast<std::ptrdiff_t>(offset),
                                 data.begin() + static_cast<std::ptrdiff_t>(candidate));
                out.gaps.push_back(std::move(gap));
            }
            VarBlock block;
            block.offset = static_cast<std::uint32_t>(candidate);
            block.payload_length = length;
            out.blocks.push_back(block);
            offset = candidate + 4 + length;
            have_block = true;
            break;
        }
        if (have_block) {
            continue;
        }
        // Trailing residue after the last document is the measured shape
        // (project_a: four bytes at 9,928).  Nothing else is accepted.
        const std::size_t remaining = data.size() - offset;
        if (!out.blocks.empty() && remaining <= kEdlCacheMaxResidue) {
            Var2Gap gap;
            gap.offset = static_cast<std::uint32_t>(offset);
            gap.bytes.assign(data.begin() + static_cast<std::ptrdiff_t>(offset), data.end());
            out.gaps.push_back(std::move(gap));
            break;
        }
        if (error != nullptr) {
            const std::uint32_t declared = read_u32(data, offset);
            error->message = "214/CEdl Var2Data is not a Props-document cache walk at offset " +
                             std::to_string(offset) + ": it declares " +
                             std::to_string(declared) + " payload bytes, " +
                             std::to_string(data.size() - offset - 4) +
                             " remain, and no document starts within " +
                             std::to_string(kEdlCacheMaxResidue) + " bytes";
            error->offset = offset;
        }
        return false;
    }
    if (out.blocks.empty()) {
        if (error != nullptr) {
            error->message = "214/CEdl Var2Data holds no Props-document block";
            error->offset = 0;
        }
        return false;
    }
    for (const auto& entry : var_meta_entries) {
        bool found = false;
        for (const auto& block : out.blocks) {
            if (block.offset == entry.offset) {
                found = true;
                break;
            }
        }
        if (!found) {
            if (error != nullptr) {
                error->message = "VarMeta entry (uid " + std::to_string(entry.uid) + ", key " +
                                 hex32(entry.key) + ") points at offset " +
                                 std::to_string(entry.offset) +
                                 ", which is not a 214/CEdl document block start";
                error->offset = entry.offset;
            }
            return false;
        }
    }
    return true;
}

bool parse_view_records(std::span<const std::uint8_t> payload,
                        std::vector<ViewRecord>& out,
                        ParseError* error) {
    out.clear();
    if (payload.size() < kViewMainHeaderSize) {
        if (error != nullptr) {
            error->message = "CV_iew main block is " + std::to_string(payload.size()) +
                             " bytes, shorter than its 16-byte header";
            error->offset = payload.size();
        }
        return false;
    }
    const std::uint32_t size_word = read_u32(payload, 0);
    // The item count is a u16 at +12: three corpus blocks carry a non-zero word
    // at +14, which a u32 read would turn into a huge count.
    const std::uint16_t count = read_u16(payload, 12);
    if (size_word != payload.size() - 4) {
        if (error != nullptr) {
            error->message = "CV_iew main block size word is " + std::to_string(size_word) +
                             " but the payload is " + std::to_string(payload.size()) + " bytes";
            error->offset = 0;
        }
        return false;
    }
    if (static_cast<std::size_t>(count) >
        (payload.size() - kViewMainHeaderSize) / kViewItemHeaderSize) {
        if (error != nullptr) {
            error->message = "CV_iew main block declares " + std::to_string(count) +
                             " items but is too short to hold them";
            error->offset = 12;
        }
        return false;
    }
    std::size_t pos = kViewMainHeaderSize;
    const std::uint32_t item_count = count;
    out.reserve(item_count);
    for (std::uint32_t i = 0; i < item_count; ++i) {
        if (pos + kViewItemHeaderSize > payload.size()) {
            if (error != nullptr) {
                error->message = "CV_iew item " + std::to_string(i) + " starts past the block end";
                error->offset = pos;
            }
            return false;
        }
        const std::uint32_t length = read_u32(payload, pos);
        const std::uint32_t id = read_u32(payload, pos + 4);
        const std::uint32_t kind = read_u32(payload, pos + 8);
        if (!props::is_props_id(id)) {
            if (error != nullptr) {
                error->message = "CV_iew item " + std::to_string(i) + " has id " + hex32(id) +
                                 ", which is not a view property id";
                error->offset = pos;
            }
            return false;
        }
        if (length > payload.size() - pos - kViewItemHeaderSize) {
            if (error != nullptr) {
                error->message = "CV_iew item " + std::to_string(i) + " declares " +
                                 std::to_string(length) + " payload bytes but only " +
                                 std::to_string(payload.size() - pos - kViewItemHeaderSize) +
                                 " remain";
                error->offset = pos;
            }
            return false;
        }
        ViewRecord record;
        record.record_offset = static_cast<std::uint32_t>(pos);
        record.payload_length = length;
        record.id = id;
        record.kind = kind;
        record.preview = preview_bytes(payload.subspan(pos + kViewItemHeaderSize, length));
        out.push_back(std::move(record));

        // Measured: an odd payload length is followed by one pad byte, so the
        // next item starts on an even offset (the header is 12 bytes).
        std::size_t stride = kViewItemHeaderSize + static_cast<std::size_t>(length);
        if ((stride & 1u) != 0) {
            ++stride;
        }
        if (pos + stride > payload.size()) {
            // Tolerate a missing pad after the LAST item only.
            if (i + 1 == item_count && pos + kViewItemHeaderSize + length == payload.size()) {
                pos = payload.size();
                continue;
            }
            if (error != nullptr) {
                error->message = "CV_iew item " + std::to_string(i) + " runs past the block end";
                error->offset = pos;
            }
            return false;
        }
        pos += stride;
    }
    if (pos != payload.size()) {
        if (error != nullptr) {
            error->message = "CV_iew main block has " + std::to_string(payload.size() - pos) +
                             " trailing byte(s) after " + std::to_string(count) + " item(s)";
            error->offset = pos;
        }
        return false;
    }
    return true;
}

bool parse_row_record_set(std::span<const std::uint8_t> row,
                          RowRecordSet& out,
                          ParseError* error) {
    out = RowRecordSet{};
    if (row.size() < kRowRecordSetOffset + 8) {
        if (error != nullptr) {
            error->message = "row is " + std::to_string(row.size()) +
                             " bytes, too short for the record set at +0x6e";
            error->offset = row.size();
        }
        return false;
    }
    const std::uint32_t size = read_u32(row, kRowRecordSetOffset);
    const std::uint32_t count = read_u32(row, kRowRecordSetOffset + 4);
    out.size = size;
    if (count == 0) {
        // Measured "no set" shape (all-zero size/count, e.g. the corpus file's
        // built-in tables).  A non-zero size with no records is not understood.
        if (size != 0) {
            if (error != nullptr) {
                error->message = "row record set declares 0 records but " +
                                 std::to_string(size) + " bytes";
                error->offset = kRowRecordSetOffset;
            }
            return false;
        }
        return true;
    }
    if (count > 1024) {
        if (error != nullptr) {
            error->message = "row record set declares " + std::to_string(count) +
                             " records; refusing an implausible count";
            error->offset = kRowRecordSetOffset + 4;
        }
        return false;
    }
    if (static_cast<std::size_t>(size) < 8 + static_cast<std::size_t>(count) * 8 ||
        static_cast<std::size_t>(size) > row.size() - kRowRecordSetOffset) {
        if (error != nullptr) {
            error->message = "row record set size " + std::to_string(size) +
                             " does not fit the row with " + std::to_string(count) +
                             " record(s)";
            error->offset = kRowRecordSetOffset;
        }
        return false;
    }
    out.records.reserve(count);
    std::uint32_t previous = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t entry = kRowRecordSetOffset + 8 + static_cast<std::size_t>(i) * 8;
        const std::uint32_t id = read_u32(row, entry);
        const std::uint32_t offset = read_u32(row, entry + 4);
        if (!props::is_props_id(id)) {
            if (error != nullptr) {
                error->message = "row record " + std::to_string(i) + " has id " + hex32(id) +
                                 ", which is not a property id";
                error->offset = entry;
            }
            return false;
        }
        if (offset < 8 + static_cast<std::size_t>(count) * 8 || offset >= size ||
            (i != 0 && offset <= previous)) {
            if (error != nullptr) {
                error->message = "row record " + std::to_string(i) + " payload offset " +
                                 std::to_string(offset) +
                                 " is outside the record set or out of order";
                error->offset = entry + 4;
            }
            return false;
        }
        RowRecord record;
        record.id = id;
        record.payload_offset = offset;
        record.payload_length = (i + 1 < count ? read_u32(row, entry + 12) : size) - offset;
        if (i + 1 < count) {
            const std::uint32_t next = read_u32(row, entry + 12);
            if (next < offset || next > size) {
                if (error != nullptr) {
                    error->message = "row record " + std::to_string(i) +
                                     " payload runs past the record set";
                    error->offset = entry + 4;
                }
                return false;
            }
            record.payload_length = next - offset;
        }
        record.preview = preview_bytes(
            row.subspan(kRowRecordSetOffset + offset, record.payload_length));
        out.records.push_back(std::move(record));
        previous = offset;
    }
    out.decoded = true;
    // ...0009 is the display name for both measured key families
    // (0x20400009 on CTable/CFilter, 0x2d400009 on CGrouping).
    for (const auto& record : out.records) {
        if ((record.id & 0x0000ffffu) == 0x0009u) {
            out.name = utf16_ascii(row, kRowRecordSetOffset + record.payload_offset,
                                   record.payload_length / 2);
            break;
        }
    }
    // A malformed entry/name is still a framing-level success: the parser has
    // exposed the records, and the name (when printable) is decoded.
    return true;
}

bool parse_view_state(std::string_view klass,
                      const PartSet& parts,
                      ViewState& out,
                      ParseError* error) {
    out = ViewState{};
    out.klass = std::string(klass);
    out.fixed_data.assign(parts.fixed_data.begin(), parts.fixed_data.end());
    out.fixed_meta_bytes.assign(parts.fixed_meta.begin(), parts.fixed_meta.end());
    out.var_meta_bytes.assign(parts.var_meta.begin(), parts.var_meta.end());
    out.var2_data.assign(parts.var2_data.begin(), parts.var2_data.end());

    if (!parse_fixed_meta(out.fixed_meta_bytes, out.fixed_meta, error)) {
        return false;
    }
    if (!parse_var_meta(out.var_meta_bytes, out.var_meta, error)) {
        return false;
    }
    if (!parse_var2(out.var2_data, out.var2, error)) {
        // 214/CEdl keeps one Props document per save and repoints VarMeta, so a
        // strict walk can stop on stale residue.  Every other class must walk
        // strictly (wave 18, F2).
        if (klass != "CEdl") {
            return false;
        }
        ParseError strict_error;
        if (error != nullptr) {
            strict_error = *error;
        }
        ParseError cache_error;
        if (!parse_var2_edl_cache(out.var2_data, out.var_meta.entries, out.var2,
                                  &cache_error)) {
            if (error != nullptr) {
                *error = cache_error;
            }
            return false;
        }
        std::size_t residue = 0;
        for (const auto& gap : out.var2.gaps) {
            residue += gap.bytes.size();
        }
        out.warnings.push_back(
            "214/CEdl Var2Data is a cumulative document cache: " +
            std::to_string(out.var2.blocks.size()) + " Props-document block(s), " +
            std::to_string(out.var2.gaps.size()) + " residue run(s) / " +
            std::to_string(residue) + " byte(s); strict walk stopped at " +
            std::to_string(strict_error.offset) + " (" + strict_error.message + ")");
    }

    if (out.var_meta.var2_size != out.var2.size) {
        out.warnings.push_back("VarMeta declares var2_size " +
                               std::to_string(out.var_meta.var2_size) +
                               " but Var2Data is " + std::to_string(out.var2.size) + " bytes");
    }
    if (out.var_meta.trailing_bytes != 0) {
        out.warnings.push_back("VarMeta has " + std::to_string(out.var_meta.trailing_bytes) +
                               " trailing byte(s) after the declared entries");
    }
    if (out.fixed_meta.fixed_data_size != out.fixed_data.size()) {
        out.warnings.push_back("FixedMeta declares fixed_data_size " +
                               std::to_string(out.fixed_meta.fixed_data_size) +
                               " but FixedData is " + std::to_string(out.fixed_data.size()) +
                               " bytes");
    }

    // A single row means the row is the whole stream; multiple rows are derived
    // from the first two FixedMeta offsets, then validated against every entry.
    if (!out.fixed_meta.entries.empty()) {
        if (out.fixed_meta.entries.size() == 1) {
            out.row_size = out.fixed_meta.fixed_data_size;
        } else if (out.fixed_meta.entries[1].fixed_offset >
                   out.fixed_meta.entries[0].fixed_offset) {
            out.row_size = out.fixed_meta.entries[1].fixed_offset -
                           out.fixed_meta.entries[0].fixed_offset;
        } else {
            out.warnings.push_back("FixedMeta entry offsets are not increasing; row size unknown");
        }
    }

    std::size_t reported_offset_mismatches = 0;
    if (out.row_size > 0) {
        const std::size_t row_count = out.fixed_data.size() / out.row_size;
        if (out.fixed_data.size() % out.row_size != 0) {
            out.warnings.push_back("FixedData size is not a multiple of the derived row size " +
                                   std::to_string(out.row_size));
        }
        for (std::size_t i = 0; i < row_count; ++i) {
            const std::size_t base = i * out.row_size;
            FixedRow row;
            row.row_index = static_cast<std::uint32_t>(i);
            row.ordinal = read_u32(std::span<const std::uint8_t>(out.fixed_data), base);
            const std::size_t caption_capacity =
                out.row_size > 4 ? std::min<std::size_t>(128, out.row_size - 4) : 0;
            row.caption = utf16_ascii(out.fixed_data, base + 4, caption_capacity / 2);
            if (out.row_size == kDisplayCaptionRowSize) {
                // The row's inline record set carries the display name (wave 17,
                // F1) on the three measured 230-byte classes.  CDrawing also has
                // 230-byte rows but its bytes at +0x6e are not a record set, so
                // it keeps the legacy +136 read.
                const bool has_record_set = out.klass == "CTable" || out.klass == "CFilter" ||
                                            out.klass == "CGrouping";
                if (!has_record_set) {
                    row.display_caption = utf16_ascii(out.fixed_data,
                                                      base + kDisplayCaptionOffset, 39);
                } else {
                    const std::span<const std::uint8_t> row_bytes(out.fixed_data.data() + base,
                                                                  out.row_size);
                    ParseError set_error;
                    if (parse_row_record_set(row_bytes, row.definition, &set_error)) {
                        if (!row.definition.name.empty()) {
                            row.display_caption = row.definition.name;
                        } else {
                            row.display_caption = utf16_ascii(out.fixed_data,
                                                              base + kDisplayCaptionOffset, 39);
                        }
                    } else {
                        out.warnings.push_back("row " + std::to_string(i + 1) +
                                               " record set at +0x6e is opaque: " +
                                               set_error.message);
                        row.display_caption = utf16_ascii(out.fixed_data,
                                                          base + kDisplayCaptionOffset, 39);
                    }
                }
            }
            if (out.row_size >= kIdentitySize) {
                const std::size_t identity = base + out.row_size - kIdentitySize;
                std::copy(out.fixed_data.begin() + static_cast<std::ptrdiff_t>(identity),
                          out.fixed_data.begin() + static_cast<std::ptrdiff_t>(identity + 16),
                          row.identity.begin());
            }
            out.rows.push_back(std::move(row));
        }
        for (std::size_t i = 0; i < out.fixed_meta.entries.size(); ++i) {
            const std::uint32_t expected = static_cast<std::uint32_t>(i * out.row_size);
            if (out.fixed_meta.entries[i].fixed_offset != expected &&
                reported_offset_mismatches < 4) {
                out.warnings.push_back("FixedMeta entry " + std::to_string(i) +
                                       " offset " + std::to_string(
                                                           out.fixed_meta.entries[i].fixed_offset) +
                                       " is not the contiguous row offset " +
                                       std::to_string(expected));
                ++reported_offset_mismatches;
            }
        }
    } else if (!out.fixed_data.empty()) {
        out.warnings.push_back("FixedData is non-empty but no row size could be derived");
    }

    // Resolve every VarMeta entry to the block it names.  Blocks are walked in
    // stream order, while VarMeta entries are grouped by uid, so the two orders
    // differ (measured on CV_iew).
    out.blocks = out.var2.blocks;
    out.block_refs.assign(out.blocks.size(), ViewBlockRef{});
    std::vector<bool> referenced(out.blocks.size(), false);
    for (const auto& entry : out.var_meta.entries) {
        std::size_t found = out.blocks.size();
        for (std::size_t i = 0; i < out.blocks.size(); ++i) {
            if (out.blocks[i].offset == entry.offset) {
                found = i;
                break;
            }
        }
        if (found == out.blocks.size()) {
            out.warnings.push_back("VarMeta entry (uid " + std::to_string(entry.uid) + ", key " +
                                   hex32(entry.key) + ") points at offset " +
                                   std::to_string(entry.offset) +
                                   ", which is not a block start");
            continue;
        }
        if (referenced[found]) {
            out.warnings.push_back("VarMeta entry (uid " + std::to_string(entry.uid) + ", key " +
                                   hex32(entry.key) + ") repeats block offset " +
                                   std::to_string(entry.offset));
            continue;
        }
        referenced[found] = true;
        ViewBlockRef& ref = out.block_refs[found];
        ref.uid = entry.uid;
        ref.key = entry.key;
        ref.offset = out.blocks[found].offset;
        ref.payload_length = out.blocks[found].payload_length;
        ref.kind = std::string(block_kind(out.klass, entry.key));
        ref.preview = preview_bytes(out.block_bytes(out.blocks[found]));
        if (!out.rows.empty() &&
            (entry.uid == 0 ||
             std::none_of(out.rows.begin(), out.rows.end(),
                          [uid = entry.uid](const FixedRow& row) { return row.ordinal == uid; }))) {
            out.warnings.push_back("VarMeta entry uid " + std::to_string(entry.uid) +
                                   " has no fixed row ordinal (rows: " +
                                   std::to_string(out.rows.size()) + ")");
        }
    }
    for (std::size_t i = 0; i < out.blocks.size(); ++i) {
        if (referenced[i]) {
            continue;
        }
        ViewBlockRef& ref = out.block_refs[i];
        ref.offset = out.blocks[i].offset;
        ref.payload_length = out.blocks[i].payload_length;
        ref.kind = "unreferenced";
        ref.preview = preview_bytes(out.block_bytes(out.blocks[i]));
        out.warnings.push_back("Var2Data block at " + std::to_string(out.blocks[i].offset) +
                               " is not named by any VarMeta entry");
    }

    if (out.klass == "CTable") {
        for (std::size_t i = 0; i < out.rows.size(); ++i) {
            TableDef table;
            table.row_index = static_cast<std::uint32_t>(i);
            table.ordinal = out.rows[i].ordinal;
            table.caption = out.rows[i].caption;
            table.display_caption = out.rows[i].display_caption;
            table.identity = out.rows[i].identity;

            // VarMeta uid is the row's ORDINAL, not its row index: the corpus
            // fixtures skip ordinals (13 and 22 are absent in 22 rows), so
            // matching on i+1 would shift every later row.
            const ViewBlockRef* ref = out.find_block_ref(table.ordinal, kColumnBlockKey);
            if (ref == nullptr) {
                out.warnings.push_back("table row " + std::to_string(i + 1) +
                                       " has no 0x20400006 column block");
                out.tables.push_back(std::move(table));
                continue;
            }
            const VarBlock* block = nullptr;
            for (const auto& candidate : out.blocks) {
                if (candidate.offset == ref->offset) {
                    block = &candidate;
                    break;
                }
            }
            if (block == nullptr) {
                out.warnings.push_back("table row " + std::to_string(i + 1) +
                                       " lost its column block");
                out.tables.push_back(std::move(table));
                continue;
            }
            table.block_offset = block->offset;
            table.block_length = block->payload_length;
            const std::span<const std::uint8_t> payload = out.block_bytes(*block);
            if (payload.size() < kTableBlockHeaderSize) {
                out.warnings.push_back("table row " + std::to_string(i + 1) + " column block is " +
                                       std::to_string(payload.size()) + " bytes, shorter than 12");
                out.tables.push_back(std::move(table));
                continue;
            }
            table.header_ordinal = read_u32(payload, 0);
            table.last_column_index = read_u16(payload, 4);
            table.format_word = payload[6];
            table.date_format = payload[7];
            table.flags = read_u32(payload, 8);
            if (table.header_ordinal != table.ordinal) {
                out.warnings.push_back("table row " + std::to_string(i + 1) + " ordinal " +
                                       std::to_string(table.ordinal) +
                                       " disagrees with block ordinal " +
                                       std::to_string(table.header_ordinal));
            }
            const std::size_t declared_columns =
                static_cast<std::size_t>(table.last_column_index) + 1;
            const std::size_t expected =
                kTableBlockHeaderSize + declared_columns * kColumnRecordSize;
            if (expected != payload.size()) {
                out.warnings.push_back("table row " + std::to_string(i + 1) +
                                       " column block is " + std::to_string(payload.size()) +
                                       " bytes but its header implies " +
                                       std::to_string(declared_columns) + " columns (" +
                                       std::to_string(expected) + " bytes); left opaque");
                out.tables.push_back(std::move(table));
                continue;
            }
            table.columns.reserve(declared_columns);
            for (std::size_t c = 0; c < declared_columns; ++c) {
                ColumnDef column;
                const std::size_t record =
                    kTableBlockHeaderSize + c * kColumnRecordSize;
                column.record_offset = static_cast<std::uint32_t>(record);
                column.field_id = read_u32(payload, record);
                for (std::size_t b = 0; b < column.opaque_header.size(); ++b) {
                    column.opaque_header[b] = payload[record + 4 + b];
                }
                column.title = utf16_ascii(payload, record + kColumnTitleOffset,
                                           kColumnTitleCapacity);
                table.columns.push_back(std::move(column));
            }
            table.decoded = true;
            out.tables.push_back(std::move(table));
        }
    } else if (out.klass == "CV_iew") {
        for (std::size_t i = 0; i < out.rows.size(); ++i) {
            ViewDef view;
            view.row_index = static_cast<std::uint32_t>(i);
            view.ordinal = out.rows[i].ordinal;
            view.name = out.rows[i].caption;
            view.identity = out.rows[i].identity;
            for (const auto& ref : out.block_refs) {
                if (ref.uid == view.ordinal) {
                    view.blocks.push_back(ref);
                }
            }
            // Decode the main view-state block (wave 15, W62).  A block that
            // does not walk stays opaque with a warning: the collection is
            // still readable and the encoder copies the block verbatim.
            for (const auto& block : view.blocks) {
                if (block.key != kColumnBlockKey) {
                    continue;
                }
                const VarBlock* var_block = nullptr;
                for (const auto& candidate : out.blocks) {
                    if (candidate.offset == block.offset) {
                        var_block = &candidate;
                        break;
                    }
                }
                if (var_block == nullptr) {
                    out.warnings.push_back("view row " + std::to_string(i + 1) +
                                           " lost its 0x20400006 main block");
                    break;
                }
                view.main_block_offset = var_block->offset;
                view.main_payload_length = var_block->payload_length;
                if (!decode_view_main(out, view)) {
                    view.main_decoded = false;
                    out.warnings.push_back("view row " + std::to_string(i + 1) +
                                           " main block is opaque");
                }
                break;
            }
            // Notes: nine u32 chunk lengths followed by the chunks.
            for (const auto& block : view.blocks) {
                if (block.key != kViewNotesKey) {
                    continue;
                }
                const VarBlock* var_block = nullptr;
                for (const auto& candidate : out.blocks) {
                    if (candidate.offset == block.offset) {
                        var_block = &candidate;
                        break;
                    }
                }
                if (var_block == nullptr) {
                    break;
                }
                const std::span<const std::uint8_t> payload = out.block_bytes(*var_block);
                if (payload.size() >= kViewNotesHeaderSize) {
                    std::uint64_t total = 0;
                    std::array<std::uint32_t, kViewNotesChunks> lengths{};
                    for (std::size_t chunk = 0; chunk < kViewNotesChunks; ++chunk) {
                        lengths[chunk] = read_u32(payload, chunk * 4);
                        total += lengths[chunk];
                    }
                    if (total + kViewNotesHeaderSize == payload.size()) {
                        view.notes_decoded = true;
                        view.notes_chunk_lengths = lengths;
                        std::size_t at = kViewNotesHeaderSize;
                        for (std::size_t chunk = 0; chunk < kViewNotesChunks; ++chunk) {
                            view.notes_previews[chunk] =
                                preview_bytes(payload.subspan(at, lengths[chunk]), 40);
                            at += lengths[chunk];
                        }
                    }
                }
                break;
            }
            out.views.push_back(std::move(view));
        }
        // Inference: a combination view stores view names (not a table name) in
        // 0x2240000a.  If that name matches another view's name, mark it.
        recompute_combination_flags(out);
    } else if (out.klass == "CEdl") {
        // Measured: a CEdl Var2Data block payload is a complete Props-format
        // document (header, records, no tail).  Parse it with core/props rather
        // than re-implementing the record framing, then decode the records whose
        // payload is understood (view name, table name, column field list).
        for (const auto& block : out.blocks) {
            EmbeddedProps embedded;
            if (decode_edl_block(out, block, embedded)) {
                out.embedded_props.push_back(std::move(embedded));
            } else {
                out.warnings.push_back("CEdl block at " + std::to_string(block.offset) +
                                       " did not parse as a Props document: "
                                       "the documented size/record framing does not match");
            }
        }
    } else if (out.klass == "CFilter" || out.klass == "CGrouping") {
        // Framing-level decode (wave 17, F1): the row's inline record set is
        // already on FixedRow::definition.  Here we resolve each row's
        // 0x20400006 block and, for CFilter, the applies-to string the block
        // points at (payload +16).
        const bool filter = out.klass == "CFilter";
        for (std::size_t i = 0; i < out.rows.size(); ++i) {
            const FixedRow& row = out.rows[i];
            const ViewBlockRef* ref =
                out.find_block_ref(row.ordinal, filter ? kFilterBlockKey : kGroupingBlockKey);
            const VarBlock* var_block = nullptr;
            if (ref != nullptr) {
                for (const auto& candidate : out.blocks) {
                    if (candidate.offset == ref->offset) {
                        var_block = &candidate;
                        break;
                    }
                }
            }
            if (var_block == nullptr) {
                out.warnings.push_back(std::string(filter ? "filter" : "grouping") + " row " +
                                       std::to_string(i + 1) +
                                       " has no decoded 0x20400006 block");
            }
            const std::span<const std::uint8_t> payload =
                var_block != nullptr ? out.block_bytes(*var_block)
                                     : std::span<const std::uint8_t>();
            if (filter) {
                FilterDef def;
                def.row_index = row.row_index;
                def.ordinal = row.ordinal;
                def.caption = row.caption;
                def.display_name = row.display_caption;
                def.definition = row.definition;
                if (var_block != nullptr) {
                    def.block_offset = var_block->offset;
                    def.block_length = var_block->payload_length;
                    def.block_decoded = payload.size() >= kFilterBlockHeaderSize;
                    def.block_ordinal = read_u32(payload, 0);
                    if (def.block_ordinal != def.ordinal) {
                        out.warnings.push_back("filter row " + std::to_string(i + 1) +
                                               " ordinal " + std::to_string(def.ordinal) +
                                               " disagrees with block ordinal " +
                                               std::to_string(def.block_ordinal));
                    }
                    if (payload.size() >= 8) {
                        def.block_flags = read_u32(payload, 4);
                    }
                    if (payload.size() >= kFilterBlockHeaderSize) {
                        def.criterion_count = read_u16(payload, 14);
                        def.string_table_offset = read_u32(payload, 16);
                        const std::size_t expected =
                            kFilterBlockHeaderSize +
                            static_cast<std::size_t>(def.criterion_count) *
                                kFilterCriterionSize;
                        if (def.string_table_offset == expected &&
                            def.string_table_offset <= payload.size() &&
                            (def.string_table_offset & 1u) == 0) {
                            def.criteria_decoded = true;
                        } else {
                            out.warnings.push_back(
                                "filter row " + std::to_string(i + 1) +
                                " block header does not satisfy string-table offset == "
                                "20 + 36*criterion_count (offset " +
                                std::to_string(def.string_table_offset) + ", count " +
                                std::to_string(def.criterion_count) + ")");
                        }
                        // The string table is NUL-terminated UTF-16; the first
                        // string is the applies-to kind, the rest are prompts.
                        std::size_t at = def.string_table_offset;
                        std::vector<std::string> strings;
                        bool terminated = true;
                        while (at < payload.size()) {
                            std::size_t end = at;
                            bool found = false;
                            while (end + 1 < payload.size()) {
                                if (payload[end] == 0 && payload[end + 1] == 0) {
                                    found = true;
                                    break;
                                }
                                end += 2;
                            }
                            if (!found) {
                                terminated = false;
                                break;
                            }
                            strings.push_back(utf16_ascii(payload, at, (end - at) / 2));
                            at = end + 2;
                        }
                        if (terminated) {
                            def.string_table = std::move(strings);
                            if (!def.string_table.empty() &&
                                !def.string_table.front().empty()) {
                                def.applies_to_decoded = true;
                                def.applies_to_offset = def.string_table_offset;
                                def.applies_to = def.string_table.front();
                            }
                        }
                    }
                }
                // The row's ...0007 record is a u16 word followed by an optional
                // UTF-16 label (wave 18, F2).  It holds no criteria in any
                // measured row; the criteria live in the block.
                if (row.definition.decoded && out.row_size > kRowRecordSetOffset) {
                    const RowRecord* criteria = row.definition.find(kFilterCriteriaKey);
                    if (criteria != nullptr && criteria->payload_length >= 2) {
                        const std::size_t base =
                            static_cast<std::size_t>(row.row_index) * out.row_size +
                            kRowRecordSetOffset + criteria->payload_offset;
                        if (base + criteria->payload_length <= out.fixed_data.size()) {
                            def.criteria_record_decoded = true;
                            def.criteria_record_word = read_u16(out.fixed_data, base);
                            if (criteria->payload_length > 2) {
                                def.criteria_record_label =
                                    utf16_ascii(out.fixed_data, base + 2,
                                                (criteria->payload_length - 2) / 2);
                            }
                        }
                    }
                }
                out.filters.push_back(std::move(def));
            } else {
                GroupingDef def;
                def.row_index = row.row_index;
                def.ordinal = row.ordinal;
                def.caption = row.caption;
                def.display_name = row.display_caption;
                def.definition = row.definition;
                if (var_block != nullptr) {
                    def.block_offset = var_block->offset;
                    def.block_length = var_block->payload_length;
                    def.block_decoded = payload.size() >= 4;
                    def.block_ordinal = read_u32(payload, 0);
                    if (def.block_ordinal != def.ordinal && payload.size() >= 4) {
                        out.warnings.push_back("grouping row " + std::to_string(i + 1) +
                                               " ordinal " + std::to_string(def.ordinal) +
                                               " disagrees with block ordinal " +
                                               std::to_string(def.block_ordinal));
                    }
                    if (payload.size() >= 8) {
                        def.block_flags = read_u32(payload, 4);
                    }
                    if (payload.size() >= kGroupingBlockHeaderSize) {
                        def.header_word_8 = read_u16(payload, 8);
                        def.header_word_10 = read_u16(payload, 10);
                    }
                    def.has_body = payload.size() > kGroupingBlockHeaderSize;
                    if (payload.size() >= kGroupingBlockHeaderSize + 4 &&
                        props::is_props_id(read_u32(payload, kGroupingBlockHeaderSize))) {
                        def.body_field_decoded = true;
                        def.body_field_id = read_u32(payload, kGroupingBlockHeaderSize);
                    }
                }
                out.groupings.push_back(std::move(def));
            }
        }
    }
    out.parsed = true;
    return true;
}

std::string describe_view_state(const ViewState& state,
                                std::size_t max_columns,
                                std::size_t max_blocks) {
    std::ostringstream out;
    out << "class " << state.klass << ": fixed_data=" << state.fixed_data.size()
        << " fixed_meta=" << state.fixed_meta_bytes.size()
        << " var_meta=" << state.var_meta_bytes.size()
        << " var2=" << state.var2_data.size() << "\n";
    out << "  fixed_meta: entries=" << state.fixed_meta.declared_count
        << " entry_size=" << state.fixed_meta.entry_size
        << " data_size=" << state.fixed_meta.fixed_data_size
        << " flags=" << state.fixed_meta.flags << "\n";
    out << "  var_meta: entries=" << state.var_meta.declared_count
        << " var2_size=" << state.var_meta.var2_size
        << " trailing=" << state.var_meta.trailing_bytes << "\n";
    out << "  blocks=" << state.blocks.size() << " rows=" << state.rows.size()
        << " row_size=" << state.row_size;
    if (state.var2.cache_tolerant) {
        std::size_t residue = 0;
        for (const auto& gap : state.var2.gaps) {
            residue += gap.bytes.size();
        }
        out << " cache-tolerant=1 residue=" << residue << " byte(s) in "
            << state.var2.gaps.size() << " run(s)";
    }
    out << "\n";
    for (const auto& gap : state.var2.gaps) {
        out << "  residue off=" << gap.offset << " len=" << gap.bytes.size() << "\n";
    }
    for (const auto& warning : state.warnings) {
        out << "  warning: " << warning << "\n";
    }
    for (const auto& table : state.tables) {
        out << "  table " << table.ordinal << " \"" << table.caption << "\"";
        if (!table.display_caption.empty()) {
            out << " display=\"" << table.display_caption << "\"";
        }
        out << " columns=" << table.columns.size();
        if (table.decoded) {
            out << " date_format=" << hex32(table.date_format) << " flags=" << table.flags
                << " block=" << table.block_offset << "+" << table.block_length;
        } else {
            out << " undecoded";
        }
        out << "\n";
        std::size_t shown = 0;
        for (const auto& column : table.columns) {
            if (max_columns != 0 && shown >= max_columns) {
                out << "    ... " << (table.columns.size() - shown) << " more column(s)\n";
                break;
            }
            out << "    col " << shown << " field=" << hex32(column.field_id) << " title=\""
                << column.title << "\" header="
                << byte_hex(column.opaque_header.data(), column.opaque_header.size()) << "\n";
            ++shown;
        }
    }
    for (const auto& view : state.views) {
        out << "  view " << view.ordinal << " \"" << view.name << "\" blocks="
            << view.blocks.size() << "\n";
        if (view.main_decoded) {
            out << "    main block=" << view.main_payload_length << " bytes items="
                << view.records.size();
            if (!view.display_name.empty()) {
                out << " name=\"" << view.display_name << "\"";
            }
            out << "\n";
            if (!view.table_or_top.empty()) {
                out << "      table-or-top=\"" << view.table_or_top << "\""
                    << (view.looks_like_combination ? " (combination-view inference)" : "")
                    << "\n";
            }
            if (!view.filter_or_bottom.empty()) {
                out << "      filter-or-bottom=\"" << view.filter_or_bottom << "\"\n";
            }
            if (!view.group_name.empty()) {
                out << "      group=\"" << view.group_name << "\"\n";
            }
            if (view.options_decoded) {
                out << "      options=970 bytes margins=" << view.page_margins_thousandths[0]
                    << "," << view.page_margins_thousandths[1] << ","
                    << view.page_margins_thousandths[2] << ","
                    << view.page_margins_thousandths[3]
                    << " extra=" << view.page_margin_extra << "\n";
            }
            if (view.notes_decoded) {
                out << "      notes=9 chunks ";
                for (std::size_t chunk = 0; chunk < view.notes_chunk_lengths.size(); ++chunk) {
                    out << view.notes_chunk_lengths[chunk];
                    if (chunk + 1 != view.notes_chunk_lengths.size()) {
                        out << ",";
                    }
                }
                out << "\n";
            }
            std::size_t shown_records = 0;
            for (const auto& record : view.records) {
                if (max_blocks != 0 && shown_records >= max_blocks) {
                    out << "      ... " << (view.records.size() - shown_records)
                        << " more item(s)\n";
                    break;
                }
                out << "      item off=" << record.record_offset << " id=" << hex32(record.id)
                    << " kind=" << record.kind << " len=" << record.payload_length
                    << " preview=\"" << record.preview << "\"\n";
                ++shown_records;
            }
        } else {
            out << "    main block not item-decoded\n";
        }
        std::size_t shown = 0;
        for (const auto& block : view.blocks) {
            if (max_blocks != 0 && shown >= max_blocks) {
                out << "    ... " << (view.blocks.size() - shown) << " more block(s)\n";
                break;
            }
            out << "    key=" << hex32(block.key) << " off=" << block.offset
                << " len=" << block.payload_length << " kind=" << block.kind
                << " preview=\"" << block.preview << "\"\n";
            ++shown;
        }
    }
    for (const auto& filter : state.filters) {
        out << "  filter " << filter.ordinal << " \"" << filter.caption << "\"";
        if (!filter.display_name.empty()) {
            out << " display=\"" << filter.display_name << "\"";
        }
        out << " block=" << filter.block_offset << "+" << filter.block_length;
        if (filter.block_decoded) {
            out << " block-ordinal=" << filter.block_ordinal;
        } else {
            out << " undecoded";
        }
        if (filter.block_decoded) {
            out << " flags=" << filter.block_flags
                << " criteria=" << filter.criterion_count
                << " string-table=" << filter.string_table_offset
                << (filter.criteria_decoded ? "" : " (unframed)");
        }
        if (filter.applies_to_decoded) {
            out << " applies-to=\"" << filter.applies_to << "\"@" << filter.applies_to_offset;
        }
        out << " records=" << filter.definition.records.size();
        if (filter.definition.decoded) {
            out << " name=\"" << filter.definition.name << "\"";
        }
        if (filter.criteria_record_decoded) {
            out << " criteria-record-word=" << filter.criteria_record_word;
            if (!filter.criteria_record_label.empty()) {
                out << " label=\"" << filter.criteria_record_label << "\"";
            }
        }
        out << "\n";
        for (std::size_t s = 0; s < filter.string_table.size(); ++s) {
            out << "    string[" << s << "]=\"" << filter.string_table[s] << "\"\n";
        }
    }
    for (const auto& grouping : state.groupings) {
        out << "  grouping " << grouping.ordinal << " \"" << grouping.caption << "\"";
        if (!grouping.display_name.empty()) {
            out << " display=\"" << grouping.display_name << "\"";
        }
        out << " block=" << grouping.block_offset << "+" << grouping.block_length;
        if (grouping.block_decoded) {
            out << " block-ordinal=" << grouping.block_ordinal;
        } else {
            out << " undecoded";
        }
        if (grouping.block_decoded) {
            out << " flags=" << grouping.block_flags
                << " word8=" << grouping.header_word_8
                << " word10=" << grouping.header_word_10
                << (grouping.has_body ? " body=stored" : " body=none");
            if (grouping.body_field_decoded) {
                out << " body-field=" << hex32(grouping.body_field_id);
            }
        }
        out << " records=" << grouping.definition.records.size();
        if (grouping.definition.decoded) {
            out << " name=\"" << grouping.definition.name << "\"";
        }
        out << "\n";
    }
    for (const auto& embedded : state.embedded_props) {
        out << "  props block off=" << embedded.block_offset
            << " records=" << embedded.document.records.size()
            << " tail=" << embedded.document.tail_size();
        if (embedded.view_name_decoded) {
            out << " view=\"" << embedded.view_name << "\"";
        }
        if (embedded.table_name_decoded) {
            out << " table=\"" << embedded.table_name << "\"";
        }
        if (embedded.field_list_decoded) {
            out << " field-list=" << embedded.field_ids.size()
                << (embedded.field_list_terminated ? "+terminator" : "");
            if (max_columns == 0) {
                out << " fields=";
                for (std::size_t i = 0; i < embedded.field_ids.size(); ++i) {
                    out << (i == 0 ? "" : ",") << hex32(embedded.field_ids[i]);
                }
            }
        }
        out << "\n";
    }
    return out.str();
}

namespace {

// Validates that `table_index` names a decoded CTable whose column block is
// wholly inside Var2Data, and returns it with the block payload's stream
// offset.  Shared by both mutators so they cannot disagree about the target.
const TableDef* writable_table(const ViewState& state,
                               std::size_t table_index,
                               WriteError* error,
                               std::size_t& payload_start) {
    if (!state.parsed) {
        write_fail(error,
                   "view state did not parse: parse_view_state() must succeed before an edit");
        return nullptr;
    }
    if (table_index >= state.tables.size()) {
        write_fail(error, "table index " + std::to_string(table_index) + " is out of range (" +
                              std::to_string(state.tables.size()) + " table(s))");
        return nullptr;
    }
    const TableDef& table = state.tables[table_index];
    if (!table.decoded) {
        write_fail(error, "table " + std::to_string(table_index) +
                              " has no decoded 0x20400006 column block");
        return nullptr;
    }
    const std::size_t start = static_cast<std::size_t>(table.block_offset) + 4;
    if (start > state.var2_data.size() ||
        static_cast<std::size_t>(table.block_length) < kTableBlockHeaderSize ||
        static_cast<std::size_t>(table.block_length) > state.var2_data.size() - start) {
        write_fail(error, "table " + std::to_string(table_index) +
                              " column block lies outside Var2Data");
        return nullptr;
    }
    const std::size_t declared = static_cast<std::size_t>(table.last_column_index) + 1;
    if (declared != table.columns.size() ||
        static_cast<std::size_t>(table.block_length) !=
            kTableBlockHeaderSize + declared * kColumnRecordSize) {
        write_fail(error, "table " + std::to_string(table_index) +
                              " column block does not match its decoded column list");
        return nullptr;
    }
    payload_start = start;
    return &table;
}

}  // namespace

PartSet EncodedParts::parts() const {
    PartSet out;
    out.fixed_data = fixed_data;
    out.fixed_meta = fixed_meta;
    out.var_meta = var_meta;
    out.var2_data = var2_data;
    return out;
}

bool encode_view_state(const ViewState& state, EncodedParts& out, WriteError* error) {
    out = EncodedParts{};
    if (!state.parsed) {
        return write_fail(error,
                          "view state did not parse: encode_view_state() only writes a "
                          "ViewState produced by a successful parse_view_state()");
    }

    constexpr std::size_t kMaxU32 = 0xffffffffu;

    // FixedData is copied verbatim.  Its 16-byte identity values belong to
    // Project and change on every save; regenerating them would invent data.
    if (state.fixed_data.size() > kMaxU32) {
        return write_fail(error, "FixedData is too large to describe with a u32 size");
    }
    std::vector<std::uint8_t> fixed_data = state.fixed_data;

    // FixedMeta: 16-byte header + one entry per parsed entry.
    const std::size_t fixed_entry_count = state.fixed_meta.entries.size();
    std::uint32_t fixed_entry_size = 0;
    if (fixed_entry_count != 0) {
        if (state.fixed_meta.entry_size < 8) {
            return write_fail(error, "FixedMeta entry size " +
                                         std::to_string(state.fixed_meta.entry_size) +
                                         " is too small to hold state/var_count/fixed_offset");
        }
        fixed_entry_size = state.fixed_meta.entry_size;
    }
    if (fixed_entry_count > kMaxU32) {
        return write_fail(error, "FixedMeta has too many entries to describe with a u32 count");
    }
    std::vector<std::uint8_t> fixed_meta(
        kFixedMetaHeaderSize + fixed_entry_count * fixed_entry_size, 0);
    put_u32(fixed_meta, 0, kMetaMagic);
    put_u32(fixed_meta, 4, state.fixed_meta.flags);
    put_u32(fixed_meta, 8, static_cast<std::uint32_t>(fixed_entry_count));
    put_u32(fixed_meta, 12, static_cast<std::uint32_t>(fixed_data.size()));
    for (std::size_t i = 0; i < fixed_entry_count; ++i) {
        const FixedMetaEntry& entry = state.fixed_meta.entries[i];
        const std::size_t base = kFixedMetaHeaderSize + i * fixed_entry_size;
        const std::size_t opaque = fixed_entry_size - 8;
        if (entry.opaque_tail.size() != opaque) {
            return write_fail(error, "FixedMeta entry " + std::to_string(i) + " has " +
                                         std::to_string(entry.opaque_tail.size()) +
                                         " opaque byte(s) but entry size " +
                                         std::to_string(fixed_entry_size) + " needs " +
                                         std::to_string(opaque));
        }
        put_u16(fixed_meta, base, entry.state);
        put_u16(fixed_meta, base + 2, entry.var_count);
        put_u32(fixed_meta, base + 4, entry.fixed_offset);
        std::copy(entry.opaque_tail.begin(), entry.opaque_tail.end(),
                  fixed_meta.begin() + static_cast<std::ptrdiff_t>(base + 8));
    }

    // Var2Data: re-frame every block in stream order, writing any cache residue
    // back immediately before the block it preceded.  The new offsets are the
    // positions the length words land at, so VarMeta offsets below are derived
    // from the bytes rather than trusted from the input.
    if (state.block_refs.size() != state.blocks.size()) {
        return write_fail(error, "ViewState block_refs do not match its Var2Data blocks");
    }
    std::vector<std::uint8_t> var2_data;
    var2_data.reserve(state.var2_data.size());
    std::vector<std::uint32_t> new_offsets(state.blocks.size(), 0);
    std::size_t next_gap = 0;
    const auto append_gap = [&](const Var2Gap& gap) {
        if (gap.offset > state.var2_data.size() ||
            gap.bytes.size() > state.var2_data.size() - gap.offset) {
            return false;
        }
        var2_data.insert(var2_data.end(), gap.bytes.begin(), gap.bytes.end());
        return true;
    };
    for (std::size_t i = 0; i < state.blocks.size(); ++i) {
        const VarBlock& block = state.blocks[i];
        while (next_gap < state.var2.gaps.size() &&
               state.var2.gaps[next_gap].offset < block.offset) {
            if (!append_gap(state.var2.gaps[next_gap])) {
                return write_fail(error, "Var2Data cache residue at offset " +
                                             std::to_string(
                                                 state.var2.gaps[next_gap].offset) +
                                             " lies outside the stream");
            }
            ++next_gap;
        }
        const std::size_t start = static_cast<std::size_t>(block.offset) + 4;
        if (start > state.var2_data.size() ||
            static_cast<std::size_t>(block.payload_length) >
                state.var2_data.size() - start) {
            return write_fail(error, "Var2Data block " + std::to_string(i) + " at offset " +
                                         std::to_string(block.offset) +
                                         " runs past the stream");
        }
        if (var2_data.size() > kMaxU32) {
            return write_fail(error, "Var2Data is too large to describe with a u32 size");
        }
        new_offsets[i] = static_cast<std::uint32_t>(var2_data.size());
        append_u32(var2_data, block.payload_length);
        var2_data.insert(
            var2_data.end(),
            state.var2_data.begin() + static_cast<std::ptrdiff_t>(start),
            state.var2_data.begin() +
                static_cast<std::ptrdiff_t>(start + block.payload_length));
    }
    while (next_gap < state.var2.gaps.size()) {
        if (!append_gap(state.var2.gaps[next_gap])) {
            return write_fail(error, "Var2Data trailing cache residue at offset " +
                                         std::to_string(state.var2.gaps[next_gap].offset) +
                                         " lies outside the stream");
        }
        ++next_gap;
    }
    if (var2_data.size() > kMaxU32) {
        return write_fail(error, "Var2Data is too large to describe with a u32 size");
    }

    // VarMeta: preserve the three reserved header words and any trailing bytes
    // the parser found, but recompute the count, offsets and var2_size.
    if (state.var_meta_bytes.size() < kVarMetaHeaderSize) {
        return write_fail(error, "VarMeta is shorter than its 24-byte header");
    }
    const std::size_t var_entry_count = state.var_meta.entries.size();
    const std::size_t var_entries_size = kVarMetaHeaderSize + var_entry_count * 12;
    if (state.var_meta_bytes.size() < var_entries_size) {
        return write_fail(error, "VarMeta is shorter than its decoded entry list");
    }
    if (var_entry_count > kMaxU32) {
        return write_fail(error, "VarMeta has too many entries to describe with a u32 count");
    }
    const std::size_t trailing = state.var_meta_bytes.size() - var_entries_size;
    std::vector<std::uint8_t> var_meta(var_entries_size + trailing, 0);
    put_u32(var_meta, 0, kMetaMagic);
    put_u32(var_meta, 4, read_u32(state.var_meta_bytes, 4));
    put_u32(var_meta, 8, static_cast<std::uint32_t>(var_entry_count));
    put_u32(var_meta, 12, read_u32(state.var_meta_bytes, 12));
    put_u32(var_meta, 16, read_u32(state.var_meta_bytes, 16));
    put_u32(var_meta, 20, static_cast<std::uint32_t>(var2_data.size()));
    std::vector<bool> claimed(state.blocks.size(), false);
    for (std::size_t i = 0; i < var_entry_count; ++i) {
        const VarMetaEntry& entry = state.var_meta.entries[i];
        std::size_t matched = state.blocks.size();
        // Prefer the block the entry originally named: two blocks can share a
        // (uid, key) pair, and then only the old offset distinguishes them.
        for (std::size_t j = 0; j < state.blocks.size(); ++j) {
            if (!claimed[j] && state.blocks[j].offset == entry.offset &&
                state.block_refs[j].uid == entry.uid &&
                state.block_refs[j].key == entry.key) {
                matched = j;
                break;
            }
        }
        if (matched == state.blocks.size()) {
            for (std::size_t j = 0; j < state.blocks.size(); ++j) {
                if (!claimed[j] && state.block_refs[j].uid == entry.uid &&
                    state.block_refs[j].key == entry.key) {
                    matched = j;
                    break;
                }
            }
        }
        if (matched == state.blocks.size()) {
            return write_fail(error, "VarMeta entry " + std::to_string(i) + " (uid " +
                                         std::to_string(entry.uid) + ", key " +
                                         hex32(entry.key) + ") has no matching Var2Data block");
        }
        claimed[matched] = true;
        const std::size_t base = kVarMetaHeaderSize + i * 12;
        put_u32(var_meta, base, entry.uid);
        put_u32(var_meta, base + 4, new_offsets[matched]);
        put_u32(var_meta, base + 8, entry.key);
    }
    if (trailing != 0) {
        std::copy(state.var_meta_bytes.end() - static_cast<std::ptrdiff_t>(trailing),
                  state.var_meta_bytes.end(),
                  var_meta.begin() + static_cast<std::ptrdiff_t>(var_entries_size));
    }

    out.fixed_data = std::move(fixed_data);
    out.fixed_meta = std::move(fixed_meta);
    out.var_meta = std::move(var_meta);
    out.var2_data = std::move(var2_data);
    return true;
}

bool set_table_date_format(ViewState& state,
                           std::size_t table_index,
                           std::uint8_t date_format,
                           WriteError* error) {
    std::size_t payload_start = 0;
    const TableDef* table = writable_table(state, table_index, error, payload_start);
    if (table == nullptr) {
        return false;
    }
    const std::size_t at = payload_start + 7;
    if (at >= state.var2_data.size()) {
        return write_fail(error, "table " + std::to_string(table_index) +
                                     " date-format byte lies outside Var2Data");
    }
    state.var2_data[at] = date_format;
    state.tables[table_index].date_format = date_format;
    return true;
}

bool set_table_column(ViewState& state,
                      std::size_t table_index,
                      std::size_t column_index,
                      std::uint32_t field_id,
                      std::string_view title,
                      WriteError* error) {
    std::size_t payload_start = 0;
    const TableDef* table = writable_table(state, table_index, error, payload_start);
    if (table == nullptr) {
        return false;
    }
    if (column_index >= table->columns.size()) {
        return write_fail(error, "column index " + std::to_string(column_index) +
                                     " is out of range (" +
                                     std::to_string(table->columns.size()) + " column(s))");
    }
    if (field_id == 0) {
        return write_fail(error, "field id 0 is not a valid column field");
    }
    if (title.size() > kColumnTitleCapacity) {
        return write_fail(error, "title is " + std::to_string(title.size()) +
                                     " code units; the CTable column title capacity is " +
                                     std::to_string(kColumnTitleCapacity));
    }
    for (const char ch : title) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        if (byte < 0x20 || byte > 0x7e) {
            return write_fail(error,
                              "title must be printable ASCII (the decoder reads ASCII titles only)");
        }
    }
    const ColumnDef& column = table->columns[column_index];
    const std::size_t record = payload_start + column.record_offset;
    if (record > state.var2_data.size() ||
        kColumnRecordSize > state.var2_data.size() - record) {
        return write_fail(error, "table " + std::to_string(table_index) + " column " +
                                     std::to_string(column_index) +
                                     " record lies outside Var2Data");
    }
    put_u32(state.var2_data, record, field_id);
    // Project zero-filled the rest of the title area after the replacement's
    // NUL (measured on the w14 tablecol2-I/J fixture pair), so do the same.
    std::fill(
        state.var2_data.begin() + static_cast<std::ptrdiff_t>(record + kColumnTitleOffset),
        state.var2_data.begin() + static_cast<std::ptrdiff_t>(record + kColumnRecordSize),
        std::uint8_t{0});
    for (std::size_t i = 0; i < title.size(); ++i) {
        state.var2_data[record + kColumnTitleOffset + i * 2] =
            static_cast<std::uint8_t>(title[i]);
    }
    ColumnDef& target = state.tables[table_index].columns[column_index];
    target.field_id = field_id;
    target.title = std::string(title);
    return true;
}

bool append_table_column(ViewState& state,
                         std::size_t table_index,
                         std::uint32_t field_id,
                         std::string_view title,
                         WriteError* error) {
    std::size_t payload_start = 0;
    const TableDef* table = writable_table(state, table_index, error, payload_start);
    if (table == nullptr) {
        return false;
    }
    if (field_id == 0) {
        return write_fail(error, "field id 0 is not a valid column field");
    }
    if (title.size() > kColumnTitleCapacity) {
        return write_fail(error, "title is " + std::to_string(title.size()) +
                                     " code units; the CTable column title capacity is " +
                                     std::to_string(kColumnTitleCapacity));
    }
    for (const char ch : title) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        if (byte < 0x20 || byte > 0x7e) {
            return write_fail(error,
                              "title must be printable ASCII (the decoder reads ASCII titles only)");
        }
    }
    // `last_column_index` is a u16, so the table can hold at most 0x10000
    // columns; one more would not be representable.
    if (table->columns.size() >= static_cast<std::size_t>(0x10000u)) {
        return write_fail(error, "table " + std::to_string(table_index) + " already holds " +
                                     std::to_string(table->columns.size()) +
                                     " columns; the u16 last-column index cannot grow further");
    }
    if (table->block_length > 0xffffffffu - kColumnRecordSize) {
        return write_fail(error, "table " + std::to_string(table_index) +
                                     " column block would overflow its u32 length word");
    }
    const std::size_t column_count = table->columns.size();
    const std::size_t insert_at =
        payload_start + kTableBlockHeaderSize + column_count * kColumnRecordSize;
    if (insert_at > state.var2_data.size()) {
        return write_fail(error, "table " + std::to_string(table_index) +
                                     " append position lies outside Var2Data");
    }

    // Build the measured fresh-column record: field id, the nine bytes Project
    // wrote for a fresh column, then the zero-filled UTF-16 title area.
    std::vector<std::uint8_t> record(kColumnRecordSize, std::uint8_t{0});
    put_u32(record, 0, field_id);
    for (std::size_t i = 0; i < kFreshColumnHeader.size(); ++i) {
        record[4 + i] = kFreshColumnHeader[i];
    }
    for (std::size_t i = 0; i < title.size(); ++i) {
        record[kColumnTitleOffset + i * 2] = static_cast<std::uint8_t>(title[i]);
    }

    const std::uint32_t block_offset = table->block_offset;
    const std::uint32_t new_block_length = table->block_length + kColumnRecordSize;
    std::size_t block_index = state.blocks.size();
    for (std::size_t i = 0; i < state.blocks.size(); ++i) {
        if (state.blocks[i].offset == block_offset) {
            block_index = i;
            break;
        }
    }
    if (block_index == state.blocks.size()) {
        return write_fail(error, "table " + std::to_string(table_index) +
                                     " column block is not in the Var2Data walk");
    }

    // Everything validated; mutate.  Insert first, then repair every offset that
    // moved (later Var2 blocks and their VarMeta entries).
    state.var2_data.insert(state.var2_data.begin() + static_cast<std::ptrdiff_t>(insert_at),
                           record.begin(), record.end());
    put_u32(state.var2_data, block_offset, new_block_length);
    // Block payload +4 is the u16 last-column index; the payload starts four
    // bytes after the length word.
    put_u16(state.var2_data, static_cast<std::size_t>(block_offset) + 8,
            static_cast<std::uint16_t>(column_count));
    state.blocks[block_index].payload_length = new_block_length;
    for (auto& block : state.blocks) {
        if (block.offset > block_offset) {
            block.offset += kColumnRecordSize;
        }
    }
    state.var2.blocks = state.blocks;
    state.var2.size = static_cast<std::uint32_t>(state.var2_data.size());
    for (std::size_t i = 0; i < state.block_refs.size() && i < state.blocks.size(); ++i) {
        state.block_refs[i].offset = state.blocks[i].offset;
    }
    for (auto& entry : state.var_meta.entries) {
        if (entry.offset > block_offset) {
            entry.offset += kColumnRecordSize;
        }
    }
    // Keep the decoded per-table / per-view block offsets consistent too, so a
    // second edit that targets a later table still finds it in Var2Data.
    for (auto& other : state.tables) {
        if (other.block_offset > block_offset) {
            other.block_offset += kColumnRecordSize;
        }
    }
    for (auto& other : state.views) {
        if (other.main_block_offset > block_offset) {
            other.main_block_offset += kColumnRecordSize;
        }
    }
    state.var_meta.var2_size = state.var2.size;

    TableDef& target = state.tables[table_index];
    target.block_length = new_block_length;
    target.last_column_index = static_cast<std::uint16_t>(column_count);
    ColumnDef column;
    column.record_offset =
        static_cast<std::uint32_t>(kTableBlockHeaderSize + column_count * kColumnRecordSize);
    column.field_id = field_id;
    column.opaque_header = kFreshColumnHeader;
    column.title = std::string(title);
    target.columns.push_back(std::move(column));
    return true;
}

TableEdit table_date_format_edit(std::size_t table_index, std::uint8_t date_format) {
    TableEdit edit;
    edit.kind = TableEdit::Kind::date_format;
    edit.table_index = table_index;
    edit.date_format = date_format;
    return edit;
}

TableEdit table_column_replace_edit(std::size_t table_index,
                                    std::size_t column_index,
                                    std::uint32_t field_id,
                                    std::string_view title) {
    TableEdit edit;
    edit.kind = TableEdit::Kind::replace_column;
    edit.table_index = table_index;
    edit.column_index = column_index;
    edit.field_id = field_id;
    edit.title = std::string(title);
    return edit;
}

TableEdit table_column_append_edit(std::size_t table_index,
                                   std::uint32_t field_id,
                                   std::string_view title) {
    TableEdit edit;
    edit.kind = TableEdit::Kind::append_column;
    edit.table_index = table_index;
    edit.field_id = field_id;
    edit.title = std::string(title);
    return edit;
}

bool apply_table_edits(const ViewState& state,
                       std::span<const TableEdit> edits,
                       EncodedParts& out,
                       WriteError* error) {
    out = EncodedParts{};
    if (!state.parsed) {
        return write_fail(error,
                          "view state did not parse: apply_table_edits() only accepts a "
                          "ViewState produced by a successful parse_view_state()");
    }
    // Work on a copy: the caller keeps its state even if a later edit refuses.
    ViewState work = state;
    for (std::size_t i = 0; i < edits.size(); ++i) {
        const TableEdit& edit = edits[i];
        WriteError local;
        local.message = "unknown edit kind";
        bool ok = false;
        switch (edit.kind) {
            case TableEdit::Kind::date_format:
                ok = set_table_date_format(work, edit.table_index, edit.date_format, &local);
                break;
            case TableEdit::Kind::replace_column:
                ok = set_table_column(work, edit.table_index, edit.column_index, edit.field_id,
                                      edit.title, &local);
                break;
            case TableEdit::Kind::append_column:
                ok = append_table_column(work, edit.table_index, edit.field_id, edit.title, &local);
                break;
        }
        if (!ok) {
            return write_fail(error, "table edit " + std::to_string(i) + ": " + local.message);
        }
    }
    return encode_view_state(work, out, error);
}

namespace {

// ---------------------------------------------------------------------------
// Wave 17 (F1): CV_iew editing and derived-record regeneration.
// ---------------------------------------------------------------------------

// The view an Edl record names: exact caption, then accelerator-insensitive
// caption, then display name, then (only when unambiguous) the single view that
// applies the CEdl table name, then the only single view in the collection.
const ViewDef* find_applied_view(const ViewState& views,
                                 std::string_view view_name,
                                 std::string_view table_name) {
    if (views.klass != "CV_iew") {
        return nullptr;
    }
    const std::string needle = strip_accelerators(view_name);
    for (const auto& view : views.views) {
        if (view.name == view_name) {
            return &view;
        }
    }
    for (const auto& view : views.views) {
        if (strip_accelerators(view.name) == needle) {
            return &view;
        }
    }
    for (const auto& view : views.views) {
        if (!view.display_name.empty() &&
            strip_accelerators(view.display_name) == needle) {
            return &view;
        }
    }
    if (!table_name.empty()) {
        const std::string table_needle = strip_accelerators(table_name);
        const ViewDef* match = nullptr;
        std::size_t count = 0;
        for (const auto& view : views.views) {
            if (!view.looks_like_combination && !view.table_or_top.empty() &&
                strip_accelerators(view.table_or_top) == table_needle) {
                match = &view;
                ++count;
            }
        }
        if (count == 1) {
            return match;
        }
    }
    const ViewDef* only = nullptr;
    std::size_t count = 0;
    for (const auto& view : views.views) {
        if (!view.looks_like_combination && !view.table_or_top.empty()) {
            only = &view;
            ++count;
        }
    }
    return count == 1 ? only : nullptr;
}

// The table a view names, accelerator-insensitive.  Returns nullptr when no
// table matches OR when more than one does (the caller refuses rather than
// picking one).
const TableDef* find_table_for_selector(const ViewState& tables, std::string_view selector) {
    if (tables.klass != "CTable" || selector.empty()) {
        return nullptr;
    }
    const std::string needle = strip_accelerators(selector);
    const TableDef* match = nullptr;
    for (const auto& table : tables.tables) {
        if (strip_accelerators(table_selector_name(table)) == needle) {
            if (match != nullptr) {
                return nullptr;
            }
            match = &table;
        }
    }
    return match;
}

struct EdlReplacement {
    std::uint32_t id = 0;
    std::vector<std::uint8_t> payload;
};

// Rebuilds a CEdl Props document with the listed records' payloads replaced.
// Every other record (id, kind, payload), the record order, the tail and the
// header's format/declared-entry words are preserved; the two size words are
// recomputed with the input's variant delta (measured: stream_size - 4 for both
// the synthetic and the corpus CEdl documents).
bool rebuild_edl_document(const props::PropsDocument& document,
                          const std::vector<EdlReplacement>& replacements,
                          std::vector<std::uint8_t>& out,
                          WriteError* error) {
    if (document.bytes.size() < kFixedMetaHeaderSize ||
        document.header.size_word > document.bytes.size() ||
        document.tail_offset > document.bytes.size()) {
        return write_fail(error, "embedded Props document is structurally inconsistent");
    }
    for (const auto& replacement : replacements) {
        if (document.find(replacement.id) == nullptr) {
            return write_fail(error, "CEdl document has no record " + hex32(replacement.id) +
                                         " to regenerate");
        }
    }
    const auto replacement_for = [&replacements](std::uint32_t id) -> const EdlReplacement* {
        for (const auto& replacement : replacements) {
            if (replacement.id == id) {
                return &replacement;
            }
        }
        return nullptr;
    };

    std::vector<std::uint8_t> built;
    built.reserve(document.bytes.size() + 64);
    built.insert(built.end(), document.bytes.begin(),
                 document.bytes.begin() + static_cast<std::ptrdiff_t>(kFixedMetaHeaderSize));
    for (const auto& record : document.records) {
        const EdlReplacement* replacement = replacement_for(record.id);
        const std::size_t length =
            replacement != nullptr ? replacement->payload.size() : record.payload_length;
        if (length > 0xffffffffu) {
            return write_fail(error, "regenerated CEdl record " + hex32(record.id) +
                                         " does not fit a u32 payload length");
        }
        append_u32(built, static_cast<std::uint32_t>(length));
        append_u32(built, record.id);
        append_u32(built, record.kind);
        if (replacement != nullptr) {
            built.insert(built.end(), replacement->payload.begin(),
                         replacement->payload.end());
        } else {
            const std::span<const std::uint8_t> payload = document.payload(record);
            built.insert(built.end(), payload.begin(), payload.end());
        }
    }
    built.insert(built.end(),
                 document.bytes.begin() + static_cast<std::ptrdiff_t>(document.tail_offset),
                 document.bytes.end());
    const std::size_t delta = document.bytes.size() - document.header.size_word;
    if (built.size() < delta + kFixedMetaHeaderSize) {
        return write_fail(error, "regenerated CEdl document is too short for its header");
    }
    const std::uint64_t new_word = built.size() - delta;
    if (new_word > 0xffffffffu) {
        return write_fail(error, "regenerated CEdl document does not fit a u32 size word");
    }
    put_u32(built, 0, static_cast<std::uint32_t>(new_word));
    put_u32(built, 4, static_cast<std::uint32_t>(new_word));
    props::PropsDocument check;
    if (!props::parse_props(built, check, nullptr)) {
        return write_fail(error, "regenerated CEdl document does not re-parse as Props");
    }
    if (check.records.size() != document.records.size()) {
        return write_fail(error, "regenerated CEdl document re-parses to " +
                                     std::to_string(check.records.size()) + " record(s), not " +
                                     std::to_string(document.records.size()));
    }
    out = std::move(built);
    return true;
}

}  // namespace

bool set_view_name(ViewState& state,
                   std::size_t view_index,
                   std::string_view name,
                   WriteError* error) {
    if (!state.parsed) {
        return write_fail(error, "view state did not parse: set_view_name() needs a parsed state");
    }
    if (state.klass != "CV_iew") {
        return write_fail(error, "set_view_name() expects a 214/CV_iew state, not " + state.klass);
    }
    if (view_index >= state.views.size()) {
        return write_fail(error, "view index " + std::to_string(view_index) +
                                     " is out of range (" + std::to_string(state.views.size()) +
                                     " view(s))");
    }
    const ViewDef& view = state.views[view_index];
    if (view.row_index >= state.rows.size()) {
        return write_fail(error, "view " + std::to_string(view_index) + " has no decoded row");
    }
    if (state.row_size < 4 + kIdentitySize) {
        return write_fail(error, "CV_iew row size " + std::to_string(state.row_size) +
                                     " cannot hold a caption");
    }
    const std::size_t capacity = (state.row_size - 4 - kIdentitySize) / 2;
    std::string why;
    if (!printable_ascii(name, why)) {
        return write_fail(error, why);
    }
    if (name.size() > capacity) {
        return write_fail(error, "view name is " + std::to_string(name.size()) +
                                     " code units; the CV_iew caption capacity is " +
                                     std::to_string(capacity));
    }
    const std::size_t base =
        static_cast<std::size_t>(view.row_index) * state.row_size + 4;
    const std::size_t end = base + (static_cast<std::size_t>(state.row_size) - 4 - kIdentitySize);
    if (end > state.fixed_data.size()) {
        return write_fail(error, "view caption lies outside FixedData");
    }
    std::fill(state.fixed_data.begin() + static_cast<std::ptrdiff_t>(base),
              state.fixed_data.begin() + static_cast<std::ptrdiff_t>(end), std::uint8_t{0});
    for (std::size_t i = 0; i < name.size(); ++i) {
        state.fixed_data[base + i * 2] = static_cast<std::uint8_t>(name[i]);
    }
    state.rows[view.row_index].caption = std::string(name);
    state.views[view_index].name = std::string(name);
    recompute_combination_flags(state);
    return true;
}

bool set_view_table(ViewState& state,
                    std::size_t view_index,
                    std::string_view table_name,
                    WriteError* error) {
    if (!state.parsed) {
        return write_fail(error, "view state did not parse: set_view_table() needs a parsed state");
    }
    if (state.klass != "CV_iew") {
        return write_fail(error, "set_view_table() expects a 214/CV_iew state, not " + state.klass);
    }
    if (view_index >= state.views.size()) {
        return write_fail(error, "view index " + std::to_string(view_index) +
                                     " is out of range (" + std::to_string(state.views.size()) +
                                     " view(s))");
    }
    const ViewDef& view = state.views[view_index];
    if (!view.main_decoded) {
        return write_fail(error, "view " + std::to_string(view_index) +
                                     " main block is not item-decoded");
    }
    const ViewRecord* target = nullptr;
    for (const auto& record : view.records) {
        if (record.id == kViewTableKey) {
            target = &record;
            break;
        }
    }
    if (target == nullptr) {
        return write_fail(error, "view " + std::to_string(view_index) +
                                     " main block has no 0x2240000a table item");
    }
    std::string why;
    if (!printable_ascii(table_name, why)) {
        return write_fail(error, why);
    }
    if (table_name.size() > kViewTableNameCapacity) {
        return write_fail(error, "table name is " + std::to_string(table_name.size()) +
                                     " code units; the writer's bound is " +
                                     std::to_string(kViewTableNameCapacity));
    }
    const std::size_t block_index = block_index_for_offset(state, view.main_block_offset);
    if (block_index == state.blocks.size()) {
        return write_fail(error, "view " + std::to_string(view_index) +
                                     " main block is not in the Var2Data walk");
    }
    const std::span<const std::uint8_t> payload = state.block_bytes(state.blocks[block_index]);
    const std::size_t item_start = target->record_offset;
    if (item_start + kViewItemHeaderSize + target->payload_length > payload.size()) {
        return write_fail(error, "view " + std::to_string(view_index) +
                                     " table item lies outside its block");
    }
    std::vector<std::uint8_t> new_item;
    const std::vector<std::uint8_t> text = utf16_bytes(table_name, true);
    append_u32(new_item, static_cast<std::uint32_t>(text.size()));
    append_u32(new_item, target->id);
    append_u32(new_item, target->kind);
    new_item.insert(new_item.end(), text.begin(), text.end());
    const std::size_t old_stride =
        pad_to_even(kViewItemHeaderSize + static_cast<std::size_t>(target->payload_length));
    const std::size_t new_stride = pad_to_even(kViewItemHeaderSize + text.size());
    new_item.resize(new_stride, std::uint8_t{0});
    std::size_t old_end = item_start + old_stride;
    if (old_end > payload.size()) {
        old_end = payload.size();  // a missing pad after the LAST measured item
    }
    std::vector<std::uint8_t> built;
    built.reserve(payload.size() - old_stride + new_stride);
    built.insert(built.end(), payload.begin(),
                 payload.begin() + static_cast<std::ptrdiff_t>(item_start));
    built.insert(built.end(), new_item.begin(), new_item.end());
    built.insert(built.end(), payload.begin() + static_cast<std::ptrdiff_t>(old_end),
                 payload.end());
    if (built.size() < kViewMainHeaderSize) {
        return write_fail(error, "regenerated CV_iew main block is shorter than its header");
    }
    put_u32(built, 0, static_cast<std::uint32_t>(built.size() - 4));
    put_u32(built, 4, static_cast<std::uint32_t>(built.size() - 4));
    std::vector<ViewRecord> check_records;
    if (!parse_view_records(built, check_records, nullptr)) {
        return write_fail(error, "regenerated CV_iew main block does not re-parse");
    }
    if (!replace_block_payload(state, view.main_block_offset, std::move(built), error)) {
        return false;
    }
    ViewDef& changed = state.views[view_index];
    if (!decode_view_main(state, changed)) {
        return write_fail(error, "regenerated CV_iew main block could not be decoded back");
    }
    recompute_combination_flags(state);
    return true;
}

bool set_view_page_margins(ViewState& state,
                           std::size_t view_index,
                           const std::array<std::uint32_t, 4>& margins,
                           WriteError* error) {
    if (!state.parsed) {
        return write_fail(error,
                          "view state did not parse: set_view_page_margins() needs a parsed state");
    }
    if (state.klass != "CV_iew") {
        return write_fail(error, "set_view_page_margins() expects a 214/CV_iew state, not " +
                                     state.klass);
    }
    if (view_index >= state.views.size()) {
        return write_fail(error, "view index " + std::to_string(view_index) +
                                     " is out of range (" + std::to_string(state.views.size()) +
                                     " view(s))");
    }
    const ViewDef& view = state.views[view_index];
    if (!view.options_decoded) {
        return write_fail(error, "view " + std::to_string(view_index) +
                                     " has no decoded 970-byte 0x22400009 options item");
    }
    for (const std::uint32_t margin : margins) {
        if (margin > 100000u) {
            return write_fail(error, "page margin " + std::to_string(margin) +
                                         " exceeds the writer's 100000 (100 inch) bound");
        }
    }
    const ViewRecord* target = nullptr;
    for (const auto& record : view.records) {
        if (record.id == kViewOptionsKey && record.payload_length == kViewOptionsSize) {
            target = &record;
            break;
        }
    }
    const std::size_t block_index = block_index_for_offset(state, view.main_block_offset);
    if (target == nullptr || block_index == state.blocks.size()) {
        return write_fail(error, "view " + std::to_string(view_index) +
                                     " options item is not in its main block");
    }
    const std::size_t payload_start =
        static_cast<std::size_t>(state.blocks[block_index].offset) + 4;
    const std::size_t base = payload_start + target->record_offset + kViewItemHeaderSize +
                             kViewOptionsMarginOffset;
    if (base + kViewOptionsMarginSize > state.var2_data.size()) {
        return write_fail(error, "view " + std::to_string(view_index) +
                                     " options margins lie outside Var2Data");
    }
    for (std::size_t i = 0; i < margins.size(); ++i) {
        put_u32(state.var2_data, base + i * 4, margins[i]);
    }
    state.views[view_index].page_margins_thousandths = margins;
    return true;
}

bool regenerate_edl(ViewState& state,
                    const ViewState& from_table_state,
                    WriteError* error) {
    if (!state.parsed) {
        return write_fail(error, "view state did not parse: regenerate_edl() needs a parsed state");
    }
    if (state.klass != "CEdl") {
        return write_fail(error, "regenerate_edl() expects a 214/CEdl state, not " + state.klass);
    }
    if (!from_table_state.parsed || from_table_state.klass != "CTable") {
        return write_fail(error,
                          "regenerate_edl() needs a parsed 214/CTable state as its source");
    }
    if (state.embedded_props.empty()) {
        return write_fail(error, "CEdl has no decoded Props block");
    }

    // Identify the current Props block.  One block: it is the one.  Several
    // (corpus files cache one per row): use the block named by the first fixed
    // row's ordinal, then a block whose view name matches a linked view.
    std::size_t target_index = state.embedded_props.size();
    if (state.embedded_props.size() == 1) {
        target_index = 0;
    } else if (!state.rows.empty()) {
        const ViewBlockRef* ref = state.find_block_ref(state.rows[0].ordinal, kColumnBlockKey);
        if (ref != nullptr) {
            for (std::size_t i = 0; i < state.embedded_props.size(); ++i) {
                if (state.embedded_props[i].block_offset == ref->offset) {
                    target_index = i;
                    break;
                }
            }
        }
    }
    if (target_index == state.embedded_props.size()) {
        return write_fail(error, "CEdl has " + std::to_string(state.embedded_props.size()) +
                                     " decoded Props block(s); the current one cannot be "
                                     "identified from the first fixed row");
    }
    const EmbeddedProps& edl = state.embedded_props[target_index];
    const ViewState* views = from_table_state.find_related("CV_iew");
    if (views != nullptr && (!views->parsed || views->klass != "CV_iew")) {
        return write_fail(error, "linked 214/CV_iew state is not a parsed CV_iew collection");
    }

    const ViewDef* applied_view = nullptr;
    if (views != nullptr) {
        applied_view = find_applied_view(*views, edl.view_name, edl.table_name);
        if (applied_view == nullptr) {
            return write_fail(error, "CEdl view name \"" + edl.view_name +
                                         "\" does not resolve to a view in the linked 214/CV_iew");
        }
    }
    std::string table_selector =
        applied_view != nullptr ? applied_view->table_or_top : edl.table_name;
    if (table_selector.empty()) {
        table_selector = edl.table_name;
    }
    const TableDef* table = find_table_for_selector(from_table_state, table_selector);
    if (table == nullptr) {
        return write_fail(error, "CEdl applied table \"" + table_selector +
                                     "\" does not resolve to exactly one decoded table");
    }
    if (!table->decoded) {
        return write_fail(error, "CEdl applied table \"" + table_selector +
                                     "\" has no decoded 0x20400006 column block");
    }

    std::vector<EdlReplacement> replacements;
    if (applied_view != nullptr) {
        EdlReplacement view_name;
        view_name.id = kEdlViewNameKey;
        view_name.payload = utf16_bytes(applied_view->name, true);
        replacements.push_back(std::move(view_name));
    }
    EdlReplacement table_name;
    table_name.id = kEdlTableNameKey;
    table_name.payload = utf16_bytes(table_selector_name(*table), true);
    replacements.push_back(std::move(table_name));

    std::vector<std::uint8_t> fields;
    for (std::size_t column = 1; column < table->columns.size(); ++column) {
        append_u32(fields, table->columns[column].field_id);
    }
    if (edl.field_list_terminated) {
        append_u32(fields, kNoFieldId);
    }
    EdlReplacement field_list;
    field_list.id = kEdlFieldListKey;
    field_list.payload = std::move(fields);
    replacements.push_back(std::move(field_list));

    std::vector<std::uint8_t> built;
    if (!rebuild_edl_document(edl.document, replacements, built, error)) {
        return false;
    }
    if (built == edl.document.bytes) {
        return true;  // already consistent: nothing to change
    }
    const std::uint32_t block_offset = edl.block_offset;
    if (!replace_block_payload(state, block_offset, std::move(built), error)) {
        return false;
    }
    const std::size_t index = block_index_for_offset(state, block_offset);
    if (index == state.blocks.size()) {
        return write_fail(error, "regenerated CEdl block is not in the Var2Data walk");
    }
    EmbeddedProps refreshed;
    if (!decode_edl_block(state, state.blocks[index], refreshed)) {
        return write_fail(error, "regenerated CEdl block does not parse back as Props");
    }
    state.embedded_props[target_index] = std::move(refreshed);
    return true;
}

bool regenerate_view_state(ViewState& state, const TableEdits& edits, WriteError* error) {
    if (!state.parsed) {
        return write_fail(error,
                          "view state did not parse: regenerate_view_state() needs a parsed "
                          "ViewState");
    }
    if (state.klass != "CTable") {
        return write_fail(error,
                          "regenerate_view_state() expects the 214/CTable state, not " + state.klass);
    }
    ViewState table = state;
    for (std::size_t i = 0; i < edits.size(); ++i) {
        const TableEdit& edit = edits[i];
        WriteError local;
        local.message = "unknown edit kind";
        bool ok = false;
        switch (edit.kind) {
            case TableEdit::Kind::date_format:
                ok = set_table_date_format(table, edit.table_index, edit.date_format, &local);
                break;
            case TableEdit::Kind::replace_column:
                ok = set_table_column(table, edit.table_index, edit.column_index, edit.field_id,
                                      edit.title, &local);
                break;
            case TableEdit::Kind::append_column:
                ok = append_table_column(table, edit.table_index, edit.field_id, edit.title,
                                         &local);
                break;
        }
        if (!ok) {
            return write_fail(error, "table edit " + std::to_string(i) + ": " + local.message);
        }
    }
    ViewState* edl_state = state.find_related("CEdl");
    ViewState* views_state = state.find_related("CV_iew");
    ViewState edl_work;
    const bool have_edl = edl_state != nullptr;
    if (have_edl) {
        if (!edl_state->parsed) {
            return write_fail(error, "linked 214/CEdl state did not parse");
        }
        edl_work = *edl_state;
        if (!regenerate_edl(edl_work, table, error)) {
            return false;
        }
    }
    if (views_state != nullptr && !views_state->parsed) {
        return write_fail(error, "linked 214/CV_iew state did not parse");
    }
    for (const std::string_view klass : {"CFilter", "CGrouping"}) {
        const ViewState* related = state.find_related(klass);
        if (related != nullptr && !related->parsed) {
            return write_fail(error, "linked 214/" + std::string(klass) + " state did not parse");
        }
    }
    // Everything validated; commit.
    state = std::move(table);
    if (have_edl) {
        *edl_state = std::move(edl_work);
    }
    return true;
}

}  // namespace msproject::viewstate
