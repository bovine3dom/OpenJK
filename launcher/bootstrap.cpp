#include "bootstrap.h"

#include <miniz.h>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <set>
#include <stdexcept>
#include <system_error>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#else
#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <unistd.h>
extern char** environ;
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

namespace bootstrap {
namespace fs = std::filesystem;
namespace {

struct CloseFile { void operator()(FILE* file) const { std::fclose(file); } };
using File = std::unique_ptr<FILE, CloseFile>;
File open_read(const fs::path& path) {
#ifdef _WIN32
    return File(_wfopen(path.c_str(), L"rb"));
#else
    return File(std::fopen(path.c_str(), "rb"));
#endif
}

bool utf8(const std::string& s) {
    for (std::size_t i = 0; i < s.size();) {
        unsigned c = static_cast<unsigned char>(s[i++]);
        if (c < 0x80) continue;
        unsigned n, value, minimum;
        if (c >= 0xc2 && c <= 0xdf) { n = 1; value = c & 31; minimum = 0x80; }
        else if (c >= 0xe0 && c <= 0xef) { n = 2; value = c & 15; minimum = 0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { n = 3; value = c & 7; minimum = 0x10000; }
        else return false;
        if (n > s.size() - i) return false;
        while (n--) {
            c = static_cast<unsigned char>(s[i++]);
            if ((c & 0xc0) != 0x80) return false;
            value = (value << 6) | (c & 63);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
    }
    return true;
}

void check_text(const std::string& text) {
    if (text.find_first_of(std::string("\0\r\n", 3)) != std::string::npos || !utf8(text))
        throw std::runtime_error("Use valid UTF-8 without NUL, CR, or LF characters.");
}

template<class T, class F> Result<T> guarded(ErrorCode code, const fs::path& path, F action) {
    Result<T> result;
    try { result.value = action(); }
    catch (const std::exception& e) { result.error = Error{code, e.what(), path}; }
    catch (...) { result.error = Error{code, "Unexpected failure. Check the paths and retry.", path}; }
    return result;
}

std::string lower(std::string text) {
    for (char& c : text) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return text;
}

bool component_equal(const fs::path& first, const fs::path& second) {
#ifdef _WIN32
    return CompareStringOrdinal(first.c_str(), -1, second.c_str(), -1, TRUE) == CSTR_EQUAL;
#else
    return first == second;
#endif
}

std::vector<std::string> archives(Game game) {
    return {"assets0.pk3", "assets1.pk3", "assets2.pk3", game == Game::academy ? "assets3.pk3" : "assets5.pk3"};
}

std::vector<std::string> sentinels(Game game) {
    if (game == Game::academy) return {"maps/yavin1.bsp", "models/players/_humanoid/animation.cfg"};
    return {"maps/kejim_post.bsp", "maps/kejim_base.bsp", "ext_data/npcs.cfg", "models/players/_humanoid/animation.cfg"};
}

bool present(const fs::path& path) {
    std::error_code ec;
    auto status = fs::status(path, ec);
    if (ec == std::errc::no_such_file_or_directory || ec == std::errc::not_a_directory) return false;
    if (ec) throw fs::filesystem_error("Cannot read path. Check permissions", path, ec);
    return fs::is_regular_file(status) && fs::file_size(path) != 0;
}

ValidationResult check_base(Game game, const fs::path& base, bool readiness) {
    ValidationResult result;
    result.data_root = fs::weakly_canonical(fs::absolute(base)).parent_path();
    const auto other = game == Game::academy ? Game::outcast : Game::academy;
    const auto required = archives(game), opposite = archives(other);
    bool other_complete = true;
    for (const auto& name : required)
        if (!present(base / name)) result.missing_files.push_back(name);
    for (const auto& name : opposite) other_complete &= present(base / name);
    if (!readiness) {
        result.state = result.missing_files.empty() ? ValidationState::ready :
            other_complete ? ValidationState::wrong_game : ValidationState::incomplete;
        result.detail = result ? "Required PK3 files found; archive contents were not checked." :
            "Select the correct game's data folder, or restore its missing PK3 files.";
        return result;
    }
    std::set<std::string> names;
    std::set<std::string> to_read(required.begin(), required.end());
    if (other_complete && !result.missing_files.empty()) to_read.insert(opposite.begin(), opposite.end());
    const auto expected = sentinels(game), other_expected = sentinels(other);
    std::set<std::string> watched(expected.begin(), expected.end());
    watched.insert(other_expected.begin(), other_expected.end());
    for (const auto& name : to_read) {
        const auto path = base / name;
        if (!present(path)) continue;
        auto file = open_read(path);
        if (!file) {
            result.state = ValidationState::unreadable;
            result.detail = "Cannot open " + path.u8string() + ". Check file permissions.";
            return result;
        }
        mz_zip_archive zip{};
        if (!mz_zip_reader_init_cfile(&zip, file.get(), 0, 0)) {
            result.state = ValidationState::damaged_archive;
            result.detail = "Cannot read the central directory of " + path.u8string() + ". Restore this PK3 from your game installation.";
            return result;
        }
        struct End { mz_zip_archive* zip; ~End() { mz_zip_reader_end(zip); } } end{&zip};
        for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&zip); ++i) {
            mz_uint size = mz_zip_reader_get_filename(&zip, i, nullptr, 0);
            std::string entry(size, '\0');
            if (!size || mz_zip_reader_get_filename(&zip, i, entry.data(), size) != size) {
                result.state = ValidationState::damaged_archive;
                result.detail = "Invalid PK3 directory in " + path.u8string() + ". Restore this archive.";
                return result;
            }
            entry.pop_back();
            entry = lower(entry);
            if (watched.count(entry) && !mz_zip_reader_is_file_a_directory(&zip, i)) {
                if (!mz_zip_validate_file(&zip, i, 0)) {
                    result.state = ValidationState::damaged_archive;
                    result.detail = "Cannot read required data in " + path.u8string() + ". Restore this archive.";
                    return result;
                }
                names.insert(entry);
            }
        }
    }
    const auto contains = [&](const auto& list) {
        return std::all_of(list.begin(), list.end(), [&](const auto& name) { return names.count(name) != 0; });
    };
    if (contains(other_expected) && (!contains(expected) || !result.missing_files.empty())) {
        result.state = ValidationState::wrong_game;
        result.detail = "This folder contains the other game. Select the requested game's data folder.";
    } else {
        for (const auto& name : expected) if (!names.count(name)) result.missing_files.push_back(name);
        result.state = result.missing_files.empty() ? ValidationState::ready : ValidationState::incomplete;
        result.detail = result ? "Game data is ready." : "Required game data is missing. Restore the listed files from your game installation.";
    }
    return result;
}

void io_failure(const char* message) {
    throw std::runtime_error(std::string(message) + ": " + std::strerror(errno) + ". Check permissions and free disk space.");
}

} // namespace

const char* state_name(ValidationState state) {
    switch (state) {
    case ValidationState::ready: return "ready";
    case ValidationState::not_found: return "not_found";
    case ValidationState::incomplete: return "incomplete";
    case ValidationState::wrong_game: return "wrong_game";
    case ValidationState::damaged_archive: return "damaged_archive";
    case ValidationState::unreadable: return "unreadable";
    }
    return "unreadable";
}

ValidationResult validate(Game game, const fs::path& input, bool readiness) {
    ValidationResult best;
    best.detail = "No base folder found. Select the game folder, GameData, base, or the game app.";
    try {
        auto selected = input;
        check_text(selected.u8string());
        if (selected.empty()) return best;
        while (selected.has_relative_path() && (selected.filename().empty() || selected.filename() == "."))
            selected = selected.parent_path();
        if (selected.empty()) selected = ".";
        std::vector<fs::path> candidates{selected / "base", selected / "GameData" / "base"};
        if (lower(selected.filename().u8string()) == "base") candidates.insert(candidates.begin(), selected);
        if (lower(selected.extension().u8string()) == ".app") {
            candidates.push_back(selected / "Contents" / "base");
            candidates.push_back(selected / "Contents" / "Resources" / "GameData" / "base");
        }
        std::error_code ec;
        if (fs::is_directory(selected, ec)) {
            std::vector<fs::path> apps;
            for (fs::directory_iterator it(selected, ec), end; !ec && it != end; it.increment(ec))
                if (lower(it->path().extension().u8string()) == ".app") apps.push_back(it->path() / "Contents" / "base");
            std::sort(apps.begin(), apps.end());
            candidates.insert(candidates.end(), apps.begin(), apps.end());
        }
        std::error_code access_error = ec;
        for (const auto& base : candidates) {
            ec.clear();
            if (!fs::is_directory(base, ec)) {
                if (ec && ec != std::errc::no_such_file_or_directory && ec != std::errc::not_a_directory) access_error = ec;
                continue;
            }
            auto result = check_base(game, base, readiness);
            if (result) return result;
            if (best.state == ValidationState::not_found || static_cast<int>(result.state) > static_cast<int>(best.state)) best = std::move(result);
        }
        if (best.state == ValidationState::not_found && access_error &&
            access_error != std::errc::no_such_file_or_directory && access_error != std::errc::not_a_directory) {
            best.state = ValidationState::unreadable;
            best.detail = "Cannot inspect this folder: " + access_error.message() + ". Check folder permissions.";
        }
    } catch (const std::exception& e) {
        best.state = ValidationState::unreadable;
        best.detail = std::string(e.what()) + ". Check the selected path and permissions.";
    } catch (...) {
        best.state = ValidationState::unreadable;
        best.detail = "Cannot inspect the game folder. Check the path and permissions.";
    }
    return best;
}

Result<BootstrapConfig> load_config(const fs::path& path) {
    return guarded<BootstrapConfig>(ErrorCode::invalid_config, path, [&] {
        check_text(path.u8string());
        BootstrapConfig config;
        auto file = open_read(path);
        if (!file) {
            if (errno == ENOENT) return config;
            io_failure("Cannot open bootstrap.ini");
        }
        std::string contents;
        char buffer[4096];
        while (auto count = std::fread(buffer, 1, sizeof(buffer), file.get())) {
            contents.append(buffer, count);
            if (contents.size() > 1024 * 1024) throw std::runtime_error("bootstrap.ini is too large. Remove it and select the game paths again.");
        }
        if (std::ferror(file.get())) io_failure("Cannot read bootstrap.ini");
        std::set<std::string> keys;
        for (std::size_t start = 0; start < contents.size();) {
            auto end = contents.find('\n', start);
            if (end == std::string::npos) end = contents.size();
            auto line = contents.substr(start, end - start);
            start = end + 1;
            check_text(line);
            if (line.empty()) continue;
            auto equal = line.find('=');
            if (equal == std::string::npos) throw std::runtime_error("Expected key=value in bootstrap.ini.");
            auto key = line.substr(0, equal), value = line.substr(equal + 1);
            if (!keys.insert(key).second) throw std::runtime_error("Duplicate key in bootstrap.ini: " + key);
            if (key == "version") {
                if (value != "1") throw std::runtime_error("Unsupported bootstrap.ini version. Use version=1.");
            } else if (key == "ja_path" || key == "jo_path") {
                if (value.empty()) throw std::runtime_error("Empty game path in bootstrap.ini. Remove the key or select a game folder.");
                (key == "ja_path" ? config.ja_path : config.jo_path) = fs::u8path(value);
            } else if (key == "last_campaign") {
                if (value != "ja" && value != "jo") throw std::runtime_error("last_campaign must be ja or jo.");
                config.last_campaign = value == "ja" ? Game::academy : Game::outcast;
            } else throw std::runtime_error("Unknown bootstrap.ini key: " + key);
        }
        if (!keys.count("version")) throw std::runtime_error("bootstrap.ini needs version=1. Remove it to reset the saved paths.");
        return config;
    });
}

Result<bool> save_config(const fs::path& path, const BootstrapConfig& config) {
    return guarded<bool>(ErrorCode::io, path, [&] {
        check_text(path.u8string());
        std::string contents = "version=1\n";
        const auto add = [&](const char* key, const auto& value) {
            if (!value) return;
            auto text = value->u8string();
            check_text(text);
            if (text.empty()) throw std::runtime_error("Cannot save an empty game path.");
            contents += std::string(key) + "=" + text + "\n";
        };
        add("ja_path", config.ja_path);
        add("jo_path", config.jo_path);
        contents += std::string("last_campaign=") + (config.last_campaign == Game::academy ? "ja\n" : "jo\n");
        if (contents.size() > 1024 * 1024) throw std::runtime_error("Game paths are too long to save in bootstrap.ini.");
        auto parent = fs::absolute(path).parent_path();
        fs::create_directories(parent);
        fs::path temporary;
        struct Cleanup { fs::path& path; ~Cleanup() { std::error_code ec; if (!path.empty()) fs::remove(path, ec); } } cleanup{temporary};
#ifdef _WIN32
        HANDLE handle = INVALID_HANDLE_VALUE;
        for (unsigned i = 0; i < 1000; ++i) {
            auto candidate = parent / (L".bootstrap-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(i) + L".tmp");
            handle = CreateFileW(candidate.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (handle != INVALID_HANDLE_VALUE) { temporary = candidate; break; }
            if (GetLastError() != ERROR_FILE_EXISTS) throw std::system_error(GetLastError(), std::system_category(), "Cannot create config temporary file");
        }
        if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create a unique config temporary file.");
        DWORD written = 0;
        bool ok = WriteFile(handle, contents.data(), static_cast<DWORD>(contents.size()), &written, nullptr) && written == contents.size() && FlushFileBuffers(handle);
        DWORD error = ok ? ERROR_SUCCESS : GetLastError();
        if (!CloseHandle(handle) && ok) { ok = false; error = GetLastError(); }
        if (!ok) throw std::system_error(error, std::system_category(), "Cannot flush bootstrap.ini");
        if (!MoveFileExW(temporary.c_str(), fs::absolute(path).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::system_error(GetLastError(), std::system_category(), "Cannot replace bootstrap.ini");
#else
        auto pattern = (parent / ".bootstrap-XXXXXX").string();
        int fd = mkstemp(pattern.data());
        if (fd < 0) io_failure("Cannot create config temporary file");
        temporary = pattern;
        File file(fdopen(fd, "wb"));
        if (!file) { close(fd); io_failure("Cannot open config temporary file"); }
        if (fchmod(fd, 0600) || std::fwrite(contents.data(), 1, contents.size(), file.get()) != contents.size() ||
            std::fflush(file.get()) || fsync(fd)) io_failure("Cannot flush bootstrap.ini");
        if (std::fclose(file.release())) io_failure("Cannot close bootstrap.ini");
        fs::rename(temporary, path);
        int directory = open(parent.c_str(), O_RDONLY | O_DIRECTORY);
        if (directory < 0) io_failure("Cannot open config folder for sync");
        int status = fsync(directory);
        int saved_errno = errno;
        close(directory);
        if (status) { errno = saved_errno; io_failure("Cannot sync config folder"); }
#endif
        temporary.clear();
        return true;
    });
}

Result<fs::path> default_profile_root() {
    return guarded<fs::path>(ErrorCode::platform, {}, []() -> fs::path {
#ifdef _WIN32
        PWSTR documents = nullptr;
        HRESULT status = SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &documents);
        if (FAILED(status)) throw std::runtime_error("Cannot locate Documents. Select a profile with --profile PATH.");
        std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> holder(documents, &CoTaskMemFree);
        return fs::path(documents) / L"My Games" / L"OpenJK";
#else
#ifndef __APPLE__
        if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) return fs::u8path(xdg) / "openjk";
#endif
        const char* home = std::getenv("HOME");
        if (!home || !*home) throw std::runtime_error("HOME is not set. Select a profile with --profile PATH.");
#ifdef __APPLE__
        return fs::u8path(home) / "Library" / "Application Support" / "OpenJK";
#else
        return fs::u8path(home) / ".local" / "share" / "openjk";
#endif
#endif
    });
}

Result<fs::path> executable_path() {
    return guarded<fs::path>(ErrorCode::platform, {}, []() -> fs::path {
#ifdef _WIN32
        std::wstring buffer(1024, L'\0');
        for (;;) {
            DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (!size) throw std::system_error(GetLastError(), std::system_category(), "Cannot locate launcher");
            if (size < buffer.size()) { buffer.resize(size); return fs::canonical(fs::path(buffer)); }
            buffer.resize(buffer.size() * 2);
        }
#elif defined(__APPLE__)
        uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        std::string buffer(size, '\0');
        if (_NSGetExecutablePath(buffer.data(), &size)) throw std::runtime_error("Cannot locate launcher executable.");
        return fs::canonical(fs::path(buffer.c_str()));
#else
        return fs::canonical("/proc/self/exe");
#endif
    });
}

fs::path package_root(const fs::path& executable) {
    auto directory = executable.parent_path();
#ifdef OPENJK_LAUNCHER_BUNDLE
    if (directory.filename() == "MacOS" && directory.parent_path().filename() == "Contents") return directory;
#else
    if (directory.filename() == "MacOS" && directory.parent_path().filename() == "Contents")
        return directory.parent_path().parent_path().parent_path();
#endif
    return directory;
}

fs::path campaign_profile(const fs::path& root, Game game) {
    return game == Game::outcast ? root / "campaigns" / "jo" : root;
}

Result<bool> path_is_within(const fs::path& path, const fs::path& root) {
    return guarded<bool>(ErrorCode::invalid_argument, path, [&] {
        auto candidate = fs::weakly_canonical(fs::absolute(path));
        auto container = fs::weakly_canonical(fs::absolute(root));
        auto part = candidate.begin();
        for (const auto& expected : container) {
            if (part == candidate.end() || !component_equal(*part, expected)) return false;
            ++part;
        }
        return true;
    });
}

Result<fs::path> default_engine(const fs::path& executable) {
    return guarded<fs::path>(ErrorCode::platform, executable, [&]() -> fs::path {
#ifdef OPENJK_SP_ENGINE_NAME
        auto name = fs::u8path(OPENJK_SP_ENGINE_NAME);
#else
        throw std::runtime_error("OPENJK_SP_ENGINE_NAME is not defined. Rebuild the launcher or use --engine PATH.");
        fs::path name;
#endif
#ifdef _WIN32
        if (lower(name.extension().u8string()) != ".exe") name += L".exe";
#endif
        auto adjacent = executable.parent_path() / name;
#ifdef __APPLE__
        if (name.extension() == ".app") return adjacent / "Contents" / "MacOS" / name.stem();
        if (!fs::exists(adjacent)) {
            auto bundle = package_root(executable) / (name.string() + ".app") / "Contents" / "MacOS" / name;
            if (fs::exists(bundle)) return bundle;
        }
#endif
        return adjacent;
    });
}

Result<std::vector<std::string>> launch_arguments(const fs::path& engine, const fs::path& package,
    const fs::path& academy, const fs::path& profile, Game game, bool new_game, const std::vector<std::string>& extra) {
    return guarded<std::vector<std::string>>(ErrorCode::invalid_argument, {}, [&] {
        std::vector<std::string> args{engine.u8string(), "+set", "fs_basepath", package.u8string(),
            "+set", "fs_cdpath", academy.u8string(), "+set", "fs_homepath", campaign_profile(profile, game).u8string(),
            "+set", "fs_game", "OpenJK", "+set", "com_outcast", game == Game::outcast ? "1" : "0"};
        if (engine.empty() || package.empty() || academy.empty() || profile.empty()) throw std::runtime_error("Engine, package, game, and profile paths must not be empty.");
        if (!fs::exists(campaign_profile(profile, game) / "OpenJK" / "openjk_sp.cfg"))
            args.insert(args.end(), {"+set", "r_mode", "-2", "+set", "r_fullscreen", "1", "+set", "cg_fovAspectAdjust", "1"});
        if (new_game) { args.push_back("+map"); args.push_back(game == Game::academy ? "yavin1" : "kejim_post"); }
        args.insert(args.end(), extra.begin(), extra.end());
        for (const auto& arg : args) {
            check_text(arg);
            if (arg.find('"') != std::string::npos)
                throw std::runtime_error("Engine arguments and paths must not contain double quote characters.");
        }
        return args;
    });
}

Result<std::string> latest_save(const fs::path& profile, Game game) {
    return guarded<std::string>(ErrorCode::io, profile, [&] {
        const auto directory = campaign_profile(profile, game) / "OpenJK" / "saves";
        std::string latest;
        fs::file_time_type time = fs::file_time_type::min();
        if (!fs::exists(directory)) return latest;
        for (const auto& entry : fs::directory_iterator(directory)) {
            const auto name = entry.path().stem().u8string();
            if (entry.path().extension() != ".sav" || lower(name) == "current" ||
                name.empty() || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos ||
                !entry.is_regular_file() || entry.file_size() == 0) continue;
            const auto modified = entry.last_write_time();
            if (modified > time || (modified == time && name > latest)) {
                latest = name;
                time = modified;
            }
        }
        return latest;
    });
}

std::wstring quote_windows_argument(const std::wstring& arg) {
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') { ++slashes; continue; }
        result.append(slashes * (c == L'"' ? 2 : 1), L'\\');
        if (c == L'"') result += L'\\';
        result += c;
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    result += L'"';
    return result;
}

Result<bool> spawn(const std::vector<std::string>& args) {
    return guarded<bool>(ErrorCode::spawn, {}, [&] {
        if (args.empty() || args.front().empty()) throw std::runtime_error("Specify an engine executable with --engine PATH.");
        for (const auto& arg : args) check_text(arg);
#ifdef _WIN32
        std::wstring command;
        for (const auto& arg : args) {
            if (!command.empty()) command += L' ';
            command += quote_windows_argument(fs::u8path(arg).wstring());
        }
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        auto engine = fs::u8path(args.front());
        if (!CreateProcessW(engine.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process))
            throw std::system_error(GetLastError(), std::system_category(), "Cannot start engine. Check --engine and its required libraries");
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
#else
        std::vector<char*> argv;
        for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
        argv.push_back(nullptr);
        pid_t pid;
        int error = posix_spawn(&pid, args.front().c_str(), nullptr, nullptr, argv.data(), environ);
        if (error) throw std::system_error(error, std::generic_category(), "Cannot start engine. Check --engine, execute permissions, and required libraries");
#endif
        return true;
    });
}

std::string inspect_argument(const std::string& arg) {
    const char* hex = "0123456789abcdef";
    std::string result = "\"";
    for (unsigned char c : arg) {
        if (c == '"' || c == '\\') { result += '\\'; result += static_cast<char>(c); }
        else if (c < 32 || c == 127) { result += "\\u00"; result += hex[c >> 4]; result += hex[c & 15]; }
        else result += static_cast<char>(c);
    }
    return result + '"';
}

} // namespace bootstrap
