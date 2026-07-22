#include "world_generation.h"

#include <array>
#include <cstddef>
#include <utility>
#include <vector>

#include "pcg32.h"
#include "terrain.h"

// IMPORTANT: every constant and pass below is part of Tiny World's compatibility
// contract. Any change to the FNV constants, the stream selector, the eligible-cell
// traversal order, the feature target sizes, the pass order, the cardinal
// direction order, the growth proposal limit, the barrier-orientation rule, the
// hill-halo construction, or the acceptance rules changes every generated world
// for every seed. Such a change must later be gated behind an explicit recipe
// version rather than silently altering existing output.

namespace {

// 64-bit FNV-1a parameters (see draft-eastlake-fnv). These are the released
// algorithm constants; the offset basis also equals hash_seed_text("").
constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

// The Pcg32 stream selector for Tiny World. The bytes spell "TINY", giving this
// recipe its own independent stream so future recipes can share a seed without
// colliding. It is a released compatibility constant and must not change.
constexpr std::uint64_t kTinyWorldStream = 0x54494E59ULL;

// The inclusive bounds of the protected 3x3 open square around the spawn. These
// cells are never eligible for feature placement, so the spawn and its immediate
// neighbourhood always stay open.
constexpr std::size_t kSpawnProtectMinX = 13u;
constexpr std::size_t kSpawnProtectMaxX = 15u;
constexpr std::size_t kSpawnProtectMinY = 6u;
constexpr std::size_t kSpawnProtectMaxY = 8u;

// Exact interior terrain totals every accepted candidate must contain. They are
// the sums of the per-pass target sizes below: water 18+14, fields 22+18+14,
// mountains 8+6, barriers 6+5.
constexpr std::size_t kWaterCells = 32u;
constexpr std::size_t kFieldCells = 54u;
constexpr std::size_t kMountainCells = 14u;
constexpr std::size_t kBarrierCells = 11u;

// The hill halo is deterministic but its size varies with mountain shape, so it
// is bounded below rather than fixed exactly.
constexpr std::size_t kMinHillCells = 20u;

// Cardinal component limits and minimum sizes for each clustered feature.
constexpr std::size_t kWaterMinComponents = 1u;
constexpr std::size_t kWaterMaxComponents = 2u;
constexpr std::size_t kWaterMinComponentSize = 14u;
constexpr std::size_t kFieldMinComponents = 1u;
constexpr std::size_t kFieldMaxComponents = 3u;
constexpr std::size_t kFieldMinComponentSize = 14u;
constexpr std::size_t kMountainMinComponents = 1u;
constexpr std::size_t kMountainMaxComponents = 2u;
constexpr std::size_t kMountainMinComponentSize = 6u;
constexpr std::size_t kBarrierMinComponents = 1u;
constexpr std::size_t kBarrierMaxComponents = 2u;
constexpr std::size_t kBarrierMinComponentSize = 5u;

// The bounded-growth proposal budget: a blob that has not reached its target size
// after target_size * this many proposals fails the whole candidate.
constexpr std::size_t kProposalBudgetFactor = 64u;

// Row-major flat index into a tiny_world_width * tiny_world_height buffer.
[[nodiscard]] constexpr std::size_t cell_index(std::size_t x, std::size_t y) noexcept {
    return y * tiny_world_width + x;
}

// True when (x, y) is an interior cell, i.e. not on the solid wall boundary.
[[nodiscard]] constexpr bool is_interior(std::size_t x, std::size_t y) noexcept {
    return x >= 1u && x + 1u < tiny_world_width && y >= 1u && y + 1u < tiny_world_height;
}

// True when (x, y) is inside the protected 3x3 spawn square.
[[nodiscard]] constexpr bool is_protected_spawn(std::size_t x, std::size_t y) noexcept {
    return x >= kSpawnProtectMinX && x <= kSpawnProtectMaxX &&
           y >= kSpawnProtectMinY && y <= kSpawnProtectMaxY;
}

// An eligible cell for feature placement: interior, outside the protected spawn
// square, and currently open. Painted features and the boundary are never open,
// so a cell is annexed at most once and no pass overwrites an earlier feature.
[[nodiscard]] bool is_eligible(const std::vector<Terrain>& cells, std::size_t x, std::size_t y) {
    return is_interior(x, y) && !is_protected_spawn(x, y) &&
           cells[cell_index(x, y)] == Terrain::open;
}

// Paint a barrier glyph. Both wall variants are equally impassable; the parity
// rule only records a consistent serialized glyph and consumes no RNG.
[[nodiscard]] constexpr Terrain barrier_glyph(std::size_t x, std::size_t y) noexcept {
    return ((x + y) % 2u == 0u) ? Terrain::wall_horizontal : Terrain::wall_vertical;
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
[[nodiscard]] bool grow_blob(Pcg32& engine, std::vector<Terrain>& cells,
                             std::size_t target_size, Paint paint) {
    std::vector<std::size_t> eligible;
    eligible.reserve(tiny_world_width * tiny_world_height);
    for (std::size_t y = 1; y + 1 < tiny_world_height; ++y) {
        for (std::size_t x = 1; x + 1 < tiny_world_width; ++x) {
            if (is_eligible(cells, x, y)) {
                eligible.push_back(cell_index(x, y));
            }
        }
    }
    if (eligible.empty()) {
        return false;
    }

    const std::uint32_t start_pick = engine.next_bounded(static_cast<std::uint32_t>(eligible.size()));
    const std::size_t start = eligible[start_pick];
    paint(cells, start % tiny_world_width, start / tiny_world_width);

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

        const std::size_t cx = blob[blob_pick] % tiny_world_width;
        const std::size_t cy = blob[blob_pick] / tiny_world_width;
        std::size_t nx = cx;
        std::size_t ny = cy;
        switch (direction) {
            case 0u: ny = cy - 1u; break;  // up    (cy >= 1 for interior cells)
            case 1u: nx = cx + 1u; break;  // right
            case 2u: ny = cy + 1u; break;  // down
            default: nx = cx - 1u; break;  // left  (direction == 3; cx >= 1)
        }

        if (is_eligible(cells, nx, ny)) {
            paint(cells, nx, ny);
            blob.push_back(cell_index(nx, ny));
        }
    }
    return true;
}

// Stamp the deterministic one-cell hill halo around the mountains (REQ-014). No
// RNG is consumed. Every eligible eight-neighbour of any mountain is marked in a
// mask, then the interior is scanned in row-major order and each marked cell that
// is still open becomes a hill. Because later passes paint only open cells, the
// halo is never overwritten and every hill stays adjacent to a mountain.
void add_hill_halo(std::vector<Terrain>& cells) {
    std::vector<bool> hill_mask(tiny_world_width * tiny_world_height, false);

    for (std::size_t y = 1; y + 1 < tiny_world_height; ++y) {
        for (std::size_t x = 1; x + 1 < tiny_world_width; ++x) {
            if (cells[cell_index(x, y)] != Terrain::mountain) {
                continue;
            }
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) {
                        continue;
                    }
                    const std::size_t nx = static_cast<std::size_t>(static_cast<int>(x) + dx);
                    const std::size_t ny = static_cast<std::size_t>(static_cast<int>(y) + dy);
                    if (is_eligible(cells, nx, ny)) {
                        hill_mask[cell_index(nx, ny)] = true;
                    }
                }
            }
        }
    }

    for (std::size_t y = 1; y + 1 < tiny_world_height; ++y) {
        for (std::size_t x = 1; x + 1 < tiny_world_width; ++x) {
            const std::size_t idx = cell_index(x, y);
            if (hill_mask[idx] && cells[idx] == Terrain::open) {
                cells[idx] = Terrain::hill;
            }
        }
    }
}

// Grow one complete candidate terrain buffer from the engine. The passes run in a
// fixed order so RNG consumption is deterministic: build the wall boundary, grow
// the water bodies, the mountain cores, stamp the hill halo, grow the field
// regions, then the barrier ridges. Returns false if any growth pass exhausts its
// proposal budget, leaving the buffer for the caller to discard.
[[nodiscard]] bool grow_candidate(Pcg32& engine, std::vector<Terrain>& cells) {
    cells.assign(tiny_world_width * tiny_world_height, Terrain::open);

    // Top and bottom rows (including all four corners) are horizontal walls.
    for (std::size_t x = 0; x < tiny_world_width; ++x) {
        cells[cell_index(x, 0)] = Terrain::wall_horizontal;
        cells[cell_index(x, tiny_world_height - 1)] = Terrain::wall_horizontal;
    }
    // The remaining left and right boundary cells are vertical walls.
    for (std::size_t y = 1; y + 1 < tiny_world_height; ++y) {
        cells[cell_index(0, y)] = Terrain::wall_vertical;
        cells[cell_index(tiny_world_width - 1, y)] = Terrain::wall_vertical;
    }

    const auto paint_water = [](std::vector<Terrain>& buffer, std::size_t x, std::size_t y) {
        buffer[cell_index(x, y)] = Terrain::water;
    };
    const auto paint_mountain = [](std::vector<Terrain>& buffer, std::size_t x, std::size_t y) {
        buffer[cell_index(x, y)] = Terrain::mountain;
    };
    const auto paint_fields = [](std::vector<Terrain>& buffer, std::size_t x, std::size_t y) {
        buffer[cell_index(x, y)] = Terrain::fields;
    };
    const auto paint_barrier = [](std::vector<Terrain>& buffer, std::size_t x, std::size_t y) {
        buffer[cell_index(x, y)] = barrier_glyph(x, y);
    };

    // Water bodies, then mountain cores. Order and sizes are compatibility fixed.
    if (!grow_blob(engine, cells, 18u, paint_water)) return false;
    if (!grow_blob(engine, cells, 14u, paint_water)) return false;
    if (!grow_blob(engine, cells, 8u, paint_mountain)) return false;
    if (!grow_blob(engine, cells, 6u, paint_mountain)) return false;

    // Deterministic hill halo immediately after both mountain blobs (no RNG).
    add_hill_halo(cells);

    // Field regions, then short barrier ridges.
    if (!grow_blob(engine, cells, 22u, paint_fields)) return false;
    if (!grow_blob(engine, cells, 18u, paint_fields)) return false;
    if (!grow_blob(engine, cells, 14u, paint_fields)) return false;
    if (!grow_blob(engine, cells, 6u, paint_barrier)) return false;
    if (!grow_blob(engine, cells, 5u, paint_barrier)) return false;

    return true;
}

// Count all walkable cells reachable from the spawn using cardinal movement. The
// search is iterative (an explicit stack plus a row-major visited buffer) so it
// never recurses regardless of map size.
[[nodiscard]] std::size_t count_reachable_from_spawn(const std::vector<Terrain>& cells) {
    const std::size_t total_cells = tiny_world_width * tiny_world_height;
    std::vector<bool> visited(total_cells, false);
    std::vector<std::size_t> stack;
    stack.reserve(total_cells);

    const std::size_t spawn = cell_index(static_cast<std::size_t>(tiny_world_spawn.x),
                                         static_cast<std::size_t>(tiny_world_spawn.y));
    visited[spawn] = true;
    stack.push_back(spawn);
    std::size_t reached = 1u;

    while (!stack.empty()) {
        const std::size_t current = stack.back();
        stack.pop_back();
        const std::size_t x = current % tiny_world_width;
        const std::size_t y = current / tiny_world_width;

        auto consider = [&](std::size_t nx, std::size_t ny) {
            const std::size_t neighbour = cell_index(nx, ny);
            if (!visited[neighbour] && is_walkable(cells[neighbour])) {
                visited[neighbour] = true;
                ++reached;
                stack.push_back(neighbour);
            }
        };
        if (x > 0u) {
            consider(x - 1u, y);
        }
        if (x + 1u < tiny_world_width) {
            consider(x + 1u, y);
        }
        if (y > 0u) {
            consider(x, y - 1u);
        }
        if (y + 1u < tiny_world_height) {
            consider(x, y + 1u);
        }
    }

    return reached;
}

// Sizes of the cardinal-connected interior components whose cells satisfy `match`.
// The search is iterative and restricted to interior cells so an interior barrier
// ridge touching the boundary wall is never merged with it. Row-major visited and
// start ordering keep the result deterministic (only the sizes are inspected).
template <typename Match>
[[nodiscard]] std::vector<std::size_t> interior_component_sizes(const std::vector<Terrain>& cells,
                                                                Match match) {
    std::vector<bool> visited(tiny_world_width * tiny_world_height, false);
    std::vector<std::size_t> sizes;
    std::vector<std::size_t> stack;

    for (std::size_t y = 1; y + 1 < tiny_world_height; ++y) {
        for (std::size_t x = 1; x + 1 < tiny_world_width; ++x) {
            const std::size_t start = cell_index(x, y);
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
                const std::size_t cx = current % tiny_world_width;
                const std::size_t cy = current / tiny_world_width;
                auto consider = [&](std::size_t nx, std::size_t ny) {
                    if (!is_interior(nx, ny)) {
                        return;
                    }
                    const std::size_t neighbour = cell_index(nx, ny);
                    if (!visited[neighbour] && match(cells[neighbour])) {
                        visited[neighbour] = true;
                        stack.push_back(neighbour);
                    }
                };
                if (cx > 0u) {
                    consider(cx - 1u, cy);
                }
                if (cx + 1u < tiny_world_width) {
                    consider(cx + 1u, cy);
                }
                if (cy > 0u) {
                    consider(cx, cy - 1u);
                }
                if (cy + 1u < tiny_world_height) {
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
                                     std::size_t min_components, std::size_t max_components,
                                     std::size_t min_size) {
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

// True when every hill cell is an eight-neighbour of at least one mountain cell.
[[nodiscard]] bool every_hill_touches_mountain(const std::vector<Terrain>& cells) {
    for (std::size_t y = 1; y + 1 < tiny_world_height; ++y) {
        for (std::size_t x = 1; x + 1 < tiny_world_width; ++x) {
            if (cells[cell_index(x, y)] != Terrain::hill) {
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
                    if (is_interior(nx, ny) && cells[cell_index(nx, ny)] == Terrain::mountain) {
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

// Enforce every acceptance rule directly on the candidate buffer (REQ-016/017).
// The validator inspects the buffer itself and re-derives connectivity and every
// component invariant iteratively, so an invalid candidate is never returned.
[[nodiscard]] bool is_valid_candidate(const std::vector<Terrain>& cells) {
    if (cells.size() != tiny_world_width * tiny_world_height) {
        return false;
    }

    // The protected 3x3 spawn square must be entirely open.
    for (std::size_t y = kSpawnProtectMinY; y <= kSpawnProtectMaxY; ++y) {
        for (std::size_t x = kSpawnProtectMinX; x <= kSpawnProtectMaxX; ++x) {
            if (cells[cell_index(x, y)] != Terrain::open) {
                return false;
            }
        }
    }

    // The full wall boundary must be intact.
    for (std::size_t x = 0; x < tiny_world_width; ++x) {
        if (cells[cell_index(x, 0)] != Terrain::wall_horizontal ||
            cells[cell_index(x, tiny_world_height - 1)] != Terrain::wall_horizontal) {
            return false;
        }
    }
    for (std::size_t y = 1; y + 1 < tiny_world_height; ++y) {
        if (cells[cell_index(0, y)] != Terrain::wall_vertical ||
            cells[cell_index(tiny_world_width - 1, y)] != Terrain::wall_vertical) {
            return false;
        }
    }

    // Exact interior terrain totals and a hill floor, counted in one scan.
    std::size_t water = 0u;
    std::size_t fields = 0u;
    std::size_t mountain = 0u;
    std::size_t hill = 0u;
    std::size_t barrier = 0u;
    for (std::size_t y = 1; y + 1 < tiny_world_height; ++y) {
        for (std::size_t x = 1; x + 1 < tiny_world_width; ++x) {
            switch (cells[cell_index(x, y)]) {
                case Terrain::water:    ++water; break;
                case Terrain::fields:   ++fields; break;
                case Terrain::mountain: ++mountain; break;
                case Terrain::hill:     ++hill; break;
                case Terrain::wall_horizontal:
                case Terrain::wall_vertical: ++barrier; break;
                case Terrain::open:     break;
            }
        }
    }
    if (water != kWaterCells || fields != kFieldCells || mountain != kMountainCells ||
        barrier != kBarrierCells) {
        return false;
    }
    if (hill < kMinHillCells) {
        return false;
    }

    // Every hill must touch a mountain (guaranteed by construction; re-checked).
    if (!every_hill_touches_mountain(cells)) {
        return false;
    }

    // Cardinal component limits and minimum sizes for each clustered feature.
    if (!components_within(
            interior_component_sizes(cells, [](Terrain t) { return t == Terrain::water; }),
            kWaterMinComponents, kWaterMaxComponents, kWaterMinComponentSize)) {
        return false;
    }
    if (!components_within(
            interior_component_sizes(cells, [](Terrain t) { return t == Terrain::fields; }),
            kFieldMinComponents, kFieldMaxComponents, kFieldMinComponentSize)) {
        return false;
    }
    if (!components_within(
            interior_component_sizes(cells, [](Terrain t) { return t == Terrain::mountain; }),
            kMountainMinComponents, kMountainMaxComponents, kMountainMinComponentSize)) {
        return false;
    }
    if (!components_within(
            interior_component_sizes(cells,
                                     [](Terrain t) {
                                         return t == Terrain::wall_horizontal ||
                                                t == Terrain::wall_vertical;
                                     }),
            kBarrierMinComponents, kBarrierMaxComponents, kBarrierMinComponentSize)) {
        return false;
    }

    // Every walkable cell must be reachable from the spawn. Because the boundary
    // is solid wall, the total walkable count equals the walkable interior count,
    // but count the whole grid so the invariant matches the specification exactly.
    std::size_t total_walkable = 0u;
    for (const Terrain terrain : cells) {
        if (is_walkable(terrain)) {
            ++total_walkable;
        }
    }
    return count_reachable_from_spawn(cells) == total_walkable;
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

WorldGenerationResult generate_tiny_world(std::uint64_t numeric_seed) {
    // A single engine grows every candidate sequentially; retries continue from
    // its current state rather than reseeding, so attempt numbers are stable.
    Pcg32 engine(numeric_seed, kTinyWorldStream);

    std::vector<Terrain> cells;
    for (std::uint32_t attempt = 0; attempt < tiny_world_candidate_limit; ++attempt) {
        if (grow_candidate(engine, cells) && is_valid_candidate(cells)) {
            Map map(tiny_world_width, tiny_world_height, cells, tiny_world_spawn);
            return GeneratedWorld{std::move(map), numeric_seed, attempt};
        }
    }

    return WorldGenerationError{WorldGenerationErrorCode::candidate_limit_exhausted, numeric_seed};
}

std::string_view to_string(WorldGenerationErrorCode code) noexcept {
    switch (code) {
        case WorldGenerationErrorCode::candidate_limit_exhausted:
            return "candidate_limit_exhausted";
    }
    return "unknown";
}
