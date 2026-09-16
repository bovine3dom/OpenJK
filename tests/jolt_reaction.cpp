// SPDX-License-Identifier: GPL-2.0-or-later
#include "physics/jolt_reaction.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <algorithm>
#include <initializer_list>
#include "jolt_stormtrooper_pose.h"

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
	JoltReaction::Transform startPose = {{{1,0,0,0},{0,1,0,0},{0,0,1,0}}};
	JoltReaction::Transform endPose = {{{-1,0,0,.6f},{0,-1,0,0},{0,0,1,0}}};
	const float duration = JoltReaction::RecoveryDuration(&startPose, &endPose, 1);
	Check(duration > 2, "large get-up corrections receive sufficient time");
	auto previousPose = startPose;
	for (int frame = 1; frame <= int(std::ceil(duration*120)); ++frame) {
		auto pose = endPose;
		JoltReaction::BlendRecovery(&startPose, &pose, 1, frame/(120*duration));
		Check(std::abs(pose.matrix[0][3]-previousPose.matrix[0][3])*120 <= .751f, "get-up translation speed is bounded");
		float trace = 0;
		for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) trace += pose.matrix[r][c]*previousPose.matrix[r][c];
		Check(std::acos(std::max(-1.0f, std::min(1.0f, (trace-1)*.5f)))*120 < 2.65f, "get-up angular speed is bounded");
		previousPose = pose;
	}
	Check(previousPose.matrix[0][3] == endPose.matrix[0][3], "get-up blend reaches the fixed first frame");
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
	const float starts[][3] = {{0,0,1}, {0,0,1.15f}, {0,0,1.55f}, {0,.23f,1.5f}, {0,.35f,1.2f}, {0,-.23f,1.5f}, {0,-.35f,1.2f}, {0,.15f,1}, {.1f,.15f,.55f}, {0,-.15f,1}, {.1f,-.15f,.55f}, {0,.15f,.08f}, {0,-.15f,.08f}};
	const float ends[][3] = {{0,0,1.15f}, {0,0,1.55f}, {0,0,1.78f}, {0,.35f,1.2f}, {.2f,.35f,.95f}, {0,-.35f,1.2f}, {-.2f,-.35f,.95f}, {.1f,.15f,.55f}, {0,.15f,.08f}, {.1f,-.15f,.55f}, {0,-.15f,.08f}, {.14f,.15f,.08f}, {.14f,-.15f,.08f}};
	const int parents[] = {-1,0,1,1,3,1,5,0,7,0,9,8,10};
	for (int i = 0; i < JoltReaction::PartCount; ++i) {
		for (int r = 0; r < 3; ++r) { parts[i].bone.matrix[r][r] = 1; parts[i].bone.matrix[r][3] = starts[i][r]; parts[i].end[r] = ends[i][r]; }
		parts[i].parent = parents[i]; parts[i].mass = i < 2 ? 15 : 3; parts[i].radius = .06f;
	}
	const float velocity[] = {2,0,0};
	// A 20 Hz server must still supply a distinct, correctly timed pose each display frame.
	for (int fps : {60, 120, 144}) {
		JoltReaction::FallSimulation history(parts, velocity, 0);
		JoltReaction::FallSimulation reference(parts, velocity, 0);
		history.Impulse(1, side, starts[2], 2);
		reference.Impulse(1, side, starts[2], 2);
		JoltReaction::Transform sampled[JoltReaction::PartCount], expected[JoltReaction::PartCount];
		double server = 0, referenceTime = 0;
		float previous = 0;
		for (int frame = 1; frame <= fps; ++frame) {
			const double display = double(frame) / fps;
			while (server + .05 <= display + .000001) { Check(history.Advance(.05f), "batched history step"); server += .05; }
			const double query = std::max(0.0, display - .05);
			while (referenceTime < query - .000001) { reference.Advance(1.0f / 120); referenceTime += 1.0 / 120; }
			history.Sample(sampled, float(query - server));
			reference.Sample(expected, float(query - referenceTime));
			const float x = sampled[0].matrix[0][3];
			Check(std::abs(x - expected[0].matrix[0][3]) < .001f, "sample retains intermediate substeps across server interval");
			for (int p = 0; p < JoltReaction::PartCount; ++p) for (int r = 0; r < 3; ++r) for (int c = 0; c < 4; ++c)
				Check(std::abs(sampled[p].matrix[r][c]-expected[p].matrix[r][c]) < .001f, "timestamped limb rotation and position match the reference");
			if (query > .02) Check(x > previous + .001f, "no held poses between server ticks");
			previous = x;
		}
	}
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
	float carry[3]; rider.SurfaceVelocity(1, stopped, carry);
	Check(std::abs(carry[2]-.12f) < .001f, "handoff can retain moving-platform velocity");
	rider.SetMeshEnabled(1, false);
	for (int i = 0; i < 120; ++i) rider.Advance(1.0f / 120);
	rider.Sample(pose, 1);
	Check(pose[0].matrix[2][3] < -1, "non-solid or removed brush model stops colliding");
	JoltReaction::FallSimulation controlled(parts, stopped);
	Check(controlled.AddMesh(0, floor, 6), "controlled rig collision floor");
	controlled.Follow(parts, 0);
	controlled.Engage();
	for (int i = 0; i < 240; ++i) {
		if (i % 6 == 0) controlled.Drive(parts, stopped, .05f);
		Check(controlled.Advance(1.0f / 120), "standing controller step");
	}
	std::printf("Standing: phase=%d height=%.3f error=%.3f steps=%u\n", int(controlled.Balance().phase), controlled.Balance().pelvisHeight, controlled.Balance().error, controlled.Balance().corrections);
	Check(controlled.Balance().phase != JoltReaction::ControlPhase::Falling, "controller maintains standing support");
	controlled.React(1, forward, starts[2], 1.0f, .25f);
	float visible = 0, peakError = 0;
	for (int i = 0; i < 240; ++i) {
		if (i % 6 == 0) controlled.Drive(parts, stopped, .05f);
		controlled.Advance(1.0f / 120); controlled.Sample(pose);
		visible = std::max(visible, std::abs(pose[1].matrix[0][2]));
		peakError = std::max(peakError, controlled.Balance().error);
	}
	std::printf("Mild: phase=%d height=%.3f error=%.3f steps=%u visible=%.3f peakerror=%.3f\n", int(controlled.Balance().phase), controlled.Balance().pelvisHeight, controlled.Balance().error, controlled.Balance().corrections, visible, peakError);
	Check(visible > .07f, "small hit has readable motor-driven recoil");
	Check(controlled.Balance().phase != JoltReaction::ControlPhase::Falling, "small hit does not force a fall");
	controlled.React(7, side, starts[7], 1.0f, .82f);
	for (int i = 0; i < 360; ++i) {
		if (i % 6 == 0) controlled.Drive(parts, stopped, .05f);
		controlled.Advance(1.0f / 120);
	}
	std::printf("Shove: phase=%d height=%.3f error=%.3f steps=%u\n", int(controlled.Balance().phase), controlled.Balance().pelvisHeight, controlled.Balance().error, controlled.Balance().corrections);
	Check(controlled.Balance().corrections > 0, "leg disturbance starts a corrective step attempt");
	Check(controlled.Balance().phase == JoltReaction::ControlPhase::Falling, "severe leg weakness permits a fall");
	for (float speed : {.4f, .8f, 1.0f}) {
		JoltReaction::FallSimulation pushed(parts, stopped);
		pushed.AddMesh(0, floor, 6); pushed.Follow(parts, 0); pushed.Engage();
		for (int i = 0; i < 240; ++i) {
			if (i%6 == 0) pushed.Drive(parts, stopped, .05f);
			Check(pushed.Advance(1.0f/120), "pre-push standing step");
		}
		const float push[] = {speed,0,0}; pushed.AddVelocity(push);
		for (int i = 0; i < 600; ++i) {
			if (i%6 == 0) pushed.Drive(parts, stopped, .05f);
			Check(pushed.Advance(1.0f/120), "balance recovery step");
			Check(pushed.Balance().phase != JoltReaction::ControlPhase::Falling, "moderate push remains recoverable");
		}
		std::printf("Push %.2f: phase=%d steps=%u landed=%u error=%.3f height=%.3f\n", speed, int(pushed.Balance().phase), pushed.Balance().corrections, pushed.Balance().landings, pushed.Balance().error, pushed.Balance().pelvisHeight);
		Check(pushed.Balance().phase == JoltReaction::ControlPhase::Tracking && pushed.Balance().error < .10f, "push settles into a balanced stance");
		if (speed >= 1.0f) Check(pushed.Balance().landings > 0, "corrective step lands before balance recovery");
		float before[3], after[3]; pushed.RootVelocity(before); pushed.Sample(pose);
		pushed.ReleaseControl(); pushed.RootVelocity(after);
		JoltReaction::Transform released[JoltReaction::PartCount]; pushed.Sample(released);
		for (int r = 0; r < 3; ++r) Check(before[r] == after[r], "release adds no root velocity");
		for (int p = 0; p < JoltReaction::PartCount; ++p) for (int r = 0; r < 3; ++r) for (int c = 0; c < 4; ++c)
			Check(pose[p].matrix[r][c] == released[p].matrix[r][c], "release preserves every bone pose");
	}
	const float stormStart[][3] = {{0,0,.81f},{0,-.02f,.95f},{.04f,.02f,1.34f},{-.09f,.11f,1.29f},{0,.20f,1.04f},{.12f,-.11f,1.29f},{.15f,-.23f,1.04f},{-.01f,.09f,.82f},{.07f,.17f,.46f},{.04f,-.08f,.80f},{.12f,-.10f,.43f},{.06f,.23f,.10f},{-.04f,-.10f,.10f}};
	const float stormEnd[][3] = {{0,-.02f,.95f},{.04f,.02f,1.34f},{.06f,.04f,1.48f},{0,.20f,1.04f},{.22f,.20f,1.01f},{.15f,-.23f,1.04f},{.21f,-.02f,1.01f},{.07f,.17f,.46f},{.06f,.23f,.10f},{.12f,-.10f,.43f},{-.04f,-.10f,.10f},{.16f,.33f,.10f},{.06f,0,.10f}};
	const float stormMass[] = {12,24,5,3,2,3,2,8,4,8,4,1.5f,1.5f};
	const float stormRadius[] = {.06f,.14f,.06f,.055f,.045f,.055f,.045f,.085f,.06f,.085f,.06f,.06f,.06f};
	for (int i = 0; i < JoltReaction::PartCount; ++i) {
		for (int r = 0; r < 3; ++r) { parts[i].bone.matrix[r][3] = stormStart[i][r]; parts[i].end[r] = stormEnd[i][r]; }
		parts[i].mass = stormMass[i]; parts[i].radius = stormRadius[i];
	}
	JoltReaction::Part walking[JoltReaction::PartCount];
	std::copy(parts, parts + JoltReaction::PartCount, walking);
	JoltReaction::FallSimulation moving(parts, stopped, 0);
	moving.Follow(parts, 0);
	for (int frame = 0; frame < 20; ++frame) {
		for (auto& part : walking) { part.bone.matrix[0][3] += .05f; part.end[0] += .05f; }
		moving.Follow(walking, .05f);
	}
	float before[3], after[3]; moving.RootVelocity(before); moving.Sample(pose);
	moving.Engage(); moving.RootVelocity(after);
	Check(std::abs(before[0]-1) < .001f, "shadow rig carries animation root velocity");
	for (int r = 0; r < 3; ++r) Check(before[r] == after[r], "engagement preserves moving root velocity");
	const float navigation[] = {2,0,0};
	moving.SetRootVelocity(navigation); moving.RootVelocity(after);
	Check(std::abs(after[0]-2) < .001f, "handoff can match authoritative locomotion velocity");
	moving.ReleaseControl(); Check(moving.Advance(1.0f/120), "moving release step");
	JoltReaction::Transform carried[JoltReaction::PartCount]; moving.Sample(carried);
	Check(carried[0].matrix[0][3] > pose[0].matrix[0][3]+.005f, "momentum continues through the moving fall handoff");
	JoltReaction::FallSimulation storm(parts, stopped);
	storm.AddMesh(0, floor, 6); storm.Follow(parts, 0); storm.Engage();
	for (int i = 0; i < 600; ++i) { if (i % 6 == 0) storm.Drive(parts, stopped, .05f); storm.Advance(1.0f/120); }
	std::printf("Storm: phase=%d error=%.3f steps=%u height=%.3f\n", int(storm.Balance().phase), storm.Balance().error, storm.Balance().corrections, storm.Balance().pelvisHeight);
	Check(storm.Balance().phase != JoltReaction::ControlPhase::Falling, "stock-proportioned rig remains balanced without a hit");
	const float diagonal[] = {.7071068f,.7071068f,0};
	storm.React(1, diagonal, stormEnd[1], .6f, .125f);
	for (int i = 0; i < 1200; ++i) {
		if (i%6 == 0) storm.Drive(parts, stopped, .05f);
		Check(storm.Advance(1.0f/120), "stock-proportioned recoil step");
		Check(storm.Balance().phase != JoltReaction::ControlPhase::Falling, "stock-proportioned mild hit stays balanced for ten seconds");
	}
	std::puts("PASS: full-body gravity, momentum, floor contacts, finite poses, and settling");
	for (int i = 0; i < JoltReaction::PartCount; ++i) {
		for (int r = 0; r < 3; ++r) for (int c = 0; c < 4; ++c) parts[i].bone.matrix[r][c] = StockPose[i][r*4+c];
		for (int r = 0; r < 3; ++r) parts[i].end[r] = StockPose[i][12+r];
		parts[i].radius = StockPose[i][15]; parts[i].mass = StockPose[i][16];
	}
	const float largeFloor[] = {-200,-200,0, 200,-200,0, 200,200,0, -200,-200,0, 200,200,0, -200,200,0};
	for (int delay : {90, 120, 150}) {
		JoltReaction::FallSimulation captured(parts, stopped);
		captured.AddMesh(0, largeFloor, 6); captured.Follow(parts, 0); captured.Engage();
		for (int i = 0; i < delay; ++i) {
			if (i%6 == 0) captured.Drive(parts, stopped, .05f);
			Check(captured.Advance(1.0f/120), "captured standing step");
		}
		const float shove[] = {1.3f,0,0}; captured.AddVelocity(shove);
		for (int i = 0; i < 840; ++i) {
			if (i%6 == 0) captured.Drive(parts, stopped, .05f);
			Check(captured.Advance(1.0f/120), "captured stance control step");
			const auto b = captured.Balance();
			Check(b.phase != JoltReaction::ControlPhase::Falling, "captured stance stays upright through the correction");
			Check(b.assistForce <= 220.01f && b.assistTorque <= 80.01f, "root assistance remains bounded");
			if (!b.contacts) Check(b.assistForce == 0 && b.assistTorque == 0, "root assistance needs actual foot contact");
		}
		std::printf("Captured: phase=%d steps=%u landed=%u error=%.3f\n", int(captured.Balance().phase), captured.Balance().corrections, captured.Balance().landings, captured.Balance().error);
		Check(captured.Balance().phase == JoltReaction::ControlPhase::Tracking && captured.Balance().landings > 0, "captured stock stance recovers after a push");
		Check(captured.Balance().error < .1f, "captured stance settles over its foot support");
	}
	std::puts("PASS: Jolt reaction direction, joint limits, settling, reset, pause, invalid input, and fixed stepping");
	JoltReaction::Transform blocked[JoltReaction::PartCount];
	for (int i = 0; i < JoltReaction::PartCount; ++i) {
		blocked[i] = parts[i].bone;
		if (i >= 3 && i <= 6) blocked[i].matrix[0][3] += .4f;
	}
	JoltReaction::FallSimulation clearance(parts, stopped);
	Check(clearance.RecoveryPathClear(blocked), "unobstructed arm handoff is permitted");
	const float wall[] = {140.15f,-105,0, 140.15f,-102,0, 140.15f,-102,2, 140.15f,-105,0, 140.15f,-102,2, 140.15f,-105,2};
	clearance.AddMesh(0, wall, 6);
	Check(!clearance.RecoveryPathClear(blocked), "arm handoff cannot pass through a wall");
	JoltReaction::FallSimulation airborne(parts, stopped);
	airborne.Follow(parts, 0); airborne.Engage(); airborne.ReleaseControl();
	for (int i = 0; i < 120; ++i) {
		airborne.Advance(1.0f/120);
		Check(airborne.Balance().braceMask == 0, "bracing needs a sensed surface");
	}
	const float back[] = {-1,0,0};
	for (const auto& direction : {forward, side, back}) {
		JoltReaction::FallSimulation brace(parts, stopped);
		brace.AddMesh(0, largeFloor, 6); brace.Follow(parts, 0); brace.Engage();
		brace.Impulse(1, direction, parts[1].end, 25); brace.ReleaseControl();
		unsigned attempts = 0, contacts = 0;
		for (int i = 0; i < 600; ++i) {
			Check(brace.Advance(1.0f/120), "braced fall solver step");
			const auto b = brace.Balance(); attempts |= b.braceMask; contacts |= b.handContacts;
			Check(b.assistForce == 0 && b.assistTorque == 0, "bracing does not apply root assistance");
		}
		std::printf("Brace: targets=%u contacts=%u speed=%.3f hand=%u head=%u\n", attempts, contacts, brace.Speed(), brace.Balance().firstHandContact, brace.Balance().firstHeadContact);
		Check(attempts != 0 && contacts != 0, "fall reaches a surface and records hand contact");
		if (direction == forward) Check(!brace.Balance().firstHeadContact || brace.Balance().firstHandContact <= brace.Balance().firstHeadContact, "forward bracing places a hand before the head strikes");
		brace.Sample(pose);
		JoltReaction::Transform target[JoltReaction::PartCount];
		std::copy(pose, pose+JoltReaction::PartCount, target);
		target[4].matrix[0][3] += .08f; target[6].matrix[0][3] += .08f;
		brace.PrepareRecovery(target, 1);
		JoltReaction::Transform unchanged[JoltReaction::PartCount]; brace.Sample(unchanged);
		for (int i = 0; i < JoltReaction::PartCount; ++i) for (int r = 0; r < 3; ++r) for (int c = 0; c < 4; ++c)
			Check(pose[i].matrix[r][c] == unchanged[i].matrix[r][c], "preparation does not teleport any bone");
		const auto steps = brace.Steps();
		for (int i = 0; i < 180; ++i) {
			Check(brace.Advance(1.0f/120), "physical get-up preparation step");
			Check(brace.Balance().assistForce == 0 && brace.Balance().assistTorque == 0, "preparation has no root tether");
		}
		Check(brace.Steps() > steps && brace.Balance().phase == JoltReaction::ControlPhase::Preparing, "collision simulation continues during preparation");
		float before[3], after[3]; brace.RootVelocity(before);
		brace.ReleaseControl(); brace.RootVelocity(after);
		Check(brace.Balance().phase == JoltReaction::ControlPhase::Falling, "preparation can be interrupted");
		for (int r = 0; r < 3; ++r) Check(before[r] == after[r], "interrupting preparation preserves momentum");
		brace.Sample(pose); brace.RootVelocity(before);
		brace.Kill(); brace.Sample(unchanged); brace.RootVelocity(after);
		for (int i = 0; i < JoltReaction::PartCount; ++i) for (int r = 0; r < 3; ++r) for (int c = 0; c < 4; ++c)
			Check(pose[i].matrix[r][c] == unchanged[i].matrix[r][c], "death preserves the current physical pose");
		for (int r = 0; r < 3; ++r) Check(before[r] == after[r], "death adds no velocity");
		for (int i = 0; i < 360; ++i) brace.Advance(1.0f/120);
		Check(brace.Balance().phase == JoltReaction::ControlPhase::Dead && brace.Balance().strength == 0, "corpses remain passive and cannot get up");
	}
	{
		JoltReaction::FallSimulation dying(parts, stopped);
		dying.AddMesh(0, largeFloor, 6); dying.Follow(parts, 0); dying.Engage();
		dying.Sample(pose);
		float before[3], after[3]; dying.RootVelocity(before);
		dying.Kill(true); dying.RootVelocity(after);
		JoltReaction::Transform unchanged[JoltReaction::PartCount]; dying.Sample(unchanged);
		for (int i = 0; i < JoltReaction::PartCount; ++i) for (int r = 0; r < 3; ++r) for (int c = 0; c < 4; ++c)
			Check(pose[i].matrix[r][c] == unchanged[i].matrix[r][c], "guided death preserves the initial pose");
		for (int r = 0; r < 3; ++r) Check(before[r] == after[r], "guided death preserves momentum");
		Check(dying.Balance().strength > 0, "standing death retains initial muscle support");
		float strength = dying.Balance().strength;
		for (int i = 0; i < 120; ++i) {
			Check(dying.Advance(1.0f/120), "guided death solver step");
			const auto b = dying.Balance();
			Check(b.strength <= strength && b.assistForce == 0 && b.assistTorque == 0, "death strength fades without root assistance");
			strength = b.strength;
		}
		Check(strength == 0 && dying.Balance().phase == JoltReaction::ControlPhase::Dead, "guided death ends in a passive corpse");
	}
	{
		JoltReaction::FallSimulation injured(parts, stopped);
		injured.AddMesh(0, largeFloor, 6); injured.Follow(parts, 0); injured.Engage();
		injured.SetVitality(.25f);
		for (int hit = 0; hit < 3; ++hit) {
			injured.React(1, forward, parts[1].end, .2f, .3f);
			for (int i = 0; i < 24; ++i) Check(injured.Advance(1.0f/120), "injured balance step");
		}
		const float weakened = injured.Balance().strength;
		Check(weakened < .9f && weakened > .7f, "health and repeated hits reduce support within bounds");
		for (int i = 0; i < 600; ++i) injured.Advance(1.0f/120);
		Check(injured.Balance().phase == JoltReaction::ControlPhase::Tracking && injured.Balance().strength > weakened,
			"recent-hit stress recovers without making low health alone force a fall");
	}
	std::puts("PASS: contact-aware bracing, physical preparation, and speed-limited get-up transitions");
	{
		JoltReaction::FallSimulation weakLegs(parts, stopped);
		weakLegs.AddMesh(0, largeFloor, 6); weakLegs.Follow(parts, 0); weakLegs.Engage();
		JoltReaction::RegionalControl profile;
		profile.strength[int(JoltReaction::Region::Legs)] = 0;
		profile.strength[int(JoltReaction::Region::Feet)] = 0;
		weakLegs.SetRegionalControl(profile);
		for (int i = 0; i < 360; ++i) Check(weakLegs.Advance(1.0f/120), "regional control solver step");
		Check(weakLegs.Balance().phase == JoltReaction::ControlPhase::Falling, "unsupported legs yield independently of upper-body motors");
		JoltReaction::FallSimulation a(parts, stopped), b(parts, stopped);
		a.AddMesh(0, largeFloor, 6); b.AddMesh(0, largeFloor, 6);
		a.Follow(parts, 0); b.Follow(parts, 0); a.Engage(); b.Engage();
		for (int frame = 0; frame < 30; ++frame) {
			a.Electrocute(.7f); b.Electrocute(.7f);
			for (int i = 0; i < 6; ++i) Check(a.Advance(1.0f/120), "Lightning substep");
			Check(b.Advance(.05f), "Lightning server step");
		}
		JoltReaction::Transform other[JoltReaction::PartCount]; a.Sample(pose); b.Sample(other);
		for (int i = 0; i < JoltReaction::PartCount; ++i) for (int r = 0; r < 3; ++r) for (int c = 0; c < 4; ++c)
			Check(std::abs(pose[i].matrix[r][c]-other[i].matrix[r][c]) < .001f, "Lightning motion is independent of frame partition");
	}
	{
		JoltReaction::FallSimulation held(parts, stopped);
		held.AddMesh(0, largeFloor, 6); held.Follow(parts, 0); held.Engage();
		float target[] = {parts[2].bone.matrix[0][3],parts[2].bone.matrix[1][3],parts[2].bone.matrix[2][3]+.8f};
		float mass = 0; for (const auto& part : parts) mass += part.mass;
		held.Grip(parts, target, false);
		for (int i = 0; i < 120; ++i) held.Advance(1.0f/120);
		Check(held.Balance().gripping && held.Balance().gripForce == 0 && held.Balance().phase == JoltReaction::ControlPhase::Tracking,
			"level one Grip keeps ground support without lift");
		JoltReaction::RegionalControl profile;
		profile.strength[int(JoltReaction::Region::Legs)] = .18f;
		profile.strength[int(JoltReaction::Region::Feet)] = .1f;
		held.SetRegionalControl(profile); held.Grip(parts, target, true);
		for (int i = 0; i < 360; ++i) {
			if (i % 12 == 0) held.Electrocute(.6f);
			Check(held.Advance(1.0f/120), "Grip and Lightning share one physical rig");
			const auto b = held.Balance();
			Check(b.gripForce <= mass*80+.1f && b.assistForce == 0 && b.assistTorque == 0, "suspension uses a bounded external force without standing assistance");
		}
		held.Sample(pose);
		std::printf("Grip: neck=%.3f target=%.3f force=%.1f shock=%.2f\n", pose[2].matrix[2][3], target[2], held.Balance().gripForce, held.Balance().shock);
		Check(std::abs(pose[2].matrix[2][3]-target[2]) < .2f, "Grip lifts the body to its suspension target");
		float before[3], after[3]; held.RootVelocity(before);
		held.ReleaseGrip(); held.SetRegionalControl(JoltReaction::RegionalControl{}); held.RootVelocity(after);
		for (int r = 0; r < 3; ++r) Check(before[r] == after[r], "Grip release preserves velocity");
		for (int i = 0; i < 180; ++i) Check(held.Advance(1.0f/120), "released body falls with finite state");
		Check(!held.Balance().gripping && held.Balance().gripForce == 0 && held.Balance().shock == 0,
			"Grip releases and Lightning expires without a continuing force");
		held.Grip(parts, target, true); held.Electrocute(1); held.Kill();
		Check(held.Balance().gripping && held.Balance().shock == 0 && held.Balance().strength == 0, "death ends muscle control but preserves external Grip");
		held.ReleaseGrip();
		Check(!held.Balance().gripping && held.Balance().phase == JoltReaction::ControlPhase::Dead, "releasing a held corpse does not revive its controller");
	}
	{
		JoltReaction::FallSimulation corpse(parts, stopped);
		corpse.AddMesh(1, largeFloor, 6); corpse.Follow(parts, 0); corpse.Engage(); corpse.Kill();
		for (int i = 0; i < 1200 && corpse.Awake(); ++i) Check(corpse.Advance(1.0f/120), "corpse settling step");
		Check(!corpse.Awake(), "corpse sleeps on the platform");
		const auto steps = corpse.Steps();
		corpse.Advance(.1f);
		Check(corpse.Steps() == steps, "sleeping worlds skip solver updates");
		corpse.Sample(pose);
		const float hit[3] = {pose[1].matrix[0][3], pose[1].matrix[1][3], pose[1].matrix[2][3]};
		corpse.Impulse(1, forward, hit, 2);
		Check(corpse.Awake(), "an impact wakes a sleeping corpse");
		corpse.Advance(.1f);
		Check(corpse.Steps() > steps, "physics resumes after an impact");
		for (int i = 0; i < 1200 && corpse.Awake(); ++i) corpse.Advance(1.0f/120);
		Check(!corpse.Awake(), "corpse settles after an impact");
		JoltReaction::Transform platform = {};
		for (int r = 0; r < 3; ++r) platform.matrix[r][r] = 1;
		platform.matrix[2][3] = .1f;
		corpse.MoveMesh(1, platform, .1f); corpse.Advance(.1f);
		Check(corpse.Awake(), "moving platform wakes a sleeping corpse");
		corpse.MoveMesh(1, platform, .1f);
		for (int i = 0; i < 1200 && corpse.Awake(); ++i) corpse.Advance(1.0f/120);
		Check(!corpse.Awake(), "corpse settles after the platform stops");
		corpse.Sample(pose);
		const float height = pose[0].matrix[2][3];
		corpse.SetMeshEnabled(1, false);
		Check(corpse.Awake(), "removing support wakes a sleeping corpse");
		corpse.Advance(.2f); corpse.Sample(pose);
		Check(pose[0].matrix[2][3] < height-.1f, "corpse falls after support is removed");
	}
}
