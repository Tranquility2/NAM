#include "journal.h"

#include <variant>

namespace nam::console {

namespace {

// A fallback-safe visitor: one operator() per entry kind, each returning fixed
// frontend wording plus decimal counters and the deterministic landmark name
// only (SEC-001 / SEC-002).
struct EntryFormatter {
    std::string operator()(const DiscoveryEntry& entry) const {
        return "Found a hidden site off the route (" + std::to_string(entry.ordinal) + " of " +
               std::to_string(entry.total) + ").";
    }
    std::string operator()(const VantageEntry&) const {
        return "Climbed to a vantage point and the level opened up.";
    }
    std::string operator()(const LandmarkEntry& entry) const {
        return "Sighted " + entry.landmark_name +
               "; the land opened up and the exit direction was revealed.";
    }
    std::string operator()(const CompletionEntry& entry) const {
        if (entry.total_levels <= 1u) {
            return "Reached the exit after " + entry.landmark_name + "; level complete.";
        }
        return "Left the " + std::string(to_string(entry.tier)) + " level through the exit (" +
               std::to_string(entry.level_number) + " of " +
               std::to_string(entry.total_levels) + " complete, " +
               std::to_string(entry.discoveries_found) + " of " +
               std::to_string(entry.discovery_total) + " discoveries found).";
    }
    std::string operator()(const InitialCompletionEntry& entry) const {
        return "Found " + entry.landmark_name + " and the exit at spawn; the level was already complete.";
    }
};

}  // namespace

void Journal::record_event(const GameEvent& event, const JournalContext& context) {
    const auto* move = std::get_if<MoveAttemptedEvent>(&event.data);
    if (move == nullptr || move->outcome.result != MoveResult::moved) return;

    if (move->discovery_recorded) {
        entries_.push_back(JournalEntry{
            DiscoveryEntry{event.sequence, context.discoveries_found, context.discovery_total}});
    }

    // A landmark grants the same wide reveal, but it is recorded below as the
    // larger milestone rather than twice. Only the level's first vantage point
    // becomes an entry: a level opens up once, and a sweep that climbs every one
    // of them would otherwise flood the run's entry budget.
    if (move->wide_reveal_granted && !level_vantage_logged_ &&
        move->objective_update.transition != ObjectiveTransition::landmark_discovered) {
        entries_.push_back(JournalEntry{VantageEntry{event.sequence}});
        level_vantage_logged_ = true;
    }

    switch (move->objective_update.transition) {
        case ObjectiveTransition::landmark_discovered:
            entries_.push_back(JournalEntry{LandmarkEntry{event.sequence, context.landmark_name}});
            return;
        case ObjectiveTransition::level_completed:
            entries_.push_back(JournalEntry{CompletionEntry{
                event.sequence, context.landmark_name, context.tier, context.level_number,
                context.total_levels, context.discoveries_found, context.discovery_total}});
            return;
        case ObjectiveTransition::none:
            return;
    }
}

void Journal::record_initial_completion(const std::string& landmark_name) {
    entries_.push_back(JournalEntry{InitialCompletionEntry{landmark_name}});
}

void Journal::begin_level() noexcept {
    level_vantage_logged_ = false;
}

std::string format_entry(const JournalEntry& entry) {
    return std::visit(EntryFormatter{}, entry.data);
}

}  // namespace nam::console
