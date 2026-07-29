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
    return "Rested on " + terrain_name(entry.terrain) + " and recovered " +
           std::to_string(entry.stamina_recovered) + " stamina (" +
           std::to_string(entry.provisions_after) + " provisions left).";
}

[[nodiscard]] std::string camp_prose(const CampEntry& entry) {
    return "Camped on " + terrain_name(entry.terrain) + " overnight; day " +
           std::to_string(entry.day_after) + " began with " +
           std::to_string(entry.stamina_after) + " stamina (" +
           std::to_string(entry.provisions_after) + " provisions left).";
}

[[nodiscard]] std::string bivouac_prose(const BivouacEntry& entry) {
    return "Bivouacked on " + terrain_name(entry.terrain) + " overnight; day " +
           std::to_string(entry.day_after) + " began with " +
           std::to_string(entry.stamina_after) + " stamina (" +
           std::to_string(entry.provisions_after) + " provisions left).";
}

[[nodiscard]] std::string overdue_prose(const OverdueEntry& entry) {
    if (entry.beacon_discovered) {
        return "Missed the level deadline after discovering " + entry.beacon_name +
               "; a late retrieval party collected the explorer.";
    }
    return "Missed the level deadline before discovering " + entry.beacon_name +
           "; a late retrieval party collected the explorer.";
}

[[nodiscard]] std::string rescue_prose(const RescueEntry& entry) {
    if (entry.beacon_discovered) {
        return "Ran out of provisions after reaching " + entry.beacon_name +
               "; signaled for an early rescue.";
    }
    return "Ran out of provisions before reaching " + entry.beacon_name +
           "; signaled for an early rescue.";
}

// A fallback-safe visitor: one operator() per entry kind, each returning fixed
// frontend wording plus decimal counters and the deterministic beacon name only
// (SEC-001 / SEC-002).
struct EntryFormatter {
    std::string operator()(const TravelEntry& entry) const { return travel_prose(entry); }
    std::string operator()(const RestEntry& entry) const { return rest_prose(entry); }
    std::string operator()(const CampEntry& entry) const { return camp_prose(entry); }
    std::string operator()(const BivouacEntry& entry) const { return bivouac_prose(entry); }
    std::string operator()(const DiscoveryEntry& entry) const {
        return "Discovered " + entry.beacon_name + "; the exit direction was revealed.";
    }
    std::string operator()(const CompletionEntry& entry) const {
        return "Reached the exit after " + entry.beacon_name + "; level complete.";
    }
    std::string operator()(const InitialCompletionEntry& entry) const {
        return "Found " + entry.beacon_name + " and the exit at spawn; the level was already complete.";
    }
    std::string operator()(const RescueEntry& entry) const { return rescue_prose(entry); }
    std::string operator()(const OverdueEntry& entry) const { return overdue_prose(entry); }
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
        // Only a provision-funded rest that recovered stamina produces an entry; a
        // heroic full-stamina rest, a no-daylight rest, and a no-provisions rest are
        // no-ops here. Rest always breaks travel grouping.
        if (rested->result == RestResult::recovered) {
            entries_.push_back(JournalEntry{RestEntry{event.sequence, rested->terrain,
                                                      rested->stamina_before,
                                                      rested->stamina_recovered,
                                                      rested->stamina_after,
                                                      rested->provisions_after}});
        }
        travel_open_ = false;
        return;
    }

    if (const auto* camped = std::get_if<CampedEvent>(&event.data)) {
        // Only a successful camp or bivouac produces an entry; an ineligible or
        // unaffordable camp is a no-op here. A normal camp and an emergency bivouac
        // are distinct journal variants so report counts derive from typed entries,
        // not prose (REQ-032). Camp always breaks travel grouping.
        if (camped->result == CampResult::camped) {
            if (camped->kind == CampKind::bivouac) {
                entries_.push_back(JournalEntry{BivouacEntry{
                    event.sequence, camped->terrain, camped->time.before.day, camped->time.after.day,
                    camped->stamina_after, camped->provisions_after}});
            } else {
                entries_.push_back(JournalEntry{CampEntry{
                    event.sequence, camped->terrain, camped->time.before.day, camped->time.after.day,
                    camped->stamina_after, camped->provisions_after}});
            }
        }
        travel_open_ = false;
        return;
    }
}

void Journal::record_rescue(const std::string& beacon_name, bool beacon_discovered) {
    // The sequence field is informational; the rescue is appended after the
    // triggering command's own entry. Keep grouping closed after a terminal entry.
    entries_.push_back(JournalEntry{RescueEntry{0, beacon_name, beacon_discovered}});
    travel_open_ = false;
}

void Journal::record_overdue(const std::string& beacon_name, bool beacon_discovered) {
    // The overdue entry is appended after the triggering command's own entry. Keep
    // grouping closed after a terminal entry.
    entries_.push_back(JournalEntry{OverdueEntry{0, beacon_name, beacon_discovered}});
    travel_open_ = false;
}

void Journal::record_initial_completion(const std::string& beacon_name) {
    entries_.push_back(JournalEntry{InitialCompletionEntry{beacon_name}});
    travel_open_ = false;
}

std::string format_entry(const JournalEntry& entry) {
    return std::visit(EntryFormatter{}, entry.data);
}

}  // namespace nam::console
