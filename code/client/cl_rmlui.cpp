// SPDX-License-Identifier: GPL-2.0-or-later
#include <RmlUi/Core.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/RenderManager.h>
#include "client.h"
#include <algorithm>
#include <cmath>

namespace {

// Embedded, project-owned artwork. No filesystem, font, or texture is required.
const char* reticleRml = R"(
<rml><head><style>
body { margin: 0; padding: 0; width: 32px; height: 32px; }
#dot { position: absolute; left: 43.75%; top: 43.75%; width: 12.5%; height: 12.5%;
      background-color: rgba(255, 255, 255, 65%); border: 1px rgba(0, 0, 0, 25%);
      border-radius: 100px; box-sizing: border-box; }
resource-rings { position: absolute; left: 50%; top: 50%; width: 0; height: 0; }
</style></head><body><resource-rings id="resources"/><div id="dot"/></body></rml>
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
					source.colour[c] * 255.0f / source.colour.alpha)) : 0;
			vertex.modulate[3] = source.colour.alpha;
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

class ResourceRings final : public Rml::Element {
	Rml::Geometry geometry;
	ReticleHud::Display display;
	float pixelScale = 1;

	void Arc(Rml::Mesh& mesh, float radius, float thickness, float start, float sweep,
		Rml::Colourb color, float alpha) {
		if (alpha <= 0 || sweep <= 0) return;
		const int segments = std::max(1, int(std::ceil(sweep / 7.5f)));
		const int first = int(mesh.vertices.size());
		color.alpha = byte(255 * alpha);
		for (int i = 0; i <= segments; ++i) {
			const float angle = (start + sweep * i / segments) * 0.01745329252f;
			for (float r : {radius - thickness, radius}) {
				Rml::Vertex vertex{};
				vertex.position = {std::cos(angle) * r * pixelScale, std::sin(angle) * r * pixelScale};
				vertex.colour = color.ToPremultiplied();
				mesh.vertices.push_back(vertex);
			}
			if (i < segments) {
				const int n = first + 2 * i;
				mesh.indices.insert(mesh.indices.end(), {n, n + 1, n + 2, n + 2, n + 1, n + 3});
			}
		}
	}
	void Ring(Rml::Mesh& mesh, float radius, float value, Rml::Colourb color, float alpha) {
		Arc(mesh, radius, 0.8f, -90, 360, color, alpha * 0.12f);
		Arc(mesh, radius, 0.8f, -90, 360 * value, color, alpha * 0.55f);
	}
	void OnRender() override {
		if (display.forceAlpha <= 0 && display.ammoAlpha <= 0 && display.stanceAlpha <= 0 &&
			display.healthAlpha <= 0 && display.armorAlpha <= 0) return;
		Rml::Mesh mesh;
		Ring(mesh, 10, display.force, {135, 205, 255}, display.forceAlpha);
		Ring(mesh, 14, display.ammo, {255, 225, 140}, display.ammoAlpha);
		if (display.stance >= 0 && display.stance <= 2) {
			const Rml::Colourb colors[] = {{110, 175, 255}, {255, 225, 125}, {255, 105, 100}};
			Arc(mesh, 14, 1.8f, -170.0f + display.stance * 60, 40, colors[display.stance], display.stanceAlpha * 0.65f);
		}
		const Rml::Colourb healthColor{240, 115, 115}, armorColor{135, 215, 155};
		Arc(mesh, 18, 0.8f, 100, 75, healthColor, display.healthAlpha * 0.12f);
		Arc(mesh, 18, 0.8f, 100, 75 * display.health, healthColor, display.healthAlpha * 0.55f);
		Arc(mesh, 18, 0.8f, 5, 75, armorColor, display.armorAlpha * 0.12f);
		Arc(mesh, 18, 0.8f, 80 - 75 * display.armor, 75 * display.armor, armorColor, display.armorAlpha * 0.55f);
		geometry = GetRenderManager()->MakeGeometry(std::move(mesh));
		geometry.Render(GetAbsoluteOffset());
	}
public:
	explicit ResourceRings(const Rml::String& tag) : Rml::Element(tag) {}
	void SetDisplay(const ReticleHud::Display& value, float scale) { display = value; pixelScale = scale; }
};

Rml::ElementInstancerGeneric<ResourceRings> resourceInstancer;
ReticleHud::Activity activity;
ReticleSystem systemInterface;
ReticleRenderer renderInterface;
Rml::Context* context = nullptr;
Rml::ElementDocument* document = nullptr;
Rml::Element* dot = nullptr;
ResourceRings* resources = nullptr;
bool initialized = false;
cvar_t* enabled = nullptr;
cvar_t* scale = nullptr;
cvar_t* hudEnabled = nullptr;

} // namespace

void CL_RmlUiShutdown() {
	if (initialized) Rml::Shutdown();
	context = nullptr;
	document = nullptr;
	dot = nullptr;
	resources = nullptr;
	activity.Reset();
	initialized = false;
}

void CL_RmlUiInit() {
	CL_RmlUiShutdown();
	enabled = Cvar_Get("cg_rmluiReticle", "1", CVAR_ARCHIVE);
	scale = Cvar_Get("cg_rmluiReticleScale", "0.75", CVAR_ARCHIVE);
	hudEnabled = Cvar_Get("cg_rmluiHud", "1", CVAR_ARCHIVE);
	Rml::SetSystemInterface(&systemInterface);
	Rml::SetRenderInterface(&renderInterface);
	initialized = Rml::Initialise();
	if (initialized) {
		Rml::Factory::RegisterElementInstancer("resource-rings", &resourceInstancer);
		context = Rml::CreateContext("reticle", {cls.glconfig.vidWidth, cls.glconfig.vidHeight});
		if (context) document = context->LoadDocumentFromMemory(reticleRml);
	}
	if (document) {
		dot = document->GetElementById("dot");
		resources = static_cast<ResourceRings*>(document->GetElementById("resources"));
	}
	if (!document || !dot || !resources) {
		Com_Printf("RmlUi: reticle initialization failed; using legacy crosshair\n");
		CL_RmlUiShutdown();
		return;
	}
	document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
	Com_Printf("RmlUi: reticle ready (6.3)\n");
}

int CL_RmlUiDrawReticle(float x, float y, float size, const float* color, const reticleHudState_t* state) {
	if (!document || !enabled->integer || !re.DrawUiGeometry) { activity.Reset(); return 0; }
	if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(size) || size <= 0) return qfalse;
	const int width = cls.glconfig.vidWidth, height = cls.glconfig.vidHeight;
	if (width <= 0 || height <= 0) return qfalse;
	const float uiScale = std::isfinite(scale->value) ? std::max(0.25f, std::min(4.0f, scale->value)) : 0.75f;
	const float pixels = size * height / 480.0f * uiScale;
	context->SetDimensions({width, height});
	document->SetProperty(Rml::PropertyId::Left, Rml::Property(x * width / 640.0f - pixels * 0.5f, Rml::Unit::PX));
	document->SetProperty(Rml::PropertyId::Top, Rml::Property(y * height / 480.0f - pixels * 0.5f, Rml::Unit::PX));
	document->SetProperty(Rml::PropertyId::Width, Rml::Property(pixels, Rml::Unit::PX));
	document->SetProperty(Rml::PropertyId::Height, Rml::Property(pixels, Rml::Unit::PX));
	Rml::Colourb tint;
	for (int c = 0; c < 4; ++c)
		tint[c] = byte(255 * (std::isfinite(color[c]) ? std::max(0.0f, std::min(1.0f, color[c])) : 1.0f));
	tint.alpha = byte(tint.alpha * 0.65f);
	dot->SetProperty(Rml::PropertyId::BackgroundColor, Rml::Property(tint, Rml::Unit::COLOUR));
	const bool drawHud = state && hudEnabled->integer;
	if (!drawHud) activity.Reset();
	resources->SetDisplay(drawHud ? activity.Update(*state, systemInterface.GetElapsedTime()) : ReticleHud::Display(),
		height / 480.0f * uiScale);
	context->Update();
	context->Render();
	return ReticleHud::DotDrawn | (drawHud ? ReticleHud::ResourcesDrawn : 0);
}
