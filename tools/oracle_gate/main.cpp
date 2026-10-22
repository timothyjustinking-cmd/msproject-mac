// tools/oracle_gate -- write our own 214/CTable append + a formula into a real
// .mpp, so the coordinator can have MS Project open and re-save the result.
//
// This exists because Project's automation surface cannot append a table column
// (docs/handoff/w62.md §3): the only way to prove Project accepts our bytes is
// to write them ourselves and let Project re-save the file, then decode the
// re-saved 214/CTable.
//
//   oracle_gate <in.mpp> <out.mpp>
//
// Exit 0 and a summary on success; non-zero with a message on any refusal.

#include "cfb_writer.h"
#include "props.h"
#include "viewstate.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

std::string find_stream(mppwrite::CfbDocument const& doc,
                        std::string_view suffix) {
    for (std::string const& path : doc.stored_stream_paths()) {
        if (path.size() >= suffix.size() &&
            path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0) {
            return path;
        }
    }
    return {};
}

}  // namespace

int main(int argc, char** argv) {
    using mppwrite::CfbDocument;
    if (argc != 3) {
        std::fprintf(stderr, "usage: oracle_gate <in.mpp> <out.mpp>\n");
        return 2;
    }
    try {
        CfbDocument doc = CfbDocument::from_file(argv[1]);

        std::string const fd = find_stream(doc, "214/CTable/FixedData");
        std::string const fm = find_stream(doc, "214/CTable/FixedMeta");
        std::string const vm = find_stream(doc, "214/CTable/VarMeta");
        std::string const v2 = find_stream(doc, "214/CTable/Var2Data");
        if (fd.empty() || fm.empty() || vm.empty() || v2.empty()) {
            std::fprintf(stderr, "error: no 214/CTable streams in %s\n", argv[1]);
            return 1;
        }

        std::vector<std::uint8_t> const fixed_data = doc.read_stream(fd);
        std::vector<std::uint8_t> const fixed_meta = doc.read_stream(fm);
        std::vector<std::uint8_t> const var_meta = doc.read_stream(vm);
        std::vector<std::uint8_t> const var2_data = doc.read_stream(v2);
        msproject::viewstate::PartSet parts{fixed_data, fixed_meta, var_meta,
                                            var2_data};
        msproject::viewstate::ViewState state;
        msproject::viewstate::ParseError parse_error;
        if (!msproject::viewstate::parse_view_state("CTable", parts, state,
                                                    &parse_error)) {
            std::fprintf(stderr, "error: parse_view_state: %s at %zu\n",
                         parse_error.message.c_str(), parse_error.offset);
            return 1;
        }
        std::size_t const columns_before = state.tables.empty()
                                                ? 0
                                                : state.tables.front().columns.size();

        /* Number1 (0x0b400057) appended after the last Entry column. */
        msproject::viewstate::WriteError write_error;
        if (!msproject::viewstate::append_table_column(state, 0, 0x0b400057u,
                                                       "Number1", &write_error)) {
            std::fprintf(stderr, "error: append: %s\n", write_error.message.c_str());
            return 1;
        }
        msproject::viewstate::EncodedParts encoded;
        if (!msproject::viewstate::encode_view_state(state, encoded, &write_error)) {
            std::fprintf(stderr, "error: encode: %s\n", write_error.message.c_str());
            return 1;
        }
        /* Re-parse our own output before writing: the gate must not hand Project a
         * stream we cannot read back. */
        {
            msproject::viewstate::ViewState check;
            msproject::viewstate::ParseError err2;
            if (!msproject::viewstate::parse_view_state("CTable", encoded.parts(),
                                                        check, &err2)) {
                std::fprintf(stderr, "error: re-parse: %s\n", err2.message.c_str());
                return 1;
            }
            if (check.tables.empty() ||
                check.tables.front().columns.size() != columns_before + 1) {
                std::fprintf(stderr, "error: re-parse column count\n");
                return 1;
            }
        }

        doc.replace_stream(fd, encoded.fixed_data);
        doc.replace_stream(fm, encoded.fixed_meta);
        doc.replace_stream(vm, encoded.var_meta);
        doc.replace_stream(v2, encoded.var2_data);

        /* A formula in Text2 (0x0b400036): [Name]. */
        std::string const props_path = find_stream(doc, "114/Props");
        if (props_path.empty()) {
            std::fprintf(stderr, "error: no 114/Props\n");
            return 1;
        }
        std::vector<std::uint8_t> props_out;
        std::string props_error;
        if (!msproject::props::set_custom_field_formula(
                doc.read_stream(props_path), 0x0B400036u, "[Name]", props_out,
                &props_error)) {
            std::fprintf(stderr, "error: formula: %s\n", props_error.c_str());
            return 1;
        }
        doc.replace_stream(props_path, props_out);

        doc.save(argv[2]);
        std::printf("wrote %s\n", argv[2]);
        std::printf("  214/CTable columns: %zu -> %zu (appended 0x0b400057 \"Number1\")\n",
                    columns_before, columns_before + 1);
        std::printf("  114/Props formula: 0x0b400036 = [Name]\n");
        std::printf("  streams replaced: %s, %s, %s, %s, %s\n", fd.c_str(),
                    fm.c_str(), vm.c_str(), v2.c_str(), props_path.c_str());
        return 0;
    } catch (std::exception const& exc) {
        std::fprintf(stderr, "error: %s\n", exc.what());
        return 1;
    }
}
