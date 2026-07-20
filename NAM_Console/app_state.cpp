#include "app_state.h"

#include <utility>
#include <variant>

#include "messages.h"

namespace nam::console {

void Hud::record_event(const GameEvent& event) {
    const MoveAttemptedEvent& payload = std::get<MoveAttemptedEvent>(event.data);
    const MoveOutcome& outcome = payload.outcome;

    ++attempt_count_;
    last_move_succeeded_ = outcome.result == MoveResult::moved;
    if (last_move_succeeded_) {
        ++move_count_;
    }

    recent_.push_back(RecentMove{payload.direction, outcome.result});
    while (recent_.size() > recent_capacity) {
        recent_.pop_front();
    }

    message_ = describe_move(outcome);
}

void Hud::set_message(std::string message) {
    message_ = std::move(message);
}

}  // namespace nam::console
