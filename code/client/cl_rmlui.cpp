// SPDX-License-Identifier: GPL-2.0-or-later
#include <RmlUi/Core.h>
#include "client.h"
#include <algorithm>
#include <cmath>

namespace {

// Embedded, project-owned artwork. No filesystem, font, or texture is required.
const char* reticleRml = R"(
<rml><head><style>
body { margin: 0; padding: 0; width: 32px; height: 32px; }
div { position: absolute; background-color: white; border: 1px black;
      box-sizing: border-box; }
#top { left: 44%; top: 0%; width: 12%; height: 30%; }
#bottom { left: 44%; top: 70%; width: 12%; height: 30%; }
#left { left: 0%; top: 44%; width: 30%; height: 12%; }
#right { left: 70%; top: 44%; width: 30%; height: 12%; }
#dot { left: 45%; top: 45%; width: 10%; height: 10%; }
</style></head><body><div id="top"/><div id="bottom"/>
<div id="left"/><div id="right"/><div id="dot"/></body></rml>
)";

class ReticleSystem final : public Rml::SystemInterface {
public:
	double GetElapsedTime() override { return Sys_Milliseconds() * 0.001; }
	bool LogMessage(Rml::Log::Type, const Rml::String& message) override {
		Com_Printf("RmlUi: %s\n", message.c_str());
		return true;
	}
};

class ReticleRenderer final : public Rml::RenderInterface {
	struct Geometry {
		Rml::Span<const Rml::Vertex> vertices;
		Rml::Span<const int> indices;
	};
	bool scissor = false;
	int clip[4] = {};
public:
	vec4_t tint = {1, 1, 1, 1};

	Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
		Rml::Span<const int> indices) override {
		// This adapter is limited to the embedded reticle, not arbitrary documents.
		if (vertices.size() > REF_UI_MAX_VERTICES || indices.size() > REF_UI_MAX_INDICES) {
			Com_Printf("RmlUi: reticle geometry exceeds renderer limits\n");
			return 0;
		}
		return reinterpret_cast<Rml::CompiledGeometryHandle>(new Geometry{vertices, indices});
	}
	void ReleaseGeometry(Rml::CompiledGeometryHandle handle) override {
		delete reinterpret_cast<Geometry*>(handle);
	}
	void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation,
		Rml::TextureHandle texture) override {
		if (texture) return;
		const auto& geometry = *reinterpret_cast<Geometry*>(handle);
		polyVert_t vertices[REF_UI_MAX_VERTICES] = {};
		for (size_t i = 0; i < geometry.vertices.size(); ++i) {
			const auto& source = geometry.vertices[i];
			auto& vertex = vertices[i];
			vertex.xyz[0] = (source.position.x + translation.x) * 640.0f / cls.glconfig.vidWidth;
			vertex.xyz[1] = (source.position.y + translation.y) * 480.0f / cls.glconfig.vidHeight;
			// RmlUi uses premultiplied colors; the legacy renderer uses straight alpha.
			for (int c = 0; c < 3; ++c)
				vertex.modulate[c] = source.colour.alpha ? static_cast<byte>(std::min(255.0f,
					source.colour[c] * 255.0f / source.colour.alpha * tint[c])) : 0;
			vertex.modulate[3] = static_cast<byte>(source.colour.alpha * tint[3]);
		}
		re.DrawUiGeometry(static_cast<int>(geometry.vertices.size()), vertices,
			static_cast<int>(geometry.indices.size()), geometry.indices.data(), scissor ? clip : nullptr);
	}
	void EnableScissorRegion(bool enable) override { scissor = enable; }
	void SetScissorRegion(Rml::Rectanglei region) override {
		clip[0] = region.Left(); clip[1] = region.Top();
		clip[2] = region.Width(); clip[3] = region.Height();
	}
	Rml::TextureHandle LoadTexture(Rml::Vector2i&, const Rml::String&) override { return 0; }
	Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override { return 0; }
	void ReleaseTexture(Rml::TextureHandle) override {}
};

ReticleSystem systemInterface;
ReticleRenderer renderInterface;
Rml::Context* context = nullptr;
Rml::ElementDocument* document = nullptr;
bool initialized = false;
cvar_t* enabled = nullptr;
cvar_t* scale = nullptr;

} // namespace

void CL_RmlUiShutdown() {
	if (initialized) Rml::Shutdown();
	context = nullptr;
	document = nullptr;
	initialized = false;
}

void CL_RmlUiInit() {
	CL_RmlUiShutdown();
	enabled = Cvar_Get("cg_rmluiReticle", "1", CVAR_ARCHIVE);
	scale = Cvar_Get("cg_rmluiReticleScale", "1", CVAR_ARCHIVE);
	Rml::SetSystemInterface(&systemInterface);
	Rml::SetRenderInterface(&renderInterface);
	initialized = Rml::Initialise();
	if (initialized) {
		context = Rml::CreateContext("reticle", {cls.glconfig.vidWidth, cls.glconfig.vidHeight});
		if (context) document = context->LoadDocumentFromMemory(reticleRml);
	}
	if (!document) {
		Com_Printf("RmlUi: reticle initialization failed; using legacy crosshair\n");
		CL_RmlUiShutdown();
		return;
	}
	document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
	Com_Printf("RmlUi: reticle ready (6.3)\n");
}

qboolean CL_RmlUiDrawReticle(float x, float y, float size, const float* color) {
	if (!document || !enabled->integer || !re.DrawUiGeometry) return qfalse;
	if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(size) || size <= 0) return qfalse;
	const int width = cls.glconfig.vidWidth, height = cls.glconfig.vidHeight;
	if (width <= 0 || height <= 0) return qfalse;
	const float uiScale = std::isfinite(scale->value) ? std::max(0.25f, std::min(4.0f, scale->value)) : 1.0f;
	const float pixels = size * height / 480.0f * uiScale;
	context->SetDimensions({width, height});
	document->SetProperty(Rml::PropertyId::Left, Rml::Property(x * width / 640.0f - pixels * 0.5f, Rml::Unit::PX));
	document->SetProperty(Rml::PropertyId::Top, Rml::Property(y * height / 480.0f - pixels * 0.5f, Rml::Unit::PX));
	document->SetProperty(Rml::PropertyId::Width, Rml::Property(pixels, Rml::Unit::PX));
	document->SetProperty(Rml::PropertyId::Height, Rml::Property(pixels, Rml::Unit::PX));
	for (int c = 0; c < 4; ++c)
		renderInterface.tint[c] = std::isfinite(color[c]) ? std::max(0.0f, std::min(1.0f, color[c])) : 1.0f;
	context->Update();
	context->Render();
	return qtrue;
}
