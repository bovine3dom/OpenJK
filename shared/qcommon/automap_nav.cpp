// SPDX-License-Identifier: GPL-2.0-or-later
#include "automap_nav.h"
#include <Recast.h>
#include <algorithm>
#include <map>
#include <memory>
#include <limits>
#include <tuple>

namespace Automap {
namespace {
Point Middle(const NavFace &face) {
	Point p = {};
	for (const auto &v : face.points) for (int j=0;j<3;++j) p[j]+=v[j]/face.points.size();
	return p;
}
float Area(const NavFace &face) {
	float area=0;
	for (size_t i=0;i<face.points.size();++i) {
		const auto &a=face.points[i], &b=face.points[(i+1)%face.points.size()];
		area+=a[0]*b[1]-b[0]*a[1];
	}
	return std::abs(area)*0.5f;
}
}

bool NavMap::Build(const std::vector<Point> &input) {
	faces.clear(); floors.clear(); links.clear();
	if (input.empty() || input.size()%3 || input.size()>3000000) return false;
	std::vector<float> verts;
	std::vector<int> indices;
	for (const auto &p : input) {
		for (float v : p) if (!std::isfinite(v) || std::abs(v)>65536) return false;
		indices.push_back(int(indices.size()));
		verts.insert(verts.end(),{p[0],p[2],-p[1]});
	}
	rcConfig cfg = {};
	cfg.cs=16; cfg.ch=4; cfg.walkableSlopeAngle=45;
	cfg.walkableHeight=14; cfg.walkableClimb=4; cfg.walkableRadius=1;
	cfg.maxEdgeLen=64; cfg.maxSimplificationError=1.3f;
	cfg.minRegionArea=8; cfg.mergeRegionArea=64; cfg.maxVertsPerPoly=6;
	rcCalcBounds(verts.data(),int(input.size()),cfg.bmin,cfg.bmax);
	cfg.bmin[1]-=cfg.ch; cfg.bmax[1]+=cfg.walkableHeight*cfg.ch;
	rcCalcGridSize(cfg.bmin,cfg.bmax,cfg.cs,&cfg.width,&cfg.height);
	// Bound synchronous map-open work. Oversized maps retain the slice view.
	if (cfg.width<=0 || cfg.height<=0 || double(cfg.width)*cfg.height>4000000) return false;
	rcContext ctx;
	std::unique_ptr<rcHeightfield,decltype(&rcFreeHeightField)> solid(rcAllocHeightfield(),rcFreeHeightField);
	std::unique_ptr<rcCompactHeightfield,decltype(&rcFreeCompactHeightfield)> compact(rcAllocCompactHeightfield(),rcFreeCompactHeightfield);
	std::unique_ptr<rcContourSet,decltype(&rcFreeContourSet)> contours(rcAllocContourSet(),rcFreeContourSet);
	std::unique_ptr<rcPolyMesh,decltype(&rcFreePolyMesh)> mesh(rcAllocPolyMesh(),rcFreePolyMesh);
	if (!solid || !compact || !contours || !mesh ||
		!rcCreateHeightfield(&ctx,*solid,cfg.width,cfg.height,cfg.bmin,cfg.bmax,cfg.cs,cfg.ch)) return false;
	std::vector<unsigned char> areas(input.size()/3,RC_NULL_AREA);
	rcMarkWalkableTriangles(&ctx,cfg.walkableSlopeAngle,verts.data(),int(input.size()),indices.data(),int(areas.size()),areas.data());
	// Preserve landings and stair elevations when Recast simplifies its contours.
	for (size_t i=0;i<areas.size();++i) if (areas[i]!=RC_NULL_AREA) {
		const float a=input[i*3][2],b=input[i*3+1][2],c=input[i*3+2][2];
		if (std::max({a,b,c})-std::min({a,b,c})<=16) {
			const int band=int(std::floor((a+b+c)/(3*16)));
			areas[i]=static_cast<unsigned char>((band%62+62)%62+1);
		}
	}
	if (!rcRasterizeTriangles(&ctx,verts.data(),int(input.size()),indices.data(),areas.data(),int(areas.size()),*solid,cfg.walkableClimb)) return false;
	rcFilterLowHangingWalkableObstacles(&ctx,cfg.walkableClimb,*solid);
	rcFilterLedgeSpans(&ctx,cfg.walkableHeight,cfg.walkableClimb,*solid);
	rcFilterWalkableLowHeightSpans(&ctx,cfg.walkableHeight,*solid);
	if (!rcBuildCompactHeightfield(&ctx,cfg.walkableHeight,cfg.walkableClimb,*solid,*compact)) return false;
	solid.reset();
	if (!rcErodeWalkableArea(&ctx,cfg.walkableRadius,*compact) || !rcBuildDistanceField(&ctx,*compact) ||
		!rcBuildRegions(&ctx,*compact,0,cfg.minRegionArea,cfg.mergeRegionArea) ||
		!rcBuildContours(&ctx,*compact,cfg.maxSimplificationError,cfg.maxEdgeLen,*contours) ||
		!rcBuildPolyMesh(&ctx,*contours,cfg.maxVertsPerPoly,*mesh)) return false;
	std::map<int,float> heights;
	for (int i=0;i<mesh->npolys;++i) {
		NavFace face;
		for (int j=0;j<mesh->nvp;++j) {
			const auto index=mesh->polys[i*mesh->nvp*2+j];
			if (index==RC_MESH_NULL_IDX) break;
			const auto *v=mesh->verts+index*3;
			face.points.push_back({{mesh->bmin[0]+v[0]*mesh->cs,-(mesh->bmin[2]+v[2]*mesh->cs),mesh->bmin[1]+v[1]*mesh->ch}});
		}
		if (face.points.size()<3) { faces.clear(); return false; }
		float low=face.points[0][2],high=low;
		for (const auto &p : face.points) { low=std::min(low,p[2]); high=std::max(high,p[2]); }
		if (high-low<=16) heights[int(std::round(Middle(face)[2]/16))]+=Area(face);
		faces.push_back(face);
	}
	if (faces.empty()) return false;
	// Seed from large level areas. A fixed seed tolerance prevents stair-chain merging.
	float support=4096;
	for (const auto &sample : heights) support=std::max(support,sample.second*0.1f);
	while (!heights.empty()) {
		const auto peak=std::max_element(heights.begin(),heights.end(),[](const std::pair<const int,float> &a,const std::pair<const int,float> &b){return a.second<b.second;});
		const float height=peak->first*16.0f;
		if (peak->second<support && !floors.empty()) break;
		floors.push_back({height});
		for (auto it=heights.begin();it!=heights.end();) {
			if (std::abs(it->first*16-height)<=48) it=heights.erase(it); else ++it;
		}
		if (floors.size()==64) break;
	}
	if (floors.empty()) floors.push_back({Middle(faces[0])[2]});
	std::sort(floors.begin(),floors.end(),[](const Floor &a,const Floor &b){return a.height<b.height;});
	for (auto &face : faces) {
		const float z=Middle(face)[2];
		for (int i=1;i<int(floors.size());++i) if (std::abs(z-floors[i].height)<std::abs(z-floors[face.floor].height)) face.floor=i;
	}
	// Preserve actual navmesh adjacency across bands; proximity alone is not a link.
	for (int i=0;i<mesh->npolys;++i) for (int j=0;j<int(faces[i].points.size());++j) {
		const unsigned short other=mesh->polys[i*mesh->nvp*2+mesh->nvp+j];
		if (other>=mesh->npolys || other<=i || faces[i].floor==faces[other].floor) continue;
		links.push_back({Middle(faces[i]),Middle(faces[other]),faces[i].floor,faces[other].floor});
	}
	return true;
}

std::vector<FloorLink> NavMap::DisplayLinks(float radius) const {
	auto sorted=links;
	for (auto &link : sorted) if (link.from>link.to) { std::swap(link.from,link.to); std::swap(link.a,link.b); }
	std::sort(sorted.begin(),sorted.end(),[](const FloorLink &a,const FloorLink &b) {
		return std::tie(a.from,a.to,a.a,a.b)<std::tie(b.from,b.to,b.a,b.b);
	});
	auto near=[&](const Point &a,const Point &b) {
		float distance=0;
		for (int j=0;j<3;++j) distance+=(a[j]-b[j])*(a[j]-b[j]);
		return distance<=radius*radius;
	};
	std::vector<FloorLink> result;
	for (const auto &link : sorted) {
		bool grouped=false;
		for (const auto &prior : result) if (link.from==prior.from && link.to==prior.to && near(link.a,prior.a) && near(link.b,prior.b)) { grouped=true; break; }
		// Keep a real connection as the representative. Do not chain groups or average endpoints into walls.
		if (!grouped) result.push_back(link);
	}
	return result;
}

int NavMap::FloorAt(const Point &point) const {
	int result=0;
	float best=std::numeric_limits<float>::max();
	for (const auto &face : faces) {
		// Prefer the supporting polygon, then nearby polygons at a similar elevation.
		bool positive=false,negative=false;
		float distance=std::numeric_limits<float>::max();
		for (size_t i=0;i<face.points.size();++i) {
			const auto &a=face.points[i],&b=face.points[(i+1)%face.points.size()];
			const float dx=b[0]-a[0],dy=b[1]-a[1],cross=dx*(point[1]-a[1])-dy*(point[0]-a[0]);
			positive|=cross>0; negative|=cross<0;
			const float length=dx*dx+dy*dy;
			const float t=length ? std::max(0.0f,std::min(1.0f,((point[0]-a[0])*dx+(point[1]-a[1])*dy)/length)) : 0;
			distance=std::min(distance,std::hypot(point[0]-a[0]-t*dx,point[1]-a[1]-t*dy));
		}
		if (!(positive && negative)) distance=0;
		const float dz=point[2]-Middle(face)[2];
		const float score=distance+std::abs(dz)+(distance==0 && dz>=-8 ? -1000000 : 0);
		if (score<best) { best=score; result=face.floor; }
	}
	return result;
}
}
