// SPDX-License-Identifier: GPL-2.0-or-later
#include "physics/jolt_reaction.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

static void Check(bool condition, const char* message) {
	if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static float Magnitude(const JoltReaction::Pose& pose) {
	float result = 0;
	for (const auto& joint : pose.angles) for (float angle : joint) {
		Check(std::isfinite(angle) && std::abs(angle) < 22, "finite, bounded joint angles");
		result += std::abs(angle);
	}
	return result;
}
int main() {
	JoltReaction::Simulation sim{JoltReaction::Rig{}};
	const float forward[] = {1, 0, 0}, side[] = {0, 1, 0}, point[] = {0, 0, 0.32f};
	Check(Magnitude(sim.Sample()) < 0.001f, "neutral initial pose");
	sim.Impulse(0, forward, point, 5);
	Check(sim.Advance(0.05f), "advance first impact");
	const auto pitch = sim.Sample(1);
	Check(pitch.angles[0][0] > 0.5f && std::abs(pitch.angles[0][2]) < 0.1f, "forward hit produces pitch");
	for (int i = 0; i < 480; ++i) { sim.Advance(1.0f / 120); Magnitude(sim.Sample()); }
	std::printf("Reaction residual after four seconds: %.3f degrees\n", Magnitude(sim.Sample(1)));
	Check(Magnitude(sim.Sample(1)) < 0.5f, "motors return within half a degree of animation");
	sim.Reset();
	sim.Impulse(0, side, point, 5);
	sim.Advance(0.05f);
	Check(sim.Sample(1).angles[0][2] < -0.5f, "side hit produces roll");
	for (int i = 0; i < 300; ++i) { sim.Impulse(i % 2, forward, point, 1000); sim.Advance(0.016f); Magnitude(sim.Sample()); }
	sim.Reset();
	Check(Magnitude(sim.Sample()) == 0, "reset clears pose and velocities");
	Check(!sim.Advance(2) && !sim.Advance(-1), "discontinuities reset simulation");
	const float invalid[] = {std::numeric_limits<float>::quiet_NaN(), 0, 0};
	sim.Impulse(0, invalid, point, 5);
	sim.Advance(0.1f);
	Check(Magnitude(sim.Sample()) < 0.001f, "invalid impulse rejected");
	JoltReaction::Simulation other{JoltReaction::Rig{}};
	sim.Reset();
	sim.Impulse(0, forward, point, 5);
	other.Impulse(0, forward, point, 5);
	for (int i = 0; i < 10; ++i) sim.Advance(0.01f);
	for (int i = 0; i < 2; ++i) other.Advance(0.05f);
	Check(std::abs(sim.Sample(1).angles[0][0] - other.Sample(1).angles[0][0]) < 0.001f, "fixed-step frame partition independence");
	const auto paused = sim.Sample();
	sim.Advance(0);
	Check(Magnitude(paused) == Magnitude(sim.Sample()), "pause preserves state");
	JoltReaction::Part parts[JoltReaction::PartCount] = {};
	const float starts[][3] = {{0,0,1}, {0,0,1.15f}, {0,0,1.55f}, {0,.23f,1.5f}, {0,.35f,1.2f}, {0,-.23f,1.5f}, {0,-.35f,1.2f}, {0,.15f,1}, {.1f,.15f,.55f}, {0,-.15f,1}, {-.1f,-.15f,.55f}};
	const float ends[][3] = {{0,0,1.15f}, {0,0,1.55f}, {0,0,1.78f}, {0,.35f,1.2f}, {.2f,.35f,.95f}, {0,-.35f,1.2f}, {-.2f,-.35f,.95f}, {.1f,.15f,.55f}, {0,.15f,.08f}, {-.1f,-.15f,.55f}, {0,-.15f,.08f}};
	const int parents[] = {-1,0,1,1,3,1,5,0,7,0,9};
	for (int i = 0; i < JoltReaction::PartCount; ++i) {
		for (int r = 0; r < 3; ++r) { parts[i].bone.matrix[r][r] = 1; parts[i].bone.matrix[r][3] = starts[i][r]; parts[i].end[r] = ends[i][r]; }
		parts[i].parent = parents[i]; parts[i].mass = i < 2 ? 15 : 3; parts[i].radius = .06f;
	}
	const float velocity[] = {2,0,0};
	JoltReaction::FallSimulation falling(parts, velocity);
	const float floor[] = {-10,-10,0, 10,-10,0, 10,10,0, -10,-10,0, 10,10,0, -10,10,0};
	Check(falling.AddMesh(0, floor, 6), "collision mesh creation");
	falling.Impulse(1, forward, starts[2], 30);
	JoltReaction::Transform pose[JoltReaction::PartCount];
	for (int i = 0; i < 480; ++i) {
		Check(falling.Advance(1.0f/120), "fall solver step");
		falling.Sample(pose, 1);
		for (const auto& bone : pose) for (const auto& row : bone.matrix) for (float v : row) Check(std::isfinite(v), "finite full-body pose");
	}
	Check(pose[0].matrix[2][3] < .5f && pose[0].matrix[2][3] > -.1f, "gravity drops the pelvis and floor supports it");
	Check(pose[0].matrix[0][3] > .4f, "running momentum carries into fall");
	Check(falling.Speed() < .5f, "fall settles on collision floor");
	const float stopped[] = {0,0,0};
	JoltReaction::FallSimulation rider(parts, stopped);
	Check(rider.AddMesh(1, floor, 6), "kinematic mesh creation");
	JoltReaction::Transform platform = {};
	for (int i = 0; i < 3; ++i) platform.matrix[i][i] = 1;
	for (int i = 0; i < 480; ++i) {
		platform.matrix[2][3] = (i + 1) * .001f;
		rider.MoveMesh(1, platform, 1.0f / 120);
		Check(rider.Advance(1.0f / 120), "moving platform step");
	}
	rider.Sample(pose, 1);
	Check(pose[0].matrix[2][3] > .4f, "kinematic platform supports the rig");
	rider.SetMeshEnabled(1, false);
	for (int i = 0; i < 120; ++i) rider.Advance(1.0f / 120);
	rider.Sample(pose, 1);
	Check(pose[0].matrix[2][3] < -1, "non-solid or removed brush model stops colliding");
	std::puts("PASS: full-body gravity, momentum, floor contacts, finite poses, and settling");
	std::puts("PASS: Jolt reaction direction, joint limits, settling, reset, pause, invalid input, and fixed stepping");
}
