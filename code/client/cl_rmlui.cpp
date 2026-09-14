// SPDX-License-Identifier: GPL-2.0-or-later
#include <RmlUi/Core.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/ElementText.h>
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
		// Larger documents must split meshes at the renderer's command limits.
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
		const auto& geometry = *reinterpret_cast<Geometry*>(handle);
		polyVert_t vertices[REF_UI_MAX_VERTICES] = {};
		for (size_t i = 0; i < geometry.vertices.size(); ++i) {
			const auto& source = geometry.vertices[i];
			auto& vertex = vertices[i];
			vertex.xyz[0] = (source.position.x + translation.x) * 640.0f / cls.glconfig.vidWidth;
			vertex.xyz[1] = (source.position.y + translation.y) * 480.0f / cls.glconfig.vidHeight;
			vertex.st[0] = source.tex_coord.x;
			vertex.st[1] = source.tex_coord.y;
			for (int c = 0; c < 4; ++c) vertex.modulate[c] = source.colour[c];
		}
		re.DrawUiGeometry(static_cast<int>(geometry.vertices.size()), vertices,
			static_cast<int>(geometry.indices.size()), geometry.indices.data(), scissor ? clip : nullptr, qhandle_t(texture));
	}
	void EnableScissorRegion(bool enable) override { scissor = enable; }
	void SetScissorRegion(Rml::Rectanglei region) override {
		clip[0] = region.Left(); clip[1] = region.Top();
		clip[2] = region.Width(); clip[3] = region.Height();
	}
	Rml::TextureHandle LoadTexture(Rml::Vector2i&, const Rml::String&) override { return 0; }
	Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> data, Rml::Vector2i size) override {
		if (size.x <= 0 || size.y <= 0 || data.size() != size_t(size.x) * size.y * 4) return 0;
		const qhandle_t texture = re.CreateUiTexture(size.x, size.y, data.data());
		if (!texture) Com_Printf("RmlUi: cannot upload font texture %dx%d\n", size.x, size.y);
		return Rml::TextureHandle(texture);
	}
	void ReleaseTexture(Rml::TextureHandle texture) override { re.ReleaseUiTexture(qhandle_t(texture)); }
};

class RadialElement : public Rml::Element {
protected:
	Rml::Geometry geometry;
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
public:
	explicit RadialElement(const Rml::String& tag) : Rml::Element(tag) {}
};

class ResourceRings final : public RadialElement {
	ReticleHud::Display display;
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
	explicit ResourceRings(const Rml::String& tag) : RadialElement(tag) {}
	void SetDisplay(const ReticleHud::Display& value, float scale) { display = value; pixelScale = scale; }
};

class SelectionWheelElement final : public RadialElement {
	RadialWheel::View view;
	float opacity = 1;
	void OnRender() override {
		const int count = RadialWheel::Count(view.available, view.slotCount);
		if (!count) return;
		Rml::Mesh mesh;
		const float step = 360.0f / count;
		const bool weapon = view.kind == RadialWheel::Kind::Weapon;
		const Rml::Colourb accent = weapon ? Rml::Colourb(240, 215, 145) : Rml::Colourb(100, 180, 240);
		const Rml::Colourb edge = weapon ? Rml::Colourb(245, 225, 175) : Rml::Colourb(160, 205, 240);
		for (int sector = 0; sector < count; ++sector) {
			const bool selected = RadialWheel::Slot(view.available, sector, view.slotCount) == RadialWheel::Highlighted(view);
			const float start = -90 + sector * step - step / 2 + 1.5f;
			Arc(mesh, RadialWheel::Radius, 44, start, step - 3,
				selected ? accent : Rml::Colourb(15, 20, 25), (selected ? 0.28f : 0.4f) * opacity);
			Arc(mesh, RadialWheel::Radius, 0.8f, start, step - 3, edge, (selected ? 0.7f : 0.25f) * opacity);
		}
		const size_t cursorStart = mesh.vertices.size();
		if (view.pointer) Arc(mesh, 2, 2, 0, 360, {255, 255, 255}, 0.65f * opacity);
		for (size_t i = cursorStart; i < mesh.vertices.size(); ++i)
			mesh.vertices[i].position += Rml::Vector2f(view.x, view.y) * pixelScale;
		geometry = GetRenderManager()->MakeGeometry(std::move(mesh));
		geometry.Render(GetAbsoluteOffset());
	}
public:
	explicit SelectionWheelElement(const Rml::String& tag) : RadialElement(tag) {}
	void SetView(const RadialWheel::View& value, float scale, float alpha) {
		view = value; pixelScale = scale; opacity = alpha;
	}
};

Rml::ElementInstancerGeneric<SelectionWheelElement> wheelInstancer;
Rml::ElementInstancerGeneric<ResourceRings> resourceInstancer;
ReticleHud::Activity activity;
ReticleSystem systemInterface;
ReticleRenderer renderInterface;
Rml::Context* context = nullptr;
Rml::ElementDocument* document = nullptr;
Rml::Element* dot = nullptr;
ResourceRings* resources = nullptr;
Rml::Context* wheelContext = nullptr;
Rml::ElementDocument* wheelDocument = nullptr;
SelectionWheelElement* wheelElement = nullptr;
Rml::Element* wheelLabel = nullptr;
Rml::ElementText* wheelLabelText = nullptr;
void* fontData = nullptr;
bool fontReady = false;
bool initialized = false;
cvar_t* enabled = nullptr;
cvar_t* scale = nullptr;
cvar_t* hudEnabled = nullptr;

} // namespace

void CL_RmlUiShutdown() {
	CL_SelectionWheelsCancel();
	if (initialized) Rml::Shutdown();
	if (fontData) FS_FreeFile(fontData);
	fontData = nullptr;
	fontReady = false;
	context = nullptr;
	document = nullptr;
	dot = nullptr;
	resources = nullptr;
	wheelContext = nullptr;
	wheelDocument = nullptr;
	wheelElement = nullptr;
	wheelLabel = nullptr;
	wheelLabelText = nullptr;
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
		const int fontSize = FS_ReadFile("ui/fonts/plex/IBMPlexMono-Regular.ttf", &fontData);
		fontReady = fontSize > 0 && Rml::LoadFontFace({static_cast<const Rml::byte*>(fontData), size_t(fontSize)},
			"IBM Plex Mono", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal);
		Com_Printf(fontReady ? "RmlUi: IBM Plex Mono loaded\n" : "RmlUi: IBM Plex Mono missing; selection wheels unavailable\n");
		Rml::Factory::RegisterElementInstancer("resource-rings", &resourceInstancer);
		Rml::Factory::RegisterElementInstancer("selection-wheel", &wheelInstancer);
		context = Rml::CreateContext("reticle", {cls.glconfig.vidWidth, cls.glconfig.vidHeight});
		if (context) document = context->LoadDocumentFromMemory(reticleRml);
		wheelContext = Rml::CreateContext("selection-wheel", {cls.glconfig.vidWidth, cls.glconfig.vidHeight});
		if (wheelContext) wheelDocument = wheelContext->LoadDocumentFromMemory(R"(
<rml><head><style>
body { margin: 0; width: 100%; height: 100%; }
selection-wheel { position: absolute; left: 50%; top: 50%; width: 0; height: 0; }
#selection-name { position: absolute; font-family: IBM Plex Mono; color: #e5ecf2; text-align: center; line-height: 120%; }
</style></head><body><selection-wheel id="wheel"/><div id="selection-name">Force</div></body></rml>)");
	}
	if (document) {
		dot = document->GetElementById("dot");
		resources = static_cast<ResourceRings*>(document->GetElementById("resources"));
	}
	if (wheelDocument) {
		wheelElement = static_cast<SelectionWheelElement*>(wheelDocument->GetElementById("wheel"));
		wheelLabel = wheelDocument->GetElementById("selection-name");
		if (wheelLabel) wheelLabelText = static_cast<Rml::ElementText*>(wheelLabel->GetFirstChild());
	}
	if (!document || !dot || !resources || !wheelElement || !wheelLabelText) {
		Com_Printf("RmlUi: reticle initialization failed; using legacy crosshair\n");
		CL_RmlUiShutdown();
		return;
	}
	document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
	wheelDocument->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
	Com_Printf("RmlUi: reticle ready (6.3)\n");
}

int CL_RmlUiDrawReticle(float x, float y, float size, const float* color, const reticleHudState_t* state) {
	if (!document || !enabled->integer || !re.DrawUiGeometry) { activity.Reset(); return 0; }
	if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(size) || size <= 0) return qfalse;
	const int width = cls.glconfig.vidWidth, height = cls.glconfig.vidHeight;
	if (width <= 0 || height <= 0) return qfalse;
	if (CL_ForceWheelActive()) return ReticleHud::DotDrawn | (state && hudEnabled->integer ? ReticleHud::ResourcesDrawn : 0);
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

bool CL_RmlUiAvailable() { return wheelElement && fontReady; }

void CL_RmlUiDrawSelectionWheel(const RadialWheel::View& view, const char* label, float opacity) {
	if (!CL_RmlUiAvailable()) return;
	wheelContext->SetDimensions({cls.glconfig.vidWidth, cls.glconfig.vidHeight});
	const float pixelScale = cls.glconfig.vidHeight / 480.0f;
	wheelElement->SetView(view, pixelScale, opacity);
	const float labelWidth = std::round(120 * pixelScale);
	wheelLabel->SetProperty(Rml::PropertyId::Width, Rml::Property(labelWidth, Rml::Unit::PX));
	wheelLabel->SetProperty(Rml::PropertyId::Left, Rml::Property(std::round((cls.glconfig.vidWidth - labelWidth) / 2), Rml::Unit::PX));
	wheelLabel->SetProperty(Rml::PropertyId::Top, Rml::Property(std::round(cls.glconfig.vidHeight / 2.0f + 18 * pixelScale), Rml::Unit::PX));
	const float fontSize = view.kind == RadialWheel::Kind::Weapon ? 12 : 14;
	wheelLabel->SetProperty(Rml::PropertyId::FontSize, Rml::Property(std::round(fontSize * pixelScale), Rml::Unit::PX));
	wheelLabel->SetProperty(Rml::PropertyId::Opacity, Rml::Property(opacity, Rml::Unit::NUMBER));
	// Stock Western StringEd labels use single-byte characters, not UTF-8.
	Rml::String text;
	for (const unsigned char* p = reinterpret_cast<const unsigned char*>(label); p && *p; ++p)
		text += Rml::StringUtilities::ToUTF8(Rml::Character(*p));
	wheelLabelText->SetText(text);
	wheelContext->Update();
	wheelContext->Render();
}
