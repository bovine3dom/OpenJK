// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <memory>

namespace JoltReaction {
// Metres, kilograms, seconds. Local axes: forward X, left Y, up Z.
struct Rig {
	float torsoLength = 0.4f, torsoRadius = 0.14f;
	float headLength = 0.22f, headRadius = 0.09f;
};
struct Pose { float angles[2][3] = {}; }; // Relative pitch, yaw, roll in degrees.

class Simulation {
	struct Impl;
	std::unique_ptr<Impl> impl;
public:
	explicit Simulation(const Rig& rig);
	~Simulation();
	void Reset();
	void Impulse(int part, const float direction[3], const float point[3], float strength);
	bool Advance(float seconds);
	Pose Sample(float secondsAhead = 0) const;
	unsigned Steps() const;
};
}
