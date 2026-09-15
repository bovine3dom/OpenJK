// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cmath>
#include <cctype>
#include <cstddef>
#include "qcommon/radial_wheel.h"

namespace ForceWheel {
constexpr int MaxPowers = 12;
using RadialWheel::Radius;
using RadialWheel::IconRadius;
using RadialWheel::DeadZone;
using RadialWheel::Pi;
using RadialWheel::Preview;
using RadialWheel::Selection;

inline bool AllowsCommand(const char* command) {
	const char* allowed[] = {"+forcewheel", "+weaponwheel", "forcenext", "forceprev", "weapnext", "weapprev", "+forward", "+back", "+moveleft", "+moveright",
		"+moveup", "+movedown", "+speed", "+strafe", "+left", "+right"};
	for (const char* name : allowed) {
		std::size_t i = 0;
		while (name[i] && std::tolower(static_cast<unsigned char>(command[i])) == name[i]) ++i;
		if (!name[i] && (!command[i] || std::isspace(static_cast<unsigned char>(command[i])))) return true;
	}
	return false;
}

// Slot indices follow cgame's existing Force selection order, not power IDs.
struct Frame {
	int available = 0, current = 0;
	int energy = 0, activePowers = 0;
	bool allowed = false, open = false;
	int selected = -1, hovered = -1;
	float x = 0, y = 0;
};

inline int Highlighted(const Frame& frame) {
	return RadialWheel::Highlighted(frame.available, frame.current, frame.hovered, MaxPowers);
}

inline int Count(int mask) {
	return RadialWheel::Count(mask, MaxPowers);
}
inline int Slot(int mask, int sector) {
	return RadialWheel::Slot(mask, sector, MaxPowers);
}

} // namespace ForceWheel
