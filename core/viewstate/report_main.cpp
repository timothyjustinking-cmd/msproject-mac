// viewstate_report -- dump the `214/` view-state tree of an extracted .mpp.
//
// The argument is a directory of already-extracted streams, not an .mpp file.
// Extract them with olefile or scripts/mpp_dump.py, preserving the stream path:
//
//   <root>/214/CTable/FixedData
//   <root>/214/CTable/FixedMeta
//   <root>/214/CTable/VarMeta
//   <root>/214/CTable/Var2Data
//   <root>/214/CV_iew/...            (same four parts)
//   <root>/214/Props
//
// <root>/214 is used when it exists; otherwise <root> itself is treated as the
// 214 root.  A ".bin" suffix on a part file is accepted (FixedData.bin).
//
// This tool reads by default; --roundtrip additionally re-encodes each class
// with encode_view_state() and checks the result against the extracted
// streams, which is the writer's no-op proof on real fixture bytes.  See
// docs/handoff/w58.md for the writer's measured scope.
//
// --regenerate-edl parses 214/CTable, 214/CV_iew and 214/CEdl from one tree,
// links them and runs regenerate_edl() with no table edits.  It prints the
// derived CEdl records before and after and the byte delta.  It never writes a
// file: on a tree Project re-saved the run is a no-op, while on a tree whose
// CEdl is stale relative to its CTable it reports the records it would refresh.

#include "viewstate.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <map>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

std::uint64_t fnv1a64(const std::vector<std::uint8_t>& bytes) {
    std::uint64_t hash = 0xcbf29ce484222325ull;
    for (const std::uint8_t byte : bytes) {
        hash ^= byte;
        hash *= 0x100000001b3ull;
    }
    return hash;
}

bool read_file(const fs::path& path, std::vector<std::uint8_t>& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    file.seekg(0, std::ios::end);
    const std::streamoff length = file.tellg();
    file.seekg(0, std::ios::beg);
    if (length < 0) {
        return false;
    }
    out.assign(static_cast<std::size_t>(length), 0);
    if (!out.empty()) {
        file.read(reinterpret_cast<char*>(out.data()), length);
    }
    return static_cast<bool>(file);
}

std::string part_name(const fs::path& path) {
    std::string name = path.filename().string();
    const std::string suffix = ".bin";
    if (name.size() > suffix.size() &&
        name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
        name.resize(name.size() - suffix.size());
    }
    return name;
}

int g_exit = 0;

bool load_class(const std::map<std::string, std::map<std::string, fs::path>>& classes,
                const std::string& klass,
                std::array<std::vector<std::uint8_t>, 4>& storage,
                msproject::viewstate::PartSet& parts,
                std::string& error) {
    const auto found = classes.find(klass);
    if (found == classes.end()) {
        error = "214/" + klass + " is missing";
        return false;
    }
    static constexpr std::array<const char*, 4> kNames = {"FixedData", "FixedMeta", "VarMeta",
                                                          "Var2Data"};
    std::array<std::vector<std::uint8_t>*, 4> targets = {&storage[0], &storage[1], &storage[2],
                                                          &storage[3]};
    for (std::size_t i = 0; i < kNames.size(); ++i) {
        const auto part = found->second.find(kNames[i]);
        if (part == found->second.end()) {
            error = "214/" + klass + " is missing part " + kNames[i];
            return false;
        }
        if (!read_file(part->second, *targets[i])) {
            error = "cannot read " + part->second.string();
            return false;
        }
    }
    parts.fixed_data = storage[0];
    parts.fixed_meta = storage[1];
    parts.var_meta = storage[2];
    parts.var2_data = storage[3];
    return true;
}

std::string derived_records(const msproject::viewstate::ViewState& state) {
    std::ostringstream out;
    for (const auto& embedded : state.embedded_props) {
        out << "    props block=" << embedded.block_offset << "+" << embedded.payload_length
            << " records=" << embedded.document.records.size();
        if (embedded.view_name_decoded) {
            out << " view=\"" << embedded.view_name << "\"";
        }
        if (embedded.table_name_decoded) {
            out << " table=\"" << embedded.table_name << "\"";
        }
        if (embedded.field_list_decoded) {
            out << " fields=" << embedded.field_ids.size()
                << (embedded.field_list_terminated ? "+terminator" : "");
        }
        out << "\n";
    }
    return out.str();
}

void report_props(const std::vector<std::uint8_t>& bytes) {
    msproject::props::PropsDocument document;
    msproject::props::ParseError error;
    if (!msproject::props::parse_props(bytes, document, &error)) {
        std::fprintf(stderr, "214/Props: parse error: %s at %zu\n", error.message.c_str(),
                     error.offset);
        g_exit = 1;
        return;
    }
    std::printf("214/Props: %u bytes, records=%zu tail=%u format=0x%08x declared=%u\n",
                document.size(), document.records.size(), document.tail_size(),
                document.header.format_version, document.header.declared_entries);
}

}  // namespace

int main(int argc, char** argv) {
    bool dump = false;
    bool inventory = false;
    bool roundtrip = false;
    bool regenerate = false;
    std::string only_class;
    fs::path root;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--dump") {
            dump = true;
        } else if (arg == "--inventory") {
            inventory = true;
        } else if (arg == "--roundtrip") {
            roundtrip = true;
        } else if (arg == "--regenerate-edl") {
            regenerate = true;
        } else if (arg == "--class") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "viewstate_report: --class needs a name\n");
                return 2;
            }
            only_class = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::printf("usage: viewstate_report [--dump] [--inventory] [--roundtrip]\n"
                        "                       [--regenerate-edl] [--class NAME] <root>\n"
                        "  <root>       directory holding 214/<Class>/<Part> files\n"
                        "  --dump       print every column/block (default truncates at 6)\n"
                        "  --inventory  print sizes/FNV only, skip decoding\n"
                        "  --roundtrip  re-encode each class and require byte-identical parts\n"
                        "  --regenerate-edl  link CTable/CV_iew/CEdl, run regenerate_edl()\n"
                        "                    with no table edits and print the byte delta\n"
                        "  --class N    decode one class only\n");
            return 0;
        } else if (root.empty()) {
            root = arg;
        } else {
            std::fprintf(stderr, "viewstate_report: unexpected argument: %s\n", arg.c_str());
            return 2;
        }
    }
    if (root.empty()) {
        std::fprintf(stderr,
                     "usage: viewstate_report [--dump] [--inventory] [--class NAME] <root>\n");
        return 2;
    }

    std::error_code error_code;
    if (!fs::is_directory(root, error_code)) {
        std::fprintf(stderr, "viewstate_report: %s is not a directory\n", root.string().c_str());
        return 1;
    }
    fs::path class_root = root;
    if (fs::is_directory(root / "214", error_code)) {
        class_root = root / "214";
    }

    // Inventory: every regular file below the 214 root, path relative to it.
    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(class_root, error_code)) {
        if (entry.is_regular_file(error_code)) {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        std::fprintf(stderr, "viewstate_report: no streams found under %s\n",
                     class_root.string().c_str());
        return 1;
    }

    std::printf("214 root: %s\n", class_root.string().c_str());
    for (const auto& path : files) {
        std::vector<std::uint8_t> bytes;
        if (!read_file(path, bytes)) {
            std::fprintf(stderr, "viewstate_report: cannot read %s\n", path.string().c_str());
            return 1;
        }
        const fs::path relative = fs::relative(path, class_root, error_code);
        std::printf("  %-32s %7zu bytes  fnv1a64=0x%016llx\n", relative.generic_string().c_str(),
                    bytes.size(), static_cast<unsigned long long>(fnv1a64(bytes)));
    }
    std::printf("streams: %zu\n", files.size());
    if (inventory) {
        return g_exit;
    }

    // Group part files by class directory (a directory directly under the root).
    std::map<std::string, std::map<std::string, fs::path>> classes;
    for (const auto& path : files) {
        const fs::path relative = fs::relative(path, class_root, error_code);
        if (relative.has_parent_path() &&
            relative.parent_path().generic_string().find('/') == std::string::npos) {
            classes[relative.parent_path().generic_string()][part_name(path)] = path;
        }
    }

    if (regenerate) {
        std::array<std::vector<std::uint8_t>, 4> table_bytes;
        std::array<std::vector<std::uint8_t>, 4> view_bytes;
        std::array<std::vector<std::uint8_t>, 4> edl_bytes;
        msproject::viewstate::PartSet table_parts;
        msproject::viewstate::PartSet view_parts;
        msproject::viewstate::PartSet edl_parts;
        std::string load_error;
        if (!load_class(classes, "CTable", table_bytes, table_parts, load_error) ||
            !load_class(classes, "CV_iew", view_bytes, view_parts, load_error) ||
            !load_class(classes, "CEdl", edl_bytes, edl_parts, load_error)) {
            std::fprintf(stderr, "viewstate_report: %s\n", load_error.c_str());
            return 1;
        }
        msproject::viewstate::ViewState table;
        msproject::viewstate::ViewState views;
        msproject::viewstate::ViewState edl;
        msproject::viewstate::ParseError parse_error;
        if (!msproject::viewstate::parse_view_state("CTable", table_parts, table, &parse_error) ||
            !msproject::viewstate::parse_view_state("CV_iew", view_parts, views, &parse_error) ||
            !msproject::viewstate::parse_view_state("CEdl", edl_parts, edl, &parse_error)) {
            std::fprintf(stderr, "viewstate_report: parse error: %s at %zu\n",
                         parse_error.message.c_str(), parse_error.offset);
            return 1;
        }
        table.related = {&views, &edl};
        msproject::viewstate::EncodedParts before;
        msproject::viewstate::EncodedParts after;
        msproject::viewstate::WriteError write_error;
        if (!msproject::viewstate::encode_view_state(edl, before, &write_error)) {
            std::fprintf(stderr, "viewstate_report: CEdl encode failed: %s\n",
                         write_error.message.c_str());
            return 1;
        }
        std::printf("regenerate-edl: CTable tables=%zu CV_iew views=%zu CEdl records=%zu\n",
                    table.tables.size(), views.views.size(),
                    edl.embedded_props.empty() ? 0u : edl.embedded_props[0].document.records.size());
        std::printf("  before:\n%s", derived_records(edl).c_str());
        if (!msproject::viewstate::regenerate_edl(edl, table, &write_error)) {
            std::fprintf(stderr, "viewstate_report: regenerate_edl refused: %s\n",
                         write_error.message.c_str());
            return 1;
        }
        if (!msproject::viewstate::encode_view_state(edl, after, &write_error)) {
            std::fprintf(stderr, "viewstate_report: CEdl re-encode failed: %s\n",
                         write_error.message.c_str());
            return 1;
        }
        std::printf("  after:\n%s", derived_records(edl).c_str());
        std::size_t changed = 0;
        std::size_t first = 0;
        bool have_first = false;
        const std::size_t common = std::min(before.var2_data.size(), after.var2_data.size());
        for (std::size_t i = 0; i < common; ++i) {
            if (before.var2_data[i] != after.var2_data[i]) {
                ++changed;
                if (!have_first) {
                    first = i;
                    have_first = true;
                }
            }
        }
        if (before.var2_data.size() != after.var2_data.size()) {
            std::printf("  var2 size %zu -> %zu (delta %+lld)\n", before.var2_data.size(),
                        after.var2_data.size(),
                        static_cast<long long>(after.var2_data.size()) -
                            static_cast<long long>(before.var2_data.size()));
        }
        if (changed == 0 && before.var2_data.size() == after.var2_data.size()) {
            std::printf("  regenerate_edl is a no-op: CEdl already agrees with CTable/CV_iew\n");
        } else {
            std::printf("  regenerate_edl changed %zu byte(s); first at %zu\n", changed, first);
        }
        return g_exit;
    }

    for (const auto& [klass, parts] : classes) {
        if (!only_class.empty() && klass != only_class) {
            continue;
        }
        std::printf("\n214/%s:\n", klass.c_str());
        std::vector<std::uint8_t> fixed_data;
        std::vector<std::uint8_t> fixed_meta;
        std::vector<std::uint8_t> var_meta;
        std::vector<std::uint8_t> var2;
        bool complete = true;
        for (const auto& [name, target] :
             std::initializer_list<std::pair<const char*, std::vector<std::uint8_t>*>>{
                 {"FixedData", &fixed_data},
                 {"FixedMeta", &fixed_meta},
                 {"VarMeta", &var_meta},
                 {"Var2Data", &var2}}) {
            const auto found = parts.find(name);
            if (found == parts.end()) {
                std::printf("  missing part %s\n", name);
                complete = false;
                continue;
            }
            if (!read_file(found->second, *target)) {
                std::fprintf(stderr, "viewstate_report: cannot read %s\n",
                             found->second.string().c_str());
                complete = false;
            }
        }
        if (!complete) {
            std::fprintf(stderr, "viewstate_report: 214/%s is incomplete; skipped\n",
                         klass.c_str());
            g_exit = 1;
            continue;
        }
        msproject::viewstate::PartSet part_set;
        part_set.fixed_data = fixed_data;
        part_set.fixed_meta = fixed_meta;
        part_set.var_meta = var_meta;
        part_set.var2_data = var2;

        msproject::viewstate::ViewState state;
        msproject::viewstate::ParseError parse_error;
        if (!msproject::viewstate::parse_view_state(klass, part_set, state, &parse_error)) {
            std::fprintf(stderr, "viewstate_report: 214/%s: %s at %zu\n", klass.c_str(),
                         parse_error.message.c_str(), parse_error.offset);
            g_exit = 1;
            continue;
        }
        if (roundtrip) {
            msproject::viewstate::EncodedParts encoded;
            msproject::viewstate::WriteError write_error;
            if (!msproject::viewstate::encode_view_state(state, encoded, &write_error)) {
                std::fprintf(stderr, "viewstate_report: 214/%s: encode failed: %s\n",
                             klass.c_str(), write_error.message.c_str());
                g_exit = 1;
            } else {
                const bool same =
                    encoded.fixed_data == fixed_data && encoded.fixed_meta == fixed_meta &&
                    encoded.var_meta == var_meta && encoded.var2_data == var2;
                std::printf("  roundtrip: %s fixed_data=%zu fixed_meta=%zu var_meta=%zu "
                            "var2=%zu\n",
                            same ? "byte-identical" : "DIFFERS", encoded.fixed_data.size(),
                            encoded.fixed_meta.size(), encoded.var_meta.size(),
                            encoded.var2_data.size());
                if (!same) {
                    std::fprintf(stderr,
                                 "viewstate_report: 214/%s: re-encode is not byte-identical\n",
                                 klass.c_str());
                    g_exit = 1;
                }
            }
        }
        const std::size_t max_columns = dump ? 0 : 6;
        const std::size_t max_blocks = dump ? 0 : 6;
        std::fputs(msproject::viewstate::describe_view_state(state, max_columns, max_blocks)
                       .c_str(),
                   stdout);
    }

    const fs::path props = class_root / "Props";
    if (fs::is_regular_file(props, error_code)) {
        std::vector<std::uint8_t> bytes;
        if (read_file(props, bytes)) {
            report_props(bytes);
        } else {
            std::fprintf(stderr, "viewstate_report: cannot read %s\n", props.string().c_str());
            g_exit = 1;
        }
    }
    return g_exit;
}
