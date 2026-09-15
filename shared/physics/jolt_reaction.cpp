// SPDX-License-Identifier: GPL-2.0-or-later
#include "jolt_reaction.h"
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include <algorithm>
#include <cmath>

namespace JoltReaction {
namespace {
constexpr float Step = 1.0f / 120;
// Only one actor is supported. No contacts until the world collision bridge exists.
struct Layers final : JPH::BroadPhaseLayerInterface, JPH::ObjectVsBroadPhaseLayerFilter, JPH::ObjectLayerPairFilter {
	JPH::uint GetNumBroadPhaseLayers() const override { return 1; }
	JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer) const override { return JPH::BroadPhaseLayer(0); }
	bool ShouldCollide(JPH::ObjectLayer, JPH::BroadPhaseLayer) const override { return false; }
	bool ShouldCollide(JPH::ObjectLayer, JPH::ObjectLayer) const override { return false; }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
	const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer) const override { return "reaction"; }
#endif
};

struct Runtime {
	Runtime() {
		JPH::RegisterDefaultAllocator();
		JPH::Factory::sInstance = new JPH::Factory;
		JPH::RegisterTypes();
	}
	~Runtime() {
		JPH::UnregisterTypes();
		delete JPH::Factory::sInstance;
		JPH::Factory::sInstance = nullptr;
	}
};
std::shared_ptr<Runtime> AcquireRuntime() {
	static std::weak_ptr<Runtime> runtime;
	auto result = runtime.lock();
	if (!result) { result = std::make_shared<Runtime>(); runtime = result; }
	return result;
}
}

struct Simulation::Impl {
	std::shared_ptr<Runtime> runtime = AcquireRuntime();
	Layers layers;
	JPH::TempAllocatorImpl allocator{1024 * 1024};
	JPH::JobSystemSingleThreaded jobs{128};
	JPH::PhysicsSystem world;
	JPH::BodyID bodies[3];
	JPH::RVec3 positions[3];
	JPH::Quat previous[2], current[2];
	float accumulator = 0;
	unsigned steps = 0;

	explicit Impl(const Rig& rig) {
		world.Init(8, 0, 8, 8, layers, layers, layers);
		world.SetGravity(JPH::Vec3::sZero());
		auto& interface = world.GetBodyInterface();
		const float lengths[] = {0.12f, rig.torsoLength, rig.headLength};
		const float radii[] = {0.06f, rig.torsoRadius, rig.headRadius};
		const float masses[] = {1, 28, 5};
		positions[0] = JPH::RVec3(0, 0, -0.06f);
		positions[1] = JPH::RVec3(0, 0, lengths[1] * 0.5f);
		positions[2] = JPH::RVec3(0, 0, lengths[1] + lengths[2] * 0.5f);
		const auto rotation = JPH::Quat::sRotation(JPH::Vec3::sAxisX(), JPH::JPH_PI * 0.5f);
		JPH::Body* body[3];
		for (int i = 0; i < 3; ++i) {
			auto shape = new JPH::CapsuleShape(std::max(0.01f, lengths[i] * 0.5f - radii[i]), radii[i]);
			JPH::BodyCreationSettings settings(shape, positions[i], rotation,
				i ? JPH::EMotionType::Dynamic : JPH::EMotionType::Kinematic, 0);
			settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
			settings.mMassPropertiesOverride.mMass = masses[i];
			settings.mAngularDamping = 1.5f;
			settings.mMaxAngularVelocity = 6;
			settings.mAllowSleeping = false;
			body[i] = interface.CreateBody(settings);
			bodies[i] = body[i]->GetID();
			interface.AddBody(bodies[i], JPH::EActivation::Activate);
		}
		for (int i = 0; i < 2; ++i) {
			JPH::SwingTwistConstraintSettings settings;
			settings.mPosition1 = settings.mPosition2 = JPH::RVec3(0, 0, i ? lengths[1] : 0);
			settings.mTwistAxis1 = settings.mTwistAxis2 = JPH::Vec3::sAxisZ();
			settings.mPlaneAxis1 = settings.mPlaneAxis2 = JPH::Vec3::sAxisX();
			settings.mNormalHalfConeAngle = settings.mPlaneHalfConeAngle = JPH::DegreesToRadians(i ? 12.0f : 16.0f);
			settings.mTwistMinAngle = -JPH::DegreesToRadians(8.0f);
			settings.mTwistMaxAngle = JPH::DegreesToRadians(8.0f);
			settings.mSwingMotorSettings = settings.mTwistMotorSettings = JPH::MotorSettings(3.5f, 1.0f);
			settings.mSwingMotorSettings.SetTorqueLimit(i ? 25 : 120);
			settings.mTwistMotorSettings.SetTorqueLimit(i ? 25 : 120);
			auto* constraint = static_cast<JPH::SwingTwistConstraint*>(settings.Create(*body[i], *body[i + 1]));
			constraint->SetSwingMotorState(JPH::EMotorState::Position);
			constraint->SetTwistMotorState(JPH::EMotorState::Position);
			constraint->SetTargetOrientationBS(JPH::Quat::sIdentity());
			world.AddConstraint(constraint);
			previous[i] = current[i] = JPH::Quat::sIdentity();
		}
	}
	~Impl() {
		auto constraints = world.GetConstraints();
		for (auto& constraint : constraints) world.RemoveConstraint(constraint);
		auto& interface = world.GetBodyInterface();
		for (auto id : bodies) { interface.RemoveBody(id); interface.DestroyBody(id); }
	}
};

Simulation::Simulation(const Rig& rig) : impl(new Impl(rig)) {}
Simulation::~Simulation() = default;
void Simulation::Reset() {
	auto& s = *impl;
	auto& interface = s.world.GetBodyInterface();
	const auto rotation = JPH::Quat::sRotation(JPH::Vec3::sAxisX(), JPH::JPH_PI * 0.5f);
	for (int i = 0; i < 3; ++i)
		interface.SetPositionRotationAndVelocity(s.bodies[i], s.positions[i], rotation, JPH::Vec3::sZero(), JPH::Vec3::sZero());
	for (int i = 0; i < 2; ++i) s.previous[i] = s.current[i] = JPH::Quat::sIdentity();
	for (auto& constraint : s.world.GetConstraints()) constraint->ResetWarmStart();
	s.accumulator = 0;
}
void Simulation::Impulse(int part, const float direction[3], const float point[3], float strength) {
	if (part < 0 || part > 1 || !std::isfinite(strength) || strength <= 0) return;
	for (int i = 0; i < 3; ++i) if (!std::isfinite(direction[i]) || !std::isfinite(point[i])) return;
	JPH::Vec3 impulse(direction[0], direction[1], direction[2]);
	if (impulse.LengthSq() < 0.0001f) return;
	impl->world.GetBodyInterface().AddImpulse(impl->bodies[part + 1],
		impulse.Normalized() * std::min(strength, 8.0f), JPH::RVec3(point[0], point[1], point[2]));
}
bool Simulation::Advance(float seconds) {
	if (!std::isfinite(seconds) || seconds < 0 || seconds > 0.25f) { Reset(); return false; }
	auto& s = *impl;
	s.accumulator += seconds;
	while (s.accumulator + 0.000001f >= Step) {
		for (int i = 0; i < 2; ++i) s.previous[i] = s.current[i];
		if (s.world.Update(Step, 1, &s.allocator, &s.jobs) != JPH::EPhysicsUpdateError::None) { Reset(); return false; }
		auto& interface = s.world.GetBodyInterface();
		const auto basis = interface.GetRotation(s.bodies[0]);
		for (int i = 0; i < 2; ++i) {
			const auto parent = interface.GetRotation(s.bodies[i]);
			const auto child = interface.GetRotation(s.bodies[i + 1]);
			s.current[i] = basis * parent.Conjugated() * child * basis.Conjugated();
			if (s.current[i].IsNaN()) { Reset(); return false; }
		}
		s.accumulator = std::max(0.0f, s.accumulator - Step);
		++s.steps;
	}
	return true;
}
Pose Simulation::Sample(float secondsAhead) const {
	Pose pose;
	const float alpha = std::clamp((impl->accumulator + secondsAhead) / Step, 0.0f, 1.0f);
	for (int i = 0; i < 2; ++i) {
		const auto angles = impl->previous[i].SLERP(impl->current[i], alpha).GetEulerAngles();
		pose.angles[i][0] = JPH::RadiansToDegrees(angles.GetY());
		pose.angles[i][1] = JPH::RadiansToDegrees(angles.GetZ());
		pose.angles[i][2] = JPH::RadiansToDegrees(angles.GetX());
	}
	return pose;
}
unsigned Simulation::Steps() const { return impl->steps; }
}
