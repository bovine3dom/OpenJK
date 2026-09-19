// SPDX-License-Identifier: GPL-2.0-or-later
#include "qcommon/passcode_overlay.h"
#include <cassert>
#include <cstdio>

int main()
{
	using namespace PasscodeOverlay;
	assert(ObjectiveCode("KEJIM_POST_OBJ3") == KejimRed);
	assert(ObjectiveCode("KEJIM_POST_OBJ4") == KejimGreen);
	assert(ObjectiveCode("KEJIM_POST_OBJ5") == KejimBlue);
	assert(ObjectiveCode("DOOM_COMM_OBJ4") == DoomgiverComm);
	assert(ObjectiveCode("NS_STARPAD_OBJ4") == -1);
	assert(MarkerCode("ns_red_fuel") == NarShaddaaRedFuel);
	assert(MarkerCode("ns_blue_fuel") == NarShaddaaBlueFuel);
	assert(MarkerCode("unknown") == -1);
	for (int code = 0; code < CodeCount; ++code) assert(Bit(code) == (1u << code));
	assert(Bit(-1) == 0);
	assert(Bit(CodeCount) == 0);
	std::puts("PASS: passcode objective and marker mappings");
}
