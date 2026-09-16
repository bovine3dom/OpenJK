// SPDX-License-Identifier: GPL-2.0-or-later
#include "cg_headers.h"
#include "../icarus/IcarusImplementation.h"
#include <set>
#include <string>
#include <vector>

extern int TAG_GetOrigin2(const char *owner, const char *name, vec3_t origin);

namespace {
using Automap::Point;
std::vector<Point> destinations[ENTITYNUM_WORLD];
bool scanned;
struct Member { int type, size; const byte *data; };

void Add(std::vector<Point> &points, const Point &point) {
	for (float value : point) if (!std::isfinite(value) || std::abs(value)>MAX_WORLD_COORD) return;
	for (const auto &p : points) if (DistanceSquared(p.data(),point.data())<1) return;
	if (points.size()<Automap::MaxStops) points.push_back(point);
}
float Number(const Member &member) {
	float value=0;
	if (member.size==4) memcpy(&value,member.data,4);
	return LittleFloat(value);
}
std::string String(const std::vector<Member> &members, size_t &index, const gentity_t *owner) {
	if (index>=members.size()) return {};
	const auto &m=members[index++];
	if ((m.type==CIcarus::TK_STRING || m.type==CIcarus::TK_IDENTIFIER) && m.size>0 && m.data[m.size-1]==0)
		return std::string(reinterpret_cast<const char *>(m.data),m.size-1);
	if (m.type==CIcarus::ID_GET && index+1<members.size()) {
		const int type=int(Number(members[index++]));
		const auto field=String(members,index,owner);
		if (type==CIcarus::TK_STRING && owner && owner->parms && !Q_stricmpn(field.c_str(),"SET_PARM",8)) {
			const int parm=atoi(field.c_str()+8)-1;
			if (parm>=0 && parm<MAX_PARMS) return owner->parms->parm[parm];
		}
	}
	return {};
}
gentity_t *Actor(const std::string &name, gentity_t *owner) {
	if (name=="self") return owner;
	if (name.empty()) return nullptr;
	for (int i=0;i<ENTITYNUM_WORLD;++i) if (g_entities[i].inuse &&
		((g_entities[i].script_targetname && !Q_stricmp(g_entities[i].script_targetname,name.c_str())) ||
		(g_entities[i].targetname && !Q_stricmp(g_entities[i].targetname,name.c_str())))) return &g_entities[i];
	return nullptr;
}

// Read motion metadata only. Do not execute scripts or evaluate their conditions.
void Scan(const char *name, gentity_t *owner, std::set<std::pair<std::string,int>> &seen, int depth=0) {
	if (!name || !*name || depth>16 || seen.size()>4096 || !seen.emplace(name,owner ? owner->s.number : -1).second) return;
	void *buffer=nullptr;
	const int length=IGameInterface::GetGame()->LoadFile(name,&buffer);
	if (!buffer || length<8) return;
	const auto *data=static_cast<const byte *>(buffer);
	float version; memcpy(&version,data+4,4);
	if (memcmp(data,"IBI\0",4) || std::abs(LittleFloat(version)-1.57f)>0.001f) return;
	int pos=8;
	std::vector<gentity_t *> scope;
	while (pos<length) {
		if (length-pos<9) return;
		int op,count; memcpy(&op,data+pos,4); memcpy(&count,data+pos+4,4); pos+=9;
		op=LittleLong(op); count=LittleLong(count);
		if (count<0 || count>128) return;
		std::vector<Member> members;
		for (int i=0;i<count;++i) {
			if (length-pos<8) return;
			int type,size; memcpy(&type,data+pos,4); memcpy(&size,data+pos+4,4); pos+=8;
			type=LittleLong(type); size=LittleLong(size);
			if (size<0 || size>length-pos) return;
			members.push_back({type,size,data+pos}); pos+=size;
		}
		size_t index=0;
		if (op==CIcarus::ID_AFFECT) {
			scope.push_back(owner); owner=Actor(String(members,index,owner),owner);
		} else if (op==CIcarus::ID_TASK || op==CIcarus::ID_IF || op==CIcarus::ID_ELSE || op==CIcarus::ID_LOOP || op==CIcarus::ID_BLOCK_START) {
			scope.push_back(owner);
		} else if (op==CIcarus::ID_BLOCK_END || op==CIcarus::ID_LOOPEND) {
			if (!scope.empty()) { owner=scope.back(); scope.pop_back(); }
		} else if (op==CIcarus::ID_RUN) {
			const auto script=String(members,index,owner);
			if (!script.empty()) Scan(script.c_str(),owner,seen,depth+1);
		} else if (op==CIcarus::ID_MOVE && owner && !owner->client && !members.empty()) {
			Point target;
			if (members[0].type==CIcarus::ID_TAG) {
				index=1;
				const auto tag=String(members,index,owner);
				if (!tag.empty() && index<members.size() && int(Number(members[index]))==CIcarus::TYPE_ORIGIN &&
					TAG_GetOrigin2(owner->ownername,tag.c_str(),target.data())) Add(destinations[owner->s.number],target);
			} else if (members[0].type==CIcarus::TK_VECTOR && members.size()>=4 &&
				members[1].type==CIcarus::TK_FLOAT && members[2].type==CIcarus::TK_FLOAT && members[3].type==CIcarus::TK_FLOAT) {
				for (int i=0;i<3;++i) target[i]=Number(members[i+1]);
				Add(destinations[owner->s.number],target);
			}
		}
	}
}
bool Hint(const char *value) {
	if (!value) return false;
	std::string text=value;
	for (char &c : text) if (c>='A' && c<='Z') c+=32;
	return text.find("lift")!=std::string::npos || text.find("elev")!=std::string::npos || text.find("platform")!=std::string::npos;
}
}

void CG_ResetAutomapLifts() { scanned=false; for (auto &points : destinations) points.clear(); }

void CG_AddAutomapLifts(Automap::Frame &frame) {
	if (!scanned) {
		std::set<std::pair<std::string,int>> seen;
		for (int i=0;i<ENTITYNUM_WORLD;++i) if (g_entities[i].inuse)
			for (const char *script : g_entities[i].behaviorSet) if (script && *script) Scan(script,&g_entities[i],seen);
		scanned=true;
	}
	for (int i=0;i<ENTITYNUM_WORLD && frame.liftCount<Automap::MaxLifts;++i) {
		const auto &ent=g_entities[i];
		if (!ent.inuse || !ent.classname || ent.client || !ent.model || ent.model[0]!='*' ||
			(ent.svFlags&SVF_NOCLIENT) || (ent.s.eFlags&EF_NODRAW) || (ent.flags&FL_TEAMSLAVE)) continue;
		const bool plat=!Q_stricmp(ent.classname,"func_plat"), train=!Q_stricmp(ent.classname,"func_train");
		const bool door=!Q_stricmp(ent.classname,"func_door");
		if (!plat && !train && !door && Q_stricmp(ent.classname,"func_static") && Q_stricmp(ent.classname,"func_usable")) continue;
		auto stops=train ? std::vector<Point>() : destinations[i];
		if (DistanceSquared(ent.pos1,ent.pos2)>1) { Add(stops,{{ent.pos1[0],ent.pos1[1],ent.pos1[2]}}); Add(stops,{{ent.pos2[0],ent.pos2[1],ent.pos2[2]}}); }
		if (train && ent.nextTrain) {
			const gentity_t *node=ent.nextTrain;
			for (int n=0;n<Automap::MaxStops && node && node->inuse;++n) {
				Add(stops,{{node->s.origin[0],node->s.origin[1],node->s.origin[2]}});
				node=node->nextTrain; if (node==ent.nextTrain) break;
			}
		}
		float low=ent.currentOrigin[2],high=low;
		for (const auto &p : stops) { low=std::min(low,p[2]); high=std::max(high,p[2]); }
		const bool flat=std::min(ent.maxs[0]-ent.mins[0],ent.maxs[1]-ent.mins[1])>=48 &&
			ent.maxs[2]-ent.mins[2]<=std::min(ent.maxs[0]-ent.mins[0],ent.maxs[1]-ent.mins[1]);
		const bool named=Hint(ent.targetname) || Hint(ent.script_targetname) || Hint(ent.soundSet);
		const bool scripted=!Q_stricmp(ent.classname,"func_static");
		if ((door || train) && !named) continue; // Exclude hatches, camera rigs, and moving machinery.
		if (!plat && !(((scripted || train) && named) || (flat && high-low>32))) continue;
		auto &lift=frame.lifts[frame.liftCount++]; lift.entity=i; lift.count=int(stops.size()); lift.ordered=train;
		for (int axis=0;axis<3;++axis) {
			lift.position[axis]=axis==2 ? ent.absmax[axis] : (ent.absmin[axis]+ent.absmax[axis])*0.5f;
			for (int j=0;j<lift.count;++j) lift.stops[j][axis]=stops[j][axis]+lift.position[axis]-ent.currentOrigin[axis];
		}
	}
}
