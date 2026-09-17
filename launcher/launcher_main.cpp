#include "bootstrap.h"
#include "jo_import.h"
#ifdef OPENJK_LAUNCHER_UI
#include "ui.h"
#endif

#include <iostream>
#include <set>
#include <stdexcept>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#ifdef OPENJK_LAUNCHER_UI
#include <shellapi.h>
#endif
#endif

namespace fs = std::filesystem;
using namespace bootstrap;

namespace {

int error(const std::string& detail) {
    std::cerr << "openjk-launcher: " << detail << '\n';
    return 1;
}

template<class T> T require(Result<T> result) {
    if (!result) throw std::runtime_error(result.error->detail);
    return std::move(result.value);
}

void status(Game game, const ValidationResult& result) {
    std::cout << (game == Game::academy ? "JA: " : "JO: ") << state_name(result.state);
    if (!result.data_root.empty()) std::cout << ' ' << inspect_argument(result.data_root.u8string());
    std::cout << " - " << inspect_argument(result.detail);
    if (!result.missing_files.empty()) {
        std::cout << " missing:";
        for (const auto& file : result.missing_files) std::cout << ' ' << inspect_argument(file);
    }
    std::cout << '\n';
}

int run(const std::vector<std::string>& args) {
#ifdef OPENJK_LAUNCHER_UI
    if (args.size() == 1) return launcher_ui::run();
#endif
    bool check = false, print = false, new_game = false, help = false;
    std::optional<fs::path> ja, jo, profile, engine;
    std::optional<Game> campaign;
    std::vector<std::string> extra;
    std::set<std::string> seen;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const auto& arg = args[i];
        if (arg == "--") { extra.assign(args.begin() + i + 1, args.end()); break; }
        if (!seen.insert(arg).second) return error("Duplicate option: " + arg);
        if (arg == "--headless-check") check = true;
        else if (arg == "--print-launch") print = true;
        else if (arg == "--new-game") new_game = true;
        else if (arg == "--help") help = true;
        else if (arg == "--ja-path" || arg == "--jo-path" || arg == "--profile" || arg == "--engine" || arg == "--campaign") {
            if (i + 1 == args.size() || args[i + 1].empty() || args[i + 1].compare(0, 2, "--") == 0)
                return error("Missing value for " + arg);
            const auto& value = args[++i];
            if (arg == "--campaign") {
                if (value != "ja" && value != "jo") return error("Use --campaign ja or --campaign jo.");
                campaign = value == "ja" ? Game::academy : Game::outcast;
            } else {
                auto path = fs::absolute(fs::u8path(value));
                if (arg == "--ja-path") ja = path;
                else if (arg == "--jo-path") jo = path;
                else if (arg == "--profile") profile = path;
                else engine = path;
            }
        } else return error("Unknown option: " + arg + ". Use --help for options.");
    }
    if (help) {
        std::cout << "Usage: openjk-launcher [options] [-- engine arguments]\n"
                     "  --headless-check   Check supplied game paths, or saved paths; do not launch\n"
                     "  --print-launch     Prepare the campaign and print one quoted argument per line\n"
                     "  --ja-path PATH     Select Jedi Academy data\n"
                     "  --jo-path PATH     Select Jedi Outcast data\n"
                     "  --profile PATH     Select the config and profile root\n"
                     "  --engine PATH      Select the engine executable\n"
                     "  --campaign ja|jo   Select a campaign\n"
                     "  --new-game         Start at the first map\n"
                     "  --help             Show this help\n";
        return 0;
    }
    if (check && print) return error("Use either --headless-check or --print-launch.");
    auto root = profile ? *profile : fs::absolute(require(default_profile_root()));
    auto config_path = root / "bootstrap.ini";
    auto config = require(load_config(config_path));
    const bool supplied = ja.has_value() || jo.has_value();
    const bool check_ja = ja.has_value() || (!supplied && config.ja_path.has_value());
    const bool check_jo = jo.has_value() || (!supplied && config.jo_path.has_value());
    if (!ja) ja = config.ja_path;
    if (!jo) jo = config.jo_path;
    Game game = campaign.value_or(config.last_campaign);
    if (check) {
        if (!check_ja && !check_jo) return error("No game paths are saved. Supply --ja-path PATH or --jo-path PATH.");
        bool ready = true;
        if (check_ja) { auto result = validate(Game::academy, *ja); status(Game::academy, result); ready &= bool(result); }
        if (check_jo) { auto result = validate(Game::outcast, *jo); status(Game::outcast, result); ready &= bool(result); }
        return ready ? 0 : 1;
    }
    if (!ja) return error("Jedi Academy data is required for both campaigns. Use --ja-path PATH.");
    auto academy = validate(Game::academy, *ja);
    if (!academy) { status(Game::academy, academy); return 1; }
    std::optional<ValidationResult> outcast;
    if (jo) outcast = validate(Game::outcast, *jo);
    if (game == Game::outcast && !jo) return error("Jedi Outcast data is required. Use --jo-path PATH.");
    if (outcast && !*outcast && (game == Game::outcast || seen.count("--jo-path"))) {
        status(Game::outcast, *outcast);
        return 1;
    }
    const auto academy_output = campaign_profile(root, Game::academy) / "OpenJK";
    if (require(path_is_within(root, academy.data_root)) ||
        require(path_is_within(academy_output, academy.data_root)) ||
        (outcast && *outcast && (require(path_is_within(root, outcast->data_root)) ||
            require(path_is_within(academy_output, outcast->data_root)))))
        return error("Select a profile folder outside both game-data folders.");
    if (game == Game::outcast) {
        const auto outcast_output = campaign_profile(root, Game::outcast) / "OpenJK";
        if (require(path_is_within(outcast_output, academy.data_root)) ||
            require(path_is_within(outcast_output, outcast->data_root)))
            return error("Select a profile folder outside both game-data folders.");
        if (require(path_is_within(outcast_output, academy_output)) ||
            require(path_is_within(academy_output, outcast_output)))
            return error("Jedi Academy and Jedi Outcast need separate profile folders.");
    }
    auto executable = require(executable_path());
    auto selected_engine = engine ? *engine : require(default_engine(executable));
    if (!fs::is_regular_file(selected_engine)) return error("Engine not found at " + inspect_argument(selected_engine.u8string()) + ". Use --engine PATH.");
    auto argv = require(launch_arguments(selected_engine, package_root(executable), academy.data_root, root, game, new_game, extra));
    fs::create_directories(campaign_profile(root, game));
    config.ja_path = academy.data_root;
    config.jo_path = outcast && *outcast ? std::optional<fs::path>(outcast->data_root) : std::nullopt;
    config.last_campaign = game;
    require(save_config(config_path, config));
    if (game == Game::outcast) {
        auto imported = jo_import::import_campaign(academy.data_root, outcast->data_root, campaign_profile(root, game));
        if (!imported) return error(imported.error->message);
        if (!imported.warning.empty()) std::cerr << "openjk-launcher: " << imported.warning << '\n';
    }
    if (print) {
        for (const auto& arg : argv) std::cout << inspect_argument(arg) << '\n';
        if (!std::cout) return error("Cannot write launch arguments.");
    } else require(spawn(argv));
    return 0;
}

template<class Character> int entry(int argc, Character** argv) {
    try {
        std::vector<std::string> args;
        for (int i = 0; i < argc; ++i) args.push_back(fs::path(argv[i]).u8string());
        return run(args);
    } catch (const std::exception& e) { return error(e.what()); }
    catch (...) { return error("Unexpected failure. Check the selected paths and retry."); }
}

} // namespace

#ifdef _WIN32
#ifdef OPENJK_LAUNCHER_UI
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return error("Cannot read the Windows command line.");
    const int result = entry(argc, argv);
    LocalFree(argv);
    return result;
}
#else
int wmain(int argc, wchar_t** argv) { return entry(argc, argv); }
#endif
#else
int main(int argc, char** argv) { return entry(argc, argv); }
#endif
