// app/spelling_tests.cpp -- the read-only spelling check (wave 30, W30-3.3),
// proved headlessly.
//
// The brief asks for a "read-only check over task names; no dictionary data
// from Project".  These cases pin the engine under the dialog:
//   * the built-in word list's four hygiene properties (lower-case ASCII, one
//     word per line, sorted, no duplicates) and its size;
//   * representative known words across the vocabulary the list carries;
//   * the conservative suffix rules (plural, past, gerund, adverb, comparative)
//     and the unknown stems they must NOT rescue;
//   * the token rules the dialog states: hyphens/digits split, one-letter
//     tokens and non-ASCII tokens are skipped, possessives are checked against
//     their stem, and duplicates keep the first spelling;
//   * spelling_issues() over a PlanInput: one row per bad task name, plan
//     order, uid/id/name carried, deduplicated unknown words, empty names not
//     counted, and an all-known plan producing nothing.
//
// Synthetic rows only: no corpus, no client data, no fixture and no writer.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "layout_model.h"
#include "spelling.h"
#include "spelling_words.h"

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

void check_size(std::size_t got, std::size_t want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL: %s: got %llu want %llu\n", what,
                    static_cast<unsigned long long>(got),
                    static_cast<unsigned long long>(want));
    }
}

void check_words(const std::vector<std::string>& got,
                 const std::vector<std::string>& want, const char* what) {
    ++g_checks;
    if (got == want) {
        return;
    }
    ++g_failures;
    std::printf("FAIL: %s\n  got:", what);
    for (const std::string& word : got) {
        std::printf(" [%s]", word.c_str());
    }
    std::printf("\n  want:");
    for (const std::string& word : want) {
        std::printf(" [%s]", word.c_str());
    }
    std::printf("\n");
}

void check_known(const char* word, bool want, const char* what) {
    ++g_checks;
    if (app::spelling_word_known(word) == want) {
        return;
    }
    ++g_failures;
    std::printf("FAIL: %s: \"%s\" should be %s\n", what, word,
                want ? "known" : "unknown");
}

void test_list_hygiene() {
    std::vector<std::string_view> lines;
    std::string_view text = app::kSpellingWords;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t end = text.find('\n', start);
        const std::size_t stop = end == std::string_view::npos ? text.size() : end;
        const std::string_view line = text.substr(start, stop - start);
        if (!line.empty()) {
            lines.push_back(line);
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    check(!lines.empty(), "the word list is not empty");
    check(lines.size() >= 1500, "the word list carries a useful base vocabulary");
    check_size(app::spelling_word_count(), lines.size(),
               "the set's size is the list's line count");
    bool ascii = true;
    bool lower = true;
    bool no_space = true;
    bool sorted = true;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        for (const char character : lines[index]) {
            const auto byte = static_cast<unsigned char>(character);
            if (byte < 0x20 || byte >= 0x7F) {
                ascii = false;
            }
            if (character >= 'A' && character <= 'Z') {
                lower = false;
            }
            if (character == ' ' || character == '\t' || character == '\r') {
                no_space = false;
            }
        }
        if (index > 0 && !(lines[index - 1] < lines[index])) {
            sorted = false;
        }
    }
    check(ascii, "every word is printable ASCII");
    check(lower, "every word is lower case");
    check(no_space, "one word per line, with no embedded whitespace");
    check(sorted, "the list is sorted and has no duplicates");
    check(lines.front() == "a", "the sorted list starts with \"a\"");
}

void test_known_words() {
    for (const char* word :
         {"design", "review", "meeting", "schedule", "milestone", "baseline",
          "stakeholder", "deliverable", "dependency", "assignment", "resource",
          "project", "task", "budget", "construction", "engineering",
          "commissioning", "submittal", "punchlist", "workflow", "dashboard",
          "wbs", "kpi", "qa", "uid", "ok", "don't", "cannot"}) {
        check_known(word, true, "a representative listed word is known");
    }
    check_known("DESIGN", true, "lookup is case-insensitive");
    check_known("Design", true, "a capitalised listed word is known");
    check_known("", false, "an empty word is not known");
    check_known("caf\xc3\xa9", false, "a non-ASCII word is not known");
    check_known("desgin", false, "a transposition is unknown");
    check_known("reveiw", false, "a transposition is unknown");
    check_known("xyzzy", false, "a nonsense word is unknown");
    check_known("milestonee", false, "a doubled final letter is not rescued");
}

void test_suffix_rules() {
    for (const char* word :
         {"schedules", "scheduling", "scheduled", "planned", "planning",
          "meetings", "reviewed", "changes", "milestones", "dependencies",
          "stakeholders", "deliverables", "assigned", "assigning", "stopped",
          "reviews", "phases", "policies", "quickly", "happily", "larger",
          "largest", "designer", "task's"}) {
        check_known(word, true, "a regular inflection of a listed stem is known");
    }
    for (const char* word : {"desgin", "reveiw", "schedulng", "plannning",
                             "meetingz", "projectx", "stakeholdr"}) {
        check_known(word, false, "an unknown stem is not rescued by a suffix rule");
    }
}

void test_token_rules() {
    check_words(app::spelling_unknown_words("Design review"), {},
                "two listed words are clean");
    check_words(app::spelling_unknown_words("Pre-construction kick-off"), {},
                "hyphens split into listed words");
    check_words(app::spelling_unknown_words("Phase B"), {},
                "a one-letter token is skipped, not judged");
    check_words(app::spelling_unknown_words("2026-01-05"), {},
                "digits and date punctuation produce no token");
    check_words(app::spelling_unknown_words("Caf\xc3\xa9"), {},
                "a non-ASCII token is skipped, not flagged");
    check_words(app::spelling_unknown_words("R\xc3\xa9sum\xc3\xa9 review"), {},
                "a non-ASCII token does not hide the ASCII words around it");
    check_words(app::spelling_unknown_words("Task's name"), {},
                "a possessive is checked against its stem");
    check_words(app::spelling_unknown_words("don't change"), {},
                "a listed contraction is known");
    check_words(app::spelling_unknown_words(""), {}, "empty text has no findings");
    check_words(app::spelling_unknown_words("   '  "), {},
                "punctuation and empty tokens have no findings");
    check_words(app::spelling_unknown_words("Desgin reveiw desgin"),
                {"Desgin", "reveiw"},
                "unknown words keep first-appearance order and first spelling");
    check_words(app::spelling_unknown_words("DESGIN Desgin"), {"DESGIN"},
                "deduplication is case-insensitive and keeps the first spelling");
    check_words(app::spelling_unknown_words("well-known"), {},
                "a hyphenated pair of listed words is clean");
}

app::TaskRow task(std::uint32_t uid, const char* name) {
    app::TaskRow row;
    row.uid = uid;
    row.id = uid;
    row.outline_number = std::to_string(uid);
    row.name = name;
    return row;
}

void test_plan() {
    app::PlanInput plan;
    plan.tasks = {task(1, ""), task(2, "Design review"),
                  task(3, "Desgin reveiw and desgin"),
                  task(4, "Milestone sign-off"), task(5, "Kickof meeting")};
    check_size(app::spelling_names_checked(plan), 4,
               "every non-empty task name is counted, and only those");
    const std::vector<app::SpellingIssue> issues = app::spelling_issues(plan);
    check_size(issues.size(), 2, "one issue per task name with an unknown word");
    if (issues.size() == 2) {
        check(issues[0].uid == 3 && issues[0].id == 3,
              "the first issue carries the task's uid and id");
        check(issues[0].name == "Desgin reveiw and desgin",
              "the first issue carries the task name as stored");
        check_words(issues[0].unknown_words, {"Desgin", "reveiw"},
                    "the first issue's words are deduplicated in order");
        check(issues[1].uid == 5 && issues[1].id == 5,
              "the second issue follows plan order");
        check_words(issues[1].unknown_words, {"Kickof"},
                    "the second issue carries its own unknown word");
    }

    app::PlanInput clean;
    clean.tasks = {task(7, "Design review"), task(8, "Kickoff meeting")};
    check_size(app::spelling_issues(clean).size(), 0,
               "an all-known plan produces no issues");
    check_size(app::spelling_names_checked(clean), 2,
               "the clean plan's two names are counted");
    app::PlanInput empty;
    check_size(app::spelling_issues(empty).size(), 0, "an empty plan has no issues");
    check_size(app::spelling_names_checked(empty), 0, "an empty plan checks no names");
}

}  // namespace

int main() {
    test_list_hygiene();
    test_known_words();
    test_suffix_rules();
    test_token_rules();
    test_plan();

    if (g_failures != 0) {
        std::printf("FAIL: %d of %d checks failed\n", g_failures, g_checks);
        return 1;
    }
    std::printf("spelling_tests: %d checks passed; %llu listed words\n", g_checks,
                static_cast<unsigned long long>(app::spelling_word_count()));
    return 0;
}
