// SPDX-License-Identifier: GPL-2.0-or-later
#include "../server/exe_headers.h"
#include "ui_local.h"
#include "../client/selection_menu.h"

extern void Item_RunScript(itemDef_t *, const char *);
extern qboolean Item_EnableShowViaCvar(itemDef_t *, int);
extern void UI_ForceMenuOff();
extern menuDef_t *Menus_FindByName(const char *);

namespace {
SelectionMenu::View academy;
int academyChoice = -1;

playerState_t *Player() {
	return ge && cls.state >= CA_PRIMED && svs.clients && svs.clients[0].gentity ? svs.clients[0].gentity->client : nullptr;
}
bool OptionalJA(int i) { return i < 4 || i >= 12; }
bool OptionalJO(int i) { return i == 0 || i == 3 || i == 7 || i == 12 || i == 15; }
bool Visible(itemDef_t *item, bool checkWindow = true) {
	return item && (!checkWindow || (item->window.flags & WINDOW_VISIBLE)) &&
		(!(item->cvarFlags & (CVAR_SHOW | CVAR_HIDE)) || Item_EnableShowViaCvar(item, CVAR_SHOW));
}
}

bool UI_SelectionOpen(const char *name) {
	if (!CL_RmlSelectionAvailable() || Cvar_VariableIntegerValue("com_outcast")) return false;
	const auto page = !Q_stricmp(name, "ingameForceSelect") ? SelectionMenu::Force :
		!Q_stricmp(name, "ingameWpnSelect") ? SelectionMenu::Weapons : SelectionMenu::Closed;
	if (page == SelectionMenu::Closed) return false;
	if (academy.page == SelectionMenu::Closed) {
		academy = SelectionMenu::View{};
		academyChoice = -1;
		const auto *ps = Player();
		for (int i = 0; i < 16; ++i) {
			const int power = SelectionMenu::Powers[i].power;
			academy.original[i] = academy.levels[i] = ps ? ps->forcePowerLevel[power] : uiInfo.forcePowerLevel[power];
			academy.editable[i] = OptionalJA(i);
			if (page == SelectionMenu::Force && OptionalJA(i) && academy.levels[i] < 3) academy.remaining = 1;
		}
	}
	academy.page = page;
	if (page == SelectionMenu::Force) uiInfo.uiDC.startLocalSound(uiInfo.uiDC.Assets.nullSound, CHAN_VOICE);
	Cvar_Set("ui_rmlSelectionActive", "1");
	Cvar_Set("ui_rmlSelectionHelp", Cvar_VariableIntegerValue("tier_storyinfo") == 1 ? "1" : "0");
	return true;
}

void UI_SelectionReset() {
	academy = SelectionMenu::View{};
	Cvar_Set("ui_rmlSelectionActive", "0");
	Cvar_Set("ui_rmlSelectionHelp", "0");
}

void UI_SelectionRestore() {
	if (academy.page == SelectionMenu::Closed || Menu_GetFocused()) return;
	const char *name = academy.page == SelectionMenu::Force ? "ingameForceSelect" : "ingameWpnSelect";
	if (!CL_RmlSelectionAvailable()) {
		if (academyChoice >= 0) name = "ingameForceSelect";
		UI_SelectionReset();
		Menus_ActivateByName(name);
		ui.Key_SetCatcher(Key_GetCatcher() | KEYCATCH_UI);
		return;
	}
	menuDef_t *menu = Menus_FindByName(name);
	if (menu) {
		// Renderer restarts reload menu definitions. Restore the draft without replaying onOpen.
		menu->window.flags |= WINDOW_VISIBLE | WINDOW_HASFOCUS;
		ui.Key_SetCatcher(Key_GetCatcher() | KEYCATCH_UI);
		Cvar_Set("cl_paused", "1");
	}
}

SelectionMenu::View UI_SelectionView() {
	SelectionMenu::View view;
	if (!CL_RmlSelectionAvailable()) return view;
	menuDef_t *menu = Menu_GetFocused();
	if (Cvar_VariableIntegerValue("com_outcast")) {
		if (Cvar_VariableIntegerValue("cl_joStatsState") != 3 || !Cvar_VariableIntegerValue("jo_prep_pending")) return view;
		view.outcast = true;
		view.page = !Q_stricmp(Cvar_VariableString("ui_rmlSelectionPage"), "weapons") ? SelectionMenu::Weapons : SelectionMenu::Force;
		view.remaining = Cvar_VariableIntegerValue("ui_jo_remaining");
		const auto *ps = Player();
		for (int i = 0; i < 16; ++i) {
			view.editable[i] = OptionalJO(i);
			view.original[i] = view.levels[i] = ps ? ps->forcePowerLevel[SelectionMenu::Powers[i].power] : 0;
			if (OptionalJO(i)) {
				view.levels[i] = Cvar_VariableIntegerValue(va("ui_jo_level_%s", SelectionMenu::Powers[i].key));
				view.original[i] = Cvar_VariableIntegerValue(va("ui_jo_original_%s", SelectionMenu::Powers[i].key));
			}
		}
		view.available[1] = ps && ps->forcePowerLevel[FP_SABER_OFFENSE] > 0 && (ps->stats[STAT_WEAPONS] & (1 << WP_SABER));
		for (int w = 2; w <= 13; ++w) view.available[w] = true;
		int mask = Cvar_VariableIntegerValue("ui_jo_primary"), slot = 0;
		for (int w = 3; w <= 13 && slot < 2; ++w) if (mask & (1 << w)) view.selected[slot++] = w;
		view.selected[2] = Cvar_VariableIntegerValue("ui_jo_explosive");
		view.canLeaveForce = true;
	} else {
		if (academy.page == SelectionMenu::Closed || !menu || (Q_stricmp(menu->window.name, "ingameForceSelect") && Q_stricmp(menu->window.name, "ingameWpnSelect"))) return view;
		view = academy;
		menuDef_t *weapons = Menus_FindByName("ingameWpnSelect");
		for (int w = 1; w <= 13; ++w)
			view.available[w] = Visible(Menu_FindItemByName(weapons, va("%s_button", SelectionMenu::WeaponsData[w - 1].key)));
		view.canLeaveForce = view.remaining == 0;
	}
	view.ready = view.selected[0] && view.selected[1] && view.selected[2];
	view.help = Cvar_VariableIntegerValue("ui_rmlSelectionHelp") != 0;
	return view;
}

void UI_SelectionAction(const char *action, int index) {
	auto view = UI_SelectionView();
	if (view.page == SelectionMenu::Closed) return;
	if (!Q_stricmp(action, "focus")) {
		uiInfo.uiDC.startLocalSound(uiInfo.uiDC.Assets.itemFocusSound, CHAN_LOCAL);
		return;
	}
	if (!Q_stricmp(action, "help")) { Cvar_Set("ui_rmlSelectionHelp", "1"); return; }
	if (!Q_stricmp(action, "dismiss")) { Cvar_Set("ui_rmlSelectionHelp", "0"); return; }
	if (view.help) return;
	if (!Q_stricmp(action, "weapons") && view.canLeaveForce) {
		if (view.outcast) Cvar_Set("ui_rmlSelectionPage", "weapons");
		else { Menus_CloseAll(); Menus_ActivateByName("ingameWpnSelect"); }
		return;
	}
	if (!Q_stricmp(action, "force") && view.outcast) { Cvar_Set("ui_rmlSelectionPage", "force"); return; }
	if ((!Q_stricmp(action, "upgrade") || !Q_stricmp(action, "undo")) && index >= 0 && index < 16 && view.editable[index]) {
		const bool undo = !Q_stricmp(action, "undo") || (!view.remaining && view.levels[index] > view.original[index]);
		if (undo ? view.levels[index] <= view.original[index] : (!view.remaining || view.levels[index] >= 3)) return;
		if (view.outcast) Cbuf_AddText(va("jo_prepare force %s %d\n", SelectionMenu::Powers[index].key, undo ? -1 : 1));
		else {
			academy.levels[index] += undo ? -1 : 1;
			academy.remaining += undo ? 1 : -1;
			academyChoice = undo ? -1 : index;
		}
		uiInfo.uiDC.startLocalSound(undo ? uiInfo.uiDC.Assets.forceUnchosenSound : uiInfo.uiDC.Assets.forceChosenSound, CHAN_AUTO);
		return;
	}
	if (!Q_stricmp(action, "weapon") && index >= 3 && index <= 13 && view.available[index]) {
		const bool explosive = index >= 10 && index <= 12;
		if (view.outcast) Cbuf_AddText(va("jo_prepare %s %d\n", explosive ? "explosive" : "weapon", index));
		else if (explosive) academy.selected[2] = academy.selected[2] == index ? 0 : index;
		else if (academy.selected[0] == index) academy.selected[0] = 0;
		else if (academy.selected[1] == index) academy.selected[1] = 0;
		else if (!academy.selected[0]) academy.selected[0] = index;
		else if (!academy.selected[1]) academy.selected[1] = index;
		return;
	}
	if (Q_stricmp(action, "begin") || !view.ready) return;
	if (view.outcast) { Cbuf_AddText("jo_prepare commit\n"); return; }
	menuDef_t *menu = Menus_FindByName("ingameWpnSelect");
	if (!menu) return;
	itemDef_t *begin = nullptr;
	for (int i = 0; i < menu->itemCount; ++i)
		if (!Q_stricmp(menu->items[i]->window.name, "beginmission") && Visible(menu->items[i], false)) { begin = menu->items[i]; break; }
	if (!begin || !begin->action) return;
	itemDef_t *choices[3];
	for (int i = 0; i < 3; ++i) {
		choices[i] = Menu_FindItemByName(menu, va("%s_button", SelectionMenu::WeaponsData[academy.selected[i] - 1].key));
		if (!choices[i] || !choices[i]->action) return;
	}
	if (academyChoice >= 0) {
		const int power = SelectionMenu::Powers[academyChoice].power;
		if (auto *ps = Player()) {
			ps->forcePowerLevel[power] = academy.levels[academyChoice];
			ps->forcePowersKnown |= 1 << power;
		}
		uiInfo.forcePowerLevel[power] = academy.levels[academyChoice];
		uiInfo.forcePowerUpdated = academyChoice;
		Cvar_Set("ui_forcepower_inc", va("%d", academyChoice));
	}
	// Use JA's own equipment, ammunition, and mission-start scripts at confirmation.
	itemDef_t opening = {};
	opening.parent = menu;
	Item_RunScript(&opening, menu->onOpen);
	for (auto *item : choices) Item_RunScript(item, item->action);
	academy.page = SelectionMenu::Closed;
	Cvar_Set("ui_rmlSelectionActive", "0");
	UI_ForceMenuOff();
	Item_RunScript(begin, begin->action);
}
