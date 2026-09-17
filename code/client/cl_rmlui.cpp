// SPDX-License-Identifier: GPL-2.0-or-later
#include <RmlUi/Core.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/RenderManager.h>
#include <RmlUi/Core/FontEngineInterface.h>
#include <RmlUi/Core/FontEffectInstancer.h>
#include <RmlUi/Core/FontEffect.h>
#include "client.h"
#include "selection_menu.h"
#include <algorithm>
#include <cmath>
#include <map>

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
	void SetClipboardText(const Rml::String& text) override { Sys_SetClipboardData(text.c_str()); }
	void GetClipboardText(Rml::String& text) override {
		char* data = Sys_GetClipboardData();
		text = data ? data : "";
		if (data) Z_Free(data);
	}
	bool LogMessage(Rml::Log::Type, const Rml::String& message) override {
		Com_Printf("RmlUi: %s\n", message.c_str());
		return true;
	}
};

class GameFiles final : public Rml::FileInterface {
	struct File { void* bytes; size_t size, position; };
public:
	Rml::FileHandle Open(const Rml::String& path) override {
		void* data = nullptr;
		const int length = FS_ReadFile(path.c_str(), &data);
		return length < 0 ? 0 : reinterpret_cast<Rml::FileHandle>(new File{data, size_t(length), 0});
	}
	void Close(Rml::FileHandle handle) override {
		auto* file = reinterpret_cast<File*>(handle);
		FS_FreeFile(file->bytes);
		delete file;
	}
	size_t Read(void* buffer, size_t size, Rml::FileHandle handle) override {
		auto& file = *reinterpret_cast<File*>(handle);
		const size_t count = std::min(size, file.size - file.position);
		memcpy(buffer, static_cast<byte*>(file.bytes) + file.position, count);
		file.position += count;
		return count;
	}
	bool Seek(Rml::FileHandle handle, long offset, int origin) override {
		auto& file = *reinterpret_cast<File*>(handle);
		if (origin != SEEK_SET && origin != SEEK_CUR && origin != SEEK_END) return false;
		const auto position = int64_t(origin == SEEK_SET ? 0 : origin == SEEK_CUR ? file.position : file.size) + offset;
		if (position < 0 || uint64_t(position) > file.size) return false;
		file.position = size_t(position);
		return true;
	}
	size_t Tell(Rml::FileHandle handle) override { return reinterpret_cast<File*>(handle)->position; }
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
		return reinterpret_cast<Rml::CompiledGeometryHandle>(new Geometry{vertices, indices});
	}
	void ReleaseGeometry(Rml::CompiledGeometryHandle handle) override {
		delete reinterpret_cast<Geometry*>(handle);
	}
	void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation,
		Rml::TextureHandle texture) override {
		const auto& geometry = *reinterpret_cast<Geometry*>(handle);
		polyVert_t vertices[REF_UI_MAX_VERTICES] = {};
		auto convert = [&](polyVert_t& vertex, const Rml::Vertex& source) {
			vertex.xyz[0] = (source.position.x + translation.x) * 640.0f / cls.glconfig.vidWidth;
			vertex.xyz[1] = (source.position.y + translation.y) * 480.0f / cls.glconfig.vidHeight;
			vertex.st[0] = source.tex_coord.x;
			vertex.st[1] = source.tex_coord.y;
			for (int c = 0; c < 4; ++c) vertex.modulate[c] = source.colour[c];
		};
		if (geometry.vertices.size() <= REF_UI_MAX_VERTICES && geometry.indices.size() <= REF_UI_MAX_INDICES) {
			for (size_t i = 0; i < geometry.vertices.size(); ++i) convert(vertices[i], geometry.vertices[i]);
			re.DrawUiGeometry(static_cast<int>(geometry.vertices.size()), vertices,
				static_cast<int>(geometry.indices.size()), geometry.indices.data(), scissor ? clip : nullptr, qhandle_t(texture));
			return;
		}
		// Long text can exceed one command. Expand complete triangles in bounded batches.
		constexpr int batchSize = (REF_UI_MAX_VERTICES / 3) * 3;
		int indices[batchSize];
		for (int i = 0; i < batchSize; ++i) indices[i] = i;
		for (size_t first = 0; first < geometry.indices.size(); first += batchSize) {
			const int count = int(std::min(size_t(batchSize), geometry.indices.size() - first));
			for (int i = 0; i < count; ++i) convert(vertices[i], geometry.vertices[geometry.indices[first + i]]);
			re.DrawUiGeometry(count, vertices, count, indices, scissor ? clip : nullptr, qhandle_t(texture));
		}
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
GameFiles fileInterface;
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
void* labelFontData = nullptr;
void* datapadFontData = nullptr;
bool datapadFontReady = false;
int wheelOutlineWidth = -1;
bool fontReady = false;
bool initialized = false;
std::map<int, Rml::FontEffectList> textOutlines;
std::map<Rml::FontFaceHandle, float> fontInkCenters;
cvar_t* enabled = nullptr;
cvar_t* scale = nullptr;
cvar_t* hudEnabled = nullptr;

} // namespace

void CL_RmlUiShutdown() {
	CL_AtmosphereEditorShutdown();
	CL_RmlSelectionShutdown();
	CL_CancelHudReveal();
	CL_SelectionWheelsCancel();
	textOutlines.clear();
	fontInkCenters.clear();
	if (initialized) Rml::Shutdown();
	if (fontData) FS_FreeFile(fontData);
	if (labelFontData) FS_FreeFile(labelFontData);
	if (datapadFontData) FS_FreeFile(datapadFontData);
	fontData = nullptr;
	labelFontData = nullptr;
	datapadFontData = nullptr;
	datapadFontReady = false;
	wheelOutlineWidth = -1;
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
	Rml::SetFileInterface(&fileInterface);
	Rml::SetRenderInterface(&renderInterface);
	initialized = Rml::Initialise();
	if (initialized) {
		const int fontSize = FS_ReadFile("ui/fonts/plex/IBMPlexMono-Regular.ttf", &fontData);
		fontReady = fontSize > 0 && Rml::LoadFontFace({static_cast<const Rml::byte*>(fontData), size_t(fontSize)},
			"IBM Plex Mono", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal);
		const int labelFontSize = FS_ReadFile("ui/fonts/plex/IBMPlexMono-SemiBold.ttf", &labelFontData);
		fontReady = fontReady && labelFontSize > 0 && Rml::LoadFontFace({static_cast<const Rml::byte*>(labelFontData), size_t(labelFontSize)},
			"IBM Plex Mono", Rml::Style::FontStyle::Normal, static_cast<Rml::Style::FontWeight>(600));
		const int datapadFontSize = FS_ReadFile("ui/fonts/plex/IBMPlexSans-SemiBold.ttf", &datapadFontData);
		datapadFontReady = datapadFontSize > 0 && Rml::LoadFontFace({static_cast<const Rml::byte*>(datapadFontData), size_t(datapadFontSize)},
			"IBM Plex Sans", Rml::Style::FontStyle::Normal, static_cast<Rml::Style::FontWeight>(600));
		Com_Printf(datapadFontReady ? "RmlUi: IBM Plex Sans loaded\n" : "RmlUi: IBM Plex Sans missing; using legacy datapad text\n");
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
#selection-name { position: absolute; font-family: IBM Plex Mono; font-weight: 600;
 color: #edf0ef; text-align: center; line-height: 130%; }
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
	CL_RmlSelectionInit();
	Com_Printf("RmlUi: reticle ready (6.3)\n");
	CL_AtmosphereEditorInit();
}

int CL_RmlUiDrawReticle(float x, float y, float size, const float* color, const reticleHudState_t* state) {
	if (!document || !enabled->integer || !re.DrawUiGeometry) { activity.Reset(); return 0; }
	if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(size) || size <= 0) return qfalse;
	const int width = cls.glconfig.vidWidth, height = cls.glconfig.vidHeight;
	if (width <= 0 || height <= 0) return qfalse;
	if (CL_SelectionWheelActive()) return ReticleHud::DotDrawn | (state && hudEnabled->integer ? ReticleHud::ResourcesDrawn : 0);
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
	const bool drawHud = state && (hudEnabled->integer || CL_HudRevealActive());
	if (!drawHud) activity.Reset();
	resources->SetDisplay(drawHud ? activity.Update(*state, systemInterface.GetElapsedTime(), CL_HudRevealActive()) : ReticleHud::Display(),
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
	const float labelWidth = std::round(144 * pixelScale);
	wheelLabel->SetProperty(Rml::PropertyId::Width, Rml::Property(labelWidth, Rml::Unit::PX));
	wheelLabel->SetProperty(Rml::PropertyId::Left, Rml::Property(std::round((cls.glconfig.vidWidth - labelWidth) / 2), Rml::Unit::PX));
	wheelLabel->SetProperty(Rml::PropertyId::Top, Rml::Property(std::round(cls.glconfig.vidHeight / 2.0f + 18 * pixelScale), Rml::Unit::PX));
	const float fontSize = view.kind == RadialWheel::Kind::Weapon ? 12 : 14;
	wheelLabel->SetProperty(Rml::PropertyId::FontSize, Rml::Property(std::round(fontSize * pixelScale), Rml::Unit::PX));
	wheelLabel->SetProperty(Rml::PropertyId::LetterSpacing, Rml::Property(std::round(0.5f * pixelScale), Rml::Unit::PX));
	const int outlineWidth = std::max(1, int(std::round(0.55f * pixelScale)));
	if (outlineWidth != wheelOutlineWidth) {
		wheelLabel->SetProperty("font-effect", va("outline(%dpx rgba(0, 0, 0, 75%%))", outlineWidth));
		wheelOutlineWidth = outlineWidth;
	}
	wheelLabel->SetProperty(Rml::PropertyId::Opacity, Rml::Property(opacity, Rml::Unit::NUMBER));
	// Stock Western StringEd labels use single-byte characters, not UTF-8.
	Rml::String text;
	for (const unsigned char* p = reinterpret_cast<const unsigned char*>(label); p && *p; ++p)
		text += Rml::StringUtilities::ToUTF8(Rml::Character(UiText::Codepoint(*p)));
	wheelLabelText->SetText(text);
	wheelContext->Update();
	wheelContext->Render();
}

bool CL_RmlUiText(const char* text, const UiText::Style& style, UiText::Metrics* metrics, bool draw) {
	if (!(style.datapad ? datapadFontReady : fontReady) || !context || re.Language_IsAsian() ||
		((Key_GetCatcher() & KEYCATCH_UI) && !style.datapad) || cls.glconfig.vidWidth <= 0 || cls.glconfig.vidHeight <= 0 ||
		!std::isfinite(style.size) || style.size <= 0) return false;
	const float sx = style.pixels ? 1 : cls.glconfig.vidWidth / 640.0f;
	const float sy = style.pixels ? 1 : cls.glconfig.vidHeight / 480.0f;
	int size = std::max(1, int(std::round(style.size * sy)));
	auto* fonts = Rml::GetFontEngineInterface();
	const auto weight = style.semibold || style.datapad ? static_cast<Rml::Style::FontWeight>(600) : Rml::Style::FontWeight::Normal;
	const char* family = style.datapad ? "ibm plex sans" : "ibm plex mono";
	auto face = fonts->GetFontFaceHandle(family, Rml::Style::FontStyle::Normal, weight, size);
	if (!face) return false;
	if (style.datapad) {
		// Fit the complete font metrics inside the existing menu row height.
		const float rowHeight = style.size * sy;
		while (size > 1) {
			const auto& m = fonts->GetFontMetrics(face);
			if (std::max(m.line_spacing, m.ascent + m.descent) <= rowHeight) break;
			--size;
			face = fonts->GetFontFaceHandle(family, Rml::Style::FontStyle::Normal, weight, size);
			if (!face) return false;
		}
	}
	const Rml::String language;
	const Rml::TextShapingContext shaping{language};
	const auto& fontMetrics = fonts->GetFontMetrics(face);
	const float lineHeight = style.datapad ? style.size * sy : std::ceil(fontMetrics.line_spacing);
	const float advance = float(fonts->GetStringWidth(face, "M", shaping));
	const float maxWidth = style.maxWidth < 0 ? -1 : style.maxWidth * sx;
	auto measure = [&](unsigned char prior, unsigned char c) {
		const auto glyph = Rml::StringUtilities::ToUTF8(Rml::Character(UiText::Codepoint(c)));
		const float natural = float(fonts->GetStringWidth(face, glyph, shaping));
		const float width = prior ? float(fonts->GetStringWidth(face, glyph, shaping, Rml::Character(UiText::Codepoint(prior)))) : natural;
		return UiText::Advance{width, natural};
	};
	const auto layout = style.datapad ? UiText::ArrangeMeasured(text, measure, lineHeight, maxWidth, style.wrap, style.forceColor) :
		UiText::Arrange(text, advance, lineHeight, maxWidth, style.wrap, style.forceColor);
	if (metrics) {
		metrics->width = layout.metrics.width / sx;
		metrics->height = style.datapad ? style.size * std::max(1, int(std::round(layout.metrics.height / lineHeight))) :
			std::max(lineHeight, layout.metrics.height) / sy;
	}
	if (!draw || style.color[3] <= 0 || (style.blink && ((Sys_Milliseconds() >> 7) & 1))) return true;
	std::vector<Rml::String> strings;
	for (const auto& run : layout.runs) {
		Rml::String utf8;
		for (unsigned char c : run.text) utf8 += Rml::StringUtilities::ToUTF8(Rml::Character(UiText::Codepoint(c)));
		// Populate all glyphs before generating geometry, so an atlas cannot change mid-string.
		fonts->GetStringWidth(face, utf8, shaping);
		strings.push_back(std::move(utf8));
	}
	Rml::FontEffectsHandle effects = 0;
	if (style.outline && !style.datapad) {
		const int width = std::max(1, int(std::round(cls.glconfig.vidHeight / 480.0f * 0.55f)));
		auto& list = textOutlines[width];
		if (list.empty()) {
			auto* instancer = Rml::Factory::GetFontEffectInstancer("outline");
			Rml::PropertyDictionary properties;
			instancer->GetPropertySpecification().ParsePropertyDeclaration(properties, "width", va("%dpx", width));
			instancer->GetPropertySpecification().ParsePropertyDeclaration(properties, "color", "rgba(0, 0, 0, 75%)");
			list.push_back(instancer->InstanceFontEffect("outline", properties));
		}
		effects = fonts->PrepareFontEffects(face, list);
	}
	auto& manager = context->GetRenderManager();
	manager.PrepareRender({cls.glconfig.vidWidth, cls.glconfig.vidHeight});
	float baseline = fontMetrics.ascent;
	if (style.datapad) {
		auto found = fontInkCenters.find(face);
		if (found == fontInkCenters.end()) {
			fonts->GetStringWidth(face, "H", shaping);
			Rml::TexturedMeshList reference;
			fonts->GenerateString(manager, face, 0, "H", {}, Rml::Colourb(255, 255, 255).ToPremultiplied(), 1, shaping, reference);
			float top = 0, bottom = 0;
			bool first = true;
			for (const auto& mesh : reference) for (const auto& vertex : mesh.mesh.vertices) {
				if (first) { top = bottom = vertex.position.y; first = false; }
				top = std::min(top, vertex.position.y);
				bottom = std::max(bottom, vertex.position.y);
			}
			found = fontInkCenters.emplace(face, (top + bottom) / 2).first;
		}
		const float targetCenter = style.legacyFont ? re.Font_VisualCenter(style.legacyFont, style.legacyScale) * sy : lineHeight / 2;
		baseline = targetCenter - found->second;
	}
	for (size_t i = 0; i < layout.runs.size(); ++i) {
		const auto& run = layout.runs[i];
		const float* rgb = run.color < 0 ? style.color : g_color_table[run.color];
		Rml::Colourb color;
		for (int c = 0; c < 3; ++c) color[c] = byte(std::max(0.0f, std::min(1.0f, rgb[c])) * 255);
		color.alpha = byte(std::max(0.0f, std::min(1.0f, style.color[3])) * 255);
		Rml::TexturedMeshList meshes;
		fonts->GenerateString(manager, face, effects, strings[i],
			{std::round(style.x * sx + run.x), std::round(style.y * sy + run.y + baseline)},
			color.ToPremultiplied(), color.alpha / 255.0f, shaping, meshes);
		for (auto& mesh : meshes) {
			auto geometry = manager.MakeGeometry(std::move(mesh.mesh));
			geometry.Render({}, mesh.texture);
		}
	}
	manager.ResetState();
	return true;
}
