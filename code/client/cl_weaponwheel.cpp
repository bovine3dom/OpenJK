// SPDX-License-Identifier: GPL-2.0-or-later
#include "client.h"

namespace {
WeaponWheel::Frame available;
RadialWheel::Preview preview;
RadialWheel::Selection selection;

bool Supported() {
	return available.allowed && CL_RmlUiAvailable() &&
		available.view.slotCount > 0 && available.view.slotCount <= RadialWheel::MaxSlots &&
		RadialWheel::Count(available.view.available, available.view.slotCount) > 0;
}

bool Allowed() { return Supported() && CL_WheelGameplayAllowed(); }

void Down() {
	if (Allowed() && selection.Press(Cmd_Argc() > 1 ? atoi(Cmd_Argv(1)) : -1,
		available.view.available, available.view.slotCount)) {
		CL_ForceWheelCancel();
		preview.Cancel();
		CL_ClearWheelActions();
	}
}

void Up() {
	if (!Allowed()) CL_WeaponWheelCancel();
	if (selection.Release(Cmd_Argc() > 1 ? atoi(Cmd_Argv(1)) : -1)) CL_ClearWheelActions();
}

void DefaultBinding() { CL_DefaultWheelBinding("+weaponwheel", 'h'); }

void Status() {
	Com_Printf("weaponwheel visible=%d weapon=%d equipped=%d slot=%d count=%d forceopen=%d timescale=%.3f game=%d real=%d open=%d hovered=%d\n",
		CL_WeaponWheelVisible(), available.weapon, cl.frame.ps.weapon, available.view.current,
		RadialWheel::Count(available.view.available, available.view.slotCount), CL_ForceWheelActive(),
		Cvar_VariableValue("timescale"), cl.serverTime, Sys_Milliseconds(), CL_WeaponWheelActive(), selection.Hovered());
}
}

void CL_InitWeaponWheel() {
	Cmd_AddCommand("weaponwheel_status", Status);
	Cmd_AddCommand("+weaponwheel", Down);
	Cmd_AddCommand("-weaponwheel", Up);
	Cmd_AddCommand("weaponwheel_defaults", DefaultBinding);
	if (!Cvar_Get("cg_weaponWheelBindInitialized", "0", CVAR_ARCHIVE)->integer) {
		DefaultBinding();
		Cvar_Set("cg_weaponWheelBindInitialized", "1");
	}
}

void CL_WeaponWheelCancel() {
	if (selection.CapturesInput()) CL_ClearWheelActions();
	selection.Cancel();
	preview.Cancel();
}

bool CL_WeaponWheelActive() {
	if (!Allowed()) CL_WeaponWheelCancel();
	return selection.Open();
}

bool CL_WeaponWheelCapturesInput() { CL_WeaponWheelActive(); return selection.CapturesInput(); }
bool CL_WeaponWheelKey(int key) { return Key_GetBinding(key) && !Q_stricmp(Key_GetBinding(key), "+weaponwheel"); }
bool CL_WeaponWheelMouse(int dx, int dy) {
	if (!CL_WeaponWheelCapturesInput()) return false;
	selection.Move(float(dx), float(dy));
	return true;
}

bool CL_WeaponWheelVisible() {
	const bool held = CL_WeaponWheelActive();
	return held || preview.Opacity(Sys_Milliseconds() * 0.001) > 0;
}

qboolean CL_WeaponWheelPreview() {
	if (!Allowed()) return qfalse;
	CL_ForceWheelCancel();
	CL_WeaponWheelCancel();
	preview.Show(Sys_Milliseconds() * 0.001);
	return qtrue;
}

void CL_WeaponWheelUpdate(WeaponWheel::Frame* frame) {
	available = *frame;
	if (!Allowed() || (selection.Open() && selection.Mask() != frame->view.available)) CL_WeaponWheelCancel();
	frame->selected = selection.TakeSelection();
	if (frame->selected >= 0 && !(frame->view.available & (1u << frame->selected))) frame->selected = -1;
	frame->supported = Supported();
	frame->visible = CL_WeaponWheelVisible();
	frame->view.hovered = selection.Open() ? selection.Hovered() : -1;
}

int CL_WeaponWheelDraw(const char* label) {
	if (!CL_WeaponWheelVisible()) return 0;
	RadialWheel::View view = available.view;
	view.kind = RadialWheel::Kind::Weapon;
	view.pointer = selection.Open();
	view.hovered = selection.Open() ? selection.Hovered() : -1;
	view.x = selection.X(); view.y = selection.Y();
	const float opacity = selection.Open() ? 1 : preview.Opacity(Sys_Milliseconds() * 0.001);
	CL_RmlUiDrawSelectionWheel(view, label, opacity);
	return int(opacity * 255);
}
