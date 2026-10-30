/*
 * assignment_growth.h -- W34-3.1: the measured assignment ADD and REMOVE
 * shapes, as Project 2021 writes them.
 *
 * WHY THIS FILE EXISTS.  `add_delete.h` can append an assignment row
 * (`add_assignment_row`, W54), and its row-shape builder was measured on
 * `w12-assignadd`: it leaves every existing row and FixedMeta entry
 * byte-identical and appends the new row.  Project's OWN edit save does not
 * do that.  The pairs behind `spec/writers/table-growth.md`, "W34-3.1", show
 * TWO different shapes.  The FIRST assignment on a task (`wave30-assign-add`,
 * `wave30-assign-delete`) tombstones the task's placeholder row, appends a
 * new row, rewrites the task's Work roll-up in the Task and Resource tables
 * and moves the assignment's var group.  A SECOND or THIRD assignment
 * (`wave34-assign-add-second`, `wave34-assign-add-third`) appends a row
 * beside the real rows and leaves every old row, record and var block
 * untouched.  A port that only appends a row cannot reproduce either file,
 * so this writer encodes both measured shapes.
 *
 * THE MEASURED RULES (every stream, offset and byte in
 * spec/writers/table-growth.md; both pairs have a committed null save, so the
 * pair tests mask per-save noise and compare every edit effect):
 *
 * ADD, FIRST ASSIGNMENT (`assign_resource`), measured on
 * `wave30-assign-add`:
 *   Assn FixedData  +110  append a copy of the target task's placeholder row
 *                         with +0 = largest uid + 1, +8 = the resource uid
 *   Assn FixedMeta  +34   the placeholder's entry +0 := 0x02, +2 := 0; append
 *                         {+2 4, +4 old rows*110, tail f1 ff 23 f0 69 00 02};
 *                         header +4 := 6, +8 := old rows+1, +12 := new size
 *   Assn Fixed2Data +48   {fresh identity, task Fixed2Data identity, resource
 *                         Fixed2Data identity}
 *   Assn Fixed2Meta +53   append {+4 old rows*48, +8 0xcf, +9 0x09,
 *                         +35 0x10, +52 0x28}; header +8 +1, +12 new size
 *   Assn VarMeta    +12   remove the placeholder uid group (0F400031,
 *                         0F40027A, 0F400299) and append the new uid's four
 *                         entries in ascending key order, blocks at
 *                         base, base+8, base+84 and base+104
 *   Assn Var2Data   +124  0F40027A (session word), 0F400031 (flat contour,
 *                         72 B), 0F400299 and 0F40029B (16 zero bytes each)
 *   Task FixedData         project row and target task row +8/+24 := work
 *   Task FixedMeta         project entry +16 |= 0x10, target +16 |= 0x90
 *   Task Var2Data          0B4004E5 leaf [1][0xFFFF0001] -> [1][resource]
 *   Rsc FixedData          target row +48/+90/+106 := work
 *   Rsc FixedMeta          row 0's entry +13 := 0x00
 *   114/Props              count 3->4, freed + the removed payload (120),
 *                          dirty 0->1, size 396->520
 *   DocumentSummaryInformation  work strings "0 hrs" -> "40 hrs"
 *
 * ADD, SETTLED APPEND (`assign_resource`), measured on
 * `wave34-assign-add-second` (count 1 -> 2) and `wave34-assign-add-third`
 * (count 2 -> 3); the task holds its real rows plus one free placeholder:
 *   Assn FixedData  +110  append a copy of a real row with +0 = largest uid +
 *                         1, +8 = the resource uid; every old row is kept
 *   Assn FixedMeta  +34   append {+2 4, +4 old rows*110, tail f1 ff 23 f0 69 00
 *                         02}; header +8 +1, +12 new size; +4 untouched
 *   Assn Fixed2Data +48   {fresh identity, task Fixed2Data identity, resource
 *                         Fixed2Data identity}; every old record is kept
 *   Assn Fixed2Meta +53   append {+4 old rows*48, +8 0xcf, +9 0x09, +35 0x10,
 *                         +52 0x28}; header +8 +1, +12 new size
 *   Assn VarMeta    +48   keep every entry; append the new uid's four entries
 *                         in ascending key order at base+8, base, base+84 and
 *                         base+104; header +8 +4, +0x14 := base + 124
 *   Assn Var2Data   +124  the same four blocks as the first assignment's
 *                         (session word, 72-B contour, two 16-byte zeros)
 *   Task FixedData         project row and target row +8/+24 := old total +
 *                         the new row's work
 *   Task FixedMeta         byte-identical (the flags are already 0x17/0x97)
 *   Task VarMeta           the leaf entry's offset := old Task Var2Data size;
 *                         +0x14 := old size + 8 + 4 * new resource count
 *   Task Var2Data          append {4+4*n, n, sorted resource uids}; the old
 *                         leaf record stays behind as a stale copy
 *   Rsc FixedData          target row +48/+90/+106 := the new row's work
 *   Rsc FixedMeta          byte-identical
 *   114/Props              count := new rows, size := old + 124, dirty 0->1
 *                         only if it was 1, the Task size scalar := the new
 *                         Task Var2Data size
 *   DocumentSummaryInformation  the total work strings ("40 hrs" -> "80 hrs",
 *                         "80 hrs" -> "120 hrs")
 *
 * REMOVE (`unassign_resource`), measured on `wave30-assign-delete`, which is
 * the settled shape (the assignment already exists; every earlier placeholder
 * row is unassigned; FixedMeta/Fixed2Meta carry rows-1 real entries and a
 * byte-duplicate spare):
 *   Assn FixedData  +110  append a copy of the real last row with +0 =
 *                         largest uid + 1, +8 = 0xFFFF0001
 *   Assn FixedMeta   0    the real row's entry +0 := 0x02, +2 := 0; the spare
 *                         entry +4 := new row offset, +13 := 0x01; header
 *                         +4 := 6, +8 := old rows, +12 := new size
 *   Assn Fixed2Data +48   {fresh identity, task identity, the placeholder
 *                         rows' null-resource identity}
 *   Assn Fixed2Meta  0    the spare entry +4 := new record offset, +9 := 0x01;
 *                         header +8 := old rows, +12 := new size
 *   Assn VarMeta     0    re-key the real group to the new uid and move its
 *                         four offsets +124, entries in ascending key order;
 *                         header +0x14 := old size + 124
 *   Assn Var2Data   +124  the real group's 124 bytes with the 28-byte contour
 *                         record (group +56) zeroed; block headers kept
 *   Task FixedData         project row and target task row +8/+24 := 0
 *   Task FixedMeta         target entry +16: 0x97 -> 0x87 (project keeps 0x17)
 *   Task Var2Data          leaf [1][resource] -> [1][0xFFFF0001]
 *   Rsc FixedData          target row +48/+90/+106 := 0
 *   114/Props              count 3->4, freed + the copied payload (108),
 *                          dirty 0->1, size 520->644
 *   DocumentSummaryInformation  "40 hrs" -> "0 hrs"
 *
 * THE COMMAND IS ENABLED ONLY FOR THESE SHAPES.  `assign_resource` accepts
 * the first-assignment shape (every assignment row an unassigned placeholder,
 * resource 0xFFFF0001, 100% units, whole-hour work, exactly one of them naming
 * the requested task, the measured meta/var geometry and the task/resource
 * roll-up words at zero) and the settled append shape (the task's real rows
 * plus exactly one free placeholder row, each row's meta entries matching its
 * kind, the task leaf naming the real rows in ascending resource order, the
 * roll-ups at the old total and the resource's work at zero).  The settled
 * counts are 1 and 2 real rows; a third real assignment and every other shape
 * are refused -- there is no measured file for count 3 -> 4.  `unassign_resource`
 * requires the settled removal shape above (one real assignment, last row,
 * every other row an unassigned placeholder, matching spare entries, the
 * group's blocks at the end of Var2Data).  Every other shape is refused
 * before `doc` or the output path is touched.
 *
 * IDENTITY COUNTERS.  Project renumbers every row identity's first dword on
 * save; the trailing 12 bytes are stable.  This writer keeps the control's
 * trailing bytes and derives the new record's first dword as the numerically
 * greatest assignment identity + 1 (bounded until it collides with nothing).
 * The pair tests normalise the appended record's three counter dwords to
 * Project's save; every other byte of the record is compared.
 *
 * ALL-OR-NOTHING AND PROVEN FROM DISK.  All edits are built on local copies.
 * The streams are replaced on a copy of the document, the summary is rebuilt
 * from the edited project row, and the package is written through
 * `save_props_edit` (which also writes the Props14 edit marker and proves the
 * Props edit).  Then the file is re-opened and required to hold exactly the
 * bytes built for every stream this writer owns, and exactly the measured
 * changed-stream set; on success `doc` is replaced by the re-read document.
 * Every refusal happens before `doc` or the output path is touched.  A proof
 * mismatch after the save leaves the written file at `path` (the same
 * behaviour as save_props_edit) but never replaces `doc`.
 */

#ifndef MPPWRITE_ASSIGNMENT_GROWTH_H
#define MPPWRITE_ASSIGNMENT_GROWTH_H

#include <cstddef>
#include <cstdint>
#include <string>

#include "cfb_writer.h"

namespace mppwrite {

/* The row identity the assignment and resource tables use. */
inline constexpr std::uint32_t kPlaceholderResourceUid = 0xFFFF0001u;

/* Which assignment the caller means: one task and one resource, both by
 * UniqueID.  No name or title is ever an input. */
struct AssignmentKey {
    std::uint32_t task_uid = 0;
    std::uint32_t resource_uid = 0;
};

/* What the write proved from the file on disk. */
struct AssignmentGrowthWrite {
    std::uint32_t task_uid = 0;
    std::uint32_t resource_uid = 0;
    /* The appended row's UniqueID (largest old assignment uid + 1). */
    std::uint32_t assignment_uid = 0;
    /* Add, first assignment: the placeholder uid that was tombstoned.
     * Add, settled append: 0 (nothing is tombstoned).
     * Remove: the real assignment uid that was tombstoned. */
    std::uint32_t tombstoned_uid = 0;
    std::size_t rows_before = 0;
    std::size_t rows_after = 0;
    /* The assignment's work in 60000-per-hour units (FixedData +20). */
    double work = 0.0;
    /* work / 100, the task tick count the contour and summary use. */
    std::uint32_t work_ticks = 0;
    std::size_t var2_bytes_before = 0;
    std::size_t var2_bytes_after = 0;
    std::size_t var_entries_before = 0;
    std::size_t var_entries_after = 0;
    std::uint32_t props_count_before = 0;
    std::uint32_t props_count_after = 0;
    std::uint32_t props_freed_before = 0;
    std::uint32_t props_freed_after = 0;
    std::uint32_t props_dirty_before = 0;
    std::uint32_t props_dirty_after = 0;
    std::uint32_t props_size_before = 0;
    std::uint32_t props_size_after = 0;
};

/*
 * Assign `key.resource_uid` to `key.task_uid` and save the package to `path`:
 * the measured first-assignment shape or the measured settled append shape.
 * Throws Error on a refusal (the document and `path` are untouched).
 */
AssignmentGrowthWrite assign_resource(CfbDocument& doc,
                                      AssignmentKey const& key,
                                      std::string const& path);

/*
 * Remove the one real assignment (task, resource) and save to `path`.  The
 * measured removal shape only: the assignment is the last row and the only
 * real row.  Throws Error on a refusal.
 */
AssignmentGrowthWrite unassign_resource(CfbDocument& doc,
                                        AssignmentKey const& key,
                                        std::string const& path);

} /* namespace mppwrite */

#endif /* MPPWRITE_ASSIGNMENT_GROWTH_H */
