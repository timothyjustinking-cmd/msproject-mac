/*
 * csv.h -- export a RecordSet through a TableDefinition.
 *
 * The point of the table being data is exactly this: CSV export is the table's
 * column order applied to a record set, with no per-export column logic.  The
 * same TableDefinition drives the sheet, so an exported file and an on-screen
 * sheet cannot disagree about which fields, in which order, are shown.
 *
 * FORMATTING is explicit and stable, because no oracle capture of Project's own
 * CSV output exists yet:
 *   text      verbatim
 *   number    shortest round-trip decimal
 *   integer   decimal
 *   bool      "1"/"0" (set bool_as_yes_no for "Yes"/"No")
 *   date      "YYYY-MM-DD" or "YYYY-MM-DD HH:MM[:SS]"
 *   duration  ISO-8601 by default (lossless); DurationStyle::Days for "2d"
 *   null      empty field
 *
 * Quoting is RFC 4180: a field is quoted when it contains the delimiter, the
 * quote character, CR or LF; embedded quotes are doubled.  Lines end CRLF by
 * default (what Project writes for CSV) and LF when crlf is false.
 */

#ifndef CORE_IO_CSV_H
#define CORE_IO_CSV_H

#include <cstddef>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

#include "filter.h"
#include "record_set.h"
#include "table.h"
#include "value.h"

namespace core::io {

struct CsvOptions {
    bool header = true;
    bool crlf = true;
    char delimiter = ',';
    char quote = '"';
    core::view::DurationStyle duration_style = core::view::DurationStyle::Iso8601;
    bool bool_as_yes_no = false;
};

/* Escapes one field; exposed for tests. */
std::string csv_field(std::string_view text, const CsvOptions& options);

/* Rendering of one cell, before escaping. */
std::string csv_cell(const core::view::Value* value, const CsvOptions& options);

/* All rows, file order. */
std::string export_csv(const core::view::RecordSet& records,
                       const core::view::TableDefinition& table,
                       const CsvOptions& options = CsvOptions{});

/* Rows accepted by `filter`, file order. */
std::string export_csv(const core::view::RecordSet& records,
                       const core::view::TableDefinition& table,
                       const core::view::FilterDefinition& filter,
                       const CsvOptions& options = CsvOptions{});

/* Rows in the supplied order (e.g. sort/group output).  Out-of-range indices
 * are skipped rather than producing a phantom row. */
std::string export_csv_rows(const core::view::RecordSet& records,
                            const core::view::TableDefinition& table,
                            const std::vector<std::size_t>& order,
                            const CsvOptions& options = CsvOptions{});

/* Streams the same output; returns false when the stream fails. */
bool write_csv(std::ostream& out, const core::view::RecordSet& records,
               const core::view::TableDefinition& table,
               const std::vector<std::size_t>& order, const CsvOptions& options);

}  // namespace core::io

#endif  // CORE_IO_CSV_H
