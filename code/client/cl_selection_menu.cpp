// SPDX-License-Identifier: GPL-2.0-or-later
#include <RmlUi/Core.h>
#include <RmlUi/Core/ElementInstancer.h>
#include "client.h"
#include "selection_menu.h"
#include "../qcommon/stringed_ingame.h"
#include "../qcommon/qfiles.h"
#include <algorithm>
#include <vector>

namespace {
Rml::Context* context = nullptr;
Rml::ElementDocument* document = nullptr;
float scale = 1, left = 0, top = 0;
float cursorX = 320, cursorY = 240;
int screenWidth = 640, screenHeight = 480;
int powerFocus = 0, weaponFocus = 3;
SelectionMenu::Page lastPage = SelectionMenu::Closed;
bool lastHelp = false, suspended = false;
std::vector<Rml::Element*> buttons;

// Retain shader stages, additive blending, and animated scanlines from the retail UI.
class ShaderElement : public Rml::Element {
	Rml::String source;
	qhandle_t shader = 0;
	void OnRender() override {
		const auto path = GetAttribute<Rml::String>("src", "");
		if (path.empty()) return;
		if (path != source) { source = path; shader = re.RegisterShaderNoMip(path.c_str()); }
		if (!shader) return;
		const auto position = GetAbsoluteOffset(), size = GetBox().GetSize(Rml::BoxArea::Content);
		if (size.x <= 0 || size.y <= 0) return;
		const bool outside = GetAttribute<bool>("outside", false);
		const float x = std::max(position.x, outside ? 0.0f : left);
		const float y = std::max(position.y, outside ? 0.0f : top);
		const float r = std::min(position.x + size.x, outside ? float(screenWidth) : left + 640 * scale);
		const float b = std::min(position.y + size.y, outside ? float(screenHeight) : top + 480 * scale);
		if (r <= x || b <= y) return;
		const auto color = GetProperty<Rml::Colourb>("color");
		float rgba[4];
		for (int i = 0; i < 4; ++i) rgba[i] = color[i] / 255.0f;
		re.SetColor(rgba);
		re.DrawStretchPic(x * 640 / screenWidth, y * 480 / screenHeight, (r - x) * 640 / screenWidth, (b - y) * 480 / screenHeight,
			(x - position.x) / size.x, (y - position.y) / size.y,
			(r - position.x) / size.x, (b - position.y) / size.y, shader);
		re.SetColor(nullptr);
	}
public:
	explicit ShaderElement(const Rml::String& tag) : Rml::Element(tag) {}
};

// The distinctive display face remains native. All other menu text uses Plex through RmlUi.
class StarWarsText : public Rml::Element {
	qhandle_t font = 0;
	void OnRender() override {
		if (!font) font = re.RegisterFont("anewhope");
		const auto text = GetAttribute<Rml::String>("value", "");
		const auto position = GetAbsoluteOffset(), size = GetBox().GetSize(Rml::BoxArea::Content);
		const auto color = GetProperty<Rml::Colourb>("color");
		float rgba[4];
		for (int i = 0; i < 4; ++i) rgba[i] = color[i] / 255.0f;
		const int width = re.Font_StrLenPixels(text.c_str(), font, scale);
		re.Font_DrawString(int(position.x + (size.x - width) / 2), int(position.y), text.c_str(), rgba, font | STYLE_PIXEL, int(size.x), scale);
		re.SetColor(nullptr);
	}
public:
	explicit StarWarsText(const Rml::String& tag) : Rml::Element(tag) {}
};
Rml::ElementInstancerGeneric<ShaderElement> shaderInstancer;
Rml::ElementInstancerGeneric<StarWarsText> displayFontInstancer;

Rml::Element* Element(const char* id) { return document->GetElementById(id); }
void Show(Rml::Element* element, bool show) { element->SetProperty("display", show ? "block" : "none"); }
void Color(Rml::Element* element, float r, float g, float b) {
	element->SetProperty(Rml::PropertyId::Color, Rml::Property(Rml::Colourb(byte(r * 255), byte(g * 255), byte(b * 255)), Rml::Unit::COLOUR));
}
Rml::String Utf8(const char* text) {
	Rml::String result;
	for (const auto* p = reinterpret_cast<const unsigned char*>(text); p && *p; ++p)
		result += Rml::StringUtilities::ToUTF8(Rml::Character(UiText::Codepoint(*p)));
	return result;
}
void Text(const char* id, const char* text) {
	auto* element = Element(id);
	const auto value = Utf8(text);
	if (element->GetAttribute<Rml::String>("data-value", "") != value) {
		element->SetAttribute("data-value", value);
		element->SetInnerRML(Rml::StringUtilities::EncodeRml(value));
	}
}
const char* Local(const char* key) { return SE_GetString(key); }
void Bounds(Rml::Element* element, SelectionMenu::Rect rect) {
	element->SetProperty("left", va("%gdp", rect.x)); element->SetProperty("top", va("%gdp", rect.y));
	element->SetProperty("width", va("%gdp", rect.w)); element->SetProperty("height", va("%gdp", rect.h));
}
Rml::Element* Add(const char* parent, const char* tag, const char* id, SelectionMenu::Rect rect) {
	auto owned = document->CreateElement(tag);
	auto* element = owned.get();
	element->SetId(id);
	Bounds(element, rect);
	element->SetProperty("position", "absolute");
	Element(parent)->AppendChild(std::move(owned));
	return element;
}
void Action(Rml::Element* element, const char* action, int index = -1) {
	element->SetAttribute("data-action", action);
	element->SetAttribute("data-index", index);
	buttons.push_back(element);
}
void Image(const char* id, const char* source) { Element(id)->SetAttribute("src", source); }
void Enabled(Rml::Element* element, bool enabled) {
	if (enabled) element->RemoveAttribute("disabled");
	else element->SetAttribute("disabled", "disabled");
}
const char* WeaponIcon(const SelectionMenu::View& view, int weapon, bool lit) {
	const char* name = view.outcast && weapon == 2 ? "briar" : SelectionMenu::WeaponsData[weapon - 1].icon;
	return va("gfx/hud/w_icon_%s%s", name, lit ? "" : "_na");
}

void BuildGrid() {
	const int iconX[] = {33,184,336,484}, hexX[] = {76,225,380,527};
	const int iconY[] = {30,85,141,196}, hexY[] = {41,95,149,205};
	for (int i = 0; i < 16; ++i) {
		const int c = i / 4, r = i % 4;
		Add("force-grid", "ja-shader", va("power-hex-%d", i), {float(hexX[c]),float(hexY[r]),70,49});
		Add("force-grid", "ja-shader", va("power-icon-%d", i), {float(iconX[c]),float(iconY[r]),64,64});
		auto* button = Add("force-grid", "button", va("power-%d", i), {float(iconX[c]),float(hexY[r]),115,46});
		button->SetClass("grid-button", true);
		Action(button, "upgrade", i);
	}
	for (int w = 1; w <= 13; ++w) {
		const auto& def = SelectionMenu::WeaponsData[w - 1];
		auto* hex = Add("weapon-grid", "ja-shader", va("weapon-hex-%d", w), def.hex);
		hex->SetAttribute("src", "gfx/menus/hex_pattern_gray");
		Add("weapon-grid", "ja-shader", va("weapon-icon-%d", w), def.picture);
		auto* button = Add("weapon-grid", "button", va("weapon-%d", w), def.button);
		button->SetClass("grid-button", true);
		Action(button, "weapon", w);
	}
	const int x[] = {65,152,280,367,500};
	for (int i = 0; i < 5; ++i) {
		auto* hex = Add("chosen-weapons", "ja-shader", va("slot-hex-%d", i), {float(x[i]),358,70,107});
		hex->SetAttribute("src", "gfx/menus/hex_pattern_gray");
		Add("chosen-weapons", "ja-shader", va("slot-icon-%d", i), {float(x[i] + (i == 4 ? 0 : 2)),358,60,60});
		if (i >= 2) {
			auto* button = Add("chosen-weapons", "button", va("slot-%d", i), {float(x[i] + 2),358,52,64});
			button->SetClass("grid-button", true);
			Action(button, "slot", i - 2);
		}
	}
	for (const char* id : {"force-help", "force-undo", "force-next", "weapon-help", "weapon-back", "weapon-begin", "help-close"})
		buttons.push_back(Element(id));
}

void Details(const SelectionMenu::View& view) {
	if (view.page == SelectionMenu::Force) {
		const auto& power = SelectionMenu::Powers[powerFocus];
		Image("force-preview", va("gfx/mp/NEW_f_icon_%s", power.icon));
		Text("force-description", Local(va("SP_INGAME_FORCE_%s_DESC", power.description)));
		const int rank = std::max(1, std::min(3, view.levels[powerFocus]));
		Text("force-level", Local(va("SP_INGAME_FORCE_%s_LVL%d_DESC", power.description, rank)));
		Text("force-hint", view.outcast && !view.editable[powerFocus] ? "This power advances with the story." :
			view.levels[powerFocus] > view.original[powerFocus] ? Local("MENUS_REMOVEFP") : view.editable[powerFocus] && view.remaining && rank < 3 ? Local("MENUS_ADDFP") : "");
		Enabled(Element("force-undo"), !view.help && view.levels[powerFocus] > view.original[powerFocus]);
	} else {
		Image("weapon-preview", WeaponIcon(view, weaponFocus, false));
		Text("weapon-description", Local(va("SP_INGAME_%s_DESC", view.outcast && weaponFocus == 2 ? "BLASTER_PISTOL" : SelectionMenu::WeaponsData[weaponFocus - 1].description)));
		bool selected = false;
		for (int weapon : view.selected) selected |= weapon == weaponFocus;
		Text("weapon-hint", weaponFocus <= 2 ? "" : Local(selected ? "MENUS_CLICKREMOVE" : "MENUS_CLICKSELECT"));
	}
}

void Sync(const SelectionMenu::View& view) {
	Show(Element("force-page"), view.page == SelectionMenu::Force);
	Show(Element("weapon-page"), view.page == SelectionMenu::Weapons);
	Show(Element("help-panel"), view.help);
	Element("force-title")->SetClass("points", view.outcast);
	Text("force-title", view.outcast ? Cvar_VariableString("ui_jo_prep_points") : Local(view.remaining ? "MENUS_ALLOCATE_FP" : "MENUS_ALLOCATED_FP"));
	Text("weapon-title", Local(view.ready ? "MENUS_WEAPONSCHOSEN" : "MENUS_CHOOSEWEAPONS"));
	Text("force-help", Local("MENUS_HELP")); Text("weapon-help", Local("MENUS_HELP"));
	Text("force-next", Local("MENUS_WEAPONS")); Text("weapon-begin", Local("MENUS_BEGIN_MISSION"));
	Text("force-undo", "Undo upgrade"); Text("weapon-back", "Force Powers");
	Show(Element("weapon-back"), view.outcast);
	Show(Element("force-undo"), view.outcast);
	Enabled(Element("force-next"), !view.help && view.canLeaveForce);
	Enabled(Element("weapon-begin"), !view.help && view.ready);
	for (const char* id : {"force-help", "weapon-help", "weapon-back"}) Enabled(Element(id), !view.help);
	for (int i = 0; i < 16; ++i) {
		const bool chosen = view.levels[i] > view.original[i], focused = i == powerFocus;
		Image(va("power-hex-%d", i), va("gfx/menus/hex_pattern_%d%s", std::max(0, std::min(3, view.levels[i])), chosen ? "_gold" : ""));
		Image(va("power-icon-%d", i), va("gfx/mp/NEW_f_icon_%s", SelectionMenu::Powers[i].icon));
		const float tint = chosen || focused ? 1.0f : !view.outcast && !view.remaining && view.editable[i] ? 0.25f : 0.65f;
		for (const char* part : {"hex", "icon"}) Color(Element(va("power-%s-%d", part, i)), tint, tint, tint);
		Enabled(Element(va("power-%d", i)), !view.help);
	}
	for (int w = 1; w <= 13; ++w) {
		bool selected = false;
		for (int choice : view.selected) selected |= choice == w;
		for (const char* part : {"hex", "icon"}) Show(Element(va("weapon-%s-%d", part, w)), view.available[w] || w <= 2);
		Show(Element(va("weapon-%d", w)), view.available[w] || w <= 2);
		Image(va("weapon-icon-%d", w), WeaponIcon(view, w, weaponFocus == w && view.available[w]));
		Color(Element(va("weapon-icon-%d", w)), view.available[w] ? 1 : .25f, view.available[w] ? 1 : .25f, view.available[w] ? 1 : .25f);
		const float bright = selected ? 1.0f : .5f;
		Color(Element(va("weapon-hex-%d", w)), 0, w <= 2 ? 1 : w < 10 || w == 13 ? bright : 0, w <= 2 ? 1 : w >= 10 && w <= 12 ? bright : 0);
		Enabled(Element(va("weapon-%d", w)), !view.help);
	}
	for (int i = 0; i < 5; ++i) {
		const int weapon = i < 2 ? i + 1 : view.selected[i - 2];
		Show(Element(va("slot-icon-%d", i)), weapon > 0 && view.available[weapon]);
		if (weapon > 0) Image(va("slot-icon-%d", i), WeaponIcon(view, weapon, false));
		Color(Element(va("slot-hex-%d", i)), 0, i < 4 ? 1 : 0, i < 2 || i == 4 ? 1 : 0);
		if (i >= 2) {
			Enabled(Element(va("slot-%d", i)), !view.help && weapon > 0);
			Element(va("slot-%d", i))->SetAttribute("data-weapon", weapon);
		}
	}
	Text("help-title", Local("MENUS_HELP"));
	const bool force = view.page == SelectionMenu::Force;
	Text("help-text", view.outcast && force ?
		"JO powers advance with the story. Allocate points to Absorb, Protect, Rage, Drain, or Sense. Each rank costs one point. Seven points become available during the campaign. Click a power to add a rank. Use Undo upgrade to remove a change made on this screen. Unspent points carry forward." :
		Local(force ? "MENUS_FORCE_UPGRADE_HELP" : "MENUS_WEAPON_SELECTION_HELP"));
	Element("help-okay")->SetAttribute("value", Local("MENUS_OKAY"));
	Details(view);
}

Rml::Element* ActionElement(Rml::Element* target) {
	while (target && !target->HasAttribute("data-action")) target = target->GetParentNode();
	return target;
}
struct Listener : Rml::EventListener {
	void ProcessEvent(Rml::Event& event) override {
		auto* target = ActionElement(event.GetTargetElement());
		if (!target || target->HasAttribute("disabled")) return;
		const auto action = target->GetAttribute<Rml::String>("data-action", "");
		int index = target->GetAttribute<int>("data-index", -1);
		if (event.GetType() == "mouseover" || event.GetType() == "focus") {
			if (action == "upgrade") powerFocus = index;
			if (action == "weapon") weaponFocus = index;
			if (action == "slot" && target->GetAttribute<int>("data-weapon", 0) > 0) weaponFocus = target->GetAttribute<int>("data-weapon", 0);
			if (event.GetType() == "mouseover") target->Focus();
			else UI_SelectionAction("focus");
			if (document) Details(UI_SelectionView());
		} else if (event.GetType() == "click") {
			if (action == "slot") { index = target->GetAttribute<int>("data-weapon", 0); UI_SelectionAction("weapon", index); }
			else UI_SelectionAction(action.c_str(), action == "undo" ? powerFocus : index);
		}
	}
} listener;

void Viewport() {
	const int w = cls.glconfig.vidWidth, h = cls.glconfig.vidHeight;
	if (w <= 0 || h <= 0) return;
	cursorX *= float(w) / screenWidth; cursorY *= float(h) / screenHeight;
	screenWidth = w; screenHeight = h;
	scale = std::min(w / 640.0f, h / 480.0f);
	left = (w - 640 * scale) / 2; top = (h - 480 * scale) / 2;
	context->SetDimensions({w, h});
	context->SetDensityIndependentPixelRatio(scale);
}
bool Active() { return context && document && UI_SelectionView().page != SelectionMenu::Closed; }
bool InputAllowed() { return !(Key_GetCatcher() & KEYCATCH_CONSOLE) && !Cvar_VariableIntegerValue("com_unfocused") && !Cvar_VariableIntegerValue("com_minimized"); }
void FocusStep(int direction) {
	std::vector<Rml::Element*> available;
	for (auto* button : buttons) if (button->IsVisible(true) && !button->HasAttribute("disabled")) available.push_back(button);
	if (available.empty()) return;
	auto it = std::find(available.begin(), available.end(), context->GetFocusElement());
	int index = it == available.end() ? (direction > 0 ? -1 : 0) : int(it - available.begin());
	available[(index + direction + int(available.size())) % int(available.size())]->Focus();
}
void Status() {
	const auto view = UI_SelectionView();
	const auto* focus = context ? context->GetFocusElement() : nullptr;
	Com_Printf("rml_selection active=%d campaign=%s page=%d help=%d remaining=%d ready=%d scale=%.4f viewport=%.1f,%.1f,%.1f,%.1f cursor=%.1f,%.1f focus=%s\n",
		Active(), view.outcast ? "jo" : "ja", view.page, view.help, view.remaining, view.ready, scale,
		left, top, 640 * scale, 480 * scale, cursorX, cursorY, focus ? focus->GetId().c_str() : "none");
	Com_Printf("rml_loadout selected=%d,%d,%d\n", view.selected[0], view.selected[1], view.selected[2]);
	for (int i = 0; i < 16; ++i)
		Com_Printf("rml_power name=%s level=%d original=%d editable=%d\n", SelectionMenu::Powers[i].key, view.levels[i], view.original[i], view.editable[i]);
}
}

void CL_RmlSelectionInit() {
	Cvar_Get("ui_rmlSelectionActive", "0", CVAR_ROM);
	Rml::Factory::RegisterElementInstancer("ja-shader", &shaderInstancer);
	Rml::Factory::RegisterElementInstancer("star-wars-text", &displayFontInstancer);
	context = Rml::CreateContext("mission-selection", {cls.glconfig.vidWidth, cls.glconfig.vidHeight});
	if (context) document = context->LoadDocument("ui/rmlui/selection.rml");
	if (document) {
		BuildGrid();
		for (const char* event : {"click", "mouseover", "focus"}) document->AddEventListener(event, &listener, true);
		Viewport();
	}
	Cmd_AddCommand("rml_selection_status", Status);
	Com_Printf(document ? "RmlUi: JA selection menus ready\n" : "RmlUi: selection files unavailable; using legacy menus\n");
}

void CL_RmlSelectionShutdown() {
	cls.cursorActive = qfalse;
	Cmd_RemoveCommand("rml_selection_status");
	buttons.clear();
	document = nullptr;
	if (context) Rml::RemoveContext("mission-selection");
	context = nullptr;
	lastPage = SelectionMenu::Closed;
}

void CL_RmlSelectionReset() {
	cls.cursorActive = qfalse;
	UI_SelectionReset();
	if (document) document->Hide();
	lastPage = SelectionMenu::Closed;
	powerFocus = 0; weaponFocus = 3;
	cursorX = screenWidth / 2.0f; cursorY = screenHeight / 2.0f;
}

bool CL_RmlSelectionAvailable() { return document && context; }

bool CL_RmlSelectionDraw() {
	if (!document || !context) return false;
	const auto view = UI_SelectionView();
	if (view.page == SelectionMenu::Closed) { document->Hide(); lastPage = SelectionMenu::Closed; cls.cursorActive = qfalse; return false; }
	cls.cursorActive = qtrue;
	Viewport();
	document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
	Sync(view);
	Element("selection-cursor")->SetProperty("left", va("%gpx", cursorX));
	Element("selection-cursor")->SetProperty("top", va("%gpx", cursorY));
	Show(Element("selection-cursor"), InputAllowed());
	context->Update();
	if (lastPage != view.page || lastHelp != view.help) {
		if (view.help) Element("help-close")->Focus();
		else Element(view.page == SelectionMenu::Force ? va("power-%d", powerFocus) : va("weapon-%d", weaponFocus))->Focus();
		lastPage = view.page; lastHelp = view.help;
	}
	if (!InputAllowed() && !suspended) { context->ProcessMouseLeave(); suspended = true; }
	if (InputAllowed() && suspended) { context->ProcessMouseMove(int(cursorX), int(cursorY), 0); suspended = false; }
	context->Render();
	return true;
}

bool CL_RmlSelectionMouse(int dx, int dy) {
	if (!Active()) return false;
	if (!InputAllowed()) return true;
	Viewport();
	cursorX = std::max(0.0f, std::min(float(screenWidth), cursorX + dx));
	cursorY = std::max(0.0f, std::min(float(screenHeight), cursorY + dy));
	context->ProcessMouseMove(int(cursorX), int(cursorY), 0);
	return true;
}

bool CL_RmlSelectionKey(int key, bool down) {
	if (!Active() || (Key_GetCatcher() & KEYCATCH_CONSOLE)) return false;
	if (!InputAllowed() || (key & K_CHAR_FLAG)) return true;
	if (key == A_F12) return false;
	if (key == A_MOUSE1 || key == A_MOUSE2) {
		if (down) context->ProcessMouseButtonDown(key == A_MOUSE1 ? 0 : 1, 0);
		else context->ProcessMouseButtonUp(key == A_MOUSE1 ? 0 : 1, 0);
		return true;
	}
	if (!down) return true;
	if (key == A_TAB) FocusStep(Key_IsDown(A_SHIFT) ? -1 : 1);
	else if (key == A_CURSOR_DOWN || key == A_CURSOR_RIGHT) FocusStep(1);
	else if (key == A_CURSOR_UP || key == A_CURSOR_LEFT) FocusStep(-1);
	else if (key == A_ENTER || key == A_KP_ENTER || key == A_SPACE) {
		if (auto* focus = context->GetFocusElement()) focus->Click();
	} else if (key == A_ESCAPE) {
		if (UI_SelectionView().help) UI_SelectionAction("dismiss");
	} else if (key == A_MWHEELUP || key == A_MWHEELDOWN) context->ProcessMouseWheel(Rml::Vector2f(0, key == A_MWHEELUP ? -1.0f : 1.0f), 0);
	return true;
}
