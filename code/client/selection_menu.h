// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../qcommon/q_shared.h"

namespace SelectionMenu {
enum Page { Closed, Force, Weapons };
struct PowerDef { const char *key; int power; const char *icon; const char *description; };
static const PowerDef Powers[] = {
	{"absorb", FP_ABSORB, "lt_absorb", "ABSORB"}, {"heal", FP_HEAL, "lt_heal", "HEAL"},
	{"mindtrick", FP_TELEPATHY, "lt_mind_trick", "MIND_TRICK"}, {"protect", FP_PROTECT, "lt_protect", "PROTECT"},
	{"jump", FP_LEVITATION, "jump", "JUMP"}, {"pull", FP_PULL, "pull", "PULL"},
	{"push", FP_PUSH, "push", "PUSH"}, {"sense", FP_SEE, "sight", "SENSE"},
	{"speed", FP_SPEED, "speed", "SPEED"}, {"sabdef", FP_SABER_DEFENSE, "saber_defend", "SABER_DEFENSE"},
	{"saboff", FP_SABER_OFFENSE, "saber_attack", "SABER_OFFENSE"}, {"sabthrow", FP_SABERTHROW, "saber_throw", "SABER_THROW"},
	{"drain", FP_DRAIN, "dk_drain", "DRAIN"}, {"grip", FP_GRIP, "dk_grip", "GRIP"},
	{"lightning", FP_LIGHTNING, "dk_l1", "LIGHTNING"}, {"rage", FP_RAGE, "dk_rage", "RAGE"}
};
struct Rect { float x, y, w, h; };
struct WeaponDef { const char *key; const char *icon; const char *description; Rect hex, picture, button; };
// Indices match the stock player weapon IDs, including the fixed saber and pistol slots.
static const WeaponDef WeaponsData[] = {
	{"saber", "lightsaber", "SABER", {28,40,88,120}, {24,30,80,80}, {28,40,79,64}},
	{"bpistol", "blaster_pistol", "NEW_BLASTER_PISTOL", {28,109,88,120}, {24,96,80,80}, {28,109,79,64}},
	{"brifle", "blaster", "BLASTER_RIFLE", {136,40,88,120}, {134,31,80,80}, {137,40,79,64}},
	{"disruptor", "disruptor", "DISRUPTOR_RIFLE", {136,109,88,120}, {134,99,80,80}, {137,109,79,64}},
	{"bowcaster", "bowcaster", "BOWCASTER", {222,40,88,120}, {222,31,80,80}, {224,40,79,64}},
	{"repeater", "repeater", "HEAVYREPEATER", {311,40,88,120}, {309,35,80,80}, {312,40,79,64}},
	{"demp", "demp2", "DEMP2", {222,109,88,120}, {222,99,80,80}, {225,109,79,64}},
	{"flechette", "flechette", "FLECHETTE", {311,109,88,120}, {309,100,80,80}, {312,109,79,64}},
	{"rocket", "merrsonn", "MERR_SONN", {399,109,88,120}, {395,99,80,80}, {400,109,79,64}},
	{"thermal", "thermal", "THERMAL_DETONATOR", {506,40,60,120}, {494,32,80,80}, {507,40,52,64}},
	{"tripmine", "tripmine", "TRIP_MINE", {538,109,60,120}, {526,100,80,80}, {539,109,52,64}},
	{"detpack", "detpack", "DET_PACK", {564,40,60,120}, {553,36,80,80}, {565,40,52,64}},
	{"concussion", "c_rifle", "CONCUSSION", {399,40,88,120}, {395,30,80,80}, {400,40,79,64}}
};
struct View {
	Page page = Closed;
	bool outcast = false, help = false, ready = false, canLeaveForce = false;
	int remaining = 0, levels[16] = {}, original[16] = {};
	bool editable[16] = {}, available[14] = {};
	int selected[3] = {};
};
}

bool UI_SelectionOpen(const char *name);
void UI_SelectionReset();
void UI_SelectionRestore();
SelectionMenu::View UI_SelectionView();
void UI_SelectionAction(const char *action, int index = -1);

void CL_RmlSelectionInit();
void CL_RmlSelectionShutdown();
void CL_RmlSelectionReset();
bool CL_RmlSelectionAvailable();
bool CL_RmlSelectionDraw();
bool CL_RmlSelectionKey(int key, bool down);
bool CL_RmlSelectionMouse(int dx, int dy);
