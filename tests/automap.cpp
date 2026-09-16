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
	auto area=[](const Polygon &poly) {
		float sum=0;
		for (int i=0;i<poly.count;++i) {
			const auto &a=poly.points[i],&b=poly.points[(i+1)%poly.count];
			sum+=a[0]*b[1]-b[0]*a[1];
		}
		return std::abs(sum)*0.5f;
	};
	const float partitioned=area(Clip(p,-16,false))+area(Clip(Clip(p,-16,true),48,false))+area(Clip(p,48,true));
	assert(std::abs(partitioned-area(p))<0.01f); // Splitting a slope retains its surface area.
	Point a={{0,0,-100}},b={{100,50,100}};
	assert(ClipSegment(a,b,-20,40));
	assert(a==Point({{40,20,-20}}) && b==Point({{70,35,40}}));
	std::swap(a,b);
	assert(ClipSegment(a,b,-20,40) && a[2]==-20 && b[2]==40);
	a={{0,0,10}}; b={{100,0,10}};
	assert(ClipSegment(a,b,10,20) && b[0]==100);
	assert(!ClipSegment(a,b,11,20));
	const Point centre = {{0,0,0}};
	assert(Project({{10,20,30}},centre,0,0)[0]==10 && Project({{10,20,30}},centre,0,0)[1]==-20);
	assert(Project({{0,0,30}},centre,45,55)[1]<0); // Height rises on the isometric display.
	for (float tilt : {0.0f,55.0f}) for (float yaw : {0.0f,45.0f,90.0f}) {
		const auto delta=DragPan(20,-12,1024,yaw,tilt,4.0f/3);
		const Point point={{32,80,5}};
		const auto before=Project(point,centre,yaw,tilt),after=Project(point,delta,yaw,tilt);
		assert(std::abs((after[0]-before[0])*ViewWidth/(2*1024)-20)<0.001f);
		assert(std::abs((after[1]-before[1])*ViewWidth/(2*1024)*(4.0f/3)+12)<0.001f);
	}
	assert(Clip(Clip(p,-128,true),128,false).count==3); // Wider slices retain the full floor/ramp.
	assert(IsControl("func_button",false));
	assert(IsControl("func_usable",true) && !IsControl("func_usable",false));
	assert(IsControl("misc_model_breakable",true));
	assert(!IsControl("trigger_multiple",true) && !IsControl("trigger_once",true));
	assert(!IsControl("func_door",true) && !IsControl("item_health",true) && !IsControl(nullptr,true));
	std::puts("PASS: automap height clipping, projection, and control classification");
}
