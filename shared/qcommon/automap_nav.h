// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "automap.h"
#include <vector>

namespace Automap {
struct NavFace { std::vector<Point> points; int floor = 0; };
struct Floor { float height; };
struct FloorLink { Point a, b; int from, to; };
struct NavMap {
	std::vector<NavFace> faces;
	std::vector<Floor> floors;
	std::vector<FloorLink> links;
	// Input triangles use game coordinates (Z up) and outward winding.
	bool Build(const std::vector<Point> &triangles);
	int FloorAt(const Point &point) const;
};
}
