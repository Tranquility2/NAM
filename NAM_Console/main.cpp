#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <utility>
#include <variant>

#include "console_app.h"
#include "expedition.h"
#include "game_state.h"
#include "map.h"
#include "map_parser.h"
#include "messages.h"
#include "settings.h"
#include "world_generation.h"

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

    Settings settings = cli.settings;

    // A seed selects the procedural expedition: the deterministic Small -> Medium
    // chain the prototype ships. Both seed forms resolve to one numeric identity
    // first, so they share a replay identity, and a generation failure mirrors the
    // map/data error contract (concise diagnostic on stderr, exit 1). A text seed
    // is displayed only through the escaping helper so raw control bytes never
    // reach the terminal.
    if (settings.seed_text || settings.numeric_seed) {
        const std::uint64_t numeric_seed = settings.seed_text
                                               ? hash_seed_text(*settings.seed_text)
                                               : *settings.numeric_seed;
        settings.numeric_seed = numeric_seed;
        std::optional<Expedition> expedition;
        try {
            expedition.emplace(numeric_seed);
        } catch (const std::exception& ex) {
            std::cerr << "nam_console: could not generate an expedition for seed ";
            if (settings.seed_text) {
                std::cerr << format_seed_for_display(*settings.seed_text);
            } else {
                std::cerr << "number " << numeric_seed;
            }
            std::cerr << ": " << ex.what() << "\n";
            return 1;
        }
        return run(std::move(*expedition), settings, environment);
    }

    // Load and validate the map up front. This needs no terminal, so map errors
    // are reported cleanly whether or not stdout is a TTY.
    std::optional<Map> map;
    if (settings.map_path) {
        MapLoadResult result = load_map_file(*settings.map_path);
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
    return run(std::move(state), settings, environment);
}
