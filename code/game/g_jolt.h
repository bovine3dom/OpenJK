// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#ifdef USE_JOLT_REACTIONS
void G_JoltReset();
void G_JoltForget(const gentity_t* ent);
void G_JoltFrame();
void G_JoltHit(gentity_t* ent, const float* direction, const float* point, int damage, int mod, int hitLoc);
void G_JoltBoneAngles(gentity_t* ent, int bone, int time, float* angles);
void G_JoltSelect_f();
void G_JoltStatus_f();
void G_JoltHit_f();
void G_JoltKnockdown_f();
#else
inline void G_JoltReset() {}
inline void G_JoltForget(const gentity_t*) {}
inline void G_JoltFrame() {}
inline void G_JoltHit(gentity_t*, const float*, const float*, int, int, int) {}
inline void G_JoltBoneAngles(gentity_t*, int, int, float*) {}
#endif
