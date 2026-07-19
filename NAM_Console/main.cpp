#include <exception>
#include <iostream>
#include <optional>
#include <utility>
#include <variant>

#include "console_app.h"
#include "game_state.h"
#include "map.h"
#include "map_parser.h"
#include "messages.h"
#include "settings.h"

// Process exit codes (see the plan's error contract):
//   0  normal exit
//   1  map/data error
//   2  usage, configuration, or terminal-initialization error
int main(int argc, char** argv) {
    using namespace nam::console;

    const Environment environment = Environment::from_process();
    const CliResult cli = parse_cli(argc, argv, environment);

    switch (cli.action) {
        case CliAction::error:
            std::cerr << "nam_console: " << cli.message << "\n\n" << usage_text();
            return cli.exit_code;
        case CliAction::show_help:
            std::cout << usage_text();
            return cli.exit_code;
        case CliAction::show_version:
            std::cout << version_text();
            return cli.exit_code;
        case CliAction::run:
            break;
    }

    // Load and validate the map up front. This needs no terminal, so map errors
    // are reported cleanly whether or not stdout is a TTY.
    std::optional<Map> map;
    if (cli.settings.map_path) {
        MapLoadResult result = load_map_file(*cli.settings.map_path);
        if (const MapLoadError* error = std::get_if<MapLoadError>(&result)) {
            std::cerr << "nam_console: " << describe_map_error(*error) << "\n";
            return 1;
        }
        map.emplace(std::get<Map>(std::move(result)));
    } else {
        try {
            map.emplace(builtin_map());
        } catch (const std::exception& ex) {
            std::cerr << "nam_console: built-in map failed to load: " << ex.what() << "\n";
            return 1;
        }
    }

    GameState state(std::move(*map));
    return run(std::move(state), cli.settings, environment);
}
