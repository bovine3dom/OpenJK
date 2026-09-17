// SPDX-License-Identifier: GPL-2.0-or-later
#include "sound/near_pass.h"
#include <cassert>
#include <iostream>

int main() {
	const float ear[]={0,0,0}; float point[3];
	float start[]={-160,32,0},end[]={160,32,0};
	assert(SteamSound::NearPass(start,end,ear,72,point));
	assert(point[0]==0 && point[1]==32 && point[2]==0);
	start[1]=end[1]=80; assert(!SteamSound::NearPass(start,end,ear,72,point));
	start[1]=end[1]=0;
	end[0]=-10; assert(!SteamSound::NearPass(start,end,ear,72,point));
	start[0]=10; end[0]=160; assert(!SteamSound::NearPass(start,end,ear,72,point));
	start[0]=end[0]; assert(!SteamSound::NearPass(start,end,ear,72,point));
	for(int step:{1,5,32,320}) {
		int passes=0;
		for(int x=-160;x<160;x+=step) {
			start[0]=float(x); end[0]=float(x+step); start[1]=end[1]=-32;
			passes+=SteamSound::NearPass(start,end,ear,72,point);
		}
		assert(passes==1);
	}
	std::cout<<"PASS: near-pass distance, direction, and frame-step independence\n";
}
