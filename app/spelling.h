// app/spelling.h -- the read-only spelling check (wave 30, W30-3.3).
//
// THE TARGET (wave 30 brief, G3): Spelling is a "read-only check over task
// names; no dictionary data from Project".  This port has no spelling engine,
// no Office dictionary and no custom dictionary; it cannot read Project's and
// does not pretend to.  What it HAS is its own small word list
// (spelling_words.h), and this header is the whole engine over it:
//
//   * spelling_unknown_words() tokenises one name and returns the tokens the
//     port's list does not know, in first-appearance order;
//   * spelling_issues() runs that over every task name in an app::PlanInput;
//   * spelling_names_checked() reports how many names were examined.
//
// Nothing here corrects, replaces or writes anything: the result is data for a
// read-only dialog, and the plan is read by const reference.  There is no
// dictionary data from Microsoft Project anywhere in this file or its .cpp.
//
// TOKEN RULES (deliberate, and stated in the dialog so a reader can judge a
// finding):
//   * a token is a run of ASCII letters and apostrophes; hyphens, digits,
//     spaces and punctuation separate tokens, so "Pre-construction 2026" is
//     "pre" and "construction";
//   * a one-letter token carries no spelling information and is skipped, so
//     "Phase B" is not a finding;
//   * a token with any non-ASCII byte is skipped rather than judged: this
//     list is English-only, and flagging every accented or non-Latin name
//     would be a false claim;
//   * a trailing possessive "'s" is checked against its stem ("task's");
//   * on top of the explicit list, conservative suffix rules accept the
//     regular plural, past tense, gerund, adverb, comparative and superlative
//     forms of a known stem (-s/-es/-ies, -ed/-ing with the silent-e and
//     doubled-consonant spellings, -ly, -er/-est).  A word whose stem is not
//     in the list is still unknown; the rules never guess a different stem.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace app {

struct PlanInput;

// The number of explicit words in the built-in list (suffix forms are extra).
std::size_t spelling_word_count();

// True when the word is in the built-in list or one of its regular suffix
// forms.  Case-insensitive; a non-ASCII or empty word is not known.
bool spelling_word_known(std::string_view word);

// The unknown tokens of one task name, in first-appearance order, deduplicated
// case-insensitively (the first spelling is the one returned).
std::vector<std::string> spelling_unknown_words(std::string_view text);

// One row for a task whose name has at least one unknown word.  `name` is the
// task's name as stored (or as typed in a pending Insert Task row);
// `unknown_words` is spelling_unknown_words(name).
struct SpellingIssue {
    std::uint32_t uid = 0;
    std::uint32_t id = 0;
    std::string name;
    std::vector<std::string> unknown_words;
};

// Every task name with at least one unknown word, in plan order.  Task rows
// only: PlanInput carries no project-summary row, and a row with an empty name
// has no words to check.  A pending Insert Task row that the plan carries IS
// checked (its name is visible in the sheet even though it has no file row).
std::vector<SpellingIssue> spelling_issues(const PlanInput& plan);

// The number of task rows spelling_issues() examined (those with a non-empty
// name), for the dialog's "N task name(s) checked" line.
std::size_t spelling_names_checked(const PlanInput& plan);

}  // namespace app
