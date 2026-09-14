// SPDX-License-Identifier: GPL-2.0-or-later
#include "qcommon/force_wheel.h"
#include <cassert>
#include <limits>
#include <initializer_list>
#include <cstdio>

int main() {
	using namespace ForceWheel;
	for (const char* action : {"+forward", "+back", "+moveleft", "+moveright", "+moveup", "+movedown", "+speed", "+strafe", "+FORWARD "})
		assert(AllowsCommand(action));
	for (const char* action : {"+attack", "+altattack", "+useforce", "force_throw", "+forwardevil", "weapon 1"})
		assert(!AllowsCommand(action));
	Selection wheel;
	const int mask = (1 << 0) | (1 << 3) | (1 << 7) | (1 << 11);
	assert(Count(mask) == 4 && Slot(mask, 2) == 7 && Slot(mask, -1) == -1);
	assert(!wheel.Press(1, 0));
	assert(wheel.Press(1, mask));
	assert(!wheel.Press(1, mask));
	wheel.Move(0, -10);
	assert(wheel.Hovered() == -1);
	wheel.Move(0, -70);
	assert(wheel.Hovered() == 0);
	wheel.Move(60, 20); // Stay in the old sector close to the boundary.
	assert(wheel.Hovered() == 0);
	wheel.Move(8, 8);
	assert(wheel.Hovered() == 3);
	wheel.Move(std::numeric_limits<float>::quiet_NaN(), 0);
	assert(wheel.Hovered() == 3);
	assert(!wheel.Press(2, mask)); // A second binding holds the same wheel.
	assert(!wheel.Release(1) && wheel.Open());
	assert(wheel.Release(2) && !wheel.Open() && wheel.CapturesInput());
	assert(wheel.TakeSelection() == 3 && !wheel.CapturesInput());
	assert(wheel.TakeSelection() == -1);
	assert(wheel.Press(1, mask));
	wheel.Move(0, 500);
	assert(wheel.Hovered() == 7 && wheel.Y() <= Radius);
	wheel.Move(-200, -wheel.Y());
	assert(wheel.Hovered() == 11);
	wheel.Move(-wheel.X(), -wheel.Y());
	assert(wheel.Hovered() == -1);
	assert(wheel.Release(1) && wheel.TakeSelection() == -1);
	assert(wheel.Press(1, mask));
	wheel.Move(80, 0);
	wheel.Cancel();
	assert(!wheel.Open() && wheel.TakeSelection() == -1);
	assert(!wheel.Press(1, mask)); // Key repeat cannot reopen a cancelled wheel.
	assert(!wheel.Release(1));
	assert(wheel.Press(1, 1 << 7));
	wheel.Move(80, 0);
	assert(wheel.Hovered() == 7);
	assert(wheel.Release(-1) && wheel.TakeSelection() == 7);
	std::puts("PASS: Force wheel sectors, dead zone, hysteresis, dual bindings, release, and cancellation");
}
