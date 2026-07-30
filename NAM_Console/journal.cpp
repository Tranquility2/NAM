#include "journal.h"

#include <variant>

namespace nam::console {

namespace {

// A fallback-safe visitor: one operator() per entry kind, each returning fixed
// frontend wording plus decimal counters and the deterministic landmark name
// only (SEC-001 / SEC-002).
struct EntryFormatter {
    std::string operator()(const DiscoveryEntry& entry) const {
        return "Discovered " + entry.landmark_name + "; the exit direction was revealed.";
    }
    std::string operator()(const CompletionEntry& entry) const {
        return "Reached the exit after " + entry.landmark_name + "; level complete.";
    }
    std::string operator()(const InitialCompletionEntry& entry) const {
        return "Found " + entry.landmark_name + " and the exit at spawn; the level was already complete.";
    }
};

}  // namespace

void Journal::record_event(const GameEvent& event, const std::string& landmark_name) {
    const auto* move = std::get_if<MoveAttemptedEvent>(&event.data);
    if (move == nullptr || move->outcome.result != MoveResult::moved) return;

    switch (move->objective_update.transition) {
        case ObjectiveTransition::landmark_discovered:
            entries_.push_back(JournalEntry{DiscoveryEntry{event.sequence, landmark_name}});
            return;
        case ObjectiveTransition::level_completed:
            entries_.push_back(JournalEntry{CompletionEntry{event.sequence, landmark_name}});
            return;
        case ObjectiveTransition::none:
            return;
    }
}

void Journal::record_initial_completion(const std::string& landmark_name) {
    entries_.push_back(JournalEntry{InitialCompletionEntry{landmark_name}});
}

std::string format_entry(const JournalEntry& entry) {
    return std::visit(EntryFormatter{}, entry.data);
}

}  // namespace nam::console
