#include <doctest/doctest.h>

#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

#include "direction.h"
#include "game_event.h"
#include "game_state.h"
#include "map.h"
#include "map_parser.h"
#include "move_outcome.h"
#include "visibility.h"

namespace {

Map make_map(std::string_view text) {
    MapLoadResult result = load_map(text);
    REQUIRE(std::holds_alternative<Map>(result));
    return std::get<Map>(std::move(result));
}

constexpr std::string_view kMap =
    "NAM-MAP 1\nwidth 5\nheight 5\nspawn 2 2\n---\n.....\n.=.=.\n.....\n.=.=.\n.....\n";

// A scripted command: either a movement in a direction, or a rest in place.
struct Command {
    bool is_rest = false;
    Direction direction = Direction::up;
};

Command move_cmd(Direction direction) { return Command{false, direction}; }
Command rest_cmd() { return Command{true, Direction::up}; }

GameEvent apply(GameState& state, const Command& command) {
    return command.is_rest ? state.rest() : state.move(command.direction);
}

// A flat, one-character-per-cell signature of visibility, so two games can be
// compared byte-for-byte across a mixed script.
std::string visibility_signature(const GameState& state) {
    const VisibilityMap& visibility = state.visibility();
    std::string signature;
    for (int y = 0; y < static_cast<int>(visibility.height()); ++y) {
        for (int x = 0; x < static_cast<int>(visibility.width()); ++x) {
            signature.push_back(static_cast<char>('0' +
                static_cast<int>(visibility.at(Coordinates{x, y}))));
        }
    }
    return signature;
}

}  // namespace

TEST_SUITE("game") {

TEST_CASE("identical map and input produce identical event streams") {
    const std::vector<Direction> script{
        Direction::up,   Direction::up,    Direction::left, Direction::down,
        Direction::right, Direction::right, Direction::down, Direction::left};

    GameState a(make_map(kMap));
    GameState b(make_map(kMap));

    std::uint64_t expected_sequence = 0;
    for (const Direction direction : script) {
        const GameEvent ea = a.move(direction);
        const GameEvent eb = b.move(direction);
        const MoveAttemptedEvent& pa = std::get<MoveAttemptedEvent>(ea.data);
        const MoveAttemptedEvent& pb = std::get<MoveAttemptedEvent>(eb.data);

        // Sequence numbers advance contiguously and identically on both games.
        CHECK(ea.sequence == expected_sequence);
        CHECK(eb.sequence == expected_sequence);
        ++expected_sequence;

        CHECK(pa.direction == direction);
        CHECK(pa.direction == pb.direction);
        CHECK(pa.outcome.result == pb.outcome.result);
        CHECK(pa.outcome.from == pb.outcome.from);
        CHECK(pa.outcome.to == pb.outcome.to);
        CHECK(pa.outcome.terrain == pb.outcome.terrain);
        CHECK(pa.outcome.stamina_cost == pb.outcome.stamina_cost);
        CHECK(pa.outcome.stamina_before == pb.outcome.stamina_before);
        CHECK(pa.outcome.stamina_after == pb.outcome.stamina_after);

        // Current stamina stays identical after every command in both games.
        CHECK(a.stamina() == b.stamina());
    }

    CHECK(a.actor_position() == b.actor_position());
    CHECK(a.stamina() == b.stamina());
    CHECK(a.render() == b.render());
}

TEST_CASE("identical mixed movement and rest scripts produce identical results") {
    const std::vector<Command> script{
        move_cmd(Direction::up),    rest_cmd(),
        move_cmd(Direction::left),  move_cmd(Direction::down),
        rest_cmd(),                 move_cmd(Direction::right),
        move_cmd(Direction::right), rest_cmd()};

    GameState a(make_map(kMap));
    GameState b(make_map(kMap));

    std::uint64_t expected_sequence = 0;
    for (const Command& command : script) {
        const GameEvent ea = apply(a, command);
        const GameEvent eb = apply(b, command);

        CHECK(ea.sequence == expected_sequence);
        CHECK(eb.sequence == expected_sequence);
        ++expected_sequence;

        // The active variant alternative is identical on both games.
        CHECK(ea.data.index() == eb.data.index());

        if (command.is_rest) {
            const RestedEvent& ra = std::get<RestedEvent>(ea.data);
            const RestedEvent& rb = std::get<RestedEvent>(eb.data);
            CHECK(ra.stamina_before == rb.stamina_before);
            CHECK(ra.stamina_recovered == rb.stamina_recovered);
            CHECK(ra.stamina_after == rb.stamina_after);
        } else {
            const MoveAttemptedEvent& pa = std::get<MoveAttemptedEvent>(ea.data);
            const MoveAttemptedEvent& pb = std::get<MoveAttemptedEvent>(eb.data);
            CHECK(pa.direction == pb.direction);
            CHECK(pa.outcome.result == pb.outcome.result);
            CHECK(pa.outcome.from == pb.outcome.from);
            CHECK(pa.outcome.to == pb.outcome.to);
            CHECK(pa.outcome.terrain == pb.outcome.terrain);
            CHECK(pa.outcome.stamina_cost == pb.outcome.stamina_cost);
            CHECK(pa.outcome.stamina_before == pb.outcome.stamina_before);
            CHECK(pa.outcome.stamina_after == pb.outcome.stamina_after);
        }

        CHECK(a.stamina() == b.stamina());
        CHECK(a.actor_position() == b.actor_position());
        CHECK(visibility_signature(a) == visibility_signature(b));
    }

    CHECK(a.actor_position() == b.actor_position());
    CHECK(a.stamina() == b.stamina());
    CHECK(a.render() == b.render());
    CHECK(visibility_signature(a) == visibility_signature(b));
}

TEST_CASE("peek is a pure function of state and direction") {
    GameState state(make_map(kMap));
    const MoveOutcome first = state.peek(Direction::up);
    const MoveOutcome second = state.peek(Direction::up);
    CHECK(first.result == second.result);
    CHECK(first.to == second.to);
    CHECK(first.terrain == second.terrain);
    // The repeated peeks left the actor untouched.
    CHECK(state.actor_position() == Coordinates{2, 2});
}

}  // TEST_SUITE("game")
