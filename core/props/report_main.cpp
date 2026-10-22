// props_report -- dump one raw `Props` stream.
//
// The argument is an already-extracted stream, not an .mpp file.  Extract it
// with olefile or scripts/mpp_dump.py, for example:
//
//   python -c "import olefile; o=olefile.OleFileIO('x.mpp'); open('props.bin','wb').write(o.openstream('114/Props').read())"
//
// The same decoder handles 114/Props, 214/Props and Props14 because they share
// the measured header + record layout; only the opaque tail differs in size.

#include "props.h"

#include <cstdio>
#include <fstream>
#include <string>

int main(int argc, char** argv) {
    bool dump_records = false;
    bool show_formulas = false;
    bool show_tail = false;
    std::string path;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--dump") {
            dump_records = true;
        } else if (arg == "--formulas") {
            show_formulas = true;
        } else if (arg == "--tail") {
            show_tail = true;
        } else if (arg == "--help" || arg == "-h") {
            std::printf("usage: props_report [--dump] [--formulas] [--tail] <props.bin>\n");
            return 0;
        } else if (path.empty()) {
            path = arg;
        } else {
            std::fprintf(stderr, "props_report: unexpected argument: %s\n", arg.c_str());
            return 2;
        }
    }
    if (path.empty()) {
        std::fprintf(stderr, "usage: props_report [--dump] [--formulas] [--tail] <props.bin>\n");
        return 2;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::fprintf(stderr, "props_report: cannot open %s\n", path.c_str());
        return 1;
    }
    file.seekg(0, std::ios::end);
    const std::streamoff length = file.tellg();
    file.seekg(0, std::ios::beg);
    if (length <= 0) {
        std::fprintf(stderr, "props_report: %s is empty\n", path.c_str());
        return 1;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    file.read(reinterpret_cast<char*>(bytes.data()), length);
    if (!file) {
        std::fprintf(stderr, "props_report: short read on %s\n", path.c_str());
        return 1;
    }

    msproject::props::PropsDocument document;
    msproject::props::ParseError error;
    if (!msproject::props::parse_props(bytes, document, &error)) {
        std::fprintf(stderr, "props_report: %s at offset %zu\n", error.message.c_str(),
                     error.offset);
        return 1;
    }

    if (dump_records) {
        std::fputs(msproject::props::dump_document(document).c_str(), stdout);
    } else {
        std::printf("Props stream: %u bytes\n", document.size());
        std::printf("  header: format=0x%08x declared_entries=%u\n",
                    document.header.format_version, document.header.declared_entries);
        std::printf("  records: %zu\n", document.records.size());
        std::printf("  tail: offset=%u length=%u\n", document.tail_offset,
                    document.tail_size());
    }

    if (show_formulas) {
        const auto formulas = msproject::props::extract_formulas(document);
        std::printf("formulas: %zu\n", formulas.size());
        for (const auto& formula : formulas) {
            std::printf("  field=0x%08x block=%u text=%u trailer=%s formula=%s\n",
                        formula.field_id, formula.block_offset, formula.text_offset,
                        formula.has_trailer ? "yes" : "no",
                        msproject::props::formula_with_field_ids(formula.formula).c_str());
        }
    }

    if (show_tail) {
        std::fputs(msproject::props::describe_tail(document).c_str(), stdout);
    }
    return 0;
}
