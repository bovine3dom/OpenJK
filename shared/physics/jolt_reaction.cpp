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
template<size_t N> struct PoseHistory {
	struct Frame {
		double time;
		std::array<JPH::Vec3, N> position;
		std::array<JPH::Quat, N> rotation;
	};
	std::deque<Frame> frames;
	void Push(double time, const JPH::Vec3* positions, const JPH::Quat* rotations) {
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
			previous[i] = current[i] = JPH::Quat::sIdentity();
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
	for (int i = 0; i < 2; ++i) s.previous[i] = s.current[i] = JPH::Quat::sIdentity();
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
			for (int side = 0; side < 2; ++side) {
				const bool first = a.GetID() == owner->bodies[11+side], second = b.GetID() == owner->bodies[11+side];
				if ((first && manifold.mWorldSpaceNormal.GetZ() < -.5f) || (second && manifold.mWorldSpaceNormal.GetZ() > .5f)) {
					owner->supported[side] = true;
					owner->supportHeight[side] = (first ? manifold.GetWorldSpaceContactPointOn2(0) : manifold.GetWorldSpaceContactPointOn1(0)).GetZ();
				}
			}
		}
		void OnContactAdded(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& m, JPH::ContactSettings&) override { Record(a,b,m); }
		void OnContactPersisted(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& m, JPH::ContactSettings&) override { Record(a,b,m); }
	};
	std::shared_ptr<Runtime> runtime = AcquireRuntime();
	FallLayers layers;
	JPH::TempAllocatorImpl allocator{16 * 1024 * 1024};
	JPH::JobSystemSingleThreaded jobs{1024};
	JPH::PhysicsSystem world;
	FootContacts listener{this};
	bool supported[2] = {};
	float supportHeight[2] = {};
	JPH::BodyID bodies[PartCount];
	JPH::Mat44 offsets[PartCount];
	JPH::Mat44 targetFrom[PartCount], target[PartCount];
	JPH::Vec3 endLocal[PartCount];
	JPH::Ref<JPH::SwingTwistConstraint> joints[PartCount];
	int parent[PartCount];
	float mass[PartCount], weakness[PartCount] = {}, weaknessHold[PartCount] = {};
	float totalMass = 0, targetAge = 0, targetDuration = Step, reactionAge = 10, reactionAngle = 0;
	float stepAge = 0, unsupported = 0, standingHeight = .8f;
	int swing = 0;
	JPH::Vec3 desiredVelocity = JPH::Vec3::sZero(), reactionAxis = JPH::Vec3::sAxisY();
	JPH::Vec3 stepStart = JPH::Vec3::sZero(), stepGoal = JPH::Vec3::sZero(), planted = JPH::Vec3::sZero();
	JPH::Vec3 footBias[2] = {JPH::Vec3::sZero(), JPH::Vec3::sZero()};
	BalanceStatus balance;
	JPH::Vec3 previousPosition[PartCount], position[PartCount];
	JPH::Quat previousRotation[PartCount], rotation[PartCount];
	PoseHistory<PartCount> history;
	double clock = 0;
	std::map<int, JPH::BodyID> meshes;
	float accumulator = 0;
	unsigned steps = 0;
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
	void LegTargets(JPH::Mat44* goals, int side, JPH::Vec3Arg foot) {
		const int upper = side ? 9 : 7, lower = upper + 1;
		const auto referenceHip = (goals[upper] * offsets[upper]).GetTranslation();
		const auto hip = (world.GetBodyInterface().GetWorldTransform(bodies[upper]) * offsets[upper]).GetTranslation();
		const auto knee = (goals[lower] * offsets[lower]).GetTranslation() + hip-referenceHip;
		const auto oldFoot = goals[lower] * endLocal[lower];
		const float a = std::max(.05f, (knee-hip).Length()), b = std::max(.05f, (oldFoot+hip-referenceHip-knee).Length());
		const auto direction = (foot-hip).NormalizedOr(-JPH::Vec3::sAxisZ());
		const float d = std::clamp((foot-hip).Length(), std::abs(a-b)+.005f, a+b-.005f);
		const float along = (a*a-b*b+d*d)/(2*d);
		auto bend = knee-hip-direction*(knee-hip).Dot(direction);
		bend = bend.NormalizedOr(direction.GetNormalizedPerpendicular());
		const auto joint = hip + direction*along + bend*std::sqrt(std::max(0.0f,a*a-along*along));
		const auto reachable = hip + direction*d;
		goals[upper] = JPH::Mat44::sRotationTranslation(JPH::Quat::sFromTo(JPH::Vec3::sAxisY(), (joint-hip).Normalized()), (joint+hip)*.5f);
		goals[lower] = JPH::Mat44::sRotationTranslation(JPH::Quat::sFromTo(JPH::Vec3::sAxisY(), (reachable-joint).Normalized()), (joint+reachable)*.5f);
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
			const auto torque = (foot-joint).Cross(force) * balance.strength * (1-.98f*weakness[i]);
			api.AddTorque(bodies[i], torque); api.AddTorque(bodies[parent[i]], -torque);
		}
	}
	void Control() {
		if (balance.phase == ControlPhase::Shadow) return;
		auto& api = world.GetBodyInterface();
		targetAge += Step; reactionAge += Step;
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
		if (balance.phase == ControlPhase::Tracking) for (int side = 0; side < 2; ++side) {
			if (desiredVelocity.LengthSq() > .16f) footBias[side] *= std::max(0.0f, 1-Step*3);
			if (footBias[side].LengthSq() > .0001f)
				LegTargets(goals, side, (goals[11+side]*offsets[11+side]).GetTranslation()+footBias[side]);
		}
		const auto left = Foot(0), right = Foot(1);
		const bool contacts[] = {supported[0], supported[1]};
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
		unsupported = contacts[0] || contacts[1] ? 0 : unsupported+Step;
		if (balance.phase != ControlPhase::Falling) {
			if (unsupported > .4f || balance.pelvisHeight < standingHeight * .48f) balance.phase = ControlPhase::Falling;
			if (balance.phase == ControlPhase::Tracking && balance.error > .14f) {
				swing = !contacts[0] && contacts[1] ? 0 : !contacts[1] && contacts[0] ? 1 :
					(error.Dot(left-right) > 0 ? 0 : 1);
				stepStart = swing ? right : left; planted = swing ? left : right;
				const auto lateral = ((left-right)*JPH::Vec3(1,1,0)).NormalizedOr(JPH::Vec3::sAxisY());
				auto requested = capture + lateral*(swing ? -.12f : .12f) + error.NormalizedOr(JPH::Vec3::sZero())*.08f;
				requested.SetZ(stepStart.GetZ());
				const auto reach = requested-stepStart;
				if (reach.Length() > .5f) requested = stepStart + reach.Normalized()*.5f;
				if (Ground(requested, stepGoal) && stepGoal.GetZ()-stepStart.GetZ() < .25f) {
					stepGoal.SetZ(stepGoal.GetZ()+.04f); stepAge = 0;
					balance.phase = ControlPhase::Stepping; ++balance.corrections;
				}
			}
			if (balance.phase == ControlPhase::Stepping) {
				stepAge += Step;
				const float u = std::clamp((stepAge-.08f) / .24f, 0.0f, 1.0f), smooth = u*u*(3-2*u);
				const auto foot = stepStart*(1-smooth)+stepGoal*smooth+JPH::Vec3(0,0,std::sin(JPH::JPH_PI*u)*.10f);
				LegTargets(goals, swing, foot); LegTargets(goals, 1-swing, planted);
				FootControl(swing, foot); FootControl(1-swing, planted);
				if (stepAge > .4f && ((contacts[swing] && (Foot(swing)-stepGoal).Length() < .08f) || stepAge > .9f)) {
					footBias[swing] = Foot(swing) - (target[11+swing]*offsets[11+swing]).GetTranslation();
					balance.phase = ControlPhase::Tracking;
				}
			}
			if (balance.error > .65f) balance.phase = ControlPhase::Falling;
		}
		const float wantedStrength = balance.phase == ControlPhase::Falling ? .12f : 1.0f;
		balance.strength += std::clamp(wantedStrength-balance.strength, -Step*3, Step*2);
		const float pulse = reactionAge < .08f ? reactionAge/.08f : std::max(0.0f, 1-(reactionAge-.08f)/.55f);
		const auto recoil = JPH::Quat::sRotation(reactionAxis, reactionAngle * pulse);
		goals[1] = JPH::Mat44::sRotationTranslation(recoil*goals[1].GetQuaternion(), goals[1].GetTranslation());
		goals[2] = JPH::Mat44::sRotationTranslation(JPH::Quat::sRotation(reactionAxis, reactionAngle*pulse*.35f)*goals[2].GetQuaternion(), goals[2].GetTranslation());
		for (int i = 1; i < PartCount; ++i) {
			auto desired = goals[i].GetQuaternion();
			const bool worldHip = (i == 7 || i == 9) && (balance.phase == ControlPhase::Stepping || footBias[i == 9 ? 1 : 0].LengthSq() > .0001f);
			const auto q = balance.phase == ControlPhase::Falling ?
				target[parent[i]].GetQuaternion().Conjugated()*target[i].GetQuaternion() :
				(worldHip ? api.GetRotation(bodies[parent[i]]) : goals[parent[i]].GetQuaternion()).Conjugated()*desired;
			joints[i]->SetTargetOrientationBS(q);
			const float torque = (i >= 11 ? 400.0f : i >= 7 ? 900.0f : i == 2 ? 45.0f : i == 1 ? 250.0f : 80.0f) * balance.strength * (1-.98f*weakness[i]);
			joints[i]->GetSwingMotorSettings().SetTorqueLimit(torque);
			joints[i]->GetTwistMotorSettings().SetTorqueLimit(torque);
		}
		// Bounded horizontal assistance is a bootstrap controller, not an upward pelvis tether.
		if (balance.phase != ControlPhase::Falling && (contacts[0] || contacts[1])) {
			auto goal = JPH::Vec3::sZero();
			for (int i = 0; i < PartCount; ++i) goal += goals[i].GetTranslation() * (mass[i]/totalMass);
			if (balance.phase == ControlPhase::Tracking && desiredVelocity.LengthSq() < .16f) {
				goal.SetX(support.GetX()); goal.SetY(support.GetY());
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
		}
	}
	explicit Impl(const Part* parts, const float* velocity, float gravity) {
		world.Init(2048, 0, 8192, 8192, layers, layers, layers);
		world.SetContactListener(&listener);
		world.SetGravity(JPH::Vec3(0, 0, -gravity));
		auto& interface = world.GetBodyInterface();
		JPH::Body* body[PartCount];
		for (int i = 0; i < PartCount; ++i) {
			parent[i] = parts[i].parent; mass[i] = parts[i].mass; totalMass += mass[i];
			const auto bone = Matrix(parts[i].bone);
			const auto start = bone.GetTranslation(), end = Vector(parts[i].end);
			const auto delta = end - start;
			const auto q = JPH::Quat::sFromTo(JPH::Vec3::sAxisY(), delta.Normalized());
			const auto center = (start + end) * 0.5f;
			JPH::RefConst<JPH::Shape> shape = i >= 11 ? static_cast<JPH::Shape*>(new JPH::BoxShape(JPH::Vec3(.06f, .12f, .035f), .01f)) :
				static_cast<JPH::Shape*>(new JPH::CapsuleShape(std::max(0.01f, delta.Length() * 0.5f - parts[i].radius), parts[i].radius));
			JPH::BodyCreationSettings settings(shape,
				center, q, JPH::EMotionType::Dynamic, 1);
			settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
			settings.mMassPropertiesOverride.mMass = parts[i].mass;
			settings.mLinearVelocity = Vector(velocity);
			settings.mMotionQuality = JPH::EMotionQuality::LinearCast;
			settings.mFriction = 0.7f;
			settings.mAngularDamping = 0.3f;
			settings.mMaxAngularVelocity = 15;
			body[i] = interface.CreateBody(settings);
			bodies[i] = body[i]->GetID();
			interface.AddBody(bodies[i], JPH::EActivation::Activate);
			offsets[i] = body[i]->GetWorldTransform().InversedRotationTranslation() * bone;
			endLocal[i] = body[i]->GetWorldTransform().InversedRotationTranslation() * end;
			targetFrom[i] = target[i] = body[i]->GetWorldTransform();
			previousPosition[i] = position[i] = center;
			previousRotation[i] = rotation[i] = q;
			if (parts[i].parent >= 0) {
				JPH::SwingTwistConstraintSettings joint;
				joint.mPosition1 = joint.mPosition2 = start;
				joint.mTwistAxis1 = joint.mTwistAxis2 = delta.Normalized();
				joint.mPlaneAxis1 = joint.mPlaneAxis2 = delta.Normalized().GetNormalizedPerpendicular();
				joint.mNormalHalfConeAngle = joint.mPlaneHalfConeAngle = JPH::DegreesToRadians(i < 3 ? 35.0f : 65.0f);
				joint.mTwistMinAngle = -JPH::DegreesToRadians(25.0f);
				joint.mTwistMaxAngle = JPH::DegreesToRadians(25.0f);
				joint.mMaxFrictionTorque = 1.0f;
				joint.mSwingMotorSettings.mSpringSettings = joint.mTwistMotorSettings.mSpringSettings =
					JPH::SpringSettings(JPH::ESpringMode::StiffnessAndDamping, i >= 7 ? 8000.0f : i == 1 ? 700.0f : 150.0f, i >= 7 ? 100.0f : 15.0f);
				joints[i] = static_cast<JPH::SwingTwistConstraint*>(joint.Create(*body[parts[i].parent], *body[i]));
				world.AddConstraint(joints[i]);
			}
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
	if (seconds <= 0) impl->world.GetBodyInterface().SetPositionAndRotation(found->second, m.GetTranslation(), m.GetQuaternion(), JPH::EActivation::DontActivate);
	else impl->world.GetBodyInterface().MoveKinematic(found->second, m.GetTranslation(), m.GetQuaternion(), seconds);
}
void FallSimulation::SetMeshEnabled(int model, bool enabled) {
	auto found = impl->meshes.find(model);
	if (found == impl->meshes.end()) return;
	auto& bodies = impl->world.GetBodyInterface();
	const JPH::ObjectLayer layer = enabled ? 0 : 2;
	if (bodies.GetObjectLayer(found->second) != layer) bodies.SetObjectLayer(found->second, layer);
}
void FallSimulation::AddVelocity(const float* velocity) {
	const auto v = Vector(velocity);
	if (v.IsNaN() || v.LengthSq() > 10000 || v.LengthSq() < .000001f) return;
	for (auto id : impl->bodies) impl->world.GetBodyInterface().AddLinearVelocity(id, v);
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
	if (s.balance.phase != ControlPhase::Shadow) {
		for (auto id : s.bodies) s.world.GetBodyInterface().SetMotionType(id, JPH::EMotionType::Kinematic, JPH::EActivation::DontActivate);
		for (int i = 1; i < PartCount; ++i) { s.joints[i]->SetSwingMotorState(JPH::EMotorState::Off); s.joints[i]->SetTwistMotorState(JPH::EMotorState::Off); }
		s.balance.phase = ControlPhase::Shadow; s.balance.strength = 0;
	}
	for (int i = 0; i < PartCount; ++i) {
		const auto goal = Matrix(pose[i].bone) * s.offsets[i].InversedRotationTranslation();
		if (seconds > 0) s.world.GetBodyInterface().MoveKinematic(s.bodies[i], goal.GetTranslation(), goal.GetQuaternion().Normalized(), seconds);
		else s.world.GetBodyInterface().SetPositionAndRotation(s.bodies[i], goal.GetTranslation(), goal.GetQuaternion().Normalized(), JPH::EActivation::DontActivate);
		s.targetFrom[i] = s.target[i] = goal;
	}
	if (seconds > 0) Advance(seconds);
}
void FallSimulation::Engage() {
	auto& s = *impl;
	if (s.balance.phase == ControlPhase::Tracking || s.balance.phase == ControlPhase::Stepping) return;
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
	if (part >= 7 && part < 11) {
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
}
BalanceStatus FallSimulation::Balance() const { return impl->balance; }
void FallSimulation::RootVelocity(float* velocity) const {
	const auto v = impl->world.GetBodyInterface().GetLinearVelocity(impl->bodies[0]);
	for (int i = 0; i < 3; ++i) velocity[i] = v[i];
}
bool FallSimulation::Advance(float seconds) {
	if (!std::isfinite(seconds) || seconds < 0 || seconds > 0.25f) return false;
	auto& s = *impl;
	s.accumulator += seconds;
	while (s.accumulator + 0.000001f >= Step) {
		for (int i = 0; i < PartCount; ++i) { s.previousPosition[i] = s.position[i]; s.previousRotation[i] = s.rotation[i]; }
		if (s.balance.strength > 0 || s.balance.phase != ControlPhase::Falling) s.Control();
		s.supported[0] = s.supported[1] = false;
		if (s.world.Update(Step, 1, &s.allocator, &s.jobs) != JPH::EPhysicsUpdateError::None) return false;
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
}
