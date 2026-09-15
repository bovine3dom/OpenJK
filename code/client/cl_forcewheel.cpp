// SPDX-License-Identifier: GPL-2.0-or-later
#include "client.h"
#include "../game/statindex.h"

bool CL_WheelGameplayAllowed() {
	return cls.state == CA_ACTIVE && cls.cgameStarted && cl.frame.valid &&
		cl.frame.ps.stats[STAT_HEALTH] > 0 && !Key_GetCatcher() &&
		!Cvar_VariableIntegerValue("com_unfocused") && !Cvar_VariableIntegerValue("com_minimized") &&
		!Cvar_VariableIntegerValue("cl_paused") && !CL_IsRunningInGameCinematic() &&
		CL_RmlUiAvailable();
}

void CL_DefaultWheelBinding(const char* command, char keyName) {
	for (int key = 0; key < MAX_KEYS; ++key)
		if (Key_GetBinding(key) && !Q_stricmp(Key_GetBinding(key), command)) return;
	char name[] = {keyName, 0};
	const int key = Key_StringToKeynum(name);
	if (!Key_GetBinding(key) || !*Key_GetBinding(key)) Key_SetBinding(key, command);
}

namespace {
ForceWheel::Selection selection;
ForceWheel::Preview preview;
ForceWheel::Frame available;

bool Allowed() { return available.allowed && CL_WheelGameplayAllowed(); }

void Down() {
	if (Allowed() && selection.Press(Cmd_Argc() > 1 ? atoi(Cmd_Argv(1)) : -1, available.available, ForceWheel::MaxPowers)) {
		CL_WeaponWheelCancel();
		preview.Cancel();
		CL_ClearWheelActions();
	}
}
void Up() {
	if (!Allowed()) CL_ForceWheelCancel();
	if (selection.Release(Cmd_Argc() > 1 ? atoi(Cmd_Argv(1)) : -1)) CL_ClearWheelActions();
}

void DefaultBinding() {
	CL_DefaultWheelBinding("+forcewheel", 'g');
}

void Status() {
	const usercmd_t& cmd = cl.cmds[cl.cmdNumber & CMD_MASK];
	ForceWheel::Frame frame = available;
	frame.hovered = selection.Open() ? selection.Hovered() : -1;
	Com_Printf("forcewheel open=%d hovered=%d selected=%d mask=%d timescale=%.3f game=%d real=%d buttons=%d forward=%d right=%d up=%d force=%d active=%d catcher=%d yaw=%.3f pitch=%.3f visible=%d highlighted=%d\n",
		CL_ForceWheelActive(), selection.Hovered(), available.current, available.available,
		Cvar_VariableValue("timescale"), cl.serverTime, Sys_Milliseconds(), cmd.buttons,
		cmd.forwardmove, cmd.rightmove, cmd.upmove, available.energy, available.activePowers,
		Key_GetCatcher(), cl.viewangles[YAW], cl.viewangles[PITCH], CL_ForceWheelVisible(), ForceWheel::Highlighted(frame));
}
}

void CL_InitForceWheel() {
	Cmd_AddCommand("+forcewheel", Down);
	Cmd_AddCommand("-forcewheel", Up);
	Cmd_AddCommand("forcewheel_defaults", DefaultBinding);
	Cmd_AddCommand("forcewheel_status", Status);
	if (!Cvar_Get("cg_forceWheelBindInitialized", "0", CVAR_ARCHIVE)->integer) {
		DefaultBinding();
		Cvar_Set("cg_forceWheelBindInitialized", "1");
	}
}

void CL_ForceWheelCancel() {
	if (selection.CapturesInput()) CL_ClearWheelActions();
	selection.Cancel();
	preview.Cancel();
}

void CL_SelectionWheelsCancel() {
	CL_ForceWheelCancel();
	CL_WeaponWheelCancel();
}

bool CL_SelectionWheelActive() {
	const bool force = CL_ForceWheelActive();
	const bool weapon = CL_WeaponWheelActive();
	return force || weapon;
}

bool CL_SelectionWheelCapturesInput() { return CL_ForceWheelCapturesInput() || CL_WeaponWheelCapturesInput(); }
bool CL_SelectionWheelMouse(int dx, int dy) { return CL_ForceWheelMouse(dx, dy) || CL_WeaponWheelMouse(dx, dy); }
bool CL_SelectionWheelKey(int key) { return CL_ForceWheelKey(key) || CL_WeaponWheelKey(key); }

bool CL_ForceWheelActive() {
	if (!Allowed()) CL_ForceWheelCancel();
	return selection.Open();
}

bool CL_ForceWheelVisible() {
	const bool held = CL_ForceWheelActive();
	return held || (available.available && preview.Opacity(Sys_Milliseconds() * 0.001) > 0);
}

qboolean CL_ForceWheelPreview() {
	if (!Allowed()) return qfalse;
	CL_WeaponWheelCancel();
	CL_ForceWheelCancel();
	preview.Show(Sys_Milliseconds() * 0.001);
	return qtrue;
}

bool CL_ForceWheelCapturesInput() {
	CL_ForceWheelActive();
	return selection.CapturesInput();
}

bool CL_ForceWheelKey(int key) {
	return Key_GetBinding(key) && !Q_stricmp(Key_GetBinding(key), "+forcewheel");
}

bool CL_ForceWheelMouse(int dx, int dy) {
	if (!CL_ForceWheelCapturesInput()) return false;
	selection.Move(float(dx), float(dy));
	return true;
}

void CL_ForceWheelUpdate(ForceWheel::Frame* frame) {
	available = *frame;
	if (!Allowed() || (selection.Open() && selection.Mask() != frame->available)) CL_ForceWheelCancel();
	frame->selected = selection.TakeSelection();
	if (frame->selected >= 0 && !(frame->available & (1 << frame->selected))) frame->selected = -1;
	frame->open = CL_ForceWheelVisible();
	frame->hovered = selection.Open() ? selection.Hovered() : -1;
	frame->x = selection.X(); frame->y = selection.Y();
}

int CL_ForceWheelDraw(const char* label) {
	if (!CL_ForceWheelVisible()) return 0;
	RadialWheel::View view;
	view.available = available.available;
	view.slotCount = ForceWheel::MaxPowers;
	view.current = available.current;
	view.hovered = selection.Open() ? selection.Hovered() : -1;
	view.pointer = selection.Open();
	view.x = selection.X(); view.y = selection.Y();
	const float opacity = selection.Open() ? 1 : preview.Opacity(Sys_Milliseconds() * 0.001);
	CL_RmlUiDrawSelectionWheel(view, label, opacity);
	return int(opacity * 255);
}
