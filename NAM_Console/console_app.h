#pragma once

#include <istream>
#include <optional>
#include <ostream>
#include <string>

#include "app_state.h"
#include "direction.h"
#include "expedition_report.h"
#include "game_state.h"
#include "input.h"
#include "journal.h"
#include "renderer.h"
#include "settings.h"
#include "terminal.h"

namespace nam::console {

// The console's presentation state, kept deliberately independent of the core
// ObjectiveStatus (GUD-003). `gameplay` shows the map/HUD frame; `beacon_discovery`
// is a temporary acknowledgement screen shown after a move first reaches the
// beacon and is dismissed by the next input; `expedition_complete` is the
// scrollable final report shown after the return to spawn or a rescue and stays
// active until the player acknowledges it; `expedition_rescue` is the humorous
// rescue acknowledgement screen shown before the failed report and dismissed by
// Enter; `journal` is the bounded scrollable expedition-journal screen, opened
// over any of the other states and dismissed back to the exact state it was
// opened from.
enum class Presentation {
    gameplay,
    beacon_discovery,
    expedition_complete,
    expedition_rescue,
    expedition_overdue,
    journal,
};

// A minimal, mockable view of an interactive terminal session: exactly the four
// operations the interactive loop needs. The production TerminalSession is
// adapted onto this interface, and tests provide a fake implementation so the
// loop's final-frame count and input consumption can be verified without a real
// TTY.
class InteractiveSession {
public:
    virtual ~InteractiveSession() = default;

    [[nodiscard]] virtual bool supports_ansi() const = 0;
    [[nodiscard]] virtual TerminalSize size() const = 0;
    [[nodiscard]] virtual KeyEvent read_event() = 0;
    virtual void draw(const Frame& frame) = 0;
};

// Drives one play session. The app owns the mutable GameState and the bounded
// HUD, translates semantic input events into core moves, and asks a Renderer for
// frames. It knows nothing about byte transports or platform APIs, so the same
// object serves both the interactive and the plain-text paths.
class ConsoleApp {
public:
    ConsoleApp(GameState state, Settings settings);

    // Event-driven interactive loop over a raw terminal session. Blocks on input
    // (idle CPU is ~zero), redraws after each event and on resize, and returns a
    // process exit code. Terminal restoration is guaranteed by the session RAII.
    // This production entry point is a thin forwarding adapter onto the shared
    // loop that runs against the mockable InteractiveSession interface.
    [[nodiscard]] int run_interactive(TerminalSession& session);

    // The shared interactive loop, executed against the mockable session
    // interface so tests can drive it without a real terminal.
    [[nodiscard]] int run_interactive(InteractiveSession& session);

    // Line-oriented fallback for non-terminals or --plain. Reads whole commands,
    // never touches raw mode or the cursor, and stays readable when redirected.
    [[nodiscard]] int run_plain(std::istream& input, std::ostream& output);

    // The final line shown once after interactive teardown. A completed or rescued
    // run shows its fixed restored message (naming the finished or abandoned
    // beacon) so acknowledgement can never overwrite it; any other end (quit before
    // an ending, end of input, interrupt) keeps its HUD goodbye wording.
    [[nodiscard]] const std::string& final_message() const noexcept {
        return final_report_active_ ? restored_message_ : hud_.message();
    }

private:
    [[nodiscard]] RenderInput make_input(bool emphasize) const;
    // The result of applying one command: the objective transition it caused (none
    // for rest and camp) and the expedition-ending transition the core reported on
    // the event (none, rescued, or overdue).
    struct AppliedCommand {
        ObjectiveTransition objective_transition = ObjectiveTransition::none;
        ExpeditionEndingTransition ending = ExpeditionEndingTransition::none;
    };
    // Apply one movement command and return the transitions it caused so the
    // caller can choose the resulting presentation state.
    [[nodiscard]] AppliedCommand apply_move(Direction direction, bool& emphasize);
    // Apply one rest command and return the ending transition it caused.
    [[nodiscard]] ExpeditionEndingTransition apply_rest(bool& emphasize);
    // Apply one camp command and return the ending transition it caused.
    [[nodiscard]] ExpeditionEndingTransition apply_camp(bool& emphasize);

    // Enter the completion presentation: build the completed final expedition
    // report from the fully-updated game, journal, route history, and objective
    // state, reset the report viewport to (0, 0), and set the restored final
    // message. Emits no core event. Called only after the completing event (or the
    // initial single-cell completion) has been fully recorded.
    void enter_completion();

    // Enter the rescue presentation: record the structured rescue journal entry,
    // build the rescued final report, reset the report viewport, and set the
    // restored rescued final message. Emits no core event. Called after the
    // command whose event reported a rescue has been fully recorded. Leaves the
    // presentation on the rescue acknowledgement screen; the caller draws it.
    void enter_rescue();

    // Enter the overdue presentation: record the structured overdue journal entry,
    // build the overdue final report, reset the report viewport, and set the
    // restored overdue final message. Emits no core event. Called after the command
    // whose event reported an overdue ending has been fully recorded. Leaves the
    // presentation on the overdue acknowledgement screen; the caller draws it.
    void enter_overdue();

    // Open the journal over the current presentation. Remembers the state it was
    // opened from and positions the scroll on the newest page for the given entry
    // capacity (REQ-021 / REQ-025). Emits no core event and mutates no game/HUD
    // state.
    void open_journal(int capacity);
    // Dismiss the journal by restoring the presentation it was opened from
    // (REQ-022 / REQ-038). Emits no core event.
    void dismiss_journal();
    // Move the journal scroll by `delta` entry rows and clamp it to the valid
    // range for the given page capacity, so scrolling can never leave the entry
    // window (REQ-024). A `delta` of zero only reclamps, used on resize.
    void scroll_journal(int delta, int capacity);

    GameState state_;
    Settings settings_;
    Hud hud_;
    Journal journal_;
    // The ordered route (spawn plus every successful destination), recorded from
    // the same event stream as the HUD and journal (REQ-019 / TASK-010).
    RouteHistory route_history_;
    Presentation presentation_ = Presentation::gameplay;
    // The presentation the journal was opened from, restored on dismiss or exit.
    Presentation previous_presentation_ = Presentation::gameplay;
    // Index of the topmost visible journal entry while the journal is open.
    int journal_scroll_ = 0;
    // The completed or rescued final report, built once when the run ends. Absent
    // until an ending is reached.
    std::optional<ExpeditionReport> report_;
    // The scroll offsets into the final report while it is presented.
    ReportViewport report_viewport_;
    std::string restored_message_;
    // True once a completed or rescued ending has been reached, so final_message
    // shows the restored message instead of the HUD goodbye.
    bool final_report_active_ = false;
};

// Map a semantic event to a movement direction, or std::nullopt when the event
// is not a movement command. Exposed for direct testing.
[[nodiscard]] std::optional<Direction> direction_for(const KeyEvent& event) noexcept;

// Whether an event asks to quit (Escape, or 'q'). Exposed for testing.
[[nodiscard]] bool is_quit_event(const KeyEvent& event) noexcept;

// Whether an event asks to rest in place (lower- or upper-case 'r'). Rest is a
// distinct command family from movement, so it is mapped separately from
// direction_for. Exposed for direct testing.
[[nodiscard]] bool is_rest_event(const KeyEvent& event) noexcept;

// Whether an event asks to camp in place (lower- or upper-case 'c'). Camp is a
// distinct command family from movement and rest, so it is mapped separately.
// Exposed for direct testing.
[[nodiscard]] bool is_camp_event(const KeyEvent& event) noexcept;

// Whether an event asks to open or dismiss the expedition journal (lower- or
// upper-case 'j'). `j` is reserved for the journal and is no longer a movement
// alias. Exposed for direct testing.
[[nodiscard]] bool is_journal_event(const KeyEvent& event) noexcept;

// Top-level orchestration: choose interactive vs plain mode from settings and
// platform capability, create the session if needed, and run. Returns the
// process exit code (0 normal; 2 if interactive init fails with no fallback).
[[nodiscard]] int run(GameState state, Settings settings, const Environment& environment);

}  // namespace nam::console
