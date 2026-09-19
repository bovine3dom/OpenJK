// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstring>

namespace PasscodeOverlay {

enum Code {
	KejimRed,
	KejimGreen,
	KejimBlue,
	NarShaddaaRedFuel,
	NarShaddaaBlueFuel,
	DoomgiverComm,
	CodeCount
};

struct Frame {
	unsigned active = 0;
};

inline unsigned Bit(int code) { return code >= 0 && code < CodeCount ? 1u << code : 0u; }

inline int ObjectiveCode(const char* name) {
	if (!name) return -1;
	if (!std::strcmp(name, "KEJIM_POST_OBJ3")) return KejimRed;
	if (!std::strcmp(name, "KEJIM_POST_OBJ4")) return KejimGreen;
	if (!std::strcmp(name, "KEJIM_POST_OBJ5")) return KejimBlue;
	if (!std::strcmp(name, "DOOM_COMM_OBJ4")) return DoomgiverComm;
	return -1;
}

inline int MarkerCode(const char* name) {
	if (!name) return -1;
	if (!std::strcmp(name, "ns_red_fuel")) return NarShaddaaRedFuel;
	if (!std::strcmp(name, "ns_blue_fuel")) return NarShaddaaBlueFuel;
	return -1;
}

} // namespace PasscodeOverlay
