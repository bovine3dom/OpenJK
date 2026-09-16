// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cmath>
#include <cstring>
#include <utility>

namespace Automap {
using Point = std::array<float, 3>;
constexpr int MaxMarkers = 256;
constexpr int MaxLifts = 64, MaxStops = 8;
constexpr float ViewLeft = 24, ViewTop = 66, ViewWidth = 592, ViewHeight = 310;
inline Point DragPan(float dx, float dy, float span, float yaw, float tilt, float aspect) {
	const float angle = yaw * 0.01745329252f;
	const float x = -dx * 2 * span / ViewWidth;
	const float y = dy * 2 * span / (ViewWidth * aspect * std::cos(tilt * 0.01745329252f));
	return {{std::cos(angle)*x-std::sin(angle)*y, std::sin(angle)*x+std::cos(angle)*y, 0}};
}
inline bool IsControl(const char *name, bool playerUsable) {
	return name && (!std::strcmp(name,"func_button") || (playerUsable &&
		(!std::strcmp(name,"func_usable") || !std::strcmp(name,"misc_model_breakable"))));
}
struct Marker { Point position; int entity, enabled; };
struct Lift { Point position; int entity, count, ordered; Point stops[MaxStops]; };
struct Frame {
	char map[64];
	Point player;
	float heading;
	int count;
	Marker markers[MaxMarkers];
	int liftCount;
	Lift lifts[MaxLifts];
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
inline bool ClipSegment(Point &a, Point &b, float low, float high) {
	if (a[2] > b[2]) std::swap(a,b);
	if (a[2] > high || b[2] < low) return false;
	const Point start=a, end=b;
	if (a[2] < low) for (int j=0;j<3;++j) a[j]=start[j]+(end[j]-start[j])*(low-start[2])/(end[2]-start[2]);
	if (b[2] > high) for (int j=0;j<3;++j) b[j]=start[j]+(end[j]-start[j])*(high-start[2])/(end[2]-start[2]);
	return true;
}
inline Point Project(const Point &p, const Point &centre, float yaw, float tilt) {
	const float a = yaw * 0.01745329252f, b = tilt * 0.01745329252f;
	const float dx = p[0] - centre[0], dy = p[1] - centre[1], dz = p[2] - centre[2];
	const float x = std::cos(a) * dx + std::sin(a) * dy;
	const float y = -std::sin(a) * dx + std::cos(a) * dy;
	return {{x, -std::cos(b) * y - std::sin(b) * dz, std::sin(b) * y - std::cos(b) * dz}};
}
}
