// SPDX-License-Identifier: GPL-2.0-or-later
#include "sound/mix_limiter.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

int main() {
	for(int rate:{22050,44100,48000}) {
		SteamSound::MixLimiter limiter; limiter.Reset(rate);
		std::vector<int> input(rate);
		for(int i=0;i<rate;++i) {
			input[i]=int(3000000*std::sin(i*.11));
			int left=input[i],right=-left;
			limiter.Process(left,right);
			assert(left==-right);
			if(i>=limiter.Delay()) assert(left==input[i-limiter.Delay()]);
		}
		assert(limiter.MinimumGain()==1 && limiter.LimitedFrames()==0);
		limiter.Reset(rate);
		// Impulses and overlapping loud tones straddle arbitrary mixer boundaries.
		for(int i=0;i<rate;++i) {
			const double level=(i<rate/2) ? 14.0 : .2;
			int right=int(level*3000000*std::sin(i*.11));
			if(i%997==0 && i<rate/2) right=50000000;
			int left=2*right;
			limiter.Process(left,right);
			assert(std::abs(left)<=7549748 && std::abs(right)<=7549748);
			assert(std::abs(left-2*right)<=1);
		}
		assert(limiter.Peak()>1 && limiter.MinimumGain()<.5 && limiter.LimitedFrames()>0);
		limiter.Reset(rate);
		for(int i=0;i<rate/10;++i) {int l=0,r=0; limiter.Process(l,r); assert(l==0 && r==0);}
	}
	std::cout<<"PASS: limiter headroom, transparent quiet input, stereo link, and reset\n";
}
