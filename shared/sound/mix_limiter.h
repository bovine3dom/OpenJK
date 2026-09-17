// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace SteamSound {
// Stereo-linked lookahead limiter for the engine's signed 24-bit paint samples.
class MixLimiter {
	static constexpr int Capacity=514;
	static constexpr double Scale=8388608.0, Ceiling=.9*Scale;
	struct Frame { int left,right; };
	struct Request { int64_t time; double key; };
	std::array<Frame,Capacity> audio={};
	std::array<Request,Capacity> requests={};
	int delay=133,head=0,tail=0;
	int64_t time=0;
	double gain=1,release=0,peak=0,minimum=1;
	uint64_t limited=0;
	static int Next(int i) { return (i+1)%Capacity; }
	static int Previous(int i) { return (i+Capacity-1)%Capacity; }
public:
	MixLimiter() { Reset(44100); }
	void Reset(int rate) {
		rate=std::max(1,rate);
		delay=std::min(Capacity-2,std::max(1,int(std::ceil(rate*.003))));
		release=1-std::exp(-1.0/(rate*.12));
		time=0; head=tail=0; gain=1; audio.fill({0,0}); ResetStats();
	}
	void ResetStats() { peak=0; minimum=1; limited=0; }
	int Delay() const { return delay; }
	double Peak() const { return peak/Scale; }
	double MinimumGain() const { return minimum; }
	uint64_t LimitedFrames() const { return limited; }
	void Process(int &left,int &right) {
		const double magnitude=std::max(std::abs(double(left)),std::abs(double(right)));
		peak=std::max(peak,magnitude);
		const int64_t outputTime=time-delay;
		while(head!=tail && requests[head].time<outputTime) head=Next(head);
		if(magnitude>Ceiling) {
			// Each future peak defines a linear attack ending at its safe gain.
			const double key=double(time)+delay*Ceiling/magnitude;
			while(head!=tail && requests[Previous(tail)].key>=key) tail=Previous(tail);
			requests[tail]={time,key}; tail=Next(tail);
		}
		gain+=(1-gain)*release;
		if(head!=tail) gain=std::min(gain,std::max(0.0,(requests[head].key-double(outputTime))/delay));
		minimum=std::min(minimum,gain); if(gain<.999) ++limited;
		audio[time%Capacity]={left,right};
		if(outputTime<0) left=right=0;
		else {
			const auto &frame=audio[outputTime%Capacity];
			left=int(std::lround(frame.left*gain)); right=int(std::lround(frame.right*gain));
		}
		++time;
	}
};
}
