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

enum class PlainCommand { none, quit, up, down, left, right, unknown };

[[nodiscard]] PlainCommand parse_plain_command(const std::string& normalized) {
    if (normalized.empty()) return PlainCommand::none;
    if (normalized == "q" || normalized == "quit" || normalized == "exit") return PlainCommand::quit;
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
                              ? "Welcome to NAM. Arrow keys or WASD to move, q or Esc to quit."
                              : "Plain mode. Commands: w/a/s/d or up/down/left/right, q to quit.";
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
    return input;
}

void ConsoleApp::apply_move(Direction direction, bool& emphasize) {
    const GameEvent event = state_.move(direction);
    const MoveAttemptedEvent& payload = std::get<MoveAttemptedEvent>(event.data);
    hud_.record_event(event);
    emphasize = payload.outcome.result == MoveResult::moved;
}

int ConsoleApp::run_interactive(TerminalSession& session) {
    RenderConfig config;
    config.use_ansi = session.supports_ansi();
    config.use_color = settings_.use_color && config.use_ansi;
    config.debug = settings_.debug;
    config.emphasis = settings_.animation && config.use_ansi;
    const Renderer renderer(config);

    hud_.set_message(initial_message(settings_, /*interactive=*/true));

    bool running = true;
    session.draw(renderer.render(make_input(false), session.size()));

    while (running) {
        const KeyEvent event = session.read_event();
        bool emphasize = false;
        bool dirty = false;

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
                dirty = true;  // Terminal geometry changed: rebuild the frame.
                break;
            default:
                if (is_quit_event(event)) {
                    hud_.set_message("Goodbye.");
                    running = false;
                    break;
                }
                if (const std::optional<Direction> direction = direction_for(event)) {
                    apply_move(*direction, emphasize);
                    dirty = true;  // Position and/or HUD message changed.
                }
                // Recognized-but-unmapped events (Home/End/Enter/unknown) change
                // nothing and are intentionally ignored: no movement, no redraw.
                break;
        }

        // One state update yields at most one draw; ignored keys draw nothing,
        // and the loop otherwise blocks in read_event, so idle CPU is ~zero.
        if (running && dirty) {
            session.draw(renderer.render(make_input(emphasize), session.size()));
        }
    }

    return 0;
}

int ConsoleApp::run_plain(std::istream& input, std::ostream& output) {
    const RenderConfig config{/*use_color=*/false, /*use_ansi=*/false, settings_.debug,
                              /*emphasis=*/false};
    const Renderer renderer(config);

    hud_.set_message(initial_message(settings_, /*interactive=*/false));
    output << renderer.render_plain(make_input(false));
    output.flush();

    std::string line;
    while (std::getline(input, line)) {
        const std::string command = normalize_command(line);
        bool emphasize = false;
        bool quit = false;

        switch (parse_plain_command(command)) {
            case PlainCommand::quit:
                hud_.set_message("Goodbye.");
                quit = true;
                break;
            case PlainCommand::none:
                hud_.set_message("Position held. Enter a move or q to quit.");
                break;
            case PlainCommand::up:    apply_move(Direction::up, emphasize); break;
            case PlainCommand::down:  apply_move(Direction::down, emphasize); break;
            case PlainCommand::left:  apply_move(Direction::left, emphasize); break;
            case PlainCommand::right: apply_move(Direction::right, emphasize); break;
            case PlainCommand::unknown:
                hud_.set_message("Unknown command '" + command +
                                 "'. Try w/a/s/d, up/down/left/right, or q.");
                break;
        }

        output << renderer.render_plain(make_input(false));
        output.flush();
        if (quit) {
            return 0;
        }
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
