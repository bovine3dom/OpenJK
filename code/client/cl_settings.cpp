// SPDX-License-Identifier: GPL-2.0-or-later
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include "client.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace {
struct Setting {
	std::string name, value, reset, description, group;
	int flags;
	bool binary;
};
Rml::Context* context = nullptr;
Rml::ElementDocument* document = nullptr;
std::vector<Setting> settings;
std::vector<std::string> groups;
std::string group, status = "Changes apply immediately. Reset clears the override.";
bool active = false, reloadDocument = false, ownsPause = false;
float mouseX = 0, mouseY = 0;

const char* style = R"(
body { margin: 0; width: 100%; height: 100%; font-family: IBM Plex Mono; font-size: 14dp; color: #e1e8ed; }
div { display: block; }
#panel { position: absolute; left: 12dp; top: 3%; width: 620dp; max-width: 95%; height: 94%;
 background-color: #111a22ed; border: 1dp #496477; border-radius: 6dp; box-sizing: border-box; }
#header { padding: 12dp; height: 103dp; box-sizing: border-box; }
h1 { display: block; font-size: 21dp; font-weight: 600; margin: 0 0 5dp 0; }
.toolbar { height: 35dp; white-space: nowrap; }
select { display: inline-block; width: 125dp; height: 29dp; vertical-align: middle; color: #ffffff; }
select selectvalue { height: 27dp; margin-right: 23dp; padding: 4dp 6dp; box-sizing: border-box;
 background-color: #091219; border: 1dp #536d7e; }
select selectarrow { width: 23dp; height: 27dp; background-color: #304857; border: 1dp #587b90; }
select:hover selectarrow, select selectarrow:checked { background-color: #486b80; }
select selectbox { width: 180dp; max-height: 360dp; overflow-y: auto; padding: 3dp;
 background-color: #111a22; border: 1dp #587b90; z-index: 2; }
select selectbox option { display: block; padding: 4dp 6dp; background-color: #1d2b36; }
select selectbox option:nth-child(even) { background-color: #182630; }
select selectbox option:hover, select selectbox option:checked { background-color: #486b80; }
#form { position: absolute; top: 103dp; bottom: 79dp; left: 0; right: 0;
 overflow-y: auto; overflow-x: hidden; padding: 0 12dp 10dp; box-sizing: border-box; }
.field { margin: 9dp 0; padding: 8dp; background-color: #1d2b36; border-radius: 4dp; }
.control { height: 29dp; white-space: nowrap; }
.label { display: inline-block; width: 53%; vertical-align: middle; font-weight: 600; overflow: hidden; }
input { display: inline-block; box-sizing: border-box; vertical-align: middle; }
input.text { width: 29%; height: 25dp; padding: 3dp 5dp; background-color: #091219; border: 1dp #536d7e;
 color: #ffffff; caret-color: #ffffff; }
input selection { color: #ffffff; background-color: #416681; }
input.text:focus { border-color: #91d0f2; }
input.checkbox { width: 18dp; height: 18dp; margin-right: 27%; border: 2dp #536d7e; background-color: #091219; }
input.checkbox:hover { border-color: #91d0f2; }
input.checkbox:checked { border: 4dp #0c1720; background-color: #a5cde2; }
button { display: inline-block; padding: 6dp 9dp; margin: 3dp; background-color: #304857; border: 1dp #587b90;
 border-radius: 3dp; font-weight: 600; }
button:hover { background-color: #486b80; }
button:active { background-color: #223440; }
button.reset { width: 70dp; margin: 0 0 0 7dp; padding: 4dp 6dp; }
.help { margin: 5dp 0 2dp; font-size: 12dp; color: #c5d6df; }
.caption { font-size: 11dp; color: #acbac3; }
scrollbarvertical { width: 12dp; }
scrollbarvertical slidertrack { width: 12dp; background-color: #111b23; }
scrollbarvertical sliderbar { width: 12dp; min-height: 30dp; background-color: #5e7d90; border-radius: 3dp; }
#footer { position: absolute; bottom: 0; left: 0; right: 0; height: 79dp; padding: 7dp;
 background-color: #121e28; box-sizing: border-box; }
#status { margin: 4dp; font-size: 12dp; color: #d6e3ec; }
#cursor { position: absolute; width: 8dp; height: 8dp; border: 1dp #071017; background-color: #ffffff;
 border-radius: 4dp; pointer-events: none; }
)";

std::string Escape(const std::string& text) {
	std::string out;
	for (char c : text) {
		if (c == '&') out += "&amp;";
		else if (c == '<') out += "&lt;";
		else if (c == '>') out += "&gt;";
		else if (c == '"') out += "&quot;";
		else if (c == '\'') out += "&#39;";
		else out += c;
	}
	return out;
}
Rml::Element* Element(const std::string& id) { return document ? document->GetElementById(id) : nullptr; }
bool BinaryValue(const char* value) { return value && (!std::strcmp(value, "0") || !std::strcmp(value, "1")); }
bool BooleanDescription(const char* description) {
	std::string text = description ? description : "";
	std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
	return text.find("enable") != std::string::npos || text.find(" toggle") != std::string::npos ||
		text.compare(0, 4, "use ") == 0 || text.compare(0, 6, "allow ") == 0 ||
		text.compare(0, 5, "show ") == 0 || text.compare(0, 5, "cast ") == 0;
}
std::string Prefix(const char* name) {
	const char* separator = std::strchr(name, '_');
	return separator ? std::string(name, separator - name + 1) : "other";
}
void Collect(const cvar_t* cvar) {
	if (!cvar->name || (cvar->flags & (CVAR_ROM | CVAR_INIT | CVAR_SERVER_CREATED)) ||
		((cvar->flags & CVAR_CHEAT) && !Cvar_VariableIntegerValue("sv_cheats"))) return;
	const char* value = cvar->latchedString ? cvar->latchedString : cvar->string;
	const char* description = Cvar_DescriptionString(cvar->name);
	const bool binary = cvar->validate && cvar->integral && cvar->min == 0 && cvar->max == 1;
	settings.push_back({cvar->name, value ? value : "", cvar->resetString ? cvar->resetString : "",
		description, Prefix(cvar->name), cvar->flags, binary ||
			(BinaryValue(cvar->resetString) && BinaryValue(value) && BooleanDescription(description))});
}
bool Less(const std::string& a, const std::string& b) {
	const int order = Q_stricmp(a.c_str(), b.c_str());
	return order < 0 || (order == 0 && a < b);
}
void Scan() {
	settings.clear(); groups.clear(); Cvar_ForEach(Collect);
	std::sort(settings.begin(), settings.end(), [](const Setting& a, const Setting& b) { return Less(a.name, b.name); });
	for (const auto& setting : settings)
		if (std::find(groups.begin(), groups.end(), setting.group) == groups.end()) groups.push_back(setting.group);
	std::sort(groups.begin(), groups.end(), Less);
	if (group.empty() || std::find(groups.begin(), groups.end(), group) == groups.end())
		group = groups.empty() ? "other" : groups.front();
}
size_t GroupCount() { return std::count_if(settings.begin(), settings.end(), [](const Setting& setting) { return setting.group == group; }); }
bool NeedsVideoRestart() {
	return group == "r_" && std::any_of(settings.begin(), settings.end(), [](const Setting& setting) {
		return setting.group == group && (setting.flags & CVAR_LATCH);
	});
}
void QueueReload(const char* message) { status = message; reloadDocument = true; }
void Close();

class Listener final : public Rml::EventListener {
	void ProcessEvent(Rml::Event& event) override {
		if (!active) return;
		auto* target = event.GetTargetElement();
		const std::string id = target->GetId();
		if (event.GetType() == "change" && id == "group") {
			group = static_cast<Rml::ElementFormControlSelect*>(target)->GetValue();
			QueueReload("Group selected.");
			return;
		}
		if (event.GetType() == "change" && id.size() > 1 && id[0] == 'v') {
			const size_t index = std::strtoul(id.c_str() + 1, nullptr, 10);
			if (index < settings.size()) {
				auto* input = static_cast<Rml::ElementFormControlInput*>(target);
				Cvar_SetUser(settings[index].name.c_str(), settings[index].binary ?
					(input->HasAttribute("checked") ? "1" : "0") : input->GetValue().c_str());
				status = settings[index].flags & CVAR_LATCH ? "Change queued. Use vid_restart to apply it." : "Setting changed.";
				if (auto* element = Element("status")) element->SetInnerRML(Escape(status));
			}
			return;
		}
		while (target && target->GetTagName() != "button") target = target->GetParentNode();
		if (!target) return;
		const std::string button = target->GetId();
		if (button == "close") { Close(); return; }
		if (button == "vid_restart") { Close(); Cbuf_AddText("vid_restart\n"); return; }
		if (button == "group_reset") {
			for (const auto& setting : settings) if (setting.group == group) Cvar_Clear(setting.name.c_str());
			Scan(); QueueReload("Group overrides cleared. Restart when required.");
			return;
		}
		if (button.compare(0, 5, "reset") == 0) {
			const size_t index = std::strtoul(button.c_str() + 5, nullptr, 10);
			if (index < settings.size()) {
				Cvar_Clear(settings[index].name.c_str());
				Scan(); QueueReload("Setting override cleared.");
			}
		}
	}
} listener;

std::string Markup() {
	std::ostringstream rml;
	rml << "<rml><head><style>" << style << "</style></head><body><div id='panel'><div id='header'>"
		"<h1>Settings</h1><div class='toolbar'><select id='group'>";
	for (const auto& name : groups)
		rml << "<option value='" << Escape(name) << "'" << (name == group ? " selected" : "") << ">" << Escape(name) << "</option>";
	rml << "</select><button id='group_reset'>Reset " << Escape(group) << "</button>";
	if (NeedsVideoRestart()) rml << "<button id='vid_restart'>vid_restart</button>";
	rml << "</div></div><div id='form'>";
	for (size_t i = 0; i < settings.size(); ++i) {
		const auto& setting = settings[i];
		if (setting.group != group) continue;
		rml << "<div class='field'><div class='control'><span class='label'>" << Escape(setting.name) << "</span>";
		if (setting.binary)
			rml << "<input class='checkbox' type='checkbox' id='v" << i << "'" << (setting.value == "1" ? " checked" : "") << "/>";
		else rml << "<input class='text' type='text' maxlength='255' id='v" << i << "' value='" << Escape(setting.value) << "'/>";
		rml << "<button class='reset' id='reset" << i << "'>Reset</button></div><div class='help'>"
			<< (setting.description.empty() ? "No description is registered for this setting." : Escape(setting.description))
			<< "</div><div class='caption'>Default: " << Escape(setting.reset);
		if (setting.flags & CVAR_LATCH) rml << " | Requires restart";
		if (setting.flags & CVAR_CHEAT) rml << " | Cheat protected";
		if (setting.binary) rml << " | Off / on";
		rml << "</div></div>";
	}
	rml << "</div><div id='footer'><div><button id='close'>Close / Esc</button></div><div id='status'>" << Escape(status)
		<< "</div></div></div><div id='cursor'/></body></rml>";
	return rml.str();
}
bool LoadDocument() {
	if (document) { document->Close(); document = nullptr; context->Update(); }
	document = context->LoadDocumentFromMemory(Markup());
	if (!document) return false;
	document->AddEventListener("click", &listener);
	document->AddEventListener("change", &listener);
	document->Show();
	context->ProcessMouseMove(int(mouseX), int(mouseY), 0);
	return true;
}
void Close() {
	if (!active) return;
	active = false; reloadDocument = false;
	if (document) document->Hide();
	Key_SetCatcher(Key_GetCatcher() & ~KEYCATCH_ATMOSPHERE);
	cl.mouseDx[0] = cl.mouseDx[1] = cl.mouseDy[0] = cl.mouseDy[1] = 0;
	if (ownsPause && Cvar_VariableIntegerValue("cl_paused") && !(Key_GetCatcher() & KEYCATCH_UI)) Cvar_Set("cl_paused", "0");
	ownsPause = false;
}
void Open_f() {
	if (active) { Close(); return; }
	if (cls.state != CA_ACTIVE || (Key_GetCatcher() & KEYCATCH_UI) || CL_AtmosphereEditorActive()) {
		Com_Printf("Settings: open from the console during gameplay after other panels are closed.\n"); return;
	}
	if (!CL_RmlUiAvailable()) { Com_Printf("Settings: RmlUi is unavailable.\n"); return; }
	if (Cmd_Argc() == 2) group = Cmd_Argv(1);
	Scan();
	if (!context) context = Rml::CreateContext("settings", {cls.glconfig.vidWidth, cls.glconfig.vidHeight});
	if (!context) return;
	for (int button = 0; button < 3; ++button) context->ProcessMouseButtonUp(button, 0);
	mouseX = cls.glconfig.vidWidth * .25f; mouseY = cls.glconfig.vidHeight * .25f;
	if (!LoadDocument()) { Com_Printf("Settings: could not load the panel.\n"); return; }
	Con_Close(); CL_SelectionWheelsCancel();
	Key_SetCatcher(Key_GetCatcher() | KEYCATCH_ATMOSPHERE);
	cl.mouseDx[0] = cl.mouseDx[1] = cl.mouseDy[0] = cl.mouseDy[1] = 0;
	ownsPause = Cvar_VariableIntegerValue("sv_running") && !Cvar_VariableIntegerValue("cl_paused");
	if (ownsPause) Cvar_Set("cl_paused", "1");
	active = true;
	Com_Printf("Settings: group=%s settings=%zu\n", group.c_str(), GroupCount());
}
void Status_f() {
	Com_Printf("settings active=%d group=%s settings=%zu paused=%d catcher=%d cursor=%.0f,%.0f\n", active,
		group.c_str(), GroupCount(), Cvar_VariableIntegerValue("cl_paused"), Key_GetCatcher(), mouseX, mouseY);
	if (!active || Cmd_Argc() != 2) return;
	std::string id = Cmd_Argv(1);
	Rml::Element* requested = nullptr;
	if (id.compare(0, 7, "option:") == 0) {
		auto* select = static_cast<Rml::ElementFormControlSelect*>(Element("group"));
		const std::string value = id.substr(7);
		for (int i = 0; i < select->GetNumOptions(); ++i) {
			auto* option = select->GetOption(i);
			if (option->GetAttribute<Rml::String>("value", "") == value) { requested = option; break; }
		}
	}
	const bool reset = id.compare(0, 6, "reset:") == 0;
	if (reset) id.erase(0, 6);
	if (!requested && id != "form" && id != "status" && id != "group" && id != "group_reset" && id != "vid_restart" && id != "close" && id != "cursor") {
		const auto found = std::find_if(settings.begin(), settings.end(), [&](const Setting& setting) { return !Q_stricmp(setting.name.c_str(), id.c_str()); });
		if (found == settings.end()) return;
		id = std::string(reset ? "reset" : "v") + std::to_string(found - settings.begin());
	}
	if (auto* element = requested ? requested : Element(id)) {
		const auto position = element->GetAbsoluteOffset(Rml::BoxArea::Border);
		const auto size = element->GetBox().GetSize(Rml::BoxArea::Border);
		Com_Printf("settings element=%s rect=%.2f,%.2f,%.2f,%.2f\n", Cmd_Argv(1), position.x, position.y, size.x, size.y);
		if (element->GetTagName() == "input") Com_Printf("settings value=%s checked=%d\n",
			static_cast<Rml::ElementFormControlInput*>(element)->GetValue().c_str(), element->HasAttribute("checked"));
	}
}
int Modifiers() {
	return (Key_IsDown(A_SHIFT) ? Rml::Input::KM_SHIFT : 0) | (Key_IsDown(A_CTRL) ? Rml::Input::KM_CTRL : 0) |
		(Key_IsDown(A_ALT) ? Rml::Input::KM_ALT : 0);
}
Rml::Input::KeyIdentifier Key(int key) {
	using namespace Rml::Input;
	if (key >= A_CAP_A && key <= A_CAP_Z) return KeyIdentifier(KI_A + key - A_CAP_A);
	if (key >= A_LOW_A && key <= A_LOW_Z) return KeyIdentifier(KI_A + key - A_LOW_A);
	if (key >= A_0 && key <= A_9) return KeyIdentifier(KI_0 + key - A_0);
	switch (key) {
	case A_TAB: return KI_TAB;
	case A_ENTER: case A_KP_ENTER: return KI_RETURN;
	case A_BACKSPACE: return KI_BACK;
	case A_DELETE: return KI_DELETE;
	case A_CURSOR_LEFT: return KI_LEFT;
	case A_CURSOR_RIGHT: return KI_RIGHT;
	case A_CURSOR_UP: return KI_UP;
	case A_CURSOR_DOWN: return KI_DOWN;
	case A_HOME: return KI_HOME;
	case A_END: return KI_END;
	case A_PAGE_UP: return KI_PRIOR;
	case A_PAGE_DOWN: return KI_NEXT;
	default: return KI_UNKNOWN;
	}
}
} // namespace

void CL_SettingsInit() { Cmd_AddCommand("settings", Open_f); Cmd_AddCommand("settings_status", Status_f); }
void CL_SettingsShutdown() {
	Close();
	if (context) Rml::RemoveContext("settings");
	context = nullptr; document = nullptr;
	Cmd_RemoveCommand("settings"); Cmd_RemoveCommand("settings_status");
}
bool CL_SettingsActive() { return active; }
bool CL_SettingsKey(int key, bool down) {
	if (!active || (Key_GetCatcher() & (KEYCATCH_CONSOLE | KEYCATCH_UI))) return false;
	if (key == A_ESCAPE) { if (down) Close(); return true; }
	if (key == A_MOUSE1 || key == A_MOUSE2 || key == A_MOUSE3) {
		const int button = key == A_MOUSE1 ? 0 : key == A_MOUSE2 ? 1 : 2;
		if (down) context->ProcessMouseButtonDown(button, Modifiers());
		else context->ProcessMouseButtonUp(button, Modifiers());
	} else if (key == A_MWHEELUP || key == A_MWHEELDOWN) {
		if (down) context->ProcessMouseWheel(Rml::Vector2f(0, key == A_MWHEELUP ? -1.0f : 1.0f), Modifiers());
	} else if (Key(key) != Rml::Input::KI_UNKNOWN) {
		if (down) context->ProcessKeyDown(Key(key), Modifiers());
		else context->ProcessKeyUp(Key(key), Modifiers());
	}
	return true;
}
bool CL_SettingsChar(int key) {
	if (!active || (Key_GetCatcher() & (KEYCATCH_CONSOLE | KEYCATCH_UI))) return false;
	if (key >= 32) context->ProcessTextInput(Rml::Character(key));
	return true;
}
bool CL_SettingsMouse(int dx, int dy) {
	if (!active) return false;
	if (Key_GetCatcher() & (KEYCATCH_CONSOLE | KEYCATCH_UI)) return true;
	mouseX = std::max(0.0f, std::min(float(cls.glconfig.vidWidth - 1), mouseX + dx));
	mouseY = std::max(0.0f, std::min(float(cls.glconfig.vidHeight - 1), mouseY + dy));
	context->ProcessMouseMove(int(mouseX), int(mouseY), Modifiers());
	return true;
}
void CL_SettingsDraw() {
	if (!active) return;
	if (cls.state != CA_ACTIVE || (Key_GetCatcher() & KEYCATCH_UI)) { Close(); return; }
	context->SetDimensions({cls.glconfig.vidWidth, cls.glconfig.vidHeight});
	context->SetDensityIndependentPixelRatio(std::max(.75f, std::min(1.5f, cls.glconfig.vidHeight / 800.0f)));
	if (reloadDocument) { reloadDocument = false; if (!LoadDocument()) { Close(); return; } }
	if (auto* cursor = Element("cursor")) {
		cursor->SetProperty("left", va("%gpx", mouseX)); cursor->SetProperty("top", va("%gpx", mouseY));
	}
	context->Update(); context->Render();
}
