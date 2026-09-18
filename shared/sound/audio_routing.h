// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>

namespace SteamSound {
enum class Route { Legacy, Protected, Full };
enum class SoundContext { World, Local, Voice, Mover, Weapon };
struct Routing { Route mode; const char *rule; };
inline const char *RouteName(Route mode) {
	return mode==Route::Legacy ? "legacy" : mode==Route::Protected ? "protected" : "full";
}
inline Routing ClassifySound(std::string name,SoundContext context=SoundContext::World) {
	for(char &c:name) {
		if(c=='\\') c='/';
		else if(c>='A' && c<='Z') c+= 'a'-'A';
	}
#define AUDIO_CONTEXT(ctx, mode, rule) if(context==SoundContext::ctx) return {Route::mode,rule};
#define AUDIO_PREFIX(prefix, mode, rule) if(name.compare(0,sizeof(prefix)-1,prefix)==0) return {Route::mode,rule};
#include "audio_routes.inc"
#undef AUDIO_CONTEXT
#undef AUDIO_PREFIX
	return {Route::Protected,"unclassified-review"};
}
}
