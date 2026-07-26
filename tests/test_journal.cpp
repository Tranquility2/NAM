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

GameEvent rest_event(std::uint64_t sequence, RestResult result, Terrain terrain,
                     std::uint32_t stamina_before, std::uint32_t stamina_after,
                     std::uint32_t stamina_recovered, std::uint32_t provisions_before,
                     std::uint32_t provisions_after) {
    return GameEvent{sequence,
                     RestedEvent{result, terrain, stamina_before, stamina_after,
                                 stamina_recovered, provisions_before, provisions_after}};
}

GameEvent camp_event(std::uint64_t sequence, CampResult result, CampKind kind, Terrain terrain,
                     std::uint32_t day_before, std::uint32_t day_after,
                     std::uint32_t stamina_after, std::uint32_t provisions_after) {
    CampedEvent camped;
    camped.result = result;
    camped.kind = kind;
    camped.terrain = terrain;
    camped.time.before.day = day_before;
    camped.time.after.day = day_after;
    camped.stamina_after = stamina_after;
    camped.provisions_after = provisions_after;
    return GameEvent{sequence, camped};
}

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

TEST_CASE("only recovered rests create an entry and break grouping") {
    Journal journal;
    journal.record_event(move_event(0, Direction::right, Terrain::open, 1), "Beacon");
    journal.record_event(
        rest_event(1, RestResult::recovered, Terrain::fields, 10, 16, 6, 3, 2), "Beacon");
    journal.record_event(move_event(2, Direction::right, Terrain::open, 1), "Beacon");

    REQUIRE(journal.size() == 3);
    const auto* rest = std::get_if<RestEntry>(&journal.entries()[1].data);
    REQUIRE(rest != nullptr);
    CHECK(rest->sequence == 1);
    CHECK(rest->terrain == Terrain::fields);
    CHECK(rest->stamina_before == 10);
    CHECK(rest->stamina_recovered == 6);
    CHECK(rest->stamina_after == 16);
    CHECK(rest->provisions_after == 2);
}

TEST_CASE("full and no-provisions rests create no entry and still break grouping") {
    Journal full;
    full.record_event(move_event(0, Direction::right, Terrain::open, 1), "Beacon");
    full.record_event(rest_event(1, RestResult::already_full, Terrain::open, 20, 20, 0, 1, 1),
                      "Beacon");
    full.record_event(move_event(2, Direction::right, Terrain::open, 1), "Beacon");
    CHECK(full.size() == 2);

    Journal none;
    none.record_event(move_event(0, Direction::right, Terrain::open, 1), "Beacon");
    none.record_event(rest_event(1, RestResult::no_provisions, Terrain::open, 3, 3, 0, 0, 0),
                      "Beacon");
    none.record_event(move_event(2, Direction::right, Terrain::open, 1), "Beacon");
    CHECK(none.size() == 2);
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

TEST_CASE("record rescue appends a rescue entry with beacon state") {
    Journal discovered;
    discovered.record_rescue("North Ridge", true);
    REQUIRE(discovered.size() == 1);
    const auto* reached = std::get_if<RescueEntry>(&discovered.entries()[0].data);
    REQUIRE(reached != nullptr);
    CHECK(reached->beacon_name == "North Ridge");
    CHECK(reached->beacon_discovered);

    Journal not_reached;
    not_reached.record_rescue("North Ridge", false);
    REQUIRE(not_reached.size() == 1);
    const auto* missed = std::get_if<RescueEntry>(&not_reached.entries()[0].data);
    REQUIRE(missed != nullptr);
    CHECK(missed->beacon_name == "North Ridge");
    CHECK_FALSE(missed->beacon_discovered);
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

TEST_CASE("rest prose uses recovered wording with terrain and provisions") {
    Journal recovered;
    recovered.record_event(
        rest_event(0, RestResult::recovered, Terrain::hill, 9, 12, 3, 2, 1), "Beacon");
    CHECK(format_entry(recovered.entries().front()) ==
          std::string("Rested on hill and recovered 3 stamina (1 provisions left)."));
}

TEST_CASE("rescue prose states whether the beacon was reached") {
    Journal discovered;
    discovered.record_rescue("North Ridge", true);
    CHECK(format_entry(discovered.entries().front()) ==
          std::string("Ran out of provisions after reaching North Ridge; signaled for an early "
                      "rescue."));

    Journal not_reached;
    not_reached.record_rescue("North Ridge", false);
    CHECK(format_entry(not_reached.entries().front()) ==
          std::string("Ran out of provisions before reaching North Ridge; signaled for an early "
                      "rescue."));
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
        journal.record_event(
            rest_event(3, RestResult::recovered, Terrain::open, 8, 12, 4, 2, 1),
            "North Ridge");
        journal.record_event(move_event(4, Direction::up, Terrain::hill, 2,
                                        ObjectiveTransition::beacon_discovered),
                             "North Ridge");
        journal.record_event(move_event(5, Direction::down, Terrain::hill, 2,
                                        ObjectiveTransition::expedition_completed),
                             "North Ridge");
        journal.record_rescue("North Ridge", true);
        return journal;
    };

    const Journal first = build();
    const Journal second = build();
    CHECK(first.size() == second.size());
    CHECK(prose_of(first) == prose_of(second));

    const std::vector<std::string> expected{
        "Traveled east across open ground for 2 steps.",
        "Rested on open ground and recovered 4 stamina (1 provisions left).",
        "Traveled north across hill for 1 step.",
        "Discovered North Ridge.",
        "Traveled south across hill for 1 step.",
        "Returned to spawn after reaching North Ridge; expedition complete.",
        "Ran out of provisions after reaching North Ridge; signaled for an early rescue.",
    };
    CHECK(prose_of(first) == expected);
}

TEST_CASE("only successful camps and bivouacs create typed entries and break grouping") {
    Journal journal;
    journal.record_event(move_event(0, Direction::right, Terrain::open, 1), "Beacon");
    // A successful normal camp appends a CampEntry and closes travel grouping.
    journal.record_event(
        camp_event(1, CampResult::camped, CampKind::normal, Terrain::open, 1, 2, 20, 1), "Beacon");
    // A following move starts a fresh travel group, not merged with the earlier one.
    journal.record_event(move_event(2, Direction::right, Terrain::water, 3), "Beacon");
    // A successful bivouac appends a BivouacEntry.
    journal.record_event(
        camp_event(3, CampResult::camped, CampKind::bivouac, Terrain::water, 2, 3, 10, 0), "Beacon");
    // Failed camps create no entry.
    journal.record_event(
        camp_event(4, CampResult::ineligible, CampKind::normal, Terrain::water, 3, 3, 10, 0),
        "Beacon");
    journal.record_event(
        camp_event(5, CampResult::no_provisions, CampKind::bivouac, Terrain::water, 3, 3, 10, 0),
        "Beacon");

    REQUIRE(journal.size() == 4u);
    CHECK(std::holds_alternative<TravelEntry>(journal.entries()[0].data));
    CHECK(std::holds_alternative<CampEntry>(journal.entries()[1].data));
    CHECK(std::holds_alternative<TravelEntry>(journal.entries()[2].data));
    CHECK(std::holds_alternative<BivouacEntry>(journal.entries()[3].data));
}

TEST_CASE("camp and bivouac prose name the terrain the new day and the resources") {
    Journal camp;
    camp.record_event(
        camp_event(0, CampResult::camped, CampKind::normal, Terrain::fields, 1, 2, 20, 3), "Beacon");
    CHECK(format_entry(camp.entries().front()) ==
          std::string("Camped on fields overnight; day 2 began with 20 stamina (3 provisions "
                      "left)."));

    Journal bivouac;
    bivouac.record_event(
        camp_event(0, CampResult::camped, CampKind::bivouac, Terrain::mountain, 2, 3, 10, 1),
        "Beacon");
    CHECK(format_entry(bivouac.entries().front()) ==
          std::string("Bivouacked on mountain overnight; day 3 began with 10 stamina (1 provisions "
                      "left)."));
}

TEST_CASE("overdue prose states whether the beacon was reached") {
    Journal reached;
    reached.record_overdue("North Ridge", true);
    CHECK(format_entry(reached.entries().front()) ==
          std::string("Missed the return deadline after reaching North Ridge; a late retrieval "
                      "party collected the explorer."));

    Journal not_reached;
    not_reached.record_overdue("North Ridge", false);
    CHECK(format_entry(not_reached.entries().front()) ==
          std::string("Missed the return deadline before reaching North Ridge; a late retrieval "
                      "party collected the explorer."));
}

}  // TEST_SUITE("journal")
