#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "direction.h"
#include "game_event.h"
#include "terrain.h"

namespace nam::console {

// The frontend-owned expedition journal. It is derived entirely from the ordered
// core GameEvent stream plus the core-owned beacon name, and it holds no terminal
// dimensions or presentation state (REQ-001 / GUD-003).
// Entries are structured value types rather than pre-rendered prose (REQ-004),
// so a future narrator or export path can re-render them without re-deriving
// anything from the map.

// A run of adjacent successful movements grouped by direction and destination
// terrain (REQ-007 / REQ-008). `steps` counts the merged moves; the sequence
// pair brackets the first and last merged move; `stamina_spent` is the total
// stamina the merged moves cost.
struct TravelEntry {
    Direction direction{};
    Terrain terrain{};
    std::uint64_t steps = 0;
    std::uint64_t first_sequence = 0;
    std::uint64_t last_sequence = 0;
    std::uint64_t stamina_spent = 0;
};

// A single provision-funded rest command (REQ-111). Only a rest that actually
// recovered stamina (below full with a provision to spend) produces an entry; a
// heroic full-stamina rest and a no-provisions rest produce no journal entry.
struct RestEntry {
    std::uint64_t sequence = 0;
    Terrain terrain{};
    std::uint32_t stamina_before = 0;
    std::uint32_t stamina_recovered = 0;
    std::uint32_t stamina_after = 0;
    std::uint32_t provisions_after = 0;
};

// A single successful normal camp that ended the day (REQ-032). Only a camp that
// actually happened (eligible and affordable) produces an entry; an ineligible or
// unaffordable camp produces no journal entry. Carries the terrain, the day the
// night began and the day it started, and the stamina and provisions after.
struct CampEntry {
    std::uint64_t sequence = 0;
    Terrain terrain{};
    std::uint32_t day_before = 0;
    std::uint32_t day_after = 0;
    std::uint32_t stamina_after = 0;
    std::uint32_t provisions_after = 0;
};

// A single successful emergency bivouac on water or mountain that ended the day
// (REQ-032). Only a bivouac that actually happened produces an entry. Carries the
// same time and resource fields as a normal camp.
struct BivouacEntry {
    std::uint64_t sequence = 0;
    Terrain terrain{};
    std::uint32_t day_before = 0;
    std::uint32_t day_after = 0;
    std::uint32_t stamina_after = 0;
    std::uint32_t provisions_after = 0;
};

// The command after which the expedition missed its return deadline and a late
// retrieval party collected the explorer (REQ-032 / REQ-033). It is appended after
// the triggering command's own entry. `beacon_discovered` records whether the
// beacon had been reached before the deadline was missed.
struct OverdueEntry {
    std::uint64_t sequence = 0;
    std::string beacon_name;
    bool beacon_discovered = false;
};

// The move that first entered the beacon cell (REQ-010).
struct DiscoveryEntry {
    std::uint64_t sequence = 0;
    std::string beacon_name;
};

// The move that returned to spawn and completed the expedition (REQ-011).
struct CompletionEntry {
    std::uint64_t sequence = 0;
    std::string beacon_name;
};

// A single-reachable-cell game that started already completed (REQ-012). It is
// created explicitly rather than from a command event because no command runs.
struct InitialCompletionEntry {
    std::string beacon_name;
};

// The command after which the expedition became stranded and the explorer was
// rescued (REQ-122). It is appended after the triggering command's own entry and
// never pretends a failed or full rest succeeded. `beacon_discovered` records
// whether the beacon had been reached before the rescue.
struct RescueEntry {
    std::uint64_t sequence = 0;
    std::string beacon_name;
    bool beacon_discovered = false;
};

// The payload of one journal entry. A variant so distinct entry kinds keep their
// own typed fields and prose is rendered through one total visitor (GUD-002).
using JournalEntryData =
    std::variant<TravelEntry, RestEntry, CampEntry, BivouacEntry, DiscoveryEntry, CompletionEntry,
                 InitialCompletionEntry, RescueEntry, OverdueEntry>;

// One journal entry: a structured payload with no rendered text of its own.
struct JournalEntry {
    JournalEntryData data{};
};

// Aggregates the ordered core event stream into structured journal entries. The
// model keeps only structured data; concise prose is produced separately by
// format_entry so the same entries can feed any future narrator or export.
class Journal {
public:
    // Fold one ordered core event into the journal. A successful movement merges
    // into an immediately preceding compatible travel entry (matching direction
    // and destination terrain with no intervening blocked attempt, rest, or
    // objective entry) or starts a new travel entry. A discovering or completing
    // move merges normally and then appends its objective entry so the next
    // movement starts a fresh travel group (REQ-037). A blocked movement creates
    // no visible entry but still breaks travel grouping (REQ-006). Every rest
    // produces one entry (REQ-009). `beacon_name` is used only for objective
    // entries; callers pass the core-owned deterministic name.
    void record_event(const GameEvent& event, const std::string& beacon_name);

    // Record the explicit initial-completion entry for a game that started
    // already completed at spawn (REQ-012). Breaks any travel grouping.
    void record_initial_completion(const std::string& beacon_name);

    // Record the structured rescue entry after the command that stranded the
    // expedition (REQ-122). `beacon_discovered` records whether the beacon had
    // been reached before the rescue. Breaks any travel grouping.
    void record_rescue(const std::string& beacon_name, bool beacon_discovered);

    // Record the structured overdue entry after the command that missed the return
    // deadline (REQ-033). `beacon_discovered` records whether the beacon had been
    // reached before the deadline was missed. Breaks any travel grouping.
    void record_overdue(const std::string& beacon_name, bool beacon_discovered);

    [[nodiscard]] const std::vector<JournalEntry>& entries() const noexcept { return entries_; }
    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

private:
    std::vector<JournalEntry> entries_;
    // True only when the newest entry is a travel entry still eligible to absorb
    // a compatible next move. Any blocked attempt, rest, or objective entry sets
    // this false so grouping can never span those boundaries (REQ-006).
    bool travel_open_ = false;
};

// Render one journal entry as concise cartographer prose without coordinates
// (REQ-013 through REQ-018). A single total function over every entry kind with
// a fallback return, so no entry can render empty text (GUD-002 / SEC-001).
[[nodiscard]] std::string format_entry(const JournalEntry& entry);

}  // namespace nam::console
