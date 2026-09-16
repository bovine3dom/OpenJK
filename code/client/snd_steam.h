// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#ifdef USE_STEAM_AUDIO
void S_SteamInit();
void S_SteamShutdown();
void S_SteamClear();
void S_SteamPrepare();
bool S_SteamActive();
void S_SteamUpdate(const float *head, const float axis[3][3], int listener, bool inWater);
void S_SteamBeginMix();
void S_SteamEndMix(int soundtime);
void S_SteamBeginBlock();
bool S_SteamPaint(channel_t *channel, const short *samples, int count, int offset, int volume);
void S_SteamEndBlock(portable_samplepair_t *output, int count);
#else
inline void S_SteamInit() {}
inline void S_SteamShutdown() {}
inline void S_SteamClear() {}
inline void S_SteamPrepare() {}
inline bool S_SteamActive() { return false; }
inline void S_SteamUpdate(const float *, const float [3][3], int, bool) {}
inline void S_SteamBeginMix() {}
inline void S_SteamEndMix(int) {}
inline void S_SteamBeginBlock() {}
inline bool S_SteamPaint(channel_t *, const short *, int, int, int) { return false; }
inline void S_SteamEndBlock(portable_samplepair_t *, int) {}
#endif
