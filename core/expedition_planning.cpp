#include "expedition_planning.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <queue>
#include <vector>

#include "expedition_time.h"
#include "game_event.h"
#include "terrain.h"

namespace {

// The four cardinal neighbour offsets, in a fixed order. The returned baseline is
// a scalar minimum, so neighbour order can never change the result; the explicit
// packed-index tie-break in the queue keeps diagnostics reproducible (GUD-004).
constexpr std::array<Coordinates, 4> kCardinalOffsets{
    Coordinates{0, -1}, Coordinates{0, 1}, Coordinates{-1, 0}, Coordinates{1, 0}};

// A lexicographic planning cost packed into one unsigned scalar: the high 32 bits
// hold overnight transitions and the low 32 bits hold provisions consumed, so a
// plain unsigned comparison orders by (overnights, provisions). Both components
// stay far below 2^32 for supported maps, so the pack is exact.
[[nodiscard]] std::uint64_t pack_cost(std::uint32_t overnights,
                                      std::uint32_t provisions) noexcept {
    return (static_cast<std::uint64_t>(overnights) << 32) | static_cast<std::uint64_t>(provisions);
}

[[nodiscard]] std::uint32_t cost_overnights(std::uint64_t cost) noexcept {
    return static_cast<std::uint32_t>(cost >> 32);
}

[[nodiscard]] std::uint32_t cost_provisions(std::uint64_t cost) noexcept {
    return static_cast<std::uint32_t>(cost & 0xFFFFFFFFULL);
}

}  // namespace

ExpeditionPlanBaseline compute_expedition_plan_baseline(const Map& map, Coordinates spawn,
                                                       Coordinates beacon,
                                                       std::uint32_t max_stamina) {
    // A single-cell objective (beacon at spawn) is already complete on day 1 and
    // needs no provisions (REQ-017).
    if (beacon == spawn) {
        return ExpeditionPlanBaseline{1u, 0u};
    }

    const int width = static_cast<int>(map.width());
    const int stamina_levels = static_cast<int>(max_stamina) + 1;      // 0..max
    const int daylight_levels = static_cast<int>(daylight_hours_per_day) + 1;  // 0..per_day

    const auto state_index = [width, stamina_levels](
                                 Coordinates position, int stamina, int used_hours, int phase) {
        const std::size_t cell = static_cast<std::size_t>(position.y) *
                                     static_cast<std::size_t>(width) +
                                 static_cast<std::size_t>(position.x);
        std::size_t index = cell;
        index = index * static_cast<std::size_t>(stamina_levels) + static_cast<std::size_t>(stamina);
        index =
            index * static_cast<std::size_t>(daylight_levels) + static_cast<std::size_t>(used_hours);
        index = index * 2u + static_cast<std::size_t>(phase);
        return index;
    };

    const std::size_t state_count = static_cast<std::size_t>(width) *
                                    static_cast<std::size_t>(map.height()) *
                                    static_cast<std::size_t>(stamina_levels) *
                                    static_cast<std::size_t>(daylight_levels) * 2u;

    constexpr std::uint64_t unreachable = static_cast<std::uint64_t>(-1);
    std::vector<std::uint64_t> dist(state_count, unreachable);

    // Min-heap over (cost, packed index): lexicographic planning cost first, then
    // packed state index so equal-cost ordering is reproducible (GUD-004).
    using QueueEntry = std::pair<std::uint64_t, std::size_t>;
    std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<QueueEntry>> queue;

    const std::size_t start =
        state_index(spawn, static_cast<int>(max_stamina), 0, 0);
    dist[start] = pack_cost(0u, 0u);
    queue.emplace(dist[start], start);

    const int per_day = static_cast<int>(daylight_hours_per_day);

    while (!queue.empty()) {
        const std::uint64_t cost = queue.top().first;
        const std::size_t s = queue.top().second;
        queue.pop();
        if (cost != dist[s]) {
            continue;  // Superseded by a cheaper relaxation.
        }

        // Decode the packed state.
        const int phase = static_cast<int>(s % 2u);
        std::size_t rest = s / 2u;
        const int used_hours = static_cast<int>(rest % static_cast<std::size_t>(daylight_levels));
        rest /= static_cast<std::size_t>(daylight_levels);
        const int stamina = static_cast<int>(rest % static_cast<std::size_t>(stamina_levels));
        rest /= static_cast<std::size_t>(stamina_levels);
        const Coordinates here{static_cast<int>(rest % static_cast<std::size_t>(width)),
                               static_cast<int>(rest / static_cast<std::size_t>(width))};

        // A returning-phase state at spawn is the completed round trip. Dijkstra
        // pops states in nondecreasing cost, so the first goal popped is minimal.
        if (phase == 1 && here == spawn) {
            return ExpeditionPlanBaseline{cost_overnights(cost) + 1u, cost_provisions(cost)};
        }

        const std::uint32_t overnights = cost_overnights(cost);
        const std::uint32_t provisions = cost_provisions(cost);
        const Terrain terrain = map.terrain_at(here);
        const int remaining = per_day - used_hours;

        // Move edges (no provision, no overnight) to affordable, lit, walkable
        // cardinal neighbours: enough daylight for the travel hours and enough
        // stamina for the entry cost, matching the live validation order.
        for (const Coordinates offset : kCardinalOffsets) {
            const Coordinates neighbour = here + offset;
            if (!map.contains(neighbour)) {
                continue;
            }
            const Terrain destination = map.terrain_at(neighbour);
            const std::optional<std::uint32_t> step = stamina_cost_of(destination);
            const std::optional<std::uint32_t> hours = travel_hours_of(destination);
            if (!step.has_value() || !hours.has_value()) {
                continue;  // Impassable terrain is not a graph edge.
            }
            if (static_cast<int>(*hours) > remaining || stamina < static_cast<int>(*step)) {
                continue;  // Not enough daylight or stamina.
            }
            const int next_stamina = stamina - static_cast<int>(*step);
            const int next_daylight = used_hours + static_cast<int>(*hours);
            const int next_phase = (phase == 0 && neighbour == beacon) ? 1 : phase;
            const std::size_t next = state_index(neighbour, next_stamina, next_daylight, next_phase);
            const std::uint64_t candidate = pack_cost(overnights, provisions);
            if (candidate < dist[next]) {
                dist[next] = candidate;
                queue.emplace(candidate, next);
            }
        }

        // Emergency-rest edge (one provision, no overnight): stamina below the cap,
        // enough daylight for the rest, and a rest-capable terrain.
        if (stamina < static_cast<int>(max_stamina) &&
            static_cast<int>(emergency_rest_hours) <= remaining) {
            const std::optional<std::uint32_t> recovery = rest_recovery_of(terrain);
            if (recovery.has_value()) {
                const int recovered =
                    std::min(static_cast<int>(max_stamina), stamina + static_cast<int>(*recovery));
                const int next_daylight = used_hours + static_cast<int>(emergency_rest_hours);
                const std::size_t next = state_index(here, recovered, next_daylight, phase);
                const std::uint64_t candidate = pack_cost(overnights, provisions + 1u);
                if (candidate < dist[next]) {
                    dist[next] = candidate;
                    queue.emplace(candidate, next);
                }
            }
        }

        // Camp / bivouac edge (one overnight): eligible only after a daylight hour
        // elapsed or stamina is below the cap (REQ-010). A normal camp costs one
        // provision and restores stamina to the cap; a bivouac costs two and sets
        // stamina to 10. Both start a new day at zero daylight used.
        const bool camp_eligible = used_hours >= 1 || stamina < static_cast<int>(max_stamina);
        if (camp_eligible) {
            const CampKind kind = camp_kind_of(terrain);
            const std::uint32_t provision_cost = camp_provision_cost_of(kind);
            const int camp_stamina = static_cast<int>(camp_stamina_result_of(kind, max_stamina));
            const std::size_t next = state_index(here, camp_stamina, 0, phase);
            const std::uint64_t candidate = pack_cost(overnights + 1u, provisions + provision_cost);
            if (candidate < dist[next]) {
                dist[next] = candidate;
                queue.emplace(candidate, next);
            }
        }
    }

    // A reachable beacon always yields a finite plan; the fallback keeps a run from
    // ever being blocked if the goal is somehow unreachable.
    return ExpeditionPlanBaseline{1u, 0u};
}
