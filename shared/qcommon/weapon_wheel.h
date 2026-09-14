// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "qcommon/radial_wheel.h"

namespace WeaponWheel {
struct Frame {
	RadialWheel::View view;
	int weapon = 0;
	bool allowed = false, supported = false, visible = false;
};
} // namespace WeaponWheel
