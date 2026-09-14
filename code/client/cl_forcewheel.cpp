// SPDX-License-Identifier: GPL-2.0-or-later
#include "client.h"
#include "../game/statindex.h"

namespace {
ForceWheel::Selection selection;
ForceWheel::Frame available;

bool Allowed() {
	return cls.state == CA_ACTIVE && cls.cgameStarted && cl.frame.valid &&
		cl.frame.ps.stats[STAT_HEALTH] > 0 && !Key_GetCatcher() &&
		!Cvar_VariableIntegerValue("com_unfocused") && !Cvar_VariableIntegerValue("com_minimized") &&
		!Cvar_VariableIntegerValue("cl_paused") && !CL_IsRunningInGameCinematic() &&
		available.allowed && CL_RmlUiAvailable();
}

void Down() {
	if (Allowed() && selection.Press(Cmd_Argc() > 1 ? atoi(Cmd_Argv(1)) : -1, available.available))
		CL_ClearWheelActions();
}
void Up() {
	if (!Allowed()) CL_ForceWheelCancel();
	if (selection.Release(Cmd_Argc() > 1 ? atoi(Cmd_Argv(1)) : -1)) CL_ClearWheelActions();
}

void DefaultBinding() {
	for (int key = 0; key < MAX_KEYS; ++key)
		if (Key_GetBinding(key) && !Q_stricmp(Key_GetBinding(key), "+forcewheel")) return;
	char keyName[] = "g";
	const int key = Key_StringToKeynum(keyName);
	if (!Key_GetBinding(key) || !*Key_GetBinding(key)) Key_SetBinding(key, "+forcewheel");
}

void Status() {
	const usercmd_t& cmd = cl.cmds[cl.cmdNumber & CMD_MASK];
	Com_Printf("forcewheel open=%d hovered=%d selected=%d mask=%d timescale=%.3f game=%d real=%d buttons=%d forward=%d right=%d up=%d force=%d active=%d catcher=%d yaw=%.3f pitch=%.3f\n",
		CL_ForceWheelActive(), selection.Hovered(), available.current, available.available,
		Cvar_VariableValue("timescale"), cl.serverTime, Sys_Milliseconds(), cmd.buttons,
		cmd.forwardmove, cmd.rightmove, cmd.upmove, available.energy, available.activePowers,
		Key_GetCatcher(), cl.viewangles[YAW], cl.viewangles[PITCH]);
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
}

bool CL_ForceWheelActive() {
	if (!Allowed()) CL_ForceWheelCancel();
	return selection.Open();
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
	frame->open = selection.Open();
	frame->hovered = selection.Hovered();
	frame->x = selection.X(); frame->y = selection.Y();
}

void CL_ForceWheelDraw(const char* label) {
	if (!CL_ForceWheelActive()) return;
	ForceWheel::Frame frame = available;
	frame.open = true;
	frame.hovered = selection.Hovered();
	frame.x = selection.X(); frame.y = selection.Y();
	CL_RmlUiDrawForceWheel(frame, label);
}
