#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "direction.h"
#include "game_event.h"
#include "journal.h"
#include "move_outcome.h"
#include "objective.h"
#include "terrain.h"

using namespace nam::console;

namespace {

GameEvent move_event(std::uint64_t sequence, Direction direction, Terrain terrain,
                     std::uint32_t cost,
                     ObjectiveTransition transition = ObjectiveTransition::none) {
    MoveAttemptedEvent move;
    move.direction = direction;
    move.outcome.result = MoveResult::moved;
    move.outcome.terrain = terrain;
    move.outcome.stamina_cost = cost;
    move.objective_update.transition = transition;
    return GameEvent{sequence, move};
}

GameEvent blocked_event(std::uint64_t sequence, Direction direction,
                        MoveResult result = MoveResult::blocked_by_terrain) {
    MoveAttemptedEvent move;
    move.direction = direction;
    move.outcome.result = result;
    return GameEvent{sequence, move};
}

// Every journal entry rendered through the public formatter, in order.
std::vector<std::string> prose_of(const Journal& journal) {
    std::vector<std::string> lines;
    for (const JournalEntry& entry : journal.entries()) {
        lines.push_back(format_entry(entry));
    }
    return lines;
}

}  // namespace

TEST_SUITE("journal") {

TEST_CASE("a new journal is empty") {
    Journal journal;
    CHECK(journal.empty());
    CHECK(journal.size() == 0);
    CHECK(journal.entries().empty());
}

TEST_CASE("routine movement never becomes a journal entry") {
    Journal journal;
    journal.record_event(move_event(0, Direction::right, Terrain::open, 1), "North Ridge");
    journal.record_event(move_event(1, Direction::right, Terrain::open, 1), "North Ridge");
    journal.record_event(move_event(2, Direction::up, Terrain::hill, 2), "North Ridge");
    journal.record_event(move_event(3, Direction::down, Terrain::fields, 2), "North Ridge");

    CHECK(journal.empty());
}

TEST_CASE("a blocked attempt never becomes a journal entry") {
    Journal journal;
    for (const MoveResult result : {MoveResult::blocked_by_boundary,
                                    MoveResult::blocked_by_terrain}) {
        journal.record_event(blocked_event(0, Direction::right, result), "North Ridge");
    }
    CHECK(journal.empty());
}

TEST_CASE("a discovering move appends exactly one discovery entry") {
    Journal journal;
    journal.record_event(move_event(0, Direction::right, Terrain::open, 1), "North Ridge");
    journal.record_event(
        move_event(1, Direction::right, Terrain::open, 1, ObjectiveTransition::landmark_discovered),
        "North Ridge");

    REQUIRE(journal.size() == 1);
    const auto* discovery = std::get_if<DiscoveryEntry>(&journal.entries().front().data);
    REQUIRE(discovery != nullptr);
    CHECK(discovery->sequence == 1);
    CHECK(discovery->landmark_name == std::string("North Ridge"));
}

TEST_CASE("a completing move appends exactly one completion entry") {
    Journal journal;
    journal.record_event(move_event(0, Direction::left, Terrain::open, 1), "North Ridge");
    journal.record_event(move_event(1, Direction::left, Terrain::open, 1,
                                    ObjectiveTransition::level_completed),
                         "North Ridge");

    REQUIRE(journal.size() == 1);
    const auto* completion = std::get_if<CompletionEntry>(&journal.entries().front().data);
    REQUIRE(completion != nullptr);
    CHECK(completion->sequence == 1);
    CHECK(completion->landmark_name == std::string("North Ridge"));
}

TEST_CASE("a full level produces exactly two milestone entries in order") {
    Journal journal;
    journal.record_event(move_event(0, Direction::right, Terrain::open, 1), "North Ridge");
    journal.record_event(
        move_event(1, Direction::right, Terrain::open, 1, ObjectiveTransition::landmark_discovered),
        "North Ridge");
    journal.record_event(move_event(2, Direction::right, Terrain::open, 1), "North Ridge");
    journal.record_event(
        move_event(3, Direction::right, Terrain::open, 1, ObjectiveTransition::level_completed),
        "North Ridge");

    REQUIRE(journal.size() == 2);
    CHECK(std::holds_alternative<DiscoveryEntry>(journal.entries()[0].data));
    CHECK(std::holds_alternative<CompletionEntry>(journal.entries()[1].data));
}

TEST_CASE("initial completion creates exactly one special entry") {
    Journal journal;
    journal.record_initial_completion("Spawn Cairn");

    REQUIRE(journal.size() == 1);
    const auto* initial = std::get_if<InitialCompletionEntry>(&journal.entries()[0].data);
    REQUIRE(initial != nullptr);
    CHECK(initial->landmark_name == std::string("Spawn Cairn"));
}

TEST_CASE("discovery and completion prose name the landmark exactly") {
    Journal discovery;
    discovery.record_event(
        move_event(0, Direction::right, Terrain::open, 1, ObjectiveTransition::landmark_discovered),
        "North Ridge");
    CHECK(format_entry(discovery.entries().back()) ==
          std::string("Discovered North Ridge; the exit direction was revealed."));

    Journal completion;
    completion.record_event(move_event(0, Direction::left, Terrain::open, 1,
                                       ObjectiveTransition::level_completed),
                            "North Ridge");
    CHECK(format_entry(completion.entries().back()) ==
          std::string("Reached the exit after North Ridge; level complete."));
}

TEST_CASE("initial completion prose is exact") {
    Journal journal;
    journal.record_initial_completion("Spawn Cairn");
    CHECK(format_entry(journal.entries().front()) ==
          std::string("Found Spawn Cairn and the exit at spawn; the level was already complete."));
}

TEST_CASE("repeated identical scripts produce byte-identical prose and structure") {
    const auto build = [] {
        Journal journal;
        journal.record_event(move_event(0, Direction::right, Terrain::open, 1), "North Ridge");
        journal.record_event(move_event(1, Direction::right, Terrain::open, 1), "North Ridge");
        journal.record_event(blocked_event(2, Direction::right), "North Ridge");
        journal.record_event(move_event(3, Direction::up, Terrain::hill, 2,
                                        ObjectiveTransition::landmark_discovered),
                             "North Ridge");
        journal.record_event(move_event(4, Direction::down, Terrain::hill, 2,
                                        ObjectiveTransition::level_completed),
                             "North Ridge");
        return journal;
    };

    const Journal first = build();
    const Journal second = build();
    CHECK(first.size() == second.size());
    CHECK(prose_of(first) == prose_of(second));

    const std::vector<std::string> expected{
        "Discovered North Ridge; the exit direction was revealed.",
        "Reached the exit after North Ridge; level complete.",
    };
    CHECK(prose_of(first) == expected);
}

}  // TEST_SUITE("journal")
