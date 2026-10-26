// app/spelling.cpp -- see spelling.h.

#include "spelling.h"

#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "layout_model.h"  // app::PlanInput, app::TaskRow
#include "spelling_words.h"

namespace app {
namespace {

bool ascii_letter(unsigned char byte) noexcept {
    return (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z');
}

char ascii_lower(unsigned char byte) noexcept {
    return byte >= 'A' && byte <= 'Z' ? static_cast<char>(byte - 'A' + 'a')
                                      : static_cast<char>(byte);
}

std::string lower_ascii(std::string_view word) {
    std::string out;
    out.reserve(word.size());
    for (const unsigned char byte : word) {
        out.push_back(ascii_lower(byte));
    }
    return out;
}

// The explicit list, split out of the raw literal once.  The string_views point
// into kSpellingWords' static storage, so they stay valid for the process.
const std::unordered_set<std::string_view>& words() {
    static const std::unordered_set<std::string_view> list = [] {
        std::unordered_set<std::string_view> set;
        std::string_view text = kSpellingWords;
        std::size_t start = 0;
        while (start < text.size()) {
            const std::size_t end = text.find('\n', start);
            const std::size_t stop = end == std::string_view::npos ? text.size() : end;
            const std::string_view word = text.substr(start, stop - start);
            if (!word.empty()) {
                set.insert(word);
            }
            if (end == std::string_view::npos) {
                break;
            }
            start = end + 1;
        }
        return set;
    }();
    return list;
}

bool listed(std::string_view word) noexcept {
    return !word.empty() && words().find(word) != words().end();
}

bool doubled_final_consonant(std::string_view stem) noexcept {
    if (stem.size() < 2 || stem.back() != stem[stem.size() - 2]) {
        return false;
    }
    const char last = stem.back();
    // A doubled silent e ("agreed" -> "agree" + "d") is not the consonant
    // doubling the -ed/-ing rules have to undo.
    return last != 'e' && last != 'a' && last != 'i' && last != 'o' && last != 'u';
}

// The regular inflections of an explicitly listed stem.  No rule recurses, and
// no rule changes the stem beyond these measured English spellings, so an
// unknown stem stays unknown.
bool regular_form_known(std::string_view word) {
    if (word.size() >= 3 && word.ends_with('s')) {
        if (listed(word.substr(0, word.size() - 1))) {
            return true;
        }
        if (word.size() >= 4 && word.ends_with("es") &&
            listed(word.substr(0, word.size() - 2))) {
            return true;
        }
        if (word.size() >= 4 && word.ends_with("ies")) {
            std::string stem(word.substr(0, word.size() - 3));
            stem.push_back('y');
            if (listed(stem)) {
                return true;
            }
        }
    }
    if (word.size() >= 4 && (word.ends_with("ed") || word.ends_with("er"))) {
        const std::string_view stem = word.substr(0, word.size() - 2);
        if (listed(stem)) {
            return true;
        }
        std::string with_e(stem);
        with_e.push_back('e');
        if (listed(with_e)) {
            return true;
        }
        if (doubled_final_consonant(stem) &&
            listed(stem.substr(0, stem.size() - 1))) {
            return true;
        }
    }
    if (word.size() >= 5 && (word.ends_with("ing") || word.ends_with("est"))) {
        const std::string_view stem = word.substr(0, word.size() - 3);
        if (listed(stem)) {
            return true;
        }
        std::string with_e(stem);
        with_e.push_back('e');
        if (listed(with_e)) {
            return true;
        }
        if (doubled_final_consonant(stem) &&
            listed(stem.substr(0, stem.size() - 1))) {
            return true;
        }
    }
    if (word.size() >= 5 && word.ends_with("ly")) {
        const std::string_view stem = word.substr(0, word.size() - 2);
        if (listed(stem)) {
            return true;
        }
        if (stem.size() >= 2 && stem.back() == 'i') {
            std::string with_y(stem.substr(0, stem.size() - 1));
            with_y.push_back('y');
            if (listed(with_y)) {
                return true;
            }
        }
        std::string with_e(stem);
        with_e.push_back('e');
        if (listed(with_e)) {
            return true;
        }
    }
    return false;
}

bool known_word(std::string_view word) {
    if (listed(word) || regular_form_known(word)) {
        return true;
    }
    // A possessive is the stem plus "'s"; the stem may itself be a regular
    // inflection ("stakeholders'" loses only the apostrophe, "task's" loses
    // both characters).
    if (word.size() >= 3 && word.ends_with("'s")) {
        const std::string_view stem = word.substr(0, word.size() - 2);
        return listed(stem) || regular_form_known(stem);
    }
    if (word.size() >= 2 && word.back() == '\'') {
        const std::string_view stem = word.substr(0, word.size() - 1);
        return listed(stem) || regular_form_known(stem);
    }
    return false;
}

}  // namespace

std::size_t spelling_word_count() {
    return words().size();
}

bool spelling_word_known(std::string_view word) {
    if (word.empty()) {
        return false;
    }
    for (const unsigned char byte : word) {
        if (byte >= 0x80) {
            return false;
        }
    }
    return known_word(lower_ascii(word));
}

std::vector<std::string> spelling_unknown_words(std::string_view text) {
    std::vector<std::string> unknown;
    std::unordered_set<std::string> seen;
    std::size_t position = 0;
    while (position < text.size()) {
        const auto byte = static_cast<unsigned char>(text[position]);
        if (!ascii_letter(byte) && byte != '\'' && byte < 0x80) {
            ++position;
            continue;
        }
        const std::size_t start = position;
        bool non_ascii = false;
        while (position < text.size()) {
            const auto current = static_cast<unsigned char>(text[position]);
            if (current >= 0x80) {
                non_ascii = true;
                ++position;
                continue;
            }
            if (!ascii_letter(current) && current != '\'') {
                break;
            }
            ++position;
        }
        std::size_t begin = start;
        std::size_t end = position;
        while (begin < end && text[begin] == '\'') {
            ++begin;
        }
        while (end > begin && text[end - 1] == '\'') {
            --end;
        }
        // A one-letter token is a label, an initial or a list marker, not a
        // spelling; a non-ASCII token belongs to a list this port does not
        // carry, so neither is judged.
        if (non_ascii || end - begin <= 1) {
            continue;
        }
        const std::string word = lower_ascii(text.substr(begin, end - begin));
        if (known_word(word)) {
            continue;
        }
        if (seen.insert(word).second) {
            unknown.push_back(std::string(text.substr(begin, end - begin)));
        }
    }
    return unknown;
}

std::vector<SpellingIssue> spelling_issues(const PlanInput& plan) {
    std::vector<SpellingIssue> issues;
    for (const TaskRow& task : plan.tasks) {
        if (task.name.empty()) {
            continue;
        }
        std::vector<std::string> unknown = spelling_unknown_words(task.name);
        if (unknown.empty()) {
            continue;
        }
        SpellingIssue issue;
        issue.uid = task.uid;
        issue.id = task.id;
        issue.name = task.name;
        issue.unknown_words = std::move(unknown);
        issues.push_back(std::move(issue));
    }
    return issues;
}

std::size_t spelling_names_checked(const PlanInput& plan) {
    std::size_t checked = 0;
    for (const TaskRow& task : plan.tasks) {
        if (!task.name.empty()) {
            ++checked;
        }
    }
    return checked;
}

}  // namespace app
