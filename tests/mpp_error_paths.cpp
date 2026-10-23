/*
 * mpp_error_paths.cpp -- W40: malformed .mpp inputs must fail cleanly.
 *
 * The reader has no "ok()" flag: `mpp::load_file` / `mpp::load_bytes` either
 * return a Document (with recoverable damage in Document::warnings) or throw
 * mpp::Error.  This test pins that contract for the cases the wave-9 brief
 * names: truncated file, not a CFB, missing 114/TBkndTask, FixedMeta count
 * disagreeing with FixedData, a lying VarMeta size word, a Var2Data offset out
 * of range, duplicate uids, links/assignments to dead uids, absurd declared
 * counts and empty streams.  It also runs a deterministic byte-mutation loop so
 * a future bounds bug shows up as a crash, a hang, or an exception that is not
 * mpp::Error.
 *
 * No corpus is needed: the fixtures are generated in memory by
 * mpp_fixture_builder.h.
 */

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "mpp_fixture_builder.h"
#include "reader.h"

namespace {

class TestContext {
  public:
    void check(bool condition, const std::string& what) {
        if (condition) {
            ++checks_;
            return;
        }
        ++failures_;
        std::cerr << "FAIL: " << what << std::endl;
    }

    template <typename A, typename B>
    void equal(const A& actual, const B& expected, const std::string& what) {
        if (actual == expected) {
            ++checks_;
            return;
        }
        ++failures_;
        std::cerr << "FAIL: " << what << " (actual=" << actual
                  << ", expected=" << expected << ")" << std::endl;
    }

    int failures() const { return failures_; }
    int checks() const { return checks_; }

  private:
    int failures_ = 0;
    int checks_ = 0;
};

bool has_warning(const mpp::Document& document, std::string_view needle) {
    for (const std::string& warning : document.warnings) {
        if (warning.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::string catch_error(const std::vector<std::uint8_t>& bytes) {
    try {
        (void)mpp::load_bytes(bytes);
    } catch (const mpp::Error& error) {
        return error.what();
    } catch (const std::exception& error) {
        return std::string("NON-MPP EXCEPTION: ") + error.what();
    } catch (...) {
        return "NON-STD EXCEPTION";
    }
    return {};
}

std::uint32_t get_u32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

/* The smallest valid document: project row + one live task, no VarMeta (so the
 * table is "usable == false" and every row is live), Task FixedMeta in place. */
std::vector<mpp_fixture::Stream> valid_streams() {
    std::vector<std::uint8_t> fixed = mpp_fixture::task_fixed_data(2);
    mpp_fixture::put_u32(fixed, mpp_fixture::task_row_offset(1) + 4, 1); /* uid */
    mpp_fixture::put_u32(fixed, mpp_fixture::task_row_offset(1) + 84, 4800); /* 1 day */

    std::vector<mpp_fixture::Stream> streams;
    streams.push_back({"114/TBkndTask/FixedData", std::move(fixed), 0});
    streams.push_back({"114/TBkndTask/FixedMeta", mpp_fixture::task_meta(2, 5), 0});
    return streams;
}

void test_valid_control(TestContext& context) {
    const std::vector<std::uint8_t> bytes = mpp_fixture::make_cfb(valid_streams());
    context.check(!bytes.empty(), "the in-memory fixture builds a CFB");
    try {
        const mpp::Document document = mpp::load_bytes(bytes);
        context.equal(document.tasks.size(), std::size_t{1},
                      "the valid control has one live task");
        context.equal(document.tasks[0].uid, std::uint32_t{1}, "the task uid");
        context.equal(document.tasks[0].duration_ticks, std::uint32_t{4800},
                      "the task duration");
        context.check(!has_warning(document, "unreferenced sectors"),
                      "a compact CFB reports no FAT slack");
    } catch (const std::exception& error) {
        context.check(false, std::string("the valid control loads: ") + error.what());
    }
}

/* FAT slack is not damage.  corpus/imported/sf248.mpp is the measured real case:
 * 959 sectors, a FAT describing 768, and olefile reads all 81 streams to their
 * declared sizes.  Build the same shape in memory by appending sectors no chain
 * references: the document must load with an "N unreferenced sectors" warning.
 * But a chain that POINTS past the FAT is fatal -- that is real truncation, not
 * slack -- so patch the directory chain's first FAT link into the slack area and
 * expect mpp::Error. */
void test_cfb_unreferenced_sectors(TestContext& context) {
    {
        std::vector<std::uint8_t> bytes = mpp_fixture::make_cfb(valid_streams());
        /* make_cfb pads the FAT to whole sectors, so the FAT's entry count is
         * `num_fat * 128` and can exceed the sector count.  Add whole FAT-padding
         * plus 8 sectors: exactly 8 sectors then have no FAT entry. */
        const std::size_t fat_entries =
            static_cast<std::size_t>(get_u32(bytes, 0x2C)) *
            (mpp_fixture::kSector / 4);
        const std::size_t total_sectors = bytes.size() / mpp_fixture::kSector - 1;
        const std::size_t append_sectors =
            (fat_entries > total_sectors ? fat_entries - total_sectors : 0) + 8;
        bytes.resize(bytes.size() + append_sectors * mpp_fixture::kSector, 0);
        const mpp::Document document = mpp::load_bytes(bytes);
        context.equal(document.tasks.size(), std::size_t{1},
                      "FAT slack does not drop a live task");
        context.check(has_warning(document, "8 unreferenced sectors"),
                      "FAT slack is reported as 8 unreferenced sectors");
        context.check(has_warning(document,
                                  std::to_string(fat_entries) + " of the file's"),
                      "the slack warning names the FAT and physical sector counts");
    }
    {
        std::vector<std::uint8_t> bytes = mpp_fixture::make_cfb(valid_streams());
        const std::size_t fat_entries =
            static_cast<std::size_t>(get_u32(bytes, 0x2C)) *
            (mpp_fixture::kSector / 4);
        const std::size_t total_sectors = bytes.size() / mpp_fixture::kSector - 1;
        const std::size_t append_sectors =
            (fat_entries > total_sectors ? fat_entries - total_sectors : 0) + 8;
        bytes.resize(bytes.size() + append_sectors * mpp_fixture::kSector, 0);
        /* DIFAT[0] in the header names the FAT sector; entry 0 is the directory
         * chain's first link.  Aim it at a sector that exists physically but has
         * no FAT entry, i.e. past the end of the FAT. */
        const std::uint32_t fat_sector = get_u32(bytes, 0x4C);
        const std::size_t fat_offset =
            (static_cast<std::size_t>(fat_sector) + 1) * mpp_fixture::kSector;
        mpp_fixture::put_u32(bytes, fat_offset,
                             static_cast<std::uint32_t>(fat_entries + 2));
        const std::string message = catch_error(bytes);
        context.check(!message.empty(),
                      "a chain that walks past the FAT raises mpp::Error");
        context.check(message.find("NON-MPP") == std::string::npos,
                      "the chain-past-FAT error is mpp::Error: " + message);
        context.check(message.find("FAT") != std::string::npos,
                      "the chain-past-FAT error names the FAT: " + message);
    }
}

/* The MPXJ-written MPP14 task layout (corpus/probe/SubprojectA-9.mpp and
 * SubprojectB-9.mpp) is 206-byte rows, not Project's 202: A-9's FixedData is
 * 872 = 48 + 206*4 and its FixedMeta data offsets are 48,254,460,666.  Reading
 * that with the 202-byte stride slipped 4 bytes per row and emitted UniqueID 0
 * rows with nonsense dates.  The metadata offsets are enough to see that the
 * record layout is different; the reader must refuse it with a positioned error
 * (like the MPP12 206-byte variant) instead of returning invented rows. */
void test_unsupported_task_row_stride(TestContext& context) {
    constexpr std::size_t kRows = 4;
    constexpr std::size_t kStride = 206;
    std::vector<mpp_fixture::Stream> streams;
    streams.push_back({"114/TBkndTask/FixedData",
                       std::vector<std::uint8_t>(mpp_fixture::kTaskHeader +
                                                     kStride * kRows,
                                                 0),
                       0});
    /* 3 descriptors + 4 data entries; entry offsets 48, 254, 460, 666. */
    streams.push_back({"114/TBkndTask/FixedMeta",
                       mpp_fixture::fixed_meta(kRows + 3, 47, 3,
                                               static_cast<std::uint32_t>(kRows + 3),
                                               static_cast<std::uint32_t>(
                                                   mpp_fixture::kTaskHeader),
                                               static_cast<std::uint32_t>(kStride)),
                       0});
    const std::string message = catch_error(mpp_fixture::make_cfb(streams));
    context.check(!message.empty(), "a 206-byte MPP14 task row raises mpp::Error");
    context.check(message.find("206") != std::string::npos,
                  "the unsupported-stride error names 206: " + message);
    context.check(message.find("202") != std::string::npos,
                  "the unsupported-stride error names the implemented 202: " + message);
    context.check(message.find("NON-MPP") == std::string::npos,
                  "the unsupported-stride error is mpp::Error: " + message);
}

void test_container_failures(TestContext& context) {
    context.check(!catch_error({}).empty(), "an empty buffer raises mpp::Error");
    context.check(!catch_error(std::vector<std::uint8_t>(10, 0)).empty(),
                  "a short buffer raises mpp::Error");
    context.check(!catch_error(std::vector<std::uint8_t>(512, 0)).empty(),
                  "a 512-byte non-CFB buffer raises mpp::Error");

    const std::vector<std::uint8_t> valid = mpp_fixture::make_cfb(valid_streams());
    std::vector<std::uint8_t> truncated = valid;
    truncated.resize(100);
    context.check(catch_error(truncated).find("compound file") != std::string::npos,
                  "a truncated header is reported as a compound-file problem");

    truncated = valid;
    truncated.resize(valid.size() - 128);
    const std::string message = catch_error(truncated);
    context.check(!message.empty(), "a truncated sector chain raises mpp::Error");
    context.check(message.find("NON-MPP") == std::string::npos,
                  "the truncated-chain error is mpp::Error: " + message);

    /* A directory entry that claims far more than the file holds must be
     * rejected before any allocation follows the claim. */
    std::vector<mpp_fixture::Stream> lying_size = valid_streams();
    lying_size[0].declared_size = 0xFFFFFFFFull;
    const std::string lying = catch_error(mpp_fixture::make_cfb(lying_size));
    context.check(!lying.empty(), "a 4 GiB stream size word raises mpp::Error");
    context.check(lying.find("NON-MPP") == std::string::npos,
                  "the lying-size error is mpp::Error: " + lying);
}

void test_missing_task_table(TestContext& context) {
    std::vector<mpp_fixture::Stream> streams;
    streams.push_back({"114/TBkndRsc/FixedData", std::vector<std::uint8_t>(48, 0), 0});
    const std::string message = catch_error(mpp_fixture::make_cfb(streams));
    context.check(!message.empty(), "a file without 114/TBkndTask raises mpp::Error");
    context.check(message.find("Task table") != std::string::npos,
                  "the error names the missing Task table: " + message);

    std::vector<mpp_fixture::Stream> empty_task;
    empty_task.push_back({"114/TBkndTask/FixedData", {}, 0});
    context.check(!catch_error(mpp_fixture::make_cfb(empty_task)).empty(),
                  "an empty Task FixedData raises mpp::Error");
}

void test_metadata_damage(TestContext& context) {
    /* FixedMeta header says four billion entries.  The stream is 5 entries; the
     * reader must warn from the declared word and never iterate the claim. */
    {
        std::vector<mpp_fixture::Stream> streams = valid_streams();
        std::vector<std::uint8_t> meta = mpp_fixture::task_meta(2, 5);
        mpp_fixture::put_u32(meta, 8, 0xFFFFFFFFu);
        streams[1].data = std::move(meta);
        const mpp::Document document = mpp::load_bytes(mpp_fixture::make_cfb(streams));
        context.check(has_warning(document, "declares 4294967295"),
                      "a 4-billion FixedMeta count is reported, not iterated");
        context.equal(document.tasks.size(), std::size_t{1},
                      "the row is still read when the count word is absurd");
    }
    /* The entry count does not match rows + descriptors: a warning, not a
     * crash, and no row is invented. */
    {
        std::vector<mpp_fixture::Stream> streams = valid_streams();
        streams[1].data = mpp_fixture::task_meta(2, 2);
        const mpp::Document document = mpp::load_bytes(mpp_fixture::make_cfb(streams));
        context.check(has_warning(document, "FixedMeta holds") ||
                          has_warning(document, "header declares"),
                      "a FixedMeta/FixedData count disagreement is warned");
    }
    /* Lying VarMeta size word (word at +0x14) with a real entry. */
    {
        std::vector<mpp_fixture::Stream> streams = valid_streams();
        std::vector<std::uint8_t> var2;
        mpp_fixture::append_block(var2, {0x41, 0x00, 0x00, 0x00});
        streams.push_back(
            {"114/TBkndTask/VarMeta",
             mpp_fixture::var_meta({1, 0, 0x0B40000Eu}, 1, 0xFFFFFFFFu), 0});
        streams.push_back({"114/TBkndTask/Var2Data", std::move(var2), 0});
        const mpp::Document document = mpp::load_bytes(mpp_fixture::make_cfb(streams));
        context.check(has_warning(document, "VarMeta says Var2Data is"),
                      "a lying VarMeta size word is warned");
    }
    /* Var2Data offset far past the stream. */
    {
        std::vector<mpp_fixture::Stream> streams = valid_streams();
        streams.push_back(
            {"114/TBkndTask/VarMeta",
             mpp_fixture::var_meta({1, 0x7FFFFFFFu, 0x0B40000Eu}, 1, 0), 0});
        streams.push_back({"114/TBkndTask/Var2Data", {}, 0});
        const mpp::Document document = mpp::load_bytes(mpp_fixture::make_cfb(streams));
        context.check(has_warning(document, "offset is past the end"),
                      "a Var2Data offset past the end is warned");
    }
    /* Empty optional streams: no crash, warnings only. */
    {
        std::vector<mpp_fixture::Stream> streams = valid_streams();
        streams.push_back({"114/TBkndTask/VarMeta", {}, 0});
        streams.push_back({"114/TBkndTask/Var2Data", {}, 0});
        streams.push_back({"114/TBkndTask/FixedMeta", {}, 0});
        streams.push_back({"114/TBkndRsc/FixedData", {}, 0});
        const mpp::Document document = mpp::load_bytes(mpp_fixture::make_cfb(streams));
        context.check(!document.warnings.empty(),
                      "empty metadata streams load with warnings");
        context.equal(document.tasks.size(), std::size_t{1},
                      "an empty VarMeta leaves every row live");
    }
}

void test_duplicate_and_dead_references(TestContext& context) {
    /* Two task rows with uid 7. */
    {
        std::vector<std::uint8_t> fixed = mpp_fixture::task_fixed_data(3);
        mpp_fixture::put_u32(fixed, mpp_fixture::task_row_offset(1) + 4, 7);
        mpp_fixture::put_u32(fixed, mpp_fixture::task_row_offset(2) + 4, 7);
        std::vector<mpp_fixture::Stream> streams;
        streams.push_back({"114/TBkndTask/FixedData", std::move(fixed), 0});
        streams.push_back({"114/TBkndTask/FixedMeta", mpp_fixture::task_meta(3, 6), 0});
        const mpp::Document document = mpp::load_bytes(mpp_fixture::make_cfb(streams));
        context.check(has_warning(document, "appears on more than one row"),
                      "a duplicate task UniqueID is warned");
    }
    /* A link to task 99 and an assignment for task 98, neither live. */
    {
        std::vector<mpp_fixture::Stream> streams = valid_streams();
        std::vector<std::uint8_t> link(20, 0);
        mpp_fixture::put_u32(link, 0, 10); /* link uid */
        mpp_fixture::put_u32(link, 4, 1);  /* predecessor: live task */
        mpp_fixture::put_u32(link, 8, 99); /* successor: dead */
        mpp_fixture::put_u16(link, 12, 1);
        streams.push_back({"114/TBkndCons/FixedData", std::move(link), 0});
        streams.push_back(
            {"114/TBkndCons/FixedMeta",
             mpp_fixture::fixed_meta(1, 10, 0, 1, 0, 20), 0});

        std::vector<std::uint8_t> assn(110, 0);
        mpp_fixture::put_u32(assn, 0, 20); /* assignment uid */
        mpp_fixture::put_u32(assn, 4, 98); /* dead task */
        mpp_fixture::put_u32(assn, 8, 0xFFFF0001u);
        streams.push_back({"114/TBkndAssn/FixedData", std::move(assn), 0});
        streams.push_back(
            {"114/TBkndAssn/FixedMeta",
             mpp_fixture::fixed_meta(1, 34, 0, 1, 0, 110), 0});

        const mpp::Document document = mpp::load_bytes(mpp_fixture::make_cfb(streams));
        context.check(has_warning(document, "which no live task holds"),
                      "a link/assignment to a dead uid is warned");
        context.equal(document.links.size(), std::size_t{1}, "the link row is returned");
        context.check(!document.links[0].endpoints_live(),
                      "a dead-endpoint link reports endpoints_live() == false");
        context.equal(document.assignments.size(), std::size_t{1},
                      "the assignment row is returned");
    }
}

/* Deterministic byte mutations of a valid container: no crash, no non-mpp
 * exception.  Some mutations still parse (a changed field byte), so the check is
 * the exception type, not whether it throws. */
void test_mutation_loop(TestContext& context) {
    const std::vector<std::uint8_t> valid = mpp_fixture::make_cfb(valid_streams());
    std::mt19937 rng(0x5EEDu);
    std::uniform_int_distribution<std::size_t> index(0, valid.size() - 1);
    int loaded = 0;
    int rejected = 0;
    for (int iteration = 0; iteration < 256; ++iteration) {
        std::vector<std::uint8_t> mutated = valid;
        const std::size_t at = index(rng);
        mutated[at] ^= static_cast<std::uint8_t>(1u << (iteration % 8));
        try {
            (void)mpp::load_bytes(mutated);
            ++loaded;
        } catch (const mpp::Error&) {
            ++rejected;
        } catch (const std::exception& error) {
            context.check(false, "mutation at byte " + std::to_string(at) +
                                     " raised a non-mpp exception: " + error.what());
        } catch (...) {
            context.check(false, "mutation at byte " + std::to_string(at) +
                                     " raised a non-std exception");
        }
    }
    context.equal(loaded + rejected, 256, "every mutation returned or raised mpp::Error");
    std::cout << "mutation loop: " << loaded << " loaded, " << rejected
              << " rejected with mpp::Error\n";
}

int run() {
    TestContext context;
    test_valid_control(context);
    test_container_failures(context);
    test_cfb_unreferenced_sectors(context);
    test_unsupported_task_row_stride(context);
    test_missing_task_table(context);
    test_metadata_damage(context);
    test_duplicate_and_dead_references(context);
    test_mutation_loop(context);
    std::cout << (context.failures() == 0 ? "PASS" : "FAIL") << ": " << context.checks()
              << " checks";
    if (context.failures() != 0) {
        std::cout << ", " << context.failures() << " failures";
    }
    std::cout << '\n';
    return context.failures() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

}  // namespace

int main() { return run(); }
