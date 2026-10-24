/*
 * project_props.h -- Project's stored project-level options (wave 28, W28-3.1).
 *
 * MEASURED SOURCE.  tests/data/wave28-newtask-*.mpp are four Project-2021 save
 * pairs, both directions of File > Options > Schedule > "New tasks created":
 *
 *   manual_to_auto  wave28-newtask-manual-control.mpp  wave28-newtask-auto.mpp
 *   auto_to_manual  wave28-newtask-auto-control.mpp    wave28-newtask-manual.mpp
 *
 * Each control and its edited file came from ONE continuous Project session,
 * and every file carries neutral metadata.  build/w28/W28-3.1-pair1-diff.log
 * and -pair2-diff.log (not committed) hold the byte diffs: the ONLY owned
 * change is 114/Props record 0x024013c8 (kind 2, 2-byte payload) at stream
 * offset 2986 (payload 2998):
 *
 *   Manually Scheduled   payload bytes ff 00   (0x00FF)
 *   Auto Scheduled       payload bytes 00 00   (0x0000)
 *
 * The base fixture synthetic-base.mpp stores 0x0001 for its Manually Scheduled
 * default, and core/mpp reads any non-zero payload as manual.  Project
 * NORMALISES the value to 0x00FF on its save, so this writer stores Project's
 * bytes, not the base fixture's, and the save pair matches field for field.
 * Project's COM reopen of each committed file reads
 * Project.NewTasksCreatedAsManual back as the value the file was saved with.
 *
 * The other stream differences in the pairs are Project's per-save caches
 * (SummaryInformation, opaque Props/Fixed2Data identity bytes, CEdl) and are
 * not written here -- the same exclusion the W28-1.2 actuals writer documents;
 * the save-pair test asserts every stream outside Props stays equal to the
 * control and that inside Props only the owned record and Project's known
 * per-save churn differ.
 */

#ifndef MPPWRITE_PROJECT_PROPS_H
#define MPPWRITE_PROJECT_PROPS_H

#include <cstdint>
#include <string_view>

#include "cfb_writer.h"

namespace mppwrite {

/* Store the project's new-task scheduling mode in 114/Props record
 * 0x024013c8 (kind 2, 2-byte payload): 0x00FF for Manually Scheduled, 0x0000
 * for Auto Scheduled -- the exact u16 Project 2021 writes (measured above).
 * Returns the previous raw u16 so the caller can report it.  Throws Error when
 * the stream or the record is absent or has a different shape: a file without
 * the record (an original Project 2007 MPP12 file) is a file this writer has
 * not measured, and inventing a record would break the save pair.  The stream
 * length is unchanged; only the record's two payload bytes move.
 */
std::uint16_t write_new_task_mode(CfbDocument& doc, bool manual,
                                  std::string_view root = "114");

}  // namespace mppwrite

#endif  // MPPWRITE_PROJECT_PROPS_H
