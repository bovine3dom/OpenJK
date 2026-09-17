#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace bootstrap {

enum class Game { academy, outcast };
enum class ValidationState { ready, not_found, incomplete, wrong_game, damaged_archive, unreadable };

struct ValidationResult {
    ValidationState state = ValidationState::not_found;
    std::filesystem::path data_root;
    std::vector<std::string> missing_files;
    std::string detail;
    explicit operator bool() const { return state == ValidationState::ready; }
};

enum class ErrorCode { invalid_config, io, platform, invalid_argument, spawn };
struct Error {
    ErrorCode code;
    std::string detail;
    std::filesystem::path path;
};

template<class T> struct Result {
    T value{};
    std::optional<Error> error;
    explicit operator bool() const { return !error; }
};

struct BootstrapConfig {
    std::optional<std::filesystem::path> ja_path;
    std::optional<std::filesystem::path> jo_path;
    Game last_campaign = Game::academy;
};

const char* state_name(ValidationState state);
ValidationResult validate(Game game, const std::filesystem::path& selected, bool readiness = true);
Result<BootstrapConfig> load_config(const std::filesystem::path& file);
Result<bool> save_config(const std::filesystem::path& file, const BootstrapConfig& config);
Result<std::filesystem::path> default_profile_root();
Result<std::filesystem::path> executable_path();
std::filesystem::path package_root(const std::filesystem::path& executable);
std::filesystem::path campaign_profile(const std::filesystem::path& root, Game game);
Result<bool> path_is_within(const std::filesystem::path& path, const std::filesystem::path& root);
Result<std::filesystem::path> default_engine(const std::filesystem::path& executable);
Result<std::vector<std::string>> launch_arguments(
    const std::filesystem::path& engine, const std::filesystem::path& package,
    const std::filesystem::path& academy, const std::filesystem::path& profile,
    Game game, bool new_game, const std::vector<std::string>& extra = {});
std::wstring quote_windows_argument(const std::wstring& argument);
Result<bool> spawn(const std::vector<std::string>& arguments);
std::string inspect_argument(const std::string& argument);

} // namespace bootstrap
