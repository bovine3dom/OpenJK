#include "ui_backend.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Math.h>
#include <RmlUi_Platform_SDL.h>
#include <RmlUi_Renderer_GL2.h>
#include <SDL.h>

#include <algorithm>
#include <memory>

namespace {
struct Data {
    explicit Data(SDL_Window* value) : system(value), window(value) {}
    SystemInterface_SDL system;
    RenderInterface_GL2 renderer;
    TextInputMethodEditor_SDL text_editor;
    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;
    Rml::Vector2i dimensions;
    float scale = 1.f;
    bool running = true;
};

std::unique_ptr<Data> data;

void update_dimensions(Rml::Context* context = nullptr) {
    int window_width = 0, window_height = 0, drawable_width = 0, drawable_height = 0;
    SDL_GetWindowSize(data->window, &window_width, &window_height);
    SDL_GL_GetDrawableSize(data->window, &drawable_width, &drawable_height);
    if (drawable_width <= 0 || drawable_height <= 0) return;
    data->dimensions = {drawable_width, drawable_height};
    data->scale = window_width > 0 ? float(drawable_width) / float(window_width) : 1.f;
    data->renderer.SetViewport(drawable_width, drawable_height);
    if (context) {
        context->SetDimensions(data->dimensions);
        context->SetDensityIndependentPixelRatio(data->scale);
    }
}
}

bool Backend::Initialize(const char* window_name, int width, int height, bool allow_resize) {
#ifdef SDL_HINT_IME_SHOW_UI
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
#endif
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0) return false;
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 2);
    const Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI |
        (allow_resize ? SDL_WINDOW_RESIZABLE : 0);
    SDL_Window* window = SDL_CreateWindow(window_name, SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, width, height, flags);
    if (!window) {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
        window = SDL_CreateWindow(window_name, SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED, width, height, flags);
    }
    if (!window) { SDL_Quit(); return false; }
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) { SDL_DestroyWindow(window); SDL_Quit(); return false; }
    SDL_GL_MakeCurrent(window, context);
    SDL_GL_SetSwapInterval(1);
    SDL_StopTextInput();
    data = std::make_unique<Data>(window);
    data->context = context;
    update_dimensions();
    Rml::SetTextInputHandler(&data->text_editor);
    return true;
}

void Backend::Shutdown() {
    if (!data) return;
    SDL_GL_DeleteContext(data->context);
    SDL_DestroyWindow(data->window);
    data.reset();
    SDL_Quit();
}

Rml::SystemInterface* Backend::GetSystemInterface() { return &data->system; }
Rml::RenderInterface* Backend::GetRenderInterface() { return &data->renderer; }
SDL_Window* Backend::GetWindow() { return data ? data->window : nullptr; }
Rml::Vector2i Backend::GetDimensions() { return data ? data->dimensions : Rml::Vector2i{}; }
void Backend::ConfigureContext(Rml::Context* context) { update_dimensions(context); }

bool Backend::ProcessEvents(Rml::Context* context, KeyDownCallback callback, bool power_save) {
    bool running = data->running;
    data->running = true;
    SDL_Event event;
    int available = power_save ? SDL_WaitEventTimeout(&event,
        int(Rml::Math::Min(context->GetNextUpdateDelay(), 10.0) * 1000)) : SDL_PollEvent(&event);
    while (available) {
        bool propagate = true;
        if (event.type == SDL_QUIT) {
            running = false;
            propagate = false;
        } else if (event.type == SDL_TEXTEDITING) {
            data->text_editor.HandleEdit(event.edit);
            propagate = false;
        } else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            update_dimensions(context);
            propagate = false;
        } else if (event.type == SDL_KEYDOWN) {
            const auto key = RmlSDL::ConvertKey(event.key.keysym.sym);
            const int modifiers = RmlSDL::GetKeyModifierState();
            if (event.key.keysym.sym == SDLK_ESCAPE) {
                running = false;
                propagate = false;
            } else if (callback && !callback(context, key, modifiers, data->scale, true)) {
                propagate = false;
            } else if (!RmlSDL::InputEventHandler(context, data->window, event)) {
                propagate = false;
            } else if (callback && !callback(context, key, modifiers, data->scale, false)) {
                propagate = false;
            } else {
                propagate = false;
            }
        }
        if (propagate) {
            if (event.type == SDL_MOUSEMOTION) {
                event.motion.x = int(float(event.motion.x) * data->scale);
                event.motion.y = int(float(event.motion.y) * data->scale);
            }
            RmlSDL::InputEventHandler(context, data->window, event);
        }
        available = SDL_PollEvent(&event);
    }
    return running;
}

void Backend::RequestExit() { data->running = false; }
void Backend::BeginFrame() { data->renderer.Clear(); data->renderer.BeginFrame(); }
void Backend::PresentFrame() {
    data->renderer.EndFrame();
    SDL_GL_SwapWindow(data->window);
}
