// SPDX-License-Identifier: GPL-2.0-or-later
#include "common_headers.h"
#include "g_jolt.h"
#include "Q3_Interface.h"
#include "physics/jolt_reaction.h"
#include <algorithm>
#include <cmath>
#include <chrono>

extern qboolean PM_InKnockDown(playerState_t* ps);
extern qboolean PM_InGetUp(playerState_t* ps);
extern Vehicle_t* G_IsRidingVehicle(gentity_t* ent);
extern void G_Knockdown(gentity_t* self, gentity_t* attacker, const vec3_t direction, float strength, qboolean breakSaberLock);

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

bool Eligible(const gentity_t* ent) {
	return ent && ent->inuse && ent->client && ent->NPC && ent->health > 0 &&
		ent->client->NPC_class == CLASS_STORMTROOPER && ent->NPC_type &&
		!Q_stricmp(ent->NPC_type, "stormtrooper") && ent->ghoul2.size() &&
		!Q_stricmp(ent->ghoul2[0].mFileName, "models/players/stormtrooper/model.glm") &&
		ent->lowerLumbarBone >= 0 && ent->cervicalBone >= 0 && !ent->client->dismembered;
}
bool AnimationOwnsPose(gentity_t* ent) {
	return in_camera || ent->s.weapon == WP_SABER || ent->s.weapon == WP_EMPLACED_GUN ||
		ent->client->ps.groundEntityNum == ENTITYNUM_NONE ||
		ent->client->ps.ikStatus || ent->client->ps.heldByBolt || G_IsRidingVehicle(ent) ||
		(ent->client->ps.eFlags & (EF_FORCE_GRIPPED | EF_FORCE_DRAINED | EF_HELD_BY_RANCOR | EF_HELD_BY_WAMPA)) ||
		ent->next_roff_time > level.time || Q3_TaskIDPending(ent, TID_ANIM_BOTH) ||
		Q3_TaskIDPending(ent, TID_ANIM_UPPER) || Q3_TaskIDPending(ent, TID_ANIM_LOWER) ||
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

void G_JoltReset() {
	simulation.reset();
	actor = pelvisBolt = -1;
	hits = poses = 0;
	peak = 0;
	stepMicroseconds = 0;
	measuredSteps = 0;
	suspended = false;
}
void G_JoltForget(const gentity_t* ent) {
	if (ent && ent->s.number == actor) G_JoltReset();
}
void G_JoltFrame() {
	if (!enabled) enabled = gi.cvar("g_joltReactions", "0", CVAR_CHEAT);
	if (!simulation) return;
	gentity_t* ent = &g_entities[actor];
	if (!enabled->integer || !Eligible(ent)) { G_JoltReset(); return; }
	const int elapsed = level.time - lastTime;
	lastTime = level.time;
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
}
void G_JoltHit(gentity_t* ent, const float* direction, const float* point, int damage, int mod, int hitLoc) {
	if (!Active(ent) || !direction || !point || damage <= 0 ||
		(mod != MOD_BLASTER && mod != MOD_BRYAR && mod != MOD_BRYAR_ALT)) return;
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
	++hits;
}
void G_JoltBoneAngles(gentity_t* ent, int bone, int time, float* angles) {
	if (!Active(ent)) return;
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
	gi.Printf("Jolt 5.3.0: selected stormtrooper %d; torso=%.3fm head=%.3fm radius=%.3fm; masses=28/5kg\n",
		actor, dimensions.torsoLength, dimensions.headLength, dimensions.torsoRadius);
}
void G_JoltStatus_f() {
	const auto pose = simulation ? simulation->Sample(1) : JoltReaction::Pose{};
	gi.Printf("jolt actor=%d active=%d hits=%d poses=%d steps=%u peak=%.3f torso=%.3f,%.3f,%.3f head=%.3f,%.3f,%.3f health=%d us_per_step=%.2f\n",
		actor, actor >= 0 && Active(&g_entities[actor]), hits, poses, simulation ? simulation->Steps() : 0, peak,
		pose.angles[0][0], pose.angles[0][1], pose.angles[0][2], pose.angles[1][0], pose.angles[1][1], pose.angles[1][2],
		actor >= 0 ? g_entities[actor].health : 0, measuredSteps ? stepMicroseconds / measuredSteps : 0);
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
	if (actor >= 0 && Active(&g_entities[actor]))
		G_Knockdown(&g_entities[actor], &g_entities[0], vec3_origin, 300, qtrue);
}
