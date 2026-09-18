#include "ui.h"
#include "bootstrap.h"
#include "jo_import.h"
#include "ui_backend.h"
#include "music_player.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/FileInterface.h>
#include <SDL.h>
#include <nfd.h>
#include <nfd_sdl2.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace launcher_ui {
namespace {
namespace fs = std::filesystem;
using bootstrap::Game;
constexpr std::array<const char*, 2> prefixes{"ja", "jo"};
constexpr std::array<const char*, 2> names{"Jedi Academy", "Jedi Outcast"};

template<class T> T require(bootstrap::Result<T> result) {
    if (!result) throw std::runtime_error(result.error->detail);
    return std::move(result.value);
}

std::string dialog_error() {
    const char* error = NFD_GetError();
    return error ? error : "The folder dialog failed.";
}

struct Runtime {
    bool backend = false, rml = false, nfd = false;
    ~Runtime() {
        if (nfd) NFD_Quit();
        if (rml) Rml::Shutdown();
        if (backend) Backend::Shutdown();
    }
};

class FileInterface final : public Rml::FileInterface {
public:
    Rml::FileHandle Open(const Rml::String& path) override {
#ifdef _WIN32
        return reinterpret_cast<Rml::FileHandle>(_wfopen(fs::u8path(path).c_str(), L"rb"));
#else
        return reinterpret_cast<Rml::FileHandle>(std::fopen(path.c_str(), "rb"));
#endif
    }
    void Close(Rml::FileHandle file) override { std::fclose(reinterpret_cast<FILE*>(file)); }
    size_t Read(void* buffer, size_t size, Rml::FileHandle file) override {
        return std::fread(buffer, 1, size, reinterpret_cast<FILE*>(file));
    }
    bool Seek(Rml::FileHandle file, long offset, int origin) override {
        return std::fseek(reinterpret_cast<FILE*>(file), offset, origin) == 0;
    }
    size_t Tell(Rml::FileHandle file) override {
        const long position = std::ftell(reinterpret_cast<FILE*>(file));
        return position < 0 ? 0 : static_cast<size_t>(position);
    }
};

struct Request {
    bool startup = false, launch = false, new_game = false, resume = false;
    int game = 0, mask = 3;
    std::array<std::string, 2> paths;
    fs::path profile;
};

struct Outcome {
    std::array<std::optional<bootstrap::ValidationResult>, 2> validation;
    std::optional<bootstrap::BootstrapConfig> saved;
    fs::path profile;
    std::vector<std::string> arguments;
    std::string status;
    std::array<std::string, 2> saves;
};

class UI final : public Rml::EventListener {
public:
    UI(Rml::ElementDocument* document, fs::path executable, bool nfd_ready, fs::path music_path)
        : music(std::move(music_path)), document(document), executable(std::move(executable)), nfd_ready(nfd_ready) {
        for (int i = 0; i < 2; ++i) {
            for (const char* suffix : {"path", "locate", "check", "play", "new", "menu"}) {
                auto* control = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(element(id(i, suffix)));
                if (!control) throw std::runtime_error("Invalid launcher input: " + id(i, suffix));
                controls.push_back(control);
                if (std::string(suffix) == "path") inputs[i] = control;
            }
            element(id(i, "state"));
            element(id(i, "detail"));
        }
        for (const char* id : {"status", "progress", "profile", "last"}) element(id);
        document->AddEventListener("click", this);
        document->AddEventListener("change", this);
        std::string bars;
        for (unsigned i = 0; i < launcher_music::band_count; ++i)
            bars += "<div class=\"equaliser-band\" id=\"band-" + std::to_string(i) + "\"></div>";
        element("equaliser")->SetInnerRML(bars);
        for (unsigned i = 0; i < band_elements.size(); ++i) band_elements[i] = element("band-" + std::to_string(i));
        toggle_music();
    }

    ~UI() override {
        cancel.store(true);
        if (job.valid()) job.wait();
        document->RemoveEventListener("click", this);
        document->RemoveEventListener("change", this);
        document->Close();
    }

    void start(Request request) {
        cancel.store(false);
        displayed_progress.clear();
        report("Validating", request.startup ? "Loading bootstrap.ini" : "Checking selected files");
        text("status", "Checking game files. Please wait.");
        disable(true);
        try {
            job = std::async(std::launch::async, [this, request = std::move(request)]() mutable {
                return work(std::move(request));
            });
        } catch (...) {
            disable(false);
            throw;
        }
    }

    void poll() {
        const auto visual = music.visual();
        const auto now = SDL_GetTicks();
        const float decay = std::exp(-float(std::min<Uint32>(now - visual_time, 1000)) / 180.f);
        visual_time = now;
        for (unsigned i = 0; i < band_elements.size(); ++i) {
            band_levels[i] = std::max(visual.bands[i], band_levels[i] * decay);
            const int height = int(band_levels[i] * 100);
            if (height != band_heights[i]) {
                band_elements[i]->SetProperty("height", std::to_string(height) + "%");
                band_heights[i] = height;
            }
        }
        if (!job.valid()) return;
        std::string progress;
        {
            std::lock_guard<std::mutex> lock(progress_mutex);
            progress = progress_text;
        }
        if (progress != displayed_progress) {
            text("progress", progress);
            displayed_progress = std::move(progress);
        }
        if (job.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        const Outcome result = job.get();
        saves = result.saves;
        disable(false);
        text("progress", "");
        if (result.saved) {
            profile = result.profile;
            text("profile", profile.empty() ? "Profile location is unavailable." : profile.u8string());
            inputs[0]->SetValue(result.saved->ja_path ? result.saved->ja_path->u8string() : "");
            inputs[1]->SetValue(result.saved->jo_path ? result.saved->jo_path->u8string() : "");
            text("last", result.saved->ja_path ? std::string("Last played: ") +
                names[result.saved->last_campaign == Game::outcast] : "Select the folders that contain your installed game files.");
        }
        for (int i = 0; i < 2; ++i) {
            if (!result.validation[i]) continue;
            const auto& validation = *result.validation[i];
            std::string state = bootstrap::state_name(validation.state);
            std::replace(state.begin(), state.end(), '_', ' ');
            state[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(state[0])));
            text(id(i, "state"), state);
            std::string detail = validation.detail;
            for (const auto& missing : validation.missing_files) detail += " / " + missing;
            text(id(i, "detail"), detail);
            if (validation) inputs[i]->SetValue(validation.data_root.u8string());
        }
        text("status", result.status);
        if (!result.arguments.empty()) {
            auto spawned = bootstrap::spawn(result.arguments);
            if (!spawned || !spawned.value) {
                text("status", spawned.error ? spawned.error->detail : "The engine could not start.");
            } else {
                disable(true);
                text("status", "Engine started.");
                Backend::RequestExit();
            }
        }
    }

    void ProcessEvent(Rml::Event& event) override {
        try {
            auto* target = event.GetTargetElement();
            while (target && target->GetTagName() != "input") target = target->GetParentNode();
            if (!target) return;
            if (event.GetType() == "click" && target->GetId() == "music") {
                toggle_music();
                return;
            }
            if (job.valid()) return;
            for (int i = 0; i < 2; ++i) {
                const auto& target_id = target->GetId();
                if (event.GetType() == "change" && target_id == id(i, "path")) {
                    text(id(i, "state"), "Not checked");
                    text(id(i, "detail"), "Select Check Files to validate this path.");
                }
                if (event.GetType() != "click") continue;
                if (target_id == id(i, "locate")) browse(i);
                else if (target_id == id(i, "check")) submit(i, false, false);
                else if (target_id == id(i, "play")) submit(i, true, false, true);
                else if (target_id == id(i, "menu")) submit(i, true, false);
                else if (target_id == id(i, "new")) submit(i, true, true);
            }
        } catch (const std::exception& error) {
            text("status", error.what());
        }
    }

private:
    void music_error(const std::string& error) {
        music.stop();
        element("music")->SetAttribute("value", "MUSIC: OFF");
        text("music-error", "Music is unavailable: " + error);
    }
    void toggle_music() {
        try {
            element("music")->SetAttribute("value", music.toggle() ? "MUSIC: ON" : "MUSIC: OFF");
            text("music-error", "");
        } catch (const std::exception& error) { music_error(error.what()); }
    }
    static std::string id(int game, const char* suffix) {
        return std::string(prefixes[game]) + "-" + suffix;
    }
    Rml::Element* element(const std::string& id) {
        auto* result = document->GetElementById(id);
        if (!result) throw std::runtime_error("Missing launcher element: " + id);
        return result;
    }
    void text(const std::string& id, const std::string& value) {
        element(id)->SetInnerRML(Rml::StringUtilities::EncodeRml(value));
    }
    void disable(bool disabled) {
        for (Rml::ElementFormControl* control : controls) control->SetDisabled(disabled);
        for (int i = 0; i < 2; ++i) {
            auto* button = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(element(id(i, "play")));
            button->SetDisabled(disabled || saves[i].empty());
            text(id(i, "save"), saves[i].empty() ? "No saved game" : "Latest save: " + saves[i]);
        }
    }
    void report(const std::string& phase, const std::string& item) {
        std::lock_guard<std::mutex> lock(progress_mutex);
        progress_text = phase + " / " + item;
    }
    void check_cancel() const {
        if (cancel.load()) throw std::runtime_error("Operation cancelled.");
    }
    void submit(int game, bool launch, bool new_game, bool resume = false) {
        Request request;
        request.game = game;
        request.launch = launch;
        request.new_game = new_game;
        request.resume = resume;
        request.mask = launch ? 3 : 1 << game;
        request.profile = profile;
        for (int i = 0; i < 2; ++i) request.paths[i] = inputs[i]->GetValue();
        start(std::move(request));
    }
    void browse(int game) {
        if (!nfd_ready) {
            text("status", "The folder dialog is unavailable. Enter the game path directly, then select Check Again.");
            return;
        }
        nfdpickfolderu8args_t args{};
        const auto current = inputs[game]->GetValue();
        args.defaultPath = current.empty() ? nullptr : current.c_str();
        args.title = game == 0 ? "Locate Jedi Academy files" : "Locate Jedi Outcast files";
        if (!NFD_GetNativeWindowFromSDLWindow(Backend::GetWindow(), &args.parentWindow)) {
            text("status", std::string("Cannot attach the folder dialog: ") + SDL_GetError());
            return;
        }
        nfdu8char_t* selected = nullptr;
        const auto result = NFD_PickFolderU8_With(&selected, &args);
        const std::unique_ptr<nfdu8char_t, decltype(&NFD_FreePathU8)> path(selected, NFD_FreePathU8);
        if (result == NFD_CANCEL) return;
        if (result == NFD_ERROR) {
            text("status", "Folder dialog: " + dialog_error() + " You can enter the path directly.");
            return;
        }
        if (selected) {
            inputs[game]->SetValue(selected);
            submit(game, false, false);
        }
    }

    Outcome work(Request request) {
        Outcome result;
        try {
            std::string config_warning;
            if (request.startup) {
                result.saved.emplace();
                if (request.profile.empty()) request.profile = fs::absolute(require(bootstrap::default_profile_root()));
                result.profile = request.profile;
                auto config = bootstrap::load_config(request.profile / "bootstrap.ini");
                if (config) *result.saved = std::move(config.value);
                else config_warning = "bootstrap.ini: " + config.error->detail + " ";
                if (!request.paths[0].empty()) result.saved->ja_path = fs::u8path(request.paths[0]);
                if (!request.paths[1].empty()) result.saved->jo_path = fs::u8path(request.paths[1]);
                if (result.saved->ja_path) request.paths[0] = result.saved->ja_path->u8string();
                if (result.saved->jo_path) request.paths[1] = result.saved->jo_path->u8string();
            }
            for (int i = 0; i < 2; ++i)
                result.saves[i] = require(bootstrap::latest_save(request.profile, i == 0 ? Game::academy : Game::outcast));
            for (int i = 0; i < 2; ++i) {
                if (!(request.mask & (1 << i))) continue;
                check_cancel();
                report("Validating", names[i]);
                if (request.paths[i].empty()) {
                    result.validation[i] = bootstrap::ValidationResult{};
                    result.validation[i]->detail = "Enter a path or select Locate Files.";
                } else {
                    result.validation[i] = bootstrap::validate(i == 0 ? Game::academy : Game::outcast,
                        fs::u8path(request.paths[i]));
                }
            }
            check_cancel();
            result.status = config_warning + "File check complete. See the campaign details.";
            if (!request.launch) return result;
            if (!*result.validation[0]) throw std::runtime_error("Jedi Academy files must be ready before either campaign can start.");
            const Game game = request.game == 0 ? Game::academy : Game::outcast;
            if (game == Game::outcast && !*result.validation[1])
                throw std::runtime_error("Jedi Outcast files must be ready before this campaign can start.");
            if (request.profile.empty()) throw std::runtime_error("The default profile location is unavailable. Restart the launcher.");
            const auto academy_output = bootstrap::campaign_profile(request.profile, Game::academy) / "OpenJK";
            const bool outcast_ready = result.validation[1] && bool(*result.validation[1]);
            if (require(bootstrap::path_is_within(request.profile, result.validation[0]->data_root)) ||
                require(bootstrap::path_is_within(academy_output, result.validation[0]->data_root)) ||
                (outcast_ready && (require(bootstrap::path_is_within(request.profile, result.validation[1]->data_root)) ||
                    require(bootstrap::path_is_within(academy_output, result.validation[1]->data_root)))))
                throw std::runtime_error("Select a profile folder outside both game-data folders.");
            if (game == Game::outcast) {
                const auto outcast_output = bootstrap::campaign_profile(request.profile, Game::outcast) / "OpenJK";
                if (require(bootstrap::path_is_within(outcast_output, result.validation[0]->data_root)) ||
                    require(bootstrap::path_is_within(outcast_output, result.validation[1]->data_root)))
                    throw std::runtime_error("Select a profile folder outside both game-data folders.");
                if (require(bootstrap::path_is_within(outcast_output, academy_output)) ||
                    require(bootstrap::path_is_within(academy_output, outcast_output)))
                    throw std::runtime_error("Jedi Academy and Jedi Outcast need separate profile folders.");
            }
            report("Preparing", "Checking the engine and profile");
            const auto engine = require(bootstrap::default_engine(executable));
            if (!fs::is_regular_file(engine)) throw std::runtime_error("Engine file is missing: " + engine.u8string());
            const auto campaign_profile = bootstrap::campaign_profile(request.profile, game);
            fs::create_directories(campaign_profile);
            if (game == Game::outcast) {
                report("Preparing import", "Checking free disk space");
                const auto space = jo_import::estimate_space(campaign_profile);
                if (space.error) throw std::runtime_error(space.error->message);
                check_cancel();
                auto imported = jo_import::import_campaign(result.validation[0]->data_root,
                    result.validation[1]->data_root, campaign_profile, [this](const jo_import::Progress& progress) {
                        const char* phase = "Validating import";
                        switch (progress.phase) {
                        case jo_import::Phase::validating: break;
                        case jo_import::Phase::indexing: phase = "Indexing"; break;
                        case jo_import::Phase::importing: phase = "Importing"; break;
                        case jo_import::Phase::finalizing: phase = "Finalizing"; break;
                        }
                        std::string item = progress.item;
                        if (progress.total) item = std::to_string(progress.completed) + "/" +
                            std::to_string(progress.total) + " / " + item;
                        report(phase, item);
                        return !cancel.load();
                    });
                if (!imported) throw std::runtime_error(imported.error->message + " / " + imported.error->asset);
                result.status = imported.warning;
            }
            check_cancel();
            std::vector<std::string> extra;
            if (request.resume) {
                const auto save = require(bootstrap::latest_save(request.profile, game));
                if (save.empty()) throw std::runtime_error("No saved game for this campaign. Select New Game.");
                extra = {"+load", save};
            }
            auto arguments = require(bootstrap::launch_arguments(engine, bootstrap::package_root(executable),
                result.validation[0]->data_root, request.profile, game, request.new_game, extra));
            bootstrap::BootstrapConfig config;
            config.ja_path = result.validation[0]->data_root;
            if (*result.validation[1]) config.jo_path = result.validation[1]->data_root;
            config.last_campaign = game;
            report("Preparing launch", "Saving validated paths");
            if (!require(bootstrap::save_config(request.profile / "bootstrap.ini", config)))
                throw std::runtime_error("Cannot save bootstrap.ini.");
            check_cancel();
            result.arguments = std::move(arguments);
        } catch (const std::exception& error) {
            result.status = error.what();
        } catch (...) {
            result.status = "The operation failed. Check the paths and try again.";
        }
        return result;
    }

    launcher_music::Player music;
    std::array<Rml::Element*, launcher_music::band_count> band_elements{};
    std::array<float, launcher_music::band_count> band_levels{};
    std::array<int, launcher_music::band_count> band_heights{};
    Uint32 visual_time = SDL_GetTicks();
    Rml::ElementDocument* document;
    fs::path executable, profile;
    bool nfd_ready = false;
    std::array<Rml::ElementFormControlInput*, 2> inputs{};
    std::array<std::string, 2> saves;
    std::vector<Rml::ElementFormControlInput*> controls;
    std::atomic<bool> cancel{false};
    std::mutex progress_mutex;
    std::string progress_text, displayed_progress;
    std::future<Outcome> job;
};
} // namespace

int run(const fs::path& profile, const bootstrap::BootstrapConfig& config) {
    FileInterface file_interface;
    Runtime runtime;
    try {
        runtime.backend = Backend::Initialize("OpenJedvibe", 1000, 720, true);
        if (!runtime.backend) throw std::runtime_error(std::string("Cannot create the launcher window: ") + SDL_GetError());
        Rml::SetSystemInterface(Backend::GetSystemInterface());
        Rml::SetRenderInterface(Backend::GetRenderInterface());
        Rml::SetFileInterface(&file_interface);
        runtime.rml = Rml::Initialise();
        if (!runtime.rml) throw std::runtime_error("Cannot initialize the RmlUi launcher interface.");
        runtime.nfd = NFD_Init() == NFD_OKAY;
        if (runtime.nfd && !NFD_SetDisplayPropertiesFromSDLWindow(Backend::GetWindow())) {
            NFD_Quit();
            runtime.nfd = false;
        }

        const auto executable = require(bootstrap::executable_path());
        auto resource = executable.parent_path().parent_path() / "Resources" / "launcher";
        if (!fs::is_regular_file(resource / "launcher.rml"))
            resource = bootstrap::package_root(executable) / "launcher";
        if (!fs::is_regular_file(resource / "launcher.rml"))
            resource = bootstrap::package_root(executable) / "launcher-resources";
        for (const char* file : {"launcher.rml", "launcher.rcss"}) {
            if (!fs::is_regular_file(resource / file))
                throw std::runtime_error("Launcher resource is missing: " + (resource / file).u8string());
        }
        for (const char* font : {"IBMPlexSans-SemiBold.ttf", "IBMPlexMono-Regular.ttf"}) {
            const auto path = resource / "fonts" / font;
            if (!Rml::LoadFontFace(path.u8string()))
                throw std::runtime_error("Cannot load launcher font: " + path.u8string());
        }
        auto* context = Rml::CreateContext("launcher", Backend::GetDimensions());
        if (!context) throw std::runtime_error("Cannot create the launcher UI context.");
        Backend::ConfigureContext(context);
        auto* document = context->LoadDocument((resource / "launcher.rml").u8string());
        if (!document) throw std::runtime_error("Cannot load launcher document: " + (resource / "launcher.rml").u8string());
        if (!document->GetStyleSheetContainer())
            throw std::runtime_error("Cannot load launcher stylesheet: " + (resource / "launcher.rcss").u8string());
        {
            UI ui(document, executable, runtime.nfd, resource / "cantina-band.score");
            document->Show();
            Request startup;
            startup.startup = true;
            startup.profile = profile;
            if (config.ja_path) startup.paths[0] = config.ja_path->u8string();
            if (config.jo_path) startup.paths[1] = config.jo_path->u8string();
            ui.start(std::move(startup));
            while (Backend::ProcessEvents(context)) {
                ui.poll();
                context->Update();
                Backend::BeginFrame();
                context->Render();
                Backend::PresentFrame();
                SDL_Delay(8);
            }
        }
        Rml::RemoveContext("launcher");
        return 0;
    } catch (const std::exception& error) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "OpenJedvibe launcher startup error", error.what(),
            runtime.backend ? Backend::GetWindow() : nullptr);
        return 1;
    }
}
} // namespace launcher_ui
