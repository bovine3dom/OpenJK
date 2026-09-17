// SPDX-License-Identifier: GPL-2.0-or-later
#include "sound/steam_audio.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace SteamSound;
static void Quad(Mesh &m,IPLVector3 a,IPLVector3 b,IPLVector3 c,IPLVector3 d) {
	const int n=int(m.vertices.size()); m.vertices.insert(m.vertices.end(),{a,b,c,d});
	m.triangles.push_back({{n,n+1,n+2}}); m.triangles.push_back({{n,n+2,n+3}}); m.materials.insert(m.materials.end(),{0,0});
}
int main() {
	Engine engine(44100); assert(engine.Ready());
	std::vector<IPLMaterial> material(1); material[0]={{0.1f,0.15f,0.2f},0.5f,{0.1f,0.03f,0.01f}};
	Mesh room;
	Quad(room,{-6,0,-6},{-6,0,6},{6,0,6},{6,0,-6});
	Quad(room,{-6,4,-6},{6,4,-6},{6,4,6},{-6,4,6});
	Quad(room,{-6,0,-6},{6,0,-6},{6,4,-6},{-6,4,-6});
	Quad(room,{-6,0,6},{-6,4,6},{6,4,6},{6,0,6});
	Quad(room,{-6,0,-6},{-6,4,-6},{-6,4,6},{-6,0,6});
	Quad(room,{6,0,-6},{6,0,6},{6,4,6},{6,4,-6});
	assert(engine.AddModel(room,material));
	const auto saved=engine.SaveWorld(); assert(!saved.empty());
	Mesh door; Quad(door,{0,0,-6},{0,4,-6},{0,4,6},{0,0,6}); assert(engine.AddModel(door,material));
	IPLMatrix4x4 transform={}; for(int i=0;i<4;++i) transform.elements[i][i]=1;
	std::array<Voice,Voices> voices={}; voices[0].active=true; voices[0].priority=1; voices[0].position={-2,1.5f,0};
	IPLCoordinateSpace3 listener={}; listener.origin={2,1.5f,0}; listener.right={1,0,0}; listener.up={0,1,0}; listener.ahead={0,0,-1};
	voices[0].position.x=-8;
	engine.Update(voices,listener,false,false,false); engine.Wait(); assert(engine.Status().occlusion<0.01f);
	voices[0].position.x=-2;
	engine.Update(voices,listener,false,false,false); engine.Wait(); assert(engine.Status().occlusion>0.99f);
	float input[Block],left[Block]={},right[Block]={};
	for(int i=0;i<Block;++i) input[i]=0.1f*std::sin(i*0.5f);
	auto energy=[&]() {
		float sum=0;
		for(int n=0;n<100;++n) {
			std::fill(left,left+Block,0); std::fill(right,right+Block,0);
			engine.Begin(); engine.Mix(0,input,0.5f,0.5f,1,0,left,right); engine.End(0,left,right);
			if(n>80) for(float v:left) { assert(std::isfinite(v)); sum+=v*v; }
		}
		return sum;
	};
	const float clear=energy(); assert(clear>0);
	engine.Object(1,1,transform,true); engine.Update(voices,listener,false,false,false);
	engine.Wait();
	assert(engine.Status().occlusion<0.01f && engine.Status().transmission<0.1f);
	const float blocked=energy(); assert(blocked<clear*0.1f);
	engine.Object(1,1,transform,false); engine.Update(voices,listener,true,false,true);
	engine.Wait();
	assert(engine.Status().occlusion>0.99f);
	std::fill(input,input+Block,0); input[0]=0.5f;
	float tail=0;
	for(int n=0;n<500;++n) {
		std::fill(left,left+Block,0); std::fill(right,right+Block,0);
		engine.Begin(); engine.Mix(0,input,0,0,1,0.3f,left,right); engine.End(0.3f,left,right);
		for(float v:left) { assert(std::isfinite(v)); if(n>4) tail+=v*v; }
		input[0]=0;
	}
	std::cout << "clear=" << clear << " blocked=" << blocked << " tail=" << tail << " rt60=" << engine.Status().reverb << std::endl;
	assert(tail>0.000001f);
	assert(engine.Bake({-6,0,-6},{6,4,6}));
	assert(engine.Status().probes>0 && engine.Status().probes<=256);
	const auto probes=engine.SaveProbes(); assert(!probes.empty());
	Engine restored(44100); assert(restored.Ready() && restored.LoadWorld(saved) && restored.LoadProbes(probes));
	assert(restored.Status().triangles==int(room.triangles.size()));
	restored.Update(voices,listener,true,true,true);
	restored.Wait();
	assert(restored.AddModel({},material)); // Reserve world model zero after loading the scene cache.
	Mesh partition; Quad(partition,{0,0,-2},{0,4,-2},{0,4,2},{0,0,2});
	assert(restored.AddModel(partition,material));
	restored.Object(1,1,transform,true);
	restored.Update(voices,listener,false,true,false);
	restored.Wait();
	assert(restored.Status().occlusion<0.01f);
	float routed=0;
	for(int n=0;n<100;++n) {
		for(int i=0;i<Block;++i) input[i]=0.1f*std::sin((n*Block+i)*0.5f);
		std::fill(left,left+Block,0); std::fill(right,right+Block,0);
		restored.Begin(); restored.Mix(0,input,0,0,1,0,left,right); restored.End(0,left,right);
		for(float v:left) {assert(std::isfinite(v)); routed+=v*v;}
	}
	assert(routed>0.000001f); // Only the indirect path can reach the output in this test.
	// Extend past the room edges so probe rays cannot pass along a shared boundary.
	Mesh sealedDoor; Quad(sealedDoor,{0,-1,-7},{0,5,-7},{0,5,7},{0,-1,7});
	assert(restored.AddModel(sealedDoor,material));
	restored.Object(1,2,transform,true); restored.Update(voices,listener,false,true,false); restored.Wait();
	float sealed=0;
	for(int n=0;n<100;++n) {
		std::fill(left,left+Block,0); std::fill(right,right+Block,0);
		restored.Begin(); restored.Mix(0,input,0,0,1,0,left,right); restored.End(0,left,right);
		if(n>20) for(float v:left) {assert(std::isfinite(v)); sealed+=v*v;}
	}
	std::cout << "sealed=" << sealed << " routed=" << routed << std::endl;
	assert(sealed<routed*0.01f); // A closed barrier invalidates the baked route.
	std::cout << "PASS: Steam Audio direct transmission, moving barrier, reflection tail, and cached probes\n";
}
