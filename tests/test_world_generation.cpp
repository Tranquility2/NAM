#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "map.h"
#include "terrain.h"
#include "world_generation.h"

namespace {

// The exact clustered serialization TEST-006 locks for the "glass-river" seed.
// Every feature is a connected blob grown to an exact size, so any change to the
// generator's pass order, target sizes, direction order, growth proposal limit,
// hill-halo construction, or acceptance rules would move these bytes. This doubles
// as a full compatibility fixture.
constexpr std::string_view kGlassRiverGolden =
    "=============================\n"
    "|.....x^^@@@^...............|\n"
    "|...xxx^@@^@^.........~.....|\n"
    "|....xx^^^^^^........~~~....|\n"
    "|....xxxxx..........~~~~~...|\n"
    "|....xxxxx.x..........~~~~~.|\n"
    "|....x....xxx.........~~~~..|\n"
    "|....x....xxx...............|\n"
    "|..x..=|...xx...............|\n"
    "|..x.=|=...xxxx.............|\n"
    "|..xx^^^..xxxxxxx...~~~.....|\n"
    "|xxxx^@^^^^^xx|...~~~~~~....|\n"
    "|.xxx^@@@@@^..=..~~~~~......|\n"
    "|.xxx^@^^@^^..|=|=..........|\n"
    "=============================";

// The numeric seed FNV-1a produces for "glass-river" (REQ-005).
constexpr std::uint64_t kGlassRiverSeed = 0x0F4289EAF4A1813Cull;

// Exact clustered terrain totals every accepted world must contain (REQ-016).
constexpr std::size_t kWaterCells = 32u;
constexpr std::size_t kFieldCells = 54u;
constexpr std::size_t kMountainCells = 14u;
constexpr std::size_t kBarrierCells = 11u;
constexpr std::size_t kMinHillCells = 20u;

// Pull the accepted world out of a result or fail the test loudly.
const GeneratedWorld& require_world(const WorldGenerationResult& result) {
    REQUIRE(std::holds_alternative<GeneratedWorld>(result));
    return std::get<GeneratedWorld>(result);
}

// Split the serialized map into rows for glyph-level inspection.
std::vector<std::string> to_rows(const Map& map) {
    std::vector<std::string> rows;
    std::string current;
    for (const char character : map.to_string()) {
        if (character == '\n') {
            rows.push_back(current);
            current.clear();
        } else {
            current.push_back(character);
        }
    }
    rows.push_back(current);
    return rows;
}

bool glyph_walkable(char glyph) {
    return glyph != '=' && glyph != '|';
}

bool is_water_glyph(char glyph) { return glyph == '~'; }
bool is_field_glyph(char glyph) { return glyph == 'x'; }
bool is_mountain_glyph(char glyph) { return glyph == '@'; }
bool is_barrier_glyph(char glyph) { return glyph == '=' || glyph == '|'; }

// The independently re-derived REQ-016 invariants for one map.
struct MapInvariants {
    bool boundary_intact = false;
    bool spawn_protected_open = false;
    bool fully_connected = false;
    bool hills_touch_mountains = false;
    std::size_t water = 0;
    std::size_t fields = 0;
    std::size_t mountain = 0;
    std::size_t hill = 0;
    std::size_t barrier = 0;
    std::vector<std::size_t> water_components;
    std::vector<std::size_t> field_components;
    std::vector<std::size_t> mountain_components;
    std::vector<std::size_t> barrier_components;
};

// Sizes of the cardinal-connected interior components matching `match`. The search
// is iterative and stays inside the interior, so an interior barrier ridge that
// touches the boundary wall is never merged with the boundary ring.
std::vector<std::size_t> interior_component_sizes(const std::vector<std::string>& rows,
                                                  std::size_t width, std::size_t height,
                                                  bool (*match)(char)) {
    std::vector<char> visited(width * height, 0);
    std::vector<std::size_t> sizes;
    std::vector<std::size_t> stack;

    const auto inside = [width, height](std::size_t px, std::size_t py) {
        return px >= 1u && px + 1u < width && py >= 1u && py + 1u < height;
    };

    for (std::size_t y = 1; y + 1 < height; ++y) {
        for (std::size_t x = 1; x + 1 < width; ++x) {
            const std::size_t start = y * width + x;
            if (visited[start] != 0 || !match(rows[y][x])) {
                continue;
            }
            std::size_t size = 0;
            visited[start] = 1;
            stack.clear();
            stack.push_back(start);
            while (!stack.empty()) {
                const std::size_t current = stack.back();
                stack.pop_back();
                ++size;
                const std::size_t cx = current % width;
                const std::size_t cy = current / width;
                const auto consider = [&](std::size_t nx, std::size_t ny) {
                    if (!inside(nx, ny)) {
                        return;
                    }
                    const std::size_t neighbour = ny * width + nx;
                    if (visited[neighbour] == 0 && match(rows[ny][nx])) {
                        visited[neighbour] = 1;
                        stack.push_back(neighbour);
                    }
                };
                if (cx > 0u) consider(cx - 1u, cy);
                if (cx + 1u < width) consider(cx + 1u, cy);
                if (cy > 0u) consider(cx, cy - 1u);
                if (cy + 1u < height) consider(cx, cy + 1u);
            }
            sizes.push_back(size);
        }
    }
    return sizes;
}

// True when every hill cell is an eight-neighbour of at least one mountain cell.
bool all_hills_touch_mountains(const std::vector<std::string>& rows,
                               std::size_t width, std::size_t height) {
    for (std::size_t y = 1; y + 1 < height; ++y) {
        for (std::size_t x = 1; x + 1 < width; ++x) {
            if (rows[y][x] != '^') {
                continue;
            }
            bool touches = false;
            for (int dy = -1; dy <= 1 && !touches; ++dy) {
                for (int dx = -1; dx <= 1 && !touches; ++dx) {
                    if (dx == 0 && dy == 0) {
                        continue;
                    }
                    const std::size_t nx = static_cast<std::size_t>(static_cast<int>(x) + dx);
                    const std::size_t ny = static_cast<std::size_t>(static_cast<int>(y) + dy);
                    if (nx >= 1u && nx + 1u < width && ny >= 1u && ny + 1u < height &&
                        rows[ny][nx] == '@') {
                        touches = true;
                    }
                }
            }
            if (!touches) {
                return false;
            }
        }
    }
    return true;
}

// An independent, test-side inspection over a serialized map. It deliberately
// re-derives the counts, components, hill relationship, and connectivity from the
// rendered glyphs rather than reusing the generator's internal validator, so a
// bug in one cannot mask a bug in the other.
MapInvariants inspect(const Map& map) {
    const std::size_t width = map.width();
    const std::size_t height = map.height();
    const std::vector<std::string> rows = to_rows(map);

    MapInvariants result;

    result.boundary_intact = true;
    for (std::size_t x = 0; x < width; ++x) {
        if (rows[0][x] != '=' || rows[height - 1][x] != '=') {
            result.boundary_intact = false;
        }
    }
    for (std::size_t y = 1; y + 1 < height; ++y) {
        if (rows[y][0] != '|' || rows[y][width - 1] != '|') {
            result.boundary_intact = false;
        }
    }

    // The whole protected 3x3 spawn square must be open, not just the spawn cell.
    result.spawn_protected_open = true;
    for (std::size_t y = 6; y <= 8; ++y) {
        for (std::size_t x = 13; x <= 15; ++x) {
            if (rows[y][x] != '.') {
                result.spawn_protected_open = false;
            }
        }
    }

    for (std::size_t y = 1; y + 1 < height; ++y) {
        for (std::size_t x = 1; x + 1 < width; ++x) {
            switch (rows[y][x]) {
                case '~': ++result.water; break;
                case 'x': ++result.fields; break;
                case '@': ++result.mountain; break;
                case '^': ++result.hill; break;
                case '=':
                case '|': ++result.barrier; break;
                default: break;
            }
        }
    }

    result.hills_touch_mountains = all_hills_touch_mountains(rows, width, height);
    result.water_components = interior_component_sizes(rows, width, height, is_water_glyph);
    result.field_components = interior_component_sizes(rows, width, height, is_field_glyph);
    result.mountain_components = interior_component_sizes(rows, width, height, is_mountain_glyph);
    result.barrier_components = interior_component_sizes(rows, width, height, is_barrier_glyph);

    // Iterative flood fill from the spawn over cardinal neighbours.
    std::size_t total_walkable = 0;
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            if (glyph_walkable(rows[y][x])) {
                ++total_walkable;
            }
        }
    }

    std::vector<char> visited(width * height, 0);
    std::vector<std::size_t> frontier;
    const Coordinates spawn = map.spawn();
    const std::size_t spawn_index =
        static_cast<std::size_t>(spawn.y) * width + static_cast<std::size_t>(spawn.x);
    visited[spawn_index] = 1;
    frontier.push_back(spawn_index);
    std::size_t reached = 1;
    while (!frontier.empty()) {
        const std::size_t current = frontier.back();
        frontier.pop_back();
        const std::size_t cx = current % width;
        const std::size_t cy = current / width;
        const auto visit = [&](std::size_t nx, std::size_t ny) {
            const std::size_t index = ny * width + nx;
            if (visited[index] == 0 && glyph_walkable(rows[ny][nx])) {
                visited[index] = 1;
                ++reached;
                frontier.push_back(index);
            }
        };
        if (cx > 0u) visit(cx - 1u, cy);
        if (cx + 1u < width) visit(cx + 1u, cy);
        if (cy > 0u) visit(cx, cy - 1u);
        if (cy + 1u < height) visit(cx, cy + 1u);
    }
    result.fully_connected = reached == total_walkable;

    return result;
}

// Whether component sizes stay within [min_components, max_components] and no
// component is below min_size.
bool components_within(const std::vector<std::size_t>& sizes, std::size_t min_components,
                       std::size_t max_components, std::size_t min_size) {
    if (sizes.size() < min_components || sizes.size() > max_components) {
        return false;
    }
    for (const std::size_t size : sizes) {
        if (size < min_size) {
            return false;
        }
    }
    return true;
}

// The full REQ-016 acceptance predicate, re-derived independently of the generator.
bool satisfies_all_invariants(const Map& map) {
    const MapInvariants inv = inspect(map);
    return inv.boundary_intact && inv.spawn_protected_open && inv.fully_connected &&
           inv.hills_touch_mountains && inv.water == kWaterCells && inv.fields == kFieldCells &&
           inv.mountain == kMountainCells && inv.barrier == kBarrierCells &&
           inv.hill >= kMinHillCells &&
           components_within(inv.water_components, 1u, 2u, 14u) &&
           components_within(inv.field_components, 1u, 3u, 14u) &&
           components_within(inv.mountain_components, 1u, 2u, 6u) &&
           components_within(inv.barrier_components, 1u, 2u, 5u);
}

}  // namespace

TEST_SUITE("game") {

TEST_CASE("hash_seed_text reproduces the locked FNV-1a vectors") {
    CHECK(hash_seed_text("") == 0xCBF29CE484222325ull);        // TEST-001
    CHECK(hash_seed_text("a") == 0xAF63DC4C8601EC8Cull);       // TEST-002
    CHECK(hash_seed_text("hello") == 0xA430D84680AABD0Bull);   // TEST-003
    CHECK(hash_seed_text("glass-river") == kGlassRiverSeed);   // TEST-004
}

TEST_CASE("hash_seed_text hashes every byte including embedded zeros") {
    // TEST-005: build the input programmatically so an embedded NUL is preserved
    // (a C string literal would truncate at the zero). The full string must hash
    // differently from its pre-zero prefix, proving no byte is skipped.
    std::string with_zero = "ab";
    with_zero.push_back('\0');
    with_zero += "cd";
    REQUIRE(with_zero.size() == 5);

    const std::string_view full{with_zero.data(), with_zero.size()};
    const std::string_view prefix{with_zero.data(), 2};
    CHECK(hash_seed_text(full) != hash_seed_text(prefix));

    // Hashing is a pure function of the exact bytes.
    CHECK(hash_seed_text(full) == hash_seed_text(std::string_view{with_zero.data(), with_zero.size()}));
}

TEST_CASE("the glass-river seed grows the exact clustered golden map on the first attempt") {
    // TEST-006 / TEST-007.
    const WorldGenerationResult result = generate_tiny_world(kGlassRiverSeed);
    const GeneratedWorld& world = require_world(result);

    CHECK(world.numeric_seed == kGlassRiverSeed);
    CHECK(world.generation_attempt == 0u);
    CHECK(world.map.width() == tiny_world_width);
    CHECK(world.map.height() == tiny_world_height);
    CHECK(world.map.spawn() == tiny_world_spawn);
    CHECK(world.map.terrain_at(tiny_world_spawn) == Terrain::open);

    // Wrap the map's serialization in std::string on both sides so doctest never
    // decomposes a std::string_view (GUD-003 / RISK-004).
    CHECK(std::string(world.map.to_string()) == std::string(kGlassRiverGolden));

    // Exact clustered terrain totals for the fixture.
    const MapInvariants inv = inspect(world.map);
    CHECK(inv.water == kWaterCells);
    CHECK(inv.fields == kFieldCells);
    CHECK(inv.mountain == kMountainCells);
    CHECK(inv.barrier == kBarrierCells);
    CHECK(inv.hill >= kMinHillCells);
    CHECK(satisfies_all_invariants(world.map));

    // Regenerating the same seed is byte-identical and reports the same attempt.
    const WorldGenerationResult again = generate_tiny_world(kGlassRiverSeed);
    const GeneratedWorld& repeat = require_world(again);
    CHECK(repeat.generation_attempt == world.generation_attempt);
    CHECK(std::string(repeat.map.to_string()) == std::string(world.map.to_string()));
}

TEST_CASE("hashing text and generating from the hash matches direct generation") {
    const WorldGenerationResult from_text = generate_tiny_world(hash_seed_text("glass-river"));
    const GeneratedWorld& world = require_world(from_text);
    CHECK(std::string(world.map.to_string()) == std::string(kGlassRiverGolden));
}

TEST_CASE("a large representative sample of seeds satisfies every clustered invariant") {
    // TEST-008: 512 deterministic seed texts (seed-0 .. seed-511). Every one must
    // grow a valid world; none may exhaust the candidate limit.
    for (int i = 0; i < 512; ++i) {
        const std::string seed_text = "seed-" + std::to_string(i);
        const WorldGenerationResult result = generate_tiny_world(hash_seed_text(seed_text));
        REQUIRE_MESSAGE(std::holds_alternative<GeneratedWorld>(result),
                        "seed exhausted candidates: " << seed_text);
        const GeneratedWorld& world = std::get<GeneratedWorld>(result);
        CHECK(world.map.width() == tiny_world_width);
        CHECK(world.map.height() == tiny_world_height);
        CHECK(world.map.spawn() == tiny_world_spawn);
        CHECK(world.generation_attempt < tiny_world_candidate_limit);
        CHECK_MESSAGE(satisfies_all_invariants(world.map), "invariant failed for: " << seed_text);
    }
}

TEST_CASE("distinct seed text yields distinct hashes and distinct maps") {
    // TEST-009.
    const std::uint64_t a = hash_seed_text("glass-river");
    const std::uint64_t b = hash_seed_text("stone-hollow");
    CHECK(a != b);

    // Keep both results in named locals: require_world returns a reference into
    // the result, which must outlive the comparison below.
    const WorldGenerationResult result_a = generate_tiny_world(a);
    const WorldGenerationResult result_b = generate_tiny_world(b);
    const GeneratedWorld& world_a = require_world(result_a);
    const GeneratedWorld& world_b = require_world(result_b);
    CHECK(std::string(world_a.map.to_string()) != std::string(world_b.map.to_string()));

    // Both distinct maps must still satisfy every clustered invariant.
    CHECK(satisfies_all_invariants(world_a.map));
    CHECK(satisfies_all_invariants(world_b.map));
}

TEST_CASE("each generation owns its RNG so interleaving cannot change output") {
    // TEST-010: capture the baseline, generate unrelated seeds in between, then
    // regenerate the original. Because each call constructs its own engine, the
    // repeated output must be byte-identical.
    const WorldGenerationResult first = generate_tiny_world(kGlassRiverSeed);
    const std::string baseline = std::string(require_world(first).map.to_string());

    for (int i = 0; i < 8; ++i) {
        const std::string other_text = "interleave-" + std::to_string(i);
        (void)generate_tiny_world(hash_seed_text(other_text));
    }

    const WorldGenerationResult again = generate_tiny_world(kGlassRiverSeed);
    const std::string repeat = std::string(require_world(again).map.to_string());
    CHECK(repeat == baseline);
}

TEST_CASE("the generation error code maps to a stable identifier string") {
    // Wrap in std::string so doctest decomposes std::string, not std::string_view.
    CHECK(std::string(to_string(WorldGenerationErrorCode::candidate_limit_exhausted)) ==
          "candidate_limit_exhausted");
}

}  // TEST_SUITE("game")
