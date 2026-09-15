// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <memory>

namespace JoltReaction {
constexpr int PartCount = 11;
struct Transform { float matrix[3][4]; };
void BlendTransforms(const Transform* from, Transform* to, int count, float alpha);
struct Part {
	Transform bone;
	float end[3];
	float radius, mass;
	int parent;
};
class FallSimulation {
	struct Impl;
	std::unique_ptr<Impl> impl;
public:
	FallSimulation(const Part* parts, const float* velocity, float gravity = 20.32f);
	~FallSimulation();
	bool AddMesh(int model, const float* vertices, int count);
	void MoveMesh(int model, const Transform& transform, float seconds);
	void SetMeshEnabled(int model, bool enabled);
	void AddVelocity(const float* velocity);
	void Impulse(int part, const float* direction, const float* point, float strength);
	bool Advance(float seconds);
	void Sample(Transform* bones, float ahead = 0) const;
	float Speed() const;
	unsigned Steps() const;
	void Bounds(float* mins, float* maxs) const;
};
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
