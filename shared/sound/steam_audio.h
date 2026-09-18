// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <phonon.h>
#include <array>
#include <vector>
#include <memory>

namespace SteamSound {
constexpr int Block=256, Voices=32;
struct Mesh {
	std::vector<IPLVector3> vertices;
	std::vector<IPLTriangle> triangles;
	std::vector<int> materials;
};
struct Voice {
	IPLVector3 position={};
	bool active=false;
	float priority=0;
	bool listenerAttached=false;
};
struct Info {
	int triangles=0, probes=0, active=0, reflected=0;
	int reflectionMs=0;
	int scenePeakUs=0;
	float occlusion=1, transmission=1, reverb=0;
};
class Engine {
public:
	Engine(int rate, bool headphones=false);
	~Engine();
	bool Headphones() const;
	bool Ready() const;
	bool Busy() const;
	void Wait();
	bool LoadWorld(const std::vector<unsigned char> &data);
	bool AddModel(const Mesh &mesh, const std::vector<IPLMaterial> &materials);
	void Object(int entity, int model, const IPLMatrix4x4 &transform, bool enabled);
	std::vector<unsigned char> SaveWorld();
	bool LoadProbes(const std::vector<unsigned char> &data);
	bool Bake(const IPLVector3 &low, const IPLVector3 &high);
	std::vector<unsigned char> SaveProbes();
	void SetSpatialization(const std::array<Voice,Voices> &voices, const IPLCoordinateSpace3 &listener);
	void Update(const std::array<Voice,Voices> &voices, const IPLCoordinateSpace3 &listener, bool reflections, bool pathing, bool simulateReflections);
	void ResetVoice(int index);
	void Begin();
	void Mix(int index, const float *input, float left, float right, float gain, float wet, float *outLeft, float *outRight, float transmissionFloor=0, float reverbSend=1, bool protectedDirect=false);
	void End(float wet, float *outLeft, float *outRight);
	void Indirect(float *left, float *right) const;
	Info Status() const;
	IPLDirectEffectParams DirectParams(int index) const;
private:
	struct Impl;
	std::unique_ptr<Impl> p;
};
}
