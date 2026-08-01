#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "game_event.h"
#include "level_tier.h"

namespace nam::console {

// The frontend-owned expedition journal. It is derived entirely from the ordered
// core GameEvent stream plus the core-owned landmark name, and it holds no
// terminal dimensions or presentation state (REQ-001 / GUD-003).
// Entries are structured value types rather than pre-rendered prose (REQ-004),
// so a future narrator or export path can re-render them without re-deriving
// anything from the map.
//
// The journal is a collection of memorable moments, not an event log. Routine
// movement, sight changes, and blocked input are reported
// through the immediate HUD message and the final statistics; only durable
// milestones become entries, so a full run stays readable end to end.
//
// Entries fall into the four categories the prototype can produce: an optional
// discovery, a notable encounter, an objective milestone, and a level completion.
// A full level yields about three entries, which keeps a four-tier run near its
// budget.

// An optional find the actor entered for the first time. `ordinal` is its place
// in the level's discovery order, so an entry reads as progress rather than as a
// repeated anonymous event.
struct DiscoveryEntry {
    std::uint64_t sequence = 0;
    std::uint32_t ordinal = 0;
    std::uint32_t total = 0;
};

// The move that first entered a vantage point, which opened the level up with a
// one-off wide reveal. Reaching the named landmark grants the same sight but is
// recorded as a LandmarkEntry, because that is the larger moment.
struct VantageEntry {
    std::uint64_t sequence = 0;
};

// The move that first entered the landmark cell, revealing the exit bearing and a
// wide view of the level.
struct LandmarkEntry {
    std::uint64_t sequence = 0;
    std::string landmark_name;
};

// The move that entered the exit cell and completed the level. It carries the
// level's place in the chain so the entry reads as a milestone in an expedition
// rather than as an isolated ending.
struct CompletionEntry {
    std::uint64_t sequence = 0;
    std::string landmark_name;
    LevelTier tier = LevelTier::small;
    std::uint32_t level_number = 1;
    std::uint32_t total_levels = 1;
    std::uint32_t discoveries_found = 0;
    std::uint32_t discovery_total = 0;
};

// A single-reachable-cell game that started already completed (REQ-012). It is
// created explicitly rather than from a command event because no command runs.
struct InitialCompletionEntry {
    std::string landmark_name;
};

// The payload of one journal entry. A variant so distinct entry kinds keep their
// own typed fields and prose is rendered through one total visitor (GUD-002).
using JournalEntryData =
    std::variant<DiscoveryEntry, VantageEntry, LandmarkEntry, CompletionEntry,
                 InitialCompletionEntry>;

// One journal entry: a structured payload with no rendered text of its own.
struct JournalEntry {
    JournalEntryData data{};
};

// What the frontend knows about the level an event belongs to. Core events carry
// the outcome; this supplies the surrounding level identity the journal needs to
// phrase a milestone, so the journal itself still stores no presentation state.
struct JournalContext {
    std::string landmark_name;
    LevelTier tier = LevelTier::small;
    std::uint32_t level_number = 1;
    std::uint32_t total_levels = 1;
    // The level's discovery tallies read after the event was applied.
    std::uint32_t discoveries_found = 0;
    std::uint32_t discovery_total = 0;
};

// Aggregates the ordered core event stream into structured journal entries. The
// model keeps only structured data; concise prose is produced separately by
// format_entry so the same entries can feed any future narrator or export.
class Journal {
public:
    // Fold one ordered core event into the journal. A successful movement appends
    // an entry when it records a discovery or carries an objective transition; a
    // routine step and every other event, including a blocked attempt, leave the
    // journal unchanged. One move can both find a discovery and complete the
    // level, in which case both entries are appended in that order.
    void record_event(const GameEvent& event, const JournalContext& context);

    // Record the explicit initial-completion entry for a game that started
    // already completed at spawn (REQ-012).
    void record_initial_completion(const std::string& landmark_name);

    [[nodiscard]] const std::vector<JournalEntry>& entries() const noexcept { return entries_; }
    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

private:
    std::vector<JournalEntry> entries_;
};

// Render one journal entry as concise cartographer prose without coordinates
// (REQ-013 through REQ-018). A single total function over every entry kind with
// a fallback return, so no entry can render empty text (GUD-002 / SEC-001).
[[nodiscard]] std::string format_entry(const JournalEntry& entry);

}  // namespace nam::console
