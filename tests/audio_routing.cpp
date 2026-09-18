// SPDX-License-Identifier: GPL-2.0-or-later
#include "sound/audio_routing.h"
#include <cassert>
#include <iostream>
using namespace SteamSound;
int main() {
	assert(ClassifySound("sound/weapons/blaster/fire").mode==Route::Full);
	assert(ClassifySound("SOUND\\PLAYER\\FOOTSTEPS\\metal_run1.wav").mode==Route::Full);
	assert(ClassifySound("sound/effects/explode10").mode==Route::Protected);
	assert(ClassifySound("sound/effects/stone_explosion").mode==Route::Protected);
	assert(ClassifySound("sound/weapons/explosions/cargoexplode.wav").mode==Route::Protected);
	assert(ClassifySound("sound/weapons/concussion/explosion.mp3").mode==Route::Protected);
	assert(ClassifySound("sound/weapons/detpack/explode.mp3").mode==Route::Protected);
	assert(ClassifySound("sound/weapons/repeater/alt_explode.mp3").mode==Route::Protected);
	assert(ClassifySound("sound/weapons/thermal/explode.mp3").mode==Route::Protected);
	assert(ClassifySound("sound/weapons/tie_fighter/TIEexplode.wav").mode==Route::Protected);
	assert(ClassifySound("sound/movers/platforms/lift").mode==Route::Protected);
	assert(ClassifySound("sound/ambience/prototype/alarm1").mode==Route::Protected);
	assert(ClassifySound("sound/effects/jumpstream_lp").mode==Route::Protected);
	assert(ClassifySound("sound/chars/kyle/dialogue").mode==Route::Protected);
	assert(ClassifySound("sound/interface/select").mode==Route::Legacy);
	assert(ClassifySound("unclassified").mode==Route::Protected);
	assert(ClassifySound("sound/weapons/blaster/fire",SoundContext::Voice).mode==Route::Protected);
	assert(ClassifySound("sound/weapons/blaster/fire",SoundContext::Mover).mode==Route::Protected);
	assert(ClassifySound("sound/weapons/blaster/fire",SoundContext::Local).mode==Route::Legacy);
	assert(ClassifySound("sound/weapons/thermal/explode.mp3",SoundContext::Local).mode==Route::Legacy);
	assert(ClassifySound("sound/chars/shot",SoundContext::Weapon).mode==Route::Full);
	assert(ClassifySound("sound/weapons/thermal/explode.mp3",SoundContext::Weapon).mode==Route::Protected);
	std::cout<<"PASS: routing context, case, separators, asset families, and conservative fallback\n";
}
