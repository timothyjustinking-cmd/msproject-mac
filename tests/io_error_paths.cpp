/*
 * io_error_paths.cpp -- W40: the MSPDI importer fails cleanly on malformed input.
 *
 * Contract (core/io/mspdi.h): malformed XML or a root other than <Project>
 * throws core::io::MspdiError; a bad value inside an otherwise valid document
 * is a warning and does not throw.  No input may crash, read past a buffer or
 * recurse without a bound.  The duplicate-UID and dead-reference cases are
 * warnings because the importer can still return the rows; a caller that keys
 * by uid knows the document is ambiguous.
 */

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>

#include "mspdi.h"

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

    int failures() const { return failures_; }
    int checks() const { return checks_; }

  private:
    int failures_ = 0;
    int checks_ = 0;
};

bool has_warning(const core::io::MspdiDocument& document, std::string_view needle) {
    for (const std::string& warning : document.warnings) {
        if (warning.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::string throw_message(std::string_view xml) {
    try {
        (void)core::io::import_mspdi(xml);
    } catch (const core::io::MspdiError& error) {
        return error.what();
    } catch (const std::exception& error) {
        return std::string("NON-MSPDI EXCEPTION: ") + error.what();
    } catch (...) {
        return "NON-STD EXCEPTION";
    }
    return {};
}

void test_malformed_documents(TestContext& context) {
    context.check(!throw_message("").empty(), "empty XML throws MspdiError");
    context.check(!throw_message("<Project><Tasks>").empty(),
                  "a truncated document throws MspdiError");
    context.check(throw_message("<Project><Tasks>").find("line") != std::string::npos,
                  "the truncated-document error carries a line number");
    context.check(!throw_message("<Foo/>").empty(),
                  "a non-Project root throws MspdiError");
    context.check(throw_message("<Foo/>").find("Project") != std::string::npos,
                  "the wrong-root error names the expected element");
    context.check(!throw_message("<Project><a>&nbsp;</a></Project>").empty(),
                  "an unknown entity throws MspdiError");
    context.check(!throw_message("<Project><Tasks><Task><UID>1</Task></Tasks></Project>")
                       .empty(),
                  "mismatched closing tags throw MspdiError");
    context.check(!throw_message("<Project><Tasks><Task><UID>1</UID>").empty(),
                  "an unterminated element throws MspdiError");

    /* Nesting is bounded at 256 in the parser: deeper input is an error, not a
     * stack overflow. */
    std::string deep = "<Project>";
    for (int i = 0; i < 400; ++i) {
        deep += "<a>";
    }
    std::string close;
    for (int i = 0; i < 400; ++i) {
        close += "</a>";
    }
    deep += close + "</Project>";
    const std::string depth_message = throw_message(deep);
    context.check(!depth_message.empty(), "400-level nesting throws MspdiError");
    context.check(depth_message.find("nesting too deep") != std::string::npos,
                  "deep nesting is refused by name: " + depth_message);

    bool missing_file_threw = false;
    try {
        (void)core::io::import_mspdi_file("tests/does-not-exist-w40.xml");
    } catch (const core::io::MspdiError& error) {
        missing_file_threw = std::string(error.what()).find("does-not-exist") !=
                             std::string::npos;
    }
    context.check(missing_file_threw, "a missing file throws MspdiError naming the path");
}

void test_recoverable_damage(TestContext& context) {
    /* Out-of-range UID: warning, fallback row, no throw. */
    const core::io::MspdiDocument bad_uid = core::io::import_mspdi(
        "<Project><Tasks><Task><UID>4294967296</UID></Task></Tasks></Project>");
    context.check(has_warning(bad_uid, "not a 32-bit integer"),
                  "an over-range UID is a warning");

    /* Bad typed value: the row is still imported and only that value is lost. */
    const core::io::MspdiDocument bad_value = core::io::import_mspdi(
        "<Project><Tasks><Task><UID>1</UID><Duration>forever</Duration>"
        "</Task></Tasks></Project>");
    context.check((bad_value.tasks.row_count()) == std::size_t{1},
                      "a malformed value still leaves the task row");
    context.check(has_warning(bad_value, "Duration"), "the bad field is named");

    /* Malformed assignment dates are warnings, not fatal. */
    const core::io::MspdiDocument bad_date = core::io::import_mspdi(
        "<Project><Tasks><Task><UID>1</UID></Task></Tasks>"
        "<Assignments><Assignment><UID>1</UID><TaskUID>1</TaskUID>"
        "<Start>not-a-date</Start></Assignment></Assignments></Project>");
    context.check(has_warning(bad_date, "expected an ISO date/time"),
                  "a malformed assignment date is a warning");

    /* Duplicate task uid: warned, both rows kept (the importer is not a
     * deduplicator). */
    const core::io::MspdiDocument duplicate = core::io::import_mspdi(
        "<Project><Tasks>"
        "<Task><UID>7</UID></Task><Task><UID>7</UID></Task>"
        "</Tasks></Project>");
    context.check(has_warning(duplicate, "duplicate UID"), "duplicate task UID is warned");
    context.check((duplicate.tasks.row_count()) == std::size_t{2}, "both duplicate rows are kept");

    /* Duplicate resource/assignment uids and dead references. */
    const core::io::MspdiDocument dead_refs = core::io::import_mspdi(
        "<Project>"
        "<Tasks><Task><UID>1</UID></Task></Tasks>"
        "<Resources><Resource><UID>5</UID></Resource>"
        "<Resource><UID>5</UID></Resource></Resources>"
        "<Assignments>"
        "<Assignment><UID>9</UID><TaskUID>99</TaskUID><ResourceUID>77</ResourceUID>"
        "</Assignment>"
        "<Assignment><UID>9</UID><TaskUID>1</TaskUID><ResourceUID>5</ResourceUID>"
        "</Assignment>"
        "</Assignments></Project>");
    context.check(has_warning(dead_refs, "duplicate UID"),
                  "a duplicate resource/assignment UID is warned");
    context.check(has_warning(dead_refs, "names no imported task"),
                  "an assignment to a dead task uid is warned");
    context.check(has_warning(dead_refs, "names no imported resource"),
                  "an assignment to a dead resource uid is warned");

    /* Units are kept as imported (MSPDI writes a fraction; the reader's model
     * uses percent -- the cross-check normalises; core/io keeps the raw value,
     * documented in memory/io.md). */
    const core::io::MspdiDocument units = core::io::import_mspdi(
        "<Project><Tasks><Task><UID>1</UID></Task></Tasks>"
        "<Resources><Resource><UID>1</UID></Resource></Resources>"
        "<Assignments><Assignment><UID>1</UID><TaskUID>1</TaskUID>"
        "<ResourceUID>1</ResourceUID><Units>1</Units></Assignment>"
        "</Assignments></Project>");
    context.check((units.assignments.row_count()) == std::size_t{1}, "assignment with Units imports");
}

int run() {
    TestContext context;
    test_malformed_documents(context);
    test_recoverable_damage(context);
    std::cout << (context.failures() == 0 ? "PASS" : "FAIL") << ": " << context.checks()
              << " checks\n";
    return context.failures() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

}  // namespace

int main() { return run(); }
