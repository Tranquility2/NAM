#include <doctest/doctest.h>

#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "game_state.h"
#include "level_feature.h"
#include "level_template.h"
#include "map.h"
#include "objective.h"
#include "visibility.h"
#include "terrain.h"
#include "world_generation.h"

namespace {

// A tiny open arena with authored content placed by hand. Using a handcrafted
// layout rather than a generated one keeps every feature rule test independent of
// the generator's placement choices.
Map arena(std::vector<LevelFeature> features, Coordinates exit_cell) {
    constexpr std::size_t width = 7;
    constexpr std::size_t height = 5;
    std::vector<Terrain> cells(width * height, Terrain::open);
    for (std::size_t x = 0; x < width; ++x) {
        cells[x] = Terrain::cliff;
        cells[(height - 1) * width + x] = Terrain::cliff;
    }
    for (std::size_t y = 1; y + 1 < height; ++y) {
        cells[y * width] = Terrain::cliff;
        cells[y * width + width - 1] = Terrain::cliff;
    }

    LevelLayout layout;
    layout.exit = exit_cell;
    layout.features = std::move(features);
    return Map(width, height, std::move(cells), Coordinates{1, 1}, std::move(layout));
}

const MoveAttemptedEvent& movement(const GameEvent& event) {
    REQUIRE(std::holds_alternative<MoveAttemptedEvent>(event.data));
    return std::get<MoveAttemptedEvent>(event.data);
}

// Walking length between two cells, or nullopt when no walk exists.
[[nodiscard]] std::optional<std::int64_t> walk_length(const Map& map, Coordinates from,
                                                      Coordinates to) {
    const std::vector<Coordinates> path = shortest_path(map, from, to);
    if (path.empty()) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(path.size()) - 1;
}

// What visiting `cell` adds to a spawn-to-exit journey: the price the player
// actually pays for it, and the quantity every content slot is authored against.
[[nodiscard]] std::optional<std::int64_t> detour_of(const Map& map, Coordinates exit_cell,
                                                    Coordinates cell) {
    const std::optional<std::int64_t> to_cell = walk_length(map, map.spawn(), cell);
    const std::optional<std::int64_t> to_exit = walk_length(map, cell, exit_cell);
    const std::optional<std::int64_t> direct = walk_length(map, map.spawn(), exit_cell);
    if (!to_cell || !to_exit || !direct) {
        return std::nullopt;
    }
    return *to_cell + *to_exit - *direct;
}

// A spread of seeds wide enough to meet every profile on every tier.
constexpr std::uint64_t kProbeSeeds = 36u;

[[nodiscard]] std::uint64_t probe_seed(std::uint64_t index) {
    return index * 0x9E3779B97F4A7C15ull + 0xFEEDull;
}

}  // namespace

TEST_SUITE("level_feature") {

TEST_CASE("a handcrafted map carries no authored content") {
    GameState state(Map(3, 3,
                        std::vector<Terrain>{Terrain::cliff, Terrain::cliff,
                                             Terrain::cliff, Terrain::cliff,
                                             Terrain::open, Terrain::cliff,
                                             Terrain::cliff, Terrain::cliff,
                                             Terrain::cliff},
                        Coordinates{1, 1}));

    CHECK(state.features().empty());
    CHECK(state.discovery_total() == 0u);
    CHECK(state.discoveries_found() == 0u);
    CHECK_FALSE(state.feature_at(Coordinates{1, 1}).has_value());
}

TEST_CASE("a vantage point grants one wide reveal on first entry and is then inert") {
    GameState state(arena({LevelFeature{Coordinates{2, 1}, LevelFeatureKind::vantage_point}},
                          Coordinates{5, 3}));

    // peek stays a pure preview: it reports the feature but resolves nothing.
    const MoveOutcome preview = state.peek(Direction::right);
    CHECK(preview.result == MoveResult::moved);
    REQUIRE(preview.feature.has_value());
    CHECK(*preview.feature == LevelFeatureKind::vantage_point);

    const GameEvent first = state.move(Direction::right);
    CHECK(movement(first).wide_reveal_granted);
    CHECK_FALSE(movement(first).discovery_recorded);

    // Leaving and returning does not fire it again.
    static_cast<void>(state.move(Direction::down));
    const GameEvent again = state.move(Direction::up);
    CHECK(movement(again).outcome.result == MoveResult::moved);
    CHECK_FALSE(movement(again).wide_reveal_granted);
}

TEST_CASE("a vantage point is an ordinary walkable cell and never blocks") {
    GameState state(arena({LevelFeature{Coordinates{2, 1}, LevelFeatureKind::vantage_point}},
                          Coordinates{5, 3}));

    const GameEvent event = state.move(Direction::right);
    CHECK(movement(event).outcome.result == MoveResult::moved);
    CHECK(movement(event).outcome.terrain == Terrain::open);
    CHECK(state.actor_position() == Coordinates{2, 1});
}

TEST_CASE("a vantage point reveals ground no terrain radius could reach") {
    // A wide arena so the reveal is not clipped away by the map edges. The vantage
    // sits far enough from the spawn that the cell checked below is outside the
    // spawn's own sight square but inside the vantage's.
    constexpr std::size_t width = 31;
    constexpr std::size_t height = 25;
    std::vector<Terrain> cells(width * height, Terrain::open);
    for (std::size_t x = 0; x < width; ++x) {
        cells[x] = Terrain::cliff;
        cells[(height - 1) * width + x] = Terrain::cliff;
    }
    for (std::size_t y = 1; y + 1 < height; ++y) {
        cells[y * width] = Terrain::cliff;
        cells[y * width + width - 1] = Terrain::cliff;
    }
    const Coordinates spawn{15, 12};
    LevelLayout layout;
    layout.exit = Coordinates{1, 1};
    layout.features = {LevelFeature{Coordinates{16, 12}, LevelFeatureKind::vantage_point}};
    GameState state(Map(width, height, std::move(cells), spawn, std::move(layout)));

    // Open ground sees 3, so a cell 8 away is still unexplored before the vantage.
    const Coordinates distant{16 + 8, 12};
    REQUIRE(state.visibility().at(distant) == CellVisibility::unexplored);

    const GameEvent event = state.move(Direction::right);
    REQUIRE(movement(event).wide_reveal_granted);
    CHECK(state.visibility().at(distant) == CellVisibility::visible);
}

TEST_CASE("a discovery is recorded once and grants no wide reveal") {
    GameState state(arena({LevelFeature{Coordinates{2, 1}, LevelFeatureKind::discovery}},
                          Coordinates{5, 3}));
    CHECK(state.discovery_total() == 1u);

    const MoveOutcome preview = state.peek(Direction::right);
    CHECK(preview.result == MoveResult::moved);
    CHECK(preview.feature == LevelFeatureKind::discovery);

    const GameEvent first = state.move(Direction::right);
    CHECK(movement(first).discovery_recorded);
    CHECK_FALSE(movement(first).wide_reveal_granted);
    CHECK(state.discoveries_found() == 1u);

    // Re-entering the same cell is an ordinary step.
    static_cast<void>(state.move(Direction::down));
    const GameEvent again = state.move(Direction::up);
    CHECK_FALSE(movement(again).discovery_recorded);
    CHECK(state.discoveries_found() == 1u);
}

TEST_CASE("a blocked attempt reports no feature and resolves nothing") {
    GameState state(arena({LevelFeature{Coordinates{2, 1}, LevelFeatureKind::discovery}},
                          Coordinates{5, 3}));

    const GameEvent event = state.move(Direction::up);
    CHECK(movement(event).outcome.result == MoveResult::blocked_by_terrain);
    CHECK_FALSE(movement(event).outcome.feature.has_value());
    CHECK_FALSE(movement(event).discovery_recorded);
    CHECK(state.discoveries_found() == 0u);
}

TEST_CASE("every generated level places its whole content budget on reachable walkable cells") {
    for (const LevelTier tier : {LevelTier::small, LevelTier::medium}) {
        for (std::uint64_t index = 0; index < 48u; ++index) {
            const std::uint64_t seed = index * 0x9E3779B97F4A7C15ull + 0xFEEDull;
            const WorldGenerationResult result = generate_level(tier, seed);
            REQUIRE(std::holds_alternative<GeneratedWorld>(result));
            const GeneratedWorld& world = std::get<GeneratedWorld>(result);
            const LevelTemplate level = template_of(tier, world.exit_corner);

            const std::vector<LevelFeature>& placed = world.map.layout().features;
            REQUIRE(placed.size() == level.content_slots.size());

            for (std::size_t slot = 0; slot < placed.size(); ++slot) {
                CHECK(placed[slot].kind == level.content_slots[slot].kind);
                CHECK(level.content_slots[slot].zone.contains(placed[slot].position));
                CHECK(is_walkable(world.map.terrain_at(placed[slot].position)));
                CHECK(placed[slot].position != world.map.spawn());
                CHECK(placed[slot].position != world.exit_cell);
            }

            // No two features share a cell.
            for (std::size_t left = 0; left + 1 < placed.size(); ++left) {
                for (std::size_t right = left + 1; right < placed.size(); ++right) {
                    CHECK(placed[left].position != placed[right].position);
                }
            }
        }
    }
}

TEST_CASE("content placement is deterministic for one tier and seed") {
    const WorldGenerationResult first = generate_level(LevelTier::medium, 0xC0FFEEull);
    const WorldGenerationResult second = generate_level(LevelTier::medium, 0xC0FFEEull);
    REQUIRE(std::holds_alternative<GeneratedWorld>(first));
    REQUIRE(std::holds_alternative<GeneratedWorld>(second));

    const std::vector<LevelFeature>& left = std::get<GeneratedWorld>(first).map.layout().features;
    const std::vector<LevelFeature>& right = std::get<GeneratedWorld>(second).map.layout().features;
    REQUIRE(left.size() == right.size());
    for (std::size_t index = 0; index < left.size(); ++index) {
        CHECK(left[index].position == right[index].position);
        CHECK(left[index].kind == right[index].kind);
    }
}

TEST_CASE("the feature kind identifiers are stable and non-localized") {
    CHECK(std::string(to_string(LevelFeatureKind::discovery)) == "discovery");
    CHECK(std::string(to_string(LevelFeatureKind::vantage_point)) == "vantage_point");
}

TEST_CASE("a detour band costs more the further out it is and a profile scales them together") {
    for (const DetourProfile profile :
         {DetourProfile::tight, DetourProfile::even, DetourProfile::sprawling}) {
        CHECK(target_detour_moves(DetourBand::passing, profile, 100u) <
              target_detour_moves(DetourBand::moderate, profile, 100u));
        CHECK(target_detour_moves(DetourBand::moderate, profile, 100u) <
              target_detour_moves(DetourBand::committed, profile, 100u));
    }
    for (const DetourBand band :
         {DetourBand::passing, DetourBand::moderate, DetourBand::committed}) {
        CHECK(target_detour_moves(band, DetourProfile::tight, 100u) <
              target_detour_moves(band, DetourProfile::even, 100u));
        CHECK(target_detour_moves(band, DetourProfile::even, 100u) <
              target_detour_moves(band, DetourProfile::sprawling, 100u));
    }
    // A level with no journey to speak of asks nothing of the player.
    CHECK(target_detour_moves(DetourBand::committed, DetourProfile::sprawling, 0u) == 0u);
}

TEST_CASE("the detour band identifiers are stable and non-localized") {
    CHECK(std::string(to_string(DetourBand::passing)) == "passing");
    CHECK(std::string(to_string(DetourBand::moderate)) == "moderate");
    CHECK(std::string(to_string(DetourBand::committed)) == "committed");
    CHECK(std::string(to_string(DetourProfile::tight)) == "tight");
    CHECK(std::string(to_string(DetourProfile::even)) == "even");
    CHECK(std::string(to_string(DetourProfile::sprawling)) == "sprawling");
}

TEST_CASE("content is placed on the cell in its zone that comes closest to its price") {
    // The placement rule stated exactly: among every cell generation was allowed
    // to choose, none is a better match for the slot's target than the one it
    // chose. Asserting the rule rather than a tolerance keeps this honest on maps
    // whose terrain simply cannot offer a cell at the asked-for distance.
    for (const LevelTier tier : {LevelTier::small, LevelTier::medium}) {
        for (std::uint64_t index = 0; index < kProbeSeeds; ++index) {
            const WorldGenerationResult result = generate_level(tier, probe_seed(index));
            REQUIRE(std::holds_alternative<GeneratedWorld>(result));
            const GeneratedWorld& world = std::get<GeneratedWorld>(result);
            const LevelTemplate level = template_of(tier, world.exit_corner);
            const std::vector<LevelFeature>& placed = world.map.layout().features;
            REQUIRE(placed.size() == level.content_slots.size());

            const std::optional<std::int64_t> direct =
                walk_length(world.map, world.map.spawn(), world.exit_cell);
            REQUIRE(direct.has_value());

            for (std::size_t slot = 0; slot < placed.size(); ++slot) {
                const ContentSlot& authored = level.content_slots[slot];
                const std::int64_t target = static_cast<std::int64_t>(target_detour_moves(
                    authored.band, world.detour_profile, static_cast<std::uint64_t>(*direct)));

                const std::optional<std::int64_t> chosen =
                    detour_of(world.map, world.exit_cell, placed[slot].position);
                REQUIRE(chosen.has_value());
                const std::int64_t chosen_miss =
                    *chosen > target ? (*chosen - target) : (target - *chosen);

                for (int y = authored.zone.min_y; y <= authored.zone.max_y; ++y) {
                    for (int x = authored.zone.min_x; x <= authored.zone.max_x; ++x) {
                        const Coordinates candidate{x, y};
                        if (!is_walkable(world.map.terrain_at(candidate))) continue;
                        if (candidate == world.map.spawn()) continue;
                        if (candidate == world.exit_cell) continue;
                        if (level.entry_zone.contains(candidate)) continue;
                        bool earlier_slot_took_it = false;
                        for (std::size_t before = 0; before < slot; ++before) {
                            if (placed[before].position == candidate) earlier_slot_took_it = true;
                        }
                        if (earlier_slot_took_it) continue;
                        const std::optional<std::int64_t> other =
                            detour_of(world.map, world.exit_cell, candidate);
                        if (!other) continue;
                        const std::int64_t miss =
                            *other > target ? (*other - target) : (target - *other);
                        CHECK(miss >= chosen_miss);
                    }
                }
            }
        }
    }
}

TEST_CASE("a level never asks more for the thing it hands the player on the way") {
    // The authored ordering, checked against the price the player really pays: the
    // first vantage point is nearly free, the second is a visible choice, and the
    // discovery is the trip the level actually charges for.
    for (const LevelTier tier :
         {LevelTier::small, LevelTier::medium, LevelTier::large, LevelTier::x_large}) {
        for (std::uint64_t index = 0; index < kProbeSeeds; ++index) {
            const WorldGenerationResult result = generate_level(tier, probe_seed(index));
            REQUIRE(std::holds_alternative<GeneratedWorld>(result));
            const GeneratedWorld& world = std::get<GeneratedWorld>(result);
            const std::vector<LevelFeature>& placed = world.map.layout().features;
            REQUIRE(placed.size() == 3u);

            const std::optional<std::int64_t> near =
                detour_of(world.map, world.exit_cell, placed[0].position);
            const std::optional<std::int64_t> aside =
                detour_of(world.map, world.exit_cell, placed[1].position);
            const std::optional<std::int64_t> away =
                detour_of(world.map, world.exit_cell, placed[2].position);
            REQUIRE(near.has_value());
            REQUIRE(aside.has_value());
            REQUIRE(away.has_value());

            CHECK(*near <= *aside);
            CHECK(*aside <= *away);
            CHECK(*away > 0);
        }
    }
}

TEST_CASE("some levels keep their content close and others make the player range") {
    // The variation this step exists to deliver. Averaged over a seed spread, a
    // sprawling level really does cost more to see in full than a tight one, and
    // every profile turns up often enough to be worth authoring for.
    for (const LevelTier tier : {LevelTier::small, LevelTier::large}) {
        std::int64_t totals[3] = {0, 0, 0};
        std::int64_t counts[3] = {0, 0, 0};
        for (std::uint64_t index = 0; index < kProbeSeeds * 4u; ++index) {
            const WorldGenerationResult result = generate_level(tier, probe_seed(index));
            REQUIRE(std::holds_alternative<GeneratedWorld>(result));
            const GeneratedWorld& world = std::get<GeneratedWorld>(result);
            const std::optional<std::int64_t> away =
                detour_of(world.map, world.exit_cell, world.map.layout().features[2].position);
            REQUIRE(away.has_value());
            const std::size_t bucket = static_cast<std::size_t>(world.detour_profile);
            totals[bucket] += *away;
            ++counts[bucket];
        }
        for (const std::int64_t count : counts) {
            CHECK(count > 0);
        }
        CHECK(totals[0] * counts[1] < totals[1] * counts[0]);
        CHECK(totals[1] * counts[2] < totals[2] * counts[1]);
    }
}

TEST_CASE("the same seed always spreads its content the same way") {
    for (const LevelTier tier : {LevelTier::small, LevelTier::x_large}) {
        const WorldGenerationResult first = generate_level(tier, 0xC0FFEEull);
        const WorldGenerationResult second = generate_level(tier, 0xC0FFEEull);
        REQUIRE(std::holds_alternative<GeneratedWorld>(first));
        REQUIRE(std::holds_alternative<GeneratedWorld>(second));
        CHECK(std::get<GeneratedWorld>(first).detour_profile ==
              std::get<GeneratedWorld>(second).detour_profile);
    }
}

}  // TEST_SUITE("level_feature")
