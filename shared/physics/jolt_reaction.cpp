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
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/TransformedShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <map>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include <algorithm>
#include <cmath>
#include <array>
#include <deque>

namespace JoltReaction {
namespace {
constexpr float Step = 1.0f / 120;
constexpr float AnkleHeight = .095f, SoleHalfHeight = .035f;
float Ease(float t) { t = std::clamp(t, 0.0f, 1.0f); return t*t*t*(t*(t*6-15)+10); }
template<size_t N> struct PoseHistory {
	struct Frame {
		double time;
		std::array<JPH::Vec3, N> position;
		std::array<JPH::Quat, N> rotation;
	};
	std::deque<Frame> frames;
	void Push(double time, const JPH::Vec3* positions, const JPH::Quat* rotations) {
		if (!frames.empty() && frames.back().time == time) frames.pop_back();
		Frame frame;
		frame.time = time;
		for (size_t i = 0; i < N; ++i) { frame.position[i] = positions ? positions[i] : JPH::Vec3::sZero(); frame.rotation[i] = rotations[i]; }
		frames.push_back(frame);
		while (frames.size() > 128) frames.pop_front();
	}
	void Sample(double time, JPH::Vec3* positions, JPH::Quat* rotations) const {
		auto next = std::lower_bound(frames.begin(), frames.end(), time, [](const Frame& f, double t) { return f.time < t; });
		if (next == frames.end()) next = std::prev(frames.end());
		auto previous = next == frames.begin() ? next : std::prev(next);
		const float alpha = next == previous ? 0 : std::clamp(float((time - previous->time) / (next->time - previous->time)), 0.0f, 1.0f);
		for (size_t i = 0; i < N; ++i) {
			if (positions) positions[i] = previous->position[i] * (1 - alpha) + next->position[i] * alpha;
			rotations[i] = previous->rotation[i].SLERP(next->rotation[i], alpha);
		}
	}
};
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
	// Worlds advance sequentially on the game thread; share scratch memory and jobs.
	std::unique_ptr<JPH::TempAllocatorImpl> allocator;
	std::unique_ptr<JPH::JobSystemSingleThreaded> jobs;
	Runtime() {
		JPH::RegisterDefaultAllocator();
		JPH::Factory::sInstance = new JPH::Factory;
		JPH::RegisterTypes();
		allocator.reset(new JPH::TempAllocatorImpl(16 * 1024 * 1024));
		jobs.reset(new JPH::JobSystemSingleThreaded(1024));
	}
	~Runtime() {
		jobs.reset(); allocator.reset();
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
	JPH::PhysicsSystem world;
	JPH::BodyID bodies[3];
	JPH::RVec3 positions[3];
	JPH::Quat current[2];
	PoseHistory<2> history;
	double clock = 0;
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
			settings.mSwingMotorSettings = settings.mTwistMotorSettings = JPH::MotorSettings(1.8f, 0.8f);
			settings.mSwingMotorSettings.SetTorqueLimit(i ? 25 : 120);
			settings.mTwistMotorSettings.SetTorqueLimit(i ? 25 : 120);
			auto* constraint = static_cast<JPH::SwingTwistConstraint*>(settings.Create(*body[i], *body[i + 1]));
			constraint->SetSwingMotorState(JPH::EMotorState::Position);
			constraint->SetTwistMotorState(JPH::EMotorState::Position);
			constraint->SetTargetOrientationBS(JPH::Quat::sIdentity());
			world.AddConstraint(constraint);
			current[i] = JPH::Quat::sIdentity();
		}
		history.Push(0, nullptr, current);
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
	for (auto& rotation : s.current) rotation = JPH::Quat::sIdentity();
	for (auto& constraint : s.world.GetConstraints()) constraint->ResetWarmStart();
	s.accumulator = 0;
	s.clock = 0; s.history.frames.clear(); s.history.Push(0, nullptr, s.current);
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
		if (s.world.Update(Step, 1, s.runtime->allocator.get(), s.runtime->jobs.get()) != JPH::EPhysicsUpdateError::None) { Reset(); return false; }
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
		s.clock += 1.0 / 120;
		s.history.Push(s.clock, nullptr, s.current);
	}
	return true;
}
Pose Simulation::Sample(float secondsAhead) const {
	Pose pose;
	JPH::Quat rotations[2];
	impl->history.Sample(impl->clock + impl->accumulator + secondsAhead, nullptr, rotations);
	for (int i = 0; i < 2; ++i) {
		const auto angles = rotations[i].GetEulerAngles();
		pose.angles[i][0] = JPH::RadiansToDegrees(angles.GetY());
		pose.angles[i][1] = JPH::RadiansToDegrees(angles.GetZ());
		pose.angles[i][2] = JPH::RadiansToDegrees(angles.GetX());
	}
	return pose;
}
unsigned Simulation::Steps() const { return impl->steps; }

namespace {
struct FallLayers final : JPH::BroadPhaseLayerInterface, JPH::ObjectVsBroadPhaseLayerFilter, JPH::ObjectLayerPairFilter {
	JPH::uint GetNumBroadPhaseLayers() const override { return 1; }
	JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer) const override { return JPH::BroadPhaseLayer(0); }
	bool ShouldCollide(JPH::ObjectLayer, JPH::BroadPhaseLayer) const override { return true; }
	bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override { return (a == 1 && b == 0) || (a == 0 && b == 1); }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
	const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer) const override { return "fall"; }
#endif
};
JPH::Vec3 Vector(const float* v) { return JPH::Vec3(v[0], v[1], v[2]); }
JPH::Mat44 Matrix(const Transform& t) {
	return JPH::Mat44(JPH::Vec4(t.matrix[0][0], t.matrix[1][0], t.matrix[2][0], 0),
		JPH::Vec4(t.matrix[0][1], t.matrix[1][1], t.matrix[2][1], 0),
		JPH::Vec4(t.matrix[0][2], t.matrix[1][2], t.matrix[2][2], 0),
		JPH::Vec4(t.matrix[0][3], t.matrix[1][3], t.matrix[2][3], 1));
}
void Store(JPH::Mat44Arg m, Transform& t) {
	for (int r = 0; r < 3; ++r) for (int c = 0; c < 4; ++c) t.matrix[r][c] = m(r, c);
}
}
struct FallSimulation::Impl {
	struct FootContacts final : JPH::ContactListener {
		Impl* owner;
		explicit FootContacts(Impl* value) : owner(value) {}
		void Record(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& manifold) {
			if ((a.GetID() == owner->bodies[2] || b.GetID() == owner->bodies[2]) && !owner->balance.firstHeadContact)
				owner->balance.firstHeadContact = owner->steps+1;
			for (int i = 0; i < 2; ++i)
				if ((a.GetID() == owner->bodies[i] && manifold.mWorldSpaceNormal.GetZ() < -.5f) ||
					(b.GetID() == owner->bodies[i] && manifold.mWorldSpaceNormal.GetZ() > .5f)) owner->trunkContact = true;
			for (int side = 0; side < 2; ++side) {
				const bool first = a.GetID() == owner->bodies[11+side], second = b.GetID() == owner->bodies[11+side];
				if ((first && manifold.mWorldSpaceNormal.GetZ() < -.5f) || (second && manifold.mWorldSpaceNormal.GetZ() > .5f)) {
					owner->supported[side] = true;
					owner->supportHeight[side] = (first ? manifold.GetWorldSpaceContactPointOn2(0) : manifold.GetWorldSpaceContactPointOn1(0)).GetZ();
				}
				const int hand = 4+side*2;
				const bool handFirst = a.GetID() == owner->bodies[hand];
				if (handFirst || b.GetID() == owner->bodies[hand]) {
					const auto point = (handFirst ? a : b).GetWorldTransform()*owner->endLocal[hand];
					for (JPH::uint i = 0; i < manifold.mRelativeContactPointsOn1.size(); ++i)
						if (((handFirst ? manifold.GetWorldSpaceContactPointOn1(i) : manifold.GetWorldSpaceContactPointOn2(i))-point).LengthSq() < .01f)
						{
							owner->handContacts |= 1u << side;
							if (!owner->balance.firstHandContact) owner->balance.firstHandContact = owner->steps+1;
						}
				}
			}
		}
		void OnContactAdded(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& m, JPH::ContactSettings&) override { Record(a,b,m); }
		void OnContactPersisted(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& m, JPH::ContactSettings&) override { Record(a,b,m); }
	};
	std::shared_ptr<Runtime> runtime = AcquireRuntime();
	FallLayers layers;
	JPH::PhysicsSystem world;
	FootContacts listener{this};
	bool supported[2] = {};
	bool trunkContact = false;
	float trunkGrace = 0;
	float supportHeight[2] = {};
	float contactGrace[2] = {};
	unsigned handContacts = 0;
	JPH::Vec3 handGoal[2] = {JPH::Vec3::sZero(), JPH::Vec3::sZero()};
	JPH::Vec3 previousShoulder[2] = {JPH::Vec3::sZero(), JPH::Vec3::sZero()};
	JPH::Mat44 preparationFrom[PartCount], preparationTarget[PartCount];
	float preparationAge = 0, preparationDuration = 1;
	JPH::RefConst<JPH::Shape> forearmShape[2], palmShape[2];
	bool palmsEnabled = false;
	JPH::BodyID bodies[PartCount];
	JPH::Body* body[PartCount];
	JPH::Mat44 offsets[PartCount];
	JPH::Mat44 targetFrom[PartCount], target[PartCount];
	JPH::Quat stepRotation[PartCount];
	JPH::Vec3 endLocal[PartCount];
	JPH::Ref<JPH::SwingTwistConstraint> joints[PartCount];
	int parent[PartCount];
	float mass[PartCount], weakness[PartCount] = {}, weaknessHold[PartCount] = {};
	float totalMass = 0, targetAge = 0, targetDuration = Step, reactionAge = 10, reactionAngle = 0;
	float stepAge = 0, landedAge = 10, fallAge = 0, unsupported = 0, standingHeight = .8f;
	float vitality = 1, recentStress = 0, deathAge = 0, deathStrength = 0;
	float deathTorque[PartCount] = {};
	JPH::Mat44 deathPose[PartCount];
	RegionalControl regional, requestedRegional;
	JPH::Mat44 gripPose[PartCount];
	JPH::Vec3 gripTarget = JPH::Vec3::sZero(), gripGoal = JPH::Vec3::sZero();
	bool gripLift = false;
	float gripAge = 0, shockRemaining = 0, shockTarget = 0, shockAge = 0;
	float nextGripStruggle = .45f, shockPushLimit = 0;
	JPH::Vec3 shockPushDirection = JPH::Vec3::sZero();
	int swing = 0;
	JPH::Vec3 desiredVelocity = JPH::Vec3::sZero(), reactionAxis = JPH::Vec3::sAxisY();
	JPH::Vec3 stepStart = JPH::Vec3::sZero(), stepGoal = JPH::Vec3::sZero(), planted = JPH::Vec3::sZero();
	JPH::Vec3 injuredFoot[2] = {JPH::Vec3::sZero(), JPH::Vec3::sZero()};
	JPH::Quat stanceFrom[PartCount], stanceJoint[PartCount];
	bool plantedPose = false;
	BalanceStatus balance;
	JPH::Vec3 position[PartCount];
	JPH::Quat rotation[PartCount];
	PoseHistory<PartCount> history;
	double clock = 0;
	std::map<int, JPH::BodyID> meshes;
	float accumulator = 0;
	unsigned steps = 0;
	void PalmCollision(bool enabled) {
		if (palmsEnabled == enabled) return;
		for (int side = 0; side < 2; ++side)
			world.GetBodyInterface().SetShape(bodies[4+side*2], enabled ? palmShape[side] : forearmShape[side], false, JPH::EActivation::Activate);
		palmsEnabled = enabled;
	}
	void MakeJoint(int i, JPH::Vec3Arg start, JPH::Vec3Arg direction) {
		JPH::SwingTwistConstraintSettings joint;
		joint.mPosition1 = joint.mPosition2 = start;
		joint.mTwistAxis1 = joint.mTwistAxis2 = direction;
		joint.mPlaneAxis1 = joint.mPlaneAxis2 = direction.GetNormalizedPerpendicular();
		joint.mNormalHalfConeAngle = joint.mPlaneHalfConeAngle = JPH::DegreesToRadians(i < 3 ? 35.0f : 65.0f);
		joint.mTwistMinAngle = -JPH::DegreesToRadians(25.0f);
		joint.mTwistMaxAngle = JPH::DegreesToRadians(25.0f);
		joint.mMaxFrictionTorque = 1.0f;
		joint.mSwingMotorSettings.mSpringSettings = joint.mTwistMotorSettings.mSpringSettings =
			JPH::SpringSettings(JPH::ESpringMode::StiffnessAndDamping, i >= 7 ? 8000.0f : i == 1 ? 700.0f : 150.0f, i >= 7 ? 100.0f : 15.0f);
		joints[i] = static_cast<JPH::SwingTwistConstraint*>(joint.Create(*body[parent[i]], *body[i]));
		world.AddConstraint(joints[i]);
	}
	bool Ground(JPH::Vec3Arg point, JPH::Vec3& ground) const {
		JPH::RRayCast ray(point + JPH::Vec3(0,0,.35f), JPH::Vec3(0,0,-1.25f));
		JPH::RayCastResult hit;
		if (!world.GetNarrowPhaseQuery().CastRay(ray, hit, {}, JPH::SpecifiedObjectLayerFilter(0))) return false;
		ground = ray.GetPointOnRay(hit.mFraction);
		return true;
	}
	JPH::Vec3 Foot(int side) const {
		const int index = side ? 10 : 8;
		return world.GetBodyInterface().GetWorldTransform(bodies[index]) * endLocal[index];
	}
	float MuscleStrength(int part) const {
		const float strength = 1-.98f*weakness[part];
		const auto region = part >= 11 ? Region::Feet : part >= 7 ? Region::Legs : part >= 3 ? Region::Arms : part == 2 ? Region::Head : Region::Torso;
		return (part >= 7 && part < 11 ? strength*strength : strength)*regional.strength[int(region)];
	}
	void LimbTargets(JPH::Mat44* goals, int upper, JPH::Vec3Arg foot, const JPH::Vec3* balancedHip = nullptr) {
		const int lower = upper + 1;
		const auto referenceHip = (goals[upper] * offsets[upper]).GetTranslation();
		const auto hip = balancedHip ? *balancedHip : (world.GetBodyInterface().GetWorldTransform(bodies[upper]) * offsets[upper]).GetTranslation();
		const auto knee = (goals[lower] * offsets[lower]).GetTranslation() + hip-referenceHip;
		const auto oldFoot = goals[lower] * endLocal[lower];
		const float a = std::max(.05f, (knee-hip).Length()), b = std::max(.05f, (oldFoot+hip-referenceHip-knee).Length());
		const auto direction = (foot-hip).NormalizedOr(-JPH::Vec3::sAxisZ());
		const float d = std::clamp((foot-hip).Length(), std::abs(a-b)+.005f, a+b-.005f);
		const float along = (a*a-b*b+d*d)/(2*d);
		const auto oldDirection = (oldFoot-referenceHip).NormalizedOr(-JPH::Vec3::sAxisZ());
		auto bend = knee-hip-oldDirection*(knee-hip).Dot(oldDirection);
		bend -= direction*bend.Dot(direction);
		bend = bend.NormalizedOr(direction.GetNormalizedPerpendicular());
		const auto joint = hip + direction*along + bend*std::sqrt(std::max(0.0f,a*a-along*along));
		const auto reachable = hip + direction*d;
		const auto upperRotation = JPH::Quat::sFromTo(goals[upper].GetAxisY(), (joint-hip).Normalized())*goals[upper].GetQuaternion();
		const auto lowerRotation = JPH::Quat::sFromTo(goals[lower].GetAxisY(), (reachable-joint).Normalized())*goals[lower].GetQuaternion();
		goals[upper] = JPH::Mat44::sRotationTranslation(upperRotation, (joint+hip)*.5f);
		goals[lower] = JPH::Mat44::sRotationTranslation(lowerRotation, (joint+reachable)*.5f);
	}
	void LegTargets(JPH::Mat44* goals, int side, JPH::Vec3Arg foot, const JPH::Vec3* balancedHip = nullptr) {
		LimbTargets(goals, side ? 9 : 7, foot, balancedHip);
		const int shoe = side ? 12 : 11;
		goals[shoe].SetTranslation(foot - goals[shoe].Multiply3x3(offsets[shoe].GetTranslation()));
	}
	void FootControl(int side, JPH::Vec3Arg desired) {
		auto& api = world.GetBodyInterface();
		const auto foot = Foot(side);
		auto force = (desired-foot)*2200 - api.GetPointVelocity(bodies[11+side], foot)*80;
		if (force.Length() > 300) force = force.Normalized()*300;
		for (int i = side ? 9 : 7; i < (side ? 11 : 9); ++i) {
			const auto joint = (api.GetWorldTransform(bodies[i])*offsets[i]).GetTranslation();
			const auto torque = (foot-joint).Cross(force) * balance.strength * MuscleStrength(i);
			api.AddTorque(bodies[i], torque); api.AddTorque(bodies[parent[i]], -torque);
		}
	}
	void GripForce() {
		balance.gripForce = 0;
		if (!balance.gripping) return;
		gripAge += Step;
		if (!gripLift) return;
		auto& api = world.GetBodyInterface();
		auto movement = gripTarget-gripGoal;
		if (movement.Length() > Step*6) movement *= Step*6/movement.Length();
		gripGoal += movement;
		const auto anchor = api.GetWorldTransform(bodies[1])*endLocal[1];
		auto force = (gripGoal-anchor)*(totalMass*60) - api.GetPointVelocity(bodies[1], anchor)*(totalMass*12) - world.GetGravity()*totalMass;
		const float limit = totalMass*80;
		if (force.Length() > limit) force *= limit/force.Length();
		force *= std::min(1.0f, gripAge/.2f);
		api.AddForce(bodies[1], force, anchor);
		balance.gripForce = force.Length();
	}
	void Control() {
		if (balance.phase == ControlPhase::Shadow) return;
		for (int i = 0; i < int(Region::Count); ++i)
			regional.strength[i] += std::clamp(requestedRegional.strength[i]-regional.strength[i], -Step*5, Step*5);
		shockRemaining = std::max(0.0f, shockRemaining-Step);
		balance.shock += std::clamp((shockRemaining > 0 ? shockTarget : 0)-balance.shock, -Step*4, Step*8);
		shockAge += Step;
		if (shockRemaining > 0 && balance.phase != ControlPhase::Dead) {
			const float push = std::min(std::max(0.0f, shockPushLimit-balance.shockPushUsed), shockPushLimit*Step/.25f);
			if (push > 0) for (auto id : bodies) world.GetBodyInterface().AddLinearVelocity(id, shockPushDirection*push);
			balance.shockPushUsed += push;
		}
		if (balance.phase != ControlPhase::Dead || std::any_of(body, body+PartCount, [](const JPH::Body* b) { return b->IsActive(); })) {
			trunkGrace = trunkContact || (handContacts && (supported[0] || supported[1])) ? .12f : std::max(0.0f, trunkGrace-Step);
			balance.supportedTrunk = trunkGrace > 0;
		}
		GripForce();
		if (balance.phase == ControlPhase::Dead) { DeathControl(); return; }
		auto& api = world.GetBodyInterface();
		targetAge += Step; reactionAge += Step;
		landedAge += Step;
		balance.assistForce = balance.assistTorque = 0;
		balance.handContacts = handContacts;
		balance.handContactsSeen |= handContacts;
		const unsigned previousBrace = balance.braceMask;
		balance.braceMask = 0;
		JPH::Mat44 goals[PartCount];
		const float alpha = std::clamp(targetAge / targetDuration, 0.0f, 1.0f);
		JPH::Vec3 center = JPH::Vec3::sZero(), velocity = JPH::Vec3::sZero();
		for (int i = 0; i < PartCount; ++i) {
			goals[i] = JPH::Mat44::sRotationTranslation(targetFrom[i].GetQuaternion().SLERP(target[i].GetQuaternion(), alpha),
				targetFrom[i].GetTranslation()*(1-alpha)+target[i].GetTranslation()*alpha);
			center += api.GetCenterOfMassPosition(bodies[i]) * mass[i];
			velocity += api.GetLinearVelocity(bodies[i]) * mass[i];
			if (weaknessHold[i] > 0) weaknessHold[i] -= Step;
			else weakness[i] = std::max(0.0f, weakness[i] - Step * .9f);
		}
		center /= totalMass; velocity /= totalMass;
		if (balance.gripping) {
			for (int i = 1; i <= 6; ++i) {
				const auto relative = gripPose[parent[i]].GetQuaternion().Conjugated()*gripPose[i].GetQuaternion();
				const auto desired = goals[parent[i]].GetQuaternion()*relative;
				goals[i] = JPH::Mat44::sRotationTranslation(goals[i].GetQuaternion().SLERP(desired, std::min(1.0f, gripAge/.3f)), goals[i].GetTranslation());
			}
			if (gripLift) {
				const auto forward = (goals[11].GetAxisY()*JPH::Vec3(1,1,0)).NormalizedOr(JPH::Vec3::sAxisX());
				const auto axis = JPH::Vec3::sAxisZ().Cross(forward);
				for (int side = 0; side < 2; ++side) {
					const int upper = side ? 9 : 7;
					const auto hip = (api.GetWorldTransform(bodies[upper])*offsets[upper]).GetTranslation();
					const float length = endLocal[upper].Length()*2 + endLocal[upper+1].Length()*2;
					LegTargets(goals, side, hip-JPH::Vec3(0,0,length*.97f), &hip);
				}
				if (gripAge >= nextGripStruggle) {
					const int upper = balance.gripStruggles % 2 ? 9 : 7;
					const auto impulse = axis*(.45f*(mass[upper]/8)*(.8f+.2f*std::sin(balance.gripStruggles*2.1f)));
					api.AddAngularImpulse(bodies[upper], impulse);
					api.AddAngularImpulse(bodies[parent[upper]], -impulse);
					++balance.gripStruggles;
					nextGripStruggle = gripAge+.75f+.15f*std::sin(balance.gripStruggles*1.7f);
				}
			}
		}
		const bool preparing = balance.phase == ControlPhase::Preparing;
		if (preparing) {
			preparationAge += Step;
			std::copy(preparationFrom, preparationFrom+PartCount, goals);
			balance.preparationError = 0;
		}
		if (desiredVelocity.LengthSq() > .16f) plantedPose = false;
		const bool withdraw[] = {balance.phase == ControlPhase::Tracking && weakness[7] > .6f,
			balance.phase == ControlPhase::Tracking && weakness[9] > .6f};
		for (int side = 0; side < 2; ++side) {
			const int upper = side ? 9 : 7;
			if (weakness[upper] > 0) balance.peakLegLift = std::max(balance.peakLegLift, Foot(side).GetZ()-injuredFoot[side].GetZ());
			if (withdraw[side]) {
				const float lift = (weakness[upper]-.6f)*.35f*std::clamp((.3f-weaknessHold[upper])/.1f, 0.0f, 1.0f);
				LegTargets(goals, side, injuredFoot[side]+JPH::Vec3(0,0,lift));
			}
		}
		const auto left = Foot(0), right = Foot(1);
		for (int side = 0; side < 2; ++side) contactGrace[side] = supported[side] ? .06f : std::max(0.0f, contactGrace[side]-Step);
		const bool contacts[] = {contactGrace[0] > 0, contactGrace[1] > 0};
		balance.contacts = unsigned(supported[0]) | (unsigned(supported[1]) << 1);
		const float floor = contacts[0] ? supportHeight[0] : contacts[1] ? supportHeight[1] : center.GetZ()-standingHeight;
		balance.pelvisHeight = (api.GetWorldTransform(bodies[0])*offsets[0]).GetTranslation().GetZ() - floor;
		const auto span = (right-left) * JPH::Vec3(1,1,0);
		auto capture = center + (velocity-desiredVelocity)*.20f;
		capture.SetZ(0);
		auto support = left * JPH::Vec3(1,1,0);
		const float t = span.LengthSq() > .001f ? std::clamp((capture-support).Dot(span)/span.LengthSq(), 0.0f, 1.0f) : .5f;
		support += span*t;
		const bool canSupport[] = {contacts[0] && std::max(weakness[7], weakness[8]) < .8f,
			contacts[1] && std::max(weakness[9], weakness[10]) < .8f};
		if (canSupport[0] != canSupport[1]) support = (canSupport[0] ? left : right) * JPH::Vec3(1,1,0);
		const auto error = capture-support;
		balance.error = error.Length();
		balance.peakError = std::max(balance.peakError, balance.error);
		unsupported = contacts[0] || contacts[1] ? 0 : unsupported+Step;
		if (balance.phase == ControlPhase::Tracking || balance.phase == ControlPhase::Stepping) {
			if (unsupported > .4f || balance.pelvisHeight < standingHeight * .48f) balance.phase = ControlPhase::Falling;
			if (balance.phase == ControlPhase::Tracking && landedAge > .6f && balance.error > .14f) {
				const auto forward = (goals[11].GetAxisY()*JPH::Vec3(1,1,0)).NormalizedOr(JPH::Vec3::sAxisX());
				const auto lateral = JPH::Vec3::sAxisZ().Cross(forward);
				const bool foreAft = std::abs(error.Dot(forward)) > std::abs(error.Dot(lateral));
				swing = !contacts[0] && contacts[1] ? 0 : !contacts[1] && contacts[0] ? 1 :
					foreAft ? (error.Dot(left-right) > 0 ? 1 : 0) : (error.Dot(lateral) > 0 ? 0 : 1);
				stepStart = swing ? right : left; planted = swing ? left : right;
				auto requested = capture + lateral*(swing ? -.12f : .12f) + error.NormalizedOr(JPH::Vec3::sZero())*.08f;
				requested.SetZ(stepStart.GetZ());
				const auto reach = requested-stepStart;
				if (reach.Length() > .5f) requested = stepStart + reach.Normalized()*.5f;
				if (Ground(requested, stepGoal) && stepGoal.GetZ()-stepStart.GetZ() < .25f) {
					stepGoal.SetZ(stepGoal.GetZ()+AnkleHeight); stepAge = 0;
					for (int i = 7; i < PartCount; ++i) stepRotation[i] = api.GetRotation(bodies[i]);
					balance.phase = ControlPhase::Stepping; ++balance.corrections;
				} else ++balance.rejectedSteps;
			}
			if (balance.phase == ControlPhase::Stepping) {
				stepAge += Step;
				if (stepAge < .25f) {
					const auto forward = (goals[11].GetAxisY()*JPH::Vec3(1,1,0)).NormalizedOr(JPH::Vec3::sAxisX());
					const auto requested = capture + JPH::Vec3::sAxisZ().Cross(forward)*(swing ? -.12f : .12f);
					auto change = (requested-stepGoal)*JPH::Vec3(1,1,0);
					if (change.Length() > Step*.6f) change *= Step*.6f/change.Length();
					JPH::Vec3 ground;
					if (Ground(stepGoal+change, ground) && std::abs(ground.GetZ()+AnkleHeight-stepGoal.GetZ()) < .10f)
						stepGoal = ground + JPH::Vec3(0,0,AnkleHeight);
				}
				const float u = std::clamp((stepAge-.08f) / .24f, 0.0f, 1.0f), smooth = u*u*(3-2*u);
				const auto foot = stepStart*(1-smooth)+stepGoal*smooth+JPH::Vec3(0,0,std::sin(JPH::JPH_PI*u)*.10f);
				LegTargets(goals, swing, foot); LegTargets(goals, 1-swing, planted);
				const float blend = std::min(1.0f, stepAge/.15f);
				for (int i = 7; i < PartCount; ++i) goals[i] = JPH::Mat44::sRotationTranslation(stepRotation[i].SLERP(goals[i].GetQuaternion(), blend), goals[i].GetTranslation());
				FootControl(swing, foot); FootControl(1-swing, planted);
				balance.footError = (Foot(swing)-stepGoal).Length();
				if (stepAge > .4f && supported[swing] && balance.footError < .08f) {
					++balance.landings;
					landedAge = 0;
					// Fit one balanced stance, then blend to it. Do not chase the moving hip with IK.
					JPH::Mat44 standing[PartCount];
					std::copy(target, target+PartCount, standing);
					auto referenceCenter = JPH::Vec3::sZero();
					for (int i = 0; i < PartCount; ++i) referenceCenter += target[i].GetTranslation()*(mass[i]/totalMass);
					auto shift = (Foot(0)+Foot(1))*.5f-referenceCenter;
					shift.SetZ(api.GetPosition(bodies[0]).GetZ()-target[0].GetTranslation().GetZ()-.02f);
					for (int side = 0; side < 2; ++side) {
						const int hipIndex = side ? 9 : 7;
						const auto hip = (target[hipIndex]*offsets[hipIndex]).GetTranslation()+shift;
						LegTargets(standing, side, Foot(side), &hip);
					}
					for (int i = 7; i < PartCount; ++i) {
						stanceFrom[i] = api.GetRotation(bodies[parent[i]]).Conjugated()*api.GetRotation(bodies[i]);
						stanceJoint[i] = standing[parent[i]].GetQuaternion().Conjugated()*standing[i].GetQuaternion();
					}
					plantedPose = true;
					balance.phase = ControlPhase::Tracking;
				} else if (stepAge > .9f) balance.phase = ControlPhase::Falling;
			}
			if (balance.error > .65f) balance.phase = ControlPhase::Falling;
		}
		recentStress = std::max(0.0f, recentStress-Step*.08f);
		const float wantedStrength = balance.gripping ? 1 : balance.phase == ControlPhase::Falling || preparing ? .12f :
			1-.15f*(1-vitality)-recentStress-.2f*balance.shock;
		PalmCollision(!balance.gripping && (balance.phase == ControlPhase::Falling || preparing));
		balance.strength += std::clamp(wantedStrength-balance.strength, -Step*3, Step*2);
		const float pulse = reactionAge < .08f ? reactionAge/.08f : std::max(0.0f, 1-(reactionAge-.08f)/.55f);
		const auto recoil = JPH::Quat::sRotation(reactionAxis, reactionAngle * pulse);
		goals[1] = JPH::Mat44::sRotationTranslation(recoil*goals[1].GetQuaternion(), goals[1].GetTranslation());
		goals[2] = JPH::Mat44::sRotationTranslation(JPH::Quat::sRotation(reactionAxis, reactionAngle*pulse*.35f)*goals[2].GetQuaternion(), goals[2].GetTranslation());
		fallAge = !balance.gripping && balance.phase == ControlPhase::Falling ? fallAge+Step : 0;
		if (fallAge > 0) {
			const auto forward = (target[11].GetAxisY()*JPH::Vec3(1,1,0)).NormalizedOr(JPH::Vec3::sAxisX());
			const auto axis = JPH::Vec3::sAxisZ().Cross(forward);
			for (int i = 3; i <= 6; ++i) {
				const float lift = (i == 3 || i == 5 ? -.6f : -.3f)*std::min(1.0f, fallAge/.18f);
				goals[i] = JPH::Mat44::sRotationTranslation(JPH::Quat::sRotation(axis, lift)*goals[i].GetQuaternion(), goals[i].GetTranslation());
			}
		}
		if (!balance.gripping && (fallAge > 0 || preparing)) for (int side = 0; side < 2; ++side) {
			const int upper = 3+side*2, lower = upper+1;
			const auto shoulder = (api.GetWorldTransform(bodies[upper])*offsets[upper]).GetTranslation();
			const auto hand = api.GetWorldTransform(bodies[lower])*endLocal[lower];
			JPH::Vec3 goal;
			if (preparing) {
				const auto end = preparationTarget[lower]*endLocal[lower];
				goal = preparationFrom[lower]*endLocal[lower];
				goal += (end-goal)*Ease(preparationAge/preparationDuration);
				balance.preparationError = std::max(balance.preparationError, (end-hand).Length());
			} else {
				// Look ahead along the fall, including downward motion. Each arm has its own probe.
				auto drift = velocity*JPH::Vec3(1,1,0);
				drift /= std::max(1.0f, drift.Length());
				// Place the palms ahead of the head, spread to either side. A second
				// probe catches walls between the shoulder and the projected floor target.
				const auto head = api.GetWorldTransform(bodies[2])*endLocal[2];
				const auto lateral = ((api.GetPosition(bodies[3])-api.GetPosition(bodies[5]))*JPH::Vec3(1,1,0)).NormalizedOr(JPH::Vec3::sAxisY());
				auto probe = head+drift*.20f+lateral*(side ? -.18f : .18f);
				probe.SetZ(shoulder.GetZ()+.1f);
				JPH::RRayCast ray(probe, JPH::Vec3(0,0,-2));
				JPH::RayCastResult hit;
				if (!world.GetNarrowPhaseQuery().CastRay(ray, hit, {}, JPH::SpecifiedObjectLayerFilter(0))) continue;
				auto point = ray.GetPointOnRay(hit.mFraction);
				const auto reach = point-shoulder;
				JPH::RayCastResult obstacle;
				if (world.GetNarrowPhaseQuery().CastRay(JPH::RRayCast(shoulder, reach), obstacle, {}, JPH::SpecifiedObjectLayerFilter(0)) && obstacle.mFraction < .98f) {
					hit = obstacle; point = shoulder+reach*obstacle.mFraction;
				}
				auto normal = api.GetTransformedShape(hit.mBodyID).GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, point);
				if (normal.Dot(reach) > 0) normal = -normal;
				goal = point + normal*.065f;
				const float length = (endLocal[upper]-offsets[upper].GetTranslation()).Length() +
					(endLocal[lower]-offsets[lower].GetTranslation()).Length();
				if ((goal-shoulder).Length() > 2*length+.4f || fallAge > 1.4f) continue;
				const float gap = std::max(0.0f, (shoulder-point).Dot(normal)-length);
				const float closing = -api.GetPointVelocity(bodies[parent[upper]], shoulder).Dot(normal);
				const float gravity = std::max(0.0f, -world.GetGravity().Dot(normal));
				const float arrival = gap == 0 ? 0 : gravity > .001f ?
					(std::sqrt(closing*closing+2*gravity*gap)-closing)/gravity : closing > .001f ? gap/closing : 1;
				if (arrival > .4f) continue;
			}
			if (!(previousBrace & (1u << side))) handGoal[side] = hand;
			else if (!preparing && !(handContacts & (1u << side))) handGoal[side] += shoulder-previousShoulder[side];
			previousShoulder[side] = shoulder;
			auto change = goal-handGoal[side];
			const float limit = Step*(preparing ? .8f : 2.2f);
			if (change.Length() > limit) change *= limit/change.Length();
			handGoal[side] += change;
			goals[upper] = api.GetWorldTransform(bodies[upper]); goals[lower] = api.GetWorldTransform(bodies[lower]);
			LimbTargets(goals, upper, handGoal[side]);
			balance.braceMask |= 1u << side;
			const auto carry = !preparing && !(handContacts & (1u << side)) ? api.GetPointVelocity(bodies[parent[upper]], shoulder) : JPH::Vec3::sZero();
			auto force = (handGoal[side]-hand)*(preparing ? 450 : 700)-(api.GetPointVelocity(bodies[lower], hand)-carry)*35;
			const float forceLimit = preparing ? 100 : 180;
			if (force.Length() > forceLimit) force *= forceLimit/force.Length();
			for (int i = upper; i <= lower; ++i) {
				const auto joint = (api.GetWorldTransform(bodies[i])*offsets[i]).GetTranslation();
				const auto torque = (hand-joint).Cross(force)*MuscleStrength(i);
				api.AddTorque(bodies[i], torque); api.AddTorque(bodies[parent[i]], -torque);
			}
		}
		for (int i = 1; i < PartCount; ++i) {
			auto desired = goals[i].GetQuaternion();
			if (balance.shock > 0) {
				const float amplitude = balance.shock*(i == 2 ? .04f : i >= 7 ? .09f : .18f);
				const float pulse = std::sin(shockAge*31+i*.8f) + .35f*std::sin(shockAge*17+i);
				desired = JPH::Quat::sRotation(goals[0].GetAxisX(), amplitude*pulse)*desired;
			}
			const bool injured = (i == 7 || i == 8 || i == 11) ? withdraw[0] : (i == 9 || i == 10 || i == 12) && withdraw[1];
			const bool worldHip = (i == 7 || i == 9) && ((balance.gripping && gripLift) || balance.phase == ControlPhase::Stepping || (balance.phase == ControlPhase::Tracking && injured));
			const bool bracing = i >= 3 && i <= 6 && (balance.braceMask & (1u << ((i-3)/2)));
			const bool worldArm = bracing && (i == 3 || i == 5);
			const auto q = balance.phase == ControlPhase::Tracking && plantedPose && i >= 7 && !injured ? stanceFrom[i].SLERP(stanceJoint[i], std::min(1.0f, landedAge/.35f)) :
				(worldHip || worldArm ? api.GetRotation(bodies[parent[i]]) : goals[parent[i]].GetQuaternion()).Conjugated()*desired;
			joints[i]->SetTargetOrientationBS(q);
			const bool stepping = balance.phase == ControlPhase::Stepping || landedAge < .25f;
			const float strength = MuscleStrength(i);
			const float motorStrength = std::max(balance.strength, balance.shock*(i < 7 ? .55f : .18f));
			const float torque = bracing ? (preparing ? 25 : 65)*strength :
				(i >= 11 ? (stepping ? 150.0f : 400.0f) : i >= 7 ? (stepping ? 250.0f : 900.0f) : i == 2 ? 45.0f : i == 1 ? 250.0f : 80.0f) * motorStrength * strength;
			joints[i]->GetSwingMotorSettings().SetTorqueLimit(torque);
			joints[i]->GetTwistMotorSettings().SetTorqueLimit(torque);
			if (i >= 3 && i <= 6) {
				const bool catching = bracing && !preparing;
				joints[i]->GetSwingMotorSettings().mSpringSettings = joints[i]->GetTwistMotorSettings().mSpringSettings =
					JPH::SpringSettings(JPH::ESpringMode::StiffnessAndDamping, (catching ? 450.0f : balance.gripping ? 350.0f : 150.0f)*(1+.4f*balance.shock), catching ? 35.0f : 15.0f);
			}
			if (i >= 7 && i < 11) {
				joints[i]->GetSwingMotorSettings().mSpringSettings = joints[i]->GetTwistMotorSettings().mSpringSettings =
					JPH::SpringSettings(JPH::ESpringMode::StiffnessAndDamping, 8000*strength, 100*std::sqrt(strength));
			}
		}
		// Ground contact permits bounded horizontal force and root torque.
		if ((balance.phase == ControlPhase::Tracking || balance.phase == ControlPhase::Stepping) && (supported[0] || supported[1])) {
			auto goal = JPH::Vec3::sZero();
			for (int i = 0; i < PartCount; ++i) goal += goals[i].GetTranslation() * (mass[i]/totalMass);
			if (balance.phase == ControlPhase::Tracking && desiredVelocity.LengthSq() < .16f) {
				const auto centerOfSupport = canSupport[0] && canSupport[1] ? (left+right)*.5f : support;
				goal.SetX(centerOfSupport.GetX()); goal.SetY(centerOfSupport.GetY());
			}
			if (balance.phase == ControlPhase::Stepping) {
				const float transfer = std::clamp((stepAge-.15f)/.2f, 0.0f, 1.0f) * .5f;
				const auto forward = (goals[11].GetAxisY()*JPH::Vec3(1,1,0)).NormalizedOr(JPH::Vec3::sAxisX());
				const auto shift = planted*(1-transfer)+stepGoal*transfer;
				const auto leading = shift + forward*((stepGoal-planted).Dot(forward)*(.5f-transfer));
				goal.SetX(leading.GetX()); goal.SetY(leading.GetY());
			}
			auto force = (goal-center)*600 + (desiredVelocity-velocity)*150;
			if (balance.phase == ControlPhase::Tracking) force -= error*900;
			force.SetZ(0);
			const float limit = 220 * balance.strength;
			if (force.Length() > limit) force = force.Normalized()*limit;
			api.AddForce(bodies[0], force);
			balance.assistForce = force.Length();
			JPH::Vec3 axis; float angle;
			(goals[0].GetQuaternion()*api.GetRotation(bodies[0]).Conjugated()).GetAxisAngle(axis, angle);
			if (balance.phase == ControlPhase::Tracking) {
				auto torque = axis*(angle*250) - api.GetAngularVelocity(bodies[0])*45;
				if (torque.Length() > 80) torque *= 80/torque.Length();
				api.AddTorque(bodies[0], torque);
				balance.assistTorque = torque.Length();
			}
			if (balance.phase == ControlPhase::Stepping && supported[1-swing]) {
				auto torque = axis*(angle*600) - api.GetAngularVelocity(bodies[0])*80;
				const int stance = swing ? 7 : 9;
				const float cap = 180*(1-weakness[stance])*regional.strength[int(Region::Legs)];
				if (torque.Length() > cap) torque *= cap/torque.Length();
				api.AddTorque(bodies[0], torque); api.AddTorque(bodies[stance], -torque);
			}
		}
	}
	void DeathControl() {
		if (deathStrength <= 0) return;
		deathAge += Step;
		const float t = std::min(1.0f, deathAge/.45f);
		const float fade = 1-t*t*(3-2*t);
		balance.strength = deathStrength*fade;
		if (trunkContact || t >= 1) {
			deathStrength = balance.strength = 0;
			for (int i = 1; i < PartCount; ++i) {
				joints[i]->SetSwingMotorState(JPH::EMotorState::Off);
				joints[i]->SetTwistMotorState(JPH::EMotorState::Off);
			}
			for (auto* b : body) b->SetAllowSleeping(true);
			return;
		}
		JPH::Mat44 goals[PartCount];
		std::copy(deathPose, deathPose+PartCount, goals);
		// Lower the hips while the feet keep their initial targets. The solver permits yielding.
		for (int side = 0; side < 2; ++side) {
			const int upper = side ? 9 : 7;
			const auto hip = (deathPose[upper]*offsets[upper]).GetTranslation() - JPH::Vec3(0,0,.22f*t*t);
			LegTargets(goals, side, deathPose[upper+1]*endLocal[upper+1], &hip);
		}
		const auto forward = (deathPose[11].GetAxisY()*JPH::Vec3(1,1,0)).NormalizedOr(JPH::Vec3::sAxisX());
		const auto fold = JPH::Quat::sRotation(JPH::Vec3::sAxisZ().Cross(forward), .25f*t);
		goals[1] = JPH::Mat44::sRotationTranslation(fold*goals[1].GetQuaternion(), goals[1].GetTranslation());
		for (int i = 1; i < PartCount; ++i) {
			joints[i]->SetTargetOrientationBS(goals[parent[i]].GetQuaternion().Conjugated()*goals[i].GetQuaternion());
			const float region = i >= 7 ? fade : 1.0f;
			const float torque = deathTorque[i]*fade*region;
			joints[i]->GetSwingMotorSettings().SetTorqueLimit(torque);
			joints[i]->GetTwistMotorSettings().SetTorqueLimit(torque);
		}
	}
	explicit Impl(const Part* parts, const float* velocity, float gravity) {
		world.Init(2048, 0, 8192, 8192, layers, layers, layers);
		world.SetContactListener(&listener);
		world.SetGravity(JPH::Vec3(0, 0, -gravity));
		auto& interface = world.GetBodyInterface();
		for (int i = 0; i < PartCount; ++i) {
			parent[i] = parts[i].parent; mass[i] = parts[i].mass; totalMass += mass[i];
			const auto bone = Matrix(parts[i].bone);
			const auto start = bone.GetTranslation(), end = Vector(parts[i].end);
			const auto delta = end - start;
			const auto q = JPH::Quat::sFromTo(JPH::Vec3::sAxisY(), delta.Normalized());
			const auto center = (start + end) * 0.5f - JPH::Vec3(0,0,i >= 11 ? AnkleHeight-SoleHalfHeight : 0);
			JPH::RefConst<JPH::Shape> shape = i >= 11 ? static_cast<JPH::Shape*>(new JPH::BoxShape(JPH::Vec3(.06f, .12f, SoleHalfHeight), .01f)) :
				static_cast<JPH::Shape*>(new JPH::CapsuleShape(std::max(0.01f, delta.Length() * 0.5f - parts[i].radius), parts[i].radius));
			if (i == 4 || i == 6) {
				const int side = (i-4)/2;
				forearmShape[side] = shape;
				JPH::StaticCompoundShapeSettings forearm;
				forearm.AddShape(JPH::Vec3::sZero(), JPH::Quat::sIdentity(), shape);
				forearm.AddShape(JPH::Vec3(0,delta.Length()*.5f+.025f,0), JPH::Quat::sIdentity(), new JPH::SphereShape(.045f));
				const auto compound = forearm.Create().Get();
				palmShape[side] = new JPH::OffsetCenterOfMassShape(compound, -compound->GetCenterOfMass());
			}
			JPH::BodyCreationSettings settings(shape,
				center, q, JPH::EMotionType::Dynamic, 1);
			settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
			settings.mMassPropertiesOverride.mMass = parts[i].mass;
			settings.mLinearVelocity = Vector(velocity);
			settings.mMotionQuality = JPH::EMotionQuality::LinearCast;
			settings.mFriction = 0.7f;
			settings.mAngularDamping = 0.3f;
			settings.mMaxAngularVelocity = 15;
			settings.mAllowSleeping = false; // Foot contact reports are needed on every control step.
			body[i] = interface.CreateBody(settings);
			bodies[i] = body[i]->GetID();
			interface.AddBody(bodies[i], JPH::EActivation::Activate);
			offsets[i] = body[i]->GetWorldTransform().InversedRotationTranslation() * bone;
			endLocal[i] = body[i]->GetWorldTransform().InversedRotationTranslation() * end;
			targetFrom[i] = target[i] = body[i]->GetWorldTransform();
			position[i] = center; rotation[i] = q;
			if (parts[i].parent >= 0) MakeJoint(i, start, delta.Normalized());
		}
		standingHeight = std::max(.3f, Matrix(parts[0].bone).GetTranslation().GetZ() - std::min(parts[8].end[2], parts[10].end[2]));
		history.Push(0, position, rotation);
	}
	~Impl() {
		for (auto& constraint : world.GetConstraints()) world.RemoveConstraint(constraint);
		auto& interface = world.GetBodyInterface();
		for (auto id : bodies) { interface.RemoveBody(id); interface.DestroyBody(id); }
		for (const auto& mesh : meshes) { interface.RemoveBody(mesh.second); interface.DestroyBody(mesh.second); }
	}
};
FallSimulation::FallSimulation(const Part* parts, const float* velocity, float gravity) : impl(new Impl(parts, velocity, gravity)) {}
FallSimulation::~FallSimulation() = default;
struct CollisionScene::Impl {
	std::shared_ptr<Runtime> runtime = AcquireRuntime();
	std::map<int, JPH::RefConst<JPH::Shape>> shapes;
};
CollisionScene::CollisionScene() : impl(std::make_shared<Impl>()) {}
CollisionScene::~CollisionScene() = default;
bool CollisionScene::AddMesh(int model, const float* vertices, int count) {
	JPH::TriangleList triangles;
	for (int i = 0; i + 2 < count; i += 3) triangles.emplace_back(Vector(vertices+i*3), Vector(vertices+(i+1)*3), Vector(vertices+(i+2)*3));
	JPH::MeshShapeSettings settings(triangles);
	settings.Sanitize();
	const auto result = settings.Create();
	if (result.HasError()) return false;
	impl->shapes[model] = result.Get();
	return true;
}
bool FallSimulation::UseScene(const CollisionScene& scene) {
	for (const auto& entry : scene.impl->shapes) {
		JPH::BodyCreationSettings body(entry.second, JPH::RVec3::sZero(), JPH::Quat::sIdentity(),
			entry.first ? JPH::EMotionType::Kinematic : JPH::EMotionType::Static, 0);
		auto id = impl->world.GetBodyInterface().CreateAndAddBody(body, JPH::EActivation::DontActivate);
		if (id.IsInvalid()) return false;
		impl->meshes[entry.first] = id;
	}
	return !scene.impl->shapes.empty();
}
bool FallSimulation::AddMesh(int model, const float* vertices, int count) {
	JPH::TriangleList triangles;
	for (int i = 0; i + 2 < count; i += 3) triangles.emplace_back(Vector(vertices + i * 3), Vector(vertices + (i + 1) * 3), Vector(vertices + (i + 2) * 3));
	JPH::MeshShapeSettings settings(triangles);
	settings.Sanitize();
	const auto shape = settings.Create();
	if (shape.HasError()) return false;
	JPH::BodyCreationSettings body(shape.Get(), JPH::RVec3::sZero(), JPH::Quat::sIdentity(),
		model ? JPH::EMotionType::Kinematic : JPH::EMotionType::Static, 0);
	auto id = impl->world.GetBodyInterface().CreateAndAddBody(body, JPH::EActivation::DontActivate);
	if (id.IsInvalid()) return false;
	impl->meshes[model] = id;
	return true;
}
void FallSimulation::MoveMesh(int model, const Transform& transform, float seconds) {
	auto found = impl->meshes.find(model);
	if (found == impl->meshes.end()) return;
	const auto m = Matrix(transform);
	auto& api = impl->world.GetBodyInterface();
	if ((api.GetPosition(found->second)-m.GetTranslation()).LengthSq() < 1e-10f &&
		std::abs(api.GetRotation(found->second).Dot(m.GetQuaternion())) > 1-1e-6f &&
		api.GetLinearVelocity(found->second).IsNearZero() && api.GetAngularVelocity(found->second).IsNearZero()) return;
	if (seconds <= 0) api.SetPositionAndRotation(found->second, m.GetTranslation(), m.GetQuaternion(), JPH::EActivation::DontActivate);
	else api.MoveKinematic(found->second, m.GetTranslation(), m.GetQuaternion(), seconds);
}
void FallSimulation::SetMeshEnabled(int model, bool enabled) {
	auto found = impl->meshes.find(model);
	if (found == impl->meshes.end()) return;
	auto& bodies = impl->world.GetBodyInterface();
	const JPH::ObjectLayer layer = enabled ? 0 : 2;
	if (bodies.GetObjectLayer(found->second) != layer) {
		bodies.SetObjectLayer(found->second, layer);
		if (impl->balance.phase == ControlPhase::Dead) bodies.ActivateBodies(impl->bodies, PartCount);
	}
}
void FallSimulation::AddVelocity(const float* velocity) {
	const auto v = Vector(velocity);
	if (v.IsNaN() || v.LengthSq() > 10000 || v.LengthSq() < .000001f) return;
	for (auto id : impl->bodies) impl->world.GetBodyInterface().AddLinearVelocity(id, v);
}
void FallSimulation::SetRootVelocity(const float* velocity) {
	const auto change = Vector(velocity)-impl->world.GetBodyInterface().GetLinearVelocity(impl->bodies[0]);
	const float delta[] = {change.GetX(), change.GetY(), change.GetZ()};
	AddVelocity(delta);
}
void FallSimulation::SurfaceVelocity(int model, const float* point, float* velocity) const {
	const auto found = impl->meshes.find(model);
	const auto v = found == impl->meshes.end() ? JPH::Vec3::sZero() : impl->world.GetBodyInterface().GetPointVelocity(found->second, Vector(point));
	for (int i = 0; i < 3; ++i) velocity[i] = v[i];
}
void FallSimulation::Impulse(int part, const float* direction, const float* point, float strength) {
	if (part < 0 || part >= PartCount) return;
	impl->world.GetBodyInterface().AddImpulse(impl->bodies[part], Vector(direction).NormalizedOr(JPH::Vec3::sAxisX()) * std::clamp(strength, 0.0f, 100.0f), Vector(point));
}
void FallSimulation::Drive(const Part* pose, const float* desiredVelocity, float seconds) {
	auto& s = *impl;
	for (int i = 0; i < PartCount; ++i) {
		s.targetFrom[i] = s.target[i];
		s.target[i] = Matrix(pose[i].bone) * s.offsets[i].InversedRotationTranslation();
		const float dot = std::abs(s.targetFrom[i].GetQuaternion().Normalized().Dot(s.target[i].GetQuaternion().Normalized()));
		s.balance.targetChange = std::max(s.balance.targetChange, JPH::RadiansToDegrees(2*std::acos(std::min(1.0f, dot))));
	}
	s.targetAge = 0; s.targetDuration = std::max(Step, seconds);
	s.desiredVelocity = Vector(desiredVelocity);
}
void FallSimulation::Follow(const Part* pose, float seconds) {
	auto& s = *impl;
	s.PalmCollision(false);
	if (s.balance.phase != ControlPhase::Shadow) {
		for (auto id : s.bodies) s.world.GetBodyInterface().SetMotionType(id, JPH::EMotionType::Kinematic, JPH::EActivation::DontActivate);
		for (int i = 1; i < PartCount; ++i) { s.joints[i]->SetSwingMotorState(JPH::EMotorState::Off); s.joints[i]->SetTwistMotorState(JPH::EMotorState::Off); }
		s.balance.phase = ControlPhase::Shadow; s.balance.strength = 0;
		s.plantedPose = false; s.landedAge = 10;
		s.balance.assistForce = s.balance.assistTorque = 0;
	}
	for (int i = 0; i < PartCount; ++i) {
		const auto bone = Matrix(pose[i].bone);
		const auto start = bone.GetTranslation(), end = Vector(pose[i].end);
		const auto center = (start+end)*.5f - JPH::Vec3(0,0,i >= 11 ? AnkleHeight-SoleHalfHeight : 0);
		const auto goal = JPH::Mat44::sRotationTranslation(JPH::Quat::sFromTo(JPH::Vec3::sAxisY(), (end-start).Normalized()), center);
		s.offsets[i] = goal.InversedRotationTranslation()*bone;
		s.endLocal[i] = goal.InversedRotationTranslation()*end;
		if (seconds > 0) s.world.GetBodyInterface().MoveKinematic(s.bodies[i], goal.GetTranslation(), goal.GetQuaternion().Normalized(), seconds);
		else {
			s.world.GetBodyInterface().SetPositionAndRotation(s.bodies[i], goal.GetTranslation(), goal.GetQuaternion().Normalized(), JPH::EActivation::DontActivate);
			s.position[i] = goal.GetTranslation(); s.rotation[i] = goal.GetQuaternion();
		}
		s.targetFrom[i] = s.target[i] = goal;
	}
	if (seconds > 0) Advance(seconds);
	else s.history.Push(s.clock, s.position, s.rotation);
}
void FallSimulation::Engage() {
	auto& s = *impl;
	if (s.balance.phase == ControlPhase::Tracking || s.balance.phase == ControlPhase::Stepping) return;
	for (int i = 1; i < PartCount; ++i) {
		const auto a = s.world.GetBodyInterface().GetCenterOfMassTransform(s.bodies[s.parent[i]]) * s.joints[i]->GetConstraintToBody1Matrix();
		const auto b = s.world.GetBodyInterface().GetCenterOfMassTransform(s.bodies[i]) * s.joints[i]->GetConstraintToBody2Matrix();
		s.balance.handoffGap = std::max(s.balance.handoffGap, (a.GetTranslation()-b.GetTranslation()).Length());
		JPH::Vec3 axis; float angle; (a.GetQuaternion().Conjugated()*b.GetQuaternion()).GetAxisAngle(axis, angle);
		s.balance.handoffAngle = std::max(s.balance.handoffAngle, JPH::RadiansToDegrees(angle));
		// Animation can move intermediate spine and shoulder bones before engagement.
		// Calibrate their anchors and limit frames without changing a body or its velocity.
		if ((a.GetTranslation()-b.GetTranslation()).LengthSq() > .000001f || angle > .01f) {
			s.world.RemoveConstraint(s.joints[i]);
			const auto transform = s.body[i]->GetWorldTransform();
			s.MakeJoint(i, (transform*s.offsets[i]).GetTranslation(), transform.GetAxisY());
		}
	}
	for (auto id : s.bodies) s.world.GetBodyInterface().SetMotionType(id, JPH::EMotionType::Dynamic, JPH::EActivation::Activate);
	for (int i = 1; i < PartCount; ++i) { s.joints[i]->SetSwingMotorState(JPH::EMotorState::Position); s.joints[i]->SetTwistMotorState(JPH::EMotorState::Position); }
	s.balance.phase = ControlPhase::Tracking; s.balance.strength = 1;
	s.unsupported = 0;
}
void FallSimulation::React(int part, const float* direction, const float* point, float impulse, float weakness) {
	if (part < 0 || part >= PartCount) return;
	if (impl->balance.phase == ControlPhase::Shadow) Engage();
	Impulse(part, direction, point, impulse);
	impl->weakness[part] = std::max(impl->weakness[part], std::clamp(weakness, 0.0f, 1.0f));
	impl->weaknessHold[part] = .3f;
	impl->recentStress = std::min(.12f, impl->recentStress + weakness*.06f);
	if (part >= 7 && part < 11) {
		const int side = part >= 9 ? 1 : 0;
		impl->injuredFoot[side] = impl->Foot(side);
		const int other = part % 2 ? part+1 : part-1;
		impl->weakness[other] = impl->weakness[part];
		impl->weaknessHold[other] = .3f;
	}
	impl->reactionAxis = JPH::Vec3::sAxisZ().Cross(Vector(direction)).NormalizedOr(JPH::Vec3::sAxisY());
	impl->reactionAngle = part >= 7 ? .05f : std::clamp(.15f + weakness*.25f, .15f, .35f);
	impl->reactionAge = 0;
}
void FallSimulation::ReleaseControl() {
	if (impl->balance.phase == ControlPhase::Shadow) Engage();
	impl->balance.phase = ControlPhase::Falling;
	impl->balance.assistForce = impl->balance.assistTorque = 0;
}
void FallSimulation::SetVitality(float fraction) {
	impl->vitality = std::clamp(fraction, 0.0f, 1.0f);
}
void FallSimulation::SetRegionalControl(const RegionalControl& control) {
	for (int i = 0; i < int(Region::Count); ++i)
		impl->requestedRegional.strength[i] = std::isfinite(control.strength[i]) ? std::clamp(control.strength[i], 0.0f, 1.5f) : 1;
}
void FallSimulation::Grip(const Part* pose, const float* target, bool lift) {
	auto& s = *impl;
	if (!s.balance.gripping) {
		s.gripAge = 0;
		s.nextGripStruggle = .45f; s.balance.gripStruggles = 0;
		s.gripGoal = s.world.GetBodyInterface().GetWorldTransform(s.bodies[1])*s.endLocal[1];
	}
	s.balance.gripping = true; s.gripLift = lift; s.gripTarget = Vector(target);
	for (int i = 0; i < PartCount; ++i) s.gripPose[i] = Matrix(pose[i].bone)*s.offsets[i].InversedRotationTranslation();
	if (lift) {
		if (s.balance.phase != ControlPhase::Dead) ReleaseControl();
		s.world.GetBodyInterface().ActivateBodies(s.bodies, PartCount);
	}
}
void FallSimulation::ReleaseGrip() {
	if (!impl->balance.gripping) return;
	impl->balance.gripping = false; impl->balance.gripForce = 0;
	if (impl->gripLift && impl->balance.phase != ControlPhase::Dead) ReleaseControl();
	impl->gripLift = false;
}
void FallSimulation::Electrocute(float intensity, const float* pushDirection, float pushSpeed) {
	if (!std::isfinite(intensity) || impl->balance.phase == ControlPhase::Dead) return;
	if (impl->shockRemaining <= 0) {
		impl->shockAge = 0;
		impl->shockPushLimit = impl->balance.shockPushUsed = 0;
	}
	if (pushDirection && std::isfinite(pushSpeed) && pushSpeed > 0 && !Vector(pushDirection).IsNaN()) {
		impl->shockPushLimit = std::max(impl->shockPushLimit, std::min(20.0f, pushSpeed));
		impl->shockPushDirection = Vector(pushDirection).NormalizedOr(JPH::Vec3::sZero());
	}
	impl->shockTarget = std::clamp(intensity, .1f, 1.0f);
	impl->shockRemaining = .3f;
}
void FallSimulation::Kill(bool soften) {
	auto& s = *impl;
	if (s.balance.phase == ControlPhase::Dead) return;
	s.shockRemaining = s.balance.shock = 0;
	const float height = (s.world.GetBodyInterface().GetWorldTransform(s.bodies[0])*s.offsets[0]).GetTranslation().GetZ()
		- std::min(s.Foot(0).GetZ(), s.Foot(1).GetZ());
	s.deathStrength = soften && !s.balance.gripping && !s.trunkContact && height > .45f ? s.balance.strength : 0;
	for (int i = 0; i < PartCount; ++i) s.deathPose[i] = s.world.GetBodyInterface().GetWorldTransform(s.bodies[i]);
	for (int i = 1; i < PartCount; ++i) {
		// A newly engaged rig may not have run its first bounded motor update yet.
		const float maximum = i >= 11 ? 400.0f : i >= 7 ? 900.0f : i == 2 ? 45.0f : i == 1 ? 250.0f : 80.0f;
		s.deathTorque[i] = std::min(maximum, s.joints[i]->GetSwingMotorSettings().mMaxTorqueLimit);
	}
	s.balance.phase = ControlPhase::Dead;
	s.balance.strength = s.deathStrength;
	s.balance.assistForce = s.balance.assistTorque = 0;
	s.balance.braceMask = 0;
	if (s.deathStrength > 0) return;
	for (int i = 1; i < PartCount; ++i) {
		s.joints[i]->SetSwingMotorState(JPH::EMotorState::Off);
		s.joints[i]->SetTwistMotorState(JPH::EMotorState::Off);
	}
	for (auto* body : s.body) body->SetAllowSleeping(true);
}
bool FallSimulation::Awake() const {
	for (auto id : impl->bodies) if (impl->world.GetBodyInterface().IsActive(id)) return true;
	return false;
}
float FallSimulation::TrunkSpeed() const {
	float speed = 0;
	for (int i = 0; i < 3; ++i) {
		const auto& api = impl->world.GetBodyInterface();
		speed = std::max(speed, api.GetLinearVelocity(impl->bodies[i]).Length() + .2f*api.GetAngularVelocity(impl->bodies[i]).Length());
	}
	return speed;
}
void FallSimulation::PrepareRecovery(const Transform* bones, float seconds) {
	auto& s = *impl;
	s.preparationAge = 0; s.preparationDuration = std::max(.35f, seconds);
	for (int i = 0; i < PartCount; ++i) {
		s.preparationFrom[i] = s.world.GetBodyInterface().GetWorldTransform(s.bodies[i]);
		s.preparationTarget[i] = Matrix(bones[i])*s.offsets[i].InversedRotationTranslation();
	}
	s.balance.phase = ControlPhase::Preparing;
	s.balance.braceMask = 0;
	s.balance.assistForce = s.balance.assistTorque = 0;
}
bool FallSimulation::RecoveryPathClear(const Transform* bones) const {
	Transform from[PartCount], sample[PartCount];
	Sample(from);
	JPH::Vec3 previous[4][2];
	for (int frame = 0; frame <= 16; ++frame) {
		std::copy(bones, bones+PartCount, sample);
		BlendRecovery(from, sample, PartCount, frame/16.0f);
		for (int i = 3; i <= 6; ++i) {
			const auto body = Matrix(sample[i])*impl->offsets[i].InversedRotationTranslation();
			const JPH::Vec3 points[] = {Matrix(sample[i]).GetTranslation(), body*impl->endLocal[i]};
			for (int p = 0; p < 2; ++p) {
				if (frame) {
					const auto delta = points[p]-previous[i-3][p];
					JPH::RayCastResult hit;
					if (delta.LengthSq() > .000001f && impl->world.GetNarrowPhaseQuery().CastRay(JPH::RRayCast(previous[i-3][p], delta), hit, {}, JPH::SpecifiedObjectLayerFilter(0))) return false;
				}
				previous[i-3][p] = points[p];
			}
		}
	}
	return true;
}
BalanceStatus FallSimulation::Balance() const { return impl->balance; }
void FallSimulation::RootVelocity(float* velocity) const {
	const auto v = impl->world.GetBodyInterface().GetLinearVelocity(impl->bodies[0]);
	for (int i = 0; i < 3; ++i) velocity[i] = v[i];
}
bool FallSimulation::Advance(float seconds) {
	if (!std::isfinite(seconds) || seconds < 0 || seconds > 0.25f) return false;
	auto& s = *impl;
	if (s.balance.phase == ControlPhase::Dead && !s.world.GetNumActiveBodies(JPH::EBodyType::RigidBody)) {
		s.clock += seconds;
		s.history.Push(s.clock, s.position, s.rotation);
		return true;
	}
	s.accumulator += seconds;
	while (s.accumulator + 0.000001f >= Step) {
		if (s.balance.strength > 0 || s.balance.phase != ControlPhase::Falling) s.Control();
		s.supported[0] = s.supported[1] = false;
		s.handContacts = 0;
		s.trunkContact = false;
		if (s.world.Update(Step, 1, s.runtime->allocator.get(), s.runtime->jobs.get()) != JPH::EPhysicsUpdateError::None) return false;
		for (int i = 0; i < PartCount; ++i) {
			s.position[i] = s.world.GetBodyInterface().GetPosition(s.bodies[i]);
			s.rotation[i] = s.world.GetBodyInterface().GetRotation(s.bodies[i]);
			if (s.position[i].IsNaN() || s.rotation[i].IsNaN()) return false;
		}
		s.accumulator = std::max(0.0f, s.accumulator - Step);
		++s.steps;
		s.clock += 1.0 / 120;
		s.history.Push(s.clock, s.position, s.rotation);
	}
	return true;
}
void FallSimulation::Sample(Transform* bones, float ahead) const {
	JPH::Vec3 positions[PartCount];
	JPH::Quat rotations[PartCount];
	impl->history.Sample(impl->clock + impl->accumulator + ahead, positions, rotations);
	for (int i = 0; i < PartCount; ++i)
		Store(JPH::Mat44::sRotationTranslation(rotations[i], positions[i]) * impl->offsets[i], bones[i]);
}
float FallSimulation::Speed() const {
	float speed = 0;
	for (auto id : impl->bodies) speed = std::max(speed, impl->world.GetBodyInterface().GetLinearVelocity(id).Length());
	return speed;
}
unsigned FallSimulation::Steps() const { return impl->steps; }
void FallSimulation::Bounds(float* mins, float* maxs) const {
	JPH::AABox bounds;
	for (auto id : impl->bodies) bounds.Encapsulate(impl->world.GetBodyInterface().GetTransformedShape(id).GetWorldSpaceBounds());
	for (int i = 0; i < 3; ++i) { mins[i] = bounds.mMin[i]; maxs[i] = bounds.mMax[i]; }
}
void BlendTransforms(const Transform* from, Transform* to, int count, float alpha) {
	alpha = std::clamp(alpha, 0.0f, 1.0f);
	for (int i = 0; i < count; ++i) {
		const auto a = Matrix(from[i]), b = Matrix(to[i]);
		Store(JPH::Mat44::sRotationTranslation(a.GetQuaternion().Normalized().SLERP(b.GetQuaternion().Normalized(), alpha),
			a.GetTranslation() * (1 - alpha) + b.GetTranslation() * alpha), to[i]);
	}
}
float RecoveryDuration(const Transform* from, const Transform* to, int count) {
	float seconds = .35f;
	for (int i = 0; i < count; ++i) {
		const auto a = Matrix(from[i]), b = Matrix(to[i]);
		const float dot = std::abs(a.GetQuaternion().Normalized().Dot(b.GetQuaternion().Normalized()));
		const float angle = 2*std::acos(std::min(1.0f, dot));
		seconds = std::max(seconds, 1.875f*std::max((a.GetTranslation()-b.GetTranslation()).Length()/.75f, angle/JPH::DegreesToRadians(150.0f)));
	}
	return seconds;
}
void BlendRecovery(const Transform* from, Transform* to, int count, float alpha) {
	BlendTransforms(from, to, count, Ease(alpha));
}
}
