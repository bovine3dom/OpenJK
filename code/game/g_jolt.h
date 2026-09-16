// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#ifdef USE_JOLT_REACTIONS
void G_JoltReset();
void G_JoltForget(const gentity_t* ent);
void G_JoltFrame();
void G_JoltBeginFrame();
void G_JoltHit(gentity_t* ent, const float* direction, const float* point, int damage, int mod, int hitLoc);
void G_JoltBoneAngles(gentity_t* ent, int bone, int time, float* angles);
void G_JoltSelect_f();
void G_JoltStatus_f();
void G_JoltHit_f();
void G_JoltKnockdown_f();
void G_JoltImpulse_f();
void G_JoltControl_f();
void G_JoltShoot_f();
void G_JoltBalance_f();
void G_JoltPush_f();
void G_JoltDemo_f();
void G_JoltBlast_f();
bool G_JoltOwns(const gentity_t* ent);
bool G_JoltOnGround(const gentity_t* ent);
void G_JoltMovementTrace(trace_t* result, const vec3_t start, const vec3_t mins, const vec3_t maxs,
	const vec3_t end, int passEntityNum, int contentmask, EG2_Collision collision, int lod);
bool G_JoltBlocksAI(const gentity_t* ent);
bool G_JoltPhysicsRoot(const gentity_t* ent);
bool G_JoltSuppressPain(const gentity_t* ent, int mod);
bool G_JoltRender(gentity_t* ent, int time, const float* origin, float* angles);
void G_JoltBeforeSave();
void G_JoltDeath(gentity_t* ent);
void G_JoltAfterDeath(gentity_t* ent);
bool G_JoltDead(const gentity_t* ent);
bool G_JoltKnockback(gentity_t* ent, const float* velocity);
bool G_JoltExplosion(int mod);
bool G_JoltKnockdown(gentity_t* ent, const float* direction, float strength);
#else
inline void G_JoltReset() {}
inline void G_JoltForget(const gentity_t*) {}
inline void G_JoltFrame() {}
inline void G_JoltBeginFrame() {}
inline void G_JoltHit(gentity_t*, const float*, const float*, int, int, int) {}
inline void G_JoltBoneAngles(gentity_t*, int, int, float*) {}
inline bool G_JoltOwns(const gentity_t*) { return false; }
inline bool G_JoltOnGround(const gentity_t*) { return false; }
inline bool G_JoltBlocksAI(const gentity_t*) { return false; }
inline bool G_JoltPhysicsRoot(const gentity_t*) { return false; }
inline bool G_JoltSuppressPain(const gentity_t*, int) { return false; }
inline bool G_JoltRender(gentity_t*, int, const float*, float*) { return false; }
inline void G_JoltBeforeSave() {}
inline void G_JoltDeath(gentity_t*) {}
inline void G_JoltAfterDeath(gentity_t*) {}
inline bool G_JoltDead(const gentity_t*) { return false; }
inline bool G_JoltKnockback(gentity_t*, const float*) { return false; }
inline bool G_JoltExplosion(int) { return false; }
inline bool G_JoltKnockdown(gentity_t*, const float*, float) { return false; }
#endif
