// SPDX-License-Identifier: GPL-2.0-or-later
#include "qcommon/automap.h"
#include <cassert>
#include <cstdio>

int main() {
	using namespace Automap;
	Polygon p = {{{{0,0,-100}},{{100,0,0}},{{0,100,100}}},3};
	const auto clipped = Clip(Clip(p,-32,true),32,false);
	assert(clipped.count == 5);
	for (int i=0;i<clipped.count;++i) assert(clipped.points[i][2]>=-32 && clipped.points[i][2]<=32);
	assert(Clip(p,101,true).count==0 && Clip(p,-101,false).count==0);
	const Point centre = {{0,0,0}};
	assert(Project({{10,20,30}},centre,0,0)[0]==10 && Project({{10,20,30}},centre,0,0)[1]==-20);
	assert(Project({{0,0,30}},centre,45,55)[1]<0); // Height rises on the isometric display.
	assert(IsControl("func_button",false));
	assert(IsControl("func_usable",true) && !IsControl("func_usable",false));
	assert(IsControl("misc_model_breakable",true));
	assert(!IsControl("trigger_multiple",true) && !IsControl("trigger_once",true));
	assert(!IsControl("func_door",true) && !IsControl("item_health",true) && !IsControl(nullptr,true));
	std::puts("PASS: automap height clipping, projection, and control classification");
}
