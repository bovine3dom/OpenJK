#pragma once

#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/Types.h>

namespace Rml {
class Context;
class RenderInterface;
class SystemInterface;
}
struct SDL_Window;

using KeyDownCallback = bool (*)(Rml::Context*, Rml::Input::KeyIdentifier, int, float, bool);

namespace Backend {
bool Initialize(const char* window_name, int width, int height, bool allow_resize);
void Shutdown();
Rml::SystemInterface* GetSystemInterface();
Rml::RenderInterface* GetRenderInterface();
SDL_Window* GetWindow();
Rml::Vector2i GetDimensions();
void ConfigureContext(Rml::Context* context);
bool ProcessEvents(Rml::Context* context, KeyDownCallback callback = nullptr, bool power_save = false);
void RequestExit();
void BeginFrame();
void PresentFrame();
}
