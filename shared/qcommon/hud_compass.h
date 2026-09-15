// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cmath>

namespace HudCompass {
// Map north is +Y. Positive offsets are clockwise, toward screen right.
inline float Offset(float heading, float bearing) { return std::remainder(heading - bearing, 360.0f); }
struct Marker { float offset, position; int edge, elevation; };
inline Marker Project(float heading, float dx, float dy, float dz) {
	const float offset = dx * dx + dy * dy < 1 ? 0 : Offset(heading, std::atan2(dy, dx) * 57.2957795f);
	return {offset, std::max(-1.0f, std::min(1.0f, offset / 90.0f)),
		offset < -90 ? -1 : offset > 90 ? 1 : 0, dz > 64 ? 1 : dz < -64 ? -1 : 0};
}
}
