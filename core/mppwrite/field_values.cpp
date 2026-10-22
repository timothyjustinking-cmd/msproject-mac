/*
 * field_values.cpp -- write custom-field values, prove them from disk.
 * See field_values.h for the measured encodings, the API contract and the
 * deliberate refusals.  W48 (2026-10-22) wrote Task fields; W60 (2026-10-22)
 * adds Cost1..Cost10 in the Task table and Number1..20 / Text1..30 on
 * assignments (114/TBkndAssn, keyed by the assignment property id).
 *
 * The shape is the same as the other writer entry points: validate everything
 * against the current document, apply on a copy, write, re-open from disk, and
 * prove every byte/entry that moved.  Two things Project requires beyond the
 * VarMeta value itself are implemented here because the W48 Project gate proved
 * them necessary:
 *   - the value entry must sit inside its uid's CONTIGUOUS group in VarMeta
 *     (table.h's insert_var_entry);
 *   - the field definition must be registered in 114/TBkndTask/Props (and
 *     mirrored in 114/TBkndAssn/Props), with Project's own 88-byte records.
 */

#include "field_values.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace mppwrite {
namespace {

constexpr std::size_t kFixedMetaHeaderSize = 16;
constexpr std::size_t kFixedMetaEntrySize = 47;
constexpr std::size_t kFixedMetaStateByte = 0;
constexpr std::size_t kFixedMetaFieldCount = 2;
constexpr std::size_t kFixedMetaRowOffset = 4;
constexpr std::size_t kFixedMetaFlag1Byte = 24;
constexpr std::uint8_t kFixedMetaFlag1Bit = 0x02;

constexpr std::size_t kFixed2MetaHeaderSize = 16;
constexpr std::size_t kFixed2MetaEntrySize = 96;
/* Project's own Flag1 write sets bit 0x80 at Fixed2Meta entry bytes +31 and
 * +61 (measured, w10-flag-on vs w10-flag-off).  The writer mirrors it; whether
 * Project needs it on a fresh open is unmeasured. */
constexpr std::size_t kFixed2MetaFlag1ByteA = 31;
constexpr std::size_t kFixed2MetaFlag1ByteB = 61;
constexpr std::uint8_t kFixed2MetaFlag1Bit = 0x80;

constexpr std::uint8_t kStateLive = 0x00;
constexpr std::uint32_t kSecondaryUidFamily = 0x80000000u;
/* A text value longer than this is refused rather than written: a block is
 * length-prefixed with a u32, and Project's own strings are far smaller. */
constexpr std::size_t kMaxTextBytes = 65535;

/*
 * FixedMeta entry TAIL PRESENCE BITS (measured, W48 third Project pass, on
 * corpus/probe/w11-fields-base.mpp + the single-field probes
 * w11-fields-number1/number2/duration1/date1/flag1.mpp: Project's own save of
 * the SAME synthetic base).  These are a second, independent "this field has a
 * value" store in the 47-byte FixedMeta entry; without them Project shows the
 * value as empty even when VarMeta and Props are byte-perfect.
 *
 *   Text1     0x0b400033  +21 |= 0x20
 *   Flag1     0x0b400048  +24 |= 0x02            (also the bool itself)
 *   Number1   0x0b400057  +25 |= 0x08
 *   Number2   0x0b400058  +25 |= 0x10
 *   Duration1 0x0b400067  +26 |= 0x01, +27 |= 0x20
 *   Date1     0x0b400109  +29 |= 0x40
 *
 * The six-field OR is +21=0x20, +24=0x02, +25=0x18, +26=0x01, +27=0x20,
 * +29=0x40.  W60 adds Cost1..Cost10 (Cost1 = +26 0x08 measured; the rest from
 * the binary class-slot bitmap -- see field_values.h).  Clearing a flag clears
 * only +24 bit 0x02; a value field's bits are never cleared (there is no
 * measured clear shape).
 */
struct TailBit {
    std::size_t offset = 0;
    std::uint8_t mask = 0;
};

std::vector<TailBit> tail_bits_for(std::uint32_t field_id) {
    switch (field_id) {
        case 0x0B400033u:
            return {{21, 0x20}};
        case 0x0B400048u:
            return {{24, 0x02}};
        case 0x0B400057u:
            return {{25, 0x08}};
        case 0x0B400058u:
            return {{25, 0x10}};
        case 0x0B400067u:
            return {{26, 0x01}, {27, 0x20}};
        case 0x0B400109u:
            return {{29, 0x40}};
        /* W60 Cost bits: the tail bitmap is indexed by the field's decompiled
         * class slot (byte +21 + slot/8).  Cost1's +26 0x08 is MEASURED
         * (w14-cost); Cost2..Cost10 continue the binary's slots
         * 44,45,63..69 -- see field_values.h. */
        case 0x0B40006Au:
            return {{26, 0x08}};
        case 0x0B40006Bu:
            return {{26, 0x10}};
        case 0x0B40006Cu:
            return {{26, 0x20}};
        case 0x0B400102u:
            return {{28, 0x80}};
        case 0x0B400103u:
            return {{29, 0x01}};
        case 0x0B400104u:
            return {{29, 0x02}};
        case 0x0B400105u:
            return {{29, 0x04}};
        case 0x0B400106u:
            return {{29, 0x08}};
        case 0x0B400107u:
            return {{29, 0x10}};
        case 0x0B400108u:
            return {{29, 0x20}};
        default:
            return {};
    }
}

/* Project also stores an internal 2-byte VarMeta marker 0x0b4000b7 when a
 * Duration custom field is used (payload 07 00), inserted after the last
 * overtime entry and before 0x0b4004e5 -- plain ascending key order. */
constexpr std::uint32_t kDurationHelperFieldId = 0x0B4000B7u;

/*
 * THE CUSTOM-FIELD REGISTRY: 114/TBkndTask/Props and 114/TBkndAssn/Props.
 *
 * MEASURED (W48 Project gate, 2026-10-22).  Registering the field in VarMeta is
 * necessary but NOT sufficient: Project only displays a custom field whose
 * definition is also registered in the Task table's Props stream (and, in its own
 * file, mirrored in the Assignment table's).  Project's own
 * corpus/probe/w10-fieldvalues.mpp has Task/Props = 614 bytes = an 86-byte
 * preamble + 6 * 88-byte records, and Assn/Props = 616 = 88 + 6 * 88; the
 * committed tests/data/synthetic-base.mpp has the preambles only (86 / 88).
 *
 * Task preamble words (measured by diffing the two files):
 *   u32 +0  = size-4                 (86 -> 82 in base; 614 -> 610)
 *   u32 +4  = size-4                 (the same word again)
 *   u16 +30 = 44 + 88*records        (44 in base; 572; high half of u32 +28)
 *   u32 +78 = record count           (0 in base; 6)
 *   u32 +82 = 88 * record count      (0 in base; 528)
 * Assignment preamble words:
 *   u32 +0  = size-4                 (88 -> 84; 616 -> 612)
 *   u32 +4  = size-4
 *   u32 +32 = 44 + 88*records        (44 in base; 572)
 *   u32 +80 = record count           (0; 6)
 *   u32 +84 = 88 * record count      (0; 528)
 * Everything else in the preamble is unchanged.  The count/length words were
 * the second gate failure: records present but the words still zero made
 * Project display nothing.
 */
struct PropsLayout {
    std::size_t preamble = 0;
    std::size_t total_word = 0;
    bool total_high_u16 = false;
    std::size_t count_word = 0;
    std::size_t bytes_word = 0;
};
constexpr PropsLayout kTaskPropsLayout = {86, 30, true, 78, 82};
constexpr PropsLayout kAssnPropsLayout = {88, 32, false, 80, 84};

constexpr std::size_t kPropsRecordSize = 88;
constexpr std::size_t kPropsSizeWordA = 0; /* u32 = total size - 4 */
constexpr std::size_t kPropsSizeWordB = 4; /* u32 = total size - 4 */

struct PropsTemplate {
    std::uint32_t field_id;
    std::array<std::uint8_t, kPropsRecordSize> bytes;
};

/*
 * Task/Props records, copied byte-for-byte from corpus/probe/w10-fieldvalues.mpp
 * at offsets 86,174,262,350,438,526 (synthetic-fixture bytes, not client data).
 * Field id is the first u32 LE; the 8-byte stamp at +24 and the reversed field id
 * at +32 are Project's own and are not understood.  A field without a record is
 * refused rather than invented: the record encodes the field's format.
 */
constexpr std::array<PropsTemplate, 7> kPropsTemplates = {{
    {0x0B400033u,
     {0x33, 0x00, 0x40, 0x0b, 0x08, 0x00, 0x00, 0x00, 0x64, 0x40, 0x40, 0x0f,
      0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb7, 0x39, 0x00, 0x00,
      0xbe, 0x8b, 0xeb, 0x4c, 0x82, 0xc4, 0xfa, 0x8c, 0x0b, 0x40, 0x00, 0x33,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00}},
    {0x0B400048u,
     {0x48, 0x00, 0x40, 0x0b, 0x0b, 0x00, 0x00, 0x00, 0x0a, 0x40, 0x40, 0x0f,
      0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb7, 0x39, 0x00, 0x00,
      0xbe, 0x8b, 0xeb, 0x4c, 0x82, 0xc4, 0xfa, 0x8c, 0x0b, 0x40, 0x00, 0x48,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00}},
    {0x0B400057u,
     {0x57, 0x00, 0x40, 0x0b, 0x05, 0x00, 0x00, 0x00, 0x3c, 0x40, 0x40, 0x0f,
      0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb7, 0x39, 0x00, 0x00,
      0xbe, 0x8b, 0xeb, 0x4c, 0x82, 0xc4, 0xfa, 0x8c, 0x0b, 0x40, 0x00, 0x57,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00}},
    {0x0B400058u,
     {0x58, 0x00, 0x40, 0x0b, 0x05, 0x00, 0x00, 0x00, 0x3d, 0x40, 0x40, 0x0f,
      0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb7, 0x39, 0x00, 0x00,
      0xbe, 0x8b, 0xeb, 0x4c, 0x82, 0xc4, 0xfa, 0x8c, 0x0b, 0x40, 0x00, 0x58,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00}},
    {0x0B400067u,
     {0x67, 0x00, 0x40, 0x0b, 0x03, 0x00, 0x00, 0x00, 0x28, 0x40, 0x40, 0x0f,
      0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb7, 0x39, 0x00, 0x00,
      0xbe, 0x8b, 0xeb, 0x4c, 0x82, 0xc4, 0xfa, 0x8c, 0x0b, 0x40, 0x00, 0x67,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00}},
    {0x0B400109u,
     {0x09, 0x01, 0x40, 0x0b, 0x13, 0x00, 0x00, 0x00, 0x00, 0x40, 0x40, 0x0f,
      0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb7, 0x39, 0x00, 0x00,
      0xbe, 0x8b, 0xeb, 0x4c, 0x82, 0xc4, 0xfa, 0x8c, 0x0b, 0x40, 0x01, 0x09,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00}},
    /* Cost1 -- W60, copied byte-for-byte from corpus/probe/w14-cost.mpp's
     * Task/Props at offset 86.  The assignment-property key at +8 is
     * 0x0f40401e (Cost1); +16 is 0 and +20 is 14775 in this measured record. */
    {0x0B40006Au,
     {0x6a, 0x00, 0x40, 0x0b, 0x05, 0x00, 0x00, 0x00, 0x1e, 0x40, 0x40, 0x0f,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb7, 0x39, 0x00, 0x00,
      0xbe, 0x8b, 0xeb, 0x4c, 0x82, 0xc4, 0xfa, 0x8c, 0x0b, 0x40, 0x00, 0x6a,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00}},
}};

/* Cost1's record is in the table above; Cost2..Cost10 use the same measured
 * shape with their own field id and assignment property key (built below). */
bool cost_task_props_record(std::uint32_t field_id,
                            std::array<std::uint8_t, kPropsRecordSize>& out);
bool cost_assn_props_record(std::uint32_t field_id,
                            std::array<std::uint8_t, kPropsRecordSize>& out);

bool props_template_for(std::uint32_t field_id,
                        std::array<std::uint8_t, kPropsRecordSize>& out) {
    for (PropsTemplate const& entry : kPropsTemplates) {
        if (entry.field_id == field_id) {
            out = entry.bytes;
            return true;
        }
    }
    return cost_task_props_record(field_id, out);
}

/*
 * Assn/Props records, copied from the same file at offsets 88,176,264,352,440,
 * 528, keyed by the TASK field id at record +8.  Project sorts them by the
 * ASSIGNMENT property id (the record's first u32: 0x09,0x0a,0x28,0x3c,0x3d,0x64).
 */
struct AssnPropsTemplate {
    std::uint32_t task_field_id;
    std::array<std::uint8_t, kPropsRecordSize> bytes;
};

constexpr std::array<AssnPropsTemplate, 7> kAssnPropsTemplates = {{
    {0x0B400109u,
     {0x00, 0x40, 0x40, 0x0f, 0x13, 0x00, 0x00, 0x00, 0x09, 0x01, 0x40, 0x0b,
      0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb7, 0x39, 0x00, 0x00,
      0xbe, 0x8b, 0xeb, 0x4c, 0x82, 0xc4, 0xfa, 0x8c, 0x0f, 0x40, 0x40, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00}},
    {0x0B400048u,
     {0x0a, 0x40, 0x40, 0x0f, 0x0b, 0x00, 0x00, 0x00, 0x48, 0x00, 0x40, 0x0b,
      0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb7, 0x39, 0x00, 0x00,
      0xbe, 0x8b, 0xeb, 0x4c, 0x82, 0xc4, 0xfa, 0x8c, 0x0f, 0x40, 0x40, 0x0a,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00}},
    {0x0B400067u,
     {0x28, 0x40, 0x40, 0x0f, 0x1d, 0x00, 0x00, 0x00, 0x67, 0x00, 0x40, 0x0b,
      0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb7, 0x39, 0x00, 0x00,
      0xbe, 0x8b, 0xeb, 0x4c, 0x82, 0xc4, 0xfa, 0x8c, 0x0f, 0x40, 0x40, 0x28,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00}},
    {0x0B400057u,
     {0x3c, 0x40, 0x40, 0x0f, 0x05, 0x00, 0x00, 0x00, 0x57, 0x00, 0x40, 0x0b,
      0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb7, 0x39, 0x00, 0x00,
      0xbe, 0x8b, 0xeb, 0x4c, 0x82, 0xc4, 0xfa, 0x8c, 0x0f, 0x40, 0x40, 0x3c,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00}},
    {0x0B400058u,
     {0x3d, 0x40, 0x40, 0x0f, 0x05, 0x00, 0x00, 0x00, 0x58, 0x00, 0x40, 0x0b,
      0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb7, 0x39, 0x00, 0x00,
      0xbe, 0x8b, 0xeb, 0x4c, 0x82, 0xc4, 0xfa, 0x8c, 0x0f, 0x40, 0x40, 0x3d,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00}},
    {0x0B400033u,
     {0x64, 0x40, 0x40, 0x0f, 0x08, 0x00, 0x00, 0x00, 0x33, 0x00, 0x40, 0x0b,
      0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb7, 0x39, 0x00, 0x00,
      0xbe, 0x8b, 0xeb, 0x4c, 0x82, 0xc4, 0xfa, 0x8c, 0x0f, 0x40, 0x40, 0x64,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00}},
    /* Cost1 -- W60, copied byte-for-byte from corpus/probe/w14-cost.mpp's
     * Assn/Props at offset 88: key 0x0f40401e at +0, PjTask Cost1 at +8. */
    {0x0B40006Au,
     {0x1e, 0x40, 0x40, 0x0f, 0x05, 0x00, 0x00, 0x00, 0x6a, 0x00, 0x40, 0x0b,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb7, 0x39, 0x00, 0x00,
      0xbe, 0x8b, 0xeb, 0x4c, 0x82, 0xc4, 0xfa, 0x8c, 0x0f, 0x40, 0x40, 0x1e,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00}},
}};

/* One assignment-side Props record, assembled from the measured type words of
 * the Text1/Number1 records (the four varying words are the field id, the
 * assignment property key, and the two type words; +16 is 0, +20 is 14775 and
 * +24/+28 are Project's two fixed stamps in every fresh-field record). */
constexpr std::uint32_t kPropsOpaque20 = 0x000039B7u; /* 14775 */
constexpr std::uint32_t kPropsOpaque24 = 0x4CEB8BBEu;
constexpr std::uint32_t kPropsOpaque28 = 0x8CFAC482u;

void put_be32(std::array<std::uint8_t, kPropsRecordSize>& record, std::size_t offset,
              std::uint32_t value) {
    record[offset] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
    record[offset + 1] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
    record[offset + 2] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
    record[offset + 3] = static_cast<std::uint8_t>(value & 0xFFu);
}

template <std::size_t N>
void put_u32(std::array<std::uint8_t, N>& b, std::size_t o, std::uint32_t v) {
    b[o] = static_cast<std::uint8_t>(v & 0xFFu);
    b[o + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
    b[o + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
    b[o + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
}

struct AssignmentPropsShape {
    std::uint32_t type_a = 0; /* record +4 */
    std::uint32_t type_b = 0; /* record +12 */
};

bool assignment_props_shape(FieldKind kind, AssignmentPropsShape& out) {
    switch (kind) {
        case FieldKind::text:
            out.type_a = 8;
            out.type_b = 7;
            return true;
        case FieldKind::number:
            out.type_a = 5;
            out.type_b = 5;
            return true;
        case FieldKind::duration:
        case FieldKind::date:
        case FieldKind::flag:
        case FieldKind::cost:
        case FieldKind::unknown:
            break;
    }
    return false;
}

/* Task/Props record for an assignment field: keyed by the PjTask field id,
 * carrying the assignment key at +8 (measured w14-assignfield.mpp). */
std::array<std::uint8_t, kPropsRecordSize> assignment_task_props_record(
    std::uint32_t field_id, std::uint32_t property_id,
    AssignmentPropsShape const& shape) {
    std::array<std::uint8_t, kPropsRecordSize> record{};
    put_u32(record, 0, field_id);
    put_u32(record, 4, shape.type_a);
    put_u32(record, 8, property_id);
    put_u32(record, 12, shape.type_b);
    put_u32(record, 20, kPropsOpaque20);
    put_u32(record, 24, kPropsOpaque24);
    put_u32(record, 28, kPropsOpaque28);
    put_be32(record, 32, field_id);
    return record;
}

/* Assn/Props record for an assignment field: keyed by the assignment property
 * id, carrying the PjTask field id at +8 (measured w14-assignfield.mpp). */
std::array<std::uint8_t, kPropsRecordSize> assignment_assn_props_record(
    std::uint32_t field_id, std::uint32_t property_id,
    AssignmentPropsShape const& shape) {
    std::array<std::uint8_t, kPropsRecordSize> record{};
    put_u32(record, 0, property_id);
    put_u32(record, 4, shape.type_a);
    put_u32(record, 8, field_id);
    put_u32(record, 12, shape.type_b);
    put_u32(record, 20, kPropsOpaque20);
    put_u32(record, 24, kPropsOpaque24);
    put_u32(record, 28, kPropsOpaque28);
    put_be32(record, 32, property_id);
    return record;
}

bool assn_props_template_for(std::uint32_t task_field_id,
                             std::array<std::uint8_t, kPropsRecordSize>& out) {
    for (AssnPropsTemplate const& entry : kAssnPropsTemplates) {
        if (entry.task_field_id == task_field_id) {
            out = entry.bytes;
            return true;
        }
    }
    return cost_assn_props_record(task_field_id, out);
}

/* The assignment-side property key of a Cost field: Cost1's 0x0f40401e is
 * measured (w14-cost's Assn/Props record); Cost2..Cost10 continue the stride-1
 * sequence (the same family layout as Number/Text, whose keys are measured). */
std::uint32_t cost_property_id(std::uint32_t field_id) {
    if (field_kind_for_id(field_id) != FieldKind::cost) {
        return 0;
    }
    unsigned const index = field_index_for_id(field_id);
    if (index < 1 || index > 10) {
        return 0;
    }
    return 0x0F40401Eu + (index - 1u);
}

bool cost_task_props_record(std::uint32_t field_id,
                            std::array<std::uint8_t, kPropsRecordSize>& out) {
    std::uint32_t const key = cost_property_id(field_id);
    if (key == 0) {
        return false;
    }
    out = {};
    put_u32(out, 0, field_id);
    put_u32(out, 4, 5);  /* the i64 cost type word, measured on Cost1 */
    put_u32(out, 8, key);
    put_u32(out, 12, 0); /* 0 in the measured Cost1 record (not Number's 5) */
    put_u32(out, 20, kPropsOpaque20);
    put_u32(out, 24, kPropsOpaque24);
    put_u32(out, 28, kPropsOpaque28);
    put_be32(out, 32, field_id);
    return true;
}

bool cost_assn_props_record(std::uint32_t field_id,
                            std::array<std::uint8_t, kPropsRecordSize>& out) {
    std::uint32_t const key = cost_property_id(field_id);
    if (key == 0) {
        return false;
    }
    out = {};
    put_u32(out, 0, key);
    put_u32(out, 4, 5);
    put_u32(out, 8, field_id);
    put_u32(out, 12, 0);
    put_u32(out, 20, kPropsOpaque20);
    put_u32(out, 24, kPropsOpaque24);
    put_u32(out, 28, kPropsOpaque28);
    put_be32(out, 32, key);
    return true;
}

std::string hex_id(std::uint32_t value) {
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "0x%08x", value);
    return std::string(buffer);
}

char const* kind_name(FieldKind kind) {
    switch (kind) {
        case FieldKind::text:
            return "Text";
        case FieldKind::number:
            return "Number";
        case FieldKind::duration:
            return "Duration";
        case FieldKind::date:
            return "Date";
        case FieldKind::flag:
            return "Flag";
        case FieldKind::cost:
            return "Cost";
        case FieldKind::unknown:
            return "unknown";
    }
    return "unknown";
}

std::string table_stream(std::string_view table, std::string_view name) {
    return std::string(table) + "/" + std::string(name);
}

std::uint16_t get_u16(std::vector<std::uint8_t> const& b, std::size_t o) {
    return static_cast<std::uint16_t>(b[o] | (b[o + 1] << 8));
}

std::uint32_t get_u32(std::vector<std::uint8_t> const& b, std::size_t o) {
    return static_cast<std::uint32_t>(b[o]) |
           (static_cast<std::uint32_t>(b[o + 1]) << 8) |
           (static_cast<std::uint32_t>(b[o + 2]) << 16) |
           (static_cast<std::uint32_t>(b[o + 3]) << 24);
}

void put_u16(std::vector<std::uint8_t>& b, std::size_t o, std::uint16_t v) {
    b[o] = static_cast<std::uint8_t>(v & 0xFFu);
    b[o + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
}

void put_u32(std::vector<std::uint8_t>& b, std::size_t o, std::uint32_t v) {
    b[o] = static_cast<std::uint8_t>(v & 0xFFu);
    b[o + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
    b[o + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
    b[o + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
}

/* ---- the Props registries, per table ------------------------------------ */

/* The same little-endian u32 read for a fixed-size record (records are
 * std::array, streams are std::vector). */
template <std::size_t N>
std::uint32_t get_u32(std::array<std::uint8_t, N> const& b, std::size_t o) {
    return static_cast<std::uint32_t>(b[o]) |
           (static_cast<std::uint32_t>(b[o + 1]) << 8) |
           (static_cast<std::uint32_t>(b[o + 2]) << 16) |
           (static_cast<std::uint32_t>(b[o + 3]) << 24);
}

std::uint32_t props_total(std::size_t records) {
    return static_cast<std::uint32_t>(44 + kPropsRecordSize * records);
}

void validate_props_stream(std::vector<std::uint8_t> const& props,
                           PropsLayout const& layout, std::string const& path,
                           std::string const& function) {
    if (props.size() < layout.preamble ||
        (props.size() - layout.preamble) % kPropsRecordSize != 0) {
        throw Error(function + ": " + path + " is " +
                    std::to_string(props.size()) + " bytes, not " +
                    std::to_string(layout.preamble) +
                    " + 88*n; refusing to add a field registration to a stream "
                    "this writer does not understand");
    }
    std::size_t const records = (props.size() - layout.preamble) / kPropsRecordSize;
    bool const total_ok =
        layout.total_high_u16
            ? get_u16(props, layout.total_word) == props_total(records) &&
                  get_u16(props, layout.total_word) == props.size() - 42
            : get_u32(props, layout.total_word) == props_total(records);
    if (get_u32(props, kPropsSizeWordA) != props.size() - 4 ||
        get_u32(props, kPropsSizeWordB) != props.size() - 4 || !total_ok ||
        get_u32(props, layout.count_word) != records ||
        get_u32(props, layout.bytes_word) != records * kPropsRecordSize) {
        throw Error(function + ": " + path +
                    " does not carry the measured size/count words for its layout; "
                    "refusing to rewrite it");
    }
}

void write_props_words(std::vector<std::uint8_t>& props, PropsLayout const& layout,
                       std::string const& path, std::string const& function) {
    std::size_t const records = (props.size() - layout.preamble) / kPropsRecordSize;
    std::uint32_t const total = props_total(records);
    if (props.size() < 4 || props.size() - 4 > 0xFFFFFFFFu ||
        records * kPropsRecordSize > 0xFFFFFFFFu || records > 0xFFFFFFFFu ||
        (layout.total_high_u16 && total > 0xFFFFu)) {
        throw Error(function + ": " + path + " would be " +
                    std::to_string(props.size()) + " bytes with " +
                    std::to_string(records) +
                    " records, too large for its size/count words; refusing");
    }
    put_u32(props, kPropsSizeWordA, static_cast<std::uint32_t>(props.size() - 4));
    put_u32(props, kPropsSizeWordB, static_cast<std::uint32_t>(props.size() - 4));
    if (layout.total_high_u16) {
        put_u16(props, layout.total_word, static_cast<std::uint16_t>(total));
    } else {
        put_u32(props, layout.total_word, total);
    }
    put_u32(props, layout.count_word, static_cast<std::uint32_t>(records));
    put_u32(props, layout.bytes_word,
            static_cast<std::uint32_t>(records * kPropsRecordSize));
}

/*
 * Insert records (sorted by the record's first u32) into the Props stream,
 * skipping a key that is already registered.  Existing records are never
 * rewritten.  Returns how many records were added.
 */
std::size_t register_props_records(
    std::vector<std::uint8_t>& props, PropsLayout const& layout,
    std::vector<std::array<std::uint8_t, kPropsRecordSize>> records) {
    std::sort(records.begin(), records.end(),
              [](std::array<std::uint8_t, kPropsRecordSize> const& a,
                 std::array<std::uint8_t, kPropsRecordSize> const& b) {
                  return get_u32(a, 0) < get_u32(b, 0);
              });
    std::size_t added = 0;
    for (auto const& record : records) {
        std::uint32_t const key = get_u32(record, 0);
        bool present = false;
        std::size_t insert_at = props.size();
        for (std::size_t offset = layout.preamble; offset < props.size();
             offset += kPropsRecordSize) {
            std::uint32_t const registered = get_u32(props, offset);
            if (registered == key) {
                present = true;
                break;
            }
            if (registered > key) {
                insert_at = offset;
                break;
            }
        }
        if (present) {
            continue;
        }
        props.insert(props.begin() + static_cast<std::ptrdiff_t>(insert_at),
                     record.begin(), record.end());
        ++added;
    }
    return added;
}

/* Prove one Props registry from the file on disk. */
void verify_props_stream(
    CfbDocument const& reread, std::string const& path, PropsLayout const& layout,
    std::vector<std::uint8_t> const& before,
    std::vector<std::array<std::uint8_t, kPropsRecordSize>> const& expected,
    std::size_t expected_added, std::string const& function) {
    std::vector<std::uint8_t> const after = reread.read_stream(path);
    std::size_t const expected_size = before.size() + expected_added * kPropsRecordSize;
    if (after.size() != expected_size) {
        throw Error(function + ": " + path + " is " +
                    std::to_string(after.size()) + " bytes, expected " +
                    std::to_string(expected_size));
    }
    for (std::size_t i = 0; i < layout.preamble; ++i) {
        auto in = [i](std::size_t at, std::size_t width) {
            return i >= at && i < at + width;
        };
        bool const may_move =
            in(kPropsSizeWordA, 4) || in(kPropsSizeWordB, 4) ||
            in(layout.count_word, 4) || in(layout.bytes_word, 4) ||
            in(layout.total_word, layout.total_high_u16 ? 2u : 4u);
        if (!may_move && after[i] != before[i]) {
            throw Error(function + ": " + path + " preamble byte +" +
                        std::to_string(i) +
                        " changed; only the measured size/count words may move");
        }
    }
    validate_props_stream(after, layout, path, function);
    auto records = [&layout](std::vector<std::uint8_t> const& stream) {
        std::map<std::uint32_t, std::vector<std::uint8_t>> out;
        for (std::size_t offset = layout.preamble; offset < stream.size();
             offset += kPropsRecordSize) {
            out.emplace(get_u32(stream, offset),
                        std::vector<std::uint8_t>(
                            stream.begin() + static_cast<std::ptrdiff_t>(offset),
                            stream.begin() + static_cast<std::ptrdiff_t>(
                                                 offset + kPropsRecordSize)));
        }
        return out;
    };
    std::map<std::uint32_t, std::vector<std::uint8_t>> const before_records =
        records(before);
    std::map<std::uint32_t, std::vector<std::uint8_t>> const after_records =
        records(after);
    if (before_records.size() !=
            (before.size() - layout.preamble) / kPropsRecordSize ||
        after_records.size() != (after.size() - layout.preamble) / kPropsRecordSize) {
        throw Error(function + ": " + path +
                    " repeats a field id; refusing to treat it as a registry");
    }
    for (auto const& record : before_records) {
        auto const found = after_records.find(record.first);
        if (found == after_records.end() || found->second != record.second) {
            throw Error(function + ": " + path + " record for field " +
                        hex_id(record.first) + " changed; registration must not "
                        "rewrite an existing definition");
        }
    }
    for (auto const& record : expected) {
        std::uint32_t const key = get_u32(record, 0);
        auto const found = after_records.find(key);
        if (found == after_records.end() ||
            !std::equal(record.begin(), record.end(), found->second.begin())) {
            throw Error(function + ": " + path + " has no matching record for field " +
                        hex_id(key) + " after the write");
        }
    }
}

double f64_at(std::vector<std::uint8_t> const& b, std::size_t o) {
    std::uint64_t bits = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        bits |= static_cast<std::uint64_t>(b[o + i]) << (8 * i);
    }
    double value = 0.0;
    static_assert(sizeof value == sizeof bits, "IEEE-754 double expected");
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

std::vector<std::uint8_t> read_file_bytes(std::string const& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw Error("cannot open " + path + " after writing it");
    }
    input.seekg(0, std::ios::end);
    std::streamoff const length = input.tellg();
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    return bytes;
}

/* Same policy as save.cpp/add_delete.cpp: the parent directory must exist. */
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

/* One FixedMeta entry, decoded far enough for this file. */
struct MetaEntry {
    std::size_t index = 0;
    std::uint32_t offset = 0;
    std::uint8_t state = 0;
    std::uint16_t field_count = 0;
};

std::vector<MetaEntry> read_meta_entries(std::vector<std::uint8_t> const& meta) {
    std::vector<MetaEntry> out;
    if (meta.size() < kFixedMetaHeaderSize) {
        return out;
    }
    std::size_t const count =
        (meta.size() - kFixedMetaHeaderSize) / kFixedMetaEntrySize;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        std::size_t const base = kFixedMetaHeaderSize + i * kFixedMetaEntrySize;
        MetaEntry entry;
        entry.index = i;
        entry.offset = get_u32(meta, base + kFixedMetaRowOffset);
        entry.state = meta[base + kFixedMetaStateByte];
        entry.field_count = get_u16(meta, base + kFixedMetaFieldCount);
        out.push_back(entry);
    }
    return out;
}

std::int32_t entry_index_for_offset(std::vector<MetaEntry> const& entries,
                                    std::uint32_t offset) {
    for (MetaEntry const& entry : entries) {
        if (entry.offset == offset) {
            return static_cast<std::int32_t>(entry.index);
        }
    }
    return -1;
}

bool uid_has_var_group(std::vector<VarEntry> const& entries, std::uint32_t uid) {
    for (VarEntry const& entry : entries) {
        if (entry.mid == uid || entry.mid == (uid | kSecondaryUidFamily)) {
            return true;
        }
    }
    return false;
}

void validate_table(CfbDocument const& doc, std::string_view table,
                    std::string const& function) {
    for (char const* name : {"FixedData", "FixedMeta", "Fixed2Data", "Fixed2Meta",
                             "VarMeta", "Var2Data"}) {
        std::string const path = table_stream(table, name);
        if (!doc.has_stream(path)) {
            throw Error(function + ": the table has no " + path +
                        " stream; a custom-field write needs all six Task table "
                        "streams");
        }
    }
    std::string const problem = check_table(doc, table);
    if (!problem.empty()) {
        throw Error(function + ": the Task table's size identity is broken (" +
                    problem + "); refusing to write a field value");
    }
    std::vector<std::uint8_t> const meta =
        doc.read_stream(table_stream(table, "FixedMeta"));
    if ((meta.size() - kFixedMetaHeaderSize) % kFixedMetaEntrySize != 0) {
        throw Error(function + ": FixedMeta is " + std::to_string(meta.size()) +
                    " bytes, not 16 + 47*n; refusing to edit an entry");
    }
    std::vector<std::uint8_t> const var_meta =
        doc.read_stream(table_stream(table, "VarMeta"));
    std::vector<VarEntry> const var_entries = read_var_entries(var_meta);
    std::uint32_t const declared = get_u32(var_meta, 8);
    if (declared != var_entries.size()) {
        throw Error(function + ": VarMeta header declares " +
                    std::to_string(declared) + " entries but the stream holds " +
                    std::to_string(var_entries.size()) +
                    "; refusing to add or repoint entries in a damaged stream");
    }
    std::vector<std::uint8_t> const fixed2_meta =
        doc.read_stream(table_stream(table, "Fixed2Meta"));
    if (fixed2_meta.size() < kFixed2MetaHeaderSize ||
        (fixed2_meta.size() - kFixed2MetaHeaderSize) % kFixed2MetaEntrySize != 0) {
        throw Error(function + ": Fixed2Meta is " +
                    std::to_string(fixed2_meta.size()) +
                    " bytes, not 16 + 96*n; refusing to edit the Flag mirror");
    }
}

CfbDocument save_and_verify(CfbDocument& work, std::string const& path,
                            std::string const& function, std::string_view table) {
    std::vector<std::uint8_t> const intended = work.to_bytes();
    work.save(path);
    std::vector<std::uint8_t> const on_disk = read_file_bytes(path);
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
    std::string const table_problem = check_table(reread, table);
    if (!table_problem.empty()) {
        throw Error(function + ": the written table is inconsistent: " +
                    table_problem);
    }
    return reread;
}

/* Project stores a Cost value as the f64 of its HUNDREDTHS, exactly rounded
 * (measured: 123.45 -> 12345.0 in w14-cost; 0.07 -> 7.0 in w14-cost2, NOT the
 * 7.000000000000001 that a raw 0.07 * 100.0 produces).  The caller supplies the
 * currency value.  False when the value is not finite or not representable in
 * hundredths, so a caller can refuse rather than silently round. */
bool cost_stored_value(double cost, double& stored) {
    double const scaled = cost * 100.0;
    if (!std::isfinite(scaled) || std::fabs(scaled) > 9.0e15) {
        return false;
    }
    long long const rounded = std::llround(scaled);
    stored = static_cast<double>(rounded);
    return stored / 100.0 == cost;
}

/* The measured payload encoding for a value field (text/number/date/duration).
 * Flags have no payload; the row's FixedMeta bit is the store. */
std::vector<std::uint8_t> encode_payload(FieldValue const& value) {
    switch (value.kind) {
        case FieldKind::text:
            return encode_utf16z(value.text);
        case FieldKind::number: {
            std::vector<std::uint8_t> out(8, 0);
            std::uint64_t bits = 0;
            static_assert(sizeof bits == sizeof value.number,
                          "IEEE-754 double expected");
            std::memcpy(&bits, &value.number, sizeof bits);
            for (std::size_t i = 0; i < 8; ++i) {
                out[i] = static_cast<std::uint8_t>((bits >> (8 * i)) & 0xFFu);
            }
            return out;
        }
        case FieldKind::cost: {
            double stored = 0.0;
            if (!cost_stored_value(value.cost, stored)) {
                return {};
            }
            std::vector<std::uint8_t> out(8, 0);
            std::uint64_t bits = 0;
            static_assert(sizeof bits == sizeof stored, "IEEE-754 double expected");
            std::memcpy(&bits, &stored, sizeof bits);
            for (std::size_t i = 0; i < 8; ++i) {
                out[i] = static_cast<std::uint8_t>((bits >> (8 * i)) & 0xFFu);
            }
            return out;
        }
        case FieldKind::date: {
            std::vector<std::uint8_t> out(4, 0);
            put_u32(out, 0, value.packed_date);
            return out;
        }
        case FieldKind::duration: {
            std::vector<std::uint8_t> out(4, 0);
            put_u32(out, 0, value.duration_ticks);
            return out;
        }
        case FieldKind::flag:
        case FieldKind::unknown:
            break;
    }
    return {};
}

/* Decode a block with the measured encoding.  A wrong-sized block is reported
 * as present but not decodable (storage_measured = false), never guessed. */
FieldValue decode_payload(FieldKind kind, std::vector<std::uint8_t> const& payload) {
    FieldValue out;
    out.kind = kind;
    out.present = true;
    out.storage_measured = true;
    switch (kind) {
        case FieldKind::text:
            out.text = decode_utf16z(payload);
            break;
        case FieldKind::number:
            if (payload.size() != 8) {
                out.storage_measured = false;
            } else {
                out.number = f64_at(payload, 0);
            }
            break;
        case FieldKind::date:
            if (payload.size() != 4) {
                out.storage_measured = false;
            } else {
                out.packed_date = get_u32(payload, 0);
            }
            break;
        case FieldKind::duration:
            if (payload.size() != 4) {
                out.storage_measured = false;
            } else {
                out.duration_ticks = get_u32(payload, 0);
            }
            break;
        case FieldKind::cost:
            if (payload.size() != 8) {
                out.storage_measured = false;
            } else {
                out.cost = f64_at(payload, 0) / 100.0;
            }
            break;
        case FieldKind::flag:
        case FieldKind::unknown:
            out.storage_measured = false;
            break;
    }
    return out;
}

/* The value the streams hold for (row, uid, field id), read independently of
 * the edit code.  `present` is false when no block/bit is stored. */
FieldValue read_field(CfbDocument const& doc, std::string_view table,
                      std::uint32_t uid, std::size_t row,
                      std::uint32_t field_id) {
    FieldKind const kind = field_kind_for_id(field_id);
    FieldValue out;
    out.field_id = field_id;
    out.kind = kind;
    if (kind == FieldKind::unknown) {
        return out;
    }
    if (kind == FieldKind::flag) {
        std::vector<std::uint8_t> const meta =
            doc.read_stream(table_stream(table, "FixedMeta"));
        std::int32_t const index = entry_index_for_offset(
            read_meta_entries(meta), static_cast<std::uint32_t>(task_row_offset(row)));
        if (index < 0) {
            return out;
        }
        std::size_t const base =
            kFixedMetaHeaderSize + static_cast<std::size_t>(index) * kFixedMetaEntrySize;
        if (base + kFixedMetaFlag1Byte >= meta.size()) {
            return out;
        }
        out.present = true;
        out.storage_measured = field_storage_measured(kind, 1);
        out.flag = (meta[base + kFixedMetaFlag1Byte] & kFixedMetaFlag1Bit) != 0;
        return out;
    }
    std::vector<std::uint8_t> const var_meta =
        doc.read_stream(table_stream(table, "VarMeta"));
    std::vector<std::uint8_t> const var2 =
        doc.read_stream(table_stream(table, "Var2Data"));
    for (VarEntry const& entry : read_var_entries(var_meta)) {
        if (entry.field_id != field_id) {
            continue;
        }
        if (entry.mid != uid && entry.mid != (uid | kSecondaryUidFamily)) {
            continue;
        }
        if (entry.offset == 0xFFFFFFFFu ||
            static_cast<std::size_t>(entry.offset) + 4 > var2.size()) {
            continue;
        }
        FieldValue decoded = decode_payload(kind, read_var_block(var2, entry.offset));
        decoded.field_id = field_id;
        return decoded;
    }
    return out;
}

/* Same field values, compared exactly.  `present` is not compared: it is a
 * read-back fact, and the caller's request leaves it false. */
bool same_value(FieldValue const& a, FieldValue const& b) {
    if (a.field_id != b.field_id || a.kind != b.kind) {
        return false;
    }
    switch (a.kind) {
        case FieldKind::text:
            return a.text == b.text;
        case FieldKind::number:
            return a.number == b.number;
        case FieldKind::date:
            return a.packed_date == b.packed_date;
        case FieldKind::duration:
            return a.duration_ticks == b.duration_ticks;
        case FieldKind::flag:
            return a.flag == b.flag;
        case FieldKind::cost:
            return a.cost == b.cost;
        case FieldKind::unknown:
            return true;
    }
    return true;
}

/* Prove that the written stream differs from the input only where the edit
 * intended it.  `allowed` has one flag per byte of `before`; bytes appended
 * after `before.size()` are allowed by construction. */
void check_changed_bytes(std::vector<std::uint8_t> const& before,
                         std::vector<std::uint8_t> const& after,
                         std::vector<bool> const& allowed, std::string const& label,
                         std::string const& function) {
    if (after.size() < before.size()) {
        throw Error(function + ": " + label + " shrank (" +
                    std::to_string(before.size()) + " -> " +
                    std::to_string(after.size()) +
                    "); a field write is append-only");
    }
    for (std::size_t i = 0; i < before.size(); ++i) {
        if (after[i] != before[i] && !allowed[i]) {
            throw Error(function + ": " + label + " changed at byte " +
                        std::to_string(i) +
                        ", which this edit was not allowed to touch");
        }
    }
}

} /* namespace */

/* ---- field ids ---------------------------------------------------------- */

std::uint32_t field_id_for(FieldKind kind, unsigned index) {
    if (index == 0) {
        return 0;
    }
    switch (kind) {
        case FieldKind::text:
            if (index <= 6) {
                return 0x0B400033u + 3u * (index - 1u);
            }
            if (index <= 10) {
                return 0x0B400042u + (index - 6u);
            }
            if (index <= 30) {
                return 0x0B40013Du + (index - 11u);
            }
            return 0;
        case FieldKind::flag:
            if (index <= 10) {
                return 0x0B400048u + (index - 1u);
            }
            if (index <= 20) {
                return 0x0B400124u + (index - 11u);
            }
            return 0;
        case FieldKind::number:
            if (index <= 5) {
                return 0x0B400057u + (index - 1u);
            }
            if (index <= 20) {
                return 0x0B40012Eu + (index - 6u);
            }
            return 0;
        case FieldKind::duration:
            if (index <= 3) {
                return 0x0B400067u + (index - 1u);
            }
            if (index <= 10) {
                return 0x0B400113u + (index - 4u);
            }
            return 0;
        case FieldKind::cost:
            if (index <= 3) {
                return 0x0B40006Au + (index - 1u);
            }
            if (index <= 10) {
                return 0x0B400102u + (index - 4u);
            }
            return 0;
        case FieldKind::date:
            if (index <= 10) {
                return 0x0B400109u + (index - 1u);
            }
            return 0;
        case FieldKind::unknown:
            return 0;
    }
    return 0;
}

FieldKind field_kind_for_id(std::uint32_t field_id) {
    if (field_id == 0) {
        return FieldKind::unknown;
    }
    FieldKind const kinds[] = {FieldKind::text,   FieldKind::flag,
                               FieldKind::number, FieldKind::duration,
                               FieldKind::cost,   FieldKind::date};
    unsigned const counts[] = {30, 20, 20, 10, 10, 10};
    for (std::size_t k = 0; k < sizeof kinds / sizeof kinds[0]; ++k) {
        for (unsigned index = 1; index <= counts[k]; ++index) {
            if (field_id_for(kinds[k], index) == field_id) {
                return kinds[k];
            }
        }
    }
    return FieldKind::unknown;
}

unsigned field_index_for_id(std::uint32_t field_id) {
    FieldKind const kind = field_kind_for_id(field_id);
    if (kind == FieldKind::unknown) {
        return 0;
    }
    for (unsigned index = 1; index <= 30; ++index) {
        if (field_id_for(kind, index) == field_id) {
            return index;
        }
    }
    return 0;
}

bool field_storage_measured(FieldKind kind, unsigned index) {
    switch (kind) {
        case FieldKind::text:
        case FieldKind::number:
        case FieldKind::duration:
        case FieldKind::date:
        case FieldKind::cost:
            return index != 0;
        case FieldKind::flag:
            return index == 1;
        case FieldKind::unknown:
            return false;
    }
    return false;
}

/* ---- the write ---------------------------------------------------------- */

FieldWriteResult save_field_values(CfbDocument& doc,
                                   std::vector<FieldEdit> const& edits,
                                   std::string const& path, std::string_view table) {
    std::string const function = "save_field_values";
    if (edits.empty()) {
        throw Error(function + ": the edit list is empty; there is nothing to save");
    }
    check_output_path(path, function);
    validate_table(doc, table, function);

    std::string const fixed_path = table_stream(table, "FixedData");
    std::string const meta_path = table_stream(table, "FixedMeta");
    std::string const fixed2_path = table_stream(table, "Fixed2Data");
    std::string const fixed2_meta_path = table_stream(table, "Fixed2Meta");
    std::string const var_meta_path = table_stream(table, "VarMeta");
    std::string const var2_path = table_stream(table, "Var2Data");
    std::string const props_path = table_stream(table, "Props");
    std::string const assn_props_path = "114/TBkndAssn/Props";

    /* ---- The custom-field registries.  Project shows a custom field only when
     * its definition is in the Task table's Props (and mirrors it in the
     * Assignment table's Props).  See the PropsLayout comment. */
    if (!doc.has_stream(props_path)) {
        throw Error(function + ": the Task table has no " + props_path +
                    " stream; Project's custom-field registration lives there and "
                    "the writer cannot create a stream (spec/mpp-writer.md §5.1)");
    }
    std::vector<std::uint8_t> props = doc.read_stream(props_path);
    validate_props_stream(props, kTaskPropsLayout, props_path, function);
    std::vector<std::uint8_t> const props_before = props;

    /* The Assignment-side mirror is registered too when the stream exists
     * (Project's own file has it).  A file without it still gets the Task
     * registration and a warning; the writer cannot create a stream. */
    bool const has_assn_props = doc.has_stream(assn_props_path);
    std::vector<std::uint8_t> assn_props;
    std::vector<std::uint8_t> assn_props_before;
    if (has_assn_props) {
        assn_props = doc.read_stream(assn_props_path);
        validate_props_stream(assn_props, kAssnPropsLayout, assn_props_path, function);
        assn_props_before = assn_props;
    }

    TaskTable const before_table = TaskTable::from_document(doc, table);
    std::size_t const rows = before_table.rows();
    std::vector<std::uint8_t> const fixed_before = doc.read_stream(fixed_path);
    std::vector<std::uint8_t> const fixed2_before = doc.read_stream(fixed2_path);
    std::vector<std::uint8_t> meta = doc.read_stream(meta_path);
    std::vector<std::uint8_t> fixed2_meta = doc.read_stream(fixed2_meta_path);
    std::vector<std::uint8_t> var_meta = doc.read_stream(var_meta_path);
    std::vector<std::uint8_t> var2 = doc.read_stream(var2_path);
    std::vector<MetaEntry> const meta_entries = read_meta_entries(meta);
    std::vector<VarEntry> const var_entries = read_var_entries(var_meta);

    /* ---- Validate every edit before touching anything. */
    struct Prepared {
        FieldEdit edit;
        FieldKind kind = FieldKind::unknown;
        unsigned index = 0;
        std::size_t row = 0;
        std::size_t entry_index = 0;
        std::vector<std::uint8_t> payload; /* empty for a flag */
    };
    std::vector<Prepared> prepared;
    prepared.reserve(edits.size());
    std::vector<std::pair<std::uint32_t, std::uint32_t>> seen; /* (uid, field) */
    for (FieldEdit const& edit : edits) {
        Prepared item;
        item.edit = edit;
        std::uint32_t const field_id = edit.value.field_id;
        item.kind = field_kind_for_id(field_id);
        item.index = field_index_for_id(field_id);
        std::string const label =
            "uid " + std::to_string(edit.uid) + " field " + hex_id(field_id);
        if (item.kind == FieldKind::unknown) {
            throw Error(function + ": " + label +
                        " is not a task custom field in the measured id ranges "
                        "(see field_value.h / analysis/field_id_map.tsv)");
        }
        if (edit.value.kind != item.kind) {
            throw Error(function + ": " + label + " is a " + kind_name(item.kind) +
                        " field but the value says kind " +
                        std::to_string(static_cast<int>(edit.value.kind)) +
                        "; the kind must match the id");
        }
        if (!field_storage_measured(item.kind, item.index)) {
            throw Error(function + ": " + label +
                        " is not stored where it is measured (only Flag1's bit "
                        "position is known; Flag2..Flag20 are unmeasured)");
        }
        std::array<std::uint8_t, kPropsRecordSize> props_template{};
        if (!props_template_for(field_id, props_template)) {
            throw Error(function + ": " + label +
                        " has no measured registration record in " + props_path +
                        " (only Text1, Flag1, Number1, Number2, Duration1, Date1 "
                        "and Cost1..Cost10 are measured); Project ignores a custom "
                        "field "
                        "that is not registered there, and the record encodes the "
                        "field's format, so it cannot be guessed");
        }
        if (edit.uid == 0) {
            throw Error(function + ": " + label +
                        " is the project row (uid 0); project custom fields have "
                        "no measured storage in the Task table");
        }
        std::size_t row = rows;
        for (std::size_t candidate = 0; candidate < rows; ++candidate) {
            if (before_table.uid(candidate) == edit.uid) {
                row = candidate;
                break;
            }
        }
        if (row >= rows || row == 0) {
            throw Error(function + ": " + label +
                        ": no task row has that UniqueID");
        }
        std::int32_t const entry_index = entry_index_for_offset(
            meta_entries, static_cast<std::uint32_t>(task_row_offset(row)));
        if (entry_index < 0) {
            throw Error(function + ": " + label +
                        ": the row has no FixedMeta entry, so Project treats it as "
                        "deleted");
        }
        MetaEntry const& entry = meta_entries[static_cast<std::size_t>(entry_index)];
        if (entry.state != kStateLive) {
            throw Error(function + ": " + label +
                        ": the row's FixedMeta entry is a tombstone (deleted row)");
        }
        if (!uid_has_var_group(var_entries, edit.uid)) {
            throw Error(function + ": " + label +
                        ": the row has no VarMeta group, so Project does not read "
                        "it as a live task");
        }
        for (auto const& key : seen) {
            if (key.first == edit.uid && key.second == field_id) {
                throw Error(function + ": " + label +
                            " appears twice; one value per field");
            }
        }
        seen.emplace_back(edit.uid, field_id);

        switch (item.kind) {
            case FieldKind::text:
                if (edit.value.text.find('\0') != std::string::npos) {
                    throw Error(function + ": " + label +
                                ": the text contains an embedded NUL, which the "
                                "UTF-16+NUL block cannot represent");
                }
                if (edit.value.text.size() > kMaxTextBytes) {
                    throw Error(function + ": " + label + ": the text is " +
                                std::to_string(edit.value.text.size()) +
                                " bytes, longer than the " +
                                std::to_string(kMaxTextBytes) +
                                "-byte limit for one value");
                }
                break;
            case FieldKind::number:
                if (!std::isfinite(edit.value.number)) {
                    throw Error(function + ": " + label +
                                ": the number is not finite");
                }
                break;
            case FieldKind::date:
                if (!is_date(edit.value.packed_date)) {
                    throw Error(function + ": " + label +
                                ": the date word " +
                                hex_id(edit.value.packed_date) +
                                " is not a packed date (0 and 0xffffffff mean "
                                "\"no date\")");
                }
                break;
            case FieldKind::duration:
                if (edit.value.duration_ticks == kInvalidDate) {
                    throw Error(function + ": " + label +
                                ": duration 0xffffffff is the invalid sentinel, not "
                                "a duration");
                }
                break;
            case FieldKind::flag:
                /* The value is the bool itself; nothing else to check. */
                break;
            case FieldKind::cost: {
                double stored = 0.0;
                if (!cost_stored_value(edit.value.cost, stored)) {
                    throw Error(function + ": " + label + ": cost " +
                                std::to_string(edit.value.cost) +
                                " is not finite or not representable in hundredths "
                                "(Project stores cost * 100 as an f64, exactly "
                                "rounded: 123.45 -> 12345, 0.07 -> 7)");
                }
                break;
            }
            case FieldKind::unknown:
                break; /* already refused above */
        }
        item.row = row;
        item.entry_index = static_cast<std::size_t>(entry_index);
        if (item.kind != FieldKind::flag) {
            item.payload = encode_payload(item.edit.value);
        }
        prepared.push_back(std::move(item));
    }

    /* ---- Register every written field once, in TBkndTask/Props and (when the
     * stream exists) its TBkndAssn/Props mirror.  A field already registered is
     * left alone: a rewrite of a registered field must not duplicate it. */
    std::vector<std::uint32_t> fields_to_register;
    for (Prepared const& item : prepared) {
        bool known = false;
        for (std::uint32_t const id : fields_to_register) {
            known = known || id == item.edit.value.field_id;
        }
        if (!known) {
            fields_to_register.push_back(item.edit.value.field_id);
        }
    }
    std::vector<std::array<std::uint8_t, kPropsRecordSize>> task_records;
    std::vector<std::array<std::uint8_t, kPropsRecordSize>> assn_records;
    for (std::uint32_t const field_id : fields_to_register) {
        std::array<std::uint8_t, kPropsRecordSize> record{};
        (void)props_template_for(field_id, record); /* validated above */
        task_records.push_back(record);
        if (has_assn_props) {
            std::array<std::uint8_t, kPropsRecordSize> assn_record{};
            if (!assn_props_template_for(field_id, assn_record)) {
                throw Error(function + ": internal error: field " + hex_id(field_id) +
                            " has a Task Props template but no Assignment mirror");
            }
            assn_records.push_back(assn_record);
        }
    }
    std::size_t const task_registered =
        register_props_records(props, kTaskPropsLayout, task_records);
    std::size_t const assn_registered =
        has_assn_props
            ? register_props_records(assn_props, kAssnPropsLayout, assn_records)
            : 0;
    if (task_registered > 0) {
        write_props_words(props, kTaskPropsLayout, props_path, function);
    }
    if (assn_registered > 0) {
        write_props_words(assn_props, kAssnPropsLayout, assn_props_path, function);
    }

    /* ---- Apply.  `allowed_*` records exactly which bytes may move. */
    std::vector<bool> allowed_meta(meta.size(), false);
    std::vector<bool> allowed_fixed2_meta(fixed2_meta.size(), false);
    std::vector<bool> const allowed_var2(var2.size(), false);
    std::vector<std::string> warnings;
    struct WrittenField {
        std::uint32_t uid = 0;
        std::uint32_t field_id = 0;
        std::uint32_t offset = 0;
        bool added_entry = false;
    };
    std::vector<WrittenField> written;
    written.reserve(prepared.size());
    std::size_t added_entries = 0;
    bool wrote_mirror = false;
    std::map<std::uint32_t, std::size_t> touched_row_entry; /* uid -> meta entry */

    for (Prepared const& item : prepared) {
        std::size_t const meta_base =
            kFixedMetaHeaderSize + item.entry_index * kFixedMetaEntrySize;
        touched_row_entry[item.edit.uid] = item.entry_index;
        /* The FixedMeta tail presence bits: without them Project shows the value
         * as empty even with perfect VarMeta/Props (see tail_bits_for).  For a
         * flag the bit is the value itself, so a False clears it; a value
         * field's bits are only ever set. */
        for (TailBit const& bit : tail_bits_for(item.edit.value.field_id)) {
            allowed_meta[meta_base + bit.offset] = true;
            bool const set =
                item.kind != FieldKind::flag || item.edit.value.flag;
            if (set) {
                meta[meta_base + bit.offset] = static_cast<std::uint8_t>(
                    meta[meta_base + bit.offset] | bit.mask);
            } else {
                meta[meta_base + bit.offset] = static_cast<std::uint8_t>(
                    meta[meta_base + bit.offset] & static_cast<std::uint8_t>(~bit.mask));
            }
        }
        if (item.kind == FieldKind::flag) {
            std::size_t const fixed2_base =
                kFixed2MetaHeaderSize + item.entry_index * kFixed2MetaEntrySize;
            /* Fixed2Meta pairs with FixedMeta by entry index (core/mpp's
             * reader), so the same entry index is the mirror. */
            if (fixed2_base + kFixed2MetaEntrySize <= fixed2_meta.size()) {
                for (std::size_t const offset :
                     {fixed2_base + kFixed2MetaFlag1ByteA,
                      fixed2_base + kFixed2MetaFlag1ByteB}) {
                    allowed_fixed2_meta[offset] = true;
                    if (item.edit.value.flag) {
                        fixed2_meta[offset] = static_cast<std::uint8_t>(
                            fixed2_meta[offset] | kFixed2MetaFlag1Bit);
                    } else {
                        fixed2_meta[offset] = static_cast<std::uint8_t>(
                            fixed2_meta[offset] & ~kFixed2MetaFlag1Bit);
                    }
                }
                wrote_mirror = true;
            } else {
                warnings.push_back("uid " + std::to_string(item.edit.uid) +
                                   " field " + hex_id(item.edit.value.field_id) +
                                   ": the row has no Fixed2Meta entry, so Project's "
                                   "Flag1 mirror (+31/+61 bit 0x80) was not written; "
                                   "the Task FixedMeta bit is written and verified");
            }
            continue;
        }

        /* A value block is always appended to Var2Data. */
        std::uint32_t const offset = static_cast<std::uint32_t>(var2.size());
        std::vector<std::uint8_t> const block = encode_block(item.payload);
        var2.insert(var2.end(), block.begin(), block.end());
        /* Repoint every existing (uid family, key) entry at the new block. */
        std::vector<VarEntry> const current = read_var_entries(var_meta);
        bool repointed = false;
        for (VarEntry const& entry : current) {
            if (entry.field_id != item.edit.value.field_id) {
                continue;
            }
            if (entry.mid != item.edit.uid &&
                entry.mid != (item.edit.uid | kSecondaryUidFamily)) {
                continue;
            }
            std::size_t const base =
                kVarMetaHeaderSize + entry.index * kVarMetaEntrySize;
            put_u32(var_meta, base + 4, offset);
            repointed = true;
        }
        if (!repointed) {
            /* A NEW entry must sit inside its uid's contiguous group, in key
             * order -- NOT at the end of the array.  MEASURED (W48 Project
             * gate): Project reopens a file whose uid entries are contiguous
             * ([0]*9, [1]*14, [2]*8 in w10-fieldvalues.mpp) with the values, and
             * ignores entries appended after other uids' groups. */
            insert_var_entry(var_meta, item.edit.uid, item.edit.value.field_id, offset);
            ++added_entries;
        }
        /* A Duration custom field also carries Project's internal 2-byte marker
         * 0x0b4000b7 (payload 07 00), inserted in ascending-key order (after the
         * overtime entries); it is added once per row. */
        if (item.edit.value.field_id == 0x0B400067u) {
            bool has_helper = false;
            for (VarEntry const& entry : read_var_entries(var_meta)) {
                if (entry.mid == item.edit.uid &&
                    entry.field_id == kDurationHelperFieldId) {
                    has_helper = true;
                    break;
                }
            }
            if (!has_helper) {
                std::uint32_t const helper_offset = static_cast<std::uint32_t>(var2.size());
                std::vector<std::uint8_t> const helper_block = encode_block({0x07, 0x00});
                var2.insert(var2.end(), helper_block.begin(), helper_block.end());
                insert_var_entry(var_meta, item.edit.uid, kDurationHelperFieldId,
                                 helper_offset);
                ++added_entries;
                WrittenField helper;
                helper.uid = item.edit.uid;
                helper.field_id = kDurationHelperFieldId;
                helper.offset = helper_offset;
                helper.added_entry = true;
                written.push_back(helper);
            }
        }
        WrittenField record;
        record.uid = item.edit.uid;
        record.field_id = item.edit.value.field_id;
        record.offset = offset;
        record.added_entry = !repointed;
        written.push_back(record);
    }
    /* FixedMeta +2 is the row's exact VarMeta entry count, including the
     * duration marker (measured: base 11 -> 17 with the six fields).  Recompute
     * it from the array instead of incrementing, so a rewrite and the helper
     * entry both come out right. */
    for (auto const& touched : touched_row_entry) {
        std::uint32_t const uid = touched.first;
        std::size_t count = 0;
        for (VarEntry const& entry : read_var_entries(var_meta)) {
            if (entry.mid == uid || entry.mid == (uid | kSecondaryUidFamily)) {
                ++count;
            }
        }
        if (count > 0xFFFFu) {
            throw Error(function + ": uid " + std::to_string(uid) + " has " +
                        std::to_string(count) +
                        " VarMeta entries, too many for FixedMeta +2; refusing");
        }
        std::size_t const base =
            kFixedMetaHeaderSize + touched.second * kFixedMetaEntrySize;
        put_u16(meta, base + kFixedMetaFieldCount,
                static_cast<std::uint16_t>(count));
        allowed_meta[base + kFixedMetaFieldCount] = true;
        allowed_meta[base + kFixedMetaFieldCount + 1] = true;
    }
    put_u32(var_meta, 8,
            static_cast<std::uint32_t>(read_var_entries(var_meta).size()));
    put_u32(var_meta, kVarMetaVar2SizeOffset,
            static_cast<std::uint32_t>(var2.size()));

    CfbDocument work = CfbDocument::from_bytes(doc.to_bytes());
    work.replace_stream(meta_path, meta);
    work.replace_stream(var_meta_path, var_meta);
    work.replace_stream(var2_path, var2);
    if (wrote_mirror) {
        work.replace_stream(fixed2_meta_path, fixed2_meta);
    }
    if (task_registered > 0) {
        work.replace_stream(props_path, props);
    }
    if (assn_registered > 0) {
        work.replace_stream(assn_props_path, assn_props);
    }

    CfbDocument const reread = save_and_verify(work, path, function, table);
    TaskTable const after_table = TaskTable::from_document(reread, table);
    if (after_table.rows() != rows) {
        throw Error(function + ": the written file has " +
                    std::to_string(after_table.rows()) + " rows, expected " +
                    std::to_string(rows) +
                    "; a field write must not add or remove rows");
    }

    /* ---- Prove the intended bytes are the only ones that moved. */
    std::vector<std::uint8_t> const fixed_after = reread.read_stream(fixed_path);
    std::vector<std::uint8_t> const fixed2_after = reread.read_stream(fixed2_path);
    if (fixed_after != fixed_before || fixed2_after != fixed2_before) {
        throw Error(function + ": FixedData/Fixed2Data changed; a custom-field "
                    "write must not rewrite the fixed rows");
    }
    check_changed_bytes(doc.read_stream(meta_path), reread.read_stream(meta_path),
                        allowed_meta, "FixedMeta", function);
    check_changed_bytes(doc.read_stream(var2_path), reread.read_stream(var2_path),
                        allowed_var2, "Var2Data", function);
    check_changed_bytes(doc.read_stream(fixed2_meta_path),
                        reread.read_stream(fixed2_meta_path), allowed_fixed2_meta,
                        "Fixed2Meta", function);

    /*
     * VarMeta is proved by ENTRY: inserting a value inside a uid group shifts
     * every later entry by 12 bytes, so a byte allow-list would be the wrong
     * shape.  The entry multiset must be (all original entries, unchanged
     * offsets, except the edited fields which point at their new block) + (the
     * new entries), in Project's measured order: contiguous by uid, ascending
     * field id inside a uid.
     */
    std::vector<std::uint8_t> const var_meta_before = doc.read_stream(var_meta_path);
    std::vector<std::uint8_t> const var_meta_after = reread.read_stream(var_meta_path);
    std::vector<std::uint8_t> const var2_after = reread.read_stream(var2_path);
    std::vector<VarEntry> const entries_after = read_var_entries(var_meta_after);
    if (entries_after.size() != var_entries.size() + added_entries) {
        throw Error(function + ": VarMeta holds " +
                    std::to_string(entries_after.size()) + " entries, expected " +
                    std::to_string(var_entries.size() + added_entries));
    }
    for (std::size_t i = 0; i < kVarMetaHeaderSize; ++i) {
        bool const may_move =
            (i >= 8 && i < 12) ||
            (i >= kVarMetaVar2SizeOffset && i < kVarMetaVar2SizeOffset + 4);
        if (!may_move && var_meta_after[i] != var_meta_before[i]) {
            throw Error(function + ": VarMeta header byte +" + std::to_string(i) +
                        " changed; only the entry count and the Var2Data size may "
                        "be updated");
        }
    }
    if (get_u32(var_meta_after, 8) != entries_after.size()) {
        throw Error(function + ": VarMeta +8 reads " +
                    std::to_string(get_u32(var_meta_after, 8)) + ", expected " +
                    std::to_string(entries_after.size()));
    }
    if (get_u32(var_meta_after, kVarMetaVar2SizeOffset) != var2_after.size()) {
        throw Error(function + ": VarMeta +0x14 reads " +
                    std::to_string(get_u32(var_meta_after, kVarMetaVar2SizeOffset)) +
                    ", expected the Var2Data size " +
                    std::to_string(var2_after.size()));
    }
    if (var2_after.size() != var2.size()) {
        throw Error(function + ": Var2Data is " + std::to_string(var2_after.size()) +
                    " bytes, expected " + std::to_string(var2.size()));
    }

    auto written_index = [&written](VarEntry const& entry) {
        for (std::size_t k = 0; k < written.size(); ++k) {
            if (written[k].field_id == entry.field_id &&
                (entry.mid == written[k].uid ||
                 entry.mid == (written[k].uid | kSecondaryUidFamily))) {
                return static_cast<std::int32_t>(k);
            }
        }
        return static_cast<std::int32_t>(-1);
    };
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::vector<std::uint32_t>>
        before_untouched;
    for (VarEntry const& entry : var_entries) {
        if (written_index(entry) < 0) {
            before_untouched[{entry.mid, entry.field_id}].push_back(entry.offset);
        }
    }
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::vector<std::uint32_t>>
        after_untouched;
    std::vector<bool> edit_seen(written.size(), false);
    for (VarEntry const& entry : entries_after) {
        std::int32_t const which = written_index(entry);
        if (which < 0) {
            after_untouched[{entry.mid, entry.field_id}].push_back(entry.offset);
            continue;
        }
        std::size_t const k = static_cast<std::size_t>(which);
        if (entry.offset != written[k].offset) {
            throw Error(function + ": uid " + std::to_string(written[k].uid) +
                        " field " + hex_id(written[k].field_id) +
                        " still points at Var2Data offset " +
                        std::to_string(entry.offset) + ", not the written block " +
                        std::to_string(written[k].offset));
        }
        edit_seen[k] = true;
    }
    for (std::size_t k = 0; k < written.size(); ++k) {
        if (!edit_seen[k]) {
            throw Error(function + ": uid " + std::to_string(written[k].uid) +
                        " field " + hex_id(written[k].field_id) +
                        " has no VarMeta entry after the write");
        }
    }
    if (before_untouched != after_untouched) {
        throw Error(function + ": an untouched VarMeta entry changed; a field write "
                    "may only repoint the edited entries and insert the new ones");
    }
    auto entry_key = [](VarEntry const& entry) {
        return std::tuple<std::uint32_t, int, std::uint32_t>(
            entry.mid & 0x7FFFFFFFu, (entry.mid & 0x80000000u) != 0 ? 1 : 0,
            entry.field_id);
    };
    for (std::size_t i = 1; i < entries_after.size(); ++i) {
        if (!(entry_key(entries_after[i - 1]) <= entry_key(entries_after[i]))) {
            throw Error(function + ": VarMeta is not grouped by uid and sorted by "
                        "field id after the write (entries " +
                        std::to_string(i - 1) + " and " + std::to_string(i) +
                        "); Project ignores values outside a uid's contiguous group");
        }
    }

    /* ---- FixedMeta tail presence bits and the exact VarMeta entry count. */
    std::vector<std::uint8_t> const meta_after = reread.read_stream(meta_path);
    for (Prepared const& item : prepared) {
        std::size_t const base =
            kFixedMetaHeaderSize + item.entry_index * kFixedMetaEntrySize;
        for (TailBit const& bit : tail_bits_for(item.edit.value.field_id)) {
            bool const expected =
                item.kind != FieldKind::flag || item.edit.value.flag;
            bool const actual =
                (meta_after[base + bit.offset] & bit.mask) != 0;
            if (actual != expected) {
                throw Error(function + ": uid " + std::to_string(item.edit.uid) +
                            " field " + hex_id(item.edit.value.field_id) +
                            " did not read back its FixedMeta tail bit at +" +
                            std::to_string(bit.offset));
            }
        }
    }
    for (auto const& touched : touched_row_entry) {
        std::uint32_t const uid = touched.first;
        std::size_t count = 0;
        for (VarEntry const& entry : entries_after) {
            if (entry.mid == uid || entry.mid == (uid | kSecondaryUidFamily)) {
                ++count;
            }
        }
        std::size_t const base =
            kFixedMetaHeaderSize + touched.second * kFixedMetaEntrySize;
        if (get_u16(meta_after, base + kFixedMetaFieldCount) != count) {
            throw Error(function + ": uid " + std::to_string(uid) +
                        " FixedMeta +2 reads " +
                        std::to_string(get_u16(meta_after, base + kFixedMetaFieldCount)) +
                        ", expected its " + std::to_string(count) +
                        " VarMeta entries");
        }
    }

    /* ---- The custom-field registries, proved from the file. */
    verify_props_stream(reread, props_path, kTaskPropsLayout, props_before,
                        task_records, task_registered, function);
    if (has_assn_props) {
        verify_props_stream(reread, assn_props_path, kAssnPropsLayout,
                            assn_props_before, assn_records, assn_registered,
                            function);
    }

    /* ---- Re-read every edit through the measured encoding. */
    FieldWriteResult result;
    result.fields_written = prepared.size();
    for (Prepared const& item : prepared) {
        FieldValue const stored = read_field(reread, table, item.edit.uid, item.row,
                                             item.edit.value.field_id);
        if (!stored.present || !stored.storage_measured ||
            !same_value(stored, item.edit.value)) {
            throw Error(function + ": uid " + std::to_string(item.edit.uid) +
                        " field " + hex_id(item.edit.value.field_id) +
                        " does not read back as written (present " +
                        (stored.present ? "true" : "false") + ", storage_measured " +
                        (stored.storage_measured ? "true" : "false") + ", text '" +
                        stored.text + "', number " + std::to_string(stored.number) +
                        ", date " + hex_id(stored.packed_date) + ", duration " +
                        std::to_string(stored.duration_ticks) + ", flag " +
                        (stored.flag ? "true" : "false") + ")");
        }
        FieldWrite write;
        write.uid = item.edit.uid;
        write.value = stored;
        result.fields.push_back(std::move(write));
    }
    result.warnings = std::move(warnings);
    result.warnings.push_back("re-read from disk: " +
                              std::to_string(prepared.size()) +
                              " custom-field value(s) match what was written");
    if (task_registered > 0) {
        result.warnings.push_back(
            "registered " + std::to_string(task_registered) +
            " field definition(s) in " + props_path +
            " so Project displays the values (one 88-byte record per field, sorted "
            "by field id)");
    }
    if (assn_registered > 0) {
        result.warnings.push_back(
            "registered " + std::to_string(assn_registered) +
            " field definition(s) in " + assn_props_path +
            " (Project's assignment-side mirror)");
    }
    if (!has_assn_props) {
        result.warnings.push_back(
            "the file has no " + assn_props_path +
            " stream; the Task-side registration was written, the assignment mirror "
            "was not (the writer cannot create a stream)");
    }
    doc = reread;
    return result;
}

/* ---- W52: clear one custom-field value --------------------------------- */

namespace {

FieldValue empty_value_for(std::uint32_t field_id, FieldKind kind) {
    FieldValue empty;
    empty.field_id = field_id;
    empty.kind = kind;
    switch (kind) {
        case FieldKind::text:
            empty.text.clear(); /* UTF-16LE NUL only, 2 bytes */
            break;
        case FieldKind::number:
            empty.number = 0.0;
            break;
        case FieldKind::duration:
            empty.duration_ticks = 0;
            break;
        case FieldKind::date:
            empty.packed_date = kInvalidDate; /* 0xffffffff = Project's no-date */
            break;
        case FieldKind::cost:
            empty.cost = 0.0; /* f64 hundredths 0.0 */
            break;
        case FieldKind::flag:
            empty.flag = false;
            break;
        case FieldKind::unknown:
            break;
    }
    return empty;
}

bool tail_bits_are_clear(std::vector<std::uint8_t> const& meta,
                         std::size_t entry_base, std::uint32_t field_id) {
    for (TailBit const& bit : tail_bits_for(field_id)) {
        if ((meta[entry_base + bit.offset] & bit.mask) != 0) {
            return false;
        }
    }
    return true;
}

} /* namespace */

ClearedField clear_field_value(CfbDocument& doc, std::uint32_t uid,
                               std::uint32_t field_id, std::string const& path,
                               std::string_view table) {
    std::string const function = "clear_field_value";
    check_output_path(path, function);
    FieldKind const kind = field_kind_for_id(field_id);
    unsigned const index = field_index_for_id(field_id);
    std::string const label =
        "uid " + std::to_string(uid) + " field " + hex_id(field_id);
    if (kind == FieldKind::unknown) {
        throw Error(function + ": " + label +
                    " is not a task custom field in the measured id ranges");
    }
    if (!field_storage_measured(kind, index)) {
        throw Error(function + ": " + label +
                    " is not stored where it is measured (only Flag1's bit "
                    "position is known; Flag2..Flag20 are unmeasured)");
    }
    if (tail_bits_for(field_id).empty()) {
        throw Error(function + ": " + label +
                    " has no measured FixedMeta tail-presence bit, so a cleared "
                    "value cannot be made blank; only Text1, Flag1, Number1, "
                    "Number2, Duration1, Date1 and Cost1..Cost10 are measured");
    }
    if (uid == 0) {
        throw Error(function + ": " + label +
                    " is the project row (uid 0); project custom fields have no "
                    "measured storage in the Task table");
    }
    validate_table(doc, table, function);

    std::string const fixed_path = table_stream(table, "FixedData");
    std::string const meta_path = table_stream(table, "FixedMeta");
    std::string const fixed2_path = table_stream(table, "Fixed2Data");
    std::string const fixed2_meta_path = table_stream(table, "Fixed2Meta");
    std::string const var_meta_path = table_stream(table, "VarMeta");
    std::string const var2_path = table_stream(table, "Var2Data");
    std::string const props_path = table_stream(table, "Props");
    std::string const assn_props_path = "114/TBkndAssn/Props";

    TaskTable const before_table = TaskTable::from_document(doc, table);
    std::size_t const rows = before_table.rows();
    std::size_t row = rows;
    for (std::size_t candidate = 0; candidate < rows; ++candidate) {
        if (before_table.uid(candidate) == uid) {
            row = candidate;
            break;
        }
    }
    if (row >= rows || row == 0) {
        throw Error(function + ": " + label + ": no task row has that UniqueID");
    }
    std::vector<std::uint8_t> const fixed_before = doc.read_stream(fixed_path);
    std::vector<std::uint8_t> const fixed2_before = doc.read_stream(fixed2_path);
    std::vector<std::uint8_t> meta = doc.read_stream(meta_path);
    std::vector<std::uint8_t> fixed2_meta = doc.read_stream(fixed2_meta_path);
    std::vector<std::uint8_t> var_meta = doc.read_stream(var_meta_path);
    std::vector<std::uint8_t> var2 = doc.read_stream(var2_path);
    std::vector<std::uint8_t> const var_meta_before = var_meta;
    std::vector<std::uint8_t> const var2_before = var2;
    std::vector<std::uint8_t> const meta_before = meta;

    std::vector<MetaEntry> const meta_entries = read_meta_entries(meta);
    std::int32_t const entry_index = entry_index_for_offset(
        meta_entries, static_cast<std::uint32_t>(task_row_offset(row)));
    if (entry_index < 0) {
        throw Error(function + ": " + label +
                    ": the row has no FixedMeta entry, so Project treats it as "
                    "deleted");
    }
    MetaEntry const& entry = meta_entries[static_cast<std::size_t>(entry_index)];
    if (entry.state != kStateLive) {
        throw Error(function + ": " + label +
                    ": the row's FixedMeta entry is a tombstone (deleted row)");
    }
    std::vector<VarEntry> const var_entries = read_var_entries(var_meta);
    if (!uid_has_var_group(var_entries, uid)) {
        throw Error(function + ": " + label +
                    ": the row has no VarMeta group, so Project does not read it "
                    "as a live task");
    }

    FieldValue const empty = empty_value_for(field_id, kind);
    FieldValue const stored = read_field(doc, table, uid, row, field_id);
    std::size_t const meta_base =
        kFixedMetaHeaderSize + static_cast<std::size_t>(entry_index) * kFixedMetaEntrySize;
    std::vector<bool> allowed_meta(meta.size(), false);
    std::vector<bool> allowed_fixed2_meta(fixed2_meta.size(), false);
    std::vector<bool> const allowed_var2(var2.size(), false);
    std::vector<bool> allowed_var_meta(var_meta.size(), false);
    std::vector<std::string> warnings;
    bool changed = false;
    bool entry_kept = false;
    bool was_present = false;

    if (kind == FieldKind::flag) {
        was_present = (meta[meta_base + kFixedMetaFlag1Byte] & kFixedMetaFlag1Bit) != 0;
        std::size_t const flag_byte = meta_base + kFixedMetaFlag1Byte;
        allowed_meta[flag_byte] = true;
        if ((meta[flag_byte] & kFixedMetaFlag1Bit) != 0) {
            meta[flag_byte] = static_cast<std::uint8_t>(meta[flag_byte] &
                                                        ~kFixedMetaFlag1Bit);
            changed = true;
        }
        std::size_t const fixed2_base =
            kFixed2MetaHeaderSize + static_cast<std::size_t>(entry_index) *
                                        kFixed2MetaEntrySize;
        if (fixed2_base + kFixed2MetaEntrySize <= fixed2_meta.size()) {
            for (std::size_t const offset :
                 {fixed2_base + kFixed2MetaFlag1ByteA,
                  fixed2_base + kFixed2MetaFlag1ByteB}) {
                allowed_fixed2_meta[offset] = true;
                if ((fixed2_meta[offset] & kFixed2MetaFlag1Bit) != 0) {
                    fixed2_meta[offset] = static_cast<std::uint8_t>(
                        fixed2_meta[offset] & ~kFixed2MetaFlag1Bit);
                    changed = true;
                }
            }
        } else {
            warnings.push_back("uid " + std::to_string(uid) + " field " +
                               hex_id(field_id) +
                               ": the row has no Fixed2Meta entry, so Project's "
                               "Flag1 mirror (+31/+61 bit 0x80) was not cleared; "
                               "the Task FixedMeta bit is cleared and verified");
        }
    } else {
        bool const stored_empty =
            stored.present && stored.storage_measured && same_value(stored, empty);
        was_present = !tail_bits_are_clear(meta, meta_base, field_id) ||
                      (stored.present && !stored_empty);
        for (TailBit const& bit : tail_bits_for(field_id)) {
            allowed_meta[meta_base + bit.offset] = true;
            if ((meta[meta_base + bit.offset] & bit.mask) != 0) {
                meta[meta_base + bit.offset] = static_cast<std::uint8_t>(
                    meta[meta_base + bit.offset] &
                    static_cast<std::uint8_t>(~bit.mask));
                changed = true;
            }
        }
        std::vector<std::size_t> matching;
        for (VarEntry const& var_entry : var_entries) {
            if (var_entry.field_id == field_id &&
                (var_entry.mid == uid ||
                 var_entry.mid == (uid | kSecondaryUidFamily))) {
                matching.push_back(var_entry.index);
            }
        }
        entry_kept = !matching.empty();
        bool const already_empty = stored_empty;
        if (entry_kept && !already_empty) {
            /* Append the empty payload as a new block and repoint every matching
             * entry; the old block stays behind as unreferenced bytes (the same
             * append-only scheme save_field_values uses). */
            std::uint32_t const offset = static_cast<std::uint32_t>(var2.size());
            std::vector<std::uint8_t> const block =
                encode_block(encode_payload(empty));
            var2.insert(var2.end(), block.begin(), block.end());
            for (std::size_t const matching_index : matching) {
                std::size_t const base =
                    kVarMetaHeaderSize + matching_index * kVarMetaEntrySize;
                for (std::size_t byte = 0; byte < 4; ++byte) {
                    allowed_var_meta[base + 4 + byte] = true;
                }
                put_u32(var_meta, base + 4, offset);
            }
            for (std::size_t const byte : {kVarMetaVar2SizeOffset,
                                           kVarMetaVar2SizeOffset + 1,
                                           kVarMetaVar2SizeOffset + 2,
                                           kVarMetaVar2SizeOffset + 3}) {
                allowed_var_meta[byte] = true;
            }
            put_u32(var_meta, kVarMetaVar2SizeOffset,
                    static_cast<std::uint32_t>(var2.size()));
            changed = true;
        }
        if (!entry_kept) {
            warnings.push_back(
                "uid " + std::to_string(uid) + " field " + hex_id(field_id) +
                ": the row has no VarMeta entry for this field, so no block was "
                "written or repointed; the presence bits were cleared and the "
                "field stays absent");
        }
    }

    /* Nothing to change: still prove the file is the same all-or-nothing way. */
    if (!changed) {
        warnings.push_back("uid " + std::to_string(uid) + " field " +
                           hex_id(field_id) +
                           " was already empty; the file is unchanged");
    }

    CfbDocument work = CfbDocument::from_bytes(doc.to_bytes());
    if (meta != meta_before) {
        work.replace_stream(meta_path, meta);
    }
    if (var_meta != var_meta_before) {
        work.replace_stream(var_meta_path, var_meta);
        work.replace_stream(var2_path, var2);
    }
    if (fixed2_meta != doc.read_stream(fixed2_meta_path)) {
        work.replace_stream(fixed2_meta_path, fixed2_meta);
    }

    CfbDocument const reread = save_and_verify(work, path, function, table);
    TaskTable const after_table = TaskTable::from_document(reread, table);
    if (after_table.rows() != rows) {
        throw Error(function + ": the written file has " +
                    std::to_string(after_table.rows()) + " rows, expected " +
                    std::to_string(rows) + "; a field clear must not change rows");
    }

    /* ---- Prove only the intended bytes moved. */
    if (reread.read_stream(fixed_path) != fixed_before ||
        reread.read_stream(fixed2_path) != fixed2_before) {
        throw Error(function + ": FixedData/Fixed2Data changed; a custom-field "
                    "clear must not rewrite the fixed rows");
    }
    std::vector<std::uint8_t> const meta_after = reread.read_stream(meta_path);
    std::vector<std::uint8_t> const var_meta_after = reread.read_stream(var_meta_path);
    std::vector<std::uint8_t> const var2_after = reread.read_stream(var2_path);
    std::vector<std::uint8_t> const fixed2_meta_after =
        reread.read_stream(fixed2_meta_path);
    check_changed_bytes(meta_before, meta_after, allowed_meta, "FixedMeta", function);
    check_changed_bytes(var_meta_before, var_meta_after, allowed_var_meta, "VarMeta",
                        function);
    check_changed_bytes(var2_before, var2_after, allowed_var2, "Var2Data", function);
    check_changed_bytes(doc.read_stream(fixed2_meta_path), fixed2_meta_after,
                        allowed_fixed2_meta, "Fixed2Meta", function);
    if (var_meta_after.size() != var_meta_before.size()) {
        throw Error(function + ": VarMeta changed size; a clear must not add or "
                    "remove entries");
    }
    if (get_u32(var_meta_after, 8) != get_u32(var_meta_before, 8) ||
        get_u32(var_meta_after, kVarMetaVar2SizeOffset) != var2_after.size()) {
        throw Error(function + ": VarMeta's header counts do not read back as the "
                    "written streams");
    }
    if (get_u16(meta_after, meta_base + kFixedMetaFieldCount) !=
        get_u16(meta_before, meta_base + kFixedMetaFieldCount)) {
        throw Error(function + ": FixedMeta +2 changed; the VarMeta entry was kept, "
                    "so the row's field count must not change");
    }
    std::vector<VarEntry> const entries_after = read_var_entries(var_meta_after);
    if (entries_after.size() != var_entries.size()) {
        throw Error(function + ": VarMeta holds " +
                    std::to_string(entries_after.size()) + " entries after the "
                    "clear, expected " + std::to_string(var_entries.size()));
    }
    for (std::size_t i = 0; i < entries_after.size(); ++i) {
        if (entries_after[i].mid != var_entries[i].mid ||
            entries_after[i].field_id != var_entries[i].field_id) {
            throw Error(function + ": VarMeta entry " + std::to_string(i) +
                        " changed identity; a clear must not reorder entries");
        }
        bool const is_cleared =
            entries_after[i].field_id == field_id &&
            (entries_after[i].mid == uid ||
             entries_after[i].mid == (uid | kSecondaryUidFamily));
        if (!is_cleared && entries_after[i].offset != var_entries[i].offset) {
            throw Error(function + ": an untouched VarMeta entry moved; a clear "
                        "may only repoint the cleared field's entries");
        }
    }

    /* The registries are not ours to touch (Project keeps the field registered). */
    bool const has_props = doc.has_stream(props_path);
    if (has_props && reread.read_stream(props_path) != doc.read_stream(props_path)) {
        throw Error(function + ": " + props_path + " changed; clearing a value "
                    "must leave the field registration byte-identical");
    }
    bool const has_assn_props = doc.has_stream(assn_props_path);
    if (has_assn_props &&
        reread.read_stream(assn_props_path) != doc.read_stream(assn_props_path)) {
        throw Error(function + ": " + assn_props_path + " changed; clearing a "
                    "value must leave the assignment-side registration "
                    "byte-identical");
    }
    for (std::string const& path_name : doc.stream_paths()) {
        if (path_name == meta_path || path_name == var_meta_path ||
            path_name == var2_path || path_name == fixed2_meta_path ||
            path_name == props_path || path_name == assn_props_path) {
            continue;
        }
        if (!reread.has_stream(path_name) ||
            reread.read_stream(path_name) != doc.read_stream(path_name)) {
            throw Error(function + ": " + path_name +
                        " changed; clearing a custom-field value may only touch "
                        "FixedMeta, Fixed2Meta, VarMeta and Var2Data");
        }
    }

    /* ---- Re-read the value through the measured encoding. */
    FieldValue const after = read_field(reread, table, uid, row, field_id);
    if (kind == FieldKind::flag) {
        if (!after.present || !after.storage_measured || after.flag) {
            throw Error(function + ": " + label +
                        " does not read back as a cleared Flag1");
        }
    } else if (entry_kept) {
        if (!after.present || !after.storage_measured ||
            !same_value(after, empty)) {
            throw Error(function + ": " + label +
                        " does not read back as the empty value");
        }
    } else if (after.present) {
        throw Error(function + ": " + label +
                    " grew a VarMeta entry while being cleared");
    }
    if (!tail_bits_are_clear(meta_after, meta_base, field_id) &&
        kind != FieldKind::flag) {
        throw Error(function + ": " + label +
                    " still has a FixedMeta tail-presence bit set after the clear");
    }

    ClearedField result;
    result.uid = uid;
    result.field_id = field_id;
    result.kind = kind;
    result.was_present = was_present;
    result.entry_kept = entry_kept;
    result.flag = false;
    result.value = after;
    result.warnings = std::move(warnings);
    result.warnings.push_back(
        "re-read from disk: uid " + std::to_string(uid) + " field " +
        hex_id(field_id) +
        " is cleared (empty encoding stored, FixedMeta presence bits clear, field "
        "registration kept)");
    doc = reread;
    return result;
}

/* ---- W60: assignment custom fields (114/TBkndAssn) ---------------------- */

std::uint32_t assignment_property_id(FieldKind kind, unsigned index) {
    if (kind == FieldKind::number && index >= 1 && index <= 20) {
        return 0x0F40403Cu + (index - 1u);
    }
    if (kind == FieldKind::text && index >= 1 && index <= 30) {
        return 0x0F404064u + (index - 1u);
    }
    return 0;
}

namespace {

/* 114/TBkndAssn geometry (measured W54/W60; see add_delete.h and
 * docs/analysis/oracle/w14-oracle.md §3):
 *   FixedData    110 * rows, no header
 *   FixedMeta    16 + 34 * rows (one entry per row, +4 = row*110)
 *   Fixed2Data   48 * rows (three 16-byte identities)
 *   Fixed2Meta   16 + 53 * rows (+4 = row*48)
 *   VarMeta      24 + 12 * entries
 *   Var2Data     length-prefixed blocks. */
constexpr std::size_t kAssignmentRowSize = 110;
constexpr std::size_t kAssignmentMetaEntrySize = 34;
constexpr std::size_t kAssignmentFixed2RecordSize = 48;
constexpr std::size_t kAssignmentFixed2MetaEntrySize = 53;
constexpr std::size_t kDependentMetaHeaderSize = 16;
constexpr std::uint32_t kDependentMetaMagic = 0xFADFADBAu;
constexpr std::size_t kAssignmentMetaStateByte = 0;
constexpr std::size_t kAssignmentMetaFieldCount = 2;
constexpr std::size_t kAssignmentMetaRowOffset = 4;
constexpr std::size_t kAssignmentFixed2MetaRecordOffset = 4;
constexpr std::size_t kAssignmentUidWord = 0;
constexpr std::size_t kAssignmentTaskUidWord = 4;

struct AssignmentTable {
    std::string fixed_path;
    std::string meta_path;
    std::string fixed2_path;
    std::string fixed2_meta_path;
    std::string var_meta_path;
    std::string var2_path;
    std::vector<std::uint8_t> fixed;
    std::vector<std::uint8_t> meta;
    std::vector<std::uint8_t> fixed2;
    std::vector<std::uint8_t> fixed2_meta;
    std::vector<std::uint8_t> var_meta;
    std::vector<std::uint8_t> var2;
    std::vector<MetaEntry> entries;
    std::size_t rows = 0;
};

std::vector<MetaEntry> read_meta_entries_of(std::vector<std::uint8_t> const& meta,
                                            std::size_t entry_size) {
    std::vector<MetaEntry> out;
    if (meta.size() < kFixedMetaHeaderSize) {
        return out;
    }
    std::size_t const count = (meta.size() - kFixedMetaHeaderSize) / entry_size;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        std::size_t const base = kFixedMetaHeaderSize + i * entry_size;
        MetaEntry entry;
        entry.index = i;
        entry.offset = get_u32(meta, base + kAssignmentMetaRowOffset);
        entry.state = meta[base + kAssignmentMetaStateByte];
        entry.field_count = get_u16(meta, base + kAssignmentMetaFieldCount);
        out.push_back(entry);
    }
    return out;
}

/* The measured assignment table, strictly validated like add_delete.cpp does
 * (W54): every size/count word must agree, entries must cover whole rows, and
 * the VarMeta array must be grouped by ascending uid. */
AssignmentTable load_assignment_table(CfbDocument const& doc,
                                      std::string const& function) {
    AssignmentTable out;
    out.fixed_path = "114/TBkndAssn/FixedData";
    out.meta_path = "114/TBkndAssn/FixedMeta";
    out.fixed2_path = "114/TBkndAssn/Fixed2Data";
    out.fixed2_meta_path = "114/TBkndAssn/Fixed2Meta";
    out.var_meta_path = "114/TBkndAssn/VarMeta";
    out.var2_path = "114/TBkndAssn/Var2Data";
    for (std::string const* path :
         {&out.fixed_path, &out.meta_path, &out.fixed2_path, &out.fixed2_meta_path,
          &out.var_meta_path, &out.var2_path}) {
        if (!doc.has_stream(*path)) {
            throw Error(function + ": the assignment table has no " + *path +
                        " stream; an assignment custom-field write needs all six "
                        "TBkndAssn streams");
        }
    }
    out.fixed = doc.read_stream(out.fixed_path);
    if (out.fixed.size() % kAssignmentRowSize != 0) {
        throw Error(function + ": " + out.fixed_path + " is " +
                    std::to_string(out.fixed.size()) + " bytes, not n*110; "
                    "refusing to change the assignment table's geometry");
    }
    out.rows = out.fixed.size() / kAssignmentRowSize;
    out.meta = doc.read_stream(out.meta_path);
    if (out.meta.size() !=
        kDependentMetaHeaderSize + kAssignmentMetaEntrySize * out.rows) {
        throw Error(function + ": " + out.meta_path + " is " +
                    std::to_string(out.meta.size()) + " bytes, not 16 + 34*" +
                    std::to_string(out.rows) +
                    "; refusing to change the assignment table's geometry");
    }
    if (get_u32(out.meta, 0) != kDependentMetaMagic ||
        get_u32(out.meta, 8) != out.rows ||
        get_u32(out.meta, 12) != out.fixed.size()) {
        throw Error(function + ": " + out.meta_path +
                    " header count/size disagree with the assignment table");
    }
    out.entries = read_meta_entries_of(out.meta, kAssignmentMetaEntrySize);
    std::vector<bool> covered(out.rows, false);
    for (MetaEntry const& entry : out.entries) {
        if (entry.offset % kAssignmentRowSize != 0) {
            throw Error(function + ": " + out.meta_path + " entry " +
                        std::to_string(entry.index) + " has offset " +
                        std::to_string(entry.offset) + ", not a whole row");
        }
        std::size_t const row = entry.offset / kAssignmentRowSize;
        if (row >= out.rows) {
            throw Error(function + ": " + out.meta_path + " entry " +
                        std::to_string(entry.index) + " names row " +
                        std::to_string(row) + ", past the last row");
        }
        if (covered[row]) {
            throw Error(function + ": " + out.meta_path +
                        " has more than one entry for assignment row " +
                        std::to_string(row) + "; refusing a damaged table");
        }
        covered[row] = true;
    }
    out.fixed2 = doc.read_stream(out.fixed2_path);
    out.fixed2_meta = doc.read_stream(out.fixed2_meta_path);
    if (out.fixed2.size() != out.rows * kAssignmentFixed2RecordSize) {
        throw Error(function + ": " + out.fixed2_path + " is " +
                    std::to_string(out.fixed2.size()) + " bytes, not " +
                    std::to_string(out.rows) + "*48");
    }
    if (out.fixed2_meta.size() !=
        kDependentMetaHeaderSize + kAssignmentFixed2MetaEntrySize * out.rows) {
        throw Error(function + ": " + out.fixed2_meta_path + " is " +
                    std::to_string(out.fixed2_meta.size()) + " bytes, not 16 + " +
                    std::to_string(out.rows) + "*53");
    }
    if (get_u32(out.fixed2_meta, 0) != kDependentMetaMagic ||
        get_u32(out.fixed2_meta, 8) != out.rows ||
        get_u32(out.fixed2_meta, 12) != out.fixed2.size()) {
        throw Error(function + ": " + out.fixed2_meta_path +
                    " header count/size disagree with the assignment table");
    }
    for (std::size_t row = 0; row < out.rows; ++row) {
        if (get_u32(out.fixed2_meta, kDependentMetaHeaderSize +
                                        row * kAssignmentFixed2MetaEntrySize +
                                        kAssignmentFixed2MetaRecordOffset) !=
            row * kAssignmentFixed2RecordSize) {
            throw Error(function + ": " + out.fixed2_meta_path + " entry " +
                        std::to_string(row) +
                        " does not name its Fixed2Data record");
        }
    }
    out.var_meta = doc.read_stream(out.var_meta_path);
    out.var2 = doc.read_stream(out.var2_path);
    if (out.var_meta.size() < kVarMetaHeaderSize ||
        (out.var_meta.size() - kVarMetaHeaderSize) % kVarMetaEntrySize != 0) {
        throw Error(function + ": " + out.var_meta_path + " is " +
                    std::to_string(out.var_meta.size()) + " bytes, not 24 + 12*k");
    }
    std::vector<VarEntry> const entries = read_var_entries(out.var_meta);
    if (get_u32(out.var_meta, 8) != entries.size() ||
        get_u32(out.var_meta, kVarMetaVar2SizeOffset) != out.var2.size()) {
        throw Error(function + ": " + out.var_meta_path +
                    " header count/size disagree with the assignment table");
    }
    bool first = true;
    std::uint32_t previous_uid = 0;
    for (VarEntry const& entry : entries) {
        std::uint32_t const uid = entry.mid & 0x7FFFFFFFu;
        if (!first && uid < previous_uid) {
            throw Error(function + ": " + out.var_meta_path +
                        " is not grouped by ascending uid; refusing to append an "
                        "entry to a damaged array");
        }
        previous_uid = uid;
        first = false;
    }
    return out;
}

/* Where a new assignment VarMeta entry belongs: after the LAST entry of the
 * uid's contiguous group.  MEASURED (w14-assignfield): Project appends the new
 * custom entries after the assignment's existing entries and does NOT sort
 * them by key (the group is 0x0f400010 .. 0x0f40029b, then 0x0f40403c,
 * 0x0f404064).  A uid with no group is placed before the first larger uid. */
std::size_t assignment_var_insert_offset(
    std::vector<std::uint8_t> const& var_meta, std::uint32_t uid) {
    std::vector<VarEntry> const entries = read_var_entries(var_meta);
    std::size_t at = var_meta.size();
    bool found_group = false;
    for (VarEntry const& entry : entries) {
        std::uint32_t const base = entry.mid & 0x7FFFFFFFu;
        if (base == uid) {
            at = kVarMetaHeaderSize + (entry.index + 1) * kVarMetaEntrySize;
            found_group = true;
        } else if (!found_group && base > uid) {
            return kVarMetaHeaderSize + entry.index * kVarMetaEntrySize;
        }
    }
    return at;
}

void insert_assignment_var_entry(std::vector<std::uint8_t>& var_meta,
                                 std::uint32_t uid, std::uint32_t key,
                                 std::uint32_t offset) {
    std::size_t const at = assignment_var_insert_offset(var_meta, uid);
    std::vector<std::uint8_t> entry(kVarMetaEntrySize, 0);
    put_u32(entry, 0, uid);
    put_u32(entry, 4, offset);
    put_u32(entry, 8, key);
    var_meta.insert(var_meta.begin() + static_cast<std::ptrdiff_t>(at),
                    entry.begin(), entry.end());
}

struct AssignmentRow {
    std::size_t row = 0;
    std::size_t entry_index = 0;
    std::uint32_t task_uid = 0;
};

AssignmentRow find_assignment_row(AssignmentTable const& table,
                                  std::uint32_t uid, std::string const& label,
                                  std::string const& function) {
    std::vector<std::size_t> matches;
    for (std::size_t row = 0; row < table.rows; ++row) {
        if (get_u32(table.fixed, row * kAssignmentRowSize + kAssignmentUidWord) ==
            uid) {
            matches.push_back(row);
        }
    }
    if (matches.empty()) {
        throw Error(function + ": " + label +
                    ": no assignment row has that UniqueID");
    }
    if (matches.size() > 1) {
        throw Error(function + ": " + label +
                    ": the assignment table holds that UniqueID more than once; "
                    "refusing a damaged table");
    }
    AssignmentRow out;
    out.row = matches.front();
    std::int32_t const index = entry_index_for_offset(
        table.entries, static_cast<std::uint32_t>(out.row * kAssignmentRowSize));
    if (index < 0) {
        throw Error(function + ": " + label +
                    ": the row has no FixedMeta entry, so Project does not treat "
                    "it as an assignment");
    }
    out.entry_index = static_cast<std::size_t>(index);
    if (table.entries[out.entry_index].state != kStateLive) {
        throw Error(function + ": " + label +
                    ": the row's FixedMeta entry is a tombstone (deleted)");
    }
    out.task_uid =
        get_u32(table.fixed, out.row * kAssignmentRowSize + kAssignmentTaskUidWord);
    if (out.task_uid == 0) {
        throw Error(function + ": " + label +
                    ": the row is an unassigned placeholder (task uid 0); Project "
                    "does not read custom fields from it");
    }
    return out;
}

/* Best-effort liveness of the task an assignment names: false only when the
 * Task table is readable and positively says the uid is dead (missing row, no
 * FixedMeta entry or a tombstone).  A Task table this writer cannot parse does
 * not block an assignment-only write (that table is not touched). */
bool task_uid_is_live(CfbDocument const& doc, std::uint32_t uid) {
    if (!doc.has_stream("114/TBkndTask/FixedData") ||
        !doc.has_stream("114/TBkndTask/FixedMeta")) {
        return true;
    }
    std::vector<std::uint8_t> const fixed =
        doc.read_stream("114/TBkndTask/FixedData");
    std::vector<std::uint8_t> const meta =
        doc.read_stream("114/TBkndTask/FixedMeta");
    if (fixed.size() < 48 || (fixed.size() - 48) % 202 != 0 ||
        meta.size() < 16 || (meta.size() - 16) % 47 != 0) {
        return true;
    }
    std::size_t const rows = (fixed.size() - 48) / 202;
    std::vector<MetaEntry> const entries = read_meta_entries(meta);
    for (std::size_t row = 0; row < rows; ++row) {
        std::size_t const base = 48 + row * 202;
        if (get_u32(fixed, base + 4) != uid) {
            continue;
        }
        std::int32_t const index = entry_index_for_offset(
            entries, static_cast<std::uint32_t>(base));
        return index >= 0 && entries[static_cast<std::size_t>(index)].state ==
                                 kStateLive;
    }
    return false;
}

FieldValue read_assignment_field(AssignmentTable const& table, std::uint32_t uid,
                                 std::uint32_t field_id) {
    FieldKind const kind = field_kind_for_id(field_id);
    FieldValue out;
    out.field_id = field_id;
    out.kind = kind;
    std::uint32_t const key =
        assignment_property_id(kind, field_index_for_id(field_id));
    if (key == 0) {
        return out; /* no measured assignment storage for this field */
    }
    for (VarEntry const& entry : read_var_entries(table.var_meta)) {
        if (entry.mid != uid && entry.mid != (uid | kSecondaryUidFamily)) {
            continue;
        }
        if (entry.field_id != key) {
            continue;
        }
        if (entry.offset == 0xFFFFFFFFu ||
            static_cast<std::size_t>(entry.offset) + 4 > table.var2.size()) {
            continue;
        }
        FieldValue decoded = decode_payload(kind, read_var_block(table.var2, entry.offset));
        decoded.field_id = field_id;
        return decoded;
    }
    return out;
}

CfbDocument save_assignment_and_verify(CfbDocument& work, std::string const& path,
                                       std::string const& function) {
    std::vector<std::uint8_t> const intended = work.to_bytes();
    work.save(path);
    std::vector<std::uint8_t> const on_disk = read_file_bytes(path);
    if (on_disk != intended) {
        throw Error(function + ": " + path + " holds " +
                    std::to_string(on_disk.size()) +
                    " bytes, but the document built " +
                    std::to_string(intended.size()) + "; the write did not land");
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
    return reread;
}

bool props_has_record(std::vector<std::uint8_t> const& props, std::size_t preamble,
                      std::uint32_t key) {
    if (props.size() < preamble) {
        return false;
    }
    for (std::size_t offset = preamble;
         offset + kPropsRecordSize <= props.size();
         offset += kPropsRecordSize) {
        if (get_u32(props, offset) == key) {
            return true;
        }
    }
    return false;
}

} /* namespace */

FieldValue read_assignment_field_value(CfbDocument const& doc,
                                       std::uint32_t assignment_uid,
                                       std::uint32_t field_id) {
    std::string const function = "read_assignment_field_value";
    AssignmentTable const table = load_assignment_table(doc, function);
    return read_assignment_field(table, assignment_uid, field_id);
}

AssignmentFieldWriteResult save_assignment_field_values(
    CfbDocument& doc, std::vector<AssignmentFieldEdit> const& edits,
    std::string const& path) {
    std::string const function = "save_assignment_field_values";
    if (edits.empty()) {
        throw Error(function + ": the edit list is empty; there is nothing to save");
    }
    check_output_path(path, function);
    AssignmentTable const table = load_assignment_table(doc, function);

    std::string const task_props_path = "114/TBkndTask/Props";
    std::string const assn_props_path = "114/TBkndAssn/Props";
    for (std::string const& stream : {task_props_path, assn_props_path}) {
        if (!doc.has_stream(stream)) {
            throw Error(function + ": the file has no " + stream +
                        " stream; Project's assignment-side field registration "
                        "lives there and the writer cannot create a stream");
        }
    }
    std::vector<std::uint8_t> task_props = doc.read_stream(task_props_path);
    std::vector<std::uint8_t> assn_props = doc.read_stream(assn_props_path);
    validate_props_stream(task_props, kTaskPropsLayout, task_props_path, function);
    validate_props_stream(assn_props, kAssnPropsLayout, assn_props_path, function);

    /* ---- Validate every edit before touching anything. */
    struct Prepared {
        AssignmentFieldEdit edit;
        FieldKind kind = FieldKind::unknown;
        unsigned index = 0;
        std::uint32_t key = 0;
        std::size_t row = 0;
        std::size_t entry_index = 0;
        std::uint32_t task_uid = 0;
        std::vector<std::uint8_t> payload;
    };
    std::vector<Prepared> prepared;
    prepared.reserve(edits.size());
    std::vector<std::pair<std::uint32_t, std::uint32_t>> seen;
    for (AssignmentFieldEdit const& edit : edits) {
        Prepared item;
        item.edit = edit;
        std::uint32_t const field_id = edit.value.field_id;
        std::string const label = "assignment " +
                                  std::to_string(edit.assignment_uid) + " field " +
                                  hex_id(field_id);
        item.kind = field_kind_for_id(field_id);
        item.index = field_index_for_id(field_id);
        item.key = assignment_property_id(item.kind, item.index);
        if (item.kind == FieldKind::unknown) {
            throw Error(function + ": " + label +
                        " is not a task/assignment custom field in the measured "
                        "id ranges (see field_value.h / analysis/field_id_map.tsv)");
        }
        if (item.key == 0) {
            throw Error(function + ": " + label +
                        " has no measured storage on an assignment (only "
                        "Number1..Number20 and Text1..Text30 are measured there; "
                        "Cost, Flag, Date and Duration assignment fields are not)");
        }
        if (edit.value.kind != item.kind) {
            throw Error(function + ": " + label + " is a " + kind_name(item.kind) +
                        " field but the value says kind " +
                        std::to_string(static_cast<int>(edit.value.kind)) +
                        "; the kind must match the id");
        }
        if (edit.assignment_uid == 0) {
            throw Error(function + ": " + label +
                        ": the assignment uid is 0; no assignment row has it");
        }
        AssignmentRow const row =
            find_assignment_row(table, edit.assignment_uid, label, function);
        if (!task_uid_is_live(doc, row.task_uid)) {
            throw Error(function + ": " + label + ": the row's task uid " +
                        std::to_string(row.task_uid) +
                        " has no live task row; Project would not show the "
                        "assignment");
        }
        for (auto const& key : seen) {
            if (key.first == edit.assignment_uid && key.second == field_id) {
                throw Error(function + ": " + label +
                            " appears twice; one value per field");
            }
        }
        seen.emplace_back(edit.assignment_uid, field_id);
        switch (item.kind) {
            case FieldKind::text:
                if (edit.value.text.find('\0') != std::string::npos) {
                    throw Error(function + ": " + label +
                                ": the text contains an embedded NUL, which the "
                                "UTF-16+NUL block cannot represent");
                }
                if (edit.value.text.size() > kMaxTextBytes) {
                    throw Error(function + ": " + label + ": the text is " +
                                std::to_string(edit.value.text.size()) +
                                " bytes, longer than the " +
                                std::to_string(kMaxTextBytes) +
                                "-byte limit for one value");
                }
                break;
            case FieldKind::number:
                if (!std::isfinite(edit.value.number)) {
                    throw Error(function + ": " + label +
                                ": the number is not finite");
                }
                break;
            case FieldKind::cost:
            case FieldKind::date:
            case FieldKind::duration:
            case FieldKind::flag:
            case FieldKind::unknown:
                break; /* already refused above */
        }
        item.row = row.row;
        item.entry_index = row.entry_index;
        item.task_uid = row.task_uid;
        item.payload = encode_payload(item.edit.value);
        prepared.push_back(std::move(item));
    }

    /* ---- Register every written field once, in both Props streams. */
    std::vector<std::uint32_t> fields_to_register;
    for (Prepared const& item : prepared) {
        bool known = false;
        for (std::uint32_t const id : fields_to_register) {
            known = known || id == item.edit.value.field_id;
        }
        if (!known) {
            fields_to_register.push_back(item.edit.value.field_id);
        }
    }
    std::vector<std::array<std::uint8_t, kPropsRecordSize>> task_records;
    std::vector<std::array<std::uint8_t, kPropsRecordSize>> assn_records;
    for (std::uint32_t const field_id : fields_to_register) {
        FieldKind const kind = field_kind_for_id(field_id);
        unsigned const index = field_index_for_id(field_id);
        std::uint32_t const key = assignment_property_id(kind, index);
        AssignmentPropsShape shape;
        if (!assignment_props_shape(kind, shape)) {
            throw Error(function + ": internal error: no Props shape for field " +
                        hex_id(field_id));
        }
        task_records.push_back(
            assignment_task_props_record(field_id, key, shape));
        assn_records.push_back(
            assignment_assn_props_record(field_id, key, shape));
    }
    std::size_t const task_registered =
        register_props_records(task_props, kTaskPropsLayout, task_records);
    std::size_t const assn_registered =
        register_props_records(assn_props, kAssnPropsLayout, assn_records);
    if (task_registered > 0) {
        write_props_words(task_props, kTaskPropsLayout, task_props_path, function);
    }
    if (assn_registered > 0) {
        write_props_words(assn_props, kAssnPropsLayout, assn_props_path, function);
    }

    /* ---- Apply. */
    std::vector<std::uint8_t> meta = table.meta;
    std::vector<std::uint8_t> var_meta = table.var_meta;
    std::vector<std::uint8_t> var2 = table.var2;
    std::vector<bool> allowed_meta(meta.size(), false);
    std::vector<bool> const allowed_var2(var2.size(), false);
    struct WrittenField {
        std::uint32_t uid = 0;
        std::uint32_t field_id = 0;
        std::uint32_t key = 0;
        std::uint32_t offset = 0;
        std::size_t row = 0;
        bool added_entry = false;
    };
    std::vector<WrittenField> written;
    written.reserve(prepared.size());
    std::size_t added_entries = 0;
    std::map<std::uint32_t, std::size_t> touched_row; /* assignment uid -> row */
    for (Prepared const& item : prepared) {
        touched_row[item.edit.assignment_uid] = item.row;
        std::uint32_t const offset = static_cast<std::uint32_t>(var2.size());
        std::vector<std::uint8_t> const block = encode_block(item.payload);
        var2.insert(var2.end(), block.begin(), block.end());
        bool repointed = false;
        for (VarEntry const& entry : read_var_entries(var_meta)) {
            if (entry.field_id != item.key) {
                continue;
            }
            if (entry.mid != item.edit.assignment_uid &&
                entry.mid != (item.edit.assignment_uid | kSecondaryUidFamily)) {
                continue;
            }
            std::size_t const base =
                kVarMetaHeaderSize + entry.index * kVarMetaEntrySize;
            put_u32(var_meta, base + 4, offset);
            repointed = true;
        }
        if (!repointed) {
            insert_assignment_var_entry(var_meta, item.edit.assignment_uid,
                                        item.key, offset);
            ++added_entries;
        }
        WrittenField record;
        record.uid = item.edit.assignment_uid;
        record.field_id = item.edit.value.field_id;
        record.key = item.key;
        record.offset = offset;
        record.row = item.row;
        record.added_entry = !repointed;
        written.push_back(record);
    }
    /* FixedMeta +2 is the row's exact VarMeta entry count; assignments have no
     * tail presence bit (measured: Project's own write changes only +2). */
    for (auto const& touched : touched_row) {
        std::size_t count = 0;
        for (VarEntry const& entry : read_var_entries(var_meta)) {
            if ((entry.mid & 0x7FFFFFFFu) == touched.first) {
                ++count;
            }
        }
        if (count > 0xFFFFu) {
            throw Error(function + ": assignment " +
                        std::to_string(touched.first) + " has " +
                        std::to_string(count) +
                        " VarMeta entries, too many for FixedMeta +2; refusing");
        }
        std::int32_t const index = entry_index_for_offset(
            table.entries,
            static_cast<std::uint32_t>(touched.second * kAssignmentRowSize));
        if (index < 0) {
            throw Error(function + ": internal error: assignment " +
                        std::to_string(touched.first) + " lost its FixedMeta entry");
        }
        std::size_t const base = kFixedMetaHeaderSize +
                                 static_cast<std::size_t>(index) *
                                     kAssignmentMetaEntrySize;
        put_u16(meta, base + kAssignmentMetaFieldCount,
                static_cast<std::uint16_t>(count));
        allowed_meta[base + kAssignmentMetaFieldCount] = true;
        allowed_meta[base + kAssignmentMetaFieldCount + 1] = true;
    }
    put_u32(var_meta, 8,
            static_cast<std::uint32_t>(read_var_entries(var_meta).size()));
    put_u32(var_meta, kVarMetaVar2SizeOffset,
            static_cast<std::uint32_t>(var2.size()));

    CfbDocument work = CfbDocument::from_bytes(doc.to_bytes());
    work.replace_stream(table.meta_path, meta);
    work.replace_stream(table.var_meta_path, var_meta);
    work.replace_stream(table.var2_path, var2);
    if (task_registered > 0) {
        work.replace_stream(task_props_path, task_props);
    }
    if (assn_registered > 0) {
        work.replace_stream(assn_props_path, assn_props);
    }

    CfbDocument const reread =
        save_assignment_and_verify(work, path, function);
    AssignmentTable const after = load_assignment_table(reread, function);
    if (after.rows != table.rows) {
        throw Error(function + ": the written file has " +
                    std::to_string(after.rows) + " assignment rows, expected " +
                    std::to_string(table.rows) +
                    "; a field write must not add or remove rows");
    }

    /* ---- Prove the intended bytes are the only ones that moved. */
    if (reread.read_stream(table.fixed_path) != table.fixed ||
        reread.read_stream(table.fixed2_path) != table.fixed2 ||
        reread.read_stream(table.fixed2_meta_path) != table.fixed2_meta) {
        throw Error(function + ": FixedData/Fixed2Data/Fixed2Meta changed; an "
                    "assignment custom-field write must not rewrite the rows");
    }
    check_changed_bytes(table.meta, reread.read_stream(table.meta_path),
                        allowed_meta, "TBkndAssn/FixedMeta", function);
    check_changed_bytes(table.var2, reread.read_stream(table.var2_path),
                        allowed_var2, "TBkndAssn/Var2Data", function);

    std::vector<std::uint8_t> const var_meta_after =
        reread.read_stream(table.var_meta_path);
    std::vector<std::uint8_t> const var2_after = reread.read_stream(table.var2_path);
    std::vector<VarEntry> const entries_before = read_var_entries(table.var_meta);
    std::vector<VarEntry> const entries_after = read_var_entries(var_meta_after);
    if (entries_after.size() != entries_before.size() + added_entries) {
        throw Error(function + ": VarMeta holds " +
                    std::to_string(entries_after.size()) + " entries, expected " +
                    std::to_string(entries_before.size() + added_entries));
    }
    for (std::size_t i = 0; i < kVarMetaHeaderSize; ++i) {
        bool const may_move =
            (i >= 8 && i < 12) ||
            (i >= kVarMetaVar2SizeOffset && i < kVarMetaVar2SizeOffset + 4);
        if (!may_move && var_meta_after[i] != table.var_meta[i]) {
            throw Error(function + ": VarMeta header byte +" + std::to_string(i) +
                        " changed; only the entry count and the Var2Data size may "
                        "be updated");
        }
    }
    if (get_u32(var_meta_after, 8) != entries_after.size() ||
        get_u32(var_meta_after, kVarMetaVar2SizeOffset) != var2_after.size()) {
        throw Error(function + ": VarMeta's header counts do not read back as the "
                    "written streams");
    }
    if (var2_after != var2) {
        throw Error(function + ": Var2Data is " + std::to_string(var2_after.size()) +
                    " bytes, expected " + std::to_string(var2.size()));
    }
    /* The exact entry multiset must be: every original entry (repointed to the
     * new block for an edited field) plus every added entry.  A repoint changes
     * every matching entry of the uid family, so the expectation is built from
     * the before array. */
    auto is_edited = [&written](VarEntry const& entry) {
        for (WrittenField const& w : written) {
            if (entry.field_id != w.key) {
                continue;
            }
            if (entry.mid == w.uid ||
                entry.mid == (w.uid | kSecondaryUidFamily)) {
                return true;
            }
        }
        return false;
    };
    std::multiset<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>>
        expected;
    for (VarEntry const& entry : entries_before) {
        std::uint32_t offset = entry.offset;
        if (is_edited(entry)) {
            for (WrittenField const& w : written) {
                if (entry.field_id == w.key &&
                    (entry.mid == w.uid ||
                     entry.mid == (w.uid | kSecondaryUidFamily))) {
                    offset = w.offset;
                }
            }
        }
        expected.emplace(entry.mid, entry.field_id, offset);
    }
    for (WrittenField const& w : written) {
        if (w.added_entry) {
            expected.emplace(w.uid, w.key, w.offset);
        }
    }
    std::multiset<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>>
        actual;
    for (VarEntry const& entry : entries_after) {
        actual.emplace(entry.mid, entry.field_id, entry.offset);
    }
    if (actual != expected) {
        throw Error(function + ": the VarMeta entry set after the write is not "
                    "(the original entries with the edited reps' offsets) + (the "
                    "new entries); an unrelated entry appeared, disappeared or "
                    "moved");
    }
    /* Every ADDED entry must be the tail of its uid's group (Project appends the
     * assignment custom entries; it does not key-sort them), and the added
     * entries of one uid must keep the edit order. */
    std::map<std::uint32_t, std::size_t> last_added_position;
    for (WrittenField const& w : written) {
        if (!w.added_entry) {
            continue;
        }
        std::int32_t found = -1;
        for (std::size_t i = 0; i < entries_after.size(); ++i) {
            if (entries_after[i].field_id == w.key &&
                (entries_after[i].mid == w.uid ||
                 entries_after[i].mid == (w.uid | kSecondaryUidFamily)) &&
                entries_after[i].offset == w.offset) {
                found = static_cast<std::int32_t>(i);
                break;
            }
        }
        if (found < 0) {
            throw Error(function + ": assignment " + std::to_string(w.uid) +
                        " field " + hex_id(w.field_id) +
                        " has no VarMeta entry pointing at the written block");
        }
        std::size_t const position = static_cast<std::size_t>(found);
        auto const last = last_added_position.find(w.uid);
        if (last != last_added_position.end() && position <= last->second) {
            throw Error(function + ": the new assignment entries are not in edit "
                        "order; refusing an array Project would reorder");
        }
        last_added_position[w.uid] = position;
        for (std::size_t i = position + 1; i < entries_after.size(); ++i) {
            if ((entries_after[i].mid & 0x7FFFFFFFu) != w.uid) {
                continue;
            }
            bool written_entry = false;
            for (WrittenField const& other : written) {
                if (other.added_entry && other.uid == w.uid &&
                    entries_after[i].field_id == other.key &&
                    entries_after[i].offset == other.offset) {
                    written_entry = true;
                    break;
                }
            }
            if (!written_entry) {
                throw Error(function + ": a new assignment entry was inserted "
                            "before an existing entry of the same uid; Project "
                            "appends assignment custom entries, so this is refused");
            }
        }
    }
    /* Group contiguity: entries with the same base uid are consecutive. */
    std::set<std::uint32_t> closed;
    std::uint32_t previous_uid = 0;
    bool first_entry = true;
    for (VarEntry const& entry : entries_after) {
        std::uint32_t const uid = entry.mid & 0x7FFFFFFFu;
        if (!first_entry && uid != previous_uid) {
            closed.insert(previous_uid);
            if (closed.count(uid) != 0) {
                throw Error(function + ": VarMeta is not grouped by uid after the "
                            "write; Project would not read the assignment value");
            }
        }
        previous_uid = uid;
        first_entry = false;
    }
    /* FixedMeta +2 and the tail bytes read back as written (no tail bit). */
    std::vector<std::uint8_t> const meta_after = reread.read_stream(table.meta_path);
    for (auto const& touched : touched_row) {
        std::size_t count = 0;
        for (VarEntry const& entry : entries_after) {
            if ((entry.mid & 0x7FFFFFFF) == touched.first) {
                ++count;
            }
        }
        std::int32_t const index = entry_index_for_offset(
            after.entries,
            static_cast<std::uint32_t>(touched.second * kAssignmentRowSize));
        if (index < 0) {
            throw Error(function + ": the written file lost the FixedMeta entry "
                        "for assignment " + std::to_string(touched.first));
        }
        std::size_t const base = kFixedMetaHeaderSize +
                                 static_cast<std::size_t>(index) *
                                     kAssignmentMetaEntrySize;
        if (get_u16(meta_after, base + kAssignmentMetaFieldCount) != count) {
            throw Error(function + ": assignment " +
                        std::to_string(touched.first) + " FixedMeta +2 reads " +
                        std::to_string(
                            get_u16(meta_after, base + kAssignmentMetaFieldCount)) +
                        ", expected its " + std::to_string(count) +
                        " VarMeta entries");
        }
        if (meta_after.size() != table.meta.size()) {
            throw Error(function + ": TBkndAssn/FixedMeta changed size; an "
                        "assignment field write must not add an entry");
        }
    }

    verify_props_stream(reread, task_props_path, kTaskPropsLayout,
                        doc.read_stream(task_props_path), task_records,
                        task_registered, function);
    verify_props_stream(reread, assn_props_path, kAssnPropsLayout,
                        doc.read_stream(assn_props_path), assn_records,
                        assn_registered, function);

    /* ---- Re-read every edit through the measured encoding. */
    AssignmentFieldWriteResult result;
    result.fields_written = prepared.size();
    for (Prepared const& item : prepared) {
        FieldValue const stored = read_assignment_field(
            after, item.edit.assignment_uid, item.edit.value.field_id);
        if (!stored.present || !stored.storage_measured ||
            !same_value(stored, item.edit.value)) {
            throw Error(function + ": assignment " +
                        std::to_string(item.edit.assignment_uid) + " field " +
                        hex_id(item.edit.value.field_id) +
                        " does not read back as written (present " +
                        (stored.present ? "true" : "false") +
                        ", storage_measured " +
                        (stored.storage_measured ? "true" : "false") + ", text '" +
                        stored.text + "', number " +
                        std::to_string(stored.number) + ")");
        }
        AssignmentFieldWrite write;
        write.assignment_uid = item.edit.assignment_uid;
        write.value = stored;
        result.fields.push_back(std::move(write));
    }
    result.warnings.push_back(
        "re-read from disk: " + std::to_string(prepared.size()) +
        " assignment custom-field value(s) match what was written");
    if (task_registered > 0 || assn_registered > 0) {
        result.warnings.push_back(
            "registered " + std::to_string(task_registered) +
            " field definition(s) in " + task_props_path + " and " +
            std::to_string(assn_registered) + " in " + assn_props_path +
            " so Project displays the assignment values");
    }
    result.warnings.push_back(
        "an assignment custom field has no FixedMeta tail presence bit (measured: "
        "Project's own write changes only the row entry's +2 count)");
    doc = reread;
    return result;
}

/* ---- W60: the store-completeness diagnostics --------------------------- */

FieldStores field_stores(CfbDocument const& doc, std::uint32_t uid,
                         std::uint32_t field_id, std::string_view table) {
    FieldStores out;
    FieldKind const kind = field_kind_for_id(field_id);
    if (kind == FieldKind::unknown) {
        return out;
    }
    std::string const fixed_path = table_stream(table, "FixedData");
    std::string const meta_path = table_stream(table, "FixedMeta");
    std::string const props_path = table_stream(table, "Props");
    if (!doc.has_stream(fixed_path) || !doc.has_stream(meta_path)) {
        return out;
    }
    std::vector<std::uint8_t> const fixed = doc.read_stream(fixed_path);
    std::vector<std::uint8_t> const meta = doc.read_stream(meta_path);
    if (fixed.size() < 48 || (fixed.size() - 48) % 202 != 0) {
        return out;
    }
    std::size_t const rows = (fixed.size() - 48) / 202;
    std::size_t row = rows;
    for (std::size_t candidate = 0; candidate < rows; ++candidate) {
        if (get_u32(fixed, 48 + candidate * 202 + 4) == uid) {
            row = candidate;
            break;
        }
    }
    if (row >= rows) {
        return out;
    }
    std::vector<MetaEntry> const entries = read_meta_entries(meta);
    std::int32_t const entry_index = entry_index_for_offset(
        entries, static_cast<std::uint32_t>(task_row_offset(row)));
    if (entry_index < 0) {
        return out;
    }
    std::size_t const base =
        kFixedMetaHeaderSize + static_cast<std::size_t>(entry_index) *
                                   kFixedMetaEntrySize;
    if (kind == FieldKind::flag) {
        out.flag_field = true;
        out.presence = true; /* the row entry exists; a clear bit means False */
    } else {
        std::string const var_meta_path = table_stream(table, "VarMeta");
        std::string const var2_path = table_stream(table, "Var2Data");
        if (doc.has_stream(var_meta_path) && doc.has_stream(var2_path)) {
            std::vector<std::uint8_t> const var_meta =
                doc.read_stream(var_meta_path);
            std::vector<std::uint8_t> const var2 = doc.read_stream(var2_path);
            for (VarEntry const& entry : read_var_entries(var_meta)) {
                if (entry.field_id != field_id ||
                    (entry.mid != uid &&
                     entry.mid != (uid | kSecondaryUidFamily))) {
                    continue;
                }
                out.var_meta = true;
                if (entry.offset != 0xFFFFFFFFu &&
                    static_cast<std::size_t>(entry.offset) + 4 <= var2.size()) {
                    try {
                        FieldValue const decoded =
                            decode_payload(kind, read_var_block(var2, entry.offset));
                        out.var2_block = decoded.storage_measured;
                    } catch (Error const&) {
                        out.var2_block = false;
                    }
                }
                break;
            }
        }
        std::vector<TailBit> const bits = tail_bits_for(field_id);
        if (!bits.empty()) {
            out.presence = true;
            for (TailBit const& bit : bits) {
                if (base + bit.offset >= meta.size() ||
                    (meta[base + bit.offset] & bit.mask) == 0) {
                    out.presence = false;
                }
            }
        }
    }
    if (doc.has_stream(props_path)) {
        out.registered =
            props_has_record(doc.read_stream(props_path), kTaskPropsLayout.preamble,
                             field_id);
    }
    return out;
}

FieldStores assignment_field_stores(CfbDocument const& doc,
                                    std::uint32_t assignment_uid,
                                    std::uint32_t field_id) {
    FieldStores out;
    FieldKind const kind = field_kind_for_id(field_id);
    std::uint32_t const key =
        assignment_property_id(kind, field_index_for_id(field_id));
    if (key == 0) {
        return out;
    }
    AssignmentTable table;
    try {
        table = load_assignment_table(doc, "assignment_field_stores");
    } catch (Error const&) {
        return out;
    }
    for (VarEntry const& entry : read_var_entries(table.var_meta)) {
        if (entry.field_id != key ||
            (entry.mid != assignment_uid &&
             entry.mid != (assignment_uid | kSecondaryUidFamily))) {
            continue;
        }
        out.var_meta = true;
        if (entry.offset != 0xFFFFFFFFu &&
            static_cast<std::size_t>(entry.offset) + 4 <= table.var2.size()) {
            try {
                FieldValue const decoded =
                    decode_payload(kind, read_var_block(table.var2, entry.offset));
                out.var2_block = decoded.storage_measured;
            } catch (Error const&) {
                out.var2_block = false;
            }
        }
        break;
    }
    /* Measured: Project writes no FixedMeta tail bit for an assignment field,
     * so its presence store does not exist and is trivially satisfied. */
    out.presence = true;
    std::string const task_props_path = "114/TBkndTask/Props";
    std::string const assn_props_path = "114/TBkndAssn/Props";
    bool const task_registered =
        doc.has_stream(task_props_path) &&
        props_has_record(doc.read_stream(task_props_path),
                         kTaskPropsLayout.preamble, field_id);
    bool const assn_registered =
        doc.has_stream(assn_props_path) &&
        props_has_record(doc.read_stream(assn_props_path),
                         kAssnPropsLayout.preamble, key);
    out.registered = task_registered && assn_registered;
    return out;
}

} /* namespace mppwrite */
