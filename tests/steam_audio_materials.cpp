// SPDX-License-Identifier: GPL-2.0-or-later
#include "sound/steam_audio.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>

using namespace SteamSound;
static void Quad(Mesh &mesh,IPLVector3 a,IPLVector3 b,IPLVector3 c,IPLVector3 d) {
	const int n=int(mesh.vertices.size()); mesh.vertices.insert(mesh.vertices.end(),{a,b,c,d});
	mesh.triangles.push_back({{n,n+1,n+2}}); mesh.triangles.push_back({{n,n+2,n+3}});
	mesh.materials.insert(mesh.materials.end(),{0,0});
}
static double Response(const char *name,const IPLMaterial &material,float roomScale=1) {
	Engine engine(44100); assert(engine.Ready());
	Mesh room;
	Quad(room,{-5,0,-2},{5,0,-2},{5,0,2},{-5,0,2});
	Quad(room,{-5,4,2},{5,4,2},{5,4,-2},{-5,4,-2});
	Quad(room,{-5,0,-2},{-5,4,-2},{5,4,-2},{5,0,-2});
	Quad(room,{5,0,2},{5,4,2},{-5,4,2},{-5,0,2});
	Quad(room,{-5,0,2},{-5,4,2},{-5,4,-2},{-5,0,-2});
	Quad(room,{5,0,-2},{5,4,-2},{5,4,2},{5,0,2});
	for(auto &v:room.vertices) {v.x*=roomScale; v.y*=roomScale; v.z*=roomScale;}
	assert(engine.AddModel(room,{material}));
	std::array<Voice,Voices> voices={}; voices[0].active=true; voices[0].priority=1; voices[0].position={-2,1.5f,0};
	IPLCoordinateSpace3 listener={}; listener.origin={2,1.5f,0};
	listener.right={1,0,0}; listener.up={0,1,0}; listener.ahead={0,0,-1};
	engine.Update(voices,listener,true,false,true); engine.Wait();
	double early=0,late=0,peak=0; int first=-1;
	for(int frame=0;frame<3*44100;frame+=Block) {
		float input[Block]={},left[Block]={},right[Block]={};
		if(!frame) input[0]=.5f;
		engine.Begin(); engine.Mix(0,input,.5f,.5f,1,.2f,left,right); engine.End(.2f,left,right);
		engine.Indirect(left,right);
		for(int i=0;i<Block;++i) {
			const double energy=left[i]*left[i]+right[i]*right[i];
			assert(std::isfinite(energy));
			(frame+i<.6*44100 ? early : late)+=energy;
			peak=std::max(peak,energy);
			if(first<0 && energy>1e-12) first=frame+i;
		}
	}
	assert(first>=0 && early>0);
	std::cout<<name<<" early_energy="<<early<<" late_energy="<<late<<" peak_power="<<peak
		<<" first_seconds="<<first/44100.0<<" rt60="<<engine.Status().reverb<<'\n';
	return early+late;
}
int main() {
	const double metal=Response("metal",{{.20f,.07f,.06f},.1f,{.10f,.015f,.005f}});
	Response("metal_room_scale_2",{{.20f,.07f,.06f},.1f,{.10f,.015f,.005f}},2);
	Response("metal_scattering_0.7",{{.20f,.07f,.06f},.7f,{.10f,.015f,.005f}});
	const double damped=Response("metal_absorption_0.20_0.35_0.65",{{.20f,.35f,.65f},.1f,{.10f,.015f,.005f}});
	assert(damped<metal);
	std::cout<<"PASS: higher absorption reduces reflected energy in the test room\n";
}
