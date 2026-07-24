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

// A single rest command (REQ-009). A rest at full stamina still produces an entry
// with `stamina_recovered == 0` and `stamina_before == stamina_after`.
struct RestEntry {
    std::uint64_t sequence = 0;
    std::uint32_t stamina_before = 0;
    std::uint32_t stamina_recovered = 0;
    std::uint32_t stamina_after = 0;
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

// The payload of one journal entry. A variant so distinct entry kinds keep their
// own typed fields and prose is rendered through one total visitor (GUD-002).
using JournalEntryData =
    std::variant<TravelEntry, RestEntry, DiscoveryEntry, CompletionEntry, InitialCompletionEntry>;

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
