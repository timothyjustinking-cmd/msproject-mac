/*
 * mspdi_writer.h -- MSPDI (Project XML) export.
 *
 * This is the interchange path that works before the .mpp writer can carry the
 * full model: an MspdiDocument goes back out as the XML subset that
 * core::io::import_mspdi reads, so a project can leave this code and be opened
 * in MS Project.
 *
 * WHAT IS WRITTEN
 *   The project header (MspdiProject), then <Tasks>, <Resources> and
 *   <Assignments> in that order.  Each record is <UID>, <ID>, the mapped
 *   values in a fixed element order, then <PredecessorLink> children under the
 *   successor task.  Only mapped elements are emitted: whatever the importer
 *   dropped (DurationFormat, EmailAddress, calendars, timephased data, ...) is
 *   not in the model, so it cannot be written back.  Writer output re-imports
 *   with <em>no warnings</em> -- that is asserted by the tests.
 *
 * VALUE RULES
 *   Null and empty Text values emit no element at all (never <x/>), which is
 *   consistent with the importer, where <x/> and an absent element are both
 *   Null.  A zero integer, a false flag or a zero duration is a real value and
 *   is emitted.  Durations become ISO-8601 "PT#H#M#S" from 6-second ticks, so
 *   a 480-minute day is "PT8H0M0S".  The one exception is a link's <LinkLag>:
 *   it is written as a decimal count of tenths of a minute ("4800"), the form
 *   Project's own export uses (mspdi.h, "LinkLag spelling"); the ISO form is
 *   still accepted on import for the committed hand-written sample.  Dates
 *   become "YYYY-MM-DDTHH:MM:SS" with no timezone, matching the importer's
 *   civil-time model.  Text is UTF-8 and is escaped for &, <, >, " and '.
 *
 * INFERRED, NOT ORACLE-VERIFIED (see spec/mspdi.md section 8)
 *   * The element order inside a record follows the committed sample files;
 *     it is not a validated MSPDI schema sequence.  That MS Project's importer
 *     accepts these documents is an inference -- no oracle round-trip was run
 *     (the oracle is coordinator-only).
 *   * <ID> is written from the model, and the row's 1-based ordinal when the
 *     model has no ID, because Project's exports always carry it.  ID is a
 *     display sequence number, not project data.
 *   * The three containers are always present; an empty one is written as
 *     <Tasks/> rather than omitted.
 *
 * LIMITS
 *   * The importer trims direct text, so a model value with leading or
 *     trailing whitespace re-imports trimmed.  Documents produced by
 *     import_mspdi never contain such values, so round-trip is exact for them.
 *   * Values MSPDI cannot represent are omitted: non-finite numbers (NaN/inf)
 *     and dates outside the years 1..9999.
 *   * An assignment row with no MspdiAssignmentRef cannot name its task and
 *     resource and is skipped; a link whose successor UID is not a task row
 *     has nowhere to go and is skipped.  Everything else that is in the model
 *     and mapped is written.
 *   * Program name/guid/calendar definitions and extended attributes are not
 *     synthesised: the model does not carry them.
 */

#ifndef CORE_IO_MSPDI_WRITER_H
#define CORE_IO_MSPDI_WRITER_H

#include <string>

#include "mspdi.h"

namespace core::io::mspdi {

/* Serialises the whole document.  Does not fail: unrepresentable values and
 * records are omitted as described above. */
std::string write(const MspdiDocument& document);

/* write(document) saved to path (binary, LF line endings).  Throws MspdiError
 * when the file cannot be opened or written. */
void write_file(const MspdiDocument& document, const std::string& path);

}  // namespace core::io::mspdi

#endif  // CORE_IO_MSPDI_WRITER_H
