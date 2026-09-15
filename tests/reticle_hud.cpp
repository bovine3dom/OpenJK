// SPDX-License-Identifier: GPL-2.0-or-later
#include "qcommon/reticle_hud.h"
#include "qcommon/hud_compass.h"
#include <cassert>
#include <cstdio>

int main() {
	ReticleHud::Activity activity;
	reticleHudState_t state;
	state.weapon = 3;
	state.force = state.forceMax = 100;
	state.ammo = 150;
	state.ammoMax = 300;
	double now = 0;
	auto tick = [&]() { now += 0.1; state.time += 100; return activity.Update(state, now); };
	auto idle = [&]() { for (int i = 0; i < 30; ++i) tick(); return tick(); };
	auto view = tick();
	assert(view.forceAlpha == 0 && view.ammoAlpha == 1 && view.ammo == 0.5f);
	assert(idle().ammoAlpha == 0);
	state.ammo -= 1;
	assert(tick().ammoAlpha == 1);
	idle();
	state.ammo += 20;
	assert(tick().ammoAlpha == 1);
	state.firing = true;
	assert(idle().ammoAlpha == 1);
	state.firing = false;
	for (int i = 0; i < 18; ++i) tick();
	view = tick();
	assert(view.ammoAlpha > 0 && view.ammoAlpha < 1);
	assert(idle().ammoAlpha == 0);
	state.force = 0;
	view = idle();
	assert(view.forceAlpha == 1 && view.force == 0);
	state.force = 60;
	assert(tick().forceAlpha == 1);
	state.force = 100;
	assert(tick().forceAlpha == 1);
	assert(idle().forceAlpha == 0);
	state.forceActive = true;
	assert(idle().forceAlpha == 1);
	state.forceActive = false;
	idle();
	state.forceWarning = true;
	assert(tick().forceAlpha == 1);
	state.forceWarning = false;
	state.weapon = 1;
	state.stance = 1;
	view = tick();
	assert(view.stanceAlpha == 1 && view.stance == 1 && view.ammoAlpha == 0);
	assert(idle().stanceAlpha == 0);
	state.stance = 0;
	assert(tick().stanceAlpha == 1);
	state.saberActive = true;
	assert(idle().stanceAlpha == 1);
	state.saberActive = false;
	assert(idle().stanceAlpha == 0);
	state.weapon = 3;
	state.stance = -1;
	state.ammo = 0;
	view = tick();
	assert(view.ammoAlpha == 1 && view.ammo == 0 && view.stanceAlpha == 0);
	state.forceMax = state.ammoMax = 0;
	view = tick();
	assert(view.forceAlpha == 0 && view.ammoAlpha == 0);
	state.forceMax = 100;
	state.force = 200;
	assert(tick().force == 1);
	activity.Reset();
	view = tick();
	assert(view.forceAlpha == 0);
	state.force = 50;
	tick();
	state.time = -100; // A loaded save starts a fresh activity history.
	state.force = 100;
	assert(tick().forceAlpha == 0);

	activity.Reset();
	state.healthMax = state.health = state.armor = 100;
	view = tick();
	assert(view.healthAlpha == 0 && view.armorAlpha == 0);
	auto after = [&](int ticks) { ReticleHud::Display v; for (int i = 0; i < ticks; ++i) v = tick(); return v; };
	state.armor = 60; // Shield-only damage reveals both resources.
	view = tick();
	assert(view.healthAlpha == 1 && view.armorAlpha == 1 && view.armor == 0.6f);
	assert(after(40).healthAlpha == 1);
	view = after(12);
	assert(view.healthAlpha > 0 && view.healthAlpha < 1);
	assert(after(6).armorAlpha == 0);
	state.health = 80;
	tick();
	state.health = 90; // A pickup must not shorten an existing damage hold.
	tick();
	assert(after(40).healthAlpha == 1);
	assert(after(20).healthAlpha == 0);
	state.health = 99;
	assert(tick().healthAlpha == 1);
	assert(after(25).healthAlpha == 1);
	view = after(8);
	assert(view.healthAlpha > 0 && view.healthAlpha < 1);
	assert(after(5).healthAlpha == 0);
	state.health = 25;
	assert(tick().healthAlpha == 1);
	view = after(60);
	assert(view.healthAlpha == 0.45f && view.armorAlpha == 0);
	assert(after(30).healthAlpha == 0.45f); // Critical health stays steady, without pulsing.
	state.health = 26;
	tick();
	assert(after(40).healthAlpha == 0);
	state.armor = 0;
	assert(tick().armorAlpha == 1);
	view = after(60);
	assert(view.healthAlpha == 0 && view.armorAlpha == 0);
	state.health = 0;
	view = tick();
	assert(view.healthAlpha == 0 && view.armorAlpha == 0);
	activity.Reset();
	state.health = 25;
	assert(tick().healthAlpha == 0.45f); // Loading at critical health needs no damage event.
	activity.Reset();
	state.healthMax = 200;
	state.health = 50;
	assert(tick().healthAlpha == 0.45f);
	state.healthMax = 0;
	assert(tick().healthAlpha == 0);
	state.healthMax = 100;
	state.health = state.armor = 200;
	view = tick();
	assert(view.health == 1 && view.armor == 1);
	state.health = state.armor = state.healthMax = 100;
	state.force = state.forceMax = 100;
	state.ammo = state.ammoMax = 100;
	idle();
	idle();
	view = activity.Update(state, now, true);
	assert(view.healthAlpha == 1 && view.armorAlpha == 1 && view.forceAlpha == 1 && view.ammoAlpha == 1);
	view = tick();
	assert(view.healthAlpha == 0 && view.armorAlpha == 0 && view.forceAlpha == 0 && view.ammoAlpha == 0);
	state.forceMax = state.ammoMax = 0;
	state.stance = 2;
	view = activity.Update(state, now, true);
	assert(view.forceAlpha == 0 && view.ammoAlpha == 0 && view.stanceAlpha == 1);

	using HudCompass::Project;
	assert(Project(90, 0, 10, 0).position == 0); // North is ahead.
	assert(Project(90, 10, 0, 0).position == 1); // East is right when facing north.
	assert(Project(90, -10, 0, 0).position == -1);
	assert(Project(0, -10, -1, 0).edge == 1 && Project(0, -10, 1, 0).edge == -1);
	assert(std::abs(HudCompass::Offset(359, 1) + 2) < 0.001f);
	assert(std::abs(HudCompass::Offset(1, 359) - 2) < 0.001f);
	assert(Project(0, 0, 0, 100).position == 0 && Project(0, 0, 0, 100).elevation == 1);
	assert(Project(0, 10, 0, -100).elevation == -1 && Project(0, 10, 0, 20).elevation == 0);
	std::puts("PASS: HUD activity, temporary reveal, and compass bearings");
}
