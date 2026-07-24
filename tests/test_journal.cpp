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

GameEvent rest_event(std::uint64_t sequence, std::uint32_t before, std::uint32_t recovered,
                     std::uint32_t after) {
    return GameEvent{sequence, RestedEvent{before, recovered, after}};
}

// Convenience: render every entry as prose in order for deterministic comparison.
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

TEST_CASE("matching adjacent moves merge into one travel entry") {
    Journal journal;
    journal.record_event(move_event(0, Direction::right, Terrain::open, 1), "Beacon");
    journal.record_event(move_event(1, Direction::right, Terrain::open, 1), "Beacon");
    journal.record_event(move_event(2, Direction::right, Terrain::open, 1), "Beacon");

    REQUIRE(journal.size() == 1);
    const auto* travel = std::get_if<TravelEntry>(&journal.entries().front().data);
    REQUIRE(travel != nullptr);
    CHECK(travel->direction == Direction::right);
    CHECK(travel->terrain == Terrain::open);
    CHECK(travel->steps == 3);
    CHECK(travel->first_sequence == 0);
    CHECK(travel->last_sequence == 2);
    CHECK(travel->stamina_spent == 3);
}

TEST_CASE("a travel entry accumulates stamina across mixed step costs of one terrain") {
    Journal journal;
    journal.record_event(move_event(0, Direction::up, Terrain::hill, 2), "Beacon");
    journal.record_event(move_event(1, Direction::up, Terrain::hill, 2), "Beacon");

    REQUIRE(journal.size() == 1);
    const auto* travel = std::get_if<TravelEntry>(&journal.entries().front().data);
    REQUIRE(travel != nullptr);
    CHECK(travel->steps == 2);
    CHECK(travel->stamina_spent == 4);
}

TEST_CASE("a direction change starts a new travel entry") {
    Journal journal;
    journal.record_event(move_event(0, Direction::right, Terrain::open, 1), "Beacon");
    journal.record_event(move_event(1, Direction::up, Terrain::open, 1), "Beacon");

    REQUIRE(journal.size() == 2);
    const auto* first = std::get_if<TravelEntry>(&journal.entries()[0].data);
    const auto* second = std::get_if<TravelEntry>(&journal.entries()[1].data);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    CHECK(first->direction == Direction::right);
    CHECK(second->direction == Direction::up);
    CHECK(second->steps == 1);
}

TEST_CASE("a terrain change starts a new travel entry") {
    Journal journal;
    journal.record_event(move_event(0, Direction::right, Terrain::open, 1), "Beacon");
    journal.record_event(move_event(1, Direction::right, Terrain::fields, 2), "Beacon");

    REQUIRE(journal.size() == 2);
    const auto* first = std::get_if<TravelEntry>(&journal.entries()[0].data);
    const auto* second = std::get_if<TravelEntry>(&journal.entries()[1].data);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    CHECK(first->terrain == Terrain::open);
    CHECK(second->terrain == Terrain::fields);
}

TEST_CASE("a blocked attempt creates no entry but breaks grouping") {
    Journal journal;
    journal.record_event(move_event(0, Direction::right, Terrain::open, 1), "Beacon");
    journal.record_event(blocked_event(1, Direction::right), "Beacon");
    journal.record_event(move_event(2, Direction::right, Terrain::open, 1), "Beacon");

    REQUIRE(journal.size() == 2);
    const auto* first = std::get_if<TravelEntry>(&journal.entries()[0].data);
    const auto* second = std::get_if<TravelEntry>(&journal.entries()[1].data);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    CHECK(first->steps == 1);
    CHECK(first->last_sequence == 0);
    CHECK(second->steps == 1);
    CHECK(second->first_sequence == 2);
}

TEST_CASE("every blocked result kind breaks grouping without an entry") {
    for (const MoveResult result : {MoveResult::blocked_by_boundary, MoveResult::blocked_by_terrain,
                                    MoveResult::blocked_by_stamina}) {
        Journal journal;
        journal.record_event(move_event(0, Direction::right, Terrain::open, 1), "Beacon");
        journal.record_event(blocked_event(1, Direction::right, result), "Beacon");
        journal.record_event(move_event(2, Direction::right, Terrain::open, 1), "Beacon");
        CHECK(journal.size() == 2);
    }
}

TEST_CASE("a rest creates one entry and breaks grouping") {
    Journal journal;
    journal.record_event(move_event(0, Direction::right, Terrain::open, 1), "Beacon");
    journal.record_event(rest_event(1, 4, 3, 7), "Beacon");
    journal.record_event(move_event(2, Direction::right, Terrain::open, 1), "Beacon");

    REQUIRE(journal.size() == 3);
    const auto* rest = std::get_if<RestEntry>(&journal.entries()[1].data);
    REQUIRE(rest != nullptr);
    CHECK(rest->sequence == 1);
    CHECK(rest->stamina_before == 4);
    CHECK(rest->stamina_recovered == 3);
    CHECK(rest->stamina_after == 7);
}

TEST_CASE("a rest at full stamina still creates an entry") {
    Journal journal;
    journal.record_event(rest_event(0, 12, 0, 12), "Beacon");

    REQUIRE(journal.size() == 1);
    const auto* rest = std::get_if<RestEntry>(&journal.entries()[0].data);
    REQUIRE(rest != nullptr);
    CHECK(rest->stamina_recovered == 0);
    CHECK(rest->stamina_before == 12);
    CHECK(rest->stamina_after == 12);
}

TEST_CASE("a discovering move merges into travel then appends a discovery entry") {
    Journal journal;
    journal.record_event(move_event(0, Direction::right, Terrain::open, 1), "North Ridge");
    journal.record_event(
        move_event(1, Direction::right, Terrain::open, 1, ObjectiveTransition::beacon_discovered),
        "North Ridge");

    REQUIRE(journal.size() == 2);
    const auto* travel = std::get_if<TravelEntry>(&journal.entries()[0].data);
    const auto* discovery = std::get_if<DiscoveryEntry>(&journal.entries()[1].data);
    REQUIRE(travel != nullptr);
    REQUIRE(discovery != nullptr);
    CHECK(travel->steps == 2);
    CHECK(travel->last_sequence == 1);
    CHECK(discovery->sequence == 1);
    CHECK(discovery->beacon_name == "North Ridge");
}

TEST_CASE("an objective entry breaks grouping so the next move starts a new travel entry") {
    Journal journal;
    journal.record_event(
        move_event(0, Direction::right, Terrain::open, 1, ObjectiveTransition::beacon_discovered),
        "North Ridge");
    journal.record_event(move_event(1, Direction::right, Terrain::open, 1), "North Ridge");

    REQUIRE(journal.size() == 3);
    CHECK(std::holds_alternative<TravelEntry>(journal.entries()[0].data));
    CHECK(std::holds_alternative<DiscoveryEntry>(journal.entries()[1].data));
    const auto* travel = std::get_if<TravelEntry>(&journal.entries()[2].data);
    REQUIRE(travel != nullptr);
    CHECK(travel->steps == 1);
    CHECK(travel->first_sequence == 1);
}

TEST_CASE("a completing move merges into travel then appends a completion entry") {
    Journal journal;
    journal.record_event(move_event(0, Direction::left, Terrain::open, 1), "North Ridge");
    journal.record_event(move_event(1, Direction::left, Terrain::open, 1,
                                    ObjectiveTransition::expedition_completed),
                         "North Ridge");

    REQUIRE(journal.size() == 2);
    const auto* travel = std::get_if<TravelEntry>(&journal.entries()[0].data);
    const auto* completion = std::get_if<CompletionEntry>(&journal.entries()[1].data);
    REQUIRE(travel != nullptr);
    REQUIRE(completion != nullptr);
    CHECK(travel->steps == 2);
    CHECK(completion->sequence == 1);
    CHECK(completion->beacon_name == "North Ridge");
}

TEST_CASE("initial completion creates exactly one special entry") {
    Journal journal;
    journal.record_initial_completion("Spawn Cairn");

    REQUIRE(journal.size() == 1);
    const auto* initial = std::get_if<InitialCompletionEntry>(&journal.entries()[0].data);
    REQUIRE(initial != nullptr);
    CHECK(initial->beacon_name == "Spawn Cairn");
}

TEST_CASE("travel prose uses exact singular and plural grammar") {
    Journal one;
    one.record_event(move_event(0, Direction::right, Terrain::open, 1), "Beacon");
    CHECK(format_entry(one.entries().front()) ==
          std::string("Traveled east across open ground for 1 step."));

    Journal many;
    many.record_event(move_event(0, Direction::right, Terrain::open, 1), "Beacon");
    many.record_event(move_event(1, Direction::right, Terrain::open, 1), "Beacon");
    many.record_event(move_event(2, Direction::right, Terrain::open, 1), "Beacon");
    CHECK(format_entry(many.entries().front()) ==
          std::string("Traveled east across open ground for 3 steps."));
}

TEST_CASE("travel prose maps every direction to a cardinal name") {
    Journal journal;
    journal.record_event(move_event(0, Direction::up, Terrain::open, 1), "Beacon");
    journal.record_event(move_event(1, Direction::down, Terrain::open, 1), "Beacon");
    journal.record_event(move_event(2, Direction::left, Terrain::open, 1), "Beacon");
    journal.record_event(move_event(3, Direction::right, Terrain::open, 1), "Beacon");

    const std::vector<std::string> lines = prose_of(journal);
    REQUIRE(lines.size() == 4);
    CHECK(lines[0] == std::string("Traveled north across open ground for 1 step."));
    CHECK(lines[1] == std::string("Traveled south across open ground for 1 step."));
    CHECK(lines[2] == std::string("Traveled west across open ground for 1 step."));
    CHECK(lines[3] == std::string("Traveled east across open ground for 1 step."));
}

TEST_CASE("rest prose is exact for recovery and for full stamina") {
    Journal recovered;
    recovered.record_event(rest_event(0, 4, 3, 7), "Beacon");
    CHECK(format_entry(recovered.entries().front()) ==
          std::string("Rested and recovered 3 stamina."));

    Journal full;
    full.record_event(rest_event(0, 12, 0, 12), "Beacon");
    CHECK(format_entry(full.entries().front()) ==
          std::string("Rested, but stamina was already full."));
}

TEST_CASE("discovery and completion prose name the beacon exactly") {
    Journal discovery;
    discovery.record_event(
        move_event(0, Direction::right, Terrain::open, 1, ObjectiveTransition::beacon_discovered),
        "North Ridge");
    CHECK(format_entry(discovery.entries().back()) == std::string("Discovered North Ridge."));

    Journal completion;
    completion.record_event(move_event(0, Direction::left, Terrain::open, 1,
                                       ObjectiveTransition::expedition_completed),
                            "North Ridge");
    CHECK(format_entry(completion.entries().back()) ==
          std::string("Returned to spawn after reaching North Ridge; expedition complete."));
}

TEST_CASE("initial completion prose is exact") {
    Journal journal;
    journal.record_initial_completion("Spawn Cairn");
    CHECK(format_entry(journal.entries().front()) ==
          std::string("Found Spawn Cairn at spawn; the expedition was already complete."));
}

TEST_CASE("repeated identical scripts produce byte-identical prose and structure") {
    const auto build = [] {
        Journal journal;
        journal.record_event(move_event(0, Direction::right, Terrain::open, 1), "North Ridge");
        journal.record_event(move_event(1, Direction::right, Terrain::open, 1), "North Ridge");
        journal.record_event(blocked_event(2, Direction::right), "North Ridge");
        journal.record_event(rest_event(3, 8, 4, 12), "North Ridge");
        journal.record_event(move_event(4, Direction::up, Terrain::hill, 2,
                                        ObjectiveTransition::beacon_discovered),
                             "North Ridge");
        journal.record_event(move_event(5, Direction::down, Terrain::hill, 2,
                                        ObjectiveTransition::expedition_completed),
                             "North Ridge");
        return journal;
    };

    const Journal first = build();
    const Journal second = build();
    CHECK(first.size() == second.size());
    CHECK(prose_of(first) == prose_of(second));

    const std::vector<std::string> expected{
        "Traveled east across open ground for 2 steps.",
        "Rested and recovered 4 stamina.",
        "Traveled north across hill for 1 step.",
        "Discovered North Ridge.",
        "Traveled south across hill for 1 step.",
        "Returned to spawn after reaching North Ridge; expedition complete.",
    };
    CHECK(prose_of(first) == expected);
}

}  // TEST_SUITE("journal")
