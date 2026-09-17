// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace SteamSound {
// The closest approach must occur in this segment, not ahead of or behind it.
inline bool NearPass(const float *start,const float *end,const float *listener,float radius,float *closest) {
	float delta[3],length2=0,projection=0;
	for(int i=0;i<3;++i) { delta[i]=end[i]-start[i]; length2+=delta[i]*delta[i]; projection+=(listener[i]-start[i])*delta[i]; }
	if(!(length2>0)) return false;
	const float t=projection/length2;
	if(!(t>0 && t<=1)) return false;
	float distance2=0;
	for(int i=0;i<3;++i) { closest[i]=start[i]+t*delta[i]; const float d=closest[i]-listener[i]; distance2+=d*d; }
	return distance2<=radius*radius;
}
}
