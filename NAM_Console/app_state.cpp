#include "app_state.h"

#include <utility>

#include "messages.h"

namespace nam::console {

void Hud::record_move(Direction direction, const MoveOutcome& outcome) {
    ++attempt_count_;
    last_move_succeeded_ = outcome.result == MoveResult::moved;
    if (last_move_succeeded_) {
        ++move_count_;
    }

    recent_.push_back(RecentMove{direction, outcome.result});
    while (recent_.size() > recent_capacity) {
        recent_.pop_front();
    }

    message_ = describe_move(outcome);
}

void Hud::set_message(std::string message) {
    message_ = std::move(message);
}

}  // namespace nam::console
