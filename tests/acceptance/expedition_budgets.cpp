#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include "coordinates.h"
#include "direction.h"
#include "expedition.h"
#include "game_state.h"
#include "journal.h"
#include "level_feature.h"
#include "objective.h"

#include "scripted_walk.h"

// The journal budget, checked against real played expeditions rather than a
// hand-built event script.
//
// Phase 3 added landmark identities, side-route variation, vantage kinds, a
// terrain set-piece per tier and two more carried bonuses. Every one of those is
// a thing that could have grown the run's bookkeeping, and the phase's exit
// condition is that none of them did. A synthetic journal test cannot see that,
// because it never plays a level: only a run that actually walks a whole
// expedition steps on the several vantage points and crosses the band the
// generator laid down.

using namespace nam::console;

namespace {

// The entry budget is owned by the journal itself (`journal_entry_budget`), so
// this sweep measures real runs against the number the design states rather than
// against a copy of it that could drift.
//
// Every tier of a run, so the budget is measured over the whole chain rather
// than the two tiers the prototype currently plays.
constexpr LevelTier kFinalTier = LevelTier::x_large;

// How thoroughly a scripted run plays each level. `sweep` is the worst case for
// the journal: it stands on every viewpoint the level placed and re-crosses the
// set-piece band repeatedly.
enum class Style {
    direct,
    sweep,
};

bool walk_to(GameState& state, Coordinates target, Journal& journal,
             const JournalContext& base) {
    while (state.actor_position() != target) {
        const std::vector<Coordinates> path =
            shortest_path(state.map(), state.actor_position(), target);
        if (path.size() < 2u) {
            return false;
        }
        for (std::size_t step = 1; step < path.size(); ++step) {
            const Coordinates delta{path[step].x - path[step - 1u].x,
                                    path[step].y - path[step - 1u].y};
            const std::optional<Direction> direction = direction_of(delta);
            REQUIRE(direction.has_value());
            const GameEvent event = state.move(*direction);

            JournalContext context = base;
            context.discoveries_found = state.discoveries_found();
            context.discovery_total = state.discovery_total();
            context.vantage_kind = vantage_kind_of(state.actor_terrain());
            journal.record_event(event, context);
        }
    }
    return true;
}

// Play one whole expedition in the given style, folding every core event into
// one expedition-wide journal exactly as the application does.
[[nodiscard]] std::size_t journal_entries_of(std::uint64_t seed, Style style) {
    Expedition expedition(seed, kFinalTier);
    Journal journal;

    while (!expedition.completed()) {
        GameState& state = expedition.state();
        JournalContext base;
        base.landmark_name = state.objective().name;
        base.tier = expedition.current_tier();
        base.level_number = expedition.completed_levels() + 1u;
        base.total_levels = expedition.total_levels();

        if (style == Style::sweep) {
            while (const std::optional<Coordinates> target = nam::test::nearest_unexplored(state)) {
                REQUIRE(walk_to(state, *target, journal, base));
            }
            for (const LevelFeature& feature : state.features()) {
                if (feature.kind == LevelFeatureKind::discovery) {
                    REQUIRE(walk_to(state, feature.position, journal, base));
                }
            }
        }
        REQUIRE(walk_to(state, state.objective().landmark, journal, base));
        REQUIRE(walk_to(state, state.objective().exit_cell, journal, base));
        REQUIRE(state.objective_completed());

        if (expedition.complete_level(LevelPerformance{}) == LevelTransition::advanced) {
            journal.begin_level();
        }
    }
    return journal.size();
}

[[nodiscard]] std::vector<std::uint64_t> budget_seeds() {
    return {0x0F4289EAF4A1813Cull, 0x1E7F3C55A0B21D09ull, 0x9A3C71E5D2408F6Bull,
            0x00000000000000FFull, 0xFFFFFFFFFFFFFFFFull};
}

}  // namespace

TEST_SUITE("expedition_budgets") {

TEST_CASE("a whole four-tier expedition stays inside the journal entry budget") {
    for (const std::uint64_t seed : budget_seeds()) {
        const std::size_t direct = journal_entries_of(seed, Style::direct);
        const std::size_t sweep = journal_entries_of(seed, Style::sweep);

        // A run that finishes every level always has something to say about each
        // of them, so an empty journal would mean the fold is broken rather than
        // that the budget is safe.
        CHECK(direct > 0u);
        CHECK(direct <= journal_entry_budget);
        CHECK(sweep <= journal_entry_budget);

        // The thorough run is the one the budget is at risk from: it steps on
        // every viewpoint and enters every discovery, so it must be the larger of
        // the two and still fit.
        CHECK(sweep >= direct);

        // A run that skips every optional thing must be visibly cheaper, or the
        // per-level caps are collapsing moments that were never distinct.
        CHECK(direct < journal_entry_budget);
    }
}

TEST_CASE("climbing every viewpoint on a level costs the journal one entry") {
    // The notable-encounter slot is what keeps a sweep inside the budget, and it
    // is shared with the set-piece crossing. Measured per level rather than per
    // run, so a level with several viewpoints cannot quietly log several.
    for (const std::uint64_t seed : budget_seeds()) {
        Expedition expedition(seed, kFinalTier);
        while (!expedition.completed()) {
            GameState& state = expedition.state();
            REQUIRE(state.vantage_total() > 1u);

            Journal journal;
            JournalContext base;
            base.landmark_name = state.objective().name;
            base.tier = expedition.current_tier();
            base.level_number = expedition.completed_levels() + 1u;
            base.total_levels = expedition.total_levels();

            for (const LevelFeature& feature : state.features()) {
                if (feature.kind == LevelFeatureKind::vantage_point) {
                    REQUIRE(walk_to(state, feature.position, journal, base));
                }
            }
            CHECK(state.vantages_reached() == state.vantage_total());

            // Several viewpoints climbed, and the level has still had exactly one
            // notable encounter. The crossing may have taken the slot first, which
            // is the point: whichever moment came first is the one reported. The
            // walk may also have passed the landmark, which is a different
            // category and is counted separately.
            std::size_t encounters = 0;
            for (const JournalEntry& entry : journal.entries()) {
                if (std::holds_alternative<VantageEntry>(entry.data) ||
                    std::holds_alternative<CrossingEntry>(entry.data)) {
                    ++encounters;
                }
            }
            CHECK(encounters == 1u);
            CHECK(journal.size() <= journal_entries_per_level);

            REQUIRE(walk_to(state, state.objective().landmark, journal, base));
            REQUIRE(walk_to(state, state.objective().exit_cell, journal, base));
            CHECK(journal.size() <= journal_entries_per_level);
            if (expedition.complete_level(LevelPerformance{}) != LevelTransition::advanced) {
                break;
            }
        }
    }
}

}  // TEST_SUITE("expedition_budgets")
