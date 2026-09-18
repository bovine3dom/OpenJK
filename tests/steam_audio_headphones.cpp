// SPDX-License-Identifier: GPL-2.0-or-later
#include "sound/steam_audio.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
using namespace SteamSound;
int main(int argc,char **) {
	Engine engine(argc>1 ? 48000 : 44100,true); assert(engine.Ready() && engine.Headphones());
	IPLCoordinateSpace3 listener={}; listener.ahead={0,0,-1}; listener.up={0,1,0}; listener.right={1,0,0};
	std::array<Voice,Voices> voices={}; voices[0].active=true;
	auto render=[&](IPLVector3 position,bool attached=false,float left=.5f,float right=.5f,int impulse=Block-1) {
		engine.ResetVoice(0);
		voices[0].position=position; voices[0].listenerAttached=attached;
		engine.SetSpatialization(voices,listener);
		std::vector<float> result;
		for(int block=0;block<4;++block) {
			float input[Block]={},l[Block]={},r[Block]={};
			if(block==0) input[impulse]=.5f;
			engine.Begin(); engine.Mix(0,input,left,right,1,0,l,r,.12f,1,true);
			for(int i=0;i<Block;++i) { assert(std::isfinite(l[i]) && std::isfinite(r[i])); result.push_back(l[i]); result.push_back(r[i]); }
		}
		return result;
	};
	auto energy=[](const std::vector<float> &v,int ear) { double e=0; for(size_t i=ear;i<v.size();i+=2) e+=v[i]*v[i]; return e; };
	const auto left=render({-2,0,0}),right=render({2,0,0});
	assert(energy(left,0)>energy(left,1)*1.5); assert(energy(right,1)>energy(right,0)*1.5);
	const auto front=render({0,0,-2}),back=render({0,0,2}),above=render({0,2,0}),below=render({0,-2,0});
	assert(front!=back && above!=below && front!=above);
	// Equal gain sums must replace, not combine with, legacy stereo panning.
	assert(render({-2,0,0},false,0,1)==left);
	const auto quiet=render({-2,0,0},false,.25f,.25f);
	assert(std::abs(energy(quiet,0)/energy(left,0)-.25)<1e-5);
	listener.ahead={0,0,1}; listener.right={-1,0,0};
	const auto turned=render({-2,0,0}); assert(energy(turned,1)>energy(turned,0)*1.5);
	for(const auto &v:{render({0,0,0}),render({-2,0,0},true)})
		assert(energy(v,0)>0 && std::abs(energy(v,0)-energy(v,1))<1e-6);
	// An impulse at the block boundary must retain its convolution tail.
	double tail=0; for(size_t i=Block*2;i<left.size();++i) tail+=left[i]*left[i]; assert(tail>0);
	const auto early=render({-2,0,0},false,.5f,.5f,0);
	for(int ear=0;ear<2;++ear) assert(std::abs(energy(early,ear)/energy(turned,ear)-1)<1e-4);
	engine.ResetVoice(0);
	float zero[Block]={},l[Block]={},r[Block]={}; engine.Begin(); engine.Mix(0,zero,1,1,1,0,l,r,.12f,1,true);
	for(int i=0;i<Block;++i) assert(l[i]==0 && r[i]==0);
	std::puts("Headphone direction, elevation, rotation, gain, centering, tails, and reset passed.");
}
