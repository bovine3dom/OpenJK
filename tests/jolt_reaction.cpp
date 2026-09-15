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
	for (int i = 0; i < 240; ++i) { sim.Advance(1.0f / 120); Magnitude(sim.Sample()); }
	Check(Magnitude(sim.Sample(1)) < 0.1f, "motors return to animation");
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
	std::puts("PASS: Jolt reaction direction, joint limits, settling, reset, pause, invalid input, and fixed stepping");
}
