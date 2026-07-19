#pragma once

#include <cstddef>
#include <deque>
#include <string>

#include "direction.h"
#include "move_outcome.h"

namespace nam::console {

// One entry in the bounded recent-move history.
struct RecentMove {
    Direction direction{};
    MoveResult result{};
};

// Structured, bounded gameplay tracking. This replaces the old ever-growing
// vector of key letters: history is capped, so a frame can always be rendered
// in constant space no matter how long the session runs. The state is plain
// data with no hidden randomness, which keeps sessions deterministic and makes
// this a natural basis for a future replay log.
class Hud {
public:
    // The most recent moves kept for display. Fixed so the HUD never grows.
    static constexpr std::size_t recent_capacity = 12;

    // Record a move attempt and its outcome, updating the counter, the bounded
    // history, and the latest-event message.
    void record_move(Direction direction, const MoveOutcome& outcome);

    // Replace the latest-event message without recording a move (used for
    // welcome text, resize notices, and shutdown).
    void set_message(std::string message);

    [[nodiscard]] std::size_t move_count() const noexcept { return move_count_; }
    [[nodiscard]] std::size_t attempt_count() const noexcept { return attempt_count_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }
    [[nodiscard]] const std::deque<RecentMove>& recent() const noexcept { return recent_; }

    // True when the last recorded move actually changed the actor's position.
    // Used to drive one-frame move emphasis when animation is enabled.
    [[nodiscard]] bool last_move_succeeded() const noexcept { return last_move_succeeded_; }

private:
    std::size_t move_count_ = 0;
    std::size_t attempt_count_ = 0;
    std::deque<RecentMove> recent_;
    std::string message_;
    bool last_move_succeeded_ = false;
};

}  // namespace nam::console
