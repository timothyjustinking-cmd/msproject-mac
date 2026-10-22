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
// This tool reads; it has no writer and no write-layout claim.  See
// docs/handoff/w56.md for the measured layouts and the differential results.

#include "viewstate.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <map>
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
    std::string only_class;
    fs::path root;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--dump") {
            dump = true;
        } else if (arg == "--inventory") {
            inventory = true;
        } else if (arg == "--class") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "viewstate_report: --class needs a name\n");
                return 2;
            }
            only_class = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::printf("usage: viewstate_report [--dump] [--inventory] [--class NAME] <root>\n"
                        "  <root>       directory holding 214/<Class>/<Part> files\n"
                        "  --dump       print every column/block (default truncates at 6)\n"
                        "  --inventory  print sizes/FNV only, skip decoding\n"
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
