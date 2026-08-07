#include "replay.h"

#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "direction.h"

using namespace nam::console;

namespace {

// The directions a compact string stands for, so a case can state the expected
// expansion without spelling out every element.
[[nodiscard]] std::vector<Direction> expand(const std::string& letters, std::size_t repeat = 1) {
    std::vector<Direction> moves;
    for (const char letter : letters) {
        Direction direction = Direction::up;
        if (letter == 's') direction = Direction::down;
        if (letter == 'a') direction = Direction::left;
        if (letter == 'd') direction = Direction::right;
        moves.insert(moves.end(), repeat, direction);
    }
    return moves;
}

}  // namespace

TEST_SUITE("replay") {

TEST_CASE("a run of one direction encodes to the bare letter") {
    CHECK(encode_replay(expand("w")) == "w");
    CHECK(encode_replay(expand("wsad")) == "wsad");
}

TEST_CASE("a repeated direction encodes to a count and one letter") {
    CHECK(encode_replay(expand("d", 4)) == "4d");
    CHECK(encode_replay(expand("s", 12)) == "12s");
}

TEST_CASE("mixed runs encode each stretch separately") {
    std::vector<Direction> moves = expand("d", 4);
    const std::vector<Direction> downs = expand("s", 3);
    moves.insert(moves.end(), downs.begin(), downs.end());
    moves.push_back(Direction::up);

    CHECK(encode_replay(moves) == "4d3sw");
}

TEST_CASE("an empty run encodes to an empty string") {
    CHECK(encode_replay(std::vector<Direction>{}).empty());
}

TEST_CASE("decoding an encoded run returns exactly the run") {
    // A deterministic pseudo-walk with runs of every length from 1 upwards, so the
    // round trip covers bare letters and counted tokens in the same string.
    std::vector<Direction> moves;
    const Direction cycle[] = {Direction::up, Direction::right, Direction::down, Direction::left};
    for (std::size_t run = 1; run <= 20; ++run) {
        moves.insert(moves.end(), run, cycle[run % 4u]);
    }

    const std::string encoded = encode_replay(moves);
    const std::optional<std::vector<Direction>> decoded = decode_replay(encoded);

    REQUIRE(decoded.has_value());
    CHECK(*decoded == moves);
}

TEST_CASE("a decoded string is independent of how its runs were split") {
    const std::optional<std::vector<Direction>> counted = decode_replay("3d");
    const std::optional<std::vector<Direction>> spelled = decode_replay("ddd");
    const std::optional<std::vector<Direction>> mixed = decode_replay("2dd");

    REQUIRE(counted.has_value());
    REQUIRE(spelled.has_value());
    REQUIRE(mixed.has_value());
    CHECK(*counted == expand("d", 3));
    CHECK(*counted == *spelled);
    CHECK(*counted == *mixed);
}

TEST_CASE("a very long stretch survives the round trip in several tokens") {
    const std::vector<Direction> moves(replay_max_repeat + 250u, Direction::left);

    const std::optional<std::vector<Direction>> decoded = decode_replay(encode_replay(moves));

    REQUIRE(decoded.has_value());
    CHECK(decoded->size() == moves.size());
    CHECK(*decoded == moves);
}

TEST_CASE("a word that is not a replay is rejected rather than played") {
    // Every one of these would move the actor if any character were played, so a
    // typo must be refused whole rather than partly obeyed.
    CHECK_FALSE(decode_replay("hello").has_value());
    CHECK_FALSE(decode_replay("save").has_value());
    CHECK_FALSE(decode_replay("wasd!").has_value());
    CHECK_FALSE(decode_replay("w a").has_value());
    CHECK_FALSE(decode_replay("-3d").has_value());
}

TEST_CASE("a count without a direction is rejected") {
    CHECK_FALSE(decode_replay("12").has_value());
    CHECK_FALSE(decode_replay("3d5").has_value());
}

TEST_CASE("a count of zero is rejected") {
    CHECK_FALSE(decode_replay("0d").has_value());
    CHECK_FALSE(decode_replay("3s0w").has_value());
}

TEST_CASE("a count above the token limit is rejected") {
    CHECK(decode_replay(std::to_string(replay_max_repeat) + "d").has_value());
    CHECK_FALSE(decode_replay(std::to_string(replay_max_repeat + 1u) + "d").has_value());
    // A long digit run is refused as it is read rather than after it overflows.
    CHECK_FALSE(decode_replay("99999999999999999999999d").has_value());
}

TEST_CASE("an expansion above the move limit is rejected") {
    std::string text;
    std::size_t total = 0;
    while (total <= replay_max_moves) {
        text += std::to_string(replay_max_repeat) + "d";
        total += replay_max_repeat;
    }

    CHECK_FALSE(decode_replay(text).has_value());
}

TEST_CASE("an empty string is not a replay") {
    CHECK_FALSE(decode_replay("").has_value());
}

}  // TEST_SUITE(replay)
