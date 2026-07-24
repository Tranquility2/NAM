#include "console_app.h"

#include <cctype>
#include <iostream>
#include <utility>
#include <variant>

#include "game_event.h"
#include "messages.h"

namespace nam::console {

namespace {

[[nodiscard]] char lower(char value) noexcept {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
}

// Trim surrounding ASCII whitespace and lower-case a plain-mode command word.
[[nodiscard]] std::string normalize_command(const std::string& line) {
    std::size_t begin = 0;
    std::size_t end = line.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(line[begin])) != 0) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(line[end - 1])) != 0) {
        --end;
    }
    std::string word;
    word.reserve(end - begin);
    for (std::size_t i = begin; i < end; ++i) {
        word.push_back(lower(line[i]));
    }
    return word;
}

enum class PlainCommand { none, quit, rest, up, down, left, right, unknown };

[[nodiscard]] PlainCommand parse_plain_command(const std::string& normalized) {
    if (normalized.empty()) return PlainCommand::none;
    if (normalized == "q" || normalized == "quit" || normalized == "exit") return PlainCommand::quit;
    if (normalized == "r" || normalized == "rest") return PlainCommand::rest;
    if (normalized == "w" || normalized == "k" || normalized == "up") return PlainCommand::up;
    if (normalized == "s" || normalized == "j" || normalized == "down") return PlainCommand::down;
    if (normalized == "a" || normalized == "h" || normalized == "left") return PlainCommand::left;
    if (normalized == "d" || normalized == "l" || normalized == "right") return PlainCommand::right;
    return PlainCommand::unknown;
}

// The one initial HUD line, shared by both modes so the seed notice cannot drift
// between them. Unseeded sessions keep their existing mode-specific welcome; a
// seeded session appends the safely escaped original seed. The seed is only ever
// shown through format_seed_for_display, so raw control bytes never reach output.
[[nodiscard]] std::string initial_message(const Settings& settings, bool interactive) {
    std::string message = interactive
                              ? "Welcome to NAM. Arrow keys or WASD to move, r to rest, q or Esc to quit."
                              : "Plain mode. Commands: w/a/s/d or up/down/left/right, r to rest, q to quit.";
    if (settings.seed_text) {
        message += " Tiny World seed: ";
        message += format_seed_for_display(*settings.seed_text);
    }
    return message;
}

}  // namespace

std::optional<Direction> direction_for(const KeyEvent& event) noexcept {
    switch (event.key) {
        case Key::up:    return Direction::up;
        case Key::down:  return Direction::down;
        case Key::left:  return Direction::left;
        case Key::right: return Direction::right;
        case Key::character:
            switch (lower(event.character)) {
                case 'w': case 'k': return Direction::up;
                case 's': case 'j': return Direction::down;
                case 'a': case 'h': return Direction::left;
                case 'd': case 'l': return Direction::right;
                default: return std::nullopt;
            }
        default:
            return std::nullopt;
    }
}

bool is_quit_event(const KeyEvent& event) noexcept {
    if (event.key == Key::escape) return true;
    return event.key == Key::character && lower(event.character) == 'q';
}

bool is_rest_event(const KeyEvent& event) noexcept {
    return event.key == Key::character && lower(event.character) == 'r';
}

ConsoleApp::ConsoleApp(GameState state, Settings settings)
    : state_(std::move(state)), settings_(std::move(settings)) {}

RenderInput ConsoleApp::make_input(bool emphasize) const {
    RenderInput input;
    input.map = &state_.map();
    input.visibility = &state_.visibility();
    input.actor = state_.actor_position();
    input.terrain = state_.actor_terrain();
    input.move_count = hud_.move_count();
    input.attempt_count = hud_.attempt_count();
    input.stamina = state_.stamina();
    input.max_stamina = state_.max_stamina();
    input.message = hud_.message();
    input.recent.assign(hud_.recent().begin(), hud_.recent().end());
    input.emphasize_actor = emphasize;
    // Production input always presents the core-owned objective; the renderer
    // gates the beacon overlay on visibility and shows the objective line.
    input.objective = &state_.objective();
    return input;
}

ObjectiveTransition ConsoleApp::apply_move(Direction direction, bool& emphasize) {
    const GameEvent event = state_.move(direction);
    const MoveAttemptedEvent& payload = std::get<MoveAttemptedEvent>(event.data);
    hud_.record_event(event);
    emphasize = payload.outcome.result == MoveResult::moved;
    // Replace the ordinary move wording only for the typed objective transitions;
    // every other successful or blocked move keeps its normal message.
    switch (payload.objective_update.transition) {
        case ObjectiveTransition::beacon_discovered:
            hud_.set_message(describe_beacon_discovered(state_.objective().name));
            break;
        case ObjectiveTransition::expedition_completed:
            hud_.set_message(describe_expedition_completed(state_.objective().name));
            break;
        case ObjectiveTransition::none:
            break;
    }
    return payload.objective_update.transition;
}

void ConsoleApp::apply_rest(bool& emphasize) {
    const GameEvent event = state_.rest();
    hud_.record_event(event);
    // Rest never moves the actor, so it never earns move emphasis.
    emphasize = false;
}

CompletionSummary ConsoleApp::make_completion_summary() const {
    // Built only after the HUD has recorded the completing event, so the counts and
    // final stamina include that move (REQ-024 / RISK-005).
    CompletionSummary summary;
    summary.beacon_name = state_.objective().name;
    summary.move_count = hud_.move_count();
    summary.attempt_count = hud_.attempt_count();
    summary.stamina = state_.stamina();
    summary.max_stamina = state_.max_stamina();
    return summary;
}

void ConsoleApp::enter_completion() {
    presentation_ = Presentation::expedition_complete;
    completion_summary_ = make_completion_summary();
    restored_message_ = restored_completion_message(state_.objective().name);
}

namespace {

// Adapts the production TerminalSession onto the mockable InteractiveSession
// interface so the shared loop is transport-agnostic and directly testable.
class TerminalSessionAdapter final : public InteractiveSession {
public:
    explicit TerminalSessionAdapter(TerminalSession& session) noexcept : session_(session) {}

    [[nodiscard]] bool supports_ansi() const override { return session_.supports_ansi(); }
    [[nodiscard]] TerminalSize size() const override { return session_.size(); }
    [[nodiscard]] KeyEvent read_event() override { return session_.read_event(); }
    void draw(const Frame& frame) override { session_.draw(frame); }

private:
    TerminalSession& session_;
};

}  // namespace

int ConsoleApp::run_interactive(TerminalSession& session) {
    TerminalSessionAdapter adapter(session);
    return run_interactive(adapter);
}

int ConsoleApp::run_interactive(InteractiveSession& session) {
    RenderConfig config;
    config.use_ansi = session.supports_ansi();
    config.use_color = settings_.use_color && config.use_ansi;
    config.debug = settings_.debug;
    config.emphasis = settings_.animation && config.use_ansi;
    const Renderer renderer(config);

    // Draw the single resulting frame after a movement command has been applied and
    // choose the next presentation state from its objective transition. Used by both
    // the gameplay and the discovery branches so a discovery-dismissing movement key
    // can transition straight to completion without an intermediate gameplay frame
    // (REQ-019).
    const auto present_move_result = [&](ObjectiveTransition transition, bool emphasize) {
        if (transition == ObjectiveTransition::expedition_completed) {
            enter_completion();
            session.draw(renderer.render_completion(completion_summary_, session.size()));
        } else if (transition == ObjectiveTransition::beacon_discovered) {
            presentation_ = Presentation::beacon_discovery;
            session.draw(renderer.render_discovery(state_.objective().name, session.size()));
        } else {
            presentation_ = Presentation::gameplay;
            session.draw(renderer.render(make_input(emphasize), session.size()));
        }
    };

    // Initial objective completion (single reachable walkable cell): start directly
    // on the completion screen and wait for an explicit acknowledgement (REQ-027).
    if (state_.objective_completed()) {
        enter_completion();
        session.draw(renderer.render_completion(completion_summary_, session.size()));
    } else {
        hud_.set_message(initial_message(settings_, /*interactive=*/true));
        presentation_ = Presentation::gameplay;
        session.draw(renderer.render(make_input(false), session.size()));
    }

    bool running = true;
    while (running) {
        const KeyEvent event = session.read_event();
        bool emphasize = false;

        switch (presentation_) {
            case Presentation::gameplay:
                switch (event.key) {
                    case Key::end_of_input:
                        hud_.set_message("End of input. Goodbye.");
                        running = false;
                        break;
                    case Key::interrupt:
                        hud_.set_message("Interrupted. Goodbye.");
                        running = false;
                        break;
                    case Key::resize:
                        session.draw(renderer.render(make_input(false), session.size()));
                        break;
                    default:
                        if (is_quit_event(event)) {
                            hud_.set_message("Goodbye.");
                            running = false;
                            break;
                        }
                        if (is_rest_event(event)) {
                            apply_rest(emphasize);
                            session.draw(renderer.render(make_input(emphasize), session.size()));
                            break;
                        }
                        if (const std::optional<Direction> direction = direction_for(event)) {
                            const ObjectiveTransition transition = apply_move(*direction, emphasize);
                            present_move_result(transition, emphasize);
                        }
                        // Recognized-but-unmapped events change nothing and draw
                        // nothing: no movement, no redraw.
                        break;
                }
                break;

            case Presentation::beacon_discovery:
                switch (event.key) {
                    case Key::end_of_input:
                        hud_.set_message("End of input. Goodbye.");
                        running = false;
                        break;
                    case Key::interrupt:
                        hud_.set_message("Interrupted. Goodbye.");
                        running = false;
                        break;
                    case Key::resize:
                        session.draw(
                            renderer.render_discovery(state_.objective().name, session.size()));
                        break;
                    case Key::enter:
                        // Dismiss the discovery screen and restore the intact
                        // gameplay frame without emitting an event (REQ-018).
                        presentation_ = Presentation::gameplay;
                        session.draw(renderer.render(make_input(false), session.size()));
                        break;
                    default:
                        if (is_quit_event(event)) {
                            hud_.set_message("Goodbye.");
                            running = false;
                            break;
                        }
                        if (const std::optional<Direction> direction = direction_for(event)) {
                            // A movement key dismisses discovery and executes that
                            // same movement exactly once (REQ-018 / RISK-003).
                            const ObjectiveTransition transition = apply_move(*direction, emphasize);
                            present_move_result(transition, emphasize);
                            break;
                        }
                        // Rest and every other key leave the discovery screen active
                        // and emit no event (REQ-018): no core update, no redraw.
                        break;
                }
                break;

            case Presentation::expedition_complete:
                switch (event.key) {
                    case Key::end_of_input:
                    case Key::interrupt:
                        // End of input or interrupt acknowledges completion and
                        // exits 0 without a goodbye line (REQ-025 / REQ-028).
                        running = false;
                        break;
                    case Key::resize:
                        session.draw(
                            renderer.render_completion(completion_summary_, session.size()));
                        break;
                    case Key::enter:
                        running = false;  // Acknowledge and exit 0.
                        break;
                    default:
                        if (is_quit_event(event)) {
                            running = false;  // q or Escape acknowledges and exits 0.
                            break;
                        }
                        // Every movement key, rest key, and other key leaves the
                        // completion screen active, emits no event, and never calls
                        // move or rest (REQ-025 / RISK-004): no state change, no draw.
                        break;
                }
                break;
        }
    }

    return 0;
}

int ConsoleApp::run_plain(std::istream& input, std::ostream& output) {
    const RenderConfig config{/*use_color=*/false, /*use_ansi=*/false, settings_.debug,
                              /*emphasis=*/false};
    const Renderer renderer(config);

    // Draw the single resulting block after a movement command has been applied and
    // choose the next presentation state from its objective transition. Shared by
    // the gameplay and discovery branches so a discovery-dismissing movement key can
    // transition straight to completion without an intermediate gameplay block.
    const auto present_move_result = [&](ObjectiveTransition transition) {
        if (transition == ObjectiveTransition::expedition_completed) {
            enter_completion();
            output << renderer.render_completion_plain(completion_summary_);
        } else if (transition == ObjectiveTransition::beacon_discovered) {
            presentation_ = Presentation::beacon_discovery;
            output << renderer.render_discovery_plain(state_.objective().name);
        } else {
            presentation_ = Presentation::gameplay;
            output << renderer.render_plain(make_input(false));
        }
        output.flush();
    };

    // Initial objective completion (single reachable walkable cell): start directly
    // on the completion screen and wait for an explicit acknowledgement or end of
    // input (REQ-027).
    if (state_.objective_completed()) {
        enter_completion();
        output << renderer.render_completion_plain(completion_summary_);
    } else {
        hud_.set_message(initial_message(settings_, /*interactive=*/false));
        presentation_ = Presentation::gameplay;
        output << renderer.render_plain(make_input(false));
    }
    output.flush();

    std::string line;
    while (std::getline(input, line)) {
        const std::string command = normalize_command(line);
        const PlainCommand parsed = parse_plain_command(command);
        bool emphasize = false;

        switch (presentation_) {
            case Presentation::gameplay:
                switch (parsed) {
                    case PlainCommand::quit:
                        hud_.set_message("Goodbye.");
                        output << renderer.render_plain(make_input(false));
                        output.flush();
                        return 0;
                    case PlainCommand::none:
                        hud_.set_message("Position held. Enter a move or q to quit.");
                        output << renderer.render_plain(make_input(false));
                        output.flush();
                        break;
                    case PlainCommand::rest:
                        apply_rest(emphasize);
                        output << renderer.render_plain(make_input(false));
                        output.flush();
                        break;
                    case PlainCommand::up:
                        present_move_result(apply_move(Direction::up, emphasize));
                        break;
                    case PlainCommand::down:
                        present_move_result(apply_move(Direction::down, emphasize));
                        break;
                    case PlainCommand::left:
                        present_move_result(apply_move(Direction::left, emphasize));
                        break;
                    case PlainCommand::right:
                        present_move_result(apply_move(Direction::right, emphasize));
                        break;
                    case PlainCommand::unknown:
                        hud_.set_message("Unknown command '" + command +
                                         "'. Try w/a/s/d, up/down/left/right, r to rest, or q.");
                        output << renderer.render_plain(make_input(false));
                        output.flush();
                        break;
                }
                break;

            case Presentation::beacon_discovery:
                switch (parsed) {
                    case PlainCommand::quit:
                        // Quit normally from the discovery screen (REQ-020).
                        hud_.set_message("Goodbye.");
                        presentation_ = Presentation::gameplay;
                        output << renderer.render_plain(make_input(false));
                        output.flush();
                        return 0;
                    case PlainCommand::none:
                        // An empty line dismisses the discovery screen to gameplay
                        // without emitting an event (REQ-020).
                        presentation_ = Presentation::gameplay;
                        output << renderer.render_plain(make_input(false));
                        output.flush();
                        break;
                    case PlainCommand::up:
                        present_move_result(apply_move(Direction::up, emphasize));
                        break;
                    case PlainCommand::down:
                        present_move_result(apply_move(Direction::down, emphasize));
                        break;
                    case PlainCommand::left:
                        present_move_result(apply_move(Direction::left, emphasize));
                        break;
                    case PlainCommand::right:
                        present_move_result(apply_move(Direction::right, emphasize));
                        break;
                    case PlainCommand::rest:
                    case PlainCommand::unknown:
                        // Rest and unknown commands keep the discovery screen active
                        // and print the reminder, emitting no event (REQ-020).
                        output << discovery_reminder() << "\n";
                        output.flush();
                        break;
                }
                break;

            case Presentation::expedition_complete:
                switch (parsed) {
                    case PlainCommand::none:
                    case PlainCommand::quit:
                        // An empty line, q, quit, or exit acknowledges completion and
                        // exits 0 with no goodbye block (REQ-026 / REQ-028).
                        return 0;
                    case PlainCommand::rest:
                    case PlainCommand::up:
                    case PlainCommand::down:
                    case PlainCommand::left:
                    case PlainCommand::right:
                    case PlainCommand::unknown:
                        // Every other command prints the reminder and stays on the
                        // completion screen without changing game or HUD counters
                        // (REQ-026 / RISK-004): move and rest are never called here.
                        output << completion_reminder() << "\n";
                        output.flush();
                        break;
                }
                break;
        }
    }

    // End of input. A completed run treats it as a valid acknowledgement and exits
    // 0 without a goodbye block (REQ-028 / ASSUMPTION-004); any other state keeps
    // the existing plain end-of-input goodbye.
    if (presentation_ == Presentation::expedition_complete) {
        return 0;
    }
    hud_.set_message("End of input. Goodbye.");
    output << renderer.render_plain(make_input(false));
    output.flush();
    return 0;
}

int run(GameState state, Settings settings, const Environment& environment) {
    const bool want_interactive =
        !settings.plain && interactive_display_supported(environment.term_supports_ansi());

    ConsoleApp app(std::move(state), std::move(settings));

    if (!want_interactive) {
        return app.run_plain(std::cin, std::cout);
    }

    TerminalStartup startup = TerminalSession::create();
    if (!startup.session) {
        std::cerr << "nam_console: cannot start interactive mode: " << describe(startup.error)
                  << "\n";
        return 2;
    }

    int code = 0;
    {
        // The session lives only for the game; its destructor restores the
        // terminal (modes, colour, cursor, and any alternate screen) before we
        // print the final line on the normal screen.
        TerminalSession session = std::move(*startup.session);
        code = app.run_interactive(session);
    }
    std::cout << app.final_message() << "\n";
    return code;
}

}  // namespace nam::console
