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
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/TransformedShape.h>
#include <map>
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
	std::shared_ptr<Runtime> runtime = AcquireRuntime();
	FallLayers layers;
	JPH::TempAllocatorImpl allocator{16 * 1024 * 1024};
	JPH::JobSystemSingleThreaded jobs{1024};
	JPH::PhysicsSystem world;
	JPH::BodyID bodies[PartCount];
	JPH::Mat44 offsets[PartCount];
	JPH::Vec3 previousPosition[PartCount], position[PartCount];
	JPH::Quat previousRotation[PartCount], rotation[PartCount];
	std::map<int, JPH::BodyID> meshes;
	float accumulator = 0;
	unsigned steps = 0;
	explicit Impl(const Part* parts, const float* velocity, float gravity) {
		world.Init(2048, 0, 8192, 8192, layers, layers, layers);
		world.SetGravity(JPH::Vec3(0, 0, -gravity));
		auto& interface = world.GetBodyInterface();
		JPH::Body* body[PartCount];
		for (int i = 0; i < PartCount; ++i) {
			const auto bone = Matrix(parts[i].bone);
			const auto start = bone.GetTranslation(), end = Vector(parts[i].end);
			const auto delta = end - start;
			const auto q = JPH::Quat::sFromTo(JPH::Vec3::sAxisY(), delta.Normalized());
			const auto center = (start + end) * 0.5f;
			JPH::BodyCreationSettings settings(new JPH::CapsuleShape(std::max(0.01f, delta.Length() * 0.5f - parts[i].radius), parts[i].radius),
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
				world.AddConstraint(joint.Create(*body[parts[i].parent], *body[i]));
			}
		}
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
bool FallSimulation::Advance(float seconds) {
	if (!std::isfinite(seconds) || seconds < 0 || seconds > 0.25f) return false;
	auto& s = *impl;
	s.accumulator += seconds;
	while (s.accumulator + 0.000001f >= Step) {
		for (int i = 0; i < PartCount; ++i) { s.previousPosition[i] = s.position[i]; s.previousRotation[i] = s.rotation[i]; }
		if (s.world.Update(Step, 1, &s.allocator, &s.jobs) != JPH::EPhysicsUpdateError::None) return false;
		for (int i = 0; i < PartCount; ++i) {
			s.position[i] = s.world.GetBodyInterface().GetPosition(s.bodies[i]);
			s.rotation[i] = s.world.GetBodyInterface().GetRotation(s.bodies[i]);
			if (s.position[i].IsNaN() || s.rotation[i].IsNaN()) return false;
		}
		s.accumulator = std::max(0.0f, s.accumulator - Step);
		++s.steps;
	}
	return true;
}
void FallSimulation::Sample(Transform* bones, float ahead) const {
	const float alpha = std::clamp((impl->accumulator + ahead) / Step, 0.0f, 1.0f);
	for (int i = 0; i < PartCount; ++i)
		Store(JPH::Mat44::sRotationTranslation(impl->previousRotation[i].SLERP(impl->rotation[i], alpha),
			impl->previousPosition[i] * (1 - alpha) + impl->position[i] * alpha) * impl->offsets[i], bones[i]);
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
