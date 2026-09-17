// SPDX-License-Identifier: GPL-2.0-or-later
#include "../server/exe_headers.h"
#include "snd_local.h"
#include "snd_steam.h"
#ifdef USE_STEAM_AUDIO
#include "sound/steam_audio.h"
#include "sdl/sdl_sound.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

extern vec3_t s_entityPosition[MAX_GENTITIES];
namespace {
constexpr float Metres=1.0f/32;
constexpr unsigned CacheVersion=3;
std::unique_ptr<SteamSound::Engine> engine;
cvar_t *enabled,*reflections,*pathing,*wet,*cache,*transmission;
std::string loadedMap;
unsigned mapChecksum=0;
int serverId=0,lastUpdate=0,lastReflection=0,simulationMs=0,mixedBlocks=0;
bool sceneCache=false,probeCache=false;
IPLVector3 mapLow={},mapHigh={};
std::map<int,int> objects;
std::vector<short> recording;
int recordFrames=0;
int recordEnd=0,recordOverlaps=0,recordGaps=0;
std::chrono::steady_clock::time_point mixStart;
int mixPeakUs=0,mixCalls=0,mixEnd=0,underrunFrames=0;
int bufferClears=0;
struct Slot {
	sfx_t *sound=nullptr;
	int entity=0,start=0;
	bool loop=false,active=false;
	float input[SteamSound::Block]={},left=0,right=0,gain=0;
};
std::array<Slot,SteamSound::Voices> slots;
IPLVector3 Position(const float *p) { return {p[0]*Metres,p[2]*Metres,-p[1]*Metres}; }
IPLVector3 Direction(const float *p) { return {p[0],p[2],-p[1]}; }
bool Eligible(const channel_t &ch) {
	return ch.thesfx && ch.entchannel!=CHAN_LOCAL && ch.entchannel!=CHAN_LOCAL_SOUND &&
		ch.entchannel!=CHAN_VOICE_GLOBAL && ch.entchannel!=CHAN_ANNOUNCER && ch.entchannel!=CHAN_MUSIC;
}
std::string CacheName(const char *kind) { return va("cache/steamaudio/%08x-v%u-sdk%x.%s",mapChecksum,CacheVersion,STEAMAUDIO_VERSION,kind); }
std::vector<unsigned char> ReadCache(const char *kind) {
	if(!cache->integer) return {};
	const auto name=CacheName(kind);
	const long size=FS_ReadFile(name.c_str(),nullptr);
	if(size<16 || size>64*1024*1024) return {};
	void *buffer=nullptr; if(FS_ReadFile(name.c_str(),&buffer)!=size || !buffer) { if(buffer) FS_FreeFile(buffer); return {}; }
	unsigned header[4]; memcpy(header,buffer,sizeof(header));
	const auto *data=static_cast<const unsigned char*>(buffer)+sizeof(header);
	std::vector<unsigned char> result;
	if(header[0]==0x53414348 && header[1]==mapChecksum && header[2]==size-sizeof(header) && header[3]==Com_BlockChecksum(data,int(header[2]))) result.assign(data,data+header[2]);
	FS_FreeFile(buffer); return result;
}
void WriteCache(const char *kind,const std::vector<unsigned char> &data) {
	if(!cache->integer || data.empty() || data.size()>64*1024*1024-16) return;
	const unsigned header[]={0x53414348,mapChecksum,unsigned(data.size()),Com_BlockChecksum(data.data(),int(data.size()))};
	std::vector<unsigned char> bytes(sizeof(header)+data.size()); memcpy(bytes.data(),header,sizeof(header)); memcpy(bytes.data()+sizeof(header),data.data(),data.size());
	FS_WriteFile(CacheName(kind).c_str(),bytes.data(),int(bytes.size()));
}
std::vector<IPLMaterial> Materials() {
	std::vector<IPLMaterial> m(32,{{0.10f,0.20f,0.30f},0.5f,{0.02f,0.01f,0.005f}});
	m[MATERIAL_CONCRETE]={{0.05f,0.07f,0.08f},0.2f,{0.015f,0.002f,0.001f}};
	m[MATERIAL_ROCK]={{0.13f,0.20f,0.24f},0.7f,{0.015f,0.002f,0.001f}};
	m[MATERIAL_SOLIDMETAL]={{0.20f,0.07f,0.06f},0.1f,{0.10f,0.015f,0.005f}};
	m[MATERIAL_HOLLOWMETAL]={{0.20f,0.07f,0.06f},0.2f,{0.20f,0.025f,0.01f}};
	m[MATERIAL_SOLIDWOOD]={{0.11f,0.07f,0.06f},0.4f,{0.07f,0.014f,0.005f}};
	m[MATERIAL_HOLLOWWOOD]=m[MATERIAL_SOLIDWOOD];
	m[MATERIAL_GLASS]={{0.06f,0.03f,0.02f},0.05f,{0.06f,0.044f,0.011f}};
	m[MATERIAL_BPGLASS]=m[MATERIAL_SHATTERGLASS]=m[MATERIAL_GLASS];
	m[MATERIAL_CARPET]={{0.24f,0.69f,0.73f},0.8f,{0.02f,0.005f,0.003f}};
	m[MATERIAL_FABRIC]=m[MATERIAL_CANVAS]=m[MATERIAL_CARPET];
	m[MATERIAL_DIRT]=m[MATERIAL_SAND]=m[MATERIAL_GRAVEL]={{0.60f,0.70f,0.80f},0.9f,{0.031f,0.012f,0.008f}};
	return m;
}
template<class T> bool Lump(const byte *data,int length,const dheader_t &header,int index,std::vector<T> &out) {
	const int offset=LittleLong(header.lumps[index].fileofs),size=LittleLong(header.lumps[index].filelen);
	if(offset<0 || size<0 || offset>length || size>length-offset || size%sizeof(T)) return false;
	out.resize(size/sizeof(T)); if(size) memcpy(out.data(),data+offset,size); return true;
}
using Point=std::array<float,3>;
void Triangle(SteamSound::Mesh &mesh,const Point &a,const Point &b,const Point &c,int material) {
	for(const auto &p:{a,b,c}) for(float v:p) if(!std::isfinite(v) || std::abs(v)>MAX_WORLD_COORD) return;
	const int n=int(mesh.vertices.size()); mesh.vertices.insert(mesh.vertices.end(),{Position(a.data()),Position(b.data()),Position(c.data())});
	mesh.triangles.push_back({{n,n+1,n+2}}); mesh.materials.push_back(material&MATERIAL_MASK);
}
bool LoadMap() {
	void *buffer=nullptr; const int length=int(FS_ReadFile(loadedMap.c_str(),&buffer));
	if(length<int(sizeof(dheader_t))) { if(buffer) FS_FreeFile(buffer); return false; }
	mapChecksum=Com_BlockChecksum(buffer,length);
	dheader_t header; memcpy(&header,buffer,sizeof(header)); const auto *data=static_cast<const byte*>(buffer);
	std::vector<dmodel_t> models; std::vector<dbrush_t> brushes; std::vector<dbrushside_t> sides;
	std::vector<dplane_t> planes; std::vector<dshader_t> shaders; std::vector<dsurface_t> surfaces; std::vector<drawVert_t> vertices;
	const bool ok=LittleLong(header.ident)==BSP_IDENT && LittleLong(header.version)==BSP_VERSION &&
		Lump(data,length,header,LUMP_MODELS,models) && Lump(data,length,header,LUMP_BRUSHES,brushes) &&
		Lump(data,length,header,LUMP_BRUSHSIDES,sides) && Lump(data,length,header,LUMP_PLANES,planes) &&
		Lump(data,length,header,LUMP_SHADERS,shaders) && Lump(data,length,header,LUMP_SURFACES,surfaces) && Lump(data,length,header,LUMP_DRAWVERTS,vertices);
	FS_FreeFile(buffer); if(!ok || models.empty()) return false;
	vec3_t low,high; for(int j=0;j<3;++j) {low[j]=LittleFloat(models[0].mins[j]);high[j]=LittleFloat(models[0].maxs[j]);}
	mapLow=Position(low); mapHigh=Position(high); std::swap(mapLow.z,mapHigh.z);
	sceneCache=engine->LoadWorld(ReadCache("scene"));
	const auto materials=Materials();
	for(size_t model=0;model<models.size();++model) {
		SteamSound::Mesh mesh;
		if(model==0 && sceneCache) { if(!engine->AddModel(mesh,materials)) return false; continue; }
		const auto &m=models[model]; const int first=LittleLong(m.firstBrush),count=LittleLong(m.numBrushes);
		if(first<0 || count<0 || first>int(brushes.size()) || count>int(brushes.size())-first) return false;
		for(int i=first;i<first+count;++i) {
			const auto &brush=brushes[i]; const int shader=LittleLong(brush.shaderNum),start=LittleLong(brush.firstSide),num=LittleLong(brush.numSides);
			if(shader<0 || shader>=int(shaders.size()) || start<0 || num<0 || num>128 || start>int(sides.size()) || num>int(sides.size())-start) return false;
			if(!(LittleLong(shaders[shader].contentFlags)&CONTENTS_SOLID)) continue;
			for(int f=0;f<num;++f) {
				const auto &side=sides[start+f]; const int plane=LittleLong(side.planeNum),surface=LittleLong(side.shaderNum);
				if(plane<0 || plane>=int(planes.size()) || surface<0 || surface>=int(shaders.size())) return false;
				const int flags=LittleLong(shaders[surface].surfaceFlags); if(flags&SURF_SKY) continue;
				Point n,u,v,origin; for(int j=0;j<3;++j) n[j]=LittleFloat(planes[plane].normal[j]);
				Point axis={{0,0,1}}; if(std::abs(n[2])>0.9f) axis={{1,0,0}};
				CrossProduct(axis.data(),n.data(),u.data()); if(VectorNormalize(u.data())==0) return false; CrossProduct(n.data(),u.data(),v.data());
				std::vector<Point> polygon(4);
				for(int j=0;j<3;++j) { origin[j]=n[j]*LittleFloat(planes[plane].dist); for(int k=0;k<4;++k) polygon[k][j]=origin[j]+MAX_WORLD_COORD*((k==0||k==3 ? 1 : -1)*u[j]+(k<2 ? 1 : -1)*v[j]); }
				for(int clip=0;clip<num && !polygon.empty();++clip) {
					if(clip==f) continue;
					const int pi=LittleLong(sides[start+clip].planeNum); if(pi<0 || pi>=int(planes.size())) return false;
					const auto &p=planes[pi]; std::vector<Point> clipped;
					auto distance=[&](const Point &a){return a[0]*LittleFloat(p.normal[0])+a[1]*LittleFloat(p.normal[1])+a[2]*LittleFloat(p.normal[2])-LittleFloat(p.dist);};
					for(size_t k=0;k<polygon.size();++k) {
						const auto &a=polygon[k],&b=polygon[(k+1)%polygon.size()]; const float da=distance(a),db=distance(b);
						if(da<=0.01f) clipped.push_back(a);
						if((da<=0.01f)!=(db<=0.01f)) {Point hit; for(int j=0;j<3;++j) hit[j]=a[j]+(b[j]-a[j])*da/(da-db); clipped.push_back(hit);}
					}
					polygon.swap(clipped);
				}
				for(size_t k=1;k+1<polygon.size();++k) Triangle(mesh,polygon[0],polygon[k],polygon[k+1],flags);
			}
		}
		// Curved collision surfaces are not represented by convex brushes.
		const int begin=LittleLong(m.firstSurface),surfaceCount=LittleLong(m.numSurfaces);
		if(begin<0 || surfaceCount<0 || begin>int(surfaces.size()) || surfaceCount>int(surfaces.size())-begin) return false;
		const int end=begin+surfaceCount;
		for(int i=begin;i<end;++i) {
			const auto &s=surfaces[i]; if(LittleLong(s.surfaceType)!=MST_PATCH) continue;
			const int shader=LittleLong(s.shaderNum),start=LittleLong(s.firstVert),num=LittleLong(s.numVerts),w=LittleLong(s.patchWidth),h=LittleLong(s.patchHeight);
			if(shader<0 || shader>=int(shaders.size()) || start<0 || num<0 || start>int(vertices.size()) || num>int(vertices.size())-start || w<3 || h<3 || w>64 || h>64 || w*h!=num) return false;
			const int flags=LittleLong(shaders[shader].surfaceFlags); if(!(LittleLong(shaders[shader].contentFlags)&CONTENTS_SOLID) || (flags&SURF_SKY)) continue;
			for(int y=0;y<h-2;y+=2) for(int x=0;x<w-2;x+=2) {
				Point grid[5][5]={};
				for(int v=0;v<=4;++v) for(int u=0;u<=4;++u) {
					const float a=u/4.0f,b=v/4.0f,bu[]={(1-a)*(1-a),2*a*(1-a),a*a},bv[]={(1-b)*(1-b),2*b*(1-b),b*b};
					for(int j=0;j<3;++j) for(int k=0;k<3;++k) for(int axis=0;axis<3;++axis) grid[v][u][axis]+=LittleFloat(vertices[start+(y+j)*w+x+k].xyz[axis])*bu[k]*bv[j];
				}
				for(int v=0;v<4;++v) for(int u=0;u<4;++u) { Triangle(mesh,grid[v][u],grid[v][u+1],grid[v+1][u],flags); Triangle(mesh,grid[v+1][u],grid[v][u+1],grid[v+1][u+1],flags); }
			}
		}
		if(!engine->AddModel(mesh,materials)) return false;
	}
	if(!sceneCache) WriteCache("scene",engine->SaveWorld());
	probeCache=engine->LoadProbes(ReadCache("probes")); return true;
}
void Status() {
	const bool reset=Cmd_Argc()==2 && !Q_stricmp(Cmd_Argv(1),"reset");
	if(reset) mixPeakUs=mixCalls=underrunFrames=bufferClears=0;
	const auto device=SNDDMA_GetAudioTiming(reset);
	const auto info=engine ? engine->Status() : SteamSound::Info{};
	Com_Printf("steam_audio active=%d map=%s triangles=%d movers=%d sources=%d reflections=%d probes=%d scene_cache=%d probe_cache=%d occlusion=%.3f transmission=%.3f rt60=%.3f simulation_ms=%d reflection_ms=%d mixed_blocks=%d\n",
		S_SteamActive(),loadedMap.c_str(),info.triangles,int(objects.size()),info.active,info.reflected,info.probes,sceneCache,probeCache,info.occlusion,info.transmission,info.reverb,simulationMs,info.reflectionMs,mixedBlocks);
	Com_Printf("steam_audio timing rate=%d mix_peak_us=%d mix_calls=%d underrun_frames=%d callbacks=%u callback_peak_us=%d lock_peak_us=%d scene_peak_us=%d buffer_clears=%d\n",dma.speed,mixPeakUs,mixCalls,underrunFrames,device.callbacks,device.callbackPeakUs,device.lockPeakUs,info.scenePeakUs,bufferClears);
	if(Cmd_Argc()==2 && !Q_stricmp(Cmd_Argv(1),"sources")) for(const auto &ch:s_channels) {
		if(!ch.thesfx || (!ch.leftvol && !ch.rightvol)) continue;
		bool visible=false;
		for(int i=0;i<cl.frame.numEntities;++i)
			visible|=cl.parseEntities[(cl.frame.parseEntitiesNum+i)&(MAX_PARSE_ENTITIES-1)].number==ch.entnum;
		const float *origin=ch.fixed_origin ? ch.origin : s_entityPosition[Com_Clampi(0,MAX_GENTITIES-1,ch.entnum)];
		const auto direct=engine ? engine->DirectParams(int(&ch-s_channels)) : IPLDirectEffectParams{};
		Com_Printf("steam_source entity=%d loop=%d visible=%d left=%d right=%d solid=%d pos=%.1f,%.1f,%.1f occlusion=%.3f transmission=%.5f,%.5f,%.5f sound=%s\n",ch.entnum,ch.loopSound,visible,ch.leftvol,ch.rightvol,bool(CM_PointContents(origin,0)&CONTENTS_SOLID),origin[0],origin[1],origin[2],direct.occlusion,direct.transmission[0],direct.transmission[1],direct.transmission[2],ch.thesfx->sSoundName);
	}
}
void Bake() {
	if(!S_SteamActive()) { Com_Printf("Steam Audio: load a level with sound enabled before baking.\n"); return; }
	Com_Printf("Steam Audio: baking local path and room probes...\n");
	const int start=Sys_Milliseconds();
	if(engine->Bake(mapLow,mapHigh)) { WriteCache("probes",engine->SaveProbes()); Com_Printf("Steam Audio: baked %d probes in %.1f seconds.\n",engine->Status().probes,(Sys_Milliseconds()-start)/1000.0f); }
	else Com_Printf("Steam Audio: no acoustic probes could be generated.\n");
}
void Emit() {
	if(Cmd_Argc()!=2 && Cmd_Argc()!=5) { Com_Printf("s_steam_emit sound [x y z]\n"); return; }
	vec3_t origin; VectorCopy(listener_origin,origin);
	if(Cmd_Argc()==5) for(int i=0;i<3;++i) {origin[i]=atof(Cmd_Argv(i+2)); if(!std::isfinite(origin[i])) return;}
	S_StartSound(origin,ENTITYNUM_WORLD,CHAN_AUTO,S_RegisterSound(Cmd_Argv(1)));
}
void Record() {
	const float seconds=Cmd_Argc()==2 ? atof(Cmd_Argv(1)) : 3;
	if(!std::isfinite(seconds) || seconds<1 || seconds>10 || dma.speed<=0) { Com_Printf("s_steam_record seconds (1 to 10)\n"); return; }
	recording.clear(); recordFrames=int(seconds*dma.speed); recording.reserve(recordFrames*2);
	recordEnd=-1; recordOverlaps=recordGaps=0;
	Com_Printf("Steam Audio: recording %.1f seconds of the final mix.\n",seconds);
}
void Capture(portable_samplepair_t *output,int count) {
	if(recordFrames<=0) return;
	if(recordEnd>=0) {
		recordOverlaps+=std::max(0,recordEnd-s_paintedtime);
		recordGaps+=std::max(0,s_paintedtime-recordEnd);
	}
	count=std::min(count,recordFrames);
	recordEnd=s_paintedtime+count;
	for(int i=0;i<count;++i) { recording.push_back(short(Com_Clampi(-32768,32767,output[i].left>>8))); recording.push_back(short(Com_Clampi(-32768,32767,output[i].right>>8))); }
	recordFrames-=count; if(recordFrames) return;
	std::vector<byte> wav(44+recording.size()*2);
	memcpy(wav.data(),"RIFF",4); memcpy(wav.data()+8,"WAVEfmt ",8); memcpy(wav.data()+36,"data",4);
	auto word=[&](int offset,unsigned value,int bytes) { for(int j=0;j<bytes;++j) wav[offset+j]=byte(value>>(j*8)); };
	word(4,unsigned(wav.size()-8),4); word(16,16,4); word(20,1,2); word(22,2,2); word(24,dma.speed,4); word(28,dma.speed*4,4); word(32,4,2); word(34,16,2); word(40,unsigned(recording.size()*2),4);
	memcpy(wav.data()+44,recording.data(),recording.size()*2);
	const auto name=std::string(va("captures/steam-audio-%d.wav",Sys_Milliseconds())); FS_WriteFile(name.c_str(),wav.data(),int(wav.size())); recording.clear();
	Com_Printf("Steam Audio capture: %s\n",name.c_str());
	Com_Printf("Steam Audio capture continuity: overlap_frames=%d gap_frames=%d\n",recordOverlaps,recordGaps);
}
}
void S_SteamInit() {
	enabled=Cvar_Get("s_steamAudio","1",CVAR_ARCHIVE); Cvar_CheckRange(enabled,0,1,qtrue);
	reflections=Cvar_Get("s_steamReflections","1",CVAR_ARCHIVE); Cvar_CheckRange(reflections,0,1,qtrue);
	pathing=Cvar_Get("s_steamPathing","1",CVAR_ARCHIVE); Cvar_CheckRange(pathing,0,1,qtrue);
	wet=Cvar_Get("s_steamReverb","0.2",CVAR_ARCHIVE); Cvar_CheckRange(wet,0,1,qfalse);
	transmission=Cvar_Get("s_steamTransmission","0.12",CVAR_ARCHIVE); Cvar_CheckRange(transmission,0,1,qfalse);
	cache=Cvar_Get("s_steamCache","1",CVAR_ARCHIVE); Cvar_CheckRange(cache,0,1,qtrue);
	Cmd_AddCommand("s_steam_status",Status); Cmd_AddCommand("s_steam_bake",Bake); Cmd_AddCommand("s_steam_emit",Emit); Cmd_AddCommand("s_steam_record",Record);
}
void S_SteamClear() { engine.reset(); loadedMap.clear(); objects.clear(); slots={}; lastUpdate=lastReflection=mixedBlocks=0; mixPeakUs=mixCalls=mixEnd=underrunFrames=bufferClears=0; sceneCache=probeCache=false; }
void S_SteamShutdown() { S_SteamClear(); recording.clear(); recordFrames=0; Cmd_RemoveCommand("s_steam_status"); Cmd_RemoveCommand("s_steam_bake"); Cmd_RemoveCommand("s_steam_emit"); Cmd_RemoveCommand("s_steam_record"); }
bool S_SteamActive() { return enabled && enabled->integer && engine && engine->Ready(); }
void S_SteamPrepare() {
	if(!enabled || !enabled->integer || !cl.mapname[0] || (dma.speed!=44100 && dma.speed!=48000)) return;
	if(loadedMap!=cl.mapname || serverId!=cl.serverId) {
		S_SteamClear(); loadedMap=cl.mapname; serverId=cl.serverId;
		engine.reset(new SteamSound::Engine(dma.speed));
		if(!engine->Ready() || !LoadMap()) { engine.reset(); Com_Printf("Steam Audio: initialization failed for %s; retaining legacy sound.\n",loadedMap.c_str()); return; }
		Status();
	}
}
void S_SteamUpdate(const float *head,const float axis[3][3],int listener,bool inWater) {
	(void)listener; (void)inWater;
	if(!enabled || !enabled->integer || cls.state!=CA_ACTIVE || !cl.mapname[0]) {
		if(cls.state!=CA_LOADING && cls.state!=CA_PRIMED && !loadedMap.empty()) S_SteamClear();
		return;
	}
	S_SteamPrepare();
	if(!engine || engine->Busy()) return;
	std::array<SteamSound::Voice,SteamSound::Voices> voices={}; bool changed=false;
	for(int i=0;i<SteamSound::Voices;++i) {
		const auto &ch=s_channels[i]; auto &slot=slots[i]; const bool active=Eligible(ch);
		if(slot.sound!=ch.thesfx || slot.entity!=ch.entnum || slot.active!=active || slot.loop!=bool(ch.loopSound) ||
			(!ch.loopSound && slot.start!=ch.startSample && slot.start!=START_SAMPLE_IMMEDIATE)) {
			if(ch.thesfx) engine->ResetVoice(i);
			slot.sound=ch.thesfx; slot.entity=ch.entnum; slot.loop=ch.loopSound; changed=true;
		}
		slot.start=ch.startSample;
		slot.active=active; if(!active) continue;
		const float *origin=ch.fixed_origin ? ch.origin : s_entityPosition[Com_Clampi(0,MAX_GENTITIES-1,ch.entnum)];
		voices[i].position=Position(origin); voices[i].active=true; voices[i].priority=float(std::max(ch.leftvol,ch.rightvol));
	}
	const int now=Sys_Milliseconds(); if(!changed && now-lastUpdate<50) return; lastUpdate=now;
	std::map<int,int> seen;
	if(ge && sv.state==SS_GAME) for(int i=0;i<ge->num_entities;++i) {
		const auto *ent=SV_GentityNum(i); if(!ent->inuse || !ent->linked || !ent->bmodel || !(ent->contents&CONTENTS_SOLID) || ent->s.modelindex<=0) continue;
		vec3_t forward,right,up; AngleVectors(ent->currentAngles,forward,right,up); VectorNegate(right,right);
		const auto x=Direction(forward),z=Direction(right),y=Direction(up),pos=Position(ent->currentOrigin);
		IPLMatrix4x4 transform={};
		transform.elements[0][0]=x.x; transform.elements[1][0]=x.y; transform.elements[2][0]=x.z;
		transform.elements[0][1]=y.x; transform.elements[1][1]=y.y; transform.elements[2][1]=y.z;
		transform.elements[0][2]=-z.x; transform.elements[1][2]=-z.y; transform.elements[2][2]=-z.z;
		transform.elements[0][3]=pos.x; transform.elements[1][3]=pos.y; transform.elements[2][3]=pos.z; transform.elements[3][3]=1;
		engine->Object(i,ent->s.modelindex,transform,true); seen[i]=ent->s.modelindex;
	}
	for(const auto &old:objects) if(!seen.count(old.first)) engine->Object(old.first,old.second,{},false);
	objects.swap(seen);
	IPLCoordinateSpace3 space={}; space.origin=Position(head); space.ahead=Direction(axis[0]); space.up=Direction(axis[2]);
	vec3_t right; VectorNegate(axis[1],right); space.right=Direction(right);
	const bool reflect=now-lastReflection>=500 || (changed && now-lastReflection>=100); if(reflect) lastReflection=now;
	engine->Update(voices,space,reflections->integer,pathing->integer,reflect);
	simulationMs=Sys_Milliseconds()-now;
}
void S_SteamBeginMix() {
	mixStart=std::chrono::steady_clock::now();
}
void S_SteamBufferCleared() { if(S_SteamActive()) ++bufferClears; }
void S_SteamEndMix(int soundtime) {
	if(!S_SteamActive()) { mixEnd=0; return; }
	const int elapsed=int(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-mixStart).count());
	mixPeakUs=std::max(mixPeakUs,elapsed); ++mixCalls;
	if(mixEnd) underrunFrames+=std::max(0,soundtime-mixEnd);
	mixEnd=s_paintedtime;
}
void S_SteamBeginBlock() {
	if(!S_SteamActive()) return;
	engine->Begin(); for(auto &slot:slots) std::fill(slot.input,slot.input+SteamSound::Block,0);
}
bool S_SteamPaint(channel_t *channel,const short *samples,int count,int offset,int volume) {
	if(!S_SteamActive() || !Eligible(*channel) || offset<0 || count<0 || offset+count>SteamSound::Block) return false;
	const int index=int(channel-s_channels); if(index<0 || index>=SteamSound::Voices || !slots[index].active || slots[index].sound!=channel->thesfx || slots[index].entity!=channel->entnum) return false;
	auto &slot=slots[index];
	for(int i=0;i<count;++i) slot.input[offset+i]=samples[i]/32768.0f;
	slot.left=channel->leftvol*volume/65536.0f; slot.right=channel->rightvol*volume/65536.0f; slot.gain=channel->master_vol*volume/65536.0f;
	return true;
}
void S_SteamEndBlock(portable_samplepair_t *output,int count) {
	if(S_SteamActive() && count==SteamSound::Block) {
		float left[SteamSound::Block]={},right[SteamSound::Block]={};
		for(int i=0;i<SteamSound::Voices;++i) { auto &s=slots[i]; engine->Mix(i,s.input,s.left,s.right,s.gain,wet->value,left,right,transmission->value); }
		engine->End(wet->value,left,right);
		for(int i=0;i<count;++i) {
			if(std::isfinite(left[i])) output[i].left+=int(Com_Clamp(-16,16,left[i])*8388608);
			if(std::isfinite(right[i])) output[i].right+=int(Com_Clamp(-16,16,right[i])*8388608);
		}
		++mixedBlocks;
	}
	Capture(output,count);
}
#endif
