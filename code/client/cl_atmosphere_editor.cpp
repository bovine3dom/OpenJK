// SPDX-License-Identifier: GPL-2.0-or-later
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include "client.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <sstream>

namespace {
using namespace Atmosphere;
Rml::Context* context = nullptr;
Rml::ElementDocument* document = nullptr;
Profile draft = {};
bool active = false, updating = false, ownsPause = false;
float mouseX = 0, mouseY = 0;
std::string filename, sourcePath;

const char* style = R"(
body { margin: 0; width: 100%; height: 100%; font-family: IBM Plex Mono; font-size: 14dp; color: #e1e8ed; }
div { display: block; }
#panel { position: absolute; left: 12dp; top: 3%; width: 540dp; max-width: 95%; height: 94%;
 background-color: #111a22ed; border: 1dp #496477; border-radius: 6dp; box-sizing: border-box; }
#header { padding: 12dp; height: 103dp; box-sizing: border-box; }
h1 { display: block; font-size: 21dp; font-weight: 600; margin: 0 0 5dp 0; }
#file { font-size: 12dp; color: #afd9f2; word-break: break-all; }
#form { position: absolute; top: 103dp; bottom: 143dp; left: 0; right: 0;
 overflow-y: auto; overflow-x: hidden; padding: 0 12dp 10dp; box-sizing: border-box; }
.field { margin: 9dp 0; padding: 8dp; background-color: #1d2b36; border-radius: 4dp; }
.label { font-weight: 600; display: inline-block; width: 81%; }
.unit { font-size: 11dp; color: #a7bbc9; }
.control { height: 29dp; white-space: nowrap; }
.channel { display: inline-block; width: 20dp; vertical-align: middle; }
input { display: inline-block; box-sizing: border-box; vertical-align: middle; }
input.text { width: 99dp; height: 25dp; padding: 3dp 5dp; background-color: #091219; border: 1dp #536d7e;
 color: #ffffff; caret-color: #ffffff; }
input selection { color: #ffffff; background-color: #416681; }
input.text:focus { border-color: #91d0f2; }
input.bad { border-color: #fa8066; }
input.range { width: 65%; height: 25dp; margin-right: 7dp; }
input.range slidertrack { height: 15dp; margin-top: 5dp; background-color: #0c1720; border-radius: 3dp; }
input.range sliderbar { width: 12dp; height: 25dp; background-color: #a5cde2; border-radius: 3dp; }
input.range sliderbar:hover { background-color: #def3ff; }
input.range sliderprogress { background-color: #42728c; }
sliderarrowdec, sliderarrowinc { width: 0; height: 0; }
scrollbarvertical { width: 12dp; }
scrollbarvertical slidertrack { width: 12dp; background-color: #111b23; }
scrollbarvertical sliderbar { width: 12dp; min-height: 30dp; background-color: #5e7d90; border-radius: 3dp; }
button { display: inline-block; padding: 6dp 9dp; margin: 3dp; background-color: #304857; border: 1dp #587b90;
 border-radius: 3dp; font-weight: 600; }
button:hover { background-color: #486b80; }
button:active { background-color: #223440; }
button.help-button { padding: 1dp 7dp; margin: 0; }
.help { display: none; margin: 5dp 0; font-size: 12dp; color: #c5d6df; }
#text_file { word-break: break-all; }
.swatch { display: inline-block; width: 40dp; height: 10dp; border: 1dp #9ba9ad; vertical-align: middle; }
.caption { font-size: 11dp; color: #acbac3; }
#footer { position: absolute; bottom: 0; left: 0; right: 0; height: 143dp; padding: 7dp;
 background-color: #121e28; box-sizing: border-box; }
#apply { background-color: #286757; }
#status { margin: 4dp; font-size: 12dp; color: #d6e3ec; }
#hint { font-size: 11dp; color: #a1b6c4; margin: 4dp; }
#compass { position: relative; width: 90dp; height: 90dp; margin: 6dp; border: 1dp #7193a8; border-radius: 45dp; }
#sun-dot { position: absolute; width: 7dp; height: 7dp; background-color: #ffe49b; border-radius: 4dp; margin-left: -3dp; margin-top: -3dp; }
#sun-readout { font-size: 11dp; }
.density-label { display: inline-block; width: 50dp; font-size: 11dp; }
.density-track { display: inline-block; width: 160dp; height: 10dp; background-color: #071017; }
.ray-density { height: 5dp; background-color: #739dd9; }
.mie-density { height: 5dp; background-color: #d5b776; }
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
		else out += c;
	}
	return out;
}

std::string Number(float value) { return va("%.6g", double(value)); }
Rml::Element* Element(const std::string& id) { return document ? document->GetElementById(id) : nullptr; }
Rml::ElementFormControlInput* Input(const std::string& id) {
	return static_cast<Rml::ElementFormControlInput*>(Element(id));
}
std::string Id(char type, int field, int component) { return va("%c_%d_%d", type, field, component); }
void Text(const std::string& id, const std::string& value) {
	if (auto* e = Element(id)) {
		const auto escaped = Escape(value);
		if (e->GetInnerRML() != escaped) e->SetInnerRML(escaped);
	}
}
void Status(const std::string& message) { Text("status", message); }

float SliderValue(int field, float position) {
	const auto& f = Fields()[field];
	const float t = std::max(0.0f, std::min(position / 1000.0f, 1.0f));
	if (!f.logarithmic) return f.minimum + t * (f.maximum - f.minimum);
	if (f.minimum == 0 && t == 0) return 0;
	const float low = f.minimum > 0 ? f.minimum : 0.00001f;
	return std::exp(std::log(low) + t * std::log(f.maximum / low));
}

float SliderPosition(int field, float value) {
	const auto& f = Fields()[field];
	if (!f.logarithmic) return 1000 * (value - f.minimum) / (f.maximum - f.minimum);
	if (value <= 0) return 0;
	const float low = f.minimum > 0 ? f.minimum : 0.00001f;
	return std::max(0.0f, std::min(1000.0f, 1000 * std::log(value / low) / std::log(f.maximum / low)));
}

bool ReadNumber(int field, int component, float& result) {
	auto* input = Input(Id('n', field, component));
	const std::string value = input->GetValue();
	char* end;
	result = strtof(value.c_str(), &end);
	const bool parsed = end != value.c_str();
	while (*end && std::isspace(static_cast<unsigned char>(*end))) ++end;
	const auto& f = Fields()[field];
	const bool valid = parsed && !*end && std::isfinite(result) && result >= f.minimum && result <= f.maximum;
	input->SetClass("bad", !valid);
	return valid;
}

void Visuals() {
	for (int field : {Rayleigh, Mie, Absorption, GroundAlbedo, CloudColor}) {
		const float* v = draft.values[field];
		const float maximum = std::max(0.00001f, std::max(v[0], std::max(v[1], v[2])));
		Element(va("swatch_%d", field))->SetProperty("background-color", va("rgb(%d,%d,%d)",
			int(255*v[0]/maximum), int(255*v[1]/maximum), int(255*v[2]/maximum)));
	}
	const float* sun = draft.values[SunDirection];
	const float length = std::sqrt(sun[0]*sun[0] + sun[1]*sun[1] + sun[2]*sun[2]);
	if (length > 0) {
		Element("sun-dot")->SetProperty("left", va("%g%%", 50 + 45 * sun[0] / length));
		Element("sun-dot")->SetProperty("top", va("%g%%", 50 - 45 * sun[1] / length));
		float azimuth = std::atan2(sun[1], sun[0]) * 180 / float(M_PI);
		if (azimuth < 0) azimuth += 360;
		Text("sun-readout", va("Azimuth %.1f deg | Elevation %.1f deg", azimuth,
			std::asin(std::max(-1.0f, std::min(1.0f, sun[2]/length))) * 180 / M_PI));
	} else Text("sun-readout", "Direction is zero. Set a positive Z component.");
	for (int height : {0, 1, 5, 10}) {
		Element(va("ray_%d", height))->SetProperty("width", va("%g%%", 100 * std::exp(-height / draft.values[RayHeight][0])));
		Element(va("mie_%d", height))->SetProperty("width", va("%g%%", 100 * std::exp(-height / draft.values[MieHeight][0])));
	}
}

void FillControls() {
	updating = true;
	Input("sky")->SetValue(draft.sky);
	for (int i = 0; i < Count; ++i)
		for (int c = 0; c < Fields()[i].components; ++c) {
			Input(Id('n', i, c))->SetValue(Number(draft.values[i][c]));
			Input(Id('n', i, c))->SetClass("bad", false);
			Input(Id('s', i, c))->SetValue(Number(SliderPosition(i, draft.values[i][c])));
		}
	updating = false;
	Visuals();
}

bool ReadControls() {
	Profile candidate = draft;
	const std::string sky = Input("sky")->GetValue();
	if (sky.empty() || sky.size() >= sizeof(candidate.sky) || sky.find_first_not_of(
		"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_/.-") != std::string::npos) {
		Status("Enter a valid sky material name from this map."); return false;
	}
	Q_strncpyz(candidate.sky, sky.c_str(), sizeof(candidate.sky));
	for (int i = 0; i < Count; ++i)
		for (int c = 0; c < Fields()[i].components; ++c)
			if (!ReadNumber(i, c, candidate.values[i][c])) {
				Status(va("%s must be between %g and %g.", Fields()[i].name, Fields()[i].minimum, Fields()[i].maximum));
				return false;
			}
	if (!Valid(candidate)) { Status("Sun direction needs positive Z and length greater than 0.5."); return false; }
	draft = candidate;
	return true;
}

void ResolveFilename() {
	filename = std::string("scripts/maps/") + draft.map + ".atmosphere";
	sourcePath = std::string("maps/") + draft.map + ".atmosphere";
	char osPath[MAX_OSPATH];
	if (!FS_GetFileReadPath(sourcePath.c_str(), osPath, sizeof(osPath))) return;
	std::error_code error;
	auto resolved = std::filesystem::canonical(osPath, error);
	if (error) return;
	sourcePath = resolved.string();
	auto maps = std::filesystem::canonical(std::filesystem::path(osPath).parent_path(), error);
	if (error) return;
	auto relative = resolved.lexically_relative(maps).generic_string();
	if (!relative.empty() && relative.compare(0, 3, "../") != 0 && relative.find_first_not_of(
		"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_/.-") == std::string::npos)
		filename = "scripts/maps/" + relative;
}

std::string FileHelp() {
	return "Apply changes the runtime preview only. Export target: " + filename +
		". Active source: " + sourcePath + ". Existing local overrides take priority after reload and build updates.";
}

std::string Export() {
	std::string text = "// File: " + filename + "\n// Edited on map: " + draft.map + "\natmosphere 1\nsky " + draft.sky + "\n";
	for (int i = 0; i < Count; ++i) {
		text += Fields()[i].name;
		for (int c = 0; c < Fields()[i].components; ++c) text += " " + Number(draft.values[i][c]);
		text += "\n";
	}
	return text;
}

void Close() {
	if (!active) return;
	active = false;
	if (document) document->Hide();
	Key_SetCatcher(Key_GetCatcher() & ~KEYCATCH_ATMOSPHERE);
	cl.mouseDx[0] = cl.mouseDx[1] = cl.mouseDy[0] = cl.mouseDy[1] = 0;
	if (ownsPause && Cvar_VariableIntegerValue("cl_paused") && !(Key_GetCatcher() & KEYCATCH_UI))
		Cvar_Set("cl_paused", "0");
	ownsPause = false;
}

class Listener final : public Rml::EventListener {
	void ProcessEvent(Rml::Event& event) override {
		if (!active || updating) return;
		auto* target = event.GetTargetElement();
		if (event.GetType() == "change") {
			const std::string id = target->GetId();
			if (id == "sky") { Status("Unapplied sky material change. Apply checks the binding."); return; }
			int field, component;
			char kind;
			if (std::sscanf(id.c_str(), "%c_%d_%d", &kind, &field, &component) != 3 ||
				field < 0 || field >= Count || component < 0 || component >= Fields()[field].components) return;
			updating = true;
			float value = draft.values[field][component];
			if (kind == 's') {
				value = SliderValue(field, std::strtof(Input(id)->GetValue().c_str(), nullptr));
				Input(Id('n', field, component))->SetValue(Number(value));
			}
			if (ReadNumber(field, component, value)) {
				draft.values[field][component] = value;
				if (kind == 'n') Input(Id('s', field, component))->SetValue(Number(SliderPosition(field, value)));
				Visuals();
			}
			updating = false;
			Status("Unapplied changes. Apply rebuilds the sky preview.");
			return;
		}
		while (target && target->GetTagName() != "button") target = target->GetParentNode();
		if (!target) return;
		const auto id = target->GetId();
		if (id == "close") { Close(); return; }
		if (id.compare(0, 5, "help_") == 0) {
			auto* help = Element("text_" + id.substr(5));
			const bool show = !help->IsClassSet("expanded");
			help->SetClass("expanded", show);
			help->SetProperty("display", show ? "block" : "none");
		} else if (id == "apply" && ReadControls()) {
			if (re.ApplyAtmosphere && re.ApplyAtmosphere(&draft)) {
				Cvar_Set("r_atmosphere", "1");
				Cvar_Set("r_compareEnhancements", "0");
				Status("Applied in memory. Copy exports it; no file was written.");
			} else Status("Cannot apply: check the current map and its cube sky material.");
		} else if (id == "toggle") {
			Cvar_SetValue("r_atmosphere", !Cvar_VariableIntegerValue("r_atmosphere"));
			Cvar_Set("r_compareEnhancements", "0");
			Status("Switched the last applied sky. Draft edits still need Apply.");
		} else if (id == "copy" && ReadControls()) {
			ResolveFilename();
			Text("file", filename);
			Text("text_file", FileHelp());
			const bool copied = Sys_SetClipboardData(Export().c_str());
			Status(copied ? "Copied filename and complete profile to clipboard." : "Clipboard unavailable. Check the desktop clipboard service.");
			Com_Printf("Atmosphere editor: %s %s\n", copied ? "copied" : "clipboard failed for", filename.c_str());
		} else if (id == "reload") {
			Cmd_ExecuteString("r_atmosphereReload");
			if (re.GetAtmosphere && re.GetAtmosphere(&draft)) {
				ResolveFilename(); FillControls(); Text("file", filename); Text("text_file", FileHelp());
				Status("Reloaded the file. Draft edits were reset.");
			} else Status("No valid file profile. The previous draft is still available.");
		}
	}
} listener;

std::string Markup() {
	std::ostringstream rml;
	rml << "<rml><head><style>" << style << "</style></head><body><div id='panel'><div id='header'>"
		"<h1>Atmosphere editor</h1><div>Map: " << Escape(draft.map) << " | Format: atmosphere 1</div>"
		"<div id='file'>" << Escape(filename) << "</div></div><div id='form'>"
		"<div class='field'><span class='label'>Sky material</span><button class='help-button' id='help_file'>?</button>"
		"<input id='sky' type='text' maxlength='63' style='width:100%;'/>"
		"<div class='caption'>Cube sky material from this map. Format 1 is the file version.</div>"
		"<div class='help' id='text_file'>" << Escape(FileHelp()) << "</div></div>";
	for (int i : {SkyBlend, Illuminance, Rayleigh, Mie, Absorption, CloudStrength, CloudColor,
		GroundAlbedo, SunDirection, Anisotropy, SunDisk, SunRadius, ObserverHeight,
		RayHeight, MieHeight, Thickness, Radius}) {
		const auto& field = Fields()[i];
		rml << "<div class='field'><span class='label'>" << field.label << " <span class='unit'>" << field.unit
			<< "</span></span><button class='help-button' id='help_" << i << "'>?</button>"
			"<div class='help' id='text_" << i << "'>" << Escape(field.help) << " Range: " << field.minimum << " to " << field.maximum
			<< (field.logarithmic ? ". The slider uses a logarithmic scale." : ".") << "</div>";
		for (int c = 0; c < field.components; ++c) {
			const char* channel = field.components == 1 ? "" : (i == SunDirection ? "XYZ" : "RGB");
			rml << "<div class='control'><span class='channel'>" << (field.components == 1 ? ' ' : channel[c]) << "</span>"
				"<input type='range' min='0' max='1000' step='1' id='" << Id('s', i, c) << "'/>"
				"<input type='text' maxlength='24' id='" << Id('n', i, c) << "'/></div>";
		}
		if (field.components == 3 && i != SunDirection)
			rml << "<div class='swatch' id='swatch_" << i << "'/><span class='caption'> Relative RGB balance</span>";
		if (i == SunDirection)
			rml << "<div id='compass'><span>N (+Y)</span><div id='sun-dot'/></div><div id='sun-readout'/>"
				"<div class='caption'>East (+X) is right. Center is overhead.</div>";
		if (i == MieHeight) {
			rml << "<div class='caption'>Density: blue = Rayleigh, gold = aerosol</div>";
			for (int height : {0, 1, 5, 10})
				rml << "<div><span class='density-label'>" << height << " km</span><div class='density-track'>"
					"<div class='ray-density' id='ray_" << height << "'/><div class='mie-density' id='mie_" << height << "'/></div></div>";
		}
		rml << "</div>";
	}
	rml << "</div><div id='footer'><div><button id='apply'>Apply preview</button><button id='toggle'>Show stock</button>"
		"<button id='close'>Close / Esc</button></div><div><button id='copy'>Copy file + filename</button>"
		"<button id='reload'>Reset from file</button></div><div id='status'>Ready.</div>"
		"<div id='hint'>Apply enables a temporary preview. Copy exports the draft. Shared filenames affect their aliases when saved.</div>"
		"</div></div><div id='cursor'/></body></rml>";
	return rml.str();
}

void Open_f() {
	if (active) { Close(); return; }
	if (cls.state != CA_ACTIVE || (Key_GetCatcher() & KEYCATCH_UI) || CL_SettingsActive()) {
		Com_Printf("Atmosphere editor: open from the console during gameplay after other panels are closed.\n"); return;
	}
	if (!CL_RmlUiAvailable() || !re.GetAtmosphere || !re.ApplyAtmosphere || !re.GetAtmosphere(&draft)) {
		Com_Printf("Atmosphere editor: requires RmlUi, Rend2, and a valid atmosphere profile for this map.\n"); return;
	}
	ResolveFilename();
	if (!context) context = Rml::CreateContext("atmosphere-editor", {cls.glconfig.vidWidth, cls.glconfig.vidHeight});
	if (!context) return;
	if (document) { document->Close(); document = nullptr; context->Update(); }
	for (int button = 0; button < 3; ++button) context->ProcessMouseButtonUp(button, 0);
	document = context->LoadDocumentFromMemory(Markup());
	if (!document) { Com_Printf("Atmosphere editor: could not load the panel.\n"); return; }
	document->AddEventListener("click", &listener);
	document->AddEventListener("change", &listener);
	FillControls();
	Con_Close();
	CL_SelectionWheelsCancel();
	Key_SetCatcher(Key_GetCatcher() | KEYCATCH_ATMOSPHERE);
	cl.mouseDx[0] = cl.mouseDx[1] = cl.mouseDy[0] = cl.mouseDy[1] = 0;
	ownsPause = Cvar_VariableIntegerValue("sv_running") && !Cvar_VariableIntegerValue("cl_paused");
	if (ownsPause) Cvar_Set("cl_paused", "1");
	active = true;
	mouseX = cls.glconfig.vidWidth * .25f; mouseY = cls.glconfig.vidHeight * .25f;
	document->Show();
	context->ProcessMouseMove(int(mouseX), int(mouseY), 0);
	Com_Printf("Atmosphere editor: map=%s export=%s source=%s\n", draft.map, filename.c_str(), sourcePath.c_str());
}

void Status_f() {
	Com_Printf("atmosphere_editor active=%d map=%s file=%s paused=%d catcher=%d\n", active,
		draft.map, filename.c_str(), Cvar_VariableIntegerValue("cl_paused"), Key_GetCatcher());
	if (!active) return;
	Profile applied = {};
	if (re.GetAtmosphere && re.GetAtmosphere(&applied))
		Com_Printf("atmosphere_editor draft_light=%g applied_light=%g enabled=%d\n",
			draft.values[Illuminance][0], applied.values[Illuminance][0], Cvar_VariableIntegerValue("r_atmosphere"));
	if (Cmd_Argc() == 2) {
		if (auto* element = Element(Cmd_Argv(1))) {
			const auto position = element->GetAbsoluteOffset(Rml::BoxArea::Border);
			const auto size = element->GetBox().GetSize(Rml::BoxArea::Border);
			Com_Printf("atmosphere_editor element=%s rect=%.2f,%.2f,%.2f,%.2f\n",
				Cmd_Argv(1), position.x, position.y, size.x, size.y);
			if (!strcmp(Cmd_Argv(1), "status"))
				Com_Printf("atmosphere_editor status=%s\n", element->GetInnerRML().c_str());
		}
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

void CL_AtmosphereEditorInit() {
	Cmd_AddCommand("atmosphere_editor", Open_f);
	Cmd_AddCommand("atmosphere_editor_status", Status_f);
}
void CL_AtmosphereEditorShutdown() {
	Close();
	if (context) Rml::RemoveContext("atmosphere-editor");
	context = nullptr; document = nullptr;
	Cmd_RemoveCommand("atmosphere_editor");
	Cmd_RemoveCommand("atmosphere_editor_status");
}
bool CL_AtmosphereEditorActive() { return active; }
bool CL_AtmosphereEditorKey(int key, bool down) {
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
bool CL_AtmosphereEditorChar(int key) {
	if (!active || (Key_GetCatcher() & (KEYCATCH_CONSOLE | KEYCATCH_UI))) return false;
	if (key >= 32) context->ProcessTextInput(Rml::Character(key));
	return true;
}
bool CL_AtmosphereEditorMouse(int dx, int dy) {
	if (!active) return false;
	if (Key_GetCatcher() & (KEYCATCH_CONSOLE | KEYCATCH_UI)) return true;
	mouseX = std::max(0.0f, std::min(float(cls.glconfig.vidWidth - 1), mouseX + dx));
	mouseY = std::max(0.0f, std::min(float(cls.glconfig.vidHeight - 1), mouseY + dy));
	context->ProcessMouseMove(int(mouseX), int(mouseY), Modifiers());
	return true;
}
void CL_AtmosphereEditorDraw() {
	if (!active) return;
	if (cls.state != CA_ACTIVE || (Key_GetCatcher() & KEYCATCH_UI) ||
		Q_stricmp(draft.map, Cvar_VariableString("mapname"))) { Close(); return; }
	context->SetDimensions({cls.glconfig.vidWidth, cls.glconfig.vidHeight});
	context->SetDensityIndependentPixelRatio(std::max(.75f, std::min(1.5f, cls.glconfig.vidHeight / 800.0f)));
	Element("cursor")->SetProperty("left", va("%gpx", mouseX));
	Element("cursor")->SetProperty("top", va("%gpx", mouseY));
	Text("toggle", Cvar_VariableIntegerValue("r_atmosphere") ? "Show stock" : "Show atmosphere");
	context->Update();
	context->Render();
}
