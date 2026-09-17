// SPDX-License-Identifier: GPL-2.0-or-later
#include "sound/steam_audio.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>

using namespace SteamSound;
using Clock=std::chrono::steady_clock;
static double Micros(Clock::time_point start) {
	return std::chrono::duration<double,std::micro>(Clock::now()-start).count();
}
static void Quad(Mesh &m,IPLVector3 a,IPLVector3 b,IPLVector3 c,IPLVector3 d) {
	const int n=int(m.vertices.size()); m.vertices.insert(m.vertices.end(),{a,b,c,d});
	m.triangles.push_back({{n,n+1,n+2}}); m.triangles.push_back({{n,n+2,n+3}}); m.materials.insert(m.materials.end(),{0,0});
}
int main() {
	Engine engine(44100); assert(engine.Ready());
	Mesh room;
	// A large static floor exposes scene-update cost when a small door moves.
	for(int x=-128;x<128;++x) for(int z=-128;z<128;++z)
		Quad(room,{float(x),0,float(z)},{float(x+1),0,float(z)},{float(x+1),0,float(z+1)},{float(x),0,float(z+1)});
	Quad(room,{-4,4,-4},{4,4,-4},{4,4,4},{-4,4,4});
	Quad(room,{-4,0,-4},{-4,4,-4},{4,4,-4},{4,0,-4});
	Quad(room,{-4,0,4},{4,0,4},{4,4,4},{-4,4,4});
	Quad(room,{-4,0,-4},{-4,0,4},{-4,4,4},{-4,4,-4});
	Quad(room,{4,0,-4},{4,4,-4},{4,4,4},{4,0,4});
	std::vector<IPLMaterial> materials(1,{{.1f,.15f,.2f},.5f,{.1f,.03f,.01f}});
	assert(engine.AddModel(room,materials));
	Mesh door; Quad(door,{0,0,-1},{0,3,-1},{0,3,1},{0,0,1});
	assert(engine.AddModel(door,materials));
	std::array<Voice,Voices> voices={};
	for(auto &voice:voices) {voice.active=true; voice.priority=1; voice.position={-2,1.5f,0};}
	IPLCoordinateSpace3 listener={}; listener.origin={2,1.5f,0};
	listener.right={1,0,0}; listener.up={0,1,0}; listener.ahead={0,0,-1};
	engine.Update(voices,listener,true,false,true); engine.Wait();
	double total=0,peak=0,resetPeak=0;
	for(int block=0;block<512;++block) {
		if(block && block%64==0) {
			voices[0].position.z+=.1f;
			engine.Update(voices,listener,true,false,true); engine.Wait();
		}
		float input[Block],left[Block]={},right[Block]={};
		for(int i=0;i<Block;++i) input[i]=.002f*std::sin((block*Block+i)*.062689f);
		const auto start=Clock::now();
		engine.Begin();
		for(int i=0;i<Voices;++i) engine.Mix(i,input,.5f,.5f,1,.2f,left,right);
		engine.End(.2f,left,right);
		const double elapsed=Micros(start); total+=elapsed; peak=std::max(peak,elapsed);
		for(float sample:left) assert(std::isfinite(sample) && std::abs(sample)<1);
	}
	for(int i=0;i<Voices;++i) {
		const auto start=Clock::now(); engine.ResetVoice(i); resetPeak=std::max(resetPeak,Micros(start));
	}
	double moveTotal=0,movePeak=0;
	for(int i=0;i<40;++i) {
		IPLMatrix4x4 transform={}; for(int j=0;j<4;++j) transform.elements[j][j]=1;
		transform.elements[2][3]=i*.1f;
		const auto start=Clock::now();
		engine.Object(1,1,transform,true); engine.Update(voices,listener,false,false,false); engine.Wait();
		const double elapsed=Micros(start); moveTotal+=elapsed; movePeak=std::max(movePeak,elapsed);
		if(i==0) assert(engine.Status().occlusion<.01f);
		if(i==39) assert(engine.Status().occlusion>.99f);
	}
	std::cout<<"voices="<<Voices<<" mix_mean_us="<<total/512<<" mix_peak_us="<<peak
		<<" reset_peak_us="<<resetPeak<<" door_mean_us="<<moveTotal/40<<" door_peak_us="<<movePeak
		<<" scene_peak_us="<<engine.Status().scenePeakUs<<'\n';
}
