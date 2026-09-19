#include "jo_import.h"

#include <miniz.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <initializer_list>
#include <map>
#include <memory>
#include <set>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace jo_import {
namespace {
namespace fs = std::filesystem;
using View = std::string_view;
using Pairs = std::vector<std::pair<std::string, std::string>>;
using Aliases = std::map<std::string, std::string>;
constexpr View humanoid = "models/players/_humanoid/";
constexpr View cinematic = "models/players/jo_cinematic/jo_cinematic";
constexpr std::uint64_t maximum_entry_size = 256ULL * 1024 * 1024;

struct Failure { Error error; };

[[noreturn]] void fail(ErrorCode code, std::string message,
                       const fs::path& path = {}, std::string asset = {}) {
    throw Failure{{code, std::move(message), path, std::move(asset)}};
}

void report(const ProgressCallback& callback, Phase phase, std::size_t done = 0,
            std::size_t total = 0, std::string item = {}) {
    if (callback && !callback({phase, done, total, std::move(item)}))
        fail(ErrorCode::cancelled, "Import cancelled. Run the import again to retry.");
}

bool starts(View s, View prefix) { return s.substr(0, prefix.size()) == prefix; }
bool ends(View s, View suffix) {
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}
bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f'; }
bool word(char c) {
    auto u = static_cast<unsigned char>(c);
    return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') ||
           (u >= '0' && u <= '9') || u == '_' ||
           (u >= 0xc0 && u != 0xd7 && u != 0xf7) ||
           u == 0x8a || u == 0x8c || u == 0x8e || u == 0x9a || u == 0x9c ||
           u == 0x9e || u == 0x9f || u == 0xaa || u == 0xb2 || u == 0xb3 || u == 0xb5 || u == 0xb9 || u == 0xba;
}
std::string lower(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return s;
}
std::string upper(std::string s) {
    for (char& c : s) if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    return s;
}
std::size_t skip_space(View s, std::size_t p) {
    while (p < s.size() && space(s[p])) ++p;
    return p;
}
bool ui(View name) {
    return starts(name, "gfx/menus/") || starts(name, "gfx/hud/") || starts(name, "gfx/2d/");
}
void check_cp1252(View text) {
    for (unsigned char c : text)
        if (c == 0x81 || c == 0x8d || c == 0x8f || c == 0x90 || c == 0x9d)
            fail(ErrorCode::invalid_data, "Undefined Windows-1252 character in game text. Check the source game installation.");
}

struct FileCloser {
    void operator()(FILE* file) const { if (file) std::fclose(file); }
};
using File = std::unique_ptr<FILE, FileCloser>;
File open_file(const fs::path& path, bool writing) {
#ifdef _WIN32
    FILE* file = _wfopen(path.c_str(), writing ? L"w+b" : L"rb");
#else
    FILE* file = std::fopen(path.c_str(), writing ? "w+b" : "rb");
#endif
    if (!file) fail(ErrorCode::io, std::string(writing ? "Cannot create file: " : "Cannot open file: ") +
                    std::strerror(errno) + ". Check the path and file permissions.", path);
    return File(file);
}

void flush_file(FILE* file, const fs::path& path) {
    if (std::fflush(file) != 0)
        fail(ErrorCode::io, "Cannot flush file. Check available disk space.", path);
#ifdef _WIN32
    if (_commit(_fileno(file)) != 0)
#else
    if (fsync(fileno(file)) != 0)
#endif
        fail(ErrorCode::io, "Cannot sync file. Check available disk space.", path);
}

struct Zip {
    fs::path path;
    File file;
    mz_zip_archive zip{};
    bool active = false;

    Zip(const fs::path& p, bool write) : path(p), file(open_file(p, write)) {
        active = write ? mz_zip_writer_init_cfile(&zip, file.get(), 0) != 0
                       : mz_zip_reader_init_cfile(&zip, file.get(), 0, 0) != 0;
        if (!active) {
            auto message = zip_error();
            if (zip.m_pState) mz_zip_end(&zip);
            fail(ErrorCode::archive, "Cannot open PK3: " + message + ". Check the game installation or output folder.", path);
        }
    }
    Zip(const Zip&) = delete;
    Zip& operator=(const Zip&) = delete;
    ~Zip() { if (active) mz_zip_end(&zip); }
    std::string zip_error() { return mz_zip_get_error_string(mz_zip_peek_last_error(&zip)); }
    void check(bool ok, const std::string& operation, const std::string& asset = {}) {
        if (!ok) fail(ErrorCode::archive, operation + ": " + zip_error() +
                      ". Check the source PK3 files and available disk space.", path, asset);
    }
    std::string read(mz_uint index, const std::string& name) {
        mz_zip_archive_file_stat stat{};
        check(mz_zip_reader_file_stat(&zip, index, &stat) != 0, "Cannot read PK3 entry", name);
        if (stat.m_uncomp_size > maximum_entry_size || stat.m_uncomp_size > std::string().max_size())
            fail(ErrorCode::invalid_data, "PK3 entry is too large to load.", path, name);
        std::string data(static_cast<std::size_t>(stat.m_uncomp_size), '\0');
        check(mz_zip_reader_extract_to_mem(&zip, index, data.data(), data.size(), 0) != 0,
              "Cannot extract PK3 entry", name);
        return data;
    }
    void finish() {
        check(mz_zip_writer_finalize_archive(&zip) != 0, "Cannot finalize PK3");
        bool ok = mz_zip_writer_end(&zip) != 0;
        active = false;
        check(ok, "Cannot close PK3 writer");
        flush_file(file.get(), path);
        if (std::fclose(file.release()) != 0)
            fail(ErrorCode::io, "Cannot close PK3. Check available disk space.", path);
    }
};

struct Entry { Zip* archive; mz_uint index; };
struct Assets {
    std::vector<std::unique_ptr<Zip>> archives;
    std::map<std::string, Entry> entries;

    void index(const std::vector<fs::path>& paths, const ProgressCallback& callback) {
        for (std::size_t p = 0; p < paths.size(); ++p) {
            report(callback, Phase::indexing, p, paths.size(), paths[p].u8string());
            auto archive = std::make_unique<Zip>(paths[p], false);
            for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&archive->zip); ++i) {
                if (mz_zip_reader_is_file_a_directory(&archive->zip, i)) continue;
                auto size = mz_zip_reader_get_filename(&archive->zip, i, nullptr, 0);
                archive->check(size > 1, "Cannot read PK3 entry name");
                std::string name(size, '\0');
                archive->check(mz_zip_reader_get_filename(&archive->zip, i, name.data(), size) == size,
                               "Cannot read PK3 entry name");
                name.pop_back();
                if (name.find('\0') != std::string::npos)
                    fail(ErrorCode::invalid_data, "PK3 entry name contains a null byte.", paths[p]);
                entries[lower(name)] = {archive.get(), i};
            }
            archives.push_back(std::move(archive));
        }
    }
    bool has(const std::string& name) const { return entries.count(name) != 0; }
    std::string read(const std::string& name) const {
        auto it = entries.find(name);
        if (it == entries.end()) fail(ErrorCode::invalid_source,
            "Required game asset is missing. Check the source game installation.", {}, name);
        return it->second.archive->read(it->second.index, name);
    }
};

struct Output {
    Zip archive;
    const ProgressCallback& callback;
    std::set<std::string> names;
    std::size_t count = 0;
    Output(const fs::path& p, const ProgressCallback& cb) : archive(p, true), callback(cb) {}
    void add(const std::string& name, const std::string& data) {
        if (!names.insert(lower(name)).second)
            fail(ErrorCode::invalid_data, "Generated PK3 entry conflicts with another asset.", archive.path, name);
        report(callback, Phase::importing, count, 0, name);
        archive.check(mz_zip_writer_add_mem(&archive.zip, name.c_str(), data.data(), data.size(), 1) != 0,
                      "Cannot write PK3 entry", name);
        ++count;
    }
};

void bounds(View data, std::size_t pos, std::size_t count) {
    if (pos > data.size() || count > data.size() - pos)
        fail(ErrorCode::invalid_data, "Truncated game asset. Check the source game installation.");
}
std::uint32_t u32(View data, std::size_t pos) {
    bounds(data, pos, 4);
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value |= static_cast<std::uint32_t>(static_cast<unsigned char>(data[pos + i])) << (i * 8);
    return value;
}
void append_u32(std::string& data, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) data += static_cast<char>((value >> (i * 8)) & 255);
}
std::size_t nonnegative(View data, std::size_t pos) {
    auto value = u32(data, pos);
    if (value > 0x7fffffffU) fail(ErrorCode::invalid_data, "Negative count, size, or offset in game asset.");
    return value;
}
void fixed_string(std::string& data, std::size_t pos, View value) {
    bounds(data, pos, 64);
    if (value.size() >= 64) fail(ErrorCode::invalid_data, "Model name exceeds its 64-byte field.");
    data.replace(pos, 64, std::string(value) + std::string(64 - value.size(), '\0'));
}
View fixed_view(View data, std::size_t pos) {
    bounds(data, pos, 64);
    View field = data.substr(pos, 64);
    auto end = field.find('\0');
    return field.substr(0, end);
}

std::string surface_names(std::string text) {
    for (std::size_t p = 0; p < text.size();) {
        if (!word(text[p])) { ++p; continue; }
        auto end = p + 1;
        while (end < text.size() && word(text[end])) ++end;
        View token(text.data() + p, end - p);
        if (token == "head_off" || token == "head_face_off" || token == "head_eyes_mouth_off" ||
            token == "head_fins_off" || token == "torso_augment_off")
            text.replace(end - 3, 3, "alt");
        p = end;
    }
    return text;
}
std::string convert_model(std::string data) {
    auto count = nonnegative(data, 152), pos = nonnegative(data, 156);
    for (std::size_t i = 0; i < count; ++i) {
        bounds(data, pos, 144);
        fixed_string(data, pos, surface_names(std::string(fixed_view(data, pos))));
        auto children = nonnegative(data, pos + 140);
        if (children > (data.size() - pos - 144) / 4)
            fail(ErrorCode::invalid_data, "Invalid model surface child count.");
        pos += 144 + children * 4;
    }
    return data;
}
std::string convert_skin(std::string data) {
    for (std::size_t p = 0; p < data.size();) {
        auto end = data.find_first_of(",\r\n", p);
        if (end == std::string::npos) break;
        if (data[end] == ',' && end > p)
            data.replace(p, end - p, surface_names(data.substr(p, end - p)));
        auto next = data.find('\n', end);
        if (next == std::string::npos) break;
        p = next + 1;
    }
    return data;
}

Aliases script_aliases(View data) {
    std::set<std::string> cockpit;
    for (std::size_t p = 0; (p = data.find("BOTH_COCKPIT_", p)) != View::npos;) {
        auto end = p + 13;
        while (end < data.size() && word(data[end])) ++end;
        if (end > p + 13) cockpit.emplace(data.substr(p, end - p));
        p = end;
    }
    std::vector<std::string> names(cockpit.begin(), cockpit.end());
    for (auto name : {"BOTH_TALKGESTURE11START", "BOTH_TALKGESTURE11STOP", "BOTH_TALKGESTURE2"}) names.emplace_back(name);
    if (names.size() > 44) fail(ErrorCode::invalid_data, "Too many legacy cinematic aliases (maximum 44).");
    Aliases aliases;
    for (std::size_t i = 0; i < names.size(); ++i)
        aliases[names[i] + '\0'] = "BOTH_CIN_" + std::to_string(i + 1) + '\0';
    unsigned slot = 45;
    for (auto name : {"BOTH_ALERT1", "TORSO_RAISEWEAP2", "TORSO_DROPWEAP2", "BOTH_TRIUMPHANT1START",
                      "BOTH_TRIUMPHANT1STARTGESTURE", "BOTH_TRIUMPHANT1STOP"})
        aliases[std::string(name) + '\0'] = "BOTH_CIN_" + std::to_string(slot++) + '\0';
    aliases[std::string("BOTH_SCARED1") + '\0'] = std::string("BOTH_CROUCH3") + '\0';
    aliases[std::string("BOTH_DEADFORWARD1") + '\0'] = std::string("BOTH_DEAD1") + '\0';
    return aliases;
}
std::string animation_config(std::string data, const Aliases& aliases) {
    std::string extra;
    for (std::size_t p = 0; p < data.size();) {
        auto end = data.find_first_of("\r\n", p);
        if (end == std::string::npos) end = data.size();
        View line(data.data() + p, end - p);
        auto first = skip_space(line, 0), split = first;
        while (split < line.size() && !space(line[split])) ++split;
        auto rest = skip_space(line, split);
        auto alias = aliases.find(std::string(line.substr(first, split - first)) + '\0');
        if (rest < line.size() && alias != aliases.end())
            extra += alias->second.substr(0, alias->second.size() - 1) + " " + std::string(line.substr(rest)) + "\n";
        p = end + 1;
    }
    while (!data.empty() && space(data.back())) data.pop_back();
    return data + "\n" + extra;
}
std::string convert_script(View data, const Aliases& aliases) {
    if (data.size() < 8 || data.substr(0, 4) != View("IBI\0", 4) || u32(data, 4) != 0x3fc8f5c3U)
        fail(ErrorCode::invalid_data, "Unsupported ICARUS version. Expected IBI version 1.57.");
    std::string output(data.substr(0, 8));
    for (std::size_t p = 8; p < data.size();) {
        bounds(data, p, 9);
        auto count = nonnegative(data, p + 4);
        output.append(data.substr(p, 9));
        p += 9;
        if (count > (data.size() - p) / 8) fail(ErrorCode::invalid_data, "Invalid ICARUS member count.");
        for (std::size_t i = 0; i < count; ++i) {
            bounds(data, p, 8);
            auto kind = u32(data, p);
            auto size = nonnegative(data, p + 4);
            p += 8;
            bounds(data, p, size);
            std::string value(data.substr(p, size));
            p += size;
            auto alias = aliases.find(value);
            if (alias != aliases.end()) value = alias->second;
            append_u32(output, kind);
            append_u32(output, static_cast<std::uint32_t>(value.size()));
            output += value;
        }
    }
    return output;
}

struct Field {
    std::string key;
    std::size_t value, end;
};
std::vector<Field> fields(View text) {
    std::vector<Field> result;
    for (std::size_t line = 0; line < text.size();) {
        auto p = skip_space(text, line), end = p;
        while (end < text.size() && word(text[end])) ++end;
        if (end > p && end < text.size() && space(text[end])) {
            auto value = skip_space(text, end);
            auto stop = text.find_first_of("\r\n", value);
            if (stop == View::npos) stop = text.size();
            result.push_back({lower(std::string(text.substr(p, end - p))), value, stop});
        }
        auto next = text.find('\n', p);
        if (next == View::npos) break;
        line = next + 1;
    }
    return result;
}
std::string field_word(View text, const Field& f, bool quoted = false) {
    auto p = f.value;
    if (quoted && p < text.size() && text[p] == '"') ++p;
    auto end = p;
    while (end < text.size() && word(text[end])) ++end;
    return std::string(text.substr(p, end - p));
}
std::string get_field(View text, View key, bool quoted = false) {
    for (const auto& f : fields(text)) if (f.key == key) return field_word(text, f, quoted);
    return {};
}
bool has_field(View text, View key) {
    for (const auto& f : fields(text)) if (f.key == key) return true;
    return false;
}

struct Definition { std::size_t open, close; std::string name; };
std::vector<Definition> definitions(View text) {
    std::vector<Definition> result;
    for (std::size_t line = 0; line < text.size();) {
        auto p = skip_space(text, line), end = p;
        while (end < text.size() && word(text[end])) ++end;
        auto open = skip_space(text, end);
        if (end > p && open < text.size() && text[open] == '{') {
            auto close = text.find('}', open + 1);
            if (close == View::npos) fail(ErrorCode::invalid_data, "Unclosed NPC definition.");
            if (close > open + 1) result.push_back({open, close, std::string(text.substr(p, end - p))});
            line = close + 1;
        } else {
            auto next = text.find('\n', p);
            if (next == View::npos) break;
            line = next + 1;
        }
    }
    return result;
}

using Powers = std::vector<std::pair<std::string, int>>;
Powers force_defaults(const std::string& cls, const std::string& rank) {
    if (cls != "CLASS_DESANN" && cls != "CLASS_LUKE" && cls != "CLASS_TAVION" &&
        cls != "CLASS_SHADOWTROOPER" && cls != "CLASS_JEDI" && cls != "CLASS_REBORN") return {};
    if (cls == "CLASS_DESANN" || cls == "CLASS_LUKE" || cls == "CLASS_TAVION" ||
        (cls == "CLASS_JEDI" && rank == "commander")) {
        Powers powers = {{"LEVITATION", 3}, {"PUSH", 3}, {"PULL", 3}, {"SABERTHROW", 3},
                         {"SPEED", 3}, {"SABER_DEFENSE", 3}, {"SABER_OFFENSE", 3}};
        if (cls == "CLASS_TAVION" || cls == "CLASS_JEDI") powers[2].second = 2;
        if (cls == "CLASS_DESANN" || cls == "CLASS_TAVION") {
            int level = cls == "CLASS_DESANN" ? 3 : 2;
            powers.emplace_back("GRIP", level);
            powers.emplace_back("LIGHTNING", level);
        }
        return powers;
    }
    if (cls == "CLASS_SHADOWTROOPER") return {{"LEVITATION", 3}, {"PUSH", 3}, {"PULL", 2},
        {"SABERTHROW", 2}, {"GRIP", 2}, {"LIGHTNING", 1}, {"SPEED", 3}, {"SABER_DEFENSE", 3}, {"SABER_OFFENSE", 3}};
    if (cls == "CLASS_JEDI" || rank == "lt") {
        Powers powers = {{"LEVITATION", 2}, {"PUSH", 2}, {"PULL", 1}, {"SABERTHROW", 2},
                         {"SPEED", 2}, {"SABER_DEFENSE", 3}, {"SABER_OFFENSE", 3}};
        if (cls != "CLASS_JEDI") powers.emplace_back("GRIP", 2);
        return powers;
    }
    if (rank == "ltjg") return {{"PUSH", 2}, {"SABERTHROW", 2}, {"SPEED", 1}, {"SABER_DEFENSE", 3}, {"SABER_OFFENSE", 2}};
    if (rank == "ensign") return {{"LEVITATION", 1}, {"PUSH", 2}, {"PULL", 1}, {"SPEED", 1}, {"SABER_DEFENSE", 1}, {"SABER_OFFENSE", 1}};
    if (rank == "crewman") return {{"LEVITATION", 2}, {"SPEED", 1}, {"SABER_DEFENSE", 1}, {"SABER_OFFENSE", 1}};
    return {{"SPEED", 1}, {"SABER_DEFENSE", 1}, {"SABER_OFFENSE", 1}};
}
std::string saber(const std::string& cls) {
    for (auto name : {"Kyle", "Luke", "Desann", "Tavion", "Reborn"})
        if (cls == "CLASS_" + upper(name)) return name;
    return "single_1";
}
std::string convert_npcs(std::string text) {
    check_cp1252(text);
    auto list = fields(text);
    for (auto it = list.rbegin(); it != list.rend(); ++it) {
        const auto& f = *it;
        if (f.key == "class") {
            auto value = field_word(text, f, true);
            if (value.empty()) continue;
            auto end = f.value + value.size() + (text[f.value] == '"' ? 1 : 0);
            if (end < text.size() && text[end] == '"') ++end;
            value = upper(value);
            if (starts(value, "CLASS_")) value.erase(0, 6);
            if (value == "GALAK_MECH") value = "GALAKMECH";
            if (value == "MORGAN") value = "MORGANKATARN";
            text.replace(f.value, end - f.value, "CLASS_" + value);
        } else if (f.key == "surfon" || f.key == "surfoff") {
            text.replace(f.value, f.end - f.value, surface_names(text.substr(f.value, f.end - f.value)));
        }
    }
    auto defs = definitions(text);
    for (auto it = defs.rbegin(); it != defs.rend(); ++it) {
        View body(text.data() + it->open + 1, it->close - it->open - 1);
        auto cls = upper(get_field(body, "class"));
        auto rank = lower(get_field(body, "rank"));
        if (rank.empty()) rank = "civilian";
        std::string extra;
        if (has_field(body, "sabercolor") && !has_field(body, "saber")) extra += "saber " + saber(cls) + "\n";
        for (const auto& power : force_defaults(cls, rank))
            if (!has_field(body, "fp_" + lower(power.first)))
                extra += "FP_" + power.first + " " + std::to_string(power.second) + "\n";
        if (!extra.empty()) text.insert(it->open + 1, "\n" + extra);
    }
    return text;
}

std::string cinematic_npcs(Output& dest, const Assets& jo, std::string npcs) {
    std::set<std::string> models;
    std::string clones;
    for (const auto& def : definitions(npcs)) {
        std::string body = npcs.substr(def.open + 1, def.close - def.open - 1);
        auto model = lower(get_field(body, "playermodel", true));
        std::string prefix = "models/players/" + model + "/";
        if (model.empty() || !jo.has(prefix + "model.glm")) continue;
        auto original = jo.read(prefix + "model.glm");
        if (fixed_view(original, 72) != "models/players/_humanoid/_humanoid") continue;
        auto clone = "jo_cinematic_" + model;
        if (models.insert(model).second) {
            auto mesh = convert_model(std::move(original));
            fixed_string(mesh, 72, cinematic);
            dest.add("models/players/" + clone + "/model.glm", mesh);
            for (const auto& entry : jo.entries) {
                const auto& path = entry.first;
                if (starts(path, prefix) && ends(path, ".skin"))
                    dest.add("models/players/" + clone + "/" + path.substr(prefix.size()), convert_skin(jo.read(path)));
            }
        }
        auto list = fields(body);
        for (auto it = list.rbegin(); it != list.rend(); ++it) {
            if (it->key != "playermodel" || lower(field_word(body, *it, true)) != model) continue;
            auto end = it->value + model.size() + (body[it->value] == '"' ? 1 : 0);
            if (end < body.size() && body[end] == '"') ++end;
            body.replace(it->value, end - it->value, clone);
        }
        clones += "\njo_cinematic_" + lower(def.name) + "\n{" + body + "}\n";
    }
    return npcs + clones;
}

struct Token { std::size_t begin, end; };
std::vector<Token> shader_tokens(View text) {
    std::vector<Token> tokens;
    for (std::size_t p = 0; (p = skip_space(text, p)) < text.size();) {
        if (text.substr(p, 2) == "//") {
            auto end = text.find('\n', p + 2);
            p = end == View::npos ? text.size() : end;
        } else if (text.substr(p, 2) == "/*") {
            auto end = text.find("*/", p + 2);
            if (end == View::npos) fail(ErrorCode::invalid_data, "Unclosed shader comment.");
            p = end + 2;
        } else {
            auto begin = p++;
            if (text[begin] == '"') {
                bool closed = false;
                while (p < text.size()) {
                    char c = text[p++];
                    if (c == '\\' && p < text.size()) ++p;
                    else if (c == '"') { closed = true; break; }
                }
                if (!closed) fail(ErrorCode::invalid_data, "Unclosed shader string.");
            } else if (text[begin] != '{' && text[begin] != '}') {
                while (p < text.size() && !space(text[p]) && text[p] != '{' && text[p] != '}' && text[p] != '"') ++p;
            }
            tokens.push_back({begin, p});
        }
    }
    return tokens;
}
Pairs shader_definitions(View text) {
    auto tokens = shader_tokens(text);
    Pairs result;
    auto value = [&](std::size_t i) { return text.substr(tokens[i].begin, tokens[i].end - tokens[i].begin); };
    for (std::size_t i = 0; i < tokens.size();) {
        auto start = i++;
        auto name = value(start);
        if (i == tokens.size() || value(i) != "{")
            fail(ErrorCode::invalid_data, "Missing shader body: " + std::string(name));
        std::size_t depth = 0;
        do {
            if (value(i) == "{") ++depth;
            if (value(i) == "}") --depth;
            ++i;
        } while (i < tokens.size() && depth);
        if (depth) fail(ErrorCode::invalid_data, "Unclosed shader: " + std::string(name));
        while (!name.empty() && name.front() == '"') name.remove_prefix(1);
        while (!name.empty() && name.back() == '"') name.remove_suffix(1);
        result.emplace_back(lower(std::string(name)), text.substr(tokens[start].begin, tokens[i - 1].end - tokens[start].begin));
    }
    return result;
}

// Keep insertion order when a later definition replaces an earlier value.
struct Ordered {
    Pairs values;
    std::map<std::string, std::size_t> positions;
    void set(const std::string& key, std::string value) {
        auto inserted = positions.emplace(key, values.size());
        if (inserted.second) values.emplace_back(key, std::move(value));
        else values[inserted.first->second].second = std::move(value);
    }
};
std::string passcode_shader(View name, View image) {
    std::string text(name);
    text += "\n{\n\tnopicmip\n\tsort additive\n\t{\n\t\tmap ";
    text += image;
    text += "\n\t\tblendFunc GL_ONE GL_ONE\n\t\trgbGen identity\n\t}\n}";
    return text;
}
void write_shaders(Output& dest, const Assets& ja, const Assets& jo) {
    Ordered merged;
    std::set<std::string> paths;
    for (auto assets : {&ja, &jo}) {
        for (const auto& entry : assets->entries) {
            const auto& path = entry.first;
            if (!starts(path, "shaders/") || !ends(path, ".shader")) continue;
            paths.insert(path);
            try {
                for (auto& def : shader_definitions(assets->read(path))) {
                    if (assets == &jo && ui(def.first) && merged.positions.count(def.first)) continue;
                    merged.set(def.first, std::move(def.second));
                }
            } catch (Failure& e) { if (e.error.asset.empty()) e.error.asset = path; throw; }
        }
    }
    for (const auto& item : {std::pair<View, View>{"gfx/passcodes/securitycode_red", "textures/system/securitycode_red"},
             {"gfx/passcodes/securitycode_green", "textures/system/securitycode_green"},
             {"gfx/passcodes/securitycode_blue", "textures/system/securitycode_blue"},
             {"gfx/passcodes/fuelpump4", "textures/narshaddaa/fuelpump4"},
             {"gfx/passcodes/fuelpump3", "textures/narshaddaa/fuelpump3"},
             {"gfx/passcodes/securitycode", "textures/system/securitycode"}})
        merged.set(std::string(item.first), passcode_shader(item.first, item.second));
    for (const auto& path : paths)
        if (path != "shaders/jo_campaign.shader") dest.add(path, "// Definitions merged into jo_campaign.shader\n");
    std::string text;
    for (const auto& def : merged.values) {
        if (!text.empty()) text += "\n\n";
        text += def.second;
    }
    dest.add("shaders/jo_campaign.shader", text + "\n");
}

bool quoted_value(View text, std::size_t p, std::string& value) {
    if (p >= text.size() || text[p] != '"') return false;
    auto begin = ++p;
    while (p < text.size()) {
        if (text[p] == '"') { value = std::string(text.substr(begin, p - begin)); return true; }
        if (text[p++] == '\\') {
            if (p == text.size() || text[p] == '\n') return false;
            ++p;
        }
    }
    return false;
}
Pairs strip_strings(View text) {
    struct Reference { std::size_t begin, end; std::string name; };
    std::vector<Reference> refs;
    for (std::size_t line = 0; line < text.size();) {
        auto p = line;
        while (p < text.size() && (text[p] == ' ' || text[p] == '\t')) ++p;
        if (text.substr(p, 9) == "REFERENCE" && p + 9 < text.size() &&
            (text[p + 9] == ' ' || text[p + 9] == '\t')) {
            p += 9;
            while (p < text.size() && (text[p] == ' ' || text[p] == '\t')) ++p;
            auto end = p;
            while (end < text.size() && word(text[end])) ++end;
            if (end > p) refs.push_back({line, end, std::string(text.substr(p, end - p))});
        }
        auto next = text.find('\n', line);
        if (next == View::npos) break;
        line = next + 1;
    }
    Pairs result;
    for (std::size_t i = 0; i < refs.size(); ++i) {
        auto end = i + 1 < refs.size() ? refs[i + 1].begin : text.size();
        View body = text.substr(refs[i].end, end - refs[i].end);
        for (std::size_t line = 0; line < body.size();) {
            auto p = line;
            while (p < body.size() && (body[p] == ' ' || body[p] == '\t')) ++p;
            std::string value;
            if (body.substr(p, 14) == "TEXT_LANGUAGE1" && p + 14 < body.size() && space(body[p + 14]) &&
                quoted_value(body, skip_space(body, p + 14), value)) {
                result.emplace_back(refs[i].name, std::move(value));
                break;
            }
            auto next = body.find('\n', line);
            if (next == View::npos) break;
            line = next + 1;
        }
    }
    return result;
}
Pairs academy_strings(View text) {
    check_cp1252(text);
    Pairs result;
    for (std::size_t p = 0; (p = text.find("REFERENCE", p)) != View::npos;) {
        auto start = p;
        p += 9;
        if ((start && word(text[start - 1])) || p == text.size() || !space(text[p])) continue;
        auto begin = skip_space(text, p), end = begin;
        while (end < text.size() && word(text[end])) ++end;
        if (end == begin || end == text.size() || !space(text[end])) continue;
        auto lang = skip_space(text, end);
        if (text.substr(lang, 12) != "LANG_ENGLISH" || lang + 12 == text.size() || !space(text[lang + 12])) continue;
        auto quote = skip_space(text, lang + 12);
        std::string value;
        if (quoted_value(text, quote, value)) {
            result.emplace_back(text.substr(begin, end - begin), value);
            p = quote + value.size() + 2;
        }
    }
    return result;
}
std::string stringed(const Pairs& entries) {
    std::string text = "VERSION \"1\"\nCONFIG \"\"\nFILENOTES \"JO import\"\n";
    for (const auto& entry : entries) text += "REFERENCE " + entry.first + "\nLANG_ENGLISH \"" + entry.second + "\"\n";
    check_cp1252(text);
    return text + "ENDMARKER\n";
}
std::string convert_menu(std::string data) {
    for (std::size_t p = 0; (p = data.find("open", p)) != std::string::npos;) {
        auto end = p + 4;
        if (end < data.size() && space(data[end])) {
            end = skip_space(data, end);
            if (View(data).substr(end, 13) == "characterMenu") {
                end = skip_space(data, end + 13);
                if (end < data.size() && data[end] == ';') {
                    data.replace(p, end + 1 - p, "uiScript startgame ;");
                    p += 20;
                    continue;
                }
            }
        }
        p += 4;
    }
    return data;
}

using Entity = Pairs;
std::string* entity_field(Entity& entity, View key) {
    for (auto& field : entity) if (field.first == key) return &field.second;
    return nullptr;
}
View entity_value(const Entity& entity, View key) {
    for (const auto& field : entity) if (field.first == key) return field.second;
    return {};
}
void set_entity_field(Entity& entity, std::string key, std::string value) {
    if (auto* field = entity_field(entity, key)) *field = std::move(value);
    else entity.emplace_back(std::move(key), std::move(value));
}
std::vector<Entity> parse_entities(View text) {
    std::vector<Entity> entities;
    for (std::size_t p = 0; (p = skip_space(text, p)) < text.size();) {
        if (text[p++] != '{') fail(ErrorCode::invalid_data, "Invalid map entity list.");
        Entity entity;
        while ((p = skip_space(text, p)) < text.size() && text[p] != '}') {
            auto read_quoted = [&](std::string& value) {
                if (p == text.size() || text[p++] != '"') fail(ErrorCode::invalid_data, "Invalid map entity field.");
                auto end = text.find('"', p);
                if (end == View::npos) fail(ErrorCode::invalid_data, "Unclosed map entity field.");
                value = std::string(text.substr(p, end - p));
                p = end + 1;
            };
            std::string key, value;
            read_quoted(key);
            p = skip_space(text, p);
            read_quoted(value);
            set_entity_field(entity, std::move(key), std::move(value));
        }
        if (p == text.size()) fail(ErrorCode::invalid_data, "Unclosed map entity.");
        ++p;
        entities.push_back(std::move(entity));
    }
    return entities;
}
std::string serialize_entities(const std::vector<Entity>& entities) {
    std::string output;
    for (const auto& entity : entities) {
        output += "{\n";
        for (const auto& field : entity) output += '"' + field.first + "\" \"" + field.second + "\"\n";
        output += "}\n";
    }
    return output;
}
std::string map_entities(const Assets& jo, View map) {
    const std::string path = "maps/" + std::string(map);
    if (jo.has(path + ".ent")) return jo.read(path + ".ent");
    auto bsp = jo.read(path + ".bsp");
    auto offset = nonnegative(bsp, 8), size = nonnegative(bsp, 12);
    bounds(bsp, offset, size);
    return bsp.substr(offset, size);
}
std::string patch_kejim_post(std::string text) {
    while (!text.empty() && text.back() == '\0') text.pop_back();
    auto entities = parse_entities(text);
    std::size_t guards = 0, counters = 0;
    for (auto& entity : entities) {
        if (entity_value(entity, "NPC_target") == "st_death" &&
            entity_value(entity, "origin") != "188 -252 360") {
            set_entity_field(entity, "NPC_target", "jo_ground_death");
            ++guards;
        }
        if (entity_value(entity, "classname") == "target_counter" &&
            entity_value(entity, "targetname") == "st_death" &&
            entity_value(entity, "target") == "run_check_door" && entity_value(entity, "count") == "7") {
            set_entity_field(entity, "targetname", "jo_ground_death");
            set_entity_field(entity, "count", "6");
            ++counters;
        }
    }
    if (guards != 6) fail(ErrorCode::invalid_data,
        "JO patch expected 6 Kejim ground guards, found " + std::to_string(guards) + ".");
    if (counters != 1) fail(ErrorCode::invalid_data,
        "JO patch expected 1 Kejim door counter, found " + std::to_string(counters) + ".");
    entities.push_back({{"classname", "target_relay"}, {"targetname", "jo_ground_death"},
                        {"target", "st_death"}, {"origin", "276 -444 28"}});
    return serialize_entities(entities);
}
std::string patch_ns_starpad(std::string text) {
    while (!text.empty() && text.back() == '\0') text.pop_back();
    auto entities = parse_entities(text);
    const char* names[] = {"fuel_codes1", "fuel_codes2"};
    const char* scripts[] = {"ns_starpad/cycle_fuel_codes1", "ns_starpad/cycle_fuel_codes2"};
    std::size_t controls[2]{};
    for (const auto& entity : entities) for (int i = 0; i < 2; ++i)
        controls[i] += entity_value(entity, "classname") == "func_usable" &&
            entity_value(entity, "targetname") == names[i] && entity_value(entity, "Usescript") == scripts[i] &&
            entity_value(entity, "endframe") == "3";
    for (int i = 0; i < 2; ++i) if (controls[i] != 1) fail(ErrorCode::invalid_data,
        "JO patch expected 1 Nar Shaddaa fuel control " + std::to_string(i + 1) +
        ", found " + std::to_string(controls[i]) + ".");
    entities.push_back({{"classname", "target_passcode"}, {"origin", "-524 -464 -744"},
        {"angles", "0 0 0"}, {"message", "ns_red_fuel"}, {"target", "NS_STARPAD_OBJ4"}, {"radius", "256"}});
    entities.push_back({{"classname", "target_passcode"}, {"origin", "-524 784 -744"},
        {"angles", "0 0 0"}, {"message", "ns_blue_fuel"}, {"target", "NS_STARPAD_OBJ4"}, {"radius", "256"}});
    return serialize_entities(entities);
}

void build_overlay(const Assets& ja, const Assets& jo, const fs::path& path,
                   const ProgressCallback& callback) {
    for (auto name : {"maps/kejim_post.bsp", "maps/kejim_base.bsp", "ext_data/npcs.cfg"})
        if (!jo.has(name)) fail(ErrorCode::invalid_source, "Missing JO sentinel. Check the Outcast installation.", {}, name);
    auto ja_anims = ja.read(std::string(humanoid) + "animation.cfg");
    auto jo_anims = jo.read(std::string(humanoid) + "animation.cfg");
    auto aliases = script_aliases(jo_anims);
    Output dest(path, callback);
    for (const auto& entry : jo.entries) {
        const auto& name = entry.first;
        bool selected = false;
        for (auto root : {"maps/", "scripts/", "textures/", "sound/", "music/", "video/", "effects/",
                          "models/", "gfx/", "menu/", "levelshots/"}) selected = selected || starts(name, root);
        if (!selected || name == "maps/kejim_post.ent" || name == "maps/ns_starpad.ent" || starts(name, humanoid) ||
            starts(name, "models/weapons2/") || (ja.has(name) && ui(name))) continue;
        try {
            auto data = jo.read(name);
            if (ends(name, ".ibi")) data = convert_script(data, aliases);
            else if (ends(name, ".glm")) data = convert_model(std::move(data));
            else if (ends(name, ".skin")) data = convert_skin(std::move(data));
            else if (ends(name, "/animation.cfg")) data = animation_config(std::move(data), aliases);
            dest.add(name, data);
        } catch (Failure& e) { if (e.error.asset.empty()) e.error.asset = name; throw; }
    }
    dest.add("maps/kejim_post.ent", patch_kejim_post(map_entities(jo, "kejim_post")));
    dest.add("maps/ns_starpad.ent", patch_ns_starpad(map_entities(jo, "ns_starpad")));
    write_shaders(dest, ja, jo);
    dest.add("ext_data/dms.dat", jo.read("ext_data/dms.dat"));
    try {
        auto npcs = convert_npcs(jo.read("ext_data/npcs.cfg"));
        dest.add("ext_data/jo/npcs.cfg", cinematic_npcs(dest, jo, std::move(npcs)));
    } catch (Failure& e) { if (e.error.asset.empty()) e.error.asset = "ext_data/npcs.cfg"; throw; }
    auto gla = jo.read(std::string(humanoid) + "_humanoid.gla");
    fixed_string(gla, 8, std::string(cinematic) + ".gla");
    dest.add(std::string(cinematic) + ".gla", gla);
    dest.add("models/players/jo_cinematic/animation.cfg", animation_config(jo_anims, aliases));
    for (const auto& entry : jo.entries) {
        const auto& name = entry.first;
        if (!starts(name, "strip/") || !ends(name, ".sp")) continue;
        auto entries = strip_strings(jo.read(name));
        if (entries.empty()) continue;
        auto package = fs::u8path(name).stem().u8string();
        auto string_path = "strings/english/" + package + ".str";
        Ordered merged;
        if (ja.has(string_path))
            for (auto& item : academy_strings(ja.read(string_path))) merged.set(item.first, std::move(item.second));
        for (const auto& item : entries) merged.set(item.first, item.second);
        dest.add(string_path, stringed(merged.values));
        if (package == "objectives") {
            std::string objectives;
            for (const auto& item : entries) objectives += item.first + "\n";
            dest.add("ext_data/jo/objectives.dat", objectives);
        }
    }
    for (auto name : {"ui/newgame.menu", "ui/newgame2.menu"})
        if (ja.has(name)) dest.add(name, convert_menu(ja.read(name)));
    dest.add(std::string(humanoid) + "animation.cfg", ja_anims);
    report(callback, Phase::finalizing, dest.count, dest.count);
    dest.archive.finish();
}

bool component_equal(const fs::path& a, const fs::path& b) {
#ifdef _WIN32
    return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
#else
    return a == b;
#endif
}
bool inside(const fs::path& path, const fs::path& root) {
    auto p = path.begin();
    for (const auto& component : root) {
        if (p == path.end() || !component_equal(*p, component)) return false;
        ++p;
    }
    return true;
}
std::vector<fs::path> source_paths(const fs::path& root, std::initializer_list<int> required) {
    for (int number : required) {
        auto path = root / "base" / ("assets" + std::to_string(number) + ".pk3");
        if (!fs::is_regular_file(path)) fail(ErrorCode::invalid_source,
            "Required PK3 is missing. Select the game folder that contains base/assets*.pk3.", path);
    }
    std::vector<fs::path> paths;
    for (const auto& entry : fs::directory_iterator(root / "base")) {
        auto name = entry.path().filename().u8string();
#ifdef _WIN32
        name = lower(name);
#endif
        if (starts(name, "assets") && ends(name, ".pk3")) paths.push_back(entry.path());
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}
void stamp_field(std::string& stamp, const std::string& field) {
    stamp += std::to_string(field.size()) + ":" + field + "\n";
}
void stamp_file(std::string& stamp, const fs::path& path, bool include_path) {
    if (include_path) stamp_field(stamp, fs::canonical(path).u8string());
    stamp_field(stamp, std::to_string(fs::file_size(path)));
    stamp_field(stamp, std::to_string(fs::last_write_time(path).time_since_epoch().count()));
}
std::string signature(const fs::path& ja, const fs::path& jo,
                      const std::vector<fs::path>& ja_paths, const std::vector<fs::path>& jo_paths) {
    std::string stamp = "OpenJK JO native import " + std::to_string(format_version) + "\n";
    for (const auto& group : {std::make_pair(ja, &ja_paths), std::make_pair(jo, &jo_paths)}) {
        stamp_field(stamp, group.first.u8string());
        stamp_field(stamp, std::to_string(group.second->size()));
        for (const auto& path : *group.second) stamp_file(stamp, path, true);
    }
    return stamp;
}
bool cache_matches(const fs::path& target, const fs::path& stamp_path, const std::string& source_stamp) {
    try {
        if (!fs::is_regular_file(target) || !fs::is_regular_file(stamp_path)) return false;
        auto expected = source_stamp;
        stamp_file(expected, target, false);
        if (fs::file_size(stamp_path) != expected.size()) return false;
        auto file = open_file(stamp_path, false);
        std::string stored(expected.size(), '\0');
        if (std::fread(stored.data(), 1, stored.size(), file.get()) != stored.size() || stored != expected) return false;
        Zip zip(target, false);
        return mz_zip_validate_archive(&zip.zip, MZ_ZIP_FLAG_VALIDATE_HEADERS_ONLY) != 0;
    } catch (const Failure&) { return false; }
      catch (const fs::filesystem_error&) { return false; }
}

struct DirectoryGuard {
    fs::path path;
    explicit DirectoryGuard(fs::path p) : path(std::move(p)) {}
    DirectoryGuard(const DirectoryGuard&) = delete;
    DirectoryGuard& operator=(const DirectoryGuard&) = delete;
    ~DirectoryGuard() { std::error_code ignored; fs::remove_all(path, ignored); }
};
void atomic_replace(const fs::path& source, const fs::path& target) {
#ifdef _WIN32
    if (!MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw fs::filesystem_error("Cannot replace output. Close applications that use this file",
                                   source, target, std::error_code(GetLastError(), std::system_category()));
#else
    fs::rename(source, target);
    int directory = open(target.parent_path().c_str(), O_RDONLY | O_DIRECTORY);
    if (directory < 0) fail(ErrorCode::io, "Cannot open output folder for sync.", target.parent_path());
    int status = fsync(directory);
    int saved_errno = errno;
    close(directory);
    if (status) {
        errno = saved_errno;
        fail(ErrorCode::io, "Cannot sync output folder.", target.parent_path());
    }
#endif
}
void write_stamp(const fs::path& path, const std::string& contents) {
    auto file = open_file(path, true);
    if (std::fwrite(contents.data(), 1, contents.size(), file.get()) != contents.size())
        fail(ErrorCode::io, "Cannot write import stamp. Check available disk space.", path);
    flush_file(file.get(), path);
    if (std::fclose(file.release()) != 0) fail(ErrorCode::io, "Cannot close import stamp.", path);
}

} // namespace

SpaceEstimate estimate_space(const fs::path& profile) {
    SpaceEstimate estimate;
    try {
        auto probe = fs::absolute(profile);
        while (!fs::exists(probe)) {
            auto parent = probe.parent_path();
            if (parent == probe || parent.empty())
                fail(ErrorCode::io, "Cannot find a filesystem for the import profile.", profile);
            probe = std::move(parent);
        }
        estimate.available = fs::space(probe).available;
    } catch (const Failure& e) { estimate.error = e.error; }
      catch (const fs::filesystem_error& e) {
        estimate.error = Error{ErrorCode::io, std::string(e.what()) + ". Check the profile path and permissions.", e.path1(), {}};
    } catch (const std::exception& e) {
        estimate.error = Error{ErrorCode::internal, std::string("Cannot check import space: ") + e.what(), profile, {}};
    }
    return estimate;
}

Result import_campaign(const fs::path& academy, const fs::path& outcast, const fs::path& profile,
                       const ProgressCallback& progress) {
    Result result;
    try {
        result.output = profile / "OpenJK" / "zz_jo_campaign.pk3";
        report(progress, Phase::validating);
        auto ja = fs::canonical(academy), jo = fs::canonical(outcast);
        auto destination = fs::weakly_canonical(fs::absolute(profile));
        auto folder = fs::weakly_canonical(destination / "OpenJK");
        if (inside(destination, ja) || inside(destination, jo) || inside(folder, ja) || inside(folder, jo))
            fail(ErrorCode::invalid_profile, "Select a profile folder outside both source game folders.", profile);
        auto ja_paths = source_paths(ja, {0, 1, 2, 3});
        auto jo_paths = source_paths(jo, {0, 1, 2, 5});
        auto source_stamp = signature(ja, jo, ja_paths, jo_paths);
        fs::create_directories(folder);
        result.output = folder / "zz_jo_campaign.pk3";
        auto lock_path = folder / ".jo-import.lock";
        if (!fs::create_directory(lock_path))
            fail(ErrorCode::busy, "Another import may be active. Wait for it to finish. If no import is active, remove this lock folder and retry.", lock_path);
        DirectoryGuard lock(lock_path);
        auto stamp_path = folder / "jo-import.native.stamp";
        if (cache_matches(result.output, stamp_path, source_stamp)) {
            result.reused = true;
            return result;
        }
        auto available = estimate_space(folder);
        if (available.error) throw Failure{*available.error};
        if (!available) fail(ErrorCode::io,
            "JO import needs at least 1 GiB of free space. Free space in the profile location, then retry.", folder);
        auto output = lock.path / "zz_jo_campaign.pk3";
        Assets ja_assets, jo_assets;
        ja_assets.index(ja_paths, progress);
        jo_assets.index(jo_paths, progress);
        build_overlay(ja_assets, jo_assets, output, progress);
        if (source_stamp != signature(ja, jo, source_paths(ja, {0, 1, 2, 3}), source_paths(jo, {0, 1, 2, 5})))
            fail(ErrorCode::invalid_source, "Source PK3 files changed during import. Wait for game updates to finish, then retry.");
        auto new_stamp = source_stamp;
        stamp_file(new_stamp, output, false);
        auto temporary_stamp = lock.path / "jo-import.native.stamp";
        write_stamp(temporary_stamp, new_stamp);
        report(progress, Phase::finalizing);
        atomic_replace(output, result.output);
        // The archive is committed. A stamp failure must not report an import failure.
        try { atomic_replace(temporary_stamp, stamp_path); }
        catch (const std::exception&) { result.warning = "Import succeeded, but the cache stamp could not be replaced. The next import may rebuild the archive."; }
    } catch (const Failure& e) { result.error = e.error; }
      catch (const fs::filesystem_error& e) {
        result.error = Error{ErrorCode::io, std::string(e.what()) + ". Check paths, permissions, and available disk space.", e.path1(), {}};
    } catch (const std::exception& e) {
        result.error = Error{ErrorCode::internal, std::string("Import failed: ") + e.what(), {}, {}};
    } catch (...) {
        result.error = Error{ErrorCode::internal, "Import failed with an unknown error.", {}, {}};
    }
    return result;
}

} // namespace jo_import
