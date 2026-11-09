// tests/edl_damage.h -- shaped damage to a 214/CEdl/Var2Data stream, for the
// view-state writers' refusal tests.  Every helper keeps the stream parseable
// (the item count and the three size words stay honest), so the writer's own
// check for that damage is what a test reaches.  Test-only.
#ifndef TESTS_EDL_DAMAGE_H
#define TESTS_EDL_DAMAGE_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace edl_damage {

using Bytes = std::vector<std::uint8_t>;

constexpr std::size_t kCountWord = 16;
constexpr std::size_t kItemHeader = 12;

inline std::uint32_t get(Bytes const& b, std::size_t at) {
    return static_cast<std::uint32_t>(b[at]) | (static_cast<std::uint32_t>(b[at + 1]) << 8) |
           (static_cast<std::uint32_t>(b[at + 2]) << 16) |
           (static_cast<std::uint32_t>(b[at + 3]) << 24);
}

inline void put(Bytes& b, std::size_t at, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        b[at + i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu);
    }
}

/* The three CEdl size words follow the stream length. */
inline void fix_size_words(Bytes& e) {
    put(e, 0, static_cast<std::uint32_t>(e.size() - 4));
    put(e, 4, static_cast<std::uint32_t>(e.size() - 8));
    put(e, 8, static_cast<std::uint32_t>(e.size() - 8));
}

/* A length word that is only changed misparses the list; to reach a writer's own
 * length check the item is resized with the stream. */
inline void resize_item(Bytes& e, std::size_t item, std::size_t new_length) {
    std::size_t const old_length = get(e, item);
    e.erase(e.begin() + static_cast<std::ptrdiff_t>(item + kItemHeader),
            e.begin() + static_cast<std::ptrdiff_t>(item + kItemHeader + old_length));
    e.insert(e.begin() + static_cast<std::ptrdiff_t>(item + kItemHeader), new_length, 0);
    put(e, item, static_cast<std::uint32_t>(new_length));
    fix_size_words(e);
}

/* The item twice in a row, with the item count and the size words kept honest. */
inline void duplicate_item(Bytes& e, std::size_t item) {
    std::size_t const length = get(e, item);
    std::size_t const total = kItemHeader + length + (length % 2);
    Bytes const copy(e.begin() + static_cast<std::ptrdiff_t>(item),
                     e.begin() + static_cast<std::ptrdiff_t>(item + total));
    e.insert(e.begin() + static_cast<std::ptrdiff_t>(item + total), copy.begin(), copy.end());
    put(e, kCountWord, get(e, kCountWord) + 1);
    fix_size_words(e);
}

/* The 0-based index of the item whose header is at `item`. */
inline std::size_t item_index(Bytes const& e, std::size_t item) {
    std::size_t index = 0;
    for (std::size_t at = 20; at < item; ++index) {
        std::size_t const length = get(e, at);
        at += kItemHeader + length + (length % 2);
    }
    return index;
}

}  // namespace edl_damage

#endif  // TESTS_EDL_DAMAGE_H
