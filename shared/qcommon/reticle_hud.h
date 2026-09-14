// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>

// Native SP cgame/client interface. Resource values use the game's own limits.
struct reticleHudState_t {
	int time = 0;
	int weapon = 0;
	int force = 0, forceMax = 0;
	int ammo = 0, ammoMax = 0;
	int health = 0, armor = 0, healthMax = 0; // Max health is also the shield capacity.
	int stance = -1; // -1: no saber, 0: fast, 1: medium/dual/staff, 2: strong.
	bool forceActive = false, forceWarning = false;
	bool firing = false, saberActive = false;
};

namespace ReticleHud {

enum { DotDrawn = 1, ResourcesDrawn = 2 };

inline float Fraction(int value, int maximum) {
	return maximum > 0 ? std::max(0.0f, std::min(1.0f, float(value) / maximum)) : 0.0f;
}

struct Display {
	float force = 0, ammo = 0;
	float forceAlpha = 0, ammoAlpha = 0, stanceAlpha = 0;
	float health = 0, armor = 0;
	float healthAlpha = 0, armorAlpha = 0;
	int stance = -1;
};

class Activity {
	reticleHudState_t previous;
	bool valid = false;
	double lastUpdate = 0, forceUntil = 0, ammoUntil = 0, stanceUntil = 0;
	double vitalsUntil = 0;
	static float Fade(double until, double now) {
		return float(std::max(0.0, std::min(1.0, (until - now) / 0.6)));
	}
public:
	void Reset() { *this = Activity(); }
	Display Update(const reticleHudState_t& state, double now) {
		if (valid && (state.time < previous.time || now - lastUpdate > 5.0)) Reset();
		const bool weaponChanged = !valid || state.weapon != previous.weapon;
		if (state.forceMax > 0 && (state.force < state.forceMax || state.forceActive || state.forceWarning ||
			(valid && state.force != previous.force))) forceUntil = now + 0.9;
		if (state.ammoMax > 0 && (weaponChanged || state.firing ||
			(valid && state.ammo != previous.ammo))) ammoUntil = now + 2.1;
		if (state.stance >= 0 && (weaponChanged || state.saberActive ||
			(valid && state.stance != previous.stance))) stanceUntil = now + 1.4;
		if (valid && state.healthMax > 0 && previous.healthMax > 0 &&
			(state.health != previous.health || state.armor != previous.armor || state.healthMax != previous.healthMax)) {
			const bool damaged = state.health < previous.health || state.armor < previous.armor;
			vitalsUntil = std::max(vitalsUntil, now + (damaged ? 5.6 : 3.6));
		}
		Display result;
		result.force = Fraction(state.force, state.forceMax);
		result.ammo = Fraction(state.ammo, state.ammoMax);
		result.forceAlpha = state.forceMax > 0 ? Fade(forceUntil, now) : 0;
		result.ammoAlpha = state.ammoMax > 0 && state.stance < 0 ? Fade(ammoUntil, now) : 0;
		result.stanceAlpha = state.stance >= 0 ? Fade(stanceUntil, now) : 0;
		result.stance = state.stance;
		result.health = Fraction(state.health, state.healthMax);
		result.armor = Fraction(state.armor, state.healthMax);
		if (state.healthMax > 0 && state.health > 0) {
			result.healthAlpha = result.armorAlpha = Fade(vitalsUntil, now);
			if (result.health <= 0.25f) result.healthAlpha = std::max(result.healthAlpha, 0.45f);
		}
		previous = state;
		lastUpdate = now;
		valid = true;
		return result;
	}
};

} // namespace ReticleHud
