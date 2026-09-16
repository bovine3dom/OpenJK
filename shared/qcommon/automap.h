// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cmath>
#include <cstring>

namespace Automap {
using Point = std::array<float, 3>;
constexpr int MaxMarkers = 256;
inline bool IsControl(const char *name, bool playerUsable) {
	return name && (!std::strcmp(name,"func_button") || (playerUsable &&
		(!std::strcmp(name,"func_usable") || !std::strcmp(name,"misc_model_breakable"))));
}
struct Marker { Point position; int entity, enabled; };
struct Frame {
	char map[64];
	Point player;
	float heading;
	int count;
	Marker markers[MaxMarkers];
};
struct Polygon { Point points[8]; int count; };
inline Polygon Clip(Polygon polygon, float height, bool above) {
	Polygon result = {};
	for (int i = 0; i < polygon.count; ++i) {
		const auto &a = polygon.points[i], &b = polygon.points[(i + 1) % polygon.count];
		const bool insideA = above ? a[2] >= height : a[2] <= height;
		const bool insideB = above ? b[2] >= height : b[2] <= height;
		if (insideA) result.points[result.count++] = a;
		if (insideA != insideB) {
			const float t = (height - a[2]) / (b[2] - a[2]);
			Point p;
			for (int j = 0; j < 3; ++j) p[j] = a[j] + t * (b[j] - a[j]);
			result.points[result.count++] = p;
		}
	}
	return result;
}
inline Point Project(const Point &p, const Point &centre, float yaw, float tilt) {
	const float a = yaw * 0.01745329252f, b = tilt * 0.01745329252f;
	const float dx = p[0] - centre[0], dy = p[1] - centre[1], dz = p[2] - centre[2];
	const float x = std::cos(a) * dx + std::sin(a) * dy;
	const float y = -std::sin(a) * dx + std::cos(a) * dy;
	return {{x, -std::cos(b) * y - std::sin(b) * dz, std::sin(b) * y - std::cos(b) * dz}};
}
}
