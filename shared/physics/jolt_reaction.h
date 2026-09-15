// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <memory>

namespace JoltReaction {
constexpr int PartCount = 13;
struct Transform { float matrix[3][4]; };
void BlendTransforms(const Transform* from, Transform* to, int count, float alpha);
struct Part {
	Transform bone;
	float end[3];
	float radius, mass;
	int parent;
};
enum class ControlPhase { Shadow, Tracking, Stepping, Falling };
struct BalanceStatus {
	ControlPhase phase = ControlPhase::Falling;
	float error = 0, strength = 0, pelvisHeight = 0;
	unsigned corrections = 0;
	float targetChange = 0;
};
class CollisionScene {
	struct Impl;
	std::shared_ptr<Impl> impl;
	friend class FallSimulation;
public:
	CollisionScene();
	~CollisionScene();
	bool AddMesh(int model, const float* vertices, int count);
};
class FallSimulation {
	struct Impl;
	std::unique_ptr<Impl> impl;
public:
	FallSimulation(const Part* parts, const float* velocity, float gravity = 20.32f);
	~FallSimulation();
	bool AddMesh(int model, const float* vertices, int count);
	bool UseScene(const CollisionScene& scene);
	void MoveMesh(int model, const Transform& transform, float seconds);
	void SetMeshEnabled(int model, bool enabled);
	void AddVelocity(const float* velocity);
	void Impulse(int part, const float* direction, const float* point, float strength);
	// Keep the same bodies and their velocities when moving from animation to active control.
	void Follow(const Part* pose, float seconds);
	void Engage();
	void Drive(const Part* pose, const float* desiredVelocity, float seconds);
	void React(int part, const float* direction, const float* point, float impulse, float weakness);
	void ReleaseControl();
	BalanceStatus Balance() const;
	void RootVelocity(float* velocity) const;
	bool Advance(float seconds);
	// Signed time offset from the last Advance boundary; zero is authoritative now.
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
	// Signed time offset from the last Advance boundary.
	Pose Sample(float secondsAhead = 0) const;
	unsigned Steps() const;
};
}
