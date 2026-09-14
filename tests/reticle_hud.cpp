// SPDX-License-Identifier: GPL-2.0-or-later
#include "qcommon/reticle_hud.h"
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
	std::puts("PASS: reticle HUD activity, fades, limits, weapon changes, and reset");
}
