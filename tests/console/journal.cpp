#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "direction.h"
#include "game_event.h"
#include "journal.h"
#include "level_tier.h"
#include "move_outcome.h"
#include "visibility.h"
#include "objective.h"
#include "set_piece.h"
#include "terrain.h"

using namespace nam::console;

namespace {

GameEvent move_event(std::uint64_t sequence, Direction direction, Terrain terrain,
                     ObjectiveTransition transition = ObjectiveTransition::none) {
    MoveAttemptedEvent move;
    move.direction = direction;
    move.outcome.result = MoveResult::moved;
    move.outcome.terrain = terrain;
    move.objective_update.transition = transition;
    return GameEvent{sequence, move};
}

// A move that first entered a discovery cell.
GameEvent discovery_event(std::uint64_t sequence, Direction direction,
                          ObjectiveTransition transition = ObjectiveTransition::none) {
    GameEvent event = move_event(sequence, direction, Terrain::open, transition);
    std::get<MoveAttemptedEvent>(event.data).discovery_recorded = true;
    return event;
}

// A successful move that granted the one-off wide reveal of a vantage point.
GameEvent vantage_event(std::uint64_t sequence, Direction direction,
                        ObjectiveTransition transition = ObjectiveTransition::none) {
    GameEvent event = move_event(sequence, direction, Terrain::hill, transition);
    std::get<MoveAttemptedEvent>(event.data).wide_reveal_radius =
        vantage_reveal_radius_of(VantageKind::lookout);
    return event;
}

// A move that first stepped into the level's terrain set-piece.
GameEvent crossing_event(std::uint64_t sequence, Direction direction, SetPieceKind kind,
                         ObjectiveTransition transition = ObjectiveTransition::none) {
    GameEvent event = move_event(sequence, direction, Terrain::shallow_water, transition);
    std::get<MoveAttemptedEvent>(event.data).set_piece_crossed = kind;
    return event;
}

GameEvent blocked_event(std::uint64_t sequence, Direction direction,
                        MoveResult result = MoveResult::blocked_by_terrain) {
    MoveAttemptedEvent move;
    move.direction = direction;
    move.outcome.result = result;
    return GameEvent{sequence, move};
}

// A single-level journal context around the core-owned landmark name, which is
// all most cases need.
JournalContext ctx(const std::string& landmark_name) {
    JournalContext context;
    context.landmark_name = landmark_name;
    return context;
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

TEST_CASE("the first vantage point of a level becomes a notable encounter") {
    Journal journal;
    JournalContext context = ctx("Glass River Exit");
    context.vantage_kind = VantageKind::lookout;
    journal.record_event(vantage_event(1, Direction::right), context);
    REQUIRE(journal.size() == 1);
    CHECK(prose_of(journal)[0] == "Climbed to a lookout and the level opened up.");
}

TEST_CASE("the journal says which kind of viewpoint the level opened up from") {
    for (const VantageKind kind : all_vantage_kinds) {
        Journal journal;
        JournalContext context = ctx("Glass River Exit");
        context.vantage_kind = kind;
        journal.record_event(vantage_event(1, Direction::right), context);
        REQUIRE(journal.size() == 1);
        CHECK(prose_of(journal)[0].find(std::string(to_string(kind))) != std::string::npos);
    }
}

TEST_CASE("a level opens up once no matter how many vantage points are climbed") {
    // A thorough sweep steps on several vantage points. Repeating the same moment
    // would spend the whole run's entry budget on it, so later ones are collapsed
    // by design rather than truncated away.
    Journal journal;
    journal.record_event(vantage_event(1, Direction::right), ctx("Glass River Exit"));
    journal.record_event(vantage_event(2, Direction::right), ctx("Glass River Exit"));
    journal.record_event(vantage_event(3, Direction::up), ctx("Glass River Exit"));
    CHECK(journal.size() == 1);
}

TEST_CASE("the next level of an expedition may log its own first vantage point") {
    Journal journal;
    journal.record_event(vantage_event(1, Direction::right), ctx("Glass River Exit"));
    journal.record_event(vantage_event(2, Direction::right), ctx("Glass River Exit"));
    journal.begin_level();
    journal.record_event(vantage_event(3, Direction::right), ctx("Moon Marker Waystone"));
    journal.record_event(vantage_event(4, Direction::right), ctx("Moon Marker Waystone"));

    // Two levels, one notable encounter each; the earlier entries all survive,
    // because the journal itself is expedition-wide.
    CHECK(journal.size() == 2);
}

TEST_CASE("reaching the landmark is recorded as the larger milestone, not as a vantage") {
    Journal journal;
    journal.record_event(
        vantage_event(1, Direction::right, ObjectiveTransition::landmark_discovered),
        ctx("Glass River Exit"));
    REQUIRE(journal.size() == 1);
    CHECK(prose_of(journal)[0].find("Glass River Exit") != std::string::npos);

    // The suppressed vantage did not consume the level's one notable encounter.
    journal.record_event(vantage_event(2, Direction::right), ctx("Glass River Exit"));
    CHECK(journal.size() == 2);
}

TEST_CASE("crossing the level's set-piece is the notable encounter it guarantees") {
    Journal journal;
    journal.record_event(crossing_event(1, Direction::right, SetPieceKind::ford),
                         ctx("Glass River Exit"));
    REQUIRE(journal.size() == 1);
    CHECK(prose_of(journal)[0].find("Forded") != std::string::npos);
}

TEST_CASE("the journal says which crossing the level made the player take") {
    for (const SetPieceKind kind : {SetPieceKind::ford, SetPieceKind::ridge,
                                    SetPieceKind::lakeshore, SetPieceKind::high_pass}) {
        Journal journal;
        journal.record_event(crossing_event(1, Direction::right, kind), ctx("Glass River Exit"));
        REQUIRE(journal.size() == 1);
        CHECK(prose_of(journal)[0].empty() == false);
    }

    // Four crossings, four distinct sentences: the entry is not a template with
    // the same words in it.
    Journal all;
    all.record_event(crossing_event(1, Direction::right, SetPieceKind::ford), ctx("A"));
    all.begin_level();
    all.record_event(crossing_event(2, Direction::right, SetPieceKind::ridge), ctx("A"));
    all.begin_level();
    all.record_event(crossing_event(3, Direction::right, SetPieceKind::lakeshore), ctx("A"));
    all.begin_level();
    all.record_event(crossing_event(4, Direction::right, SetPieceKind::high_pass), ctx("A"));
    const std::vector<std::string> prose = prose_of(all);
    REQUIRE(prose.size() == 4u);
    for (std::size_t i = 0; i < prose.size(); ++i) {
        for (std::size_t j = i + 1u; j < prose.size(); ++j) {
            CHECK(prose[i] != prose[j]);
        }
    }
}

TEST_CASE("a crossing and a vantage point share the level's one encounter entry") {
    // A level reports one notable encounter however many moments could have
    // filled it, so whichever comes first takes the slot and the other is
    // collapsed. That is what holds the per-level cap at four.
    Journal crossing_first;
    crossing_first.record_event(crossing_event(1, Direction::right, SetPieceKind::ford),
                                ctx("Glass River Exit"));
    crossing_first.record_event(vantage_event(2, Direction::right), ctx("Glass River Exit"));
    CHECK(crossing_first.size() == 1);
    CHECK(prose_of(crossing_first)[0].find("Forded") != std::string::npos);

    Journal vantage_first;
    JournalContext context = ctx("Glass River Exit");
    context.vantage_kind = VantageKind::lookout;
    vantage_first.record_event(vantage_event(1, Direction::right), context);
    vantage_first.record_event(crossing_event(2, Direction::right, SetPieceKind::ford), context);
    CHECK(vantage_first.size() == 1);
    CHECK(prose_of(vantage_first)[0].find("lookout") != std::string::npos);

    // The next level gets its own slot back.
    vantage_first.begin_level();
    vantage_first.record_event(crossing_event(3, Direction::right, SetPieceKind::ridge), context);
    CHECK(vantage_first.size() == 2);
}

TEST_CASE("a move that both crosses and climbs is recorded as the crossing") {
    Journal journal;
    GameEvent both = crossing_event(1, Direction::right, SetPieceKind::high_pass);
    std::get<MoveAttemptedEvent>(both.data).wide_reveal_radius =
        vantage_reveal_radius_of(VantageKind::summit);
    journal.record_event(both, ctx("Glass River Exit"));
    REQUIRE(journal.size() == 1);
    CHECK(prose_of(journal)[0].find("pass") != std::string::npos);
}

TEST_CASE("a new journal is empty") {
    Journal journal;
    CHECK(journal.empty());
    CHECK(journal.size() == 0);
    CHECK(journal.entries().empty());
}

TEST_CASE("routine movement never becomes a journal entry") {
    Journal journal;
    journal.record_event(move_event(0, Direction::right, Terrain::open), ctx("North Ridge"));
    journal.record_event(move_event(1, Direction::right, Terrain::open), ctx("North Ridge"));
    journal.record_event(move_event(2, Direction::up, Terrain::hill), ctx("North Ridge"));
    journal.record_event(move_event(3, Direction::down, Terrain::fields), ctx("North Ridge"));

    CHECK(journal.empty());
}

TEST_CASE("a blocked attempt never becomes a journal entry") {
    Journal journal;
    for (const MoveResult result : {MoveResult::blocked_by_boundary,
                                    MoveResult::blocked_by_terrain}) {
        journal.record_event(blocked_event(0, Direction::right, result), ctx("North Ridge"));
    }
    CHECK(journal.empty());
}

TEST_CASE("a landmark move appends exactly one objective entry") {
    Journal journal;
    journal.record_event(move_event(0, Direction::right, Terrain::open), ctx("North Ridge"));
    journal.record_event(
        move_event(1, Direction::right, Terrain::open, ObjectiveTransition::landmark_discovered),
        ctx("North Ridge"));

    REQUIRE(journal.size() == 1);
    const auto* landmark = std::get_if<LandmarkEntry>(&journal.entries().front().data);
    REQUIRE(landmark != nullptr);
    CHECK(landmark->sequence == 1);
    CHECK(landmark->landmark_name == std::string("North Ridge"));
}

TEST_CASE("a completing move appends exactly one completion entry") {
    Journal journal;
    journal.record_event(move_event(0, Direction::left, Terrain::open), ctx("North Ridge"));
    journal.record_event(move_event(1, Direction::left, Terrain::open,
                                    ObjectiveTransition::level_completed),
                         ctx("North Ridge"));

    REQUIRE(journal.size() == 1);
    const auto* completion = std::get_if<CompletionEntry>(&journal.entries().front().data);
    REQUIRE(completion != nullptr);
    CHECK(completion->sequence == 1);
    CHECK(completion->landmark_name == std::string("North Ridge"));
}

TEST_CASE("a full level produces exactly two milestone entries in order") {
    Journal journal;
    journal.record_event(move_event(0, Direction::right, Terrain::open), ctx("North Ridge"));
    journal.record_event(
        move_event(1, Direction::right, Terrain::open, ObjectiveTransition::landmark_discovered),
        ctx("North Ridge"));
    journal.record_event(move_event(2, Direction::right, Terrain::open), ctx("North Ridge"));
    journal.record_event(
        move_event(3, Direction::right, Terrain::open, ObjectiveTransition::level_completed),
        ctx("North Ridge"));

    REQUIRE(journal.size() == 2);
    CHECK(std::holds_alternative<LandmarkEntry>(journal.entries()[0].data));
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

TEST_CASE("landmark and completion prose name the landmark exactly") {
    Journal discovery;
    discovery.record_event(
        move_event(0, Direction::right, Terrain::open, ObjectiveTransition::landmark_discovered),
        ctx("North Ridge"));
    CHECK(format_entry(discovery.entries().back()) ==
          std::string("Sighted North Ridge; the land opened up and the exit direction was revealed."));

    Journal completion;
    completion.record_event(move_event(0, Direction::left, Terrain::open,
                                       ObjectiveTransition::level_completed),
                            ctx("North Ridge"));
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
        journal.record_event(move_event(0, Direction::right, Terrain::open), ctx("North Ridge"));
        journal.record_event(move_event(1, Direction::right, Terrain::open), ctx("North Ridge"));
        journal.record_event(blocked_event(2, Direction::right), ctx("North Ridge"));
        journal.record_event(move_event(3, Direction::up, Terrain::hill,
                                        ObjectiveTransition::landmark_discovered),
                             ctx("North Ridge"));
        journal.record_event(move_event(4, Direction::down, Terrain::hill,
                                        ObjectiveTransition::level_completed),
                             ctx("North Ridge"));
        return journal;
    };

    const Journal first = build();
    const Journal second = build();
    CHECK(first.size() == second.size());
    CHECK(prose_of(first) == prose_of(second));

    const std::vector<std::string> expected{
        "Sighted North Ridge; the land opened up and the exit direction was revealed.",
        "Reached the exit after North Ridge; level complete.",
    };
    CHECK(prose_of(first) == expected);
}

TEST_CASE("a first discovery appends one entry that counts the level's finds") {
    Journal journal;
    JournalContext context = ctx("North Ridge");
    context.discovery_total = 2;

    // A routine step over an already-found discovery cell records nothing.
    journal.record_event(move_event(0, Direction::right, Terrain::open), context);
    CHECK(journal.empty());

    context.discoveries_found = 1;
    journal.record_event(discovery_event(1, Direction::right), context);

    REQUIRE(journal.size() == 1);
    const auto* discovery = std::get_if<DiscoveryEntry>(&journal.entries().front().data);
    REQUIRE(discovery != nullptr);
    CHECK(discovery->sequence == 1);
    CHECK(discovery->ordinal == 1);
    CHECK(discovery->total == 2);
    CHECK(format_entry(journal.entries().front()) ==
          std::string("Found a hidden site off the route (1 of 2)."));
}

TEST_CASE("one move can both find a discovery and complete the level") {
    Journal journal;
    JournalContext context = ctx("North Ridge");
    context.discoveries_found = 1;
    context.discovery_total = 1;
    journal.record_event(discovery_event(4, Direction::right, ObjectiveTransition::level_completed),
                         context);

    REQUIRE(journal.size() == 2);
    CHECK(std::holds_alternative<DiscoveryEntry>(journal.entries()[0].data));
    CHECK(std::holds_alternative<CompletionEntry>(journal.entries()[1].data));
}

TEST_CASE("a completion inside an expedition names the tier and the level number") {
    Journal journal;
    JournalContext context = ctx("North Ridge");
    context.tier = LevelTier::small;
    context.level_number = 1;
    context.total_levels = 2;
    context.discoveries_found = 1;
    context.discovery_total = 2;
    journal.record_event(
        move_event(3, Direction::right, Terrain::open, ObjectiveTransition::level_completed),
        context);

    REQUIRE(journal.size() == 1);
    const auto* completion = std::get_if<CompletionEntry>(&journal.entries().front().data);
    REQUIRE(completion != nullptr);
    CHECK(completion->tier == LevelTier::small);
    CHECK(completion->level_number == 1);
    CHECK(completion->total_levels == 2);
    CHECK(format_entry(journal.entries().front()) ==
          std::string("Left the Small level through the exit (1 of 2 complete, "
                      "1 of 2 discoveries found)."));
}

TEST_CASE("a whole level stays within its per-level entry cap") {
    // A level reports at most one of each category: its discovery, its notable
    // encounter, its landmark, and its completion. That per-level cap is what
    // bounds the whole run at journal_entry_budget, so it is checked here on a
    // level that produces one of each.
    Journal journal;
    JournalContext context = ctx("North Ridge");
    context.total_levels = 2;
    context.discovery_total = 1;
    for (std::uint64_t i = 0; i < 12; ++i) {
        journal.record_event(move_event(i, Direction::right, Terrain::open), context);
    }
    context.discoveries_found = 1;
    journal.record_event(discovery_event(12, Direction::right), context);
    journal.record_event(
        move_event(13, Direction::up, Terrain::hill, ObjectiveTransition::landmark_discovered),
        context);
    journal.record_event(
        move_event(14, Direction::up, Terrain::hill, ObjectiveTransition::level_completed),
        context);

    CHECK(journal.size() == 3);
    CHECK(journal.size() <= journal_entry_budget_for(context.discovery_total));
    // A level holding one discovery reports at most its three fixed moments plus
    // that one, and the run budget is the same formula summed over the tiers.
    CHECK(journal_entry_budget_for(1) == journal_fixed_entries_per_level + 1u);
    CHECK(journal_entry_budget == 4u * journal_fixed_entries_per_level + 1u + 2u + 3u + 4u);
}

}  // TEST_SUITE("journal")
