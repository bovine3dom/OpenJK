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

namespace {
constexpr float MetresPerUnit = 0.0254f;
std::unique_ptr<JoltReaction::Simulation> simulation;
JoltReaction::Rig dimensions;
cvar_t* enabled = nullptr;
int actor = -1, lastTime = 0, hits = 0, poses = 0;
int pelvisBolt = -1;
bool suspended = false;
float peak = 0;
double stepMicroseconds = 0;
unsigned measuredSteps = 0;
vec3_t lastOrigin;
std::unique_ptr<JoltReaction::FallSimulation> fall;
const char* bones[JoltReaction::PartCount] = {"pelvis", "lower_lumbar", "cervical", "lhumerus", "lradius", "rhumerus", "rradius", "lfemurYZ", "ltibia", "rfemurYZ", "rtibia"};
const char* ends[JoltReaction::PartCount] = {"lower_lumbar", "cervical", "cranium", "lradius", "lhand", "rradius", "rhand", "ltibia", "ltalus", "rtibia", "rtalus"};
constexpr int parents[JoltReaction::PartCount] = {-1,0,1,1,3,1,5,0,7,0,9};
int bolts[JoltReaction::PartCount], endBolts[JoltReaction::PartCount];
std::map<int, std::vector<float>> collision;
JoltReaction::Transform fallPose[JoltReaction::PartCount];
int fallStart = 0, settledSince = 0, recoverStart = 0, lastHit = -10000;
float fallYaw = 0, instability = 0, launchSpeed = 0;
float poseError = 0;
double fallMicroseconds = 0;
unsigned fallSteps = 0;
vec3_t safeOrigin, savedMins, savedMaxs;
cvar_t* debug = nullptr;
cvar_t* reactionPose = nullptr;
vec3_t displayedMins, displayedMaxs;

void UpdatePhysicalHull(gentity_t* ent) {
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
	const mdxaBone_t identity = {{{1,0,0,0}, {0,1,0,0}, {0,0,1,0}}};
	for (size_t i = 0; i < ent->ghoul2[0].mBlist.size(); ++i)
		if (ent->ghoul2[0].mBlist[i].flags & BONE_ANGLES_PHYSICS)
			gi.G2API_SetBoneAnglesMatrixIndex(&ent->ghoul2[0], int(i), identity, BONE_ANGLES_POSTMULT, nullptr, 0, level.time);
}
void ExportSurface(int model, int count, const float* points, void*) {
	auto& mesh = collision[model];
	for (int i = 0; i < count * 3; ++i) mesh.push_back(points[i] * MetresPerUnit);
}
void MoveColliders(float seconds) {
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
bool StartFall(gentity_t* ent, const float* direction, const float* point, float strength, int hitPart = 1) {
	if (fall || collision.empty()) return false;
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
	vec3_t hit;
	VectorScale(point, MetresPerUnit, hit);
	fall->Impulse(hitPart, direction, hit, strength);
	fall->Sample(fallPose, 1);
	VectorCopy(ent->currentOrigin, safeOrigin);
	VectorCopy(ent->currentOrigin, lastOrigin);
	VectorCopy(ent->mins, savedMins); VectorCopy(ent->maxs, savedMaxs);
	fallYaw = angles[YAW]; fallStart = lastTime = level.time;
	fallMicroseconds = 0; fallSteps = 0;
	settledSince = recoverStart = 0;
	VectorClear(ent->client->ps.velocity);
	if (g_entities[0].client->ps.viewEntity == actor) G_ClearViewEntity(&g_entities[0]);
	gi.Printf("Jolt: physical fall actor=%d launch_speed=%.1f\n", actor, launchSpeed);
	return true;
}

bool Recover(gentity_t* ent) {
	vec3_t pelvis, start, end;
	for (int i = 0; i < 3; ++i) pelvis[i] = fallPose[0].matrix[i][3] / MetresPerUnit;
	trace_t trace;
	bool found = false;
	const float offsets[][2] = {{0,0}, {32,0}, {-32,0}, {0,32}, {0,-32}, {32,32}, {-32,32}, {32,-32}, {-32,-32}};
	for (const auto& offset : offsets) {
		VectorCopy(pelvis, start); start[0] += offset[0]; start[1] += offset[1]; start[2] += 24;
		VectorCopy(start, end); end[2] -= 160;
		gi.trace(&trace, start, savedMins, savedMaxs, end, actor, MASK_NPCSOLID, (EG2_Collision)0, 0);
		if (trace.startsolid || trace.allsolid || trace.fraction == 1 || trace.plane.normal[2] < .7f) continue;
		trace_t path;
		const vec3_t smallMin = {-3,-3,-3}, smallMax = {3,3,3};
		// Keep the bottom of the path probe above the resting surface.
		vec3_t pathStart; VectorCopy(pelvis, pathStart); pathStart[2] += 4;
		gi.trace(&path, pathStart, smallMin, smallMax, trace.endpos, actor, MASK_NPCSOLID, (EG2_Collision)0, 0);
		if (!path.startsolid && path.fraction == 1) { found = true; break; }
	}
	if (!found) return false;
	VectorCopy(savedMins, ent->mins); VectorCopy(savedMaxs, ent->maxs);
	G_SetOrigin(ent, trace.endpos); VectorCopy(trace.endpos, ent->client->ps.origin);
	ent->s.groundEntityNum = ent->client->ps.groundEntityNum = trace.entityNum;
	VectorCopy(trace.endpos, lastOrigin);
	VectorClear(ent->client->ps.velocity);
	ClearPhysicalBones(ent);
	NPC_SetAnim(ent, SETANIM_BOTH, BOTH_GETUP1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
	recoverStart = level.time;
	gi.linkentity(ent);
	gi.Printf("Jolt: recovery actor=%d\n", actor);
	return true;
}

bool Eligible(const gentity_t* ent) {
	return ent && ent->inuse && ent->client && ent->NPC && ent->health > 0 &&
		ent->client->NPC_class == CLASS_STORMTROOPER && ent->NPC_type &&
		!Q_stricmp(ent->NPC_type, "stormtrooper") && ent->ghoul2.size() &&
		!Q_stricmp(ent->ghoul2[0].mFileName, "models/players/stormtrooper/model.glm") &&
		ent->lowerLumbarBone >= 0 && ent->cervicalBone >= 0 && !ent->client->dismembered;
}
bool ExternalPoseOwner(gentity_t* ent) {
	return in_camera || (ent->flags & FL_NO_ANGLES) || ent->s.weapon == WP_SABER || ent->s.weapon == WP_EMPLACED_GUN ||
		ent->client->ps.ikStatus || ent->client->ps.heldByBolt || G_IsRidingVehicle(ent) ||
		(ent->client->ps.eFlags & (EF_FORCE_GRIPPED | EF_FORCE_DRAINED | EF_HELD_BY_RANCOR | EF_HELD_BY_WAMPA)) ||
		ent->next_roff_time > level.time || Q3_TaskIDPending(ent, TID_ANIM_BOTH) ||
		Q3_TaskIDPending(ent, TID_ANIM_UPPER) || Q3_TaskIDPending(ent, TID_ANIM_LOWER);
}
bool AnimationOwnsPose(gentity_t* ent) {
	return ExternalPoseOwner(ent) || ent->client->ps.groundEntityNum == ENTITYNUM_NONE ||
		PM_InKnockDown(&ent->client->ps) || PM_InGetUp(&ent->client->ps);
}
bool Active(gentity_t* ent) {
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
}

static void Reset(bool restoreOrigin) {
	if (fall && actor > 0 && g_entities[actor].inuse && g_entities[actor].client) {
		auto* ent = &g_entities[actor];
		ClearPhysicalBones(ent);
		VectorCopy(savedMins, ent->mins); VectorCopy(savedMaxs, ent->maxs);
		if (restoreOrigin && !recoverStart && ent->health > 0) { G_SetOrigin(ent, safeOrigin); VectorCopy(safeOrigin, ent->client->ps.origin); }
		gi.linkentity(ent);
	}
	fall.reset(); collision.clear();
	recoverStart = fallStart = 0; instability = launchSpeed = poseError = 0; lastHit = -10000;
	fallMicroseconds = 0; fallSteps = 0;
	simulation.reset();
	actor = pelvisBolt = -1;
	hits = poses = 0;
	peak = 0;
	stepMicroseconds = 0;
	measuredSteps = 0;
	suspended = false;
}
void G_JoltReset() { Reset(true); }
void G_JoltForget(const gentity_t* ent) {
	if (ent && ent->s.number == actor) Reset(false);
}
void G_JoltFrame() {
	if (!enabled) enabled = gi.cvar("g_joltReactions", "0", CVAR_CHEAT);
	if (!debug) debug = gi.cvar("g_joltDebug", "0", CVAR_CHEAT);
	if (!reactionPose) reactionPose = gi.cvar("g_joltReactionPose", "1", CVAR_CHEAT);
	if (!simulation) return;
	gentity_t* ent = &g_entities[actor];
	if (!enabled->integer) { G_JoltReset(); return; }
	if (!Eligible(ent)) { Reset(false); return; }
	const int elapsed = level.time - lastTime;
	lastTime = level.time;
	if (fall) {
		if (ExternalPoseOwner(ent) || (!recoverStart && DistanceSquared(ent->currentOrigin, lastOrigin) > 16 * 16)) { Reset(false); return; }
		if (recoverStart) {
			G_JoltRender(ent, level.time, ent->currentOrigin, ent->currentAngles);
			UpdatePhysicalHull(ent);
			if (level.time - recoverStart > 900) {
				ClearPhysicalBones(ent); fall.reset(); recoverStart = 0; simulation->Reset(); instability = 0;
				VectorCopy(savedMins, ent->mins); VectorCopy(savedMaxs, ent->maxs); gi.linkentity(ent);
			}
			return;
		}
		MoveColliders(std::max(.001f, elapsed * .001f));
		vec3_t pushed;
		VectorScale(ent->client->ps.velocity, MetresPerUnit, pushed);
		fall->AddVelocity(pushed); VectorClear(ent->client->ps.velocity);
		const auto start = std::chrono::steady_clock::now();
		if (!fall->Advance(elapsed * .001f)) { G_JoltReset(); return; }
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
		if (settledSince && level.time - settledSince > 600 && level.time - fallStart > 1200) Recover(ent);
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
void G_JoltHit(gentity_t* ent, const float* direction, const float* point, int damage, int mod, int hitLoc) {
	if (!ent || ent->s.number != actor || !simulation || !enabled->integer || !Eligible(ent) || !direction || !point || damage <= 0 ||
		(mod != MOD_BLASTER && mod != MOD_BRYAR && mod != MOD_BRYAR_ALT)) return;
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
	instability += damage * .035f + (part >= 7 ? .3f : 0) + std::min(1.0f, VectorLength(ent->client->ps.velocity) / 200.0f) * .55f;
	if (instability >= 1) StartFall(ent, direction, point, std::min(65.0f, damage * 2.0f + 20), part);
	++hits;
}
void G_JoltBoneAngles(gentity_t* ent, int bone, int time, float* angles) {
	if (!Active(ent) || fall || (reactionPose && !reactionPose->integer)) return;
	const int part = bone == ent->lowerLumbarBone ? 0 : bone == ent->cervicalBone ? 1 : -1;
	if (part < 0) return;
	const auto pose = simulation->Sample(std::max(0, time - level.time) * 0.001f);
	for (int i = 0; i < 3; ++i) angles[i] += pose.angles[part][i];
	++poses;
}
void G_JoltSelect_f() {
	G_JoltReset();
	if (!enabled) enabled = gi.cvar("g_joltReactions", "0", CVAR_CHEAT);
	if (!enabled->integer) { gi.Printf("Set g_joltReactions 1 first\n"); return; }
	int selected = -1;
	if (gi.argc() == 2 && !Q_stricmp(gi.argv(1), "nearest")) {
		float distance = 512 * 512;
		for (int i = 1; i < globals.num_entities; ++i) {
			if (!Eligible(&g_entities[i])) continue;
			const float next = DistanceSquared(g_entities[0].currentOrigin, g_entities[i].currentOrigin);
			if (next < distance) { selected = i; distance = next; }
		}
	} else if (gi.argc() == 1 && g_entities[0].client) {
		vec3_t start, end, forward;
		VectorCopy(g_entities[0].client->renderInfo.eyePoint, start);
		AngleVectors(g_entities[0].client->ps.viewangles, forward, nullptr, nullptr);
		VectorMA(start, 2048, forward, end);
		trace_t trace;
		gi.trace(&trace, start, nullptr, nullptr, end, 0, MASK_SHOT, (EG2_Collision)0, 0);
		selected = trace.entityNum;
	}
	if (selected < 1 || selected >= globals.num_entities || !Eligible(&g_entities[selected])) {
		gi.Printf("Aim at a stock stormtrooper and use jolt_select, or use jolt_select nearest\n"); return;
	}
	gentity_t* ent = &g_entities[selected];
	pelvisBolt = gi.G2API_AddBolt(&ent->ghoul2[0], "lower_lumbar");
	const int neck = gi.G2API_AddBolt(&ent->ghoul2[0], "cervical");
	const int head = gi.G2API_AddBolt(&ent->ghoul2[0], "cranium");
	vec3_t pelvisPosition, neckPosition, headPosition;
	if (!BoltPosition(ent, pelvisBolt, pelvisPosition) || !BoltPosition(ent, neck, neckPosition) ||
		!BoltPosition(ent, head, headPosition)) { gi.Printf("Jolt: missing humanoid rig landmarks\n"); return; }
	dimensions.torsoLength = Distance(pelvisPosition, neckPosition) * MetresPerUnit;
	dimensions.headLength = Distance(neckPosition, headPosition) * MetresPerUnit * 2;
	dimensions.torsoRadius = (ent->maxs[0] - ent->mins[0]) * MetresPerUnit * 0.20f;
	dimensions.headRadius = dimensions.headLength * 0.40f;
	if (dimensions.torsoLength < 0.15f || dimensions.torsoLength > 0.8f ||
		dimensions.headLength < 0.08f || dimensions.headLength > 0.4f ||
		dimensions.torsoRadius < 0.05f || dimensions.torsoRadius > 0.3f) {
		gi.Printf("Jolt: rig dimensions outside prototype limits (torso %.3f head %.3f radius %.3f)\n",
			dimensions.torsoLength, dimensions.headLength, dimensions.torsoRadius); return;
	}
	simulation.reset(new JoltReaction::Simulation(dimensions));
	actor = selected;
	lastTime = level.time;
	VectorCopy(ent->currentOrigin, lastOrigin);
	if (!gi.PhysicsSurfaces(MASK_NPCSOLID, ExportSurface, nullptr)) gi.Printf("Jolt: this map's collision format does not support physical falls\n");
	gi.Printf("Jolt 5.3.0: selected stormtrooper %d; torso=%.3fm head=%.3fm radius=%.3fm; masses=28/5kg\n",
		actor, dimensions.torsoLength, dimensions.headLength, dimensions.torsoRadius);
}
void G_JoltStatus_f() {
	const auto pose = simulation ? simulation->Sample(1) : JoltReaction::Pose{};
	gi.Printf("jolt actor=%d active=%d hits=%d poses=%d steps=%u peak=%.3f torso=%.3f,%.3f,%.3f head=%.3f,%.3f,%.3f health=%d us_per_step=%.2f falling=%d recovering=%d speed=%.2f instability=%.2f launch=%.1f pose_error=%.3f pelvis_z=%.2f painanim=%d movement=%.1f fall_steps=%u fall_us=%.2f\n",
		actor, actor >= 0 && Active(&g_entities[actor]) && !fall, hits, poses, simulation ? simulation->Steps() : 0, peak,
		pose.angles[0][0], pose.angles[0][1], pose.angles[0][2], pose.angles[1][0], pose.angles[1][1], pose.angles[1][2],
		actor >= 0 ? g_entities[actor].health : 0, measuredSteps ? stepMicroseconds / measuredSteps : 0,
		fall && !recoverStart, recoverStart != 0, fall ? fall->Speed() : 0, instability, launchSpeed, poseError,
		fall ? fallPose[0].matrix[2][3] / MetresPerUnit : 0, actor >= 0 && PM_PainAnim(g_entities[actor].client->ps.torsoAnim),
		actor >= 0 ? VectorLength(g_entities[actor].client->ps.velocity) : 0, fallSteps, fallSteps ? fallMicroseconds / fallSteps : 0);
}
void G_JoltHit_f() {
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

void G_JoltKnockdown_f() {
	if (actor < 0 || !Active(&g_entities[actor]) || fall) return;
	gentity_t* ent = &g_entities[actor];
	vec3_t direction;
	AngleVectors(ent->client->ps.viewangles, direction, nullptr, nullptr);
	G_Knockdown(ent, &g_entities[0], direction, 400, qtrue);
}

bool G_JoltOwns(const gentity_t* ent) { return fall && ent && ent->s.number == actor; }
bool G_JoltPhysicsRoot(const gentity_t* ent) { return G_JoltOwns(ent) && !recoverStart; }
bool G_JoltKnockdown(gentity_t* ent, const float* direction, float strength) {
	if (G_JoltOwns(ent)) return true;
	if (!Active(ent) || strength < 100) return false;
	vec3_t point; VectorCopy(ent->currentOrigin, point); point[2] += 20;
	return StartFall(ent, direction, point, std::min(80.0f, strength * .15f));
}
bool G_JoltSuppressPain(const gentity_t* ent) { return ent && ent->s.number == actor && enabled && enabled->integer && lastHit == level.time; }
void G_JoltBeforeSave() { if (fall) G_JoltReset(); }

bool G_JoltRender(gentity_t* ent, int time, const float* origin, float* angles) {
	if (!G_JoltOwns(ent)) return false;
	angles[0] = angles[2] = 0; angles[1] = fallYaw;
	JoltReaction::Transform pose[JoltReaction::PartCount];
	fall->Sample(pose, std::max(0, time - level.time) * .001f);
	const float blend = recoverStart ? std::min(1.0f, (time - recoverStart) / 900.0f) : 0;
	if (recoverStart) {
		ClearPhysicalBones(ent);
		JoltReaction::Transform targets[JoltReaction::PartCount];
		for (int i = 0; i < JoltReaction::PartCount; ++i) {
			mdxaBone_t target;
			gi.G2API_GetBoltMatrix(ent->ghoul2, 0, bolts[i], &target, angles, origin, time, nullptr, ent->s.modelScale);
			for (int r = 0; r < 3; ++r) for (int c = 0; c < 4; ++c) targets[i].matrix[r][c] = target.matrix[r][c] * (c == 3 ? MetresPerUnit : 1);
		}
		JoltReaction::BlendTransforms(pose, targets, JoltReaction::PartCount, blend);
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

void G_JoltImpulse_f() {
	if (actor < 0 || (!Active(&g_entities[actor]) && !fall) || recoverStart) return;
	auto* ent = &g_entities[actor];
	vec3_t direction, point, angles = {0, ent->client->renderInfo.legsYaw, 0};
	if (gi.argc() > 1 && !Q_stricmp(gi.argv(1), "left")) angles[YAW] += 90;
	if (gi.argc() > 1 && !Q_stricmp(gi.argv(1), "right")) angles[YAW] -= 90;
	AngleVectors(angles, direction, nullptr, nullptr);
	VectorCopy(ent->currentOrigin, point); point[2] += 24;
	G_JoltHit(ent, direction, point, 15, MOD_BLASTER, HL_CHEST);
}

void G_JoltControl_f() {
	if (actor < 0 || !Active(&g_entities[actor]) || fall) return;
	auto* ent = &g_entities[actor];
	ent->NPC->controlledTime = level.time + 30000;
	G_SetViewEntity(&g_entities[0], ent);
}
