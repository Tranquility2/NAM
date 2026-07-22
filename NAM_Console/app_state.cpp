#include "app_state.h"

#include <utility>
#include <variant>

#include "messages.h"

namespace nam::console {

void Hud::record_event(const GameEvent& event) {
    if (const auto* move = std::get_if<MoveAttemptedEvent>(&event.data)) {
        const MoveOutcome& outcome = move->outcome;

        ++attempt_count_;
        last_move_succeeded_ = outcome.result == MoveResult::moved;
        if (last_move_succeeded_) {
            ++move_count_;
            // Only a successful move enters the route history.
            recent_.push_back(RecentMove{move->direction});
            while (recent_.size() > recent_capacity) {
                recent_.pop_front();
            }
        }

        message_ = describe_move(outcome);
        return;
    }

    if (const auto* rested = std::get_if<RestedEvent>(&event.data)) {
        // Rest is not a movement: it updates only the latest message and clears
        // the success flag, leaving attempt/move counters and history unchanged.
        last_move_succeeded_ = false;
        message_ = describe_rest(*rested);
        return;
    }
}

void Hud::set_message(std::string message) {
    message_ = std::move(message);
}

}  // namespace nam::console
