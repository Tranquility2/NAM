#include "world_generation.h"

#include <array>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include "level_template.h"
#include "pcg32.h"
#include "set_piece.h"
#include "terrain.h"
#include "vantage.h"

// Generation is authored shape plus seeded content. For one tier the recipe is:
//
//   1. take the tier's LevelTemplate (entry zone, exit zone, route, spur slots);
//   2. draw the exact exit inside the fixed exit zone and decide which optional
//      spurs open;
//   3. carve that route and reserve its cells;
//   4. grow the tier's terrain budget everywhere else;
//   5. accept only if the route survived and every clustered invariant holds;
//   6. seed one cell per content slot among that slot's walkable cells.
//
// Reserving the route before growing terrain is what makes a level solvable by
// construction: the corridor from spawn to exit cannot be painted over, so the
// acceptance pass is a consistency check rather than rejection sampling.
//
// IMPORTANT: every constant and pass below is part of a tier's compatibility
// contract. Any change to the FNV constants, the stream selector, the seeded-route
// draw order, the tier template, the eligible-cell traversal order, the feature
// target sizes, the pass order, the cardinal direction order, the growth proposal
// limit, the barrier-orientation rule, the hill-halo construction, or the
// acceptance rules changes every generated world for every seed. Such a change
// must later be gated behind an explicit recipe version rather than silently
// altering existing output.
//
// The recipe is shared by every tier and parameterised by GenerationProfile and
// LevelTemplate, so Small, Medium, Large, and X-Large share one implementation
// while each keeps its own numbers and its own shape.

namespace {

// 64-bit FNV-1a parameters (see draft-eastlake-fnv). These are the released
// algorithm constants; the offset basis also equals hash_seed_text("").
constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

// The bounded-growth proposal budget: a blob that has not reached its target size
// after target_size * this many proposals fails the whole candidate.
constexpr std::size_t kProposalBudgetFactor = 64u;

// The count limits and minimum component size shared by one clustered feature.
struct ComponentLimits {
    std::size_t min_components = 1u;
    std::size_t max_components = 1u;
    std::size_t min_component_size = 1u;
};

// Everything one tier needs to grow and validate a candidate. Grouping the whole
// recipe here is what lets Small and Medium share a single implementation while
// each keeps its own released numbers.
struct GenerationProfile {
    std::size_t width = 0u;
    std::size_t height = 0u;
    Coordinates spawn{};
    // The Pcg32 stream selector. Distinct per tier so one seed cannot correlate
    // two tiers of the same expedition.
    std::uint64_t stream = 0u;
    // The ordered blob target sizes for each pass. Order and values are part of
    // the tier's compatibility contract.
    std::array<std::size_t, 2> water_blobs{};
    std::array<std::size_t, 2> mountain_blobs{};
    std::array<std::size_t, 3> field_blobs{};
    std::array<std::size_t, 2> forest_blobs{};
    std::array<std::size_t, 2> barrier_blobs{};
    // Exact interior terrain totals every accepted candidate must contain: the
    // sums of the blob targets above. `water_cells` counts shallow and deep water
    // together, because deepening converts cells of an already grown body rather
    // than adding new ones.
    std::size_t water_cells = 0u;
    std::size_t field_cells = 0u;
    std::size_t forest_cells = 0u;
    std::size_t mountain_cells = 0u;
    std::size_t barrier_cells = 0u;
    // The hill halo and the deep-water core are deterministic, but their sizes
    // vary with the shape of the feature they derive from, so both are bounded
    // below rather than fixed exactly.
    std::size_t min_hill_cells = 0u;
    std::size_t min_deep_water_cells = 0u;
    ComponentLimits water{};
    ComponentLimits fields{};
    ComponentLimits forest{};
    ComponentLimits mountain{};
    ComponentLimits barrier{};
};

// The released Tiny World recipe, retained unchanged as the Medium tier. The
// stream bytes spell "TINY" and are a released compatibility constant.
[[nodiscard]] constexpr GenerationProfile medium_profile() noexcept {
    GenerationProfile profile;
    profile.width = dimensions_of(LevelTier::medium).width;
    profile.height = dimensions_of(LevelTier::medium).height;
    profile.spawn = center_spawn_of(LevelTier::medium);
    profile.stream = 0x54494E59ULL;
    profile.water_blobs = {18u, 14u};
    profile.mountain_blobs = {8u, 6u};
    profile.field_blobs = {22u, 18u, 14u};
    profile.forest_blobs = {16u, 12u};
    profile.barrier_blobs = {6u, 5u};
    profile.water_cells = 32u;
    profile.field_cells = 54u;
    profile.forest_cells = 28u;
    profile.mountain_cells = 14u;
    profile.barrier_cells = 11u;
    profile.min_hill_cells = 20u;
    profile.min_deep_water_cells = 1u;
    profile.water = ComponentLimits{1u, 2u, 14u};
    profile.fields = ComponentLimits{1u, 3u, 14u};
    profile.forest = ComponentLimits{1u, 2u, 12u};
    profile.mountain = ComponentLimits{1u, 2u, 6u};
    profile.barrier = ComponentLimits{1u, 2u, 5u};
    return profile;
}

// The Small teaching tier. Its interior is 19x9 against Medium's 27x13, so every
// budget is scaled to roughly half while keeping the same feature vocabulary: two
// water bodies, two mountain cores, three field regions, and two barrier ridges.
// The stream bytes spell "SMAL".
[[nodiscard]] constexpr GenerationProfile small_profile() noexcept {
    GenerationProfile profile;
    profile.width = dimensions_of(LevelTier::small).width;
    profile.height = dimensions_of(LevelTier::small).height;
    profile.spawn = center_spawn_of(LevelTier::small);
    profile.stream = 0x534D414CULL;
    profile.water_blobs = {11u, 9u};
    profile.mountain_blobs = {4u, 3u};
    profile.field_blobs = {11u, 9u, 7u};
    profile.forest_blobs = {8u, 6u};
    profile.barrier_blobs = {3u, 3u};
    profile.water_cells = 20u;
    profile.field_cells = 27u;
    profile.forest_cells = 14u;
    profile.mountain_cells = 7u;
    profile.barrier_cells = 6u;
    profile.min_hill_cells = 8u;
    profile.min_deep_water_cells = 0u;
    profile.water = ComponentLimits{1u, 2u, 9u};
    profile.fields = ComponentLimits{1u, 3u, 7u};
    profile.forest = ComponentLimits{1u, 2u, 6u};
    profile.mountain = ComponentLimits{1u, 2u, 3u};
    profile.barrier = ComponentLimits{1u, 2u, 3u};
    return profile;
}

// The recipe for a tier. Large and X-Large are not authored yet (Phase 2), so
// they reuse the Medium budgets on their own dimensions and streams; that is a
// placeholder, not a released contract.
[[nodiscard]] constexpr GenerationProfile profile_of(LevelTier tier) noexcept {
    switch (tier) {
        case LevelTier::small:  return small_profile();
        case LevelTier::medium: return medium_profile();
        case LevelTier::large: {
            GenerationProfile profile = medium_profile();
            profile.width = dimensions_of(LevelTier::large).width;
            profile.height = dimensions_of(LevelTier::large).height;
            profile.spawn = center_spawn_of(LevelTier::large);
            profile.stream = 0x4C415247ULL;  // "LARG"
            return profile;
        }
        case LevelTier::x_large: {
            GenerationProfile profile = medium_profile();
            profile.width = dimensions_of(LevelTier::x_large).width;
            profile.height = dimensions_of(LevelTier::x_large).height;
            profile.spawn = center_spawn_of(LevelTier::x_large);
            profile.stream = 0x584C5247ULL;  // "XLRG"
            return profile;
        }
    }
    return medium_profile();
}

// Row-major flat index into a profile.width * profile.height buffer.
[[nodiscard]] constexpr std::size_t cell_index(const GenerationProfile& profile, std::size_t x,
                                               std::size_t y) noexcept {
    return y * profile.width + x;
}

// True when (x, y) is an interior cell, i.e. not on the solid cliff boundary.
[[nodiscard]] constexpr bool is_interior(const GenerationProfile& profile, std::size_t x,
                                         std::size_t y) noexcept {
    return x >= 1u && x + 1u < profile.width && y >= 1u && y + 1u < profile.height;
}

// True when (x, y) is inside the protected 3x3 square centred on the spawn. These
// cells are never eligible for feature placement, so the spawn and its immediate
// neighbourhood always stay open.
[[nodiscard]] constexpr bool is_protected_spawn(const GenerationProfile& profile, std::size_t x,
                                                std::size_t y) noexcept {
    const std::size_t sx = static_cast<std::size_t>(profile.spawn.x);
    const std::size_t sy = static_cast<std::size_t>(profile.spawn.y);
    return x + 1u >= sx && x <= sx + 1u && y + 1u >= sy && y <= sy + 1u;
}

// An eligible cell for feature placement: interior, outside the protected spawn
// square, not reserved by the authored route, and currently open. Painted features
// and the boundary are never open, so a cell is annexed at most once and no pass
// overwrites an earlier feature. `reserved` marks the carved route skeleton, which
// no pass - including the hill halo - may paint, so the authored route survives
// generation as a fully open corridor.
[[nodiscard]] bool is_eligible(const GenerationProfile& profile, const std::vector<bool>& reserved,
                               const std::vector<Terrain>& cells, std::size_t x, std::size_t y) {
    return is_interior(profile, x, y) && !is_protected_spawn(profile, x, y) &&
           !reserved[cell_index(profile, x, y)] &&
           cells[cell_index(profile, x, y)] == Terrain::open;
}

// Paint a barrier glyph. Interior barrier ridges and the map border are the same
// impassable terrain, so this exists only to name the intent at its call site.
[[nodiscard]] constexpr Terrain barrier_glyph(std::size_t, std::size_t) noexcept {
    return Terrain::cliff;
}

// Grow one connected feature blob to an exact target size using the common
// deterministic procedure (REQ-012). Build the row-major list of currently
// eligible cells, pick a start with one bounded draw, then repeatedly pick an
// existing blob cell and a cardinal direction and annex the proposed neighbour
// when it is eligible. Every proposal is counted; the candidate fails if the
// target is not reached within target_size * kProposalBudgetFactor proposals.
// Direction indices are 0=up, 1=right, 2=down, 3=left. `paint` writes the chosen
// terrain for an annexed cell (it receives the cell coordinates so barriers can
// derive their orientation).
template <typename Paint>
[[nodiscard]] bool grow_blob(const GenerationProfile& profile, const std::vector<bool>& reserved,
                             Pcg32& engine, std::vector<Terrain>& cells, std::size_t target_size,
                             Paint paint) {
    std::vector<std::size_t> eligible;
    eligible.reserve(profile.width * profile.height);
    for (std::size_t y = 1; y + 1 < profile.height; ++y) {
        for (std::size_t x = 1; x + 1 < profile.width; ++x) {
            if (is_eligible(profile, reserved, cells, x, y)) {
                eligible.push_back(cell_index(profile, x, y));
            }
        }
    }
    if (eligible.empty()) {
        return false;
    }

    const std::uint32_t start_pick = engine.next_bounded(static_cast<std::uint32_t>(eligible.size()));
    const std::size_t start = eligible[start_pick];
    paint(cells, start % profile.width, start / profile.width);

    std::vector<std::size_t> blob;
    blob.reserve(target_size);
    blob.push_back(start);

    const std::size_t max_proposals = target_size * kProposalBudgetFactor;
    std::size_t proposals = 0;
    while (blob.size() < target_size) {
        if (proposals >= max_proposals) {
            return false;
        }
        const std::uint32_t blob_pick = engine.next_bounded(static_cast<std::uint32_t>(blob.size()));
        const std::uint32_t direction = engine.next_bounded(4u);
        ++proposals;

        const std::size_t cx = blob[blob_pick] % profile.width;
        const std::size_t cy = blob[blob_pick] / profile.width;
        std::size_t nx = cx;
        std::size_t ny = cy;
        switch (direction) {
            case 0u: ny = cy - 1u; break;  // up    (cy >= 1 for interior cells)
            case 1u: nx = cx + 1u; break;  // right
            case 2u: ny = cy + 1u; break;  // down
            default: nx = cx - 1u; break;  // left  (direction == 3; cx >= 1)
        }

        if (is_eligible(profile, reserved, cells, nx, ny)) {
            paint(cells, nx, ny);
            blob.push_back(cell_index(profile, nx, ny));
        }
    }
    return true;
}

// Stamp the deterministic one-cell hill halo around the mountains (REQ-014). No
// RNG is consumed. Every eligible eight-neighbour of any mountain is marked in a
// mask, then the interior is scanned in row-major order and each marked cell that
// is still open becomes a hill. Because later passes paint only open cells, the
// halo is never overwritten and every hill stays adjacent to a mountain.
void add_hill_halo(const GenerationProfile& profile, const std::vector<bool>& reserved,
                   std::vector<Terrain>& cells) {
    std::vector<bool> hill_mask(profile.width * profile.height, false);

    for (std::size_t y = 1; y + 1 < profile.height; ++y) {
        for (std::size_t x = 1; x + 1 < profile.width; ++x) {
            if (cells[cell_index(profile, x, y)] != Terrain::mountain) {
                continue;
            }
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) {
                        continue;
                    }
                    const std::size_t nx = static_cast<std::size_t>(static_cast<int>(x) + dx);
                    const std::size_t ny = static_cast<std::size_t>(static_cast<int>(y) + dy);
                    if (is_eligible(profile, reserved, cells, nx, ny)) {
                        hill_mask[cell_index(profile, nx, ny)] = true;
                    }
                }
            }
        }
    }

    for (std::size_t y = 1; y + 1 < profile.height; ++y) {
        for (std::size_t x = 1; x + 1 < profile.width; ++x) {
            const std::size_t idx = cell_index(profile, x, y);
            if (hill_mask[idx] && cells[idx] == Terrain::open) {
                cells[idx] = Terrain::hill;
            }
        }
    }
}

// True when a cell holds water of either depth.
[[nodiscard]] constexpr bool is_water(Terrain terrain) noexcept {
    return terrain == Terrain::shallow_water || terrain == Terrain::deep_water;
}

// Deepen the middle of every water body. No RNG is consumed. A shallow cell whose
// four cardinal neighbours are all shallow water is marked, then the marks are
// applied in one pass, so deepening never cascades: a deep cell is always fringed
// by the shallow water it was cut out of, which is what makes the barrier readable
// instead of arbitrary.
//
// This is the water counterpart of the hill halo. A mountain grows a walkable ring
// outward; a lake grows an impassable core inward. Both derive from a feature that
// was already placed, so neither can appear where the tier did not put one.
void deepen_water_cores(const GenerationProfile& profile, std::vector<Terrain>& cells) {
    std::vector<bool> deep_mask(profile.width * profile.height, false);

    for (std::size_t y = 1; y + 1 < profile.height; ++y) {
        for (std::size_t x = 1; x + 1 < profile.width; ++x) {
            const std::size_t idx = cell_index(profile, x, y);
            if (cells[idx] != Terrain::shallow_water) {
                continue;
            }
            const bool enclosed = cells[cell_index(profile, x, y - 1u)] == Terrain::shallow_water &&
                                  cells[cell_index(profile, x, y + 1u)] == Terrain::shallow_water &&
                                  cells[cell_index(profile, x - 1u, y)] == Terrain::shallow_water &&
                                  cells[cell_index(profile, x + 1u, y)] == Terrain::shallow_water;
            deep_mask[idx] = enclosed;
        }
    }

    for (std::size_t idx = 0; idx < cells.size(); ++idx) {
        if (deep_mask[idx]) {
            cells[idx] = Terrain::deep_water;
        }
    }
}

// Grow one complete candidate terrain buffer from the engine. The passes run in a
// fixed order so RNG consumption is deterministic: build the cliff boundary, grow
// the water bodies, the mountain cores, stamp the hill halo, grow the field
// regions, then the barrier ridges. Returns false if any growth pass exhausts its
// proposal budget, leaving the buffer for the caller to discard.
// One candidate's authored layer: the seeded exit, the carved route, and the mask
// of cells no terrain pass may touch.
struct RouteLayout {
    Coordinates exit_cell{};
    std::vector<Coordinates> route;
    std::vector<bool> reserved;
    // Where this level's terrain set-piece will go. It is reserved alongside the
    // route so no grown blob may enter it: the band's terrain is written after the
    // candidate is accepted, and reserving it first is what keeps the generator's
    // clustered terrain contract untouched by an authored overlay.
    SetPieceRegion set_piece{};
};

// Where a level's terrain set-piece goes: a band of columns spanning the whole
// interior height, between spawn and the exit.
//
// Spanning the full height is what makes the crossing a certainty rather than a
// possibility. A route changes x by at most one per step, so getting from spawn's
// column to the exit's column means standing in every column between them, and
// therefore in this band. The level does not have to hope the player finds it.
//
// The band is held clear of both ends, so the crossing is a place on the way
// rather than something underfoot at spawn or at the exit.
[[nodiscard]] SetPieceRegion set_piece_region(const GenerationProfile& profile, LevelTier tier,
                                              Coordinates exit_cell) {
    const SetPieceKind kind = set_piece_of(tier);
    const int depth = set_piece_depth_of(kind);

    constexpr int clearance = 2;
    const int toward_exit = exit_cell.x >= profile.spawn.x ? 1 : -1;
    const int near_end = profile.spawn.x + toward_exit * clearance;
    const int far_end = exit_cell.x - toward_exit * clearance;
    const int low = near_end < far_end ? near_end : far_end;
    const int high = near_end < far_end ? far_end : near_end;

    int centre = (profile.spawn.x + exit_cell.x) / 2;
    if (centre < low) centre = low;
    if (centre > high) centre = high;

    int min_x = centre - (depth - 1) / 2;
    int max_x = min_x + depth - 1;
    const int last_interior = static_cast<int>(profile.width) - 2;
    if (min_x < 1) {
        min_x = 1;
        max_x = min_x + depth - 1;
    }
    if (max_x > last_interior) {
        max_x = last_interior;
        min_x = max_x - depth + 1;
    }

    // The band's terrain stops one row short of the cliff at each end so grown
    // terrain can still flow from one side of the level to the other. Walling the
    // full height off tripled how many candidates a seed had to grow before one
    // met its tier's exact terrain totals, because every blob then had to fit
    // entirely on one side. The crossing is unaffected: it is the columns.
    return SetPieceRegion{kind, min_x, max_x, 2, static_cast<int>(profile.height) - 3};
}

// Draw the seeded parts of the authored shape. Two bounded draws place the exit
// inside the tier's fixed exit zone, then one bounded draw per branch spur decides
// whether that optional side route opens. The draw order is a compatibility
// contract: it runs before any terrain pass, so every later blob sees the same
// engine state for a given seed.
[[nodiscard]] RouteLayout draw_route(const GenerationProfile& profile, const LevelTemplate& level,
                                     LevelTier tier, Pcg32& engine) {
    RouteLayout layout;

    const std::uint32_t zone_width = static_cast<std::uint32_t>(level.exit_zone.width());
    const std::uint32_t zone_height = static_cast<std::uint32_t>(level.exit_zone.height());
    layout.exit_cell = Coordinates{
        level.exit_zone.min_x + static_cast<int>(engine.next_bounded(zone_width)),
        level.exit_zone.min_y + static_cast<int>(engine.next_bounded(zone_height)),
    };

    layout.route = route_cells(level, profile.spawn, layout.exit_cell);
    for (const BranchSpur& spur : level.branch_spurs) {
        const bool opens = engine.next_bounded(2u) == 1u;
        if (!opens) {
            continue;
        }
        for (const Coordinates cell : spur_cells(spur)) {
            layout.route.push_back(cell);
        }
    }

    layout.set_piece = set_piece_region(profile, tier, layout.exit_cell);

    layout.reserved.assign(profile.width * profile.height, false);
    for (int y = layout.set_piece.min_y; y <= layout.set_piece.max_y; ++y) {
        for (int x = layout.set_piece.min_x; x <= layout.set_piece.max_x; ++x) {
            layout.reserved[cell_index(profile, static_cast<std::size_t>(x),
                                       static_cast<std::size_t>(y))] = true;
        }
    }
    for (const Coordinates cell : layout.route) {
        if (is_interior(profile, static_cast<std::size_t>(cell.x),
                        static_cast<std::size_t>(cell.y))) {
            layout.reserved[cell_index(profile, static_cast<std::size_t>(cell.x),
                                       static_cast<std::size_t>(cell.y))] = true;
        }
    }
    return layout;
}

[[nodiscard]] bool grow_candidate(const GenerationProfile& profile, const std::vector<bool>& reserved,
                                  Pcg32& engine, std::vector<Terrain>& cells) {
    cells.assign(profile.width * profile.height, Terrain::open);

    // Top and bottom rows (including all four corners) are cliffs.
    for (std::size_t x = 0; x < profile.width; ++x) {
        cells[cell_index(profile, x, 0)] = Terrain::cliff;
        cells[cell_index(profile, x, profile.height - 1)] = Terrain::cliff;
    }
    // The remaining left and right boundary cells are cliffs.
    for (std::size_t y = 1; y + 1 < profile.height; ++y) {
        cells[cell_index(profile, 0, y)] = Terrain::cliff;
        cells[cell_index(profile, profile.width - 1, y)] = Terrain::cliff;
    }

    const auto paint = [&profile](Terrain terrain) {
        return [&profile, terrain](std::vector<Terrain>& buffer, std::size_t x, std::size_t y) {
            buffer[cell_index(profile, x, y)] = terrain;
        };
    };
    const auto paint_barrier = [&profile](std::vector<Terrain>& buffer, std::size_t x,
                                          std::size_t y) {
        buffer[cell_index(profile, x, y)] = barrier_glyph(x, y);
    };

    // Water bodies, then their deterministic deep cores, then mountain cores.
    // Order and sizes are compatibility fixed.
    for (const std::size_t target : profile.water_blobs) {
        if (!grow_blob(profile, reserved, engine, cells, target, paint(Terrain::shallow_water)))
            return false;
    }
    deepen_water_cores(profile, cells);
    for (const std::size_t target : profile.mountain_blobs) {
        if (!grow_blob(profile, reserved, engine, cells, target, paint(Terrain::mountain)))
            return false;
    }

    // Deterministic hill halo immediately after every mountain blob (no RNG).
    add_hill_halo(profile, reserved, cells);

    // Field regions, forest cover, then short barrier ridges.
    for (const std::size_t target : profile.field_blobs) {
        if (!grow_blob(profile, reserved, engine, cells, target, paint(Terrain::fields)))
            return false;
    }
    for (const std::size_t target : profile.forest_blobs) {
        if (!grow_blob(profile, reserved, engine, cells, target, paint(Terrain::forest)))
            return false;
    }
    for (const std::size_t target : profile.barrier_blobs) {
        if (!grow_blob(profile, reserved, engine, cells, target, paint_barrier)) return false;
    }

    return true;
}

// Count all walkable cells reachable from the spawn using cardinal movement. The
// search is iterative (an explicit stack plus a row-major visited buffer) so it
// never recurses regardless of map size.
[[nodiscard]] std::size_t count_reachable_from_spawn(const GenerationProfile& profile,
                                                    const std::vector<Terrain>& cells) {
    const std::size_t total_cells = profile.width * profile.height;
    std::vector<bool> visited(total_cells, false);
    std::vector<std::size_t> stack;
    stack.reserve(total_cells);

    const std::size_t spawn = cell_index(profile, static_cast<std::size_t>(profile.spawn.x),
                                         static_cast<std::size_t>(profile.spawn.y));
    visited[spawn] = true;
    stack.push_back(spawn);
    std::size_t reached = 1u;

    while (!stack.empty()) {
        const std::size_t current = stack.back();
        stack.pop_back();
        const std::size_t x = current % profile.width;
        const std::size_t y = current / profile.width;

        auto consider = [&](std::size_t nx, std::size_t ny) {
            const std::size_t neighbour = cell_index(profile, nx, ny);
            if (!visited[neighbour] && is_walkable(cells[neighbour])) {
                visited[neighbour] = true;
                ++reached;
                stack.push_back(neighbour);
            }
        };
        if (x > 0u) {
            consider(x - 1u, y);
        }
        if (x + 1u < profile.width) {
            consider(x + 1u, y);
        }
        if (y > 0u) {
            consider(x, y - 1u);
        }
        if (y + 1u < profile.height) {
            consider(x, y + 1u);
        }
    }

    return reached;
}

// Walking distance in moves from `origin` to every cell, or -1 where no cardinal
// walk exists. The search is a breadth-first sweep over a row-major queue, which
// keeps it iterative and its result independent of platform and container order.
[[nodiscard]] std::vector<int> walk_distances(const GenerationProfile& profile,
                                              const std::vector<Terrain>& cells,
                                              Coordinates origin) {
    const std::size_t total_cells = profile.width * profile.height;
    std::vector<int> distance(total_cells, -1);
    std::vector<std::size_t> frontier;
    frontier.reserve(total_cells);

    const std::size_t start = cell_index(profile, static_cast<std::size_t>(origin.x),
                                         static_cast<std::size_t>(origin.y));
    distance[start] = 0;
    frontier.push_back(start);

    for (std::size_t head = 0; head < frontier.size(); ++head) {
        const std::size_t current = frontier[head];
        const std::size_t x = current % profile.width;
        const std::size_t y = current / profile.width;

        auto consider = [&](std::size_t nx, std::size_t ny) {
            const std::size_t neighbour = cell_index(profile, nx, ny);
            if (distance[neighbour] < 0 && is_walkable(cells[neighbour])) {
                distance[neighbour] = distance[current] + 1;
                frontier.push_back(neighbour);
            }
        };
        if (x > 0u) {
            consider(x - 1u, y);
        }
        if (x + 1u < profile.width) {
            consider(x + 1u, y);
        }
        if (y > 0u) {
            consider(x, y - 1u);
        }
        if (y + 1u < profile.height) {
            consider(x, y + 1u);
        }
    }

    return distance;
}

// Sizes of the cardinal-connected interior components whose cells satisfy `match`.
// The search is iterative and restricted to interior cells so an interior barrier
// ridge touching the boundary cliff is never merged with it. Row-major visited and
// start ordering keep the result deterministic (only the sizes are inspected).
template <typename Match>
[[nodiscard]] std::vector<std::size_t> interior_component_sizes(const GenerationProfile& profile,
                                                                const std::vector<Terrain>& cells,
                                                                Match match) {
    std::vector<bool> visited(profile.width * profile.height, false);
    std::vector<std::size_t> sizes;
    std::vector<std::size_t> stack;

    for (std::size_t y = 1; y + 1 < profile.height; ++y) {
        for (std::size_t x = 1; x + 1 < profile.width; ++x) {
            const std::size_t start = cell_index(profile, x, y);
            if (visited[start] || !match(cells[start])) {
                continue;
            }
            std::size_t size = 0u;
            visited[start] = true;
            stack.clear();
            stack.push_back(start);
            while (!stack.empty()) {
                const std::size_t current = stack.back();
                stack.pop_back();
                ++size;
                const std::size_t cx = current % profile.width;
                const std::size_t cy = current / profile.width;
                auto consider = [&](std::size_t nx, std::size_t ny) {
                    if (!is_interior(profile, nx, ny)) {
                        return;
                    }
                    const std::size_t neighbour = cell_index(profile, nx, ny);
                    if (!visited[neighbour] && match(cells[neighbour])) {
                        visited[neighbour] = true;
                        stack.push_back(neighbour);
                    }
                };
                if (cx > 0u) {
                    consider(cx - 1u, cy);
                }
                if (cx + 1u < profile.width) {
                    consider(cx + 1u, cy);
                }
                if (cy > 0u) {
                    consider(cx, cy - 1u);
                }
                if (cy + 1u < profile.height) {
                    consider(cx, cy + 1u);
                }
            }
            sizes.push_back(size);
        }
    }
    return sizes;
}

// True when the component count is within [min_components, max_components] and no
// component is smaller than min_size.
[[nodiscard]] bool components_within(const std::vector<std::size_t>& sizes,
                                     const ComponentLimits& limits) {
    if (sizes.size() < limits.min_components || sizes.size() > limits.max_components) {
        return false;
    }
    for (const std::size_t size : sizes) {
        if (size < limits.min_component_size) {
            return false;
        }
    }
    return true;
}

// True when every hill cell is an eight-neighbour of at least one mountain cell.
[[nodiscard]] bool every_hill_touches_mountain(const GenerationProfile& profile,
                                              const std::vector<Terrain>& cells) {
    for (std::size_t y = 1; y + 1 < profile.height; ++y) {
        for (std::size_t x = 1; x + 1 < profile.width; ++x) {
            if (cells[cell_index(profile, x, y)] != Terrain::hill) {
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
                    if (is_interior(profile, nx, ny) &&
                        cells[cell_index(profile, nx, ny)] == Terrain::mountain) {
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

// True when every deep water cell is surrounded on all four sides by water. Deep
// cores are cut out of grown water bodies, so this holds by construction; it is
// re-checked because it is the property that keeps the barrier readable.
[[nodiscard]] bool every_deep_cell_is_enclosed_by_water(const GenerationProfile& profile,
                                                        const std::vector<Terrain>& cells) {
    for (std::size_t y = 1; y + 1 < profile.height; ++y) {
        for (std::size_t x = 1; x + 1 < profile.width; ++x) {
            if (cells[cell_index(profile, x, y)] != Terrain::deep_water) {
                continue;
            }
            if (!is_water(cells[cell_index(profile, x, y - 1u)]) ||
                !is_water(cells[cell_index(profile, x, y + 1u)]) ||
                !is_water(cells[cell_index(profile, x - 1u, y)]) ||
                !is_water(cells[cell_index(profile, x + 1u, y)])) {
                return false;
            }
        }
    }
    return true;
}

// Enforce every acceptance rule directly on the candidate buffer (REQ-016/017).
// The validator inspects the buffer itself and re-derives connectivity and every
// component invariant iteratively, so an invalid candidate is never returned.
[[nodiscard]] bool is_valid_candidate(const GenerationProfile& profile, const RouteLayout& layout,
                                      const std::vector<Terrain>& cells) {
    if (cells.size() != profile.width * profile.height) {
        return false;
    }

    // The protected 3x3 spawn square must be entirely open.
    const std::size_t sx = static_cast<std::size_t>(profile.spawn.x);
    const std::size_t sy = static_cast<std::size_t>(profile.spawn.y);
    for (std::size_t y = sy - 1u; y <= sy + 1u; ++y) {
        for (std::size_t x = sx - 1u; x <= sx + 1u; ++x) {
            if (cells[cell_index(profile, x, y)] != Terrain::open) {
                return false;
            }
        }
    }

    // The carved route must have survived every terrain pass as an open corridor,
    // which is what makes the level solvable by construction rather than by
    // rejection sampling.
    for (const Coordinates cell : layout.route) {
        if (cells[cell_index(profile, static_cast<std::size_t>(cell.x),
                             static_cast<std::size_t>(cell.y))] != Terrain::open) {
            return false;
        }
    }

    // The full cliff boundary must be intact.
    for (std::size_t x = 0; x < profile.width; ++x) {
        if (cells[cell_index(profile, x, 0)] != Terrain::cliff ||
            cells[cell_index(profile, x, profile.height - 1)] != Terrain::cliff) {
            return false;
        }
    }
    for (std::size_t y = 1; y + 1 < profile.height; ++y) {
        if (cells[cell_index(profile, 0, y)] != Terrain::cliff ||
            cells[cell_index(profile, profile.width - 1, y)] != Terrain::cliff) {
            return false;
        }
    }

    // Exact interior terrain totals and a hill floor, counted in one scan.
    std::size_t water = 0u;
    std::size_t deep_water = 0u;
    std::size_t fields = 0u;
    std::size_t forest = 0u;
    std::size_t mountain = 0u;
    std::size_t hill = 0u;
    std::size_t barrier = 0u;
    for (std::size_t y = 1; y + 1 < profile.height; ++y) {
        for (std::size_t x = 1; x + 1 < profile.width; ++x) {
            switch (cells[cell_index(profile, x, y)]) {
                case Terrain::shallow_water: ++water; break;
                case Terrain::deep_water:    ++water; ++deep_water; break;
                case Terrain::fields:        ++fields; break;
                case Terrain::forest:        ++forest; break;
                case Terrain::mountain:      ++mountain; break;
                case Terrain::hill:          ++hill; break;
                case Terrain::cliff:         ++barrier; break;
                case Terrain::open:          break;
            }
        }
    }
    if (water != profile.water_cells || fields != profile.field_cells ||
        forest != profile.forest_cells || mountain != profile.mountain_cells ||
        barrier != profile.barrier_cells) {
        return false;
    }
    if (hill < profile.min_hill_cells || deep_water < profile.min_deep_water_cells) {
        return false;
    }

    // Every hill must touch a mountain, and every deep cell must sit inside water
    // (both guaranteed by construction; re-checked).
    if (!every_hill_touches_mountain(profile, cells)) {
        return false;
    }
    if (!every_deep_cell_is_enclosed_by_water(profile, cells)) {
        return false;
    }

    // Cardinal component limits and minimum sizes for each clustered feature. Water
    // is matched at either depth so deepening a body's middle does not read as
    // having split it into a ring.
    if (!components_within(interior_component_sizes(profile, cells, is_water), profile.water)) {
        return false;
    }
    if (!components_within(
            interior_component_sizes(profile, cells,
                                     [](Terrain t) { return t == Terrain::fields; }),
            profile.fields)) {
        return false;
    }
    if (!components_within(
            interior_component_sizes(profile, cells,
                                     [](Terrain t) { return t == Terrain::forest; }),
            profile.forest)) {
        return false;
    }
    if (!components_within(
            interior_component_sizes(profile, cells,
                                     [](Terrain t) { return t == Terrain::mountain; }),
            profile.mountain)) {
        return false;
    }
    if (!components_within(
            interior_component_sizes(profile, cells,
                                     [](Terrain t) { return t == Terrain::cliff; }),
            profile.barrier)) {
        return false;
    }

    // Every walkable cell must be reachable from the spawn. Because the boundary
    // is solid cliff, the total walkable count equals the walkable interior count,
    // but count the whole grid so the invariant matches the specification exactly.
    //
    // This single equality is the connectivity proof the impassable terrain
    // requires: it is strictly stronger than checking the entry, the landmark,
    // every discovery, and the exit individually, because all of those are placed
    // on walkable cells. A candidate whose cliffs or deep water sever the map is
    // rejected here and re-rolled, never patched.
    std::size_t total_walkable = 0u;
    for (const Terrain terrain : cells) {
        if (is_walkable(terrain)) {
            ++total_walkable;
        }
    }
    return count_reachable_from_spawn(profile, cells) == total_walkable;
}

}  // namespace

std::uint64_t hash_seed_text(std::string_view text) noexcept {
    // 64-bit FNV-1a over the exact input bytes: XOR each unsigned byte value into
    // the hash, then multiply by the prime. The multiply wraps modulo 2^64 by
    // defined unsigned overflow.
    std::uint64_t hash = kFnvOffsetBasis;
    for (const char character : text) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(character));
        hash *= kFnvPrime;
    }
    return hash;
}

// Place one feature per content slot on the cell whose real detour comes closest
// to the slot's band under this level's seeded profile.
//
// The detour of a cell is what visiting it adds to the journey:
// `d(spawn, cell) + d(cell, exit) - d(spawn, exit)`. That is the only honest price
// of a piece of content on open ground, so it is what placement aims at, rather
// than picking uniformly inside the zone and letting the cost fall where it may.
// Where several cells tie at the same distance from the target, the seed breaks
// the tie, which keeps identical prices from collapsing onto one fixed cell.
//
// A vantage point breaks that tie differently: among the cells that cost the
// player exactly the same, it takes the highest ground, because what a vantage
// point is worth is decided by the terrain under it (see vantage.h). The
// preference is free by construction -- it only ever chooses between cells the
// price rule had already declared equal -- so the ordering the bands establish
// can never be inverted by it.
//
// Returns nothing when a slot has no candidate, which fails the whole candidate
// and retries with fresh terrain.
[[nodiscard]] std::optional<std::vector<LevelFeature>> place_content(
    const GenerationProfile& profile, const LevelTemplate& level, const RouteLayout& layout,
    DetourProfile detour_profile, Pcg32& engine, const std::vector<Terrain>& cells) {
    struct Candidate {
        Coordinates position{};
        std::int64_t miss = 0;
        std::size_t rank = 0;
    };

    std::vector<LevelFeature> features;
    features.reserve(level.content_slots.size());

    const std::vector<int> from_spawn = walk_distances(profile, cells, profile.spawn);
    const std::vector<int> from_exit = walk_distances(profile, cells, layout.exit_cell);
    const std::size_t exit_index = cell_index(profile,
                                              static_cast<std::size_t>(layout.exit_cell.x),
                                              static_cast<std::size_t>(layout.exit_cell.y));
    const int direct = from_spawn[exit_index];
    if (direct < 0) {
        return std::nullopt;
    }

    for (const ContentSlot& slot : level.content_slots) {
        const std::int64_t target = static_cast<std::int64_t>(
            target_detour_moves(slot.band, detour_profile, static_cast<std::uint64_t>(direct)));
        const bool prefers_high_ground = slot.kind == LevelFeatureKind::vantage_point;

        std::vector<Candidate> eligible;
        for (int y = slot.zone.min_y; y <= slot.zone.max_y; ++y) {
            for (int x = slot.zone.min_x; x <= slot.zone.max_x; ++x) {
                const std::size_t ux = static_cast<std::size_t>(x);
                const std::size_t uy = static_cast<std::size_t>(y);
                if (!is_interior(profile, ux, uy)) {
                    continue;
                }
                const std::size_t index = cell_index(profile, ux, uy);
                if (!is_walkable(cells[index])) {
                    continue;
                }
                const Coordinates position{x, y};
                if (position == profile.spawn || position == layout.exit_cell) {
                    continue;
                }
                // The protected entry square is the player's first look at the
                // map; content sitting in it would be found before the level has
                // asked anything of them.
                if (level.entry_zone.contains(position)) {
                    continue;
                }
                if (from_spawn[index] < 0 || from_exit[index] < 0) {
                    continue;
                }
                bool taken = false;
                for (const LevelFeature& placed : features) {
                    if (placed.position == position) {
                        taken = true;
                    }
                }
                if (taken) {
                    continue;
                }

                const std::int64_t detour =
                    static_cast<std::int64_t>(from_spawn[index]) +
                    static_cast<std::int64_t>(from_exit[index]) - static_cast<std::int64_t>(direct);
                eligible.push_back(
                    Candidate{position, detour > target ? (detour - target) : (target - detour),
                              vantage_rank_of(vantage_kind_of(cells[index]))});
            }
        }
        if (eligible.empty()) {
            return std::nullopt;
        }

        std::int64_t best_miss = eligible.front().miss;
        for (const Candidate& candidate : eligible) {
            best_miss = std::min(best_miss, candidate.miss);
        }

        std::size_t best_rank = 0;
        if (prefers_high_ground) {
            for (const Candidate& candidate : eligible) {
                if (candidate.miss == best_miss) {
                    best_rank = std::max(best_rank, candidate.rank);
                }
            }
        }

        std::vector<Coordinates> candidates;
        for (const Candidate& candidate : eligible) {
            if (candidate.miss == best_miss &&
                (!prefers_high_ground || candidate.rank == best_rank)) {
                candidates.push_back(candidate.position);
            }
        }
        if (candidates.empty()) {
            return std::nullopt;
        }
        const std::uint32_t pick =
            engine.next_bounded(static_cast<std::uint32_t>(candidates.size()));
        features.push_back(LevelFeature{candidates[pick], slot.kind});
    }

    return features;
}

// Write the set-piece's terrain into the band that was reserved for it.
//
// It runs after the candidate is accepted, so acceptance still measures the
// terrain the generator grew rather than an authored overlay laid on top, and
// before content placement, because the band replaces open ground and so must be
// on the map before any slot is priced against a real walking distance.
//
// Route cells inside the band keep their open ground. That is the crossing: the
// ford through the river, the gap in the pass. It also preserves the acceptance
// guarantee that the carved route survives generation as an open corridor.
void paint_set_piece(const GenerationProfile& profile, const RouteLayout& layout,
                     std::vector<Terrain>& cells) {
    const SetPieceRegion& region = layout.set_piece;
    const Terrain ground = set_piece_terrain_of(region.kind);
    for (int y = region.min_y; y <= region.max_y; ++y) {
        for (int x = region.min_x; x <= region.max_x; ++x) {
            const Coordinates cell{x, y};
            if (cell == profile.spawn || cell == layout.exit_cell) {
                continue;
            }
            bool on_route = false;
            for (const Coordinates step : layout.route) {
                if (step == cell) {
                    on_route = true;
                    break;
                }
            }
            if (on_route) {
                continue;
            }
            cells[cell_index(profile, static_cast<std::size_t>(x), static_cast<std::size_t>(y))] =
                ground;
        }
    }
}

WorldGenerationResult generate_level(LevelTier tier, std::uint64_t numeric_seed) {
    const GenerationProfile profile = profile_of(tier);

    // A single engine grows every candidate sequentially; retries continue from
    // its current state rather than reseeding, so attempt numbers are stable.
    Pcg32 engine(numeric_seed, profile.stream);

    std::vector<Terrain> cells;
    for (std::uint32_t attempt = 0; attempt < level_candidate_limit; ++attempt) {
        // The exit corner is the first draw of every candidate, so a rejected
        // candidate can land the exit somewhere else entirely.
        const ExitCorner corner =
            static_cast<ExitCorner>(engine.next_bounded(exit_corner_count));
        // How widely this candidate spreads its content is drawn next, before any
        // terrain, so a rejected candidate can range differently as well as exit
        // elsewhere.
        const DetourProfile detour_profile =
            static_cast<DetourProfile>(engine.next_bounded(detour_profile_count));
        const LevelTemplate level = template_of(tier, corner);
        const RouteLayout layout = draw_route(profile, level, tier, engine);
        if (!grow_candidate(profile, layout.reserved, engine, cells) ||
            !is_valid_candidate(profile, layout, cells)) {
            continue;
        }
        paint_set_piece(profile, layout, cells);
        std::optional<std::vector<LevelFeature>> features =
            place_content(profile, level, layout, detour_profile, engine, cells);
        if (!features) {
            continue;
        }

        LevelLayout map_layout;
        map_layout.exit = layout.exit_cell;
        map_layout.features = std::move(*features);
        map_layout.set_piece = layout.set_piece;
        Map map(profile.width, profile.height, cells, profile.spawn, std::move(map_layout));
        return GeneratedWorld{std::move(map), numeric_seed, attempt, tier, layout.exit_cell,
                              corner,         detour_profile};
    }

    return WorldGenerationError{WorldGenerationErrorCode::candidate_limit_exhausted, numeric_seed};
}

WorldGenerationResult generate_tiny_world(std::uint64_t numeric_seed) {
    return generate_level(tiny_world_tier, numeric_seed);
}

std::string_view to_string(WorldGenerationErrorCode code) noexcept {
    switch (code) {
        case WorldGenerationErrorCode::candidate_limit_exhausted:
            return "candidate_limit_exhausted";
    }
    return "unknown";
}
