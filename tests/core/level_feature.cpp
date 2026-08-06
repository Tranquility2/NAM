#include <doctest/doctest.h>

#include <array>
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
#include "vantage.h"
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

// Every cell's detour price at once. Asking `detour_of` per candidate runs two
// searches per cell, which is fast enough by hand but times out under the
// sanitizers when a test scans whole zones across a spread of seeds; two sweeps
// per map answer the same question for every cell.
class DetourField {
  public:
    DetourField(const Map& map, Coordinates exit_cell)
        : width_(map.width()),
          from_spawn_(sweep(map, map.spawn())),
          from_exit_(sweep(map, exit_cell)) {
        const std::optional<std::int64_t> direct = at(from_spawn_, exit_cell);
        direct_ = direct.value_or(-1);
    }

    [[nodiscard]] bool has_direct_walk() const noexcept { return direct_ >= 0; }

    [[nodiscard]] std::int64_t direct_walk() const noexcept { return direct_; }

    [[nodiscard]] std::optional<std::int64_t> detour(Coordinates cell) const {
        const std::optional<std::int64_t> to_cell = at(from_spawn_, cell);
        const std::optional<std::int64_t> to_exit = at(from_exit_, cell);
        if (!to_cell || !to_exit || !has_direct_walk()) {
            return std::nullopt;
        }
        return *to_cell + *to_exit - direct_;
    }

  private:
    static constexpr std::int64_t kUnreached = -1;

    [[nodiscard]] static std::vector<std::int64_t> sweep(const Map& map, Coordinates origin) {
        std::vector<std::int64_t> distance(map.width() * map.height(), kUnreached);
        if (!is_walkable(map.terrain_at(origin))) {
            return distance;
        }
        std::vector<Coordinates> queue{origin};
        distance[static_cast<std::size_t>(origin.y) * map.width() +
                 static_cast<std::size_t>(origin.x)] = 0;
        for (std::size_t head = 0; head < queue.size(); ++head) {
            const Coordinates cell = queue[head];
            const std::size_t index =
                static_cast<std::size_t>(cell.y) * map.width() + static_cast<std::size_t>(cell.x);
            const std::int64_t next = distance[index] + 1;
            const Coordinates neighbours[4] = {{cell.x + 1, cell.y},
                                               {cell.x - 1, cell.y},
                                               {cell.x, cell.y + 1},
                                               {cell.x, cell.y - 1}};
            for (const Coordinates neighbour : neighbours) {
                if (neighbour.x < 0 || neighbour.y < 0) continue;
                if (static_cast<std::size_t>(neighbour.x) >= map.width()) continue;
                if (static_cast<std::size_t>(neighbour.y) >= map.height()) continue;
                if (!is_walkable(map.terrain_at(neighbour))) continue;
                const std::size_t at_neighbour = static_cast<std::size_t>(neighbour.y) *
                                                     map.width() +
                                                 static_cast<std::size_t>(neighbour.x);
                if (distance[at_neighbour] != kUnreached) continue;
                distance[at_neighbour] = next;
                queue.push_back(neighbour);
            }
        }
        return distance;
    }

    [[nodiscard]] std::optional<std::int64_t> at(const std::vector<std::int64_t>& field,
                                                 Coordinates cell) const {
        const std::size_t index =
            static_cast<std::size_t>(cell.y) * width_ + static_cast<std::size_t>(cell.x);
        if (index >= field.size() || field[index] == kUnreached) {
            return std::nullopt;
        }
        return field[index];
    }

    std::size_t width_;
    std::vector<std::int64_t> from_spawn_;
    std::vector<std::int64_t> from_exit_;
    std::int64_t direct_ = -1;
};

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
    CHECK(movement(first).granted_wide_reveal());
    CHECK_FALSE(movement(first).discovery_recorded);

    // Leaving and returning does not fire it again.
    static_cast<void>(state.move(Direction::down));
    const GameEvent again = state.move(Direction::up);
    CHECK(movement(again).outcome.result == MoveResult::moved);
    CHECK_FALSE(movement(again).granted_wide_reveal());
}

TEST_CASE("a vantage point is an ordinary walkable cell and never blocks") {
    GameState state(arena({LevelFeature{Coordinates{2, 1}, LevelFeatureKind::vantage_point}},
                          Coordinates{5, 3}));

    const GameEvent event = state.move(Direction::right);
    CHECK(movement(event).outcome.result == MoveResult::moved);
    CHECK(movement(event).outcome.terrain == Terrain::open);
    CHECK(state.actor_position() == Coordinates{2, 1});
}

TEST_CASE("a vantage point reveals ground the terrain under it never could") {
    // A wide arena so no reveal is clipped by the map edges. What a vantage point
    // shows depends on the ground it was built on, so each kind is checked against
    // both what its own terrain would have shown and what the next kind down does.
    struct Ground {
        Terrain terrain;
        VantageKind kind;
    };
    for (const Ground ground : {Ground{Terrain::open, VantageKind::cairn},
                                Ground{Terrain::hill, VantageKind::lookout},
                                Ground{Terrain::mountain, VantageKind::summit}}) {
        constexpr std::size_t width = 41;
        constexpr std::size_t height = 33;
        std::vector<Terrain> cells(width * height, Terrain::open);
        for (std::size_t x = 0; x < width; ++x) {
            cells[x] = Terrain::cliff;
            cells[(height - 1) * width + x] = Terrain::cliff;
        }
        for (std::size_t y = 1; y + 1 < height; ++y) {
            cells[y * width] = Terrain::cliff;
            cells[y * width + width - 1] = Terrain::cliff;
        }
        const Coordinates spawn{20, 16};
        const Coordinates viewpoint{21, 16};
        cells[static_cast<std::size_t>(viewpoint.y) * width +
              static_cast<std::size_t>(viewpoint.x)] = ground.terrain;

        LevelLayout layout;
        layout.exit = Coordinates{1, 1};
        layout.features = {LevelFeature{viewpoint, LevelFeatureKind::vantage_point}};
        GameState state(Map(width, height, std::move(cells), spawn, std::move(layout)));

        const int reach = vantage_reveal_radius_of(ground.kind);
        const Coordinates within{viewpoint.x + reach, viewpoint.y};
        const Coordinates beyond{viewpoint.x + reach + 1, viewpoint.y};
        REQUIRE(state.visibility().at(within) == CellVisibility::unexplored);

        const GameEvent event = state.move(Direction::right);
        REQUIRE(movement(event).wide_reveal_radius == reach);
        CHECK(state.visibility().at(within) == CellVisibility::visible);
        CHECK(state.visibility().at(beyond) != CellVisibility::visible);
        // Climbing always beats merely standing there, even on a mountain.
        CHECK(visibility_radius_of(ground.terrain) < reach);
    }
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
    CHECK_FALSE(movement(first).granted_wide_reveal());
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

TEST_CASE("the vantage kind identifiers are stable and non-localized") {
    CHECK(std::string(to_string(VantageKind::cairn)) == "cairn");
    CHECK(std::string(to_string(VantageKind::lookout)) == "lookout");
    CHECK(std::string(to_string(VantageKind::summit)) == "summit");
}

TEST_CASE("a vantage point takes its kind from the ground and the higher ground sees further") {
    // The map and the name can never disagree because there is only one of them:
    // the kind is read back off the terrain rather than stored beside it.
    CHECK(vantage_kind_of(Terrain::mountain) == VantageKind::summit);
    CHECK(vantage_kind_of(Terrain::hill) == VantageKind::lookout);
    for (const Terrain terrain :
         {Terrain::open, Terrain::fields, Terrain::forest, Terrain::shallow_water,
          Terrain::deep_water, Terrain::cliff}) {
        CHECK(vantage_kind_of(terrain) == VantageKind::cairn);
    }

    CHECK(vantage_rank_of(VantageKind::cairn) < vantage_rank_of(VantageKind::lookout));
    CHECK(vantage_rank_of(VantageKind::lookout) < vantage_rank_of(VantageKind::summit));
    CHECK(vantage_reveal_radius_of(VantageKind::cairn) <
          vantage_reveal_radius_of(VantageKind::lookout));
    CHECK(vantage_reveal_radius_of(VantageKind::lookout) <
          vantage_reveal_radius_of(VantageKind::summit));

    // Every kind is worth the walk: each one out-sees simply standing on the
    // ground it was built on.
    for (const Terrain terrain :
         {Terrain::open, Terrain::fields, Terrain::forest, Terrain::shallow_water,
          Terrain::hill, Terrain::mountain}) {
        CHECK(visibility_radius_of(terrain) <
              vantage_reveal_radius_of(vantage_kind_of(terrain)));
    }
}

TEST_CASE("a vantage point takes the best ground on offer without charging for it") {
    // Preferring high ground has to be free. If a level would pay even one extra
    // move for a summit the authored ordering of the content could invert, so the
    // preference only ever breaks a tie between cells that cost exactly the same.
    for (const LevelTier tier :
         {LevelTier::small, LevelTier::medium, LevelTier::large, LevelTier::x_large}) {
        for (std::uint64_t index = 0; index < kProbeSeeds; ++index) {
            const WorldGenerationResult result = generate_level(tier, probe_seed(index));
            REQUIRE(std::holds_alternative<GeneratedWorld>(result));
            const GeneratedWorld& world = std::get<GeneratedWorld>(result);
            const LevelTemplate level = template_of(tier, world.exit_corner);
            const std::vector<LevelFeature>& placed = world.map.layout().features;
            REQUIRE(placed.size() == 3u);

            const DetourField field(world.map, world.exit_cell);
            REQUIRE(field.has_direct_walk());
            const std::uint64_t direct = static_cast<std::uint64_t>(field.direct_walk());

            for (std::size_t slot = 0; slot < placed.size(); ++slot) {
                if (placed[slot].kind != LevelFeatureKind::vantage_point) continue;
                const ContentSlot& authored = level.content_slots[slot];
                const std::int64_t target = static_cast<std::int64_t>(
                    target_detour_moves(authored.band, world.detour_profile, direct));

                const std::optional<std::int64_t> chosen = field.detour(placed[slot].position);
                REQUIRE(chosen.has_value());
                const std::int64_t chosen_miss =
                    *chosen > target ? (*chosen - target) : (target - *chosen);
                const std::size_t chosen_rank =
                    vantage_rank_of(vantage_kind_of(world.map.terrain_at(placed[slot].position)));

                for (int y = authored.zone.min_y; y <= authored.zone.max_y; ++y) {
                    for (int x = authored.zone.min_x; x <= authored.zone.max_x; ++x) {
                        const Coordinates candidate{x, y};
                        if (!is_walkable(world.map.terrain_at(candidate))) continue;
                        if (candidate == world.map.spawn()) continue;
                        if (candidate == world.exit_cell) continue;
                        if (level.entry_zone.contains(candidate)) continue;
                        bool taken = false;
                        for (std::size_t before = 0; before < slot; ++before) {
                            if (placed[before].position == candidate) taken = true;
                        }
                        if (taken) continue;
                        const std::optional<std::int64_t> other = field.detour(candidate);
                        if (!other) continue;
                        const std::int64_t miss =
                            *other > target ? (*other - target) : (target - *other);
                        if (miss != chosen_miss) continue;
                        CHECK(vantage_rank_of(vantage_kind_of(world.map.terrain_at(candidate))) <=
                              chosen_rank);
                    }
                }
            }
        }
    }
}

TEST_CASE("every tier offers all three kinds of viewpoint across a spread of seeds") {
    // Kind follows terrain, so this is really a claim about generation: no tier
    // is so flat that the player only ever meets cairns.
    for (const LevelTier tier :
         {LevelTier::small, LevelTier::medium, LevelTier::large, LevelTier::x_large}) {
        std::array<int, vantage_kind_count> seen{};
        for (std::uint64_t index = 0; index < kProbeSeeds; ++index) {
            const WorldGenerationResult result = generate_level(tier, probe_seed(index));
            REQUIRE(std::holds_alternative<GeneratedWorld>(result));
            const GeneratedWorld& world = std::get<GeneratedWorld>(result);
            for (const LevelFeature& feature : world.map.layout().features) {
                if (feature.kind != LevelFeatureKind::vantage_point) continue;
                seen[vantage_rank_of(vantage_kind_of(world.map.terrain_at(feature.position)))] += 1;
            }
        }
        for (const int count : seen) {
            CHECK(count > 0);
        }
    }
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

            const DetourField field(world.map, world.exit_cell);
            REQUIRE(field.has_direct_walk());
            const std::uint64_t direct = static_cast<std::uint64_t>(field.direct_walk());

            for (std::size_t slot = 0; slot < placed.size(); ++slot) {
                const ContentSlot& authored = level.content_slots[slot];
                const std::int64_t target = static_cast<std::int64_t>(
                    target_detour_moves(authored.band, world.detour_profile, direct));

                const std::optional<std::int64_t> chosen = field.detour(placed[slot].position);
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
                        const std::optional<std::int64_t> other = field.detour(candidate);
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
