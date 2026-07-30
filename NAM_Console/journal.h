#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "game_event.h"

namespace nam::console {

// The frontend-owned expedition journal. It is derived entirely from the ordered
// core GameEvent stream plus the core-owned landmark name, and it holds no
// terminal dimensions or presentation state (REQ-001 / GUD-003).
// Entries are structured value types rather than pre-rendered prose (REQ-004),
// so a future narrator or export path can re-render them without re-deriving
// anything from the map.
//
// The journal is a collection of memorable moments, not an event log. Routine
// movement, stamina changes, passive recovery, and blocked input are reported
// through the immediate HUD message and the final statistics; only durable
// milestones become entries, so a full run stays readable end to end.

// The move that first entered the landmark cell and revealed the exit bearing.
struct DiscoveryEntry {
    std::uint64_t sequence = 0;
    std::string landmark_name;
};

// The move that entered the exit cell and completed the level.
struct CompletionEntry {
    std::uint64_t sequence = 0;
    std::string landmark_name;
};

// A single-reachable-cell game that started already completed (REQ-012). It is
// created explicitly rather than from a command event because no command runs.
struct InitialCompletionEntry {
    std::string landmark_name;
};

// The payload of one journal entry. A variant so distinct entry kinds keep their
// own typed fields and prose is rendered through one total visitor (GUD-002).
using JournalEntryData = std::variant<DiscoveryEntry, CompletionEntry, InitialCompletionEntry>;

// One journal entry: a structured payload with no rendered text of its own.
struct JournalEntry {
    JournalEntryData data{};
};

// Aggregates the ordered core event stream into structured journal entries. The
// model keeps only structured data; concise prose is produced separately by
// format_entry so the same entries can feed any future narrator or export.
class Journal {
public:
    // Fold one ordered core event into the journal. Only a successful movement
    // that carries an objective transition appends an entry; every other event,
    // including a blocked attempt, leaves the journal unchanged. `landmark_name`
    // is the core-owned deterministic name used by those entries.
    void record_event(const GameEvent& event, const std::string& landmark_name);

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
