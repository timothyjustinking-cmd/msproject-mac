/*
 * resource_growth.h -- W34-3.2: the measured Add Resources shape, as Project
 * 2021 writes it.
 *
 * WHY THIS FILE EXISTS.  `add_delete.h` can append a resource row
 * (`add_resource_row`, W54), measured on `w12-assignadd`, but it writes only the
 * six TBkndRsc streams.  Project's OWN Add Resources save touches eleven
 * streams: the six resource streams, FOUR `TBkndCal` streams (every resource
 * gets a calendar row) and three counts in `114/Props`.  The pairs behind
 * `spec/writers/calendar-tbkdcal.md` and `spec/writers/evidence/` are
 * `wave30-resource-add` (the second resource, "R2") and
 * `wave34-resource-add-third` (the third, "R3"), each with a committed null
 * save, so the pair tests mask per-save noise and compare every effect.
 *
 * THE MEASURED RULES (n = the resource table's rows, row 0 being the uid-0
 * placeholder; 2 for the wave30 control, 3 for the wave34 control):
 *   Rsc FixedData   +172  a clone of the last live row with +0 id := largest
 *                         live id + 1, +4 uid := largest uid + 1, and the u16
 *                         at +154 := its old value + 1 (MaxUnits stays 100 %)
 *   Rsc FixedMeta   +37   a clone of the last live row's entry with +0 := 0,
 *                         +4 := the row offset, +12 := 0x1c and +13 := 0x44 +
 *                         n (0x46 for n = 2, 0x47 for n = 3); header +8 :=
 *                         n + 4, +12 := the new FixedData size
 *   Rsc Fixed2Data  +40   {fresh identity A, f64 n + 1, A}
 *   Rsc Fixed2Meta  +51   a clone of the last live row's entry with +4 := the
 *                         record offset and +8 := 0x2b for n = 2, 0x0b for
 *                         n = 3; header +8 := n + 4, +12 := the new size
 *   Rsc Var2Data    +54   the blocks in THIS order: Created (4 B packed date,
 *                         0x0c4002d6), the 16 zero bytes (0x0c4002e3), Name
 *                         (UTF-16LE + NUL, 0x0c400001), Initials (0x0c400002),
 *                         and the small integer 2 (0x0c400055)
 *   Rsc VarMeta     +60   five entries {uid, offset, key} in ASCENDING key
 *                         order (01, 02, 55, d6, e3); header +8 += 5, +0x14 :=
 *                         the new Var2Data size
 *   Cal FixedData   +12   {1, the resource uid, the calendar uid}, the
 *                         calendar uid being the largest calendar uid + 1
 *   Cal FixedMeta   +10   {+4 the old FixedData size, +8 0x000e}; header +8
 *                         += 1, +12 := the new size
 *   Cal Fixed2Data  +48   {A, A, C}: A the fresh identity, C the shared
 *                         save-time identity the last calendar slot carries in
 *                         its third place
 *   Cal Fixed2Meta  +10   {+4 the old Fixed2Data size, +8 0x000e}; header +8
 *                         += 1, +12 := the new size
 *   114/Props             Resource count 0x02000001 := n + 4, Resource size
 *                         0x02000100 := the new Rsc Var2Data size, Calendar
 *                         count 0x03000001 := the new Cal FixedMeta count
 *   Props14               the edit marker (every measured editor save)
 *
 * THE COMMAND IS ENABLED ONLY FOR THESE SHAPES.  The two measured counts, n = 2
 * and n = 3, are the only ones accepted: the FixedMeta +13 and Fixed2Meta +8
 * bytes are measured per count and are not decoded, so every other count is
 * refused.  A resource table with a tombstoned row, a template whose MaxUnits is
 * not 100 %, a template group that does not carry the measured 0x55 / 0xe3
 * blocks, a calendar table whose row count is not n + 1, a Props count that
 * disagrees with its table, or a name that is not printable ASCII is refused
 * before `doc` or the output path is touched.  Only a work resource with
 * default rates is written (the brief's W34-4.2 scope).
 *
 * IDENTITIES.  Project renumbers every row identity's first dword on save; the
 * trailing 12 bytes are stable.  This writer keeps C's trailing bytes and
 * derives A's counter (the high half of the first dword) as the greatest
 * counter in that identity family + 1.  The pair tests normalise the appended
 * identities' first dwords to Project's save; every other appended byte is
 * compared.
 *
 * THE CREATED STAMP.  Block 0x0c4002d6 is a packed date (u16 day, u16 ticks of
 * 6 s) that Project takes from the clock.  The caller supplies it; the pair
 * tests pass the stamps the two pairs recorded.
 *
 * ALL-OR-NOTHING AND PROVEN FROM DISK.  Every edit is built on local copies and
 * written through `save_props_edit`, then the file is re-opened and required to
 * hold exactly the bytes built for each owned stream, the new Props scalars and
 * exactly the measured changed-stream set; on success `doc` is replaced by the
 * re-read document.  Every refusal happens before `doc` or the output path is
 * touched.  A proof mismatch after the save leaves the written file at `path`
 * (the same behaviour as save_props_edit) but never replaces `doc`.
 */

#ifndef MPPWRITE_RESOURCE_GROWTH_H
#define MPPWRITE_RESOURCE_GROWTH_H

#include <cstddef>
#include <cstdint>
#include <string>

#include "cfb_writer.h"

namespace mppwrite {

struct ResourceGrowthSpec {
    /* Required: 1..64 printable ASCII characters. */
    std::string name;
    /* Empty = the first character of `name`; otherwise 1..8 printable ASCII. */
    std::string initials;
    /* The Created stamp (packed date), non-zero. */
    std::uint32_t created = 0;
    /* W34-4.2: build and validate every stream exactly as a write does, then
     * return what a write would report without touching `doc` or any file
     * (`path` is not examined).  The app's edit builder uses it to refuse an
     * unmeasured shape when the command is invoked, before anything is queued. */
    bool dry_run = false;
};

/* What the write proved from the file on disk. */
struct ResourceGrowthWrite {
    std::uint32_t uid = 0;
    std::uint32_t id = 0;
    std::uint32_t calendar_uid = 0;
    std::size_t rows_before = 0;
    std::size_t rows_after = 0;
    std::uint32_t resource_count_before = 0;
    std::uint32_t resource_count_after = 0;
    std::uint32_t resource_size_before = 0;
    std::uint32_t resource_size_after = 0;
    std::uint32_t calendar_count_before = 0;
    std::uint32_t calendar_count_after = 0;
};

/*
 * Add one work resource with default rates and its calendar row, and save the
 * package to `path`.  Throws Error on a refusal (the document and `path` are
 * untouched).
 */
ResourceGrowthWrite add_work_resource(CfbDocument& doc,
                                      ResourceGrowthSpec const& spec,
                                      std::string const& path);

} /* namespace mppwrite */

#endif /* MPPWRITE_RESOURCE_GROWTH_H */
