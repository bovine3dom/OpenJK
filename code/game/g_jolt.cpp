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
extern qboolean PM_InGetUp(playerState_t* ps);
extern qboolean PM_PainAnim(int anim);
extern Vehicle_t* G_IsRidingVehicle(gentity_t* ent);
extern void G_Knockdown(gentity_t* self, gentity_t* attacker, const vec3_t direction, float strength, qboolean breakSaberLock);
extern void G_SetViewEntity(gentity_t* self, gentity_t* target);
extern qboolean G_ClearViewEntity(gentity_t* ent);
extern void WP_FireBlasterMissile(gentity_t* ent, vec3_t start, vec3_t direction, qboolean altFire);

namespace {
constexpr float MetresPerUnit = 0.0254f;
constexpr size_t MaxActors = 16;
constexpr int RecoveryBlendTime = 180;
const mdxaBone_t identityAngles = {{{1,0,0,0}, {0,1,0,0}, {0,0,1,0}}};
int selectedActor = -1, fallOwner = -1;
cvar_t* enabled = nullptr;
cvar_t* debug = nullptr;
cvar_t* reactionPose = nullptr;
std::map<int, std::vector<float>> collision;
bool collisionLoaded = false;
const char* bones[JoltReaction::PartCount] = {"pelvis", "lower_lumbar", "cervical", "lhumerus", "lradius", "rhumerus", "rradius", "lfemurYZ", "ltibia", "rfemurYZ", "rtibia"};
const char* ends[JoltReaction::PartCount] = {"lower_lumbar", "cervical", "cranium", "lradius", "lhand", "rradius", "rhand", "ltibia", "ltalus", "rtibia", "rtalus"};
constexpr int parents[JoltReaction::PartCount] = {-1,0,1,1,3,1,5,0,7,0,9};

void Settings() {
	if (!enabled) enabled = gi.cvar("g_joltReactions", "1", CVAR_ARCHIVE);
	if (!debug) debug = gi.cvar("g_joltDebug", "0", CVAR_CHEAT);
	if (!reactionPose) reactionPose = gi.cvar("g_joltReactionPose", "1", CVAR_CHEAT);
}
bool Projectile(int mod) {
	switch (mod) {
	case MOD_BLASTER: case MOD_BLASTER_ALT: case MOD_BRYAR: case MOD_BRYAR_ALT:
	case MOD_BOWCASTER: case MOD_BOWCASTER_ALT: case MOD_REPEATER:
	case MOD_FLECHETTE: case MOD_EMPLACED: case MOD_SEEKER: return true;
	default: return false;
	}
}
bool Eligible(const gentity_t* ent);
bool ExternalPoseOwner(gentity_t* ent);
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
	JoltReaction::Transform fallPose[JoltReaction::PartCount];
	JoltReaction::Transform recoveryPose[JoltReaction::PartCount];
	int recoveryAnim = BOTH_GETUP1, nextRecoverAttempt = 0;
	float recoveryLift = 0;
	int fallStart = 0, settledSince = 0, recoverStart = 0, lastHit = -10000;
	int lastHitMod = MOD_UNKNOWN;
	float fallYaw = 0, instability = 0, launchSpeed = 0;
	float poseError = 0;
	double fallMicroseconds = 0;
	unsigned fallSteps = 0;
	vec3_t safeOrigin, savedMins, savedMaxs;
	vec3_t displayedMins, displayedMaxs;
	explicit Actor(int number) : actor(number) {}
	~Actor() { Reset(true); }
	bool Initialize(gentity_t* ent);
	bool Active(gentity_t* ent);
	void Reset(bool restoreOrigin);
	void Frame();
	void Hit(gentity_t* ent, const float* direction, const float* point, int damage, int mod, int hitLoc);
	void BoneAngles(gentity_t* ent, int bone, int time, float* angles);
	bool Render(gentity_t* ent, int time, const float* origin, float* angles);
	void UpdatePhysicalHull(gentity_t* ent);
	void MoveColliders(float seconds);
	bool StartFall(gentity_t* ent, const float* direction, const float* point, float strength, int hitPart = 1);
	bool Recover(gentity_t* ent);
	void FinishRecovery(gentity_t* ent);
	void Status();
	void HitCommand();
	void KnockdownCommand();
	void ImpulseCommand();
	void ControlCommand();
};
std::map<int, std::unique_ptr<Actor>> actors;

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
	explicit PoseModel(gentity_t* ent) {
		gi.G2API_CopyGhoul2Instance(ent->ghoul2, models, -1);
		if (!models.size()) return;
		for (size_t i = 0; i < models[0].mBlist.size(); ++i) {
			if (models[0].mBlist[i].boneNumber < 0) continue;
			models[0].mBlist[i].flags &= ~BONE_ANIM_TOTAL;
			gi.G2API_SetBoneAnglesMatrixIndex(&models[0], int(i), identityAngles, BONE_ANGLES_POSTMULT, nullptr, 0, level.time);
		}
	}
	~PoseModel() { gi.G2API_CleanGhoul2Models(models); }
};
bool SetGetupClip(CGhoul2Info_v& models, const animation_t& clip, bool firstFrame) {
	const int end = firstFrame ? clip.firstFrame + 1 : clip.firstFrame + clip.numFrames;
	for (const char* bone : {"model_root", "lower_lumbar"})
		if (!gi.G2API_SetBoneAnim(&models[0], bone, clip.firstFrame, end, BONE_ANIM_OVERRIDE_FREEZE,
			50.0f / clip.frameLerp, level.time, float(clip.firstFrame), 0)) return false;
	return true;
}
void ExportSurface(int model, int count, const float* points, void*) {
	auto& mesh = collision[model];
	for (int i = 0; i < count * 3; ++i) mesh.push_back(points[i] * MetresPerUnit);
}
void Actor::MoveColliders(float seconds) {
	std::set<int> present;
	for (int i = 1; i < globals.num_entities; ++i) {
		const auto& ent = g_entities[i];
		if (!ent.inuse || !ent.bmodel || !(ent.contents & MASK_NPCSOLID) || !ent.model || ent.model[0] != '*') continue;
		present.insert(atoi(ent.model + 1));
		JoltReaction::Transform transform;
		vec3_t axis[3];
		AnglesToAxis(ent.currentAngles, axis);
		for (int r = 0; r < 3; ++r) {
			for (int c = 0; c < 3; ++c) transform.matrix[r][c] = axis[c][r];
			transform.matrix[r][3] = ent.currentOrigin[r] * MetresPerUnit;
		}
		fall->MoveMesh(atoi(ent.model + 1), transform, seconds);
	}
	for (const auto& mesh : collision) if (mesh.first) fall->SetMeshEnabled(mesh.first, present.count(mesh.first) != 0);
}
bool Actor::StartFall(gentity_t* ent, const float* direction, const float* point, float strength, int hitPart) {
	if (fall || fallOwner >= 0) return false;
	if (!collisionLoaded) {
		collisionLoaded = true;
		if (!gi.PhysicsSurfaces(MASK_NPCSOLID, ExportSurface, nullptr)) gi.Printf("Jolt: unsupported collision format; using small reactions\n");
	}
	if (collision.empty()) return false;
	JoltReaction::Part parts[JoltReaction::PartCount];
	const float masses[] = {12, 24, 5, 3, 2, 3, 2, 8, 4, 8, 4};
	const float radii[] = {.12f, .14f, .075f, .055f, .045f, .055f, .045f, .085f, .06f, .085f, .06f};
	vec3_t angles = {0, ent->client->renderInfo.legsYaw, 0}, velocity;
	for (int i = 0; i < JoltReaction::PartCount; ++i) {
		bolts[i] = gi.G2API_AddBolt(&ent->ghoul2[0], bones[i]);
		endBolts[i] = gi.G2API_AddBolt(&ent->ghoul2[0], ends[i]);
		mdxaBone_t bone, end;
		if (bolts[i] < 0 || endBolts[i] < 0 || !gi.G2API_GetBoltMatrix(ent->ghoul2, 0, bolts[i], &bone, angles, ent->currentOrigin, level.time, nullptr, ent->s.modelScale) ||
			!gi.G2API_GetBoltMatrix(ent->ghoul2, 0, endBolts[i], &end, angles, ent->currentOrigin, level.time, nullptr, ent->s.modelScale)) {
			gi.Printf("Jolt: missing segment %s -> %s\n", bones[i], ends[i]); return false;
		}
		for (int r = 0; r < 3; ++r) {
			for (int c = 0; c < 4; ++c) parts[i].bone.matrix[r][c] = bone.matrix[r][c] * (c == 3 ? MetresPerUnit : 1);
			parts[i].end[r] = (i == 2 ? 2 * end.matrix[r][3] - bone.matrix[r][3] : end.matrix[r][3]) * MetresPerUnit;
		}
		vec3_t delta;
		for (int r = 0; r < 3; ++r) delta[r] = parts[i].end[r] - parts[i].bone.matrix[r][3];
		const float length = VectorLength(delta);
		if (!std::isfinite(length) || length < 0.015f || length > 1) { gi.Printf("Jolt: invalid segment %s %.3f\n", bones[i], length); return false; }
		parts[i].radius = std::min(radii[i], length * .45f);
		parts[i].mass = masses[i];
		parts[i].parent = parents[i];
	}
	VectorScale(ent->client->ps.velocity, MetresPerUnit, velocity);
	launchSpeed = VectorLength(ent->client->ps.velocity);
	fall.reset(new JoltReaction::FallSimulation(parts, velocity, g_gravity->value * MetresPerUnit));
	for (const auto& mesh : collision) if (!fall->AddMesh(mesh.first, mesh.second.data(), int(mesh.second.size() / 3))) {
		gi.Printf("Jolt: collision model %d could not be built\n", mesh.first); fall.reset(); return false;
	}
	MoveColliders(0);
	fallOwner = actor;
	vec3_t hit;
	VectorScale(point, MetresPerUnit, hit);
	fall->Impulse(hitPart, direction, hit, strength);
	fall->Sample(fallPose, 1);
	VectorCopy(ent->currentOrigin, safeOrigin);
	VectorCopy(ent->currentOrigin, lastOrigin);
	VectorCopy(ent->mins, savedMins); VectorCopy(ent->maxs, savedMaxs);
	fallYaw = angles[YAW]; fallStart = lastTime = level.time;
	fallMicroseconds = 0; fallSteps = 0;
	settledSince = recoverStart = nextRecoverAttempt = 0;
	VectorClear(ent->client->ps.velocity);
	if (g_entities[0].client->ps.viewEntity == actor) G_ClearViewEntity(&g_entities[0]);
	gi.Printf("Jolt: physical fall actor=%d launch_speed=%.1f\n", actor, launchSpeed);
	return true;
}

bool Actor::Recover(gentity_t* ent) {
	nextRecoverAttempt = level.time + 500;
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
		if (clip.numFrames < 2 || clip.frameLerp <= 0 || !SetGetupClip(sample.models, clip, true)) continue;
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
			for (int col = 0; col < 4; ++col) {
				target[i].matrix[0][col] = c*local[i].matrix[0][col] - s*local[i].matrix[1][col];
				target[i].matrix[1][col] = s*local[i].matrix[0][col] + c*local[i].matrix[1][col];
				target[i].matrix[2][col] = local[i].matrix[2][col];
			}
			for (int r = 0; r < 3; ++r) {
				target[i].matrix[r][3] = (target[i].matrix[r][3] + trace.endpos[r]) * MetresPerUnit;
				const float delta = (target[i].matrix[r][3] - fallPose[i].matrix[r][3]) / MetresPerUnit;
				score += delta * delta;
				for (int col = 0; col < 3; ++col) {
					const float rotation = target[i].matrix[r][col] - fallPose[i].matrix[r][col];
					score += 64 * rotation * rotation;
				}
			}
		}
		if (score < best) {
			best = score; bestYaw = yaw * 180 / M_PI; recoveryAnim = anim;
			ground = trace.entityNum; VectorCopy(trace.endpos, bestOrigin);
			std::copy(target, target + JoltReaction::PartCount, recoveryPose);
		}
	}
	if (ground == ENTITYNUM_NONE) return false;
	recoveryLift = recoveryPose[0].matrix[2][3] / MetresPerUnit - pelvis[2];
	fallYaw = bestYaw;
	VectorCopy(savedMins, ent->mins); VectorCopy(savedMaxs, ent->maxs);
	G_SetOrigin(ent, bestOrigin); VectorCopy(bestOrigin, ent->client->ps.origin);
	ent->s.groundEntityNum = ent->client->ps.groundEntityNum = ground;
	VectorCopy(bestOrigin, lastOrigin);
	VectorClear(ent->client->ps.velocity);
	recoverStart = level.time;
	gi.linkentity(ent);
	gi.Printf("Jolt: grounded recovery actor=%d clip=%d pelvis_lift=%.2f\n", actor, recoveryAnim, recoveryLift);
	return true;
}

void Actor::FinishRecovery(gentity_t* ent) {
	ClearPhysicalBones(ent);
	vec3_t angles = {0, fallYaw, 0};
	G_SetAngles(ent, angles); SetClientViewAngle(ent, angles);
	ent->client->renderInfo.legsYaw = ent->NPC->desiredYaw = fallYaw;
	NPC_SetAnim(ent, SETANIM_BOTH, recoveryAnim, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD | SETANIM_FLAG_RESTART);
	const auto& clip = level.knownAnimFileSets[ent->client->clientInfo.animFileIndex].animations[recoveryAnim];
	SetGetupClip(ent->ghoul2, clip, false);
	fall.reset(); fallOwner = -1; recoverStart = 0; simulation->Reset(); instability = 0;
	VectorCopy(savedMins, ent->mins); VectorCopy(savedMaxs, ent->maxs); gi.linkentity(ent);
}

bool Eligible(const gentity_t* ent) {
	return ent && ent->inuse && ent->client && ent->NPC && ent->health > 0 &&
		ent->client->NPC_class == CLASS_STORMTROOPER && ent->ghoul2.size() &&
		!Q_stricmp(ent->ghoul2[0].mFileName, "models/players/stormtrooper/model.glm") &&
		ent->lowerLumbarBone >= 0 && ent->cervicalBone >= 0 && !ent->client->dismembered;
}
bool ExternalPoseOwner(gentity_t* ent) {
	return in_camera || (ent->flags & FL_NO_ANGLES) || ent->s.weapon == WP_SABER || ent->s.weapon == WP_EMPLACED_GUN ||
		ent->client->ps.ikStatus || ent->client->ps.heldByBolt || G_IsRidingVehicle(ent) ||
		(ent->s.weapon == WP_THERMAL && ent->client->fireDelay > 0) ||
		(ent->client->ps.eFlags & (EF_FORCE_GRIPPED | EF_FORCE_DRAINED | EF_HELD_BY_RANCOR | EF_HELD_BY_WAMPA)) ||
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
	if (fall && actor > 0 && g_entities[actor].inuse && g_entities[actor].client) {
		auto* ent = &g_entities[actor];
		ClearPhysicalBones(ent);
		VectorCopy(savedMins, ent->mins); VectorCopy(savedMaxs, ent->maxs);
		if (restoreOrigin && !recoverStart && ent->health > 0) { G_SetOrigin(ent, safeOrigin); VectorCopy(safeOrigin, ent->client->ps.origin); }
		gi.linkentity(ent);
	}
	if (fallOwner == actor) fallOwner = -1;
	fall.reset();
	recoverStart = fallStart = 0; instability = launchSpeed = poseError = 0; lastHit = -10000;
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
	if (!enabled->integer) { Reset(true); return; }
	if (!Eligible(ent)) { Reset(false); return; }
	const int elapsed = level.time - lastTime;
	lastTime = level.time;
	if (fall) {
		if (ExternalPoseOwner(ent) || (!recoverStart && DistanceSquared(ent->currentOrigin, lastOrigin) > 16 * 16)) { Reset(false); return; }
		if (recoverStart) {
			G_JoltRender(ent, level.time, ent->currentOrigin, ent->currentAngles);
			UpdatePhysicalHull(ent);
			if (level.time - recoverStart >= RecoveryBlendTime) FinishRecovery(ent);
			return;
		}
		MoveColliders(std::max(.001f, elapsed * .001f));
		vec3_t pushed;
		VectorScale(ent->client->ps.velocity, MetresPerUnit, pushed);
		fall->AddVelocity(pushed); VectorClear(ent->client->ps.velocity);
		const auto start = std::chrono::steady_clock::now();
		if (!fall->Advance(elapsed * .001f)) { Reset(true); return; }
		fallMicroseconds += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
		fallSteps = fall->Steps();
		fall->Sample(fallPose, 1);
		vec3_t origin;
		for (int r = 0; r < 3; ++r) origin[r] = fallPose[0].matrix[r][3] / MetresPerUnit;
		G_SetOrigin(ent, origin); VectorCopy(origin, ent->client->ps.origin);
		VectorCopy(origin, lastOrigin);
		G_JoltRender(ent, level.time, origin, ent->currentAngles);
		UpdatePhysicalHull(ent);
		if (fall->Speed() < .25f) { if (!settledSince) settledSince = level.time; }
		else settledSince = 0;
		if (settledSince && level.time - settledSince > 600 && level.time - fallStart > 1200 && level.time >= nextRecoverAttempt) Recover(ent);
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
		if (!simulation->Advance(elapsed * 0.001f)) gi.Printf("Jolt reaction: reset after time discontinuity or solver error\n");
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
void Actor::Hit(gentity_t* ent, const float* direction, const float* point, int damage, int mod, int hitLoc) {
	if (!ent || ent->s.number != actor || !simulation || !enabled->integer || !Eligible(ent) || !direction || !point || damage <= 0 ||
		!Projectile(mod)) return;
	const int part = hitLoc == HL_HEAD ? 2 : hitLoc == HL_ARM_LT || hitLoc == HL_HAND_LT ? 3 :
		hitLoc == HL_ARM_RT || hitLoc == HL_HAND_RT ? 5 : hitLoc == HL_LEG_LT || hitLoc == HL_FOOT_LT ? 7 :
		hitLoc == HL_LEG_RT || hitLoc == HL_FOOT_RT ? 9 : 1;
	if (fall) {
		if (!recoverStart) { vec3_t hit; VectorScale(point, MetresPerUnit, hit); fall->Impulse(part, direction, hit, std::min(30.0f, damage * 1.0f)); ++hits; }
		return;
	}
	if (!Active(ent)) return;
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
	instability += damage * .035f + (part >= 7 ? .3f : 0) + std::min(1.0f, VectorLength(ent->client->ps.velocity) / 200.0f) * .55f;
	if (instability >= 1) StartFall(ent, direction, point, std::min(65.0f, damage * 2.0f + 20), part);
	++hits;
}
void Actor::BoneAngles(gentity_t* ent, int bone, int time, float* angles) {
	if (!Active(ent) || fall || (reactionPose && !reactionPose->integer)) return;
	const int part = bone == ent->lowerLumbarBone ? 0 : bone == ent->cervicalBone ? 1 : -1;
	if (part < 0) return;
	const auto pose = simulation->Sample(std::max(0, time - level.time) * 0.001f);
	for (int i = 0; i < 3; ++i) angles[i] += pose.angles[part][i];
	++poses;
}
bool Actor::Initialize(gentity_t* ent) {
	pelvisBolt = gi.G2API_AddBolt(&ent->ghoul2[0], "lower_lumbar");
	const int neck = gi.G2API_AddBolt(&ent->ghoul2[0], "cervical");
	const int head = gi.G2API_AddBolt(&ent->ghoul2[0], "cranium");
	vec3_t pelvisPosition, neckPosition, headPosition;
	if (!BoltPosition(ent, pelvisBolt, pelvisPosition) || !BoltPosition(ent, neck, neckPosition) ||
		!BoltPosition(ent, head, headPosition)) { gi.Printf("Jolt: missing humanoid rig landmarks\n"); return false; }
	dimensions.torsoLength = Distance(pelvisPosition, neckPosition) * MetresPerUnit;
	dimensions.headLength = Distance(neckPosition, headPosition) * MetresPerUnit * 2;
	dimensions.torsoRadius = (ent->maxs[0] - ent->mins[0]) * MetresPerUnit * 0.20f;
	dimensions.headRadius = dimensions.headLength * 0.40f;
	if (dimensions.torsoLength < 0.15f || dimensions.torsoLength > 0.8f ||
		dimensions.headLength < 0.08f || dimensions.headLength > 0.4f ||
		dimensions.torsoRadius < 0.05f || dimensions.torsoRadius > 0.3f) {
		gi.Printf("Jolt: rig dimensions outside prototype limits (torso %.3f head %.3f radius %.3f)\n",
			dimensions.torsoLength, dimensions.headLength, dimensions.torsoRadius); return false;
	}
	simulation.reset(new JoltReaction::Simulation(dimensions));
	lastTime = level.time;
	VectorCopy(ent->currentOrigin, lastOrigin);
	return true;
}
void Actor::Status() {
	const auto pose = simulation ? simulation->Sample(1) : JoltReaction::Pose{};
	gi.Printf("jolt actor=%d active=%d hits=%d poses=%d steps=%u peak=%.3f torso=%.3f,%.3f,%.3f head=%.3f,%.3f,%.3f health=%d us_per_step=%.2f falling=%d recovering=%d speed=%.2f instability=%.2f launch=%.1f pose_error=%.3f pelvis_z=%.2f painanim=%d movement=%.1f fall_steps=%u fall_us=%.2f tracked=%zu recovery_clip=%d recovery_lift=%.2f\n",
		actor, actor >= 0 && Active(&g_entities[actor]) && !fall, hits, poses, simulation ? simulation->Steps() : 0, peak,
		pose.angles[0][0], pose.angles[0][1], pose.angles[0][2], pose.angles[1][0], pose.angles[1][1], pose.angles[1][2],
		actor >= 0 ? g_entities[actor].health : 0, measuredSteps ? stepMicroseconds / measuredSteps : 0,
		fall && !recoverStart, recoverStart != 0, fall ? fall->Speed() : 0, instability, launchSpeed, poseError,
		fall ? fallPose[0].matrix[2][3] / MetresPerUnit : 0, actor >= 0 && PM_PainAnim(g_entities[actor].client->ps.torsoAnim),
		actor >= 0 ? VectorLength(g_entities[actor].client->ps.velocity) : 0, fallSteps, fallSteps ? fallMicroseconds / fallSteps : 0,
		actors.size(), recoveryAnim, recoveryLift);
}
void Actor::HitCommand() {
	if (actor < 0 || !Active(&g_entities[actor])) { gi.Printf("Select an active stormtrooper first\n"); return; }
	gentity_t* ent = &g_entities[actor];
	vec3_t pivot, direction, point, angles = {0, ent->client->renderInfo.legsYaw, 0};
	if (!BoltPosition(ent, pelvisBolt, pivot)) return;
	const char* side = gi.argc() > 1 ? gi.argv(1) : "front";
	if (!Q_stricmp(side, "left")) angles[YAW] += 90;
	else if (!Q_stricmp(side, "right")) angles[YAW] -= 90;
	else if (!Q_stricmp(side, "back")) angles[YAW] += 180;
	else if (Q_stricmp(side, "front") && Q_stricmp(side, "head")) { gi.Printf("jolt_hit front|back|left|right|head\n"); return; }
	AngleVectors(angles, direction, nullptr, nullptr);
	const bool head = !Q_stricmp(side, "head");
	VectorMA(pivot, -4, direction, point);
	point[2] += (dimensions.torsoLength * (head ? 1.2f : 0.75f)) / MetresPerUnit;
	G_Damage(ent, &g_entities[0], &g_entities[0], direction, point, 5,
		DAMAGE_NO_ARMOR | DAMAGE_NO_KNOCKBACK, MOD_BLASTER, head ? HL_HEAD : HL_CHEST);
}

void Actor::KnockdownCommand() {
	if (actor < 0 || !Active(&g_entities[actor]) || fall) return;
	gentity_t* ent = &g_entities[actor];
	vec3_t direction;
	AngleVectors(ent->client->ps.viewangles, direction, nullptr, nullptr);
	G_Knockdown(ent, &g_entities[0], direction, 400, qtrue);
}

bool Actor::Render(gentity_t* ent, int time, const float* origin, float* angles) {
	if (!G_JoltOwns(ent)) return false;
	angles[0] = angles[2] = 0; angles[1] = fallYaw;
	JoltReaction::Transform pose[JoltReaction::PartCount];
	fall->Sample(pose, std::max(0, time - level.time) * .001f);
	if (recoverStart) {
		JoltReaction::Transform targets[JoltReaction::PartCount];
		std::copy(recoveryPose, recoveryPose + JoltReaction::PartCount, targets);
		JoltReaction::BlendTransforms(pose, targets, JoltReaction::PartCount, float(time - recoverStart) / RecoveryBlendTime);
		std::copy(targets, targets + JoltReaction::PartCount, pose);
	}
	const float c = cosf(fallYaw * M_PI / 180), s = sinf(fallYaw * M_PI / 180);
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
		gi.G2API_SetBoneAnglesMatrix(&ent->ghoul2[0], bones[i], matrix, BONE_ANGLES_PHYSICS, nullptr, 0, time);
		if (debug && debug->integer && i) {
			refEntity_t line = {};
			line.reType = RT_LINE; line.renderfx = RF_DEPTHHACK; line.radius = .75f;
			line.customShader = cgs.media.whiteShader;
			line.shaderRGBA[1] = line.shaderRGBA[3] = 255;
			for (int r = 0; r < 3; ++r) { line.origin[r] = pose[parents[i]].matrix[r][3] / MetresPerUnit; line.oldorigin[r] = pose[i].matrix[r][3] / MetresPerUnit; }
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
	if (actor < 0 || !Active(&g_entities[actor]) || fall) return;
	auto* ent = &g_entities[actor];
	ent->NPC->controlledTime = level.time + 30000;
	G_SetViewEntity(&g_entities[0], ent);
}

Actor* Find(int number) {
	auto found = actors.find(number);
	return found != actors.end() ? found->second.get() : nullptr;
}
Actor* Acquire(gentity_t* ent) {
	Settings();
	if (!enabled->integer || !Eligible(ent)) return nullptr;
	if (auto* state = Find(ent->s.number)) return state;
	if (actors.size() >= MaxActors || AnimationOwnsPose(ent)) return nullptr;
	std::unique_ptr<Actor> state(new Actor(ent->s.number));
	if (!state->Initialize(ent)) return nullptr;
	Actor* result = state.get();
	actors.emplace(ent->s.number, std::move(state));
	return result;
}
} // namespace

void G_JoltReset() {
	actors.clear(); selectedActor = fallOwner = -1;
	collision.clear(); collisionLoaded = false;
}
void G_JoltForget(const gentity_t* ent) {
	if (!ent) return;
	if (auto* state = Find(ent->s.number)) state->Reset(false);
	actors.erase(ent->s.number);
	if (selectedActor == ent->s.number) selectedActor = -1;
}
void G_JoltFrame() {
	Settings();
	if (!enabled->integer) { G_JoltReset(); return; }
	for (auto it = actors.begin(); it != actors.end();) {
		auto& state = *it->second;
		state.Frame();
		if (!state.simulation || (!state.fall && it->first != selectedActor && level.time - state.lastHit > 6000)) {
			if (selectedActor == it->first) selectedActor = -1;
			it = actors.erase(it);
		} else ++it;
	}
}
void G_JoltHit(gentity_t* ent, const float* direction, const float* point, int damage, int mod, int hitLoc) {
	if (damage <= 0 || !Projectile(mod) || !direction || !point) return;
	for (int i = 0; i < 3; ++i) if (!std::isfinite(direction[i]) || !std::isfinite(point[i])) return;
	if (VectorLengthSquared(direction) < .0001f) return;
	if (auto* state = Acquire(ent)) state->Hit(ent, direction, point, damage, mod, hitLoc);
}
void G_JoltBoneAngles(gentity_t* ent, int bone, int time, float* angles) {
	if (ent) if (auto* state = Find(ent->s.number)) state->BoneAngles(ent, bone, time, angles);
}
bool G_JoltOwns(const gentity_t* ent) {
	const auto* state = ent ? Find(ent->s.number) : nullptr;
	return state && state->fall;
}
bool G_JoltPhysicsRoot(const gentity_t* ent) {
	const auto* state = ent ? Find(ent->s.number) : nullptr;
	return state && state->fall && !state->recoverStart;
}
bool G_JoltRender(gentity_t* ent, int time, const float* origin, float* angles) {
	if (ent) if (auto* state = Find(ent->s.number)) return state->Render(ent, time, origin, angles);
	return false;
}
bool G_JoltKnockdown(gentity_t* ent, const float* direction, float strength) {
	if (G_JoltOwns(ent)) return true;
	if (strength < 100) return false;
	auto* state = Acquire(ent);
	if (!state || !state->Active(ent)) return false;
	vec3_t point; VectorCopy(ent->currentOrigin, point); point[2] += 20;
	return state->StartFall(ent, direction, point, std::min(80.0f, strength * .15f));
}
bool G_JoltSuppressPain(const gentity_t* ent, int mod) {
	const auto* state = ent ? Find(ent->s.number) : nullptr;
	return state && enabled->integer && state->lastHit == level.time && state->lastHitMod == mod;
}
void G_JoltBeforeSave() {
	if (fallOwner >= 0) {
		const int number = fallOwner;
		actors.erase(number);
		if (selectedActor == number) selectedActor = -1;
	}
}
void G_JoltSelect_f() {
	Settings();
	if (!enabled->integer) { gi.Printf("Set g_joltReactions 1 first\n"); return; }
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
	if (!Eligible(ent)) { gi.Printf("Aim at a stock stormtrooper and use jolt_select, or use jolt_select nearest\n"); return; }
	actors.erase(ent->s.number);
	if (auto* state = Acquire(ent)) {
		selectedActor = ent->s.number;
		gi.Printf("Jolt: selected stormtrooper %d; torso=%.3fm head=%.3fm radius=%.3fm\n", selectedActor,
			state->dimensions.torsoLength, state->dimensions.headLength, state->dimensions.torsoRadius);
	}
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

void G_JoltShoot_f() {
	const char* name = gi.argv(1);
	auto* target = gi.argc() >= 2 ? G_Find(nullptr, FOFS(targetname), name) : nullptr;
	if (!target || !target->client || target->health <= 0 || target->s.number == 0 ||
		G_Find(target, FOFS(targetname), name) || gi.argc() > 3 || (gi.argc() == 3 && Q_stricmp(gi.argv(2), "alt"))) {
		gi.Printf("jolt_shoot <unique NPC targetname> [alt]\n"); return;
	}
	vec3_t start, point, direction;
	VectorCopy(g_entities[0].client->renderInfo.eyePoint, start);
	VectorCopy(target->currentOrigin, point); point[2] += 12;
	VectorSubtract(point, start, direction);
	if (VectorNormalize(direction) > 2048) return;
	WP_FireBlasterMissile(&g_entities[0], start, direction, gi.argc() == 3 ? qtrue : qfalse);
	gi.Printf("Jolt test projectile: target=%d alt=%d\n", target->s.number, gi.argc() == 3);
}
