// SPDX-License-Identifier: GPL-2.0-or-later
#include "qcommon/automap_nav.h"
#include <cassert>
#include <iostream>
#include <limits>
#include <algorithm>

using Automap::Point;
static void Quad(std::vector<Point> &mesh,float x0,float x1,float z0,float z1,bool ceiling=false) {
	Point a={{x0,0,z0}},b={{x1,0,z1}},c={{x1,512,z1}},d={{x0,512,z0}};
	if (ceiling) mesh.insert(mesh.end(),{a,c,b,a,d,c});
	else mesh.insert(mesh.end(),{a,b,c,a,c,d});
}
int main() {
	Automap::NavMap nav;
	std::vector<Point> mesh;
	Quad(mesh,0,512,0,0); Quad(mesh,0,512,256,256);
	assert(nav.Build(mesh));
	assert(nav.floors.size()==2 && nav.links.empty());
	assert(nav.FloorAt({{256,256,24}})==0);
	assert(nav.FloorAt({{256,256,280}})==1);
	assert(nav.FloorAt({{256,256,220}})==0); // Do not select the floor overhead.
	mesh.clear();
	Quad(mesh,0,512,0,0); Quad(mesh,1024,1536,32,32);
	assert(nav.Build(mesh));
	assert(nav.floors.size()==1 && nav.links.empty()); // One band does not imply connectivity.
	mesh.clear();
	Quad(mesh,0,512,0,0); Quad(mesh,1024,1536,96,96);
	assert(nav.Build(mesh));
	assert(nav.floors.size()==2 && nav.links.empty());
	mesh.clear();
	Quad(mesh,0,512,0,0); Quad(mesh,512,1024,0,256); Quad(mesh,1024,1536,256,256);
	assert(nav.Build(mesh));
	assert(nav.floors.size()==2 && !nav.links.empty());
	for (const auto &link : nav.links) assert(link.from!=link.to);
	mesh.clear();
	Quad(mesh,0,512,0,0);
	for (int i=0;i<16;++i) Quad(mesh,512+i*32,544+i*32,(i+1)*16,(i+1)*16);
	Quad(mesh,1024,1536,256,256);
	assert(nav.Build(mesh));
	assert(nav.floors.size()==2 && !nav.links.empty());
	assert(nav.FloorAt({{256,256,24}})!=nav.FloorAt({{1280,256,280}}));
	mesh.clear();
	Quad(mesh,0,512,0,0); Quad(mesh,0,512,32,32,true);
	assert(!nav.Build(mesh)); // Insufficient headroom below the ceiling.
	assert(nav.floors.empty());
	mesh[0][0]=std::numeric_limits<float>::quiet_NaN();
	assert(!nav.Build(mesh));
	assert(!nav.Build({}));
	nav.links={
		{{{0,0,0}},{{0,0,256}},0,1},
		{{{32,0,256}},{{32,0,0}},1,0}, // Reversed connection within the same group.
		{{{120,0,0}},{{120,0,256}},0,1},
		{{{240,0,0}},{{240,0,256}},0,1}, // Near the previous member, but not the seed.
		{{{0,0,0}},{{0,512,256}},0,1}, // Only one endpoint is close.
		{{{0,0,0}},{{0,0,256}},0,2}}; // Another layer pair.
	const auto grouped=nav.DisplayLinks();
	assert(grouped.size()==4 && nav.links.size()==6);
	std::reverse(nav.links.begin(),nav.links.end());
	const auto reversed=nav.DisplayLinks();
	for (size_t i=0;i<grouped.size();++i) assert(grouped[i].a==reversed[i].a && grouped[i].b==reversed[i].b && grouped[i].from==reversed[i].from && grouped[i].to==reversed[i].to);
	std::cout << "PASS: floors, ramps, stairs, clearance, invalid input, and link grouping\n";
}
