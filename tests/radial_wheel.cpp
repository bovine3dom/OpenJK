// SPDX-License-Identifier: GPL-2.0-or-later
#include "qcommon/radial_wheel.h"
#include "qcommon/force_wheel.h"
#include <cassert>
#include <cstdio>

int main() {
	RadialWheel::View view;
	view.slotCount = 17;
	view.available = (1u << 0) | (1u << 8) | (1u << 16);
	view.current = 16;
	assert(RadialWheel::Count(view.available, view.slotCount) == 3);
	assert(RadialWheel::Slot(view.available, 1, view.slotCount) == 8);
	assert(RadialWheel::Highlighted(view) == 16);
	view.hovered = 8;
	assert(RadialWheel::Highlighted(view) == 8);
	view.slotCount = 8;
	assert(RadialWheel::Highlighted(view) == -1);
	assert(RadialWheel::Count(0xffffffffu, 32) == 32);
	assert(RadialWheel::Slot(0x80000000u, 0, 32) == 31);
	assert(RadialWheel::Slot(0x80000000u, 1, 32) == -1);
	assert(RadialWheel::Count(0xffffffffu, 0) == 0);
	assert(ForceWheel::Count(0xffff) == ForceWheel::MaxPowers);
	assert(ForceWheel::AllowsCommand("weapnext") && ForceWheel::AllowsCommand("weapprev"));
	std::puts("PASS: radial wheel slot limits, sparse inventories, and cycling commands");
}
