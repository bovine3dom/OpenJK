// SPDX-License-Identifier: GPL-2.0-or-later
#include "g_local.h"
#include "wp_saber.h"
#include <cstring>

extern int killPlayerTimer;

namespace {
struct Mission {
	const char *name;
	const char *maps[5];
	int points;
	bool loadout;
};

// One preparation screen per story mission, not per BSP. Interludes keep their story equipment.
const Mission missions[] = {
	{"Kejim", {"kejim_post", "kejim_base"}, 0, true},
	{"Artus Prime", {"artus_mine", "artus_detention", "artus_topside"}, 0, true},
	{"Valley of the Jedi", {"valley"}, 0, false},
	{"Jedi Training", {"yavin_temple", "yavin_trial"}, 0, false},
	{"Nar Shaddaa", {"ns_streets", "ns_hideout", "ns_starpad"}, 2, true},
	{"Bespin", {"bespin_undercity", "bespin_streets", "bespin_platform"}, 3, true},
	{"Cairn Installation", {"cairn_bay", "cairn_assembly", "cairn_reactor", "cairn_dock1"}, 4, true},
	{"Doomgiver", {"doom_comm", "doom_detention", "doom_shields"}, 5, true},
	{"Return to Yavin", {"yavin_swamp", "yavin_canyon", "yavin_courtyard", "yavin_final"}, 7, true},
};

struct WeaponChoice { int weapon; const char *name; };
const WeaponChoice weapons[] = {
	{WP_BLASTER, "Blaster rifle"}, {WP_DISRUPTOR, "Disruptor"},
	{WP_BOWCASTER, "Bowcaster"}, {WP_REPEATER, "Repeater"},
	{WP_DEMP2, "DEMP 2"}, {WP_FLECHETTE, "Flechette"},
	{WP_ROCKET_LAUNCHER, "Rocket launcher"}, {WP_CONCUSSION, "Concussion rifle"},
};
const WeaponChoice explosives[] = {
	{WP_THERMAL, "Thermal detonators"}, {WP_TRIP_MINE, "Trip mines"}, {WP_DET_PACK, "Detonation packs"},
};
struct PowerChoice { int power; const char *key; const char *name; };
const PowerChoice powers[] = {
	{FP_ABSORB, "absorb", "Absorb"}, {FP_PROTECT, "protect", "Protect"},
	{FP_RAGE, "rage", "Rage"}, {FP_DRAIN, "drain", "Drain"}, {FP_SEE, "sense", "Sense"},
};

struct Preparation {
	bool pending = false, editing = false;
	char map[MAX_QPATH] = {}, spawn[MAX_QPATH] = {};
	qboolean hub = qfalse;
	int mission = 0, budget = 0, primary = 0, explosive = WP_THERMAL;
	int original[ARRAY_LEN(powers)] = {}, levels[ARRAY_LEN(powers)] = {};
} prep;

int MissionForMap(const char *map) {
	if (map)
		for (int i = 0; i < ARRAY_LEN(missions); ++i)
			for (const char *name : missions[i].maps)
				if (name && !Q_stricmp(name, map)) return i;
	return -1;
}

int PrimaryCount() {
	int count = 0;
	for (const auto &weapon : weapons) if (prep.primary & (1 << weapon.weapon)) ++count;
	return count;
}

int Spent(const int *levels) {
	int total = 0;
	for (int i = 0; i < ARRAY_LEN(powers); ++i) total += levels[i];
	return total;
}

int Remaining() {
	// Absolute story budgets prevent repeated transitions or save loading from awarding extra points.
	return Q_max(prep.budget, Spent(prep.original)) - Spent(prep.levels);
}

void UpdateLabels() {
	gi.cvar_set("ui_jo_prep_title", va("Prepare for %s", missions[prep.mission].name));
	gi.cvar_set("ui_jo_prep_count", va("Main weapons: %d / 2", PrimaryCount()));
	gi.cvar_set("ui_jo_prep_points", va("Optional Force points: %d available (%d / %d unlocked)", Remaining(), prep.budget, missions[ARRAY_LEN(missions) - 1].points));
	gi.cvar_set("ui_jo_prep_valid", PrimaryCount() == 2 && Remaining() >= 0 ? "1" : "0");
	for (const auto &weapon : weapons)
		gi.cvar_set(va("ui_jo_weapon_%d", weapon.weapon), va("[%c] %s", prep.primary & (1 << weapon.weapon) ? 'x' : ' ', weapon.name));
	for (const auto &weapon : explosives)
		gi.cvar_set(va("ui_jo_weapon_%d", weapon.weapon), va("[%c] %s", prep.explosive == weapon.weapon ? 'x' : ' ', weapon.name));
	for (int i = 0; i < ARRAY_LEN(powers); ++i) {
		gi.cvar_set(va("ui_jo_power_%s", powers[i].key), va("%s: %d / %d", powers[i].name, prep.levels[i], FORCE_LEVEL_3));
		gi.cvar_set(va("ui_jo_up_%s", powers[i].key), Remaining() > 0 && prep.levels[i] < FORCE_LEVEL_3 ? "1" : "0");
		gi.cvar_set(va("ui_jo_down_%s", powers[i].key), prep.levels[i] > prep.original[i] ? "1" : "0");
	}
}

void Begin() {
	prep.editing = true;
	UpdateLabels();
	gi.cvar_set("cl_joStatsState", "3");
	gi.cvar_set("cl_paused", "1");
	gi.Printf("JO preparation: ready mission=%s budget=%d remaining=%d\n", prep.map, prep.budget, Remaining());
}
}

void G_ResetJoPreparation() {
	prep = Preparation{};
	gi.cvar("jo_prep_pending", "0", CVAR_ROM);
	gi.cvar("jo_loadoutMap", "", CVAR_ROM | CVAR_SAVEGAME | CVAR_NORESTART);
	gi.cvar_set("jo_prep_pending", "0");
}

qboolean G_QueueJoPreparation(const char *map, const char *spawn, qboolean hub) {
	if (!G_IsOutcast() || !level.clients || g_entities[0].health <= 0 || killPlayerTimer) return qfalse;
	const int from = MissionForMap(level.mapname), to = MissionForMap(map);
	if (from < 0 || to <= from || !missions[to].loadout) return qfalse;
	if (prep.pending) return qtrue;
	prep = Preparation{};
	prep.pending = true;
	prep.mission = to;
	prep.hub = hub;
	Q_strncpyz(prep.map, map, sizeof(prep.map));
	Q_strncpyz(prep.spawn, spawn ? spawn : "", sizeof(prep.spawn));
	const auto &ps = level.clients[0].ps;
	for (int power = FP_FIRST; power < FP_RAGE; ++power)
		if ((ps.forcePowersKnown & (1 << power)) && ps.forcePowerLevel[power] > 0) prep.budget = missions[to].points;
	for (int i = 0; i < ARRAY_LEN(powers); ++i)
		prep.original[i] = prep.levels[i] = Com_Clampi(0, FORCE_LEVEL_3, ps.forcePowerLevel[powers[i].power]);
	for (const auto &weapon : weapons)
		if ((ps.stats[STAT_WEAPONS] & (1 << weapon.weapon)) && PrimaryCount() < 2) prep.primary |= 1 << weapon.weapon;
	for (const auto &weapon : weapons)
		if (PrimaryCount() < 2) prep.primary |= 1 << weapon.weapon;
	for (const auto &weapon : explosives)
		if (ps.stats[STAT_WEAPONS] & (1 << weapon.weapon)) { prep.explosive = weapon.weapon; break; }
	gi.cvar_set("jo_prep_pending", "1");
	gi.cvar_set("cl_paused", "1");
	if (gi.Cvar_VariableIntegerValue("cg_missionstatusscreen")) {
		gi.cvar_set("cl_joStatsState", "1");
		gi.Printf("JO statistics: waiting for Continue\n");
	} else Begin();
	return qtrue;
}

qboolean G_JoPreparedLoadout() {
	char map[MAX_QPATH];
	gi.Cvar_VariableStringBuffer("jo_loadoutMap", map, sizeof(map));
	return G_IsOutcast() && map[0] && !Q_stricmp(map, level.mapname) ? qtrue : qfalse;
}

void G_JoPreparationCommand() {
	const char *action = gi.argv(1);
	if (!Q_stricmp(action, "status")) {
		gi.Printf("jo_prepare pending=%d editing=%d source=%s target=%s budget=%d remaining=%d primary=%d explosive=%d\n",
			prep.pending, prep.editing, level.mapname, prep.pending ? prep.map : "none", prep.budget, Remaining(), prep.primary, prep.explosive);
		for (int i = 0; i < ARRAY_LEN(powers); ++i)
			gi.Printf("jo_prepare power=%s original=%d selected=%d live=%d\n", powers[i].key, prep.original[i], prep.levels[i],
				level.clients ? level.clients[0].ps.forcePowerLevel[powers[i].power] : 0);
		if (level.clients) {
			const auto &ps = level.clients[0].ps;
			gi.Printf("jo_loadout weapons=%d active=%d ammo=%d\n", ps.stats[STAT_WEAPONS], ps.weapon, ps.ammo[weaponData[ps.weapon].ammoIndex]);
		}
		return;
	}
	if (!G_IsOutcast() || !prep.pending || !level.clients) {
		gi.Printf("jo_prepare rejected: no mission preparation is pending\n");
		return;
	}
	if (!Q_stricmp(action, "begin")) { Begin(); return; }
	if (!prep.editing) return;
	if (!Q_stricmp(action, "weapon") && gi.argc() == 3) {
		const int choice = atoi(gi.argv(2));
		for (const auto &weapon : weapons) if (weapon.weapon == choice) {
			if (prep.primary & (1 << choice)) prep.primary &= ~(1 << choice);
			else if (PrimaryCount() < 2) prep.primary |= 1 << choice;
		}
	} else if (!Q_stricmp(action, "explosive") && gi.argc() == 3) {
		for (const auto &weapon : explosives) if (weapon.weapon == atoi(gi.argv(2))) prep.explosive = weapon.weapon;
	} else if (!Q_stricmp(action, "force") && gi.argc() == 4) {
		const int direction = atoi(gi.argv(3));
		for (int i = 0; i < ARRAY_LEN(powers); ++i) if (!Q_stricmp(gi.argv(2), powers[i].key)) {
			if (direction == 1 && Remaining() > 0 && prep.levels[i] < FORCE_LEVEL_3) ++prep.levels[i];
			else if (direction == -1 && prep.levels[i] > prep.original[i]) --prep.levels[i];
		}
	} else if (!Q_stricmp(action, "commit")) {
		if (PrimaryCount() != 2 || Remaining() < 0) {
			gi.Printf("jo_prepare rejected: choose two main weapons and stay within the point budget\n");
			return;
		}
		auto &ps = level.clients[0].ps;
		int retained = ps.stats[STAT_WEAPONS] & (1 << WP_STUN_BATON);
		if (ps.forcePowerLevel[FP_SABER_OFFENSE] > 0) retained |= ps.stats[STAT_WEAPONS] & (1 << WP_SABER);
		ps.stats[STAT_WEAPONS] = retained | (1 << WP_NONE) | (1 << WP_BRYAR_PISTOL) | prep.primary | (1 << prep.explosive);
		std::memset(ps.ammo, 0, sizeof(ps.ammo));
		for (int weapon = FIRST_WEAPON; weapon < WP_NUM_WEAPONS; ++weapon)
			if (ps.stats[STAT_WEAPONS] & (1 << weapon)) ps.ammo[weaponData[weapon].ammoIndex] = ammoData[weaponData[weapon].ammoIndex].max;
		ps.weapon = retained & (1 << WP_SABER) ? WP_SABER : WP_BRYAR_PISTOL;
		ps.weaponstate = WEAPON_READY;
		for (int i = 0; i < ARRAY_LEN(powers); ++i) {
			ps.forcePowerLevel[powers[i].power] = prep.levels[i];
			if (prep.levels[i]) ps.forcePowersKnown |= 1 << powers[i].power;
			else ps.forcePowersKnown &= ~(1 << powers[i].power);
		}
		gi.Printf("JO preparation: committed target=%s weapons=%d optional_spent=%d budget=%d\n", prep.map, ps.stats[STAT_WEAPONS], Spent(prep.levels), prep.budget);
		gi.cvar_set("jo_loadoutMap", prep.map);
		gi.cvar_set("jo_prep_pending", "0");
		gi.cvar_set("cg_missionstatusscreen", "0");
		gi.cvar_set("cl_joStatsState", "2");
		gi.cvar_set("cl_paused", "0");
		prep.pending = prep.editing = false;
		gi.SendConsoleCommand("jo_prepare_close\n");
		extern void G_ChangeMap(const char *, const char *, qboolean);
		G_ChangeMap(prep.map, prep.spawn, prep.hub);
		return;
	}
	UpdateLabels();
}
