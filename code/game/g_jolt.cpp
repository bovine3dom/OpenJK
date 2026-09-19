// SPDX-License-Identifier: GPL-2.0-or-later
#include "common_headers.h"
#include "g_jolt.h"
#include "Q3_Interface.h"
#include "../cgame/cg_media.h"
#include "physics/jolt_reaction.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <map>
#include <vector>
#include <set>

extern qboolean PM_InKnockDown(playerState_t* ps);
extern qboolean PM_StabDownAnim(int anim);
extern qboolean PM_InGetUp(playerState_t* ps);
extern qboolean PM_PainAnim(int anim);
extern Vehicle_t* G_IsRidingVehicle(gentity_t* ent);
extern void G_Knockdown(gentity_t* self, gentity_t* attacker, const vec3_t direction, float strength, qboolean breakSaberLock);
extern void G_SetViewEntity(gentity_t* self, gentity_t* target);
extern qboolean G_ClearViewEntity(gentity_t* ent);
extern void WP_FireBlasterMissile(gentity_t* ent, vec3_t start, vec3_t direction, qboolean altFire);
extern qboolean G_StandardHumanoid(gentity_t* ent);
extern void thermalDetonatorExplode(gentity_t* ent);

namespace {
constexpr float MetresPerUnit = 0.0254f;
constexpr size_t MaxActors = 64;
constexpr int RecoveryBlendTime = 180;
const mdxaBone_t identityAngles = {{{1,0,0,0}, {0,1,0,0}, {0,0,1,0}}};
int selectedActor = -1;
const char* demoCases[] = {"idle", "hit", "step", "leg", "run", "fall"};
int demoCase = 0, demoStage = 0, demoDue = 0;
bool demoCycle = false;
vec3_t demoOrigin;
cvar_t* enabled = nullptr;
cvar_t* debug = nullptr;
cvar_t* reactionPose = nullptr;
cvar_t* bodyBudget = nullptr;
cvar_t* lightningPushScale = nullptr;
std::map<int, std::vector<float>> collision;
std::unique_ptr<JoltReaction::CollisionScene> collisionScene;
std::map<int, JoltReaction::Transform> colliderTransforms;
std::set<int> enabledColliders, changedColliders, movingColliders;
int colliderUpdateTime = -1;
bool collisionLoaded = false;
const char* bones[JoltReaction::PartCount] = {"pelvis", "lower_lumbar", "cervical", "lhumerus", "lradius", "rhumerus", "rradius", "lfemurYZ", "ltibia", "rfemurYZ", "rtibia", "ltalus", "rtalus"};
const char* ends[JoltReaction::PartCount] = {"lower_lumbar", "cervical", "cranium", "lradius", "lhand", "rradius", "rhand", "ltibia", "ltalus", "rtibia", "rtalus", "ltalus", "rtalus"};
constexpr int parents[JoltReaction::PartCount] = {-1,0,1,1,3,1,5,0,7,0,9,8,10};

void Settings() {
	if (!enabled) enabled = gi.cvar("g_joltReactions", "1", CVAR_ARCHIVE);
	if (!debug) debug = gi.cvar("g_joltDebug", "0", CVAR_CHEAT);
	if (!reactionPose) reactionPose = gi.cvar("g_joltReactionPose", "1", CVAR_CHEAT);
	if (!bodyBudget) bodyBudget = gi.cvar("g_joltMaxBodies", "10", CVAR_ARCHIVE);
	if (!lightningPushScale) lightningPushScale = gi.cvar("g_joltLightningPushScale", "0.5", CVAR_ARCHIVE);
}
bool Projectile(int mod) {
	switch (mod) {
	case MOD_BLASTER: case MOD_BLASTER_ALT: case MOD_BRYAR: case MOD_BRYAR_ALT:
	case MOD_BOWCASTER: case MOD_BOWCASTER_ALT: case MOD_REPEATER:
	case MOD_FLECHETTE: case MOD_EMPLACED: case MOD_SEEKER: return true;
	default: return false;
	}
}
int PresentationTime(int time) {
	// Keep a complete server interval available for interpolation, even during extrapolated client frames.
	const int fps = std::max(1, gi.Cvar_VariableIntegerValue("sv_fps"));
	return time - 1000 / fps;
}
bool Eligible(const gentity_t* ent);
bool Humanoid(const gentity_t* ent);
bool SavedCorpse(const gentity_t* ent);
int ActiveBodies();
bool ExternalPoseOwner(gentity_t* ent, bool allowGrip = false);
bool AnimationOwnsPose(gentity_t* ent);
bool BoltPosition(gentity_t* ent, int bolt, vec3_t position);
void LocalVector(gentity_t* ent, const vec3_t world, vec3_t local);

struct Actor {
	std::unique_ptr<JoltReaction::Simulation> simulation;
	JoltReaction::Rig dimensions;
	int actor, lastTime = 0, hits = 0, poses = 0;
	int pelvisBolt = -1;
	bool suspended = false;
	float peak = 0;
	double stepMicroseconds = 0;
	unsigned measuredSteps = 0;
	vec3_t lastOrigin;
	std::unique_ptr<JoltReaction::FallSimulation> fall;
	int bolts[JoltReaction::PartCount], endBolts[JoltReaction::PartCount];
	const char* rigBones[JoltReaction::PartCount];
	const char* rigEnds[JoltReaction::PartCount];
	bool rigidHelmet = false;
	JoltReaction::Transform fallPose[JoltReaction::PartCount];
	JoltReaction::Transform recoveryPose[JoltReaction::PartCount];
	JoltReaction::Transform recoveryFrom[JoltReaction::PartCount];
	int prepareStart = 0, prepareTime = 0, recoveryTime = RecoveryBlendTime;
	int recoveryAnim = BOTH_GETUP1, nextRecoverAttempt = 0;
	float recoveryLift = 0;
	int fallStart = 0, settledSince = 0, recoverStart = 0, lastHit = -10000;
	int lastHitMod = MOD_UNKNOWN;
	float fallYaw = 0, instability = 0, launchSpeed = 0, fallImpactSpeed = 0;
	bool fallDamageApplied = false;
	float poseError = 0;
	double fallMicroseconds = 0;
	unsigned fallSteps = 0;
	vec3_t safeOrigin, savedMins, savedMaxs;
	vec3_t pelvisOffset = {};
	vec3_t navigationOrigin = {};
	JoltReaction::Part reference[JoltReaction::PartCount];
	bool engaged = false;
	bool dead = false;
	bool sleepingPose = false;
	bool collidersInitialized = false;
	int gripLevel = 0, gripCaster = -1;
	int lightningCaster = -1, lightningContactStart = 0;
	JoltReaction::Part gripPose[JoltReaction::PartCount];
	int riseStart = 0;
	float handoffError = 0;
	vec3_t displayedMins, displayedMaxs;
	explicit Actor(int number) : actor(number) {
		std::copy(bones, bones+JoltReaction::PartCount, rigBones);
		std::copy(ends, ends+JoltReaction::PartCount, rigEnds);
	}
	~Actor() { Reset(true); }
	bool Initialize(gentity_t* ent);
	bool Active(gentity_t* ent);
	void Reset(bool restoreOrigin);
	void Frame();
	void Hit(gentity_t* ent, const float* direction, const float* point, int damage, int mod, int hitLoc, const gentity_t* attacker, float lightningKnockback);
	void BoneAngles(gentity_t* ent, int bone, int time, float* angles);
	bool Render(gentity_t* ent, int time, const float* origin, float* angles, bool display);
	void UpdatePhysicalHull(gentity_t* ent);
	void MoveColliders(float seconds);
	bool StartFall(gentity_t* ent, const float* direction, const float* point, float strength, int hitPart = 1);
	bool PrepareRig(gentity_t* ent);
	bool ReadParts(gentity_t* ent, JoltReaction::Part* parts, CGhoul2Info_v& models);
	void Engage(gentity_t* ent);
	void UpdateRig(gentity_t* ent, float seconds);
	void ApplyFallDamage(gentity_t* ent);
	bool Recover(gentity_t* ent);
	void ReturnToAnimation(gentity_t* ent);
	void FinishRecovery(gentity_t* ent);
	void Die(gentity_t* ent);
	void StoreCorpse();
	void CancelRecovery(gentity_t* ent);
	void Status();
	void HitCommand();
	void KnockdownCommand();
	void ImpulseCommand();
	void ControlCommand();
};
std::map<int, std::unique_ptr<Actor>> actors;
std::map<int, int> retryRigAfter;
int ActiveBodies() {
	int count = 0;
	for (const auto& entry : actors) if (entry.second->fall && (!entry.second->dead || entry.second->fall->Awake())) ++count;
	return count;
}
bool BodyRoom(const Actor* requester) {
	const int limit = std::max(1, std::min(16, bodyBudget->integer));
	if (ActiveBodies() < limit) return true;
	for (auto& entry : actors) if (entry.second.get() != requester && entry.second->fall && !entry.second->engaged) {
		entry.second->fall.reset();
		if (ActiveBodies() < limit) return true;
	}
	return false;
}

void Actor::UpdatePhysicalHull(gentity_t* ent) {
	vec3_t mins, maxs;
	fall->Bounds(mins, maxs);
	for (int r = 0; r < 3; ++r) {
		ent->mins[r] = std::min(mins[r] / MetresPerUnit, displayedMins[r]) - ent->currentOrigin[r] - 8;
		ent->maxs[r] = std::max(maxs[r] / MetresPerUnit, displayedMaxs[r]) - ent->currentOrigin[r] + 8;
	}
	gi.linkentity(ent);
}

void ClearPhysicalBones(gentity_t* ent) {
	if (!ent->ghoul2.size()) return;
	// Keep cached bone indices valid. StopBoneAngles can remove their entries.
	for (size_t i = 0; i < ent->ghoul2[0].mBlist.size(); ++i)
		if (ent->ghoul2[0].mBlist[i].flags & BONE_ANGLES_PHYSICS)
			gi.G2API_SetBoneAnglesMatrixIndex(&ent->ghoul2[0], int(i), identityAngles, BONE_ANGLES_POSTMULT, nullptr, 0, level.time);
}

struct PoseModel {
	CGhoul2Info_v models;
	explicit PoseModel(gentity_t* ent, bool neutral = true) {
		gi.G2API_CopyGhoul2Instance(ent->ghoul2, models, -1);
		if (!models.size()) return;
		for (size_t i = 0; i < models[0].mBlist.size(); ++i) {
			if (models[0].mBlist[i].boneNumber < 0) continue;
			if (neutral) models[0].mBlist[i].flags &= ~BONE_ANIM_TOTAL;
			if (neutral || (models[0].mBlist[i].flags & BONE_ANGLES_PHYSICS))
				gi.G2API_SetBoneAnglesMatrixIndex(&models[0], int(i), identityAngles, BONE_ANGLES_POSTMULT, nullptr, 0, level.time);
		}
	}
	~PoseModel() { gi.G2API_CleanGhoul2Models(models); }
};
bool SetPoseClip(CGhoul2Info_v& models, const animation_t& clip, bool firstFrame, const char* torso = "lower_lumbar") {
	const int end = firstFrame ? clip.firstFrame + 1 : clip.firstFrame + clip.numFrames;
	for (const char* bone : {"model_root", torso})
		if (!gi.G2API_SetBoneAnim(&models[0], bone, clip.firstFrame, end, BONE_ANIM_OVERRIDE_FREEZE,
			50.0f / clip.frameLerp, level.time, float(clip.firstFrame), 0)) return false;
	if (gi.G2API_GetBoneIndex(&models[0], "Motion", qfalse) >= 0)
		gi.G2API_SetBoneAnim(&models[0], "Motion", clip.firstFrame, end, BONE_ANIM_OVERRIDE_FREEZE, 50.0f/clip.frameLerp, level.time, float(clip.firstFrame), 0);
	return true;
}
void ExportSurface(int model, int count, const float* points, void*) {
	auto& mesh = collision[model];
	for (int i = 0; i < count * 3; ++i) mesh.push_back(points[i] * MetresPerUnit);
}
void Actor::MoveColliders(float seconds) {
	if (colliderUpdateTime != level.time) {
		colliderUpdateTime = level.time;
		changedColliders.swap(movingColliders);
		movingColliders.clear();
		std::set<int> present;
		for (int i = 1; i < globals.num_entities; ++i) {
			const auto& ent = g_entities[i];
			if (!ent.inuse || !ent.bmodel || !(ent.contents & MASK_NPCSOLID) || !ent.model || ent.model[0] != '*') continue;
			const int model = atoi(ent.model + 1);
			if (!collision.count(model)) continue;
			present.insert(model);
			JoltReaction::Transform transform;
			vec3_t axis[3];
			AnglesToAxis(ent.currentAngles, axis);
			for (int r = 0; r < 3; ++r) {
				for (int c = 0; c < 3; ++c) transform.matrix[r][c] = axis[c][r];
				transform.matrix[r][3] = ent.currentOrigin[r] * MetresPerUnit;
			}
			auto previous = colliderTransforms.find(model);
			if (previous == colliderTransforms.end() || memcmp(&previous->second, &transform, sizeof(transform))) {
				changedColliders.insert(model);
				movingColliders.insert(model);
			}
			colliderTransforms[model] = transform;
		}
		for (const auto& mesh : collision) if (mesh.first && enabledColliders.count(mesh.first) != present.count(mesh.first)) changedColliders.insert(mesh.first);
		enabledColliders.swap(present);
	}
	const auto update = [&](int model) {
		auto transform = colliderTransforms.find(model);
		if (enabledColliders.count(model) && transform != colliderTransforms.end()) fall->MoveMesh(model, transform->second, seconds);
		fall->SetMeshEnabled(model, enabledColliders.count(model) != 0);
	};
	if (collidersInitialized) for (int model : changedColliders) update(model);
	else {
		for (const auto& mesh : collision) if (mesh.first) update(mesh.first);
		collidersInitialized = true;
	}
}
bool Actor::StartFall(gentity_t* ent, const float* direction, const float* point, float strength, int hitPart) {
	if (!PrepareRig(ent)) return false;
	Engage(ent);
	vec3_t hit; VectorScale(point, MetresPerUnit, hit);
	fall->Impulse(hitPart, direction, hit, strength);
	fall->ReleaseControl();
	VectorClear(ent->client->ps.velocity); // The rig already carries the native velocity.
	fallImpactSpeed = 0;
	fallDamageApplied = false;
	fallStart = level.time;
	if (g_entities[0].client->ps.viewEntity == actor) G_ClearViewEntity(&g_entities[0]);
	if (debug->integer) gi.Printf("Jolt: released control actor=%d launch_speed=%.1f\n", actor, launchSpeed);
	return true;
}
bool Actor::PrepareRig(gentity_t* ent) {
	if (fall) return true;
	if (!BodyRoom(this)) return false;
	if (!collisionLoaded) {
		collisionLoaded = true;
		if (!gi.PhysicsSurfaces(MASK_NPCSOLID, ExportSurface, nullptr) && debug->integer) gi.Printf("Jolt: unsupported collision format; using small reactions\n");
		if (!collision.empty()) {
			collisionScene.reset(new JoltReaction::CollisionScene);
			for (const auto& mesh : collision) if (!collisionScene->AddMesh(mesh.first, mesh.second.data(), int(mesh.second.size()/3))) {
				collisionScene.reset(); break;
			}
		}
	}
	if (!collisionScene || !ReadParts(ent, reference, ent->ghoul2)) return false;
	vec3_t velocity; VectorScale(ent->client->ps.velocity, MetresPerUnit, velocity);
	fall.reset(new JoltReaction::FallSimulation(reference, velocity, g_gravity->value * MetresPerUnit));
	if (!fall->UseScene(*collisionScene)) { fall.reset(); return false; }
	MoveColliders(0);
	fall->Follow(reference, 0);
	VectorCopy(ent->mins, savedMins); VectorCopy(ent->maxs, savedMaxs);
	vec3_t offset;
	for (int r = 0; r < 3; ++r) offset[r] = reference[0].bone.matrix[r][3]/MetresPerUnit - ent->currentOrigin[r];
	LocalVector(ent, offset, pelvisOffset);
	fallYaw = ent->client->renderInfo.legsYaw;
	lastTime = level.time;
	return true;
}
bool Actor::ReadParts(gentity_t* ent, JoltReaction::Part* parts, CGhoul2Info_v& models) {
	const float masses[] = {12, 24, 5, 3, 2, 3, 2, 8, 4, 8, 4, 1.5f, 1.5f};
	const float radii[] = {.12f, .14f, .075f, .055f, .045f, .055f, .045f, .085f, .06f, .085f, .06f, .06f, .06f};
	vec3_t angles = {0, engaged || dead ? fallYaw : ent->client->renderInfo.legsYaw, 0}, forward;
	AngleVectors(angles, forward, nullptr, nullptr);
	for (int i = 0; i < JoltReaction::PartCount; ++i) {
		if (!fall) {
			bolts[i] = gi.G2API_AddBolt(&models[0], rigBones[i]);
			endBolts[i] = gi.G2API_AddBolt(&models[0], rigEnds[i]);
		}
		mdxaBone_t bone, end;
		if (bolts[i] < 0 || endBolts[i] < 0 || !gi.G2API_GetBoltMatrix(models, 0, bolts[i], &bone, angles, ent->currentOrigin, level.time, nullptr, ent->s.modelScale) ||
			!gi.G2API_GetBoltMatrix(models, 0, endBolts[i], &end, angles, ent->currentOrigin, level.time, nullptr, ent->s.modelScale)) {
			if (debug->integer) gi.Printf("Jolt: missing segment %s -> %s\n", bones[i], ends[i]); return false;
		}
		for (int r = 0; r < 3; ++r) {
			for (int c = 0; c < 4; ++c) parts[i].bone.matrix[r][c] = bone.matrix[r][c] * (c == 3 ? MetresPerUnit : 1);
			parts[i].end[r] = (i == 2 && !rigidHelmet ? 2 * end.matrix[r][3] - bone.matrix[r][3] : end.matrix[r][3]) * MetresPerUnit;
			if (i >= 11) parts[i].end[r] = parts[i].bone.matrix[r][3] + forward[r]*.14f;
		}
		vec3_t delta;
		for (int r = 0; r < 3; ++r) delta[r] = parts[i].end[r] - parts[i].bone.matrix[r][3];
		float length = VectorLength(delta);
		if (i == 0 && length < .015f) {
			// Some armoured rigs put pelvis and lower spine at the same point.
			mdxaBone_t upper;
			const int upperBolt = gi.G2API_AddBolt(&models[0], rigBones[2]);
			if (upperBolt < 0 || !gi.G2API_GetBoltMatrix(models, 0, upperBolt, &upper, angles, ent->currentOrigin, level.time, nullptr, ent->s.modelScale)) return false;
			for (int r = 0; r < 3; ++r) delta[r] = upper.matrix[r][3]-bone.matrix[r][3];
			if (VectorNormalize(delta) < .1f) return false;
			length = .12f;
			for (int r = 0; r < 3; ++r) parts[i].end[r] = parts[i].bone.matrix[r][3]+delta[r]*length;
		}
		if (!std::isfinite(length) || length < 0.015f || length > 1) { if (debug->integer) gi.Printf("Jolt: invalid segment %s %.3f\n", bones[i], length); return false; }
		parts[i].radius = std::min(radii[i], length * .45f);
		parts[i].mass = masses[i];
		parts[i].parent = parents[i];
		if (rigidHelmet) {
			if (i == 1) parts[i].mass = 13;
			if (i == 2) { parts[i].mass = 16; parts[i].radius = std::min(.14f, length*.45f); }
			if (i == 3 || i == 5) parts[i].parent = 2;
		}
	}
	return true;
}
void Actor::Engage(gentity_t* ent) {
	if (engaged) return;
	if (ReadParts(ent, reference, ent->ghoul2)) {
		if (debug->integer > 1) for (const auto& part : reference) {
			gi.Printf("Jolt reference {");
			for (const auto& row : part.bone.matrix) for (float v : row) gi.Printf("%.6ff,", v);
			for (float v : part.end) gi.Printf("%.6ff,", v);
			gi.Printf("%.6ff,%.6ff},\n", part.radius, part.mass);
		}
		const float elapsed = std::max(0, level.time-lastTime)*.001f;
		fall->Follow(reference, elapsed); lastTime = level.time;
		vec3_t velocity; VectorScale(ent->client->ps.velocity, MetresPerUnit, velocity);
		fall->Drive(reference, velocity, .05f);
	}
	engaged = true;
	fall->Engage();
	// Match locomotion velocity even if the last animation sample had no root displacement.
	// A common delta retains the relative linear and angular motion of the limbs.
	vec3_t velocity; VectorScale(ent->client->ps.velocity, MetresPerUnit, velocity);
	const int ground = ent->client->ps.groundEntityNum;
	if (ground > 0 && ground < ENTITYNUM_WORLD && g_entities[ground].inuse && g_entities[ground].bmodel && g_entities[ground].model && g_entities[ground].model[0] == '*') {
		vec3_t point, carry; VectorScale(ent->currentOrigin, MetresPerUnit, point);
		fall->SurfaceVelocity(atoi(g_entities[ground].model+1), point, carry);
		VectorAdd(velocity, carry, velocity);
	}
	fall->SetRootVelocity(velocity);
	launchSpeed = VectorLength(ent->client->ps.velocity);
	fall->Sample(fallPose, 1);
	VectorCopy(ent->currentOrigin, safeOrigin);
	VectorCopy(ent->currentOrigin, navigationOrigin);
	VectorCopy(ent->currentOrigin, lastOrigin);
	VectorCopy(ent->mins, savedMins); VectorCopy(ent->maxs, savedMaxs);
	fallYaw = ent->client->renderInfo.legsYaw; fallStart = 0;
	fallImpactSpeed = 0; fallDamageApplied = false;
	fallMicroseconds = 0; fallSteps = 0;
	settledSince = recoverStart = prepareStart = nextRecoverAttempt = 0;
	riseStart = 0;
	vec3_t carried; fall->RootVelocity(carried);
	if (debug->integer) gi.Printf("Jolt: active control actor=%d speed=%.1f carried=%.3f,%.3f,%.3f gap=%.3f angle=%.1f\n", actor, launchSpeed, carried[0], carried[1], carried[2], fall->Balance().handoffGap, fall->Balance().handoffAngle);
}

void Actor::UpdateRig(gentity_t* ent, float seconds) {
	if (ExternalPoseOwner(ent)) { Reset(false); return; }
	if (!engaged) {
		suspended = AnimationOwnsPose(ent);
		if (!ReadParts(ent, reference, ent->ghoul2)) { Reset(false); return; }
		MoveColliders(std::max(seconds, .001f));
		fall->Follow(reference, seconds);
		if (riseStart && !PM_InGetUp(&ent->client->ps)) riseStart = 0;
		return;
	}
	if (!recoverStart && DistanceSquared(ent->currentOrigin, lastOrigin) > 128 * 128) { Reset(false); return; }
	if ((recoverStart || prepareStart) && TIMER_Exists(ent, "noGetUpStraight") && !TIMER_Done(ent, "noGetUpStraight"))
		CancelRecovery(ent);
	if (recoverStart) {
		// Carry both ends of the handoff with a moving platform.
		for (int i = 0; i < JoltReaction::PartCount; ++i) for (int r = 0; r < 3; ++r) {
			const float movement = (ent->currentOrigin[r]-lastOrigin[r])*MetresPerUnit;
			recoveryFrom[i].matrix[r][3] += movement; recoveryPose[i].matrix[r][3] += movement;
			if (recoveryAnim < 0) { reference[i].bone.matrix[r][3] += movement; reference[i].end[r] += movement; }
		}
		VectorCopy(ent->currentOrigin, lastOrigin);
		Render(ent, level.time, ent->currentOrigin, ent->currentAngles, false);
		UpdatePhysicalHull(ent);
		if (level.time - recoverStart >= recoveryTime) FinishRecovery(ent);
		return;
	}
	const auto phase = fall->Balance().phase;
	MoveColliders(std::max(.001f, seconds));
	if (phase == JoltReaction::ControlPhase::Tracking || phase == JoltReaction::ControlPhase::Stepping) {
		vec3_t desired;
		VectorSubtract(ent->currentOrigin, lastOrigin, desired);
		const auto& command = ent->NPC->last_ucmd;
		const bool moving = !gripLevel && phase == JoltReaction::ControlPhase::Tracking &&
			(command.forwardmove || command.rightmove) && !gi.Cvar_VariableIntegerValue("d_npcfreeze");
		if (moving && seconds > 0) {
			VectorAdd(navigationOrigin, desired, navigationOrigin);
			VectorScale(desired, MetresPerUnit / seconds, desired);
		}
		else VectorClear(desired);
		desired[2] = 0;
		if (moving) {
			PoseModel referenceModel(ent, false);
			if (!referenceModel.models.size() || !ReadParts(ent, reference, referenceModel.models)) { Reset(false); return; }
			for (int i = 0; i < JoltReaction::PartCount; ++i) for (int r = 0; r < 3; ++r) {
				const float offset = (navigationOrigin[r]-ent->currentOrigin[r]) * MetresPerUnit;
				reference[i].bone.matrix[r][3] += offset; reference[i].end[r] += offset;
			}
		}
		fall->Drive(reference, desired, seconds);
	} else {
		vec3_t pushed; VectorScale(ent->client->ps.velocity, MetresPerUnit, pushed);
		if (prepareStart && VectorLengthSquared(pushed) > .01f) { prepareStart = 0; fall->ReleaseControl(); }
		if (!dead) fall->AddVelocity(pushed);
	}
	if (phase == JoltReaction::ControlPhase::Falling && !dead) {
		vec3_t velocity;
		fall->RootVelocity(velocity);
		fallImpactSpeed = std::max(fallImpactSpeed, -velocity[2]);
	}
	const auto start = std::chrono::steady_clock::now();
	const unsigned before = fall->Steps();
	if (!fall->Advance(seconds)) { Reset(true); return; }
	if (dead && sleepingPose && !fall->Awake()) return;
	fallMicroseconds += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
	fallSteps += fall->Steps()-before;
	const auto balance = fall->Balance();
	if (balance.phase == JoltReaction::ControlPhase::Falling && !fallStart) {
		fallStart = level.time;
		fallImpactSpeed = 0;
		fallDamageApplied = false;
		if (g_entities[0].client->ps.viewEntity == actor) G_ClearViewEntity(&g_entities[0]);
		if (debug->integer) gi.Printf("Jolt: lost support actor=%d steps=%u error=%.3f\n", actor, balance.corrections, balance.error);
	}
	fall->Sample(fallPose);
	for (int i = 1; i <= 2; ++i) {
		float trace = 0;
		for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) trace += fallPose[i].matrix[r][c]*reference[i].bone.matrix[r][c];
		peak = std::max(peak, acosf(std::max(-1.0f, std::min(1.0f, (trace-1)*.5f)))*180/float(M_PI));
	}
	const float yaw = fallYaw * M_PI / 180;
	vec3_t origin = {fallPose[0].matrix[0][3] / MetresPerUnit - cosf(yaw)*pelvisOffset[0] + sinf(yaw)*pelvisOffset[1],
		fallPose[0].matrix[1][3] / MetresPerUnit - sinf(yaw)*pelvisOffset[0] - cosf(yaw)*pelvisOffset[1],
		fallPose[0].matrix[2][3] / MetresPerUnit - pelvisOffset[2]};
	G_SetOrigin(ent, origin); VectorCopy(origin, ent->client->ps.origin); VectorCopy(origin, lastOrigin);
	if (phase == JoltReaction::ControlPhase::Stepping && balance.phase == JoltReaction::ControlPhase::Tracking) VectorCopy(origin, navigationOrigin);
	if (balance.phase == JoltReaction::ControlPhase::Tracking) {
		fall->RootVelocity(ent->client->ps.velocity); VectorScale(ent->client->ps.velocity, 1/MetresPerUnit, ent->client->ps.velocity);
	} else VectorClear(ent->client->ps.velocity);
	Render(ent, level.time, origin, ent->currentAngles, false);
	UpdatePhysicalHull(ent);
	if (fallStart && balance.phase == JoltReaction::ControlPhase::Falling &&
		(balance.supportedTrunk || balance.contacts) && !fallDamageApplied)
		ApplyFallDamage(ent);
	if (dead || gripLevel || balance.shock > .05f) return;
	if (prepareStart) {
		if (level.time-prepareStart >= prepareTime && (fall->TrunkSpeed() < .75f || level.time-prepareStart >= prepareTime+250)) {
			if (!Recover(ent)) { prepareStart = 0; fall->ReleaseControl(); }
		}
	} else if (balance.phase == JoltReaction::ControlPhase::Falling) {
		if (balance.supportedTrunk && fall->TrunkSpeed() < .55f) { if (!settledSince) settledSince = level.time; }
		else settledSince = 0;
		if (settledSince && level.time - settledSince >= 180 && level.time - fallStart >= 650 && level.time >= nextRecoverAttempt) Recover(ent);
	} else if (actor != selectedActor && balance.phase == JoltReaction::ControlPhase::Tracking &&
		level.time-lastHit > 1500 && balance.error < .06f && fall->Speed() < .35f) {
		if (!settledSince) settledSince = level.time;
		if (level.time-settledSince > 500) ReturnToAnimation(ent);
	} else settledSince = 0;
}

void Actor::ApplyFallDamage(gentity_t* ent) {
	fallDamageApplied = true;
	if (ent->NPC && (ent->s.weapon == WP_SABER || ent->client->NPC_class == CLASS_REBORN)) return;
	int dflags = DAMAGE_NO_ARMOR;
	float damage;
	if (ent->NPC && (ent->NPC->aiFlags & NPCAI_DIE_ON_IMPACT)) {
		damage = 1000;
		dflags |= DAMAGE_DIE_ON_IMPACT;
	} else {
		const int delta = int(fallImpactSpeed / MetresPerUnit / 10);
		if (delta < 30 || (ent->flags & FL_NO_IMPACT_DMG)) return;
		damage = delta * .5f;
	}
	ent->painDebounceTime = level.time + 200;
	G_Damage(ent, NULL, NULL, NULL, ent->currentOrigin, damage, dflags, MOD_FALLING);
}

void Actor::ReturnToAnimation(gentity_t* ent) {
	PoseModel sample(ent, false);
	JoltReaction::Part native[JoltReaction::PartCount];
	if (!sample.models.size() || !ReadParts(ent, native, sample.models)) return;
	vec3_t start, end;
	VectorCopy(ent->currentOrigin, start); VectorCopy(start, end);
	start[2] += 8; end[2] -= 16;
	trace_t trace;
	gi.trace(&trace, start, savedMins, savedMaxs, end, actor, MASK_NPCSOLID, (EG2_Collision)0, 0);
	if (trace.startsolid || trace.allsolid || trace.fraction == 1 || trace.plane.normal[2] < .7f || fabsf(trace.endpos[2]-ent->currentOrigin[2]) > 8) return;
	const float lift = (trace.endpos[2]-ent->currentOrigin[2])*MetresPerUnit;
	if (fabsf(native[0].bone.matrix[2][3]+lift-fallPose[0].matrix[2][3]) > 8*MetresPerUnit) return;
	for (int i = 0; i < JoltReaction::PartCount; ++i) {
		reference[i] = native[i];
		reference[i].bone.matrix[2][3] += lift; reference[i].end[2] += lift;
		recoveryPose[i] = reference[i].bone;
	}
	G_SetOrigin(ent, trace.endpos); VectorCopy(trace.endpos, ent->client->ps.origin); VectorCopy(trace.endpos, lastOrigin);
	ent->s.groundEntityNum = ent->client->ps.groundEntityNum = trace.entityNum;
	VectorClear(ent->client->ps.velocity);
	std::copy(fallPose, fallPose+JoltReaction::PartCount, recoveryFrom);
	recoveryTime = RecoveryBlendTime;
	recoveryAnim = -1; recoverStart = level.time;
	UpdatePhysicalHull(ent);
}

bool Actor::Recover(gentity_t* ent) {
	nextRecoverAttempt = level.time + 250;
	if (TIMER_Exists(ent, "noGetUpStraight") && !TIMER_Done(ent, "noGetUpStraight")) return false;
	const int file = ent->client->clientInfo.animFileIndex;
	if (file < 0 || file >= level.numKnownAnimFileSets) return false;
	PoseModel sample(ent);
	if (!sample.models.size()) return false;
	float best = 1e30f, bestYaw = fallYaw;
	vec3_t bestOrigin = {}, pelvis;
	int ground = ENTITYNUM_NONE;
	for (int r = 0; r < 3; ++r) pelvis[r] = fallPose[0].matrix[r][3] / MetresPerUnit;
	for (int anim : {BOTH_GETUP1, BOTH_GETUP2, BOTH_GETUP3, BOTH_GETUP4, BOTH_GETUP5}) {
		const auto& clip = level.knownAnimFileSets[file].animations[anim];
		if (clip.numFrames < 2 || clip.frameLerp <= 0 || !SetPoseClip(sample.models, clip, true, rigBones[1])) continue;
		JoltReaction::Transform local[JoltReaction::PartCount], target[JoltReaction::PartCount];
		bool valid = true;
		for (int i = 0; i < JoltReaction::PartCount; ++i) {
			mdxaBone_t matrix;
			if (!gi.G2API_GetBoltMatrix(sample.models, 0, bolts[i], &matrix, vec3_origin, vec3_origin,
				level.time, nullptr, ent->s.modelScale)) { valid = false; break; }
			memcpy(local[i].matrix, matrix.matrix, sizeof(matrix.matrix));
		}
		if (!valid) continue;
		// Fit yaw around the pelvis; do not translate the body to a distant standing spot.
		float dot = 0, cross = 0;
		for (int i = 1; i < JoltReaction::PartCount; ++i) {
			const float x = local[i].matrix[0][3] - local[0].matrix[0][3];
			const float y = local[i].matrix[1][3] - local[0].matrix[1][3];
			const float u = fallPose[i].matrix[0][3] / MetresPerUnit - pelvis[0];
			const float v = fallPose[i].matrix[1][3] / MetresPerUnit - pelvis[1];
			const float weight = i < 3 ? 3 : 1;
			dot += weight * (x*u + y*v); cross += weight * (x*v - y*u);
		}
		const float yaw = atan2f(cross, dot), c = cosf(yaw), s = sinf(yaw);
		vec3_t origin = {pelvis[0] - c*local[0].matrix[0][3] + s*local[0].matrix[1][3],
			pelvis[1] - s*local[0].matrix[0][3] - c*local[0].matrix[1][3], pelvis[2] - local[0].matrix[2][3]};
		vec3_t start, end; VectorCopy(origin, start); VectorCopy(origin, end);
		start[2] += 16; end[2] -= 48;
		trace_t trace;
		gi.trace(&trace, start, savedMins, savedMaxs, end, actor, MASK_NPCSOLID, (EG2_Collision)0, 0);
		if (trace.startsolid || trace.allsolid || trace.fraction == 1 || trace.plane.normal[2] < .7f || fabsf(trace.endpos[2] - origin[2]) > 8) continue;
		float score = 0;
		for (int i = 0; i < JoltReaction::PartCount; ++i) {
			const float weight = i >= 3 && i <= 6 ? 3 : 1;
			for (int col = 0; col < 4; ++col) {
				target[i].matrix[0][col] = c*local[i].matrix[0][col] - s*local[i].matrix[1][col];
				target[i].matrix[1][col] = s*local[i].matrix[0][col] + c*local[i].matrix[1][col];
				target[i].matrix[2][col] = local[i].matrix[2][col];
			}
			for (int r = 0; r < 3; ++r) {
				target[i].matrix[r][3] = (target[i].matrix[r][3] + trace.endpos[r]) * MetresPerUnit;
				const float delta = (target[i].matrix[r][3] - fallPose[i].matrix[r][3]) / MetresPerUnit;
				score += weight * delta * delta;
				for (int col = 0; col < 3; ++col) {
					const float rotation = target[i].matrix[r][col] - fallPose[i].matrix[r][col];
					score += weight * 64 * rotation * rotation;
				}
			}
		}
		if (prepareStart && !fall->RecoveryPathClear(target)) continue;
		if (score < best) {
			best = score; bestYaw = yaw * 180 / M_PI; recoveryAnim = anim;
			ground = trace.entityNum; VectorCopy(trace.endpos, bestOrigin);
			std::copy(target, target + JoltReaction::PartCount, recoveryPose);
		}
	}
	if (ground == ENTITYNUM_NONE) return false;
	if (!prepareStart) {
		prepareStart = level.time;
		prepareTime = int(1000*std::max(.25f, std::min(.65f, JoltReaction::RecoveryDuration(fallPose, recoveryPose, JoltReaction::PartCount))));
		fall->PrepareRecovery(recoveryPose, prepareTime*.001f);
		return true;
	}
	std::copy(fallPose, fallPose+JoltReaction::PartCount, recoveryFrom);
	recoveryTime = int(std::ceil(1000*JoltReaction::RecoveryDuration(recoveryFrom, recoveryPose, JoltReaction::PartCount)));
	if (recoveryTime > 3000) return false;
	prepareStart = 0;
	recoveryLift = recoveryPose[0].matrix[2][3] / MetresPerUnit - pelvis[2];
	fallYaw = bestYaw;
	VectorCopy(savedMins, ent->mins); VectorCopy(savedMaxs, ent->maxs);
	G_SetOrigin(ent, bestOrigin); VectorCopy(bestOrigin, ent->client->ps.origin);
	ent->s.groundEntityNum = ent->client->ps.groundEntityNum = ground;
	VectorCopy(bestOrigin, lastOrigin);
	VectorClear(ent->client->ps.velocity);
	recoverStart = level.time;
	gi.linkentity(ent);
	if (debug->integer) gi.Printf("Jolt: grounded recovery actor=%d clip=%d pelvis_lift=%.2f blend_ms=%d\n", actor, recoveryAnim, recoveryLift, recoveryTime);
	return true;
}

void Actor::FinishRecovery(gentity_t* ent) {
	trace_t space;
	gi.trace(&space, ent->currentOrigin, savedMins, savedMaxs, ent->currentOrigin, actor, MASK_NPCSOLID, (EG2_Collision)0, 0);
	if (space.startsolid || space.allsolid) { CancelRecovery(ent); return; }
	if (recoveryAnim < 0) {
		ClearPhysicalBones(ent);
		engaged = false; recoverStart = settledSince = 0;
		fall->Follow(reference, 0); simulation->Reset();
		VectorCopy(savedMins, ent->mins); VectorCopy(savedMaxs, ent->maxs); gi.linkentity(ent);
		return;
	}
	// The sampled target had no procedural or per-bone animation overrides. Match it
	// exactly, including Motion and old pelvis turn clips, before releasing physics.
	for (size_t i = 0; i < ent->ghoul2[0].mBlist.size(); ++i) {
		if (ent->ghoul2[0].mBlist[i].boneNumber < 0) continue;
		ent->ghoul2[0].mBlist[i].flags &= ~BONE_ANIM_TOTAL;
		gi.G2API_SetBoneAnglesMatrixIndex(&ent->ghoul2[0], int(i), identityAngles, BONE_ANGLES_POSTMULT, nullptr, 0, level.time);
	}
	vec3_t angles = {0, fallYaw, 0};
	G_SetAngles(ent, angles); SetClientViewAngle(ent, angles);
	ent->client->renderInfo.legsYaw = ent->NPC->desiredYaw = fallYaw;
	ent->client->ps.legsAnimTimer = ent->client->ps.torsoAnimTimer = 0;
	NPC_SetAnim(ent, SETANIM_BOTH, recoveryAnim, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD | SETANIM_FLAG_RESTART, 0);
	const auto& clip = level.knownAnimFileSets[ent->client->clientInfo.animFileIndex].animations[recoveryAnim];
	SetPoseClip(ent->ghoul2, clip, false, rigBones[1]);
	handoffError = 0;
	for (int i = 0; i < JoltReaction::PartCount; ++i) {
		mdxaBone_t actual;
		gi.G2API_GetBoltMatrix(ent->ghoul2, 0, bolts[i], &actual, angles, ent->currentOrigin, level.time, nullptr, ent->s.modelScale);
		vec3_t delta;
		for (int r = 0; r < 3; ++r) delta[r] = actual.matrix[r][3]-recoveryPose[i].matrix[r][3]/MetresPerUnit;
		handoffError = std::max(handoffError, VectorLength(delta));
	}
	fall.reset(); engaged = false; recoverStart = 0; simulation->Reset(); instability = 0;
	riseStart = level.time;
	VectorCopy(savedMins, ent->mins); VectorCopy(savedMaxs, ent->maxs); gi.linkentity(ent);
}

void Actor::CancelRecovery(gentity_t* ent) {
	if (recoverStart && fall) {
		// Resume at the displayed handoff pose, including its limb velocities.
		for (int frame = 0; frame < 2; ++frame) {
			JoltReaction::Transform pose[JoltReaction::PartCount];
			std::copy(recoveryPose, recoveryPose+JoltReaction::PartCount, pose);
			const float alpha = (level.time-recoverStart-(frame ? 0 : 1000.0f/120))/recoveryTime;
			if (recoveryAnim < 0) JoltReaction::BlendTransforms(recoveryFrom, pose, JoltReaction::PartCount, alpha);
			else JoltReaction::BlendRecovery(recoveryFrom, pose, JoltReaction::PartCount, alpha);
			JoltReaction::Part parts[JoltReaction::PartCount];
			for (int i = 0; i < JoltReaction::PartCount; ++i) {
				parts[i] = reference[i]; parts[i].bone = pose[i];
				float local[3] = {};
				for (int c = 0; c < 3; ++c) for (int r = 0; r < 3; ++r)
					local[c] += reference[i].bone.matrix[r][c]*(reference[i].end[r]-reference[i].bone.matrix[r][3]);
				for (int r = 0; r < 3; ++r) {
					parts[i].end[r] = pose[i].matrix[r][3];
					for (int c = 0; c < 3; ++c) parts[i].end[r] += pose[i].matrix[r][c]*local[c];
				}
			}
			fall->Follow(parts, frame ? 1.0f/120 : 0);
		}
		fall->Engage(); fall->Sample(fallPose);
	}
	if (prepareStart || recoverStart) {
		fall->ReleaseControl(); prepareStart = recoverStart = 0; nextRecoverAttempt = level.time+250;
	}
}
void Actor::Die(gentity_t* ent) {
	if (dead || !engaged || !fall) return;
	CancelRecovery(ent);
	dead = true; riseStart = 0;
	fall->Kill(true);
	fall->Sample(fallPose);
	Render(ent, level.time, ent->currentOrigin, ent->currentAngles, false);
}
void Actor::StoreCorpse() {
	auto* ent = &g_entities[actor];
	if (!ent->inuse || !ent->client || !fall || !engaged) return;
	fall->Sample(fallPose);
	Render(ent, level.time, ent->currentOrigin, ent->currentAngles, false);
	ent->client->renderInfo.legsYaw = ent->currentAngles[YAW] = fallYaw;
	fall->RootVelocity(ent->client->ps.velocity);
	VectorScale(ent->client->ps.velocity, 1/MetresPerUnit, ent->client->ps.velocity);
}

bool Humanoid(const gentity_t* ent) {
	return ent && ent->inuse && ent->s.number > 0 && ent->client && ent->NPC && ent->playerModel == 0 && ent->ghoul2.size() &&
		G_StandardHumanoid(const_cast<gentity_t*>(ent));
}
bool Eligible(const gentity_t* ent) {
	return Humanoid(ent) && ent->health > 0 && !ent->client->dismembered;
}
bool SavedCorpse(const gentity_t* ent) {
	if (!ent || !ent->inuse || !ent->client || !ent->NPC || ent->health > 0 || !ent->ghoul2.size()) return false;
	for (const auto& bone : ent->ghoul2[0].mBlist) if (bone.flags & BONE_ANGLES_PHYSICS) return true;
	return false;
}
bool ExternalPoseOwner(gentity_t* ent, bool allowGrip) {
	return (in_camera && ent->health > 0) || (ent->flags & (FL_NO_ANGLES | FL_DISINTEGRATED)) || ent->s.weapon == WP_EMPLACED_GUN || ent->client->ps.saberLockTime > level.time ||
		ent->client->ps.pullAttackTime > level.time || ent->client->ps.ikStatus || ent->client->ps.heldByBolt || G_IsRidingVehicle(ent) ||
		(ent->s.weapon == WP_THERMAL && ent->client->fireDelay > 0) ||
		((ent->client->ps.eFlags & EF_FORCE_GRIPPED) && ent->health > 0 && !allowGrip && !G_JoltGripping(ent)) ||
		(ent->client->ps.eFlags & (EF_FORCE_DRAINED | EF_HELD_BY_RANCOR | EF_HELD_BY_WAMPA)) ||
		ent->next_roff_time > level.time || Q3_TaskIDPending(ent, TID_ANIM_BOTH) ||
		Q3_TaskIDPending(ent, TID_ANIM_UPPER) || Q3_TaskIDPending(ent, TID_ANIM_LOWER);
}
bool AnimationOwnsPose(gentity_t* ent) {
	return ExternalPoseOwner(ent) || ent->client->ps.groundEntityNum == ENTITYNUM_NONE ||
		PM_InKnockDown(&ent->client->ps) || PM_InGetUp(&ent->client->ps);
}
bool Actor::Active(gentity_t* ent) {
	return simulation && enabled && enabled->integer && ent && ent->s.number == actor &&
		Eligible(ent) && !suspended && !AnimationOwnsPose(ent);
}
bool BoltPosition(gentity_t* ent, int bolt, vec3_t position) {
	mdxaBone_t matrix;
	vec3_t angles = {0, ent->client->renderInfo.legsYaw, 0};
	if (bolt < 0 || !gi.G2API_GetBoltMatrix(ent->ghoul2, 0, bolt, &matrix, angles,
		ent->currentOrigin, level.time, nullptr, ent->s.modelScale)) return false;
	for (int i = 0; i < 3; ++i) {
		position[i] = matrix.matrix[i][3];
		if (!std::isfinite(position[i])) return false;
	}
	return true;
}
void LocalVector(gentity_t* ent, const vec3_t world, vec3_t local) {
	const float yaw = ent->client->renderInfo.legsYaw * (M_PI / 180);
	local[0] = world[0] * cosf(yaw) + world[1] * sinf(yaw);
	local[1] = -world[0] * sinf(yaw) + world[1] * cosf(yaw);
	local[2] = world[2];
}
void Actor::Reset(bool restoreOrigin) {
	if (engaged && fall && actor > 0 && g_entities[actor].inuse && g_entities[actor].client) {
		auto* ent = &g_entities[actor];
		if (gripLevel > 1 && !dead && !ExternalPoseOwner(ent, true)) {
			vec3_t angles = {0, fall->FacingYaw(), 0};
			G_SetAngles(ent, angles); SetClientViewAngle(ent, angles);
			ent->client->renderInfo.legsYaw = angles[YAW];
			if (ent->NPC) ent->NPC->desiredYaw = angles[YAW];
		}
		ClearPhysicalBones(ent);
		VectorCopy(savedMins, ent->mins); VectorCopy(savedMaxs, ent->maxs);
		if (restoreOrigin && !recoverStart && ent->health > 0) { G_SetOrigin(ent, safeOrigin); VectorCopy(safeOrigin, ent->client->ps.origin); }
		gi.linkentity(ent);
	}
	fall.reset();
	engaged = false;
	sleepingPose = false;
	gripLevel = 0; gripCaster = -1;
	lightningCaster = -1; lightningContactStart = 0;
	recoverStart = prepareStart = riseStart = fallStart = 0; instability = launchSpeed = poseError = fallImpactSpeed = 0; lastHit = -10000;
	fallDamageApplied = false;
	fallMicroseconds = 0; fallSteps = 0;
	simulation.reset();
	pelvisBolt = -1;
	hits = poses = 0;
	peak = 0;
	stepMicroseconds = 0;
	measuredSteps = 0;
	suspended = false;
}
void Actor::Frame() {
	if (!simulation) return;
	gentity_t* ent = &g_entities[actor];
	if (!ent->inuse || !ent->client) { Reset(false); return; }
	if (!dead && ent->health <= 0 && engaged) Die(ent);
	if (fall && lightningCaster >= 0 && (!g_entities[lightningCaster].inuse || !g_entities[lightningCaster].client ||
		!(g_entities[lightningCaster].client->ps.forcePowersActive & (1 << FP_LIGHTNING)))) {
		fall->EndElectrocution(); lightningCaster = -1;
	}
	if (gripLevel && (!(ent->client->ps.eFlags & EF_FORCE_GRIPPED) || gripCaster < 0 || !g_entities[gripCaster].inuse ||
		!g_entities[gripCaster].client || g_entities[gripCaster].client->ps.forceGripEntityNum != actor ||
		!(g_entities[gripCaster].client->ps.forcePowersActive & (1 << FP_GRIP)))) G_JoltEndGrip(ent, 0);
	if (!enabled->integer && !dead) {
		if (gripLevel) {
			fall->RootVelocity(ent->client->ps.velocity);
			VectorScale(ent->client->ps.velocity, 1/MetresPerUnit, ent->client->ps.velocity);
			Reset(false);
		} else Reset(true);
		return;
	}
	if (!(dead ? Humanoid(ent) : Eligible(ent))) { Reset(false); return; }
	if (ent->health <= 0 && !dead) Die(ent);
	const int elapsed = level.time - lastTime;
	lastTime = level.time;
	if (fall) {
		UpdateRig(ent, elapsed * .001f);
		return;
	}
	instability = std::max(0.0f, instability - std::max(0, elapsed) * .0005f);
	const bool yield = AnimationOwnsPose(ent);
	if (yield || suspended || DistanceSquared(lastOrigin, ent->currentOrigin) > 128 * 128) simulation->Reset();
	suspended = yield;
	VectorCopy(ent->currentOrigin, lastOrigin);
	if (!suspended) {
		const auto start = std::chrono::steady_clock::now();
		const unsigned before = simulation->Steps();
		if (!simulation->Advance(elapsed * 0.001f) && debug->integer) gi.Printf("Jolt reaction: reset after time discontinuity or solver error\n");
		stepMicroseconds += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
		measuredSteps += simulation->Steps() - before;
	}
	const auto pose = simulation->Sample(1);
	for (const auto& joint : pose.angles) for (float angle : joint) peak = std::max(peak, fabsf(angle));
	if (debug->integer) {
		vec3_t a, b; VectorCopy(ent->currentOrigin, a); a[2] += ent->maxs[2] + 12;
		VectorCopy(a, b); a[0] -= 8; b[0] += 8; G_DebugLine(a, b, 150, 0x00ff00, qtrue);
	}
}
void Actor::Hit(gentity_t* ent, const float* direction, const float* point, int damage, int mod, int hitLoc, const gentity_t* attacker, float lightningKnockback) {
	if (!ent || ent->s.number != actor || !simulation || !enabled->integer || !Humanoid(ent) || !direction || !point || damage <= 0 ||
		(!Projectile(mod) && !G_JoltExplosion(mod) && mod != MOD_FORCE_LIGHTNING)) return;
	const bool blast = G_JoltExplosion(mod);
	const bool lightning = mod == MOD_FORCE_LIGHTNING;
	const bool jediPoise = lightning && ent->health > 0 && ent->NPC && ent->s.weapon == WP_SABER;
	if (dead && fall && !fall->Awake() && !BodyRoom(this)) return;
	const int part = hitLoc == HL_HEAD ? 2 : hitLoc == HL_ARM_LT || hitLoc == HL_HAND_LT ? 3 :
		hitLoc == HL_ARM_RT || hitLoc == HL_HAND_RT ? 5 : hitLoc == HL_LEG_LT || hitLoc == HL_FOOT_LT ? 7 :
		hitLoc == HL_LEG_RT || hitLoc == HL_FOOT_RT ? 9 : 1;
	if (!engaged && (ExternalPoseOwner(ent) || (ent->health > 0 && !blast && !lightning && !Active(ent)))) return;
	if (recoverStart) CancelRecovery(ent);
	if (prepareStart) { prepareStart = 0; fall->ReleaseControl(); nextRecoverAttempt = level.time+500; }
	if (lightning) {
		const int caster = attacker && attacker->client && (attacker->client->ps.forcePowersActive & (1 << FP_LIGHTNING)) ? attacker->s.number : -1;
		if (lastHitMod != mod || lightningCaster != caster || level.time-lastHit > 250) lightningContactStart = level.time;
		lightningCaster = caster;
	}
	lastHit = level.time; lastHitMod = mod;
	if (jediPoise) {
		const int poiseTime = 300 + ent->client->ps.forcePowerLevel[FP_SABER_DEFENSE] * 100 + ent->NPC->rank * 25;
		if (level.time-lightningContactStart < poiseTime) { ++hits; return; }
	}
	if (!reactionPose->integer && !engaged) { ++hits; return; }
	if ((ent->s.weapon != WP_SABER || engaged || blast || lightning || ent->health <= 0) && PrepareRig(ent)) {
		Engage(ent);
		fall->SetVitality(float(std::max(0, ent->health)) / std::max(1, ent->client->ps.stats[STAT_MAX_HEALTH]));
		if (lightning) {
			const int power = attacker && attacker->client ? std::max(1, std::min(3, attacker->client->ps.forcePowerLevel[FP_LIGHTNING])) : 1;
			float acceleration = 0;
			vec3_t horizontal = {direction[0], direction[1], 0};
			if (lightningKnockback > 0 && attacker && VectorNormalize(horizontal) > 0) {
				// The scale is the fraction of a nominal Force Push velocity added per second.
				const float knockback = std::max(100.0f, 200-Distance(ent->currentOrigin, attacker->currentOrigin))/(power == 1 ? 3 : 1);
				const float mass = ent->physicsBounce > 0 ? ent->physicsBounce : 200;
				acceleration = knockback*g_knockback->value/mass*(g_gravity->value > 0 ? .8f : 1)*MetresPerUnit*
					std::max(0.0f, std::min(1.0f, lightningPushScale->value));
				// Partially resisted hits retain a reduced shove.
				acceleration *= std::min(1.0f, lightningKnockback/(power == 3 ? 4.0f : 2.0f));
			}
			float intensity = std::min(1.0f, .45f+.18f*(power-1)+std::min(.15f, damage*.025f));
			if (jediPoise) {
				const float resistance = std::min(.45f, ent->client->ps.forcePowerLevel[FP_SABER_DEFENSE] * .1f + ent->NPC->rank * .025f);
				intensity *= 1-resistance;
			}
			fall->Electrocute(intensity, horizontal, acceleration);
		} else if (blast) {
			// Native damage already supplied distance-scaled knockback. Do not add it twice.
			if (!dead && (damage >= 5 || fall->Speed() > 1.5f)) { fall->ReleaseControl(); fallStart = level.time; }
			VectorClear(ent->client->ps.velocity);
		} else {
			vec3_t hit; VectorScale(point, MetresPerUnit, hit);
			if (dead) fall->Impulse(part, direction, hit, std::min(2.0f, damage*.08f));
			else {
				const float injury = part >= 7 ? std::min(.99f, .8f + damage*.01f) : std::min(.75f, damage*.025f);
				fall->React(part, direction, hit, std::min(3.0f, damage*.12f), injury);
			}
		}
		++hits;
		return;
	}
	vec3_t pivot, relative, localPoint, localDirection;
	if (!BoltPosition(ent, pelvisBolt, pivot)) return;
	VectorSubtract(point, pivot, relative);
	LocalVector(ent, relative, localPoint);
	VectorScale(localPoint, MetresPerUnit, localPoint);
	LocalVector(ent, direction, localDirection);
	// A bounded lever arm prevents distant or malformed hit points from driving the rig.
	for (int i = 0; i < 2; ++i) localPoint[i] = std::max(-0.25f, std::min(0.25f, localPoint[i]));
	localPoint[2] = std::max(0.0f, std::min(dimensions.torsoLength + dimensions.headLength, localPoint[2]));
	const bool head = hitLoc == HL_HEAD;
	simulation->Impulse(head ? 1 : 0, localDirection, localPoint, std::min(8.0f, damage * 0.65f));
	lastHit = level.time;
	lastHitMod = mod;
	++hits;
}
void Actor::BoneAngles(gentity_t* ent, int bone, int time, float* angles) {
	if (!Active(ent) || engaged || (reactionPose && !reactionPose->integer)) return;
	const int part = bone == ent->lowerLumbarBone ? 0 : bone == ent->cervicalBone ? 1 : -1;
	if (part < 0) return;
	const auto pose = simulation->Sample((PresentationTime(time) - lastTime) * 0.001f);
	for (int i = 0; i < 3; ++i) angles[i] += pose.angles[part][i];
	++poses;
}
bool Actor::Initialize(gentity_t* ent) {
	auto choose = [&](const char* primary, const char* alternate, const char* third = nullptr) {
		if (gi.G2API_AddBolt(&ent->ghoul2[0], primary) >= 0) return primary;
		if (!third || gi.G2API_AddBolt(&ent->ghoul2[0], alternate) >= 0) return alternate;
		return third;
	};
	rigBones[1] = choose("lower_lumbar", "thoracic");
	rigBones[2] = choose("cervical", "upper_lumbar");
	rigidHelmet = !Q_stricmp(rigBones[2], "upper_lumbar");
	rigBones[7] = choose("lfemurYZ", "lfemur", "Left_Thigh_Bone");
	rigBones[9] = choose("rfemurYZ", "rfemur", "Right_Thigh_Bone");
	rigEnds[0] = rigBones[1]; rigEnds[1] = rigBones[2];
	rigEnds[2] = choose("cranium", "*head_eyes");
	rigEnds[4] = choose("lhand", "*l_hand"); rigEnds[6] = choose("rhand", "*r_hand");
	JoltReaction::Part validated[JoltReaction::PartCount];
	if (!ReadParts(ent, validated, ent->ghoul2)) return false;
	pelvisBolt = bolts[1];
	const int neck = bolts[2];
	const int head = endBolts[2];
	vec3_t pelvisPosition, neckPosition, headPosition;
	if (!BoltPosition(ent, pelvisBolt, pelvisPosition) || !BoltPosition(ent, neck, neckPosition) ||
		!BoltPosition(ent, head, headPosition)) { if (debug->integer) gi.Printf("Jolt: missing humanoid rig landmarks\n"); return false; }
	dimensions.torsoLength = Distance(pelvisPosition, neckPosition) * MetresPerUnit;
	dimensions.headLength = Distance(neckPosition, headPosition) * MetresPerUnit * (rigidHelmet ? 1 : 2);
	dimensions.torsoRadius = std::max(.06f, std::min(.18f, dimensions.torsoLength*.4f));
	dimensions.headRadius = dimensions.headLength * 0.40f;
	if (dimensions.torsoLength < 0.15f || dimensions.torsoLength > 0.8f ||
		dimensions.headLength < 0.04f || dimensions.headLength > (rigidHelmet ? .8f : .4f) ||
		dimensions.torsoRadius < 0.05f || dimensions.torsoRadius > 0.3f) {
		if (debug->integer) gi.Printf("Jolt: rig dimensions outside prototype limits (torso %.3f head %.3f radius %.3f)\n",
			dimensions.torsoLength, dimensions.headLength, dimensions.torsoRadius); return false;
	}
	simulation.reset(new JoltReaction::Simulation(dimensions));
	lastTime = level.time;
	VectorCopy(ent->currentOrigin, lastOrigin);
	return true;
}
void Actor::Status() {
	const auto pose = simulation ? simulation->Sample(1) : JoltReaction::Pose{};
	const auto balance = fall ? fall->Balance() : JoltReaction::BalanceStatus{};
	gi.Printf("jolt actor=%d active=%d hits=%d poses=%d steps=%u peak=%.3f torso=%.3f,%.3f,%.3f head=%.3f,%.3f,%.3f health=%d us_per_step=%.2f falling=%d recovering=%d speed=%.2f instability=%.2f launch=%.1f pose_error=%.3f pelvis_z=%.2f painanim=%d movement=%.1f fall_steps=%u fall_us=%.2f tracked=%zu recovery_clip=%d recovery_lift=%.2f engaged=%d phase=%d corrections=%u strength=%.3f balance_error=%.3f target_change=%.2f\n",
		actor, actor >= 0 && (engaged ? balance.phase != JoltReaction::ControlPhase::Falling && !recoverStart && !prepareStart : Active(&g_entities[actor])), hits, poses, fall ? fall->Steps() : simulation ? simulation->Steps() : 0, peak,
		pose.angles[0][0], pose.angles[0][1], pose.angles[0][2], pose.angles[1][0], pose.angles[1][1], pose.angles[1][2],
		actor >= 0 ? g_entities[actor].health : 0, measuredSteps ? stepMicroseconds / measuredSteps : 0,
		engaged && balance.phase == JoltReaction::ControlPhase::Falling && !recoverStart, recoverStart != 0 || prepareStart != 0, fall ? fall->Speed() : 0, instability, launchSpeed, poseError,
		fall ? fallPose[0].matrix[2][3] / MetresPerUnit : 0, actor >= 0 && PM_PainAnim(g_entities[actor].client->ps.torsoAnim),
		actor >= 0 ? VectorLength(g_entities[actor].client->ps.velocity) : 0, fallSteps, fallSteps ? fallMicroseconds / fallSteps : 0,
		actors.size(), recoveryAnim, recoveryLift, engaged, int(balance.phase), balance.corrections, balance.strength, balance.error, balance.targetChange);
	if (fall) gi.Printf("jolt support contacts=%u landings=%u foot_error=%.3f peak_error=%.3f rejected_steps=%u assist_force=%.2f assist_torque=%.2f peak_leg_lift=%.3f\n", balance.contacts, balance.landings, balance.footError, balance.peakError, balance.rejectedSteps, balance.assistForce, balance.assistTorque, balance.peakLegLift);
	gi.Printf("jolt recovery preparing=%d blend_ms=%d brace_mask=%u hand_contacts=%u hand_contacts_seen=%u arm_error=%.3f passive_torque=%.3f\n", prepareStart != 0, recoveryTime, balance.braceMask, balance.handContacts, balance.handContactsSeen, balance.preparationError, balance.passiveTorque);
	gi.Printf("jolt ownership corpse=%d sleeping=%d active_bodies=%d body_limit=%d handoff_error=%.3f rise_start=%d\n", dead, dead && fall && !fall->Awake(), ActiveBodies(), bodyBudget ? std::max(1, std::min(16, bodyBudget->integer)) : 10, handoffError, riseStart);
	gi.Printf("jolt effects grip=%d grip_force=%.2f grip_error=%.3f legs_passive=%d shock=%.3f grip_struggles=%u shock_push=%.3f shock_rate=%.3f caster_grip=%d caster_force=%d\n", gripLevel, balance.gripForce, balance.gripError, balance.passiveLegs, balance.shock, balance.gripStruggles, balance.shockPushUsed, balance.shockPushRate,
		g_entities[0].client->ps.forceGripEntityNum, g_entities[0].client->ps.forcePower);
	gi.Printf("jolt facing facing_yaw=%.2f yaw_error=%.2f yaw_speed=%.2f torque=%.2f leg_tone=%.3f\n", fall ? fall->FacingYaw() : 0, balance.gripYawError, balance.gripYawSpeed, balance.gripTorque, balance.gripLegTone);
	if (actor > 0) {
		const auto& ent = g_entities[actor];
		const auto& player = g_entities[0];
		gi.Printf("jolt combat grounded=%d origin=%.2f,%.2f,%.2f player_origin=%.2f,%.2f,%.2f finisher=%d player_weapon=%d player_enemy=%d\n",
			G_JoltOnGround(&ent), ent.currentOrigin[0], ent.currentOrigin[1], ent.currentOrigin[2],
			player.currentOrigin[0], player.currentOrigin[1], player.currentOrigin[2], PM_StabDownAnim(player.client->ps.torsoAnim),
			player.client->ps.weapon, player.enemy ? player.enemy->s.number : -1);
	}
}
void Actor::HitCommand() {
	if (actor < 0 || (!Active(&g_entities[actor]) && !engaged)) { gi.Printf("Select an active humanoid first\n"); return; }
	gentity_t* ent = &g_entities[actor];
	vec3_t pivot, direction, point, angles = {0, ent->client->renderInfo.legsYaw, 0};
	if (!BoltPosition(ent, pelvisBolt, pivot)) return;
	const char* side = gi.argc() > 1 ? gi.argv(1) : "front";
	const int damage = gi.argc() > 2 ? atoi(gi.argv(2)) : 5;
	if (damage < 1 || damage > 50) { gi.Printf("jolt_hit damage must be 1..50\n"); return; }
	if (!Q_stricmp(side, "left")) angles[YAW] += 90;
	else if (!Q_stricmp(side, "right")) angles[YAW] -= 90;
	else if (!Q_stricmp(side, "back")) angles[YAW] += 180;
	else if (Q_stricmp(side, "front") && Q_stricmp(side, "head") && Q_stricmp(side, "legleft") && Q_stricmp(side, "legright")) {
		gi.Printf("jolt_hit front|back|left|right|head|legleft|legright\n"); return;
	}
	AngleVectors(angles, direction, nullptr, nullptr);
	const bool head = !Q_stricmp(side, "head");
	VectorMA(pivot, -4, direction, point);
	point[2] += (dimensions.torsoLength * (head ? 1.2f : 0.75f)) / MetresPerUnit;
	const int leg = !Q_stricmp(side, "legleft") ? 7 : !Q_stricmp(side, "legright") ? 9 : -1;
	if (leg >= 0 && fall) for (int r = 0; r < 3; ++r) point[r] = (engaged ? fallPose[leg] : reference[leg].bone).matrix[r][3]/MetresPerUnit;
	G_Damage(ent, &g_entities[0], &g_entities[0], direction, point, damage,
		DAMAGE_NO_ARMOR | DAMAGE_NO_KNOCKBACK, MOD_BLASTER, head ? HL_HEAD : leg == 7 ? HL_LEG_LT : leg == 9 ? HL_LEG_RT : HL_CHEST);
}

void Actor::KnockdownCommand() {
	if (actor < 0 || (!Active(&g_entities[actor]) && !engaged) || recoverStart) return;
	gentity_t* ent = &g_entities[actor];
	vec3_t direction;
	AngleVectors(ent->client->ps.viewangles, direction, nullptr, nullptr);
	G_Knockdown(ent, &g_entities[0], direction, 400, qtrue);
}

bool Actor::Render(gentity_t* ent, int time, const float* origin, float* angles, bool display) {
	if (!G_JoltOwns(ent)) return false;
	angles[0] = angles[2] = 0;
	angles[1] = fallYaw;
	if (dead && sleepingPose && !debug->integer && !fall->Awake() && VectorCompare(origin, lastOrigin)) return true;
	sleepingPose = false;
	JoltReaction::Transform pose[JoltReaction::PartCount];
	const int poseTime = display ? PresentationTime(time) : lastTime;
	if (recoverStart) {
		std::copy(recoveryPose, recoveryPose + JoltReaction::PartCount, pose);
		if (recoveryAnim < 0) JoltReaction::BlendTransforms(recoveryFrom, pose, JoltReaction::PartCount, float(time-recoverStart)/recoveryTime);
		else JoltReaction::BlendRecovery(recoveryFrom, pose, JoltReaction::PartCount, float(time-recoverStart)/recoveryTime);
	} else fall->Sample(pose, (poseTime - lastTime) * .001f);
	const float c = cosf(angles[1] * M_PI / 180), s = sinf(angles[1] * M_PI / 180);
	for (int i = 0; i < JoltReaction::PartCount; ++i) {
		mdxaBone_t matrix;
		for (int r = 0; r < 3; ++r) for (int col = 0; col < 4; ++col) {
			float v = pose[i].matrix[r][col] * (col == 3 ? 1 / MetresPerUnit : 1);
			matrix.matrix[r][col] = v - (col == 3 ? origin[r] : 0);
		}
		for (int col = 0; col < 4; ++col) {
			float x = matrix.matrix[0][col], y = matrix.matrix[1][col];
			matrix.matrix[0][col] = c*x+s*y; matrix.matrix[1][col] = -s*x+c*y;
		}
		for (int r = 0; r < 3; ++r) if (ent->s.modelScale[r]) matrix.matrix[r][3] /= ent->s.modelScale[r];
		gi.G2API_SetBoneAnglesMatrix(&ent->ghoul2[0], rigBones[i], matrix, BONE_ANGLES_PHYSICS, nullptr, 0, time);
		++poses;
		if (display && debug && debug->integer && i) {
			refEntity_t line = {};
			line.reType = RT_LINE; line.renderfx = RF_DEPTHHACK; line.radius = .75f;
			line.customShader = cgs.media.whiteShader;
			line.shaderRGBA[1] = line.shaderRGBA[3] = 255;
			for (int r = 0; r < 3; ++r) { line.origin[r] = pose[reference[i].parent].matrix[r][3] / MetresPerUnit; line.oldorigin[r] = pose[i].matrix[r][3] / MetresPerUnit; }
			cgi_R_AddRefEntityToScene(&line);
		}
	}
	ClearBounds(displayedMins, displayedMaxs);
	for (int i = 0; i < JoltReaction::PartCount; ++i) {
		mdxaBone_t end;
		gi.G2API_GetBoltMatrix(ent->ghoul2, 0, endBolts[i], &end, angles, origin, time, nullptr, ent->s.modelScale);
		vec3_t point;
		for (int r = 0; r < 3; ++r) point[r] = end.matrix[r][3];
		AddPointToBounds(point, displayedMins, displayedMaxs);
		for (int r = 0; r < 3; ++r) point[r] = pose[i].matrix[r][3] / MetresPerUnit;
		AddPointToBounds(point, displayedMins, displayedMaxs);
	}
	if (debug && debug->integer && !recoverStart) {
		poseError = 0;
		for (int i = 0; i < JoltReaction::PartCount; ++i) {
			mdxaBone_t actual;
			gi.G2API_GetBoltMatrix(ent->ghoul2, 0, bolts[i], &actual, angles, origin, time, nullptr, ent->s.modelScale);
			vec3_t delta;
			for (int r = 0; r < 3; ++r) delta[r] = actual.matrix[r][3] - pose[i].matrix[r][3] / MetresPerUnit;
			poseError = std::max(poseError, VectorLength(delta));
		}
	}
	if (dead && !display && !fall->Awake() && !(debug && debug->integer)) sleepingPose = true;
	return true;
}

void Actor::ImpulseCommand() {
	if (actor < 0 || (!Active(&g_entities[actor]) && !fall) || recoverStart) return;
	auto* ent = &g_entities[actor];
	vec3_t direction, point, angles = {0, ent->client->renderInfo.legsYaw, 0};
	if (gi.argc() > 1 && !Q_stricmp(gi.argv(1), "left")) angles[YAW] += 90;
	if (gi.argc() > 1 && !Q_stricmp(gi.argv(1), "right")) angles[YAW] -= 90;
	AngleVectors(angles, direction, nullptr, nullptr);
	VectorCopy(ent->currentOrigin, point); point[2] += 24;
	G_JoltHit(ent, direction, point, 15, MOD_BLASTER, HL_CHEST);
}

void Actor::ControlCommand() {
	if (actor < 0 || (!Active(&g_entities[actor]) && !engaged) || G_JoltBlocksAI(&g_entities[actor])) return;
	auto* ent = &g_entities[actor];
	ent->NPC->controlledTime = level.time + 30000;
	G_SetViewEntity(&g_entities[0], ent);
}

Actor* Find(int number) {
	auto found = actors.find(number);
	return found != actors.end() ? found->second.get() : nullptr;
}
Actor* Acquire(gentity_t* ent, bool dying = false, bool allowGrip = false) {
	Settings();
	if (!enabled->integer || !(dying ? Humanoid(ent) : Eligible(ent))) return nullptr;
	if (auto* state = Find(ent->s.number)) return state;
	if (retryRigAfter[ent->s.number] > level.time) return nullptr;
	if (actors.size() >= MaxActors || ExternalPoseOwner(ent, allowGrip) || (!dying && AnimationOwnsPose(ent))) return nullptr;
	std::unique_ptr<Actor> state(new Actor(ent->s.number));
	if (!state->Initialize(ent)) { retryRigAfter[ent->s.number] = level.time+1000; return nullptr; }
	Actor* result = state.get();
	actors.emplace(ent->s.number, std::move(state));
	return result;
}
bool DemoGround() {
	const vec3_t mins = {-16,-16,-24}, maxs = {16,16,40};
	const int offsets[][2] = {{0,0},{-1,-1},{-1,0},{-1,1},{0,-1},{0,1},{1,-1},{1,0},{1,1}};
	float best = 1e30f;
	for (int x = -768; x <= 768; x += 64) for (int y = -768; y <= 768; y += 64) {
		const float distance = float(x*x+y*y);
		if (distance >= best) continue;
		vec3_t center = {}; bool clear = true;
		for (const auto& offset : offsets) {
			vec3_t start = {5504.0f+x+offset[0]*160, -4520.0f+y+offset[1]*160, 256}, end = {start[0],start[1],-256};
			trace_t trace;
			gi.trace(&trace, start, mins, maxs, end, 0, MASK_NPCSOLID, (EG2_Collision)0, 0);
			if (trace.startsolid || trace.allsolid || trace.fraction == 1 || trace.plane.normal[2] < .99f) { clear = false; break; }
			if (!offset[0] && !offset[1]) VectorCopy(trace.endpos, center);
			else if (fabsf(trace.endpos[2]-center[2]) > 2) { clear = false; break; }
			VectorCopy(trace.endpos, end);
			gi.trace(&trace, center, mins, maxs, end, 0, MASK_NPCSOLID, (EG2_Collision)0, 0);
			if (trace.startsolid || trace.allsolid || trace.fraction < 1) { clear = false; break; }
		}
		if (clear) { best = distance; VectorCopy(center, demoOrigin); }
	}
	return best < 1e30f;
}
void StartDemo(int which) {
	Settings(); gi.cvar_set("g_joltReactions", "1");
	if (in_camera) {
		demoStage = 5; demoCase = which; demoDue = 0;
		if (!gi.Cvar_VariableIntegerValue("g_skippingcin")) gi.SendConsoleCommand("exitview\n");
		return;
	}
	G_ClearViewEntity(&g_entities[0]);
	if (auto* ent = G_Find(nullptr, FOFS(targetname), "jolt_demo_actor")) G_FreeEntity(ent);
	if (!DemoGround()) { gi.Printf("Jolt demo failed: no clear test area\n"); demoStage = 0; return; }
	demoCase = which; demoStage = 1; demoDue = level.time + 2000;
	gi.SendConsoleCommand(va("-forward; give weaponnum 3; give ammo; set d_npcfreeze 1; set cg_thirdPerson 0; set cg_drawGun 0; setviewpos %.1f %.1f %.1f 0; wait 10; weapon 3; npc spawn stormtrooper jolt_demo_actor\n", demoOrigin[0]-64, demoOrigin[1], demoOrigin[2]+40));
	gi.Printf("Jolt demo: %s at %.0f %.0f %.0f\n", demoCases[which], demoOrigin[0],demoOrigin[1],demoOrigin[2]);
	gi.SendServerCommand(0, "cp \"Jolt: %s\nF5 hit  F6 step  F7 leg  F8 run  F9 fall\nF10 reset  F11 slow  F12 stop\"", demoCases[which]);
}
void DemoFrame() {
	if (!demoStage || level.time < demoDue) return;
	if (demoStage == 5) { if (!in_camera) StartDemo(demoCase); return; }
	auto* ent = G_Find(nullptr, FOFS(targetname), "jolt_demo_actor");
	if (!Eligible(ent)) { gi.Printf("Jolt demo failed: actor unavailable; use jolt_demo reset\n"); demoStage = 0; return; }
	auto* state = Acquire(ent);
	if (!state) { gi.Printf("Jolt demo failed: no reaction record available\n"); demoStage = 0; return; }
	if (demoStage == 1) {
		ent->health = ent->client->ps.stats[STAT_HEALTH] = 500;
		selectedActor = ent->s.number;
		if (!state->PrepareRig(ent)) { gi.Printf("Jolt demo failed: no physical rig available\n"); demoStage = 0; return; }
		if (demoCase != 4) {
			NPC_SetAnim(ent, SETANIM_BOTH, BOTH_STAND3, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD | SETANIM_FLAG_RESTART);
			const auto& clip = level.knownAnimFileSets[ent->client->clientInfo.animFileIndex].animations[BOTH_STAND3];
			SetPoseClip(ent->ghoul2, clip, true);
		}
		gi.SendConsoleCommand(va("setviewpos %.1f %.1f %.1f 90\n", demoOrigin[0]+(demoCase == 4 ? 80 : 0), demoOrigin[1]-(demoCase == 4 ? 192 : 128), demoOrigin[2]+25));
		demoStage = 6; demoDue = level.time + 500;
	} else if (demoStage == 6) {
		if (demoCase != 4) {
			for (size_t i = 0; i < ent->ghoul2[0].mBlist.size(); ++i) if (ent->ghoul2[0].mBlist[i].boneNumber >= 0)
				gi.G2API_SetBoneAnglesMatrixIndex(&ent->ghoul2[0], int(i), identityAngles, BONE_ANGLES_POSTMULT, nullptr, 0, level.time);
			state->Engage(ent);
		}
		demoStage = 2; demoDue = level.time + 1000;
	} else if (demoStage == 2) {
		gi.SendServerCommand(0, "cp \"\"");
		if (demoCase == 1) gi.SendConsoleCommand("jolt_hit front\n");
		if (demoCase == 2) gi.SendConsoleCommand(va("jolt_push front %.2f\n", gi.cvar("g_joltDemoPush", "1.3", CVAR_CHEAT)->value));
		if (demoCase == 3) gi.SendConsoleCommand("jolt_hit legleft 15\n");
		if (demoCase == 5) state->KnockdownCommand();
		if (demoCase == 4) {
			gi.cvar_set("d_npcfreeze", "0");
			state->ControlCommand();
			gi.SendConsoleCommand("set cg_thirdPerson 1; set cg_thirdPersonRange 160; set cg_thirdPersonAngle 30; +forward\n");
		}
		demoStage = demoCase == 4 ? 3 : 4;
		demoDue = level.time + (demoCase == 4 ? 400 : demoCase >= 3 ? 12000 : 7000);
	} else if (demoStage == 3) {
		gi.SendConsoleCommand("jolt_hit legleft 15; -forward; exitview; set cg_thirdPerson 0\n");
		demoStage = 4; demoDue = level.time + 12000;
	} else {
		state->Status();
		gi.Printf("Jolt demo finished: %s\n", demoCases[demoCase]);
		if (demoCycle) StartDemo(demoCase == 5 ? 1 : demoCase + 1);
		else demoStage = 0;
	}
}
} // namespace

void G_JoltReset() {
	if (demoStage) gi.SendConsoleCommand("-forward\n");
	demoStage = 0; demoCycle = false;
	for (auto& entry : actors) if (entry.second->dead) { entry.second->StoreCorpse(); entry.second->engaged = false; }
	actors.clear(); retryRigAfter.clear(); selectedActor = -1;
	collision.clear(); collisionScene.reset(); colliderTransforms.clear();
	enabledColliders.clear(); changedColliders.clear(); movingColliders.clear(); colliderUpdateTime = -1; collisionLoaded = false;
}
void G_JoltForget(const gentity_t* ent) {
	if (!ent) return;
	if (auto* state = Find(ent->s.number)) state->Reset(false);
	actors.erase(ent->s.number);
	retryRigAfter.erase(ent->s.number);
	if (selectedActor == ent->s.number) selectedActor = -1;
}
void G_JoltFrame() {
	Settings();
	if (enabled->integer && ActiveBodies() < std::max(1, std::min(16, bodyBudget->integer))) {
		gentity_t* nearest = nullptr; float distance = 512*512;
		for (int i = 1; i < globals.num_entities; ++i) if (Eligible(&g_entities[i]) && !AnimationOwnsPose(&g_entities[i]) && (!Find(i) || !Find(i)->fall)) {
			const float d = DistanceSquared(g_entities[0].currentOrigin, g_entities[i].currentOrigin);
			if (d < distance && gi.inPVS(g_entities[0].currentOrigin, g_entities[i].currentOrigin)) { nearest = &g_entities[i]; distance = d; }
		}
		if (nearest) if (auto* state = Acquire(nearest)) state->PrepareRig(nearest);
	}
	for (auto it = actors.begin(); it != actors.end();) {
		auto& state = *it->second;
		state.Frame();
		if (!state.simulation || (!state.fall && it->first != selectedActor && level.time - state.lastHit > 6000)) {
			if (selectedActor == it->first) selectedActor = -1;
			it = actors.erase(it);
		} else ++it;
	}
	DemoFrame();
}
void G_JoltBeginFrame() {
	Settings();
	// Corpse poses use the existing Ghoul2 save data; no solver pointers are saved.
	for (int i = 1; i < globals.num_entities; ++i) if (SavedCorpse(&g_entities[i]) && !Find(i) && actors.size() < MaxActors) {
		auto* ent = &g_entities[i];
		std::unique_ptr<Actor> state(new Actor(i));
		state->dead = true; state->fallYaw = ent->currentAngles[YAW];
		if (state->Initialize(ent) && state->PrepareRig(ent)) {
			state->fall->Engage(); state->fall->Kill(); state->engaged = true;
			state->fall->Sample(state->fallPose);
			VectorCopy(ent->currentOrigin, state->lastOrigin);
			actors.emplace(i, std::move(state));
		}
	}
	for (auto& entry : actors) if (entry.second->engaged) {
		auto* ent = &g_entities[entry.first];
		if (entry.second->dead ? Humanoid(ent) : Eligible(ent)) {
			auto& state = *entry.second;
			state.Render(ent, level.time, ent->currentOrigin, ent->currentAngles, false);
			if (state.fall->Balance().phase == JoltReaction::ControlPhase::Tracking) {
				vec3_t mins, maxs; state.fall->Bounds(mins, maxs);
				VectorCopy(state.savedMins, ent->mins); VectorCopy(state.savedMaxs, ent->maxs);
				ent->mins[2] = mins[2]/MetresPerUnit - ent->currentOrigin[2] + .2f;
				gi.linkentity(ent);
			}
		}
	}
}
bool G_JoltLightningTarget(gentity_t* ent) {
	Settings();
	return enabled->integer && reactionPose->integer && Humanoid(ent) && !ent->client->dismembered && !ExternalPoseOwner(ent);
}
void G_JoltHit(gentity_t* ent, const float* direction, const float* point, int damage, int mod, int hitLoc, const gentity_t* attacker, float lightningKnockback) {
	if (damage <= 0 || (!Projectile(mod) && !G_JoltExplosion(mod) && mod != MOD_FORCE_LIGHTNING) || !direction || !point) return;
	for (int i = 0; i < 3; ++i) if (!std::isfinite(direction[i]) || !std::isfinite(point[i])) return;
	if (VectorLengthSquared(direction) < .0001f) return;
	if (auto* state = Acquire(ent, ent && (ent->health <= 0 || G_JoltExplosion(mod) || mod == MOD_FORCE_LIGHTNING))) state->Hit(ent, direction, point, damage, mod, hitLoc, attacker, lightningKnockback);
}
bool G_JoltExplosion(int mod) {
	switch (mod) {
	case MOD_THERMAL: case MOD_THERMAL_ALT: case MOD_ROCKET: case MOD_ROCKET_ALT:
	case MOD_EXPLOSIVE: case MOD_EXPLOSIVE_SPLASH: case MOD_DETPACK: case MOD_LASERTRIP: case MOD_LASERTRIP_ALT:
	case MOD_REPEATER_ALT: case MOD_FLECHETTE_ALT: case MOD_CONC: return true;
	default: return false;
	}
}
bool G_JoltGripping(const gentity_t* ent) {
	const auto* state = ent ? Find(ent->s.number) : nullptr;
	return state && state->engaged && state->gripLevel > 0;
}
bool G_JoltSupported(const gentity_t* ent) {
	const auto* state = ent ? Find(ent->s.number) : nullptr;
	return state && state->engaged && state->fall && (state->gripLevel == 1 || state->fall->Balance().contacts || state->fall->Balance().supportedTrunk);
}
bool G_JoltGrip(gentity_t* ent, int caster, const float* target, const float* head, int powerLevel) {
	if (!Humanoid(ent) || ent->client->dismembered || ExternalPoseOwner(ent, true) || caster < 0 || caster >= ENTITYNUM_WORLD || powerLevel < 1 || powerLevel > 3) return false;
	for (int r = 0; r < 3; ++r) if (!std::isfinite(target[r]) || !std::isfinite(head[r])) return false;
	auto* state = Acquire(ent, true, true);
	if (!state || !state->PrepareRig(ent)) return false;
	if (ent->health <= 0 && !state->dead) { state->Engage(ent); state->Die(ent); }
	if (state->dead && !state->fall->Awake() && powerLevel > 1 && !BodyRoom(state)) return false;
	if (!state->gripLevel && !state->dead) {
		const int file = ent->client->clientInfo.animFileIndex;
		if (file < 0 || file >= level.numKnownAnimFileSets) return false;
		const int anim = ent->s.weapon == WP_NONE || ent->s.weapon == WP_MELEE ? BOTH_CHOKE1 : BOTH_CHOKE3;
		auto clip = level.knownAnimFileSets[file].animations[anim];
		if (clip.numFrames < 2 || clip.frameLerp <= 0) return false;
		clip.firstFrame += clip.numFrames/2; clip.numFrames = 1;
		PoseModel sample(ent);
		if (!sample.models.size() || !SetPoseClip(sample.models, clip, true, state->rigBones[1]) ||
			!state->ReadParts(ent, state->gripPose, sample.models)) return false;
		state->CancelRecovery(ent);
		state->Engage(ent);
	}
	state->gripLevel = powerLevel; state->gripCaster = caster;
	state->lastHit = level.time;
	JoltReaction::RegionalControl profile;
	profile.strength[int(JoltReaction::Region::Torso)] = .7f;
	profile.strength[int(JoltReaction::Region::Head)] = .6f;
	profile.strength[int(JoltReaction::Region::Legs)] = powerLevel > 1 ? 0 : 1;
	profile.strength[int(JoltReaction::Region::Feet)] = powerLevel > 1 ? 0 : 1;
	state->fall->SetRegionalControl(profile);
	state->fall->Sample(state->fallPose);
	vec3_t anchor;
	for (int r = 0; r < 3; ++r) anchor[r] = (target[r]-head[r])*MetresPerUnit+state->fallPose[2].matrix[r][3];
	vec3_t casterPosition; VectorScale(g_entities[caster].currentOrigin, MetresPerUnit, casterPosition);
	state->fall->Grip(state->dead ? state->reference : state->gripPose, anchor, powerLevel > 1, casterPosition);
	VectorClear(ent->client->ps.velocity);
	return true;
}
void G_JoltEndGrip(gentity_t* ent, int holdTime) {
	auto* state = ent ? Find(ent->s.number) : nullptr;
	if (!state || !state->gripLevel || !state->fall) return;
	state->fall->ReleaseGrip();
	state->fall->SetRegionalControl(JoltReaction::RegionalControl{});
	state->gripLevel = 0; state->gripCaster = -1;
	state->fallStart = level.time;
	state->nextRecoverAttempt = level.time+std::max(0, holdTime);
	vec3_t velocity; state->fall->RootVelocity(velocity);
	const float speed = VectorLength(velocity), limit = 500*MetresPerUnit;
	if (speed > limit) { VectorScale(velocity, limit/speed, velocity); state->fall->SetRootVelocity(velocity); }
	VectorClear(ent->client->ps.velocity);
}
bool G_JoltDead(const gentity_t* ent) {
	const auto* state = ent ? Find(ent->s.number) : nullptr;
	return (state && state->dead && state->engaged) || SavedCorpse(ent);
}
void G_JoltDeath(gentity_t* ent) {
	if (auto* state = ent ? Find(ent->s.number) : nullptr) if (state->engaged) state->Die(ent);
}
void G_JoltAfterDeath(gentity_t* ent) {
	if (auto* state = ent ? Find(ent->s.number) : nullptr) if (state->dead && ent->inuse) {
		if (ExternalPoseOwner(ent)) { state->Reset(false); return; }
		VectorClear(ent->client->ps.velocity);
		state->Render(ent, level.time, ent->currentOrigin, ent->currentAngles, false);
		state->UpdatePhysicalHull(ent);
	}
}
bool G_JoltKnockback(gentity_t* ent, const float* velocity) {
	auto* state = ent ? Find(ent->s.number) : nullptr;
	if (!state || !state->engaged || !state->fall) return false;
	if (state->dead && !state->fall->Awake() && !BodyRoom(state)) return true;
	state->CancelRecovery(ent);
	vec3_t impulse; VectorScale(velocity, MetresPerUnit, impulse);
	const float speed = VectorLength(impulse);
	if (speed > 8) VectorScale(impulse, 8/speed, impulse);
	state->fall->AddVelocity(impulse);
	return true;
}
void G_JoltBoneAngles(gentity_t* ent, int bone, int time, float* angles) {
	if (ent) if (auto* state = Find(ent->s.number)) state->BoneAngles(ent, bone, time, angles);
}
bool G_JoltOwns(const gentity_t* ent) {
	const auto* state = ent ? Find(ent->s.number) : nullptr;
	return (state && state->fall && state->engaged) || SavedCorpse(ent);
}
bool G_JoltOnGround(const gentity_t* ent) {
	const auto* state = ent ? Find(ent->s.number) : nullptr;
	if (!state || !state->engaged || !state->fall) return false;
	const auto b = state->fall->Balance();
	return b.supportedTrunk && (b.phase == JoltReaction::ControlPhase::Falling ||
		b.phase == JoltReaction::ControlPhase::Preparing || b.phase == JoltReaction::ControlPhase::Dead);
}
void G_JoltMovementTrace(trace_t* result, const vec3_t start, const vec3_t mins, const vec3_t maxs,
	const vec3_t end, int passEntityNum, int contentmask, EG2_Collision collision, int lod) {
	// Only movement ignores prone hulls. Weapon traces retain the full hit bounds.
	std::pair<gentity_t*, int> hidden[MaxActors];
	int count = 0;
	for (const auto& entry : actors) {
		auto* ent = &g_entities[entry.first];
		if ((contentmask & (CONTENTS_PLAYERCLIP | CONTENTS_MONSTERCLIP)) && !(contentmask & CONTENTS_CORPSE) &&
			ent->inuse && (ent->contents & CONTENTS_BODY) && G_JoltOnGround(ent)) {
			hidden[count++] = {ent, ent->contents};
			ent->contents &= ~CONTENTS_BODY;
		}
	}
	gi.trace(result, start, mins, maxs, end, passEntityNum, contentmask, collision, lod);
	for (int i = 0; i < count; ++i) hidden[i].first->contents = hidden[i].second;
}
bool G_JoltBlocksAI(const gentity_t* ent) {
	const auto* state = ent ? Find(ent->s.number) : nullptr;
	return state && !state->dead && state->engaged && (state->gripLevel || state->recoverStart || state->fall->Balance().phase != JoltReaction::ControlPhase::Tracking);
}
bool G_JoltPhysicsRoot(const gentity_t* ent) {
	const auto* state = ent ? Find(ent->s.number) : nullptr;
	return state && state->fall && state->engaged && !state->recoverStart;
}
bool G_JoltRender(gentity_t* ent, int time, const float* origin, float* angles) {
	if (ent) if (auto* state = Find(ent->s.number)) return state->Render(ent, time, origin, angles, true);
	if (SavedCorpse(ent)) { VectorSet(angles, 0, ent->currentAngles[YAW], 0); return true; }
	return false;
}
bool G_JoltKnockdown(gentity_t* ent, const float* direction, float strength, bool force) {
	if (force && (!Eligible(ent) || ExternalPoseOwner(ent))) return false;
	if (!force && G_JoltBlocksAI(ent)) return true;
	if (strength < 100) return false;
	auto* state = Acquire(ent, force);
	if (!state || (!force && !state->Active(ent) && !state->engaged)) return false;
	if (force && state->engaged) {
		state->CancelRecovery(ent);
		vec3_t velocity; VectorScale(ent->client->ps.velocity, MetresPerUnit, velocity);
		// Tracking velocity includes root motion. Other phases store only the new throw.
		if (state->fall->Balance().phase == JoltReaction::ControlPhase::Tracking)
			state->fall->SetRootVelocity(velocity);
		else state->fall->AddVelocity(velocity);
	}
	vec3_t point; VectorCopy(ent->currentOrigin, point); point[2] += 20;
	const bool inherited = ent->client->ps.pm_time > 0 && (ent->client->ps.pm_flags & PMF_TIME_KNOCKBACK);
	return state->StartFall(ent, direction, point, inherited || force ? 0 : std::min(12.0f, strength * .02f));
}
bool G_JoltSuppressPain(const gentity_t* ent, int mod) {
	const auto* state = ent ? Find(ent->s.number) : nullptr;
	return state && enabled->integer && state->lastHit == level.time && state->lastHitMod == mod;
}
void G_JoltBeforeSave() {
	for (auto it = actors.begin(); it != actors.end();) {
		if (it->second->dead) { it->second->StoreCorpse(); ++it; }
		else if (it->second->engaged) {
			if (selectedActor == it->first) selectedActor = -1;
			if (it->second->gripLevel) {
				auto* ent = &g_entities[it->first];
				it->second->fall->RootVelocity(ent->client->ps.velocity);
				VectorScale(ent->client->ps.velocity, 1/MetresPerUnit, ent->client->ps.velocity);
				it->second->Reset(false);
			}
			it = actors.erase(it);
		} else ++it;
	}
}
void G_JoltSelect_f() {
	Settings();
	if (!enabled->integer) { gi.Printf("Set g_joltReactions 1 first\n"); return; }
	if (gi.argc() == 2 && !Q_stricmp(gi.argv(1), "none")) { selectedActor = -1; return; }
	gentity_t* ent = nullptr;
	if (gi.argc() == 2 && !Q_stricmp(gi.argv(1), "nearest")) {
		float distance = 512 * 512;
		for (int i = 1; i < globals.num_entities; ++i) if (Eligible(&g_entities[i])) {
			const float next = DistanceSquared(g_entities[0].currentOrigin, g_entities[i].currentOrigin);
			if (next < distance) { ent = &g_entities[i]; distance = next; }
		}
	} else if (gi.argc() == 2) {
		ent = G_Find(nullptr, FOFS(targetname), gi.argv(1));
		if (ent && G_Find(ent, FOFS(targetname), gi.argv(1))) ent = nullptr;
	} else if (gi.argc() == 1 && g_entities[0].client) {
		vec3_t start, end, forward;
		VectorCopy(g_entities[0].client->renderInfo.eyePoint, start);
		AngleVectors(g_entities[0].client->ps.viewangles, forward, nullptr, nullptr);
		VectorMA(start, 2048, forward, end);
		trace_t trace;
		gi.trace(&trace, start, nullptr, nullptr, end, 0, MASK_SHOT, (EG2_Collision)0, 0);
		if (trace.entityNum > 0 && trace.entityNum < globals.num_entities) ent = &g_entities[trace.entityNum];
	}
	if (!Eligible(ent)) { gi.Printf("Aim at a supported humanoid and use jolt_select, or use jolt_select nearest\n"); return; }
	actors.erase(ent->s.number);
	if (auto* state = Acquire(ent, true)) {
		selectedActor = ent->s.number;
		state->PrepareRig(ent);
		gi.Printf("Jolt: selected humanoid %d; torso=%.3fm head=%.3fm radius=%.3fm\n", selectedActor,
			state->dimensions.torsoLength, state->dimensions.headLength, state->dimensions.torsoRadius);
	} else gi.Printf("Jolt: selection unavailable for %d (pose_owner=%d ground=%d records=%zu)\n", ent->s.number, ExternalPoseOwner(ent), ent->client->ps.groundEntityNum, actors.size());
}
void G_JoltStatus_f() {
	if (gi.argc() > 1 && Q_stricmp(gi.argv(1), "all")) {
		auto* ent = G_Find(nullptr, FOFS(targetname), gi.argv(1));
		if (ent && ent->client) {
			if (auto* state = Find(ent->s.number)) state->Status();
			else Actor(ent->s.number).Status();
			return;
		}
	}
	if (gi.argc() == 1) if (auto* state = Find(selectedActor)) { state->Status(); return; }
	if (actors.empty()) Actor(-1).Status();
	else for (auto& entry : actors) entry.second->Status();
}
void G_JoltHit_f() { if (auto* state = Find(selectedActor)) state->HitCommand(); }
void G_JoltImpulse_f() { if (auto* state = Find(selectedActor)) state->ImpulseCommand(); }
void G_JoltControl_f() { if (auto* state = Find(selectedActor)) state->ControlCommand(); }
void G_JoltKnockdown_f() { if (auto* state = Find(selectedActor)) state->KnockdownCommand(); }
void G_JoltBalance_f() {
	if (auto* state = Find(selectedActor)) {
		auto* ent = &g_entities[selectedActor];
		if (state->PrepareRig(ent)) {
			state->Engage(ent);
			state->lastHit = level.time + 1000*std::max(0, std::min(60, atoi(gi.argv(1))));
		}
	}
}

void G_JoltPush_f() {
	auto* state = Find(selectedActor);
	if (!state || state->recoverStart) return;
	auto* ent = &g_entities[selectedActor];
	const char* side = gi.argc() > 1 ? gi.argv(1) : "front";
	const float speed = gi.argc() > 2 ? atof(gi.argv(2)) : 1.0f;
	vec3_t angles = {0, ent->client->renderInfo.legsYaw, 0}, velocity;
	if (!Q_stricmp(side, "left")) angles[YAW] += 90;
	else if (!Q_stricmp(side, "right")) angles[YAW] -= 90;
	else if (!Q_stricmp(side, "back")) angles[YAW] += 180;
	else if (Q_stricmp(side, "front")) { gi.Printf("jolt_push front|back|left|right [metres/second: 0.1..2]\n"); return; }
	if (!std::isfinite(speed) || speed < .1f || speed > 2 || !state->PrepareRig(ent)) return;
	state->Engage(ent);
	AngleVectors(angles, velocity, nullptr, nullptr); VectorScale(velocity, speed, velocity);
	state->fall->AddVelocity(velocity);
	gi.Printf("Jolt push: velocity=%.2f,%.2f,%.2f\n", velocity[0], velocity[1], velocity[2]);
}

void G_JoltDemo_f() {
	const char* mode = gi.argc() > 1 ? gi.argv(1) : "all";
	if (!Q_stricmp(mode, "stop")) {
		demoStage = 0; demoCycle = false;
		gi.SendConsoleCommand("-forward; exitview; set d_npcfreeze 1; set cg_thirdPerson 0; set timescale 1\n");
		return;
	}
	if (Q_stricmp(level.mapname, "t1_sour")) { gi.Printf("Use exec jolt-demo.cfg to load the demonstration map\n"); return; }
	demoCycle = !Q_stricmp(mode, "all");
	if (demoCycle) { StartDemo(1); return; }
	if (!Q_stricmp(mode, "reset")) { StartDemo(0); return; }
	for (int i = 0; i < 6; ++i) if (!Q_stricmp(mode, demoCases[i])) { StartDemo(i); return; }
	gi.Printf("jolt_demo all|idle|hit|step|leg|run|fall|reset|stop\n");
}

void G_JoltShoot_f() {
	const char* name = gi.argv(1);
	auto* target = gi.argc() >= 2 ? G_Find(nullptr, FOFS(targetname), name) : nullptr;
	if (!target || !target->client || target->health <= 0 || target->s.number == 0 ||
		G_Find(target, FOFS(targetname), name) || gi.argc() > 3 || (gi.argc() == 3 && Q_stricmp(gi.argv(2), "alt"))) {
		gi.Printf("jolt_shoot <unique NPC targetname> [alt]\n"); return;
	}
	vec3_t start, point, direction;
	VectorCopy(g_entities[0].client->renderInfo.eyePoint, start);
	VectorCopy(target->currentOrigin, point); point[2] += (target->mins[2] + target->maxs[2]) * .5f;
	VectorSubtract(point, start, direction);
	if (VectorNormalize(direction) > 2048) return;
	WP_FireBlasterMissile(&g_entities[0], start, direction, gi.argc() == 3 ? qtrue : qfalse);
	gi.Printf("Jolt test projectile: target=%d alt=%d\n", target->s.number, gi.argc() == 3);
}
void G_JoltBlast_f() {
	auto* target = gi.argc() == 2 ? G_Find(nullptr, FOFS(targetname), gi.argv(1)) : nullptr;
	if (!target || !target->client || !target->NPC || G_Find(target, FOFS(targetname), gi.argv(1))) {
		gi.Printf("jolt_blast <unique NPC targetname>\n"); return;
	}
	vec3_t origin, forward, angles = {0, target->currentAngles[YAW], 0};
	AngleVectors(angles, forward, nullptr, nullptr);
	VectorMA(target->currentOrigin, -64, forward, origin); origin[2] += 32;
	auto* grenade = G_Spawn();
	if (!grenade) return;
	grenade->owner = &g_entities[0]; grenade->count = 1;
	G_SetOrigin(grenade, origin);
	thermalDetonatorExplode(grenade);
}
