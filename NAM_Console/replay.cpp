#include "replay.h"

namespace nam::console {

namespace {

// The replay letter for a direction. Exactly the four plain-mode movement keys,
// so an encoded string is also a valid sequence of plain commands.
[[nodiscard]] char letter_of(Direction direction) noexcept {
    switch (direction) {
        case Direction::up:    return 'w';
        case Direction::down:  return 's';
        case Direction::left:  return 'a';
        case Direction::right: return 'd';
    }
    // Exhaustive switch above; the fallback keeps the function total for every
    // compiler in the portability baseline.
    return 'w';
}

[[nodiscard]] std::optional<Direction> direction_of_letter(char letter) noexcept {
    switch (letter) {
        case 'w': return Direction::up;
        case 's': return Direction::down;
        case 'a': return Direction::left;
        case 'd': return Direction::right;
        default:  return std::nullopt;
    }
}

}  // namespace

std::string encode_replay(const std::vector<Direction>& moves) {
    std::string text;
    std::size_t index = 0;
    while (index < moves.size()) {
        const Direction direction = moves[index];
        std::size_t run = 1;
        // A run is capped so no single token can exceed what the decoder accepts;
        // a longer stretch simply continues in the next token.
        while (index + run < moves.size() && moves[index + run] == direction &&
               run < replay_max_repeat) {
            ++run;
        }
        // A run of one is written as the bare letter, which keeps a short record
        // readable and makes a single command encode to itself.
        if (run > 1) {
            text += std::to_string(run);
        }
        text.push_back(letter_of(direction));
        index += run;
    }
    return text;
}

std::optional<std::vector<Direction>> decode_replay(const std::string& text) {
    if (text.empty()) return std::nullopt;

    std::vector<Direction> moves;
    std::size_t index = 0;
    while (index < text.size()) {
        std::size_t count = 0;
        bool counted = false;
        while (index < text.size() && text[index] >= '0' && text[index] <= '9') {
            counted = true;
            count = count * 10u + static_cast<std::size_t>(text[index] - '0');
            // Checked as it is built rather than after, so a long digit run cannot
            // overflow before it is rejected.
            if (count > replay_max_repeat) return std::nullopt;
            ++index;
        }
        // A digit run must name a direction, and a count of zero records nothing
        // and so cannot have been produced by encoding a real run.
        if (counted && count == 0) return std::nullopt;
        if (index >= text.size()) return std::nullopt;

        const std::optional<Direction> direction = direction_of_letter(text[index]);
        if (!direction) return std::nullopt;
        ++index;

        const std::size_t repeat = counted ? count : 1u;
        if (moves.size() + repeat > replay_max_moves) return std::nullopt;
        moves.insert(moves.end(), repeat, *direction);
    }
    return moves;
}

}  // namespace nam::console
