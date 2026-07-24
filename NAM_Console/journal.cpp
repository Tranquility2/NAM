#include "journal.h"

#include <variant>

#include "messages.h"

namespace nam::console {

namespace {

// The cardinal name used in travel prose (REQ-013 / REQ-014). Movement commands
// are cardinal on the map grid, so "up" reads as north, "down" as south, "left"
// as west, and "right" as east. A trailing fallback keeps the switch total for
// every compiler in the portability baseline.
[[nodiscard]] std::string cardinal_name(Direction direction) {
    switch (direction) {
        case Direction::up:    return "north";
        case Direction::down:  return "south";
        case Direction::left:  return "west";
        case Direction::right: return "east";
    }
    return "north";
}

[[nodiscard]] std::string travel_prose(const TravelEntry& entry) {
    std::string text = "Traveled " + cardinal_name(entry.direction) + " across " +
                       terrain_name(entry.terrain) + " for " + std::to_string(entry.steps) +
                       (entry.steps == 1 ? " step." : " steps.");
    return text;
}

[[nodiscard]] std::string rest_prose(const RestEntry& entry) {
    if (entry.stamina_recovered == 0) {
        return "Rested, but stamina was already full.";
    }
    return "Rested and recovered " + std::to_string(entry.stamina_recovered) + " stamina.";
}

// A fallback-safe visitor: one operator() per entry kind, each returning fixed
// frontend wording plus decimal counters and the deterministic beacon name only
// (SEC-001 / SEC-002).
struct EntryFormatter {
    std::string operator()(const TravelEntry& entry) const { return travel_prose(entry); }
    std::string operator()(const RestEntry& entry) const { return rest_prose(entry); }
    std::string operator()(const DiscoveryEntry& entry) const {
        return "Discovered " + entry.beacon_name + ".";
    }
    std::string operator()(const CompletionEntry& entry) const {
        return "Returned to spawn after reaching " + entry.beacon_name + "; expedition complete.";
    }
    std::string operator()(const InitialCompletionEntry& entry) const {
        return "Found " + entry.beacon_name + " at spawn; the expedition was already complete.";
    }
};

}  // namespace

void Journal::record_event(const GameEvent& event, const std::string& beacon_name) {
    if (const auto* move = std::get_if<MoveAttemptedEvent>(&event.data)) {
        if (move->outcome.result != MoveResult::moved) {
            // A blocked attempt creates no visible entry but breaks grouping so a
            // later compatible move cannot merge across it (REQ-005 / REQ-006).
            travel_open_ = false;
            return;
        }

        // Merge into an open, compatible travel entry, or start a new one. When
        // travel_open_ is true the newest entry is guaranteed to be a travel
        // entry; the variant check keeps the fold safe regardless.
        bool merged = false;
        if (travel_open_ && !entries_.empty()) {
            if (auto* travel = std::get_if<TravelEntry>(&entries_.back().data)) {
                if (travel->direction == move->direction &&
                    travel->terrain == move->outcome.terrain) {
                    ++travel->steps;
                    travel->last_sequence = event.sequence;
                    travel->stamina_spent += move->outcome.stamina_cost;
                    merged = true;
                }
            }
        }
        if (!merged) {
            TravelEntry travel;
            travel.direction = move->direction;
            travel.terrain = move->outcome.terrain;
            travel.steps = 1;
            travel.first_sequence = event.sequence;
            travel.last_sequence = event.sequence;
            travel.stamina_spent = move->outcome.stamina_cost;
            entries_.push_back(JournalEntry{travel});
        }
        travel_open_ = true;

        // Append the objective entry after the completing move has merged, so the
        // next movement starts a new travel group (REQ-037).
        switch (move->objective_update.transition) {
            case ObjectiveTransition::beacon_discovered:
                entries_.push_back(JournalEntry{DiscoveryEntry{event.sequence, beacon_name}});
                travel_open_ = false;
                break;
            case ObjectiveTransition::expedition_completed:
                entries_.push_back(JournalEntry{CompletionEntry{event.sequence, beacon_name}});
                travel_open_ = false;
                break;
            case ObjectiveTransition::none:
                break;
        }
        return;
    }

    if (const auto* rested = std::get_if<RestedEvent>(&event.data)) {
        entries_.push_back(JournalEntry{RestEntry{event.sequence, rested->stamina_before,
                                                  rested->stamina_recovered,
                                                  rested->stamina_after}});
        // Rest is its own command family and always breaks travel grouping.
        travel_open_ = false;
        return;
    }
}

void Journal::record_initial_completion(const std::string& beacon_name) {
    entries_.push_back(JournalEntry{InitialCompletionEntry{beacon_name}});
    travel_open_ = false;
}

std::string format_entry(const JournalEntry& entry) {
    return std::visit(EntryFormatter{}, entry.data);
}

}  // namespace nam::console
