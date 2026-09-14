// SPDX-License-Identifier: GPL-2.0-or-later
#include "client.h"

namespace {
WeaponWheel::Frame available;
RadialWheel::Preview preview;

bool Supported() {
	return available.allowed && CL_RmlUiAvailable() &&
		available.view.slotCount > 0 && available.view.slotCount <= RadialWheel::MaxSlots &&
		RadialWheel::Count(available.view.available, available.view.slotCount) > 0;
}

bool Allowed() { return Supported() && CL_WheelGameplayAllowed(); }

void Status() {
	Com_Printf("weaponwheel visible=%d weapon=%d equipped=%d slot=%d count=%d forceopen=%d timescale=%.3f game=%d real=%d\n",
		CL_WeaponWheelVisible(), available.weapon, cl.frame.ps.weapon, available.view.current,
		RadialWheel::Count(available.view.available, available.view.slotCount), CL_ForceWheelActive(),
		Cvar_VariableValue("timescale"), cl.serverTime, Sys_Milliseconds());
}
}

void CL_InitWeaponWheel() { Cmd_AddCommand("weaponwheel_status", Status); }

void CL_WeaponWheelCancel() { preview.Cancel(); }

bool CL_WeaponWheelVisible() {
	if (!Allowed()) preview.Cancel();
	return preview.Opacity(Sys_Milliseconds() * 0.001) > 0;
}

qboolean CL_WeaponWheelPreview() {
	if (!Allowed()) return qfalse;
	CL_ForceWheelCancel();
	preview.Show(Sys_Milliseconds() * 0.001);
	return qtrue;
}

void CL_WeaponWheelUpdate(WeaponWheel::Frame* frame) {
	available = *frame;
	frame->supported = Supported();
	frame->visible = CL_WeaponWheelVisible();
}

int CL_WeaponWheelDraw(const char* label) {
	if (!CL_WeaponWheelVisible()) return 0;
	RadialWheel::View view = available.view;
	view.kind = RadialWheel::Kind::Weapon;
	const float opacity = preview.Opacity(Sys_Milliseconds() * 0.001);
	CL_RmlUiDrawSelectionWheel(view, label, opacity);
	return int(opacity * 255);
}
