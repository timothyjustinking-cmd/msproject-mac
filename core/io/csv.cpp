#include "csv.h"

#include <ostream>
#include <string>

namespace core::io {
namespace {

std::vector<std::size_t> all_rows(const core::view::RecordSet& records) {
    std::vector<std::size_t> order(records.row_count());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    return order;
}

void write_row(std::ostream& out, const core::view::RecordSet& records,
               const core::view::TableDefinition& table, std::size_t row,
               const CsvOptions& options) {
    for (std::size_t column = 0; column < table.columns.size(); ++column) {
        if (column != 0) {
            out.put(options.delimiter);
        }
        const core::view::Value* value = records.get(row, table.columns[column].field);
        out << csv_field(csv_cell(value, options), options);
    }
    out << (options.crlf ? "\r\n" : "\n");
}

}  // namespace

std::string csv_field(std::string_view text, const CsvOptions& options) {
    const bool needs_quotes =
        text.find(options.delimiter) != std::string_view::npos ||
        text.find(options.quote) != std::string_view::npos ||
        text.find('\r') != std::string_view::npos ||
        text.find('\n') != std::string_view::npos;
    if (!needs_quotes) {
        return std::string(text);
    }
    std::string escaped;
    escaped.reserve(text.size() + 2);
    escaped.push_back(options.quote);
    for (const char c : text) {
        if (c == options.quote) {
            escaped.push_back(options.quote);
        }
        escaped.push_back(c);
    }
    escaped.push_back(options.quote);
    return escaped;
}

std::string csv_cell(const core::view::Value* value, const CsvOptions& options) {
    if (value == nullptr || value->is_null()) {
        return {};
    }
    if (value->kind == core::view::ValueKind::Bool && options.bool_as_yes_no) {
        return value->boolean ? "Yes" : "No";
    }
    return core::view::format_value(*value, options.duration_style);
}

std::string export_csv(const core::view::RecordSet& records,
                       const core::view::TableDefinition& table,
                       const CsvOptions& options) {
    return export_csv_rows(records, table, all_rows(records), options);
}

std::string export_csv(const core::view::RecordSet& records,
                       const core::view::TableDefinition& table,
                       const core::view::FilterDefinition& filter,
                       const CsvOptions& options) {
    return export_csv_rows(records, table, core::view::matching_rows(filter, records),
                           options);
}

std::string export_csv_rows(const core::view::RecordSet& records,
                            const core::view::TableDefinition& table,
                            const std::vector<std::size_t>& order,
                            const CsvOptions& options) {
    std::string output;
    /* One line per row plus one header; reserve a plausible amount to avoid the
     * repeated growth path without guessing per-field widths. */
    output.reserve((order.size() + 1) * (table.columns.size() * 12 + 2));
    if (options.header) {
        for (std::size_t column = 0; column < table.columns.size(); ++column) {
            if (column != 0) {
                output.push_back(options.delimiter);
            }
            output += csv_field(table.columns[column].display_title(table.record_type), options);
        }
        output += options.crlf ? "\r\n" : "\n";
    }
    for (const std::size_t row : order) {
        if (row >= records.row_count()) {
            continue;
        }
        for (std::size_t column = 0; column < table.columns.size(); ++column) {
            if (column != 0) {
                output.push_back(options.delimiter);
            }
            const core::view::Value* value = records.get(row, table.columns[column].field);
            output += csv_field(csv_cell(value, options), options);
        }
        output += options.crlf ? "\r\n" : "\n";
    }
    return output;
}

bool write_csv(std::ostream& out, const core::view::RecordSet& records,
               const core::view::TableDefinition& table,
               const std::vector<std::size_t>& order, const CsvOptions& options) {
    if (options.header) {
        for (std::size_t column = 0; column < table.columns.size(); ++column) {
            if (column != 0) {
                out.put(options.delimiter);
            }
            out << csv_field(table.columns[column].display_title(table.record_type), options);
        }
        out << (options.crlf ? "\r\n" : "\n");
    }
    for (const std::size_t row : order) {
        if (row >= records.row_count()) {
            continue;
        }
        write_row(out, records, table, row, options);
    }
    return static_cast<bool>(out);
}

}  // namespace core::io
