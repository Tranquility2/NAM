#pragma once

#include <cstdint>

// Frontend-neutral, deterministic expedition time. One expedition day contains a
// fixed number of daylight hours; every successful move, emergency rest, camp, or
// bivouac spends daylight or ends the day. Keeping the time model in the core lets
// movement, planning, endings, and every frontend share one authoritative clock
// with no wall-clock, calendar, or locale dependence (SEC-001).

// The number of daylight hours in one expedition day (REQ-001).
inline constexpr std::uint32_t daylight_hours_per_day = 12;

// The daylight hours a single successful emergency rest consumes (REQ-006).
inline constexpr std::uint32_t emergency_rest_hours = 2;

// The typed current expedition time: the numbered day (at least 1), the daylight
// hours already used during that day, and the fixed number of daylight hours per
// day. A new, nontrivial expedition starts on day 1 with 0 daylight hours used.
// The per-day capacity is stored so a frontend never has to re-derive the 12-hour
// contract, and so the value can preserve its own invariants (GUD-002).
struct ExpeditionTime {
    std::uint32_t day = 1;
    std::uint32_t daylight_hours_used = 0;
    std::uint32_t daylight_hours_per_day = ::daylight_hours_per_day;

    // The daylight hours still available in the current day, via
    // comparison-before-subtraction so the unsigned difference never underflows.
    [[nodiscard]] constexpr std::uint32_t remaining_daylight() const noexcept {
        return daylight_hours_per_day > daylight_hours_used
                   ? daylight_hours_per_day - daylight_hours_used
                   : 0u;
    }

    // Whether `hours` of travel or rest fit within the remaining daylight.
    [[nodiscard]] constexpr bool fits(std::uint32_t hours) const noexcept {
        return hours <= remaining_daylight();
    }

    // Whether the day is fully spent (no daylight hours remain).
    [[nodiscard]] constexpr bool day_exhausted() const noexcept {
        return daylight_hours_used >= daylight_hours_per_day;
    }
};

// Value equality over every field, so tests and event consumers can compare two
// times without re-deriving the comparison.
[[nodiscard]] constexpr bool operator==(const ExpeditionTime& left,
                                        const ExpeditionTime& right) noexcept {
    return left.day == right.day && left.daylight_hours_used == right.daylight_hours_used &&
           left.daylight_hours_per_day == right.daylight_hours_per_day;
}

[[nodiscard]] constexpr bool operator!=(const ExpeditionTime& left,
                                        const ExpeditionTime& right) noexcept {
    return !(left == right);
}

// A typed before/after pair of expedition times carried on a command event so a
// frontend never re-derives the time change a command caused (REQ-028).
struct TimeUpdate {
    ExpeditionTime before{};
    ExpeditionTime after{};
};

// The deterministic deadline day for an expedition whose minimum completion day
// count is `minimum_completion_days`: two days beyond the minimum (REQ-019). The
// addition is overflow-safe: minimum completion days are tiny map-derived values,
// but the guard keeps the helper total for a hypothetical hostile input.
[[nodiscard]] constexpr std::uint32_t deadline_day_for(
    std::uint32_t minimum_completion_days) noexcept {
    constexpr std::uint32_t maximum = static_cast<std::uint32_t>(-1);
    if (minimum_completion_days > maximum - 2u) {
        return maximum;
    }
    return minimum_completion_days + 2u;
}
