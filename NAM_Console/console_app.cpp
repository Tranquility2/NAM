#include "console_app.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
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

enum class PlainCommand { none, quit, journal, up, down, left, right, unknown };

[[nodiscard]] PlainCommand parse_plain_command(const std::string& normalized) {
    if (normalized.empty()) return PlainCommand::none;
    if (normalized == "q" || normalized == "quit" || normalized == "exit") return PlainCommand::quit;
    if (normalized == "j" || normalized == "journal") return PlainCommand::journal;
    if (normalized == "w" || normalized == "k" || normalized == "up") return PlainCommand::up;
    if (normalized == "s" || normalized == "down") return PlainCommand::down;
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
                              ? "Welcome to NAM. Arrow keys or WASD to move, j for journal, "
                                "q or Esc to quit."
                              : "Plain mode. Commands: w/a/s/d or up/down/left/right, "
                                "j for journal, q to quit.";
    if (settings.seed_text) {
        message += " Expedition seed: ";
        message += format_seed_for_display(*settings.seed_text);
    } else if (settings.numeric_seed) {
        // A numeric seed has no original text form, so present its exact decimal
        // identity. std::to_string is ASCII-only and locale-independent.
        message += " Expedition seed number: ";
        message += std::to_string(*settings.numeric_seed);
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
                case 's': return Direction::down;
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

bool is_journal_event(const KeyEvent& event) noexcept {
    return event.key == Key::character && lower(event.character) == 'j';
}

ConsoleApp::ConsoleApp(Expedition expedition, Settings settings)
    : expedition_(std::move(expedition)),
      settings_(std::move(settings)),
      route_history_(state().map().spawn()) {}

ConsoleApp::ConsoleApp(GameState state, Settings settings)
    : ConsoleApp(Expedition(std::move(state)), std::move(settings)) {}

RenderInput ConsoleApp::make_input(bool emphasize) const {
    RenderInput input;
    input.map = &state().map();
    input.visibility = &state().visibility();
    input.actor = state().actor_position();
    input.terrain = state().actor_terrain();
    input.move_count = hud_.move_count();
    input.attempt_count = hud_.attempt_count();
    input.message = hud_.message();
    input.recent.assign(hud_.recent().begin(), hud_.recent().end());
    input.emphasize_actor = emphasize;
    // Production input always presents the core-owned objective; the renderer
    // gates the exit overlay on visibility and shows the objective line.
    input.objective = &state().objective();
    return input;
}

JournalContext ConsoleApp::journal_context() const {
    JournalContext context;
    context.landmark_name = state().objective().name;
    context.tier = expedition_.current_tier();
    context.level_number = expedition_.completed_levels() + 1u;
    context.total_levels = expedition_.total_levels();
    context.discoveries_found = state().discoveries_found();
    context.discovery_total = state().discovery_total();
    return context;
}

ObjectiveTransition ConsoleApp::apply_move(Direction direction, bool& emphasize) {
    const GameEvent event = state().move(direction);
    const MoveAttemptedEvent& payload = std::get<MoveAttemptedEvent>(event.data);
    hud_.record_event(event);
    journal_.record_event(event, journal_context());
    route_history_.record_event(event);
    emphasize = payload.outcome.result == MoveResult::moved;
    // A first discovery replaces the ordinary move wording; an objective transition
    // outranks it below, because reaching the landmark or the exit is the more
    // important thing that happened on that command.
    if (payload.wide_reveal_granted) {
        hud_.set_message(describe_vantage_reached());
    }
    if (payload.discovery_recorded) {
        hud_.set_message(
            describe_discovery_found(state().discoveries_found(), state().discovery_total()));
    }
    // Replace the ordinary move wording only for the typed objective transitions;
    // every other successful or blocked move keeps its normal message.
    switch (payload.objective_update.transition) {
        case ObjectiveTransition::landmark_discovered:
            hud_.set_message(describe_landmark_discovered(state().objective().name));
            break;
        case ObjectiveTransition::level_completed:
            hud_.set_message(describe_level_completed(state().objective().name));
            break;
        case ObjectiveTransition::none:
            break;
    }
    return payload.objective_update.transition;
}

void ConsoleApp::enter_completion() {
    presentation_ = Presentation::level_complete;
    final_report_active_ = true;

    // Score the level into the expedition first: the report presents the carried
    // totals, and on a multi-level run this also generates the next level.
    const LevelPerformance performance{hud_.move_count()};

    // Snapshot everything the report needs before complete_level replaces the
    // level, because on an advance `state()` becomes the *next* level.
    const LevelObjective objective = state().objective();
    const Map map = state().map();
    const VisibilityMap visibility = state().visibility();

    static_cast<void>(expedition_.complete_level(performance));

    ExpeditionCarryover carryover;
    carryover.levels_completed = expedition_.completed_levels();
    carryover.total_levels = expedition_.total_levels();
    carryover.expedition_completed = expedition_.completed();
    carryover.expedition_score = expedition_.total_score();
    carryover.expedition_discoveries_found = expedition_.total_discoveries_found();
    carryover.expedition_discoveries_available = expedition_.total_discoveries_available();
    if (!expedition_.summaries().empty()) {
        const LevelSummary& summary = expedition_.summaries().back();
        carryover.discoveries_found = summary.discoveries_found;
        carryover.discovery_total = summary.discovery_total;
        carryover.applied_bonus = summary.applied_bonus;
        carryover.earned_bonus = summary.earned_bonus;
    }
    if (!expedition_.completed()) {
        carryover.next_tier = expedition_.current_tier();
    }

    // Build the report only after the completing event has updated the HUD,
    // journal, route history, objective state, and visibility.
    report_.emplace(build_expedition_report(
        objective, map, visibility, journal_, route_history_, world_identity_from(settings_),
        static_cast<std::uint64_t>(hud_.move_count()),
        static_cast<std::uint64_t>(hud_.attempt_count()), carryover));
    report_viewport_ = ReportViewport{};  // Open at (0, 0) (REQ-150).
    restored_message_ = restored_completion_message(objective.name);
}

void ConsoleApp::begin_next_level() {
    // Per-level tracking restarts with the level. The journal is expedition-wide
    // and survives, which is what makes it a record of the whole run.
    hud_ = Hud{};
    route_history_ = RouteHistory(state().map().spawn());
    report_.reset();
    final_report_active_ = false;
    presentation_ = Presentation::gameplay;
    hud_.set_message(describe_level_started(expedition_.current_tier(),
                                            expedition_.completed_levels() + 1u,
                                            expedition_.total_levels(),
                                            expedition_.active_bonus()));
}

void ConsoleApp::open_journal(int capacity) {
    previous_presentation_ = presentation_;
    presentation_ = Presentation::journal;
    const int total = static_cast<int>(journal_.size());
    // Open on the newest page while keeping chronological top-to-bottom order.
    journal_scroll_ = std::max(0, total - capacity);
}

void ConsoleApp::dismiss_journal() {
    presentation_ = previous_presentation_;
}

void ConsoleApp::scroll_journal(int delta, int capacity) {
    const int total = static_cast<int>(journal_.size());
    const int max_top = std::max(0, total - capacity);
    int next = journal_scroll_ + delta;
    next = std::max(0, std::min(next, max_top));
    journal_scroll_ = next;
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

    // Reclamp the report viewport for the current size and draw the report. Used by
    // completion entry, scrolling, and resize so the visible window never leaves
    // range (GUD-004).
    const auto draw_report = [&] {
        report_viewport_ = renderer.clamp_report_viewport(*report_, report_viewport_,
                                                          session.size());
        session.draw(renderer.render_report(*report_, report_viewport_, session.size()));
    };

    // Apply a viewport delta and redraw the clamped report.
    const auto scroll_report = [&](int vertical_delta, int horizontal_delta) {
        report_viewport_.vertical += vertical_delta;
        report_viewport_.horizontal += horizontal_delta;
        draw_report();
    };

    // Draw the single resulting frame after a movement command has been applied and
    // choose the next presentation state from its objective transition, so a
    // discovery-dismissing movement key can transition straight to completion
    // without an intermediate gameplay frame (REQ-025 / REQ-120).
    const auto present_move_result = [&](ObjectiveTransition transition, bool emphasize) {
        if (transition == ObjectiveTransition::level_completed) {
            enter_completion();
            draw_report();
        } else if (transition == ObjectiveTransition::landmark_discovered) {
            presentation_ = Presentation::landmark_discovery;
            session.draw(renderer.render_discovery(state().objective().name, session.size()));
        } else {
            presentation_ = Presentation::gameplay;
            session.draw(renderer.render(make_input(emphasize), session.size()));
        }
    };

    // Draw the frame for the current non-journal presentation. Used to restore the
    // exact underlying screen when the journal is dismissed, and shared by the
    // journal-open flow (REQ-022).
    const auto draw_underlying = [&] {
        switch (presentation_) {
            case Presentation::gameplay:
                session.draw(renderer.render(make_input(false), session.size()));
                break;
            case Presentation::landmark_discovery:
                session.draw(renderer.render_discovery(state().objective().name, session.size()));
                break;
            case Presentation::level_complete:
                draw_report();
                break;
            case Presentation::journal:
                break;  // The journal is never its own underlying screen.
        }
    };

    // Open the journal over the current presentation and draw its newest page.
    const auto open_journal_screen = [&] {
        open_journal(renderer.journal_page_capacity(session.size()));
        session.draw(renderer.render_journal(journal_, journal_scroll_, session.size()));
    };

    // Reclamp the scroll for the current size and redraw the journal. Used by
    // scrolling and by resize so the visible window never leaves range.
    const auto scroll_and_draw = [&](int delta) {
        scroll_journal(delta, renderer.journal_page_capacity(session.size()));
        session.draw(renderer.render_journal(journal_, journal_scroll_, session.size()));
    };

    // Initial objective completion (single reachable walkable cell): start directly
    // on the final report and wait for an explicit acknowledgement (REQ-002).
    if (state().objective_completed()) {
        journal_.record_initial_completion(state().objective().name);
        enter_completion();
        draw_report();
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
                        if (is_journal_event(event)) {
                            open_journal_screen();
                            break;
                        }
                        if (const std::optional<Direction> direction = direction_for(event)) {
                            present_move_result(apply_move(*direction, emphasize), emphasize);
                        }
                        // Recognized-but-unmapped events change nothing and draw
                        // nothing: no movement, no redraw.
                        break;
                }
                break;

            case Presentation::landmark_discovery:
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
                            renderer.render_discovery(state().objective().name, session.size()));
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
                        if (is_journal_event(event)) {
                            // Opening the journal never dismisses discovery; it is
                            // restored intact when the journal closes (REQ-022).
                            open_journal_screen();
                            break;
                        }
                        if (const std::optional<Direction> direction = direction_for(event)) {
                            // A movement key dismisses discovery and executes that
                            // same movement exactly once (REQ-018 / RISK-003).
                            present_move_result(apply_move(*direction, emphasize), emphasize);
                            break;
                        }
                        // Every other key leaves the discovery screen active and emits
                        // no event (REQ-018): no core update, no redraw.
                        break;
                }
                break;

            case Presentation::level_complete:
                switch (event.key) {
                    case Key::end_of_input:
                    case Key::interrupt:
                        // End of input or interrupt acknowledges the report and exits
                        // 0 without a goodbye line (REQ-007).
                        running = false;
                        break;
                    case Key::resize:
                        // Reclamp both offsets for the new size and redraw the same
                        // report without changing game/journal/score/route state
                        // (REQ-008).
                        draw_report();
                        break;
                    case Key::up:
                        scroll_report(-1, 0);  // One logical line up (REQ-004).
                        break;
                    case Key::down:
                        scroll_report(1, 0);  // One logical line down (REQ-004).
                        break;
                    case Key::page_up:
                        scroll_report(-renderer.report_page_capacity(session.size()), 0);  // REQ-005.
                        break;
                    case Key::page_down:
                        scroll_report(renderer.report_page_capacity(session.size()), 0);  // REQ-005.
                        break;
                    case Key::left:
                        scroll_report(0, -1);  // One column left (REQ-006).
                        break;
                    case Key::right:
                        scroll_report(0, 1);  // One column right (REQ-006).
                        break;
                    case Key::enter:
                        // Acknowledging an interlude starts the next level; on the
                        // last level it acknowledges the run and exits 0 (REQ-007).
                        if (interlude_active()) {
                            begin_next_level();
                            session.draw(renderer.render(make_input(false), session.size()));
                            break;
                        }
                        running = false;
                        break;
                    default:
                        if (is_quit_event(event)) {
                            // q or Escape always stops the run, mid-expedition or not
                            // (REQ-007).
                            running = false;
                            break;
                        }
                        // Every movement semantic and every other key leaves the report
                        // open, emits no core event, and never calls move
                        // (REQ-008 / RISK-004): no state change, no draw.
                        break;
                }
                break;

            case Presentation::journal:
                switch (event.key) {
                    case Key::end_of_input:
                        // Restore the previous state first so final_message observes
                        // completion when appropriate (REQ-038); a journal opened over
                        // gameplay or discovery keeps the existing EOF goodbye.
                        dismiss_journal();
                        if (previous_presentation_ != Presentation::level_complete) {
                            hud_.set_message("End of input. Goodbye.");
                        }
                        running = false;
                        break;
                    case Key::interrupt:
                        dismiss_journal();
                        if (previous_presentation_ != Presentation::level_complete) {
                            hud_.set_message("Interrupted. Goodbye.");
                        }
                        running = false;
                        break;
                    case Key::resize:
                        scroll_and_draw(0);  // Reclamp for the new size and redraw.
                        break;
                    case Key::up:
                        scroll_and_draw(-1);
                        break;
                    case Key::down:
                        scroll_and_draw(1);
                        break;
                    case Key::page_up:
                        scroll_and_draw(-renderer.journal_page_capacity(session.size()));
                        break;
                    case Key::page_down:
                        scroll_and_draw(renderer.journal_page_capacity(session.size()));
                        break;
                    case Key::enter:
                        // Enter returns to the exact previous state (REQ-022/REQ-023).
                        dismiss_journal();
                        draw_underlying();
                        break;
                    default:
                        // Escape and j dismiss the journal; this must run before any
                        // general quit predicate so Escape returns instead of quitting
                        // (REQ-023 / TASK-014).
                        if (event.key == Key::escape || is_journal_event(event)) {
                            dismiss_journal();
                            draw_underlying();
                            break;
                        }
                        if (event.key == Key::character && lower(event.character) == 'q') {
                            // q quits the game. Restore the previous presentation first
                            // so completion keeps its final message (REQ-038); other
                            // previous states keep the goodbye wording.
                            dismiss_journal();
                            if (previous_presentation_ != Presentation::level_complete) {
                                hud_.set_message("Goodbye.");
                            }
                            running = false;
                            break;
                        }
                        // Every other key leaves the journal open, emits no event, and
                        // mutates no game/HUD/journal state (REQ-024 / REQ-033).
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
    // choose the next presentation state from its objective transition. Returns true
    // when the command completed the level: the caller then returns immediately so
    // plain mode writes the full report exactly once and never reads later commands
    // (REQ-035 / REQ-151).
    const auto present_move_result = [&](ObjectiveTransition transition) -> bool {
        if (transition == ObjectiveTransition::level_completed) {
            enter_completion();
            output << renderer.render_report_plain(*report_);
            output.flush();
            // A finished level of a longer expedition is an interlude: print its
            // report, then stream straight into the next level rather than ending
            // the run.
            if (interlude_active()) {
                begin_next_level();
                output << renderer.render_plain(make_input(false));
                output.flush();
                return false;
            }
            return true;
        }
        if (transition == ObjectiveTransition::landmark_discovered) {
            presentation_ = Presentation::landmark_discovery;
            output << renderer.render_discovery_plain(state().objective().name);
        } else {
            presentation_ = Presentation::gameplay;
            output << renderer.render_plain(make_input(false));
        }
        output.flush();
        return false;
    };

    // Initial objective completion (single reachable walkable cell): write the
    // complete report once, flush, and return 0 immediately without reading stdin
    // (REQ-002 / REQ-009).
    if (state().objective_completed()) {
        journal_.record_initial_completion(state().objective().name);
        enter_completion();
        output << renderer.render_report_plain(*report_);
        output.flush();
        return 0;
    }

    hud_.set_message(initial_message(settings_, /*interactive=*/false));
    presentation_ = Presentation::gameplay;
    output << renderer.render_plain(make_input(false));
    output.flush();

    std::string line;
    while (std::getline(input, line)) {
        const std::string command = normalize_command(line);
        const PlainCommand parsed = parse_plain_command(command);
        bool emphasize = false;

        // A journal command prints the complete journal block once and immediately
        // resumes in the same presentation state: it never dismisses discovery,
        // emits an event, or prints an extra frame.
        if (parsed == PlainCommand::journal) {
            output << renderer.render_journal_plain(journal_);
            output.flush();
            continue;
        }

        switch (presentation_) {
            case Presentation::gameplay:
                switch (parsed) {
                    case PlainCommand::journal:
                        break;  // Handled before the switch; unreachable here.
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
                    case PlainCommand::up:
                        if (present_move_result(apply_move(Direction::up, emphasize))) return 0;
                        break;
                    case PlainCommand::down:
                        if (present_move_result(apply_move(Direction::down, emphasize))) return 0;
                        break;
                    case PlainCommand::left:
                        if (present_move_result(apply_move(Direction::left, emphasize))) return 0;
                        break;
                    case PlainCommand::right:
                        if (present_move_result(apply_move(Direction::right, emphasize))) return 0;
                        break;
                    case PlainCommand::unknown:
                        hud_.set_message("Unknown command '" + command +
                                         "'. Try w/a/s/d, up/down/left/right, j for journal, "
                                         "or q.");
                        output << renderer.render_plain(make_input(false));
                        output.flush();
                        break;
                }
                break;

            case Presentation::landmark_discovery:
                switch (parsed) {
                    case PlainCommand::journal:
                        break;  // Handled before the switch; unreachable here.
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
                        if (present_move_result(apply_move(Direction::up, emphasize))) return 0;
                        break;
                    case PlainCommand::down:
                        if (present_move_result(apply_move(Direction::down, emphasize))) return 0;
                        break;
                    case PlainCommand::left:
                        if (present_move_result(apply_move(Direction::left, emphasize))) return 0;
                        break;
                    case PlainCommand::right:
                        if (present_move_result(apply_move(Direction::right, emphasize))) return 0;
                        break;
                    case PlainCommand::unknown:
                        // An unknown command keeps the discovery screen active and
                        // prints the reminder, emitting no event (REQ-020 / REQ-031).
                        output << discovery_reminder() << "\n";
                        output.flush();
                        break;
                }
                break;

            case Presentation::level_complete:
                // Unreachable: completion returns 0 immediately above. The defensive
                // return keeps the switch total and guarantees no post-ending command
                // processing (REQ-035 / REQ-151).
                return 0;

            case Presentation::journal:
                break;  // Plain mode never enters the journal presentation state.
        }
    }

    // End of input in an unfinished run keeps the existing plain goodbye. A
    // completed run has already returned above, so it never reaches here.
    hud_.set_message("End of input. Goodbye.");
    output << renderer.render_plain(make_input(false));
    output.flush();
    return 0;
}

int run(GameState state, Settings settings, const Environment& environment) {
    return run(Expedition(std::move(state)), std::move(settings), environment);
}

int run(Expedition expedition, Settings settings, const Environment& environment) {
    const bool want_interactive =
        !settings.plain && interactive_display_supported(environment.term_supports_ansi());

    ConsoleApp app(std::move(expedition), std::move(settings));

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
