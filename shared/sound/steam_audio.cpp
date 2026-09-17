// SPDX-License-Identifier: GPL-2.0-or-later
#include "steam_audio.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <future>
#include <chrono>
#include <atomic>
#include <cstring>

namespace SteamSound {
namespace {
constexpr float Duration=1.5f;
constexpr float HybridDuration=0.6f;
constexpr int Order=1, Channels=4;
IPLSimulationFlags All=static_cast<IPLSimulationFlags>(IPL_SIMULATIONFLAGS_DIRECT|IPL_SIMULATIONFLAGS_REFLECTIONS|IPL_SIMULATIONFLAGS_PATHING);
IPLDirectEffectParams NeutralDirect() {
	IPLDirectEffectParams params={}; params.occlusion=params.distanceAttenuation=params.directivity=1;
	for(int i=0;i<3;++i) params.airAbsorption[i]=params.transmission[i]=1;
	return params;
}
}
struct Engine::Impl {
	IPLContext context=nullptr;
	IPLHRTF hrtf=nullptr;
	IPLEmbreeDevice embree=nullptr;
	IPLSceneType sceneType=IPL_SCENETYPE_DEFAULT;
	IPLScene scene=nullptr;
	IPLScene world=nullptr;
	IPLInstancedMesh worldInstance=nullptr;
	IPLScene serialScene=nullptr;
	IPLStaticMesh serialMesh=nullptr;
	IPLSimulator simulator=nullptr;
	IPLProbeBatch probes=nullptr;
	std::vector<IPLScene> models;
	struct Object { IPLInstancedMesh mesh=nullptr; bool enabled=false; int model=-1; IPLMatrix4x4 transform={}; };
	std::map<int,Object> objects;
	struct Source {
		IPLSource source=nullptr;
		IPLDirectEffect direct=nullptr;
		IPLPathEffect path=nullptr;
		IPLReflectionEffect reflection=nullptr;
		IPLAmbisonicsDecodeEffect decode=nullptr;
		IPLSimulationOutputs output={};
		IPLDirectEffectParams smooth={};
		float pathSH[Channels]={};
		bool active=false, reflected=false, hasReflection=false, hasPath=false, fresh=true;
		bool discardPending=false, reflectionUpdated=false;
		int tail=0;
	};
	std::array<Source,Voices+1> sources;
	IPLAudioSettings audio={};
	IPLCoordinateSpace3 listener={};
	std::array<float,Block> room={};
	std::array<float,Block> indirectLeft={},indirectRight={};
	Info info;
	unsigned worldTriangles=0;
	std::future<void> job;
	std::atomic<int> reflectionMs{0};
	std::atomic<int> scenePeakUs{0};
	std::array<bool,Voices+1> pending={};
	bool cachedRoom=true, pendingBaked=false;
	IPLVector3 bakedOrigin={};
	bool ready=false;
	bool sceneDirty=false;
	Impl(int rate) {
		audio={rate,Block};
		IPLContextSettings contextSettings={}; contextSettings.version=STEAMAUDIO_VERSION;
		if (iplContextCreate(&contextSettings,&context)!=IPL_STATUS_SUCCESS) return;
		IPLHRTFSettings hrtfSettings={}; hrtfSettings.type=IPL_HRTFTYPE_DEFAULT; hrtfSettings.volume=1;
		if(iplHRTFCreate(context,&audio,&hrtfSettings,&hrtf)!=IPL_STATUS_SUCCESS) return;
		IPLEmbreeDeviceSettings embreeSettings={};
		if(iplEmbreeDeviceCreate(context,&embreeSettings,&embree)==IPL_STATUS_SUCCESS) sceneType=IPL_SCENETYPE_EMBREE;
		IPLSceneSettings sceneSettings={}; sceneSettings.type=sceneType; sceneSettings.embreeDevice=embree;
		if (iplSceneCreate(context,&sceneSettings,&scene)!=IPL_STATUS_SUCCESS) return;
		if (iplSceneCreate(context,&sceneSettings,&world)!=IPL_STATUS_SUCCESS) return;
		sceneSettings.type=IPL_SCENETYPE_DEFAULT; sceneSettings.embreeDevice=nullptr;
		if (iplSceneCreate(context,&sceneSettings,&serialScene)!=IPL_STATUS_SUCCESS) return;
		IPLSimulationSettings settings={}; settings.flags=All; settings.sceneType=sceneType;
		settings.reflectionType=IPL_REFLECTIONEFFECTTYPE_HYBRID;
		settings.maxNumOcclusionSamples=8; settings.maxNumRays=512; settings.numDiffuseSamples=16;
		settings.maxDuration=Duration; settings.maxOrder=Order; settings.maxNumSources=5; settings.numThreads=1;
		settings.numVisSamples=1; settings.samplingRate=rate; settings.frameSize=Block;
		if (iplSimulatorCreate(context,&settings,&simulator)!=IPL_STATUS_SUCCESS) return;
		iplSimulatorSetScene(simulator,scene);
		IPLSourceSettings sourceSettings={}; sourceSettings.flags=All;
		IPLDirectEffectSettings directSettings={}; directSettings.numChannels=1;
		IPLPathEffectSettings pathSettings={}; pathSettings.maxOrder=Order; pathSettings.spatialize=IPL_TRUE;
		pathSettings.speakerLayout.type=IPL_SPEAKERLAYOUTTYPE_STEREO;
		pathSettings.hrtf=hrtf;
		IPLReflectionEffectSettings reflectionSettings={}; reflectionSettings.type=settings.reflectionType;
		reflectionSettings.irSize=int(rate*HybridDuration); reflectionSettings.numChannels=Channels;
		IPLAmbisonicsDecodeEffectSettings decodeSettings={}; decodeSettings.maxOrder=Order;
		decodeSettings.speakerLayout.type=IPL_SPEAKERLAYOUTTYPE_STEREO;
		decodeSettings.hrtf=hrtf;
		for (auto &s : sources) {
			if (iplSourceCreate(simulator,&sourceSettings,&s.source)!=IPL_STATUS_SUCCESS ||
				iplDirectEffectCreate(context,&audio,&directSettings,&s.direct)!=IPL_STATUS_SUCCESS ||
				iplPathEffectCreate(context,&audio,&pathSettings,&s.path)!=IPL_STATUS_SUCCESS ||
				iplReflectionEffectCreate(context,&audio,&reflectionSettings,&s.reflection)!=IPL_STATUS_SUCCESS ||
				iplAmbisonicsDecodeEffectCreate(context,&audio,&decodeSettings,&s.decode)!=IPL_STATUS_SUCCESS) return;
			iplSourceAdd(s.source,simulator);
			s.smooth=NeutralDirect();
			s.output.direct=s.smooth;
			s.output.pathing.shCoeffs=s.pathSH;
		}
		iplSimulatorCommit(simulator); ready=true;
	}
	~Impl() {
		if(job.valid()) job.wait();
		for (auto &s : sources) {
			if(s.decode) iplAmbisonicsDecodeEffectRelease(&s.decode);
			if(s.reflection) iplReflectionEffectRelease(&s.reflection);
			if(s.path) iplPathEffectRelease(&s.path);
			if(s.direct) iplDirectEffectRelease(&s.direct);
			if(s.source) iplSourceRelease(&s.source);
		}
		if(simulator) iplSimulatorRelease(&simulator);
		if(probes) iplProbeBatchRelease(&probes);
		for(auto &o:objects) if(o.second.mesh) iplInstancedMeshRelease(&o.second.mesh);
		if(worldInstance) iplInstancedMeshRelease(&worldInstance);
		if(scene) iplSceneRelease(&scene);
		if(world) iplSceneRelease(&world);
		if(serialMesh) iplStaticMeshRelease(&serialMesh);
		if(serialScene) iplSceneRelease(&serialScene);
		for(auto &model:models) if(model) iplSceneRelease(&model);
		if(hrtf) iplHRTFRelease(&hrtf);
		if(embree) iplEmbreeDeviceRelease(&embree);
		if(context) iplContextRelease(&context);
	}
	bool AttachWorld() {
		// Door movement must rebuild only the instance hierarchy, not the static BSP.
		IPLInstancedMeshSettings settings={}; settings.subScene=world;
		for(int i=0;i<4;++i) settings.transform.elements[i][i]=1;
		if(iplInstancedMeshCreate(scene,&settings,&worldInstance)!=IPL_STATUS_SUCCESS) return false;
		iplInstancedMeshAdd(worldInstance,scene); iplSceneCommit(scene); return true;
	}
	void Finish() {
		if(!job.valid()) return;
		job.get();
		info.occlusion=info.transmission=1;
		for(int i=0;i<=Voices;++i) {
			auto &s=sources[i];
			IPLSimulationOutputs output={}; iplSourceGetOutputs(s.source,All,&output);
			if(s.discardPending) {
				// Consume a published IR before the SDK can publish the replacement.
				if(pending[i]) {
					s.output.reflections=output.reflections; s.hasReflection=true; s.tail=1;
					float silence[Block]={},left[Block]={},right[Block]={};
					Reflect(s,silence,0,left,right); iplReflectionEffectReset(s.reflection);
					s.hasReflection=false; s.tail=0;
				}
				s.discardPending=false; continue;
			}
			s.output.direct=output.direct; s.output.pathing=output.pathing;
			std::copy(output.pathing.shCoeffs,output.pathing.shCoeffs+Channels,s.pathSH); s.output.pathing.shCoeffs=s.pathSH;
			if(pending[i]) { s.output.reflections=output.reflections; s.hasReflection=true; s.reflectionUpdated=true; }
			if(i<Voices && s.active) {info.occlusion=std::min(info.occlusion,output.direct.occlusion); info.transmission=std::min(info.transmission,output.direct.transmission[1]);}
		}
		info.reverb=sources[Voices].output.reflections.reverbTimes[1];
		// Sparse probes can miss small rooms. Use live reverb there until the listener moves.
		if(pendingBaked && info.reverb<0.01f) cachedRoom=false;
	}
	void Reflect(Source &s,float *input,float gain,float *left,float *right) {
		if (!s.hasReflection) return;
		// The remaining IR is silent: the parametric tail starts at the hybrid transition.
		auto params=s.output.reflections; params.type=IPL_REFLECTIONEFFECTTYPE_HYBRID; params.numChannels=Channels; params.irSize=int(audio.samplingRate*HybridDuration);
		float decay=Duration;
		for(float &time:params.reverbTimes) { time=std::max(0.1f,std::min(6.0f,time)); decay=std::max(decay,time); }
		bool signal=false; for(int i=0;i<Block;++i) signal|=std::abs(input[i])>0.000001f;
		if(signal) s.tail=int(audio.samplingRate*decay/Block)+1;
		const bool audible=s.tail>0;
		// Drain new IRs even while quiet so the first shot uses the current room.
		if(!audible && !s.reflectionUpdated) return;
		s.reflectionUpdated=false;
		if(audible) --s.tail;
		float reflected[Channels][Block]={},stereo[2][Block]={};
		float *in[]={input},*ambi[]={reflected[0],reflected[1],reflected[2],reflected[3]},*out[]={stereo[0],stereo[1]};
		IPLAudioBuffer ib={1,Block,in},ab={Channels,Block,ambi},ob={2,Block,out};
		iplReflectionEffectApply(s.reflection,&params,&ib,&ab,nullptr);
		if(!audible) return;
		IPLAmbisonicsDecodeEffectParams decode={}; decode.order=Order; decode.orientation=listener; decode.binaural=IPL_FALSE;
		iplAmbisonicsDecodeEffectApply(s.decode,&decode,&ab,&ob);
		for(int i=0;i<Block;++i) { left[i]+=stereo[0][i]*gain; right[i]+=stereo[1][i]*gain; }
	}
};
Engine::Engine(int rate):p(new Impl(rate)) {}
Engine::~Engine()=default;
bool Engine::Ready() const {return p->ready;}
bool Engine::Busy() const {return p->job.valid() && p->job.wait_for(std::chrono::seconds(0))!=std::future_status::ready;}
void Engine::Wait() {p->Finish();}
bool Engine::AddModel(const Mesh &mesh,const std::vector<IPLMaterial> &materials) {
	IPLScene sub=nullptr;
	IPLSceneSettings settings={}; settings.type=p->sceneType; settings.embreeDevice=p->embree;
	if(iplSceneCreate(p->context,&settings,&sub)!=IPL_STATUS_SUCCESS) return false;
	p->models.push_back(sub);
	if(mesh.triangles.empty()) return true;
	IPLStaticMeshSettings ms={}; ms.numVertices=int(mesh.vertices.size()); ms.numTriangles=int(mesh.triangles.size()); ms.numMaterials=int(materials.size());
	ms.vertices=const_cast<IPLVector3*>(mesh.vertices.data()); ms.triangles=const_cast<IPLTriangle*>(mesh.triangles.data());
	ms.materialIndices=const_cast<int*>(mesh.materials.data()); ms.materials=const_cast<IPLMaterial*>(materials.data());
	IPLStaticMesh object=nullptr;
	IPLScene target=p->models.size()==1 ? p->world : sub;
	if(iplStaticMeshCreate(target,&ms,&object)!=IPL_STATUS_SUCCESS) return false;
	iplStaticMeshAdd(object,target); iplStaticMeshRelease(&object); iplSceneCommit(target);
	if(p->models.size()==1) {
		if(!p->AttachWorld()) return false;
		p->worldTriangles=unsigned(mesh.triangles.size());
		// The SDK serializes its default scene format, not Embree's acceleration data.
		if(iplStaticMeshCreate(p->serialScene,&ms,&p->serialMesh)!=IPL_STATUS_SUCCESS) return false;
	}
	p->info.triangles+=ms.numTriangles;
	return true;
}
void Engine::Object(int entity,int model,const IPLMatrix4x4 &transform,bool enabled) {
	if(model<=0 || model>=int(p->models.size())) return;
	auto &o=p->objects[entity];
	if(o.model!=model) {
		if(o.mesh) { if(o.enabled) {iplInstancedMeshRemove(o.mesh,p->scene); p->sceneDirty=true;} iplInstancedMeshRelease(&o.mesh); }
		o={}; o.model=model;
		IPLInstancedMeshSettings settings={}; settings.subScene=p->models[model]; settings.transform=transform;
		if(iplInstancedMeshCreate(p->scene,&settings,&o.mesh)!=IPL_STATUS_SUCCESS) return;
	}
	if(!o.mesh) return;
	if(enabled!=o.enabled) { if(enabled) iplInstancedMeshAdd(o.mesh,p->scene); else iplInstancedMeshRemove(o.mesh,p->scene); o.enabled=enabled; p->sceneDirty=true; }
	if(enabled && std::memcmp(&o.transform,&transform,sizeof(transform))) { iplInstancedMeshUpdateTransform(o.mesh,p->scene,transform); o.transform=transform; p->sceneDirty=true; }
}
bool Engine::LoadWorld(const std::vector<unsigned char> &data) {
	if(data.size()<=sizeof(unsigned)) return false;
	unsigned triangles; std::memcpy(&triangles,data.data(),sizeof(triangles));
	if(triangles>2000000) return false;
	IPLSerializedObject object=nullptr; IPLSerializedObjectSettings os={}; os.data=const_cast<unsigned char*>(data.data()+sizeof(unsigned)); os.size=data.size()-sizeof(unsigned);
	if(iplSerializedObjectCreate(p->context,&os,&object)!=IPL_STATUS_SUCCESS) return false;
	IPLStaticMesh loaded=nullptr;
	bool ok=iplStaticMeshLoad(p->world,object,nullptr,nullptr,&loaded)==IPL_STATUS_SUCCESS &&
		iplStaticMeshLoad(p->serialScene,object,nullptr,nullptr,&p->serialMesh)==IPL_STATUS_SUCCESS;
	iplSerializedObjectRelease(&object);
	if(ok) { iplStaticMeshAdd(loaded,p->world); iplSceneCommit(p->world); ok=p->AttachWorld(); p->worldTriangles=triangles; p->info.triangles=int(triangles); }
	if(loaded) iplStaticMeshRelease(&loaded);
	return ok;
}
std::vector<unsigned char> Engine::SaveWorld() {
	if(!p->serialMesh) return {};
	IPLSerializedObject object=nullptr; IPLSerializedObjectSettings settings={};
	if(iplSerializedObjectCreate(p->context,&settings,&object)!=IPL_STATUS_SUCCESS) return {};
	iplStaticMeshSave(p->serialMesh,object);
	const auto *data=iplSerializedObjectGetData(object);
	std::vector<unsigned char> result(sizeof(unsigned)+iplSerializedObjectGetSize(object));
	std::memcpy(result.data(),&p->worldTriangles,sizeof(unsigned)); std::memcpy(result.data()+sizeof(unsigned),data,result.size()-sizeof(unsigned));
	iplSerializedObjectRelease(&object); return result;
}
bool Engine::LoadProbes(const std::vector<unsigned char> &data) {
	if(data.empty() || p->probes) return false;
	IPLSerializedObject object=nullptr; IPLSerializedObjectSettings settings={}; settings.data=const_cast<unsigned char*>(data.data()); settings.size=data.size();
	if(iplSerializedObjectCreate(p->context,&settings,&object)!=IPL_STATUS_SUCCESS) return false;
	const bool ok=iplProbeBatchLoad(p->context,object,&p->probes)==IPL_STATUS_SUCCESS;
	iplSerializedObjectRelease(&object);
	if(ok) { iplProbeBatchCommit(p->probes); p->info.probes=iplProbeBatchGetNumProbes(p->probes); iplSimulatorAddProbeBatch(p->simulator,p->probes); iplSimulatorCommit(p->simulator); }
	return ok;
}
bool Engine::Bake(const IPLVector3 &low,const IPLVector3 &high) {
	Wait();
	if(p->probes) return true;
	// Bake the open static space; runtime path validation handles moving barriers.
	IPLProbeArray probes=nullptr;
	if(iplProbeArrayCreate(p->context,&probes)!=IPL_STATUS_SUCCESS) return false;
	IPLProbeGenerationParams gen={}; gen.type=IPL_PROBEGENERATIONTYPE_UNIFORMFLOOR; gen.spacing=4; gen.height=1.5f;
	gen.transform.elements[0][0]=high.x-low.x; gen.transform.elements[1][1]=high.y-low.y; gen.transform.elements[2][2]=high.z-low.z;
	gen.transform.elements[0][3]=(high.x+low.x)/2; gen.transform.elements[1][3]=(high.y+low.y)/2; gen.transform.elements[2][3]=(high.z+low.z)/2; gen.transform.elements[3][3]=1;
	do { iplProbeArrayGenerateProbes(probes,p->world,&gen); gen.spacing*=1.5f; } while(iplProbeArrayGetNumProbes(probes)>256);
	if(!iplProbeArrayGetNumProbes(probes) || iplProbeBatchCreate(p->context,&p->probes)!=IPL_STATUS_SUCCESS) { iplProbeArrayRelease(&probes); return false; }
	iplProbeBatchAddProbeArray(p->probes,probes); iplProbeArrayRelease(&probes); iplProbeBatchCommit(p->probes);
	IPLPathBakeParams path={}; path.scene=p->world; path.probeBatch=p->probes; path.identifier.type=IPL_BAKEDDATATYPE_PATHING;
	path.identifier.variation=IPL_BAKEDDATAVARIATION_DYNAMIC; path.numSamples=1; path.radius=0.25f; path.threshold=0.5f; path.visRange=20; path.pathRange=100; path.numThreads=1;
	auto progress=[](float,void*) {};
	iplPathBakerBake(p->context,&path,progress,nullptr);
	IPLReflectionsBakeParams reverb={}; reverb.scene=p->world; reverb.probeBatch=p->probes; reverb.sceneType=p->sceneType;
	reverb.identifier.type=IPL_BAKEDDATATYPE_REFLECTIONS; reverb.identifier.variation=IPL_BAKEDDATAVARIATION_REVERB;
	reverb.bakeFlags=static_cast<IPLReflectionsBakeFlags>(IPL_REFLECTIONSBAKEFLAGS_BAKECONVOLUTION|IPL_REFLECTIONSBAKEFLAGS_BAKEPARAMETRIC);
	reverb.numRays=2048; reverb.numDiffuseSamples=16; reverb.numBounces=16; reverb.simulatedDuration=Duration; reverb.savedDuration=Duration;
	reverb.order=Order; reverb.numThreads=1; reverb.irradianceMinDistance=1;
	iplReflectionsBakerBake(p->context,&reverb,progress,nullptr);
	p->info.probes=iplProbeBatchGetNumProbes(p->probes);
	iplSimulatorAddProbeBatch(p->simulator,p->probes); iplSimulatorCommit(p->simulator); return true;
}
std::vector<unsigned char> Engine::SaveProbes() {
	if(!p->probes) return {};
	IPLSerializedObject object=nullptr; IPLSerializedObjectSettings settings={};
	if(iplSerializedObjectCreate(p->context,&settings,&object)!=IPL_STATUS_SUCCESS) return {};
	iplProbeBatchSave(p->probes,object); const auto *data=iplSerializedObjectGetData(object);
	std::vector<unsigned char> result(data,data+iplSerializedObjectGetSize(object)); iplSerializedObjectRelease(&object); return result;
}
void Engine::Update(const std::array<Voice,Voices> &voices,const IPLCoordinateSpace3 &listener,bool reflections,bool pathing,bool simulateReflections) {
	if(Busy()) return;
	p->Finish();
	const float dx=listener.origin.x-p->bakedOrigin.x,dy=listener.origin.y-p->bakedOrigin.y,dz=listener.origin.z-p->bakedOrigin.z;
	if(dx*dx+dy*dy+dz*dz>1) p->cachedRoom=true;
	p->listener=listener; p->info.active=p->info.reflected=0;
	std::array<int,Voices> order; std::iota(order.begin(),order.end(),0);
	std::stable_sort(order.begin(),order.end(),[&](int a,int b){return voices[a].priority>voices[b].priority;});
	for(auto &s:p->sources) { s.reflected=false; s.hasPath=false; }
	for(int j=0;j<4;++j) p->sources[order[j]].reflected=reflections && voices[order[j]].active;
	for(int i=0;i<=Voices;++i) {
		auto &s=p->sources[i]; s.active=i==Voices || voices[i].active;
		IPLSimulationInputs in={}; in.flags=s.active ? IPL_SIMULATIONFLAGS_DIRECT : static_cast<IPLSimulationFlags>(0);
		in.source=listener; if(i<Voices) in.source.origin=voices[i].position;
		in.directFlags=static_cast<IPLDirectSimulationFlags>(s.active && i<Voices ? IPL_DIRECTSIMULATIONFLAGS_OCCLUSION|IPL_DIRECTSIMULATIONFLAGS_TRANSMISSION|IPL_DIRECTSIMULATIONFLAGS_AIRABSORPTION : 0);
		in.occlusionType=IPL_OCCLUSIONTYPE_VOLUMETRIC; in.occlusionRadius=0.2f; in.numOcclusionSamples=8; in.numTransmissionRays=8;
		in.reverbScale[0]=in.reverbScale[1]=in.reverbScale[2]=1;
		in.hybridReverbTransitionTime=HybridDuration; in.hybridReverbOverlapPercent=0.25f;
		if(s.active && reflections && (s.reflected || i==Voices)) { in.flags=static_cast<IPLSimulationFlags>(in.flags|IPL_SIMULATIONFLAGS_REFLECTIONS); ++p->info.reflected; }
		if(i==Voices && p->probes && p->cachedRoom) { in.baked=IPL_TRUE; in.bakedDataIdentifier.type=IPL_BAKEDDATATYPE_REFLECTIONS; in.bakedDataIdentifier.variation=IPL_BAKEDDATAVARIATION_REVERB; }
		if(i<Voices && s.active && pathing && p->probes) {
			in.flags=static_cast<IPLSimulationFlags>(in.flags|IPL_SIMULATIONFLAGS_PATHING); in.pathingProbes=p->probes;
			in.visRadius=0.25f; in.visThreshold=0.5f; in.visRange=20; in.pathingOrder=Order; in.enableValidation=IPL_TRUE; in.findAlternatePaths=IPL_TRUE;
			s.hasPath=true;
		}
		iplSourceSetInputs(s.source,All,&in);
		if(i<Voices && s.active) ++p->info.active;
	}
	IPLSimulationSharedInputs shared={}; shared.listener=listener; shared.numRays=512; shared.numBounces=8; shared.duration=Duration; shared.order=Order; shared.irradianceMinDistance=1;
	iplSimulatorSetSharedInputs(p->simulator,All,&shared);
	const bool reflect=reflections && simulateReflections;
	for(int i=0;i<=Voices;++i) { p->pending[i]=reflect && (p->sources[i].reflected || i==Voices); if(!reflections) p->sources[i].hasReflection=false; }
	p->pendingBaked=reflect && p->probes && p->cachedRoom; if(p->pendingBaked) p->bakedOrigin=listener.origin;
	auto *state=p.get();
	p->job=std::async(std::launch::async,[state,reflect,pathing] {
		if(state->sceneDirty) {
			const auto start=std::chrono::steady_clock::now();
			iplSceneCommit(state->scene); state->sceneDirty=false;
			const int elapsed=int(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count());
			state->scenePeakUs=std::max(state->scenePeakUs.load(),elapsed);
		}
		iplSimulatorRunDirect(state->simulator);
		if(pathing && state->probes) iplSimulatorRunPathing(state->simulator);
		if(reflect) {
			const auto start=std::chrono::steady_clock::now(); iplSimulatorRunReflections(state->simulator);
			state->reflectionMs=int(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count());
		}
	});
}
void Engine::ResetVoice(int index) {
	auto &s=p->sources[index]; iplDirectEffectReset(s.direct); iplPathEffectReset(s.path); iplReflectionEffectReset(s.reflection); s.tail=0; s.hasReflection=false; s.hasPath=false; s.fresh=true;
	// Effects are mixer-owned. Do not wait for, or consume, the previous occupant's simulation.
	s.discardPending=p->job.valid(); s.reflected=false; s.output.direct=s.smooth=NeutralDirect();
	s.reflectionUpdated=false;
	std::fill(s.pathSH,s.pathSH+Channels,0); s.output.pathing.shCoeffs=s.pathSH;
}
void Engine::Begin() { p->room.fill(0); p->indirectLeft.fill(0); p->indirectRight.fill(0); }
void Engine::Mix(int index,const float *input,float left,float right,float gain,float wet,float *outLeft,float *outRight,float transmissionFloor,float reverbSend) {
	auto &s=p->sources[index]; float filtered[Block]={}; float *in[]={const_cast<float*>(input)},*out[]={filtered};
	float *wetLeft=p->indirectLeft.data(),*wetRight=p->indirectRight.data();
	IPLAudioBuffer ib={1,Block,in},ob={1,Block,out};
	auto &smooth=s.smooth; const auto &target=s.output.direct;
	if(s.fresh) { smooth=target; s.fresh=false; }
	const float blend=1-std::exp(-Block/(0.08f*p->audio.samplingRate));
	smooth.occlusion+=(target.occlusion-smooth.occlusion)*blend;
	const float floor[]={std::min(1.0f,2*transmissionFloor),transmissionFloor,.25f*transmissionFloor};
	for(int b=0;b<3;++b) {
		smooth.airAbsorption[b]+=(target.airAbsorption[b]-smooth.airAbsorption[b])*blend;
		const float transmitted=floor[b]+(1-floor[b])*target.transmission[b];
		smooth.transmission[b]+=(transmitted-smooth.transmission[b])*blend;
	}
	smooth.flags=static_cast<IPLDirectEffectFlags>(IPL_DIRECTEFFECTFLAGS_APPLYOCCLUSION|IPL_DIRECTEFFECTFLAGS_APPLYTRANSMISSION|IPL_DIRECTEFFECTFLAGS_APPLYAIRABSORPTION);
	smooth.transmissionType=IPL_TRANSMISSIONTYPE_FREQDEPENDENT;
	iplDirectEffectApply(s.direct,&smooth,&ib,&ob);
	for(int i=0;i<Block;++i) { outLeft[i]+=filtered[i]*left; outRight[i]+=filtered[i]*right; }
	if(s.hasPath && smooth.occlusion<0.99f) {
		float stereo[2][Block]={}; float *channels[]={stereo[0],stereo[1]}; IPLAudioBuffer pathOut={2,Block,channels};
		auto params=s.output.pathing; params.order=Order; params.binaural=IPL_FALSE; params.listener=p->listener; params.hrtf=p->hrtf;
		iplPathEffectApply(s.path,&params,&ib,&pathOut);
		for(int i=0;i<Block;++i) { wetLeft[i]+=stereo[0][i]*gain*(1-smooth.occlusion); wetRight[i]+=stereo[1][i]*gain*(1-smooth.occlusion); }
	}
	if(s.reflected && s.hasReflection) p->Reflect(s,in[0],gain*wet*reverbSend,wetLeft,wetRight);
	else {
		float silence[Block]={};
		if(s.hasReflection && (s.tail>0 || s.reflectionUpdated)) p->Reflect(s,silence,gain*wet*reverbSend,wetLeft,wetRight);
		// A listener-room reverb must not bypass a barrier or the source's distance falloff.
		const float roomGain=std::min(gain,std::hypot(left,right))*reverbSend;
		for(int i=0;i<Block;++i) p->room[i]+=filtered[i]*roomGain;
	}
}
void Engine::End(float wet,float *left,float *right) {
	p->Reflect(p->sources[Voices],p->room.data(),wet,p->indirectLeft.data(),p->indirectRight.data());
	for(int i=0;i<Block;++i) { left[i]+=p->indirectLeft[i]; right[i]+=p->indirectRight[i]; }
}
void Engine::Indirect(float *left,float *right) const {
	std::copy(p->indirectLeft.begin(),p->indirectLeft.end(),left);
	std::copy(p->indirectRight.begin(),p->indirectRight.end(),right);
}
Info Engine::Status() const { auto info=p->info; info.reflectionMs=p->reflectionMs.load(); info.scenePeakUs=p->scenePeakUs.load(); return info; }
IPLDirectEffectParams Engine::DirectParams(int index) const { return p->sources[index].smooth; }
}
