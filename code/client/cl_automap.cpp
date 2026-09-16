// SPDX-License-Identifier: GPL-2.0-or-later
#include "client.h"
#include "../qcommon/qfiles.h"
#include "qcommon/automap.h"
#include "qcommon/automap_nav.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <vector>

namespace {
using Automap::Point;
struct Triangle { Point p[3]; float low, high; int floor; };
struct Edge { Point a, b, normal; int count = 0, floor = 0; bool crease = false; };
std::vector<Triangle> triangles;
std::vector<Edge> edges;
std::vector<Triangle> explodedTriangles;
std::vector<Edge> explodedEdges;
Automap::Frame current = {};
Point centre = {}, minimum = {}, maximum = {};
float span = 1024, yaw = 45, tilt = 55;
std::string loadedMap;
int visibleTriangles, visibleMarkers;
int nextControl;
int nextLift, visibleLifts;
bool valid;
bool recenter = true;
Automap::NavMap nav;
std::vector<Automap::FloorLink> displayLinks;
std::vector<Point> navInput, floorOffsets, floorLow, floorHigh;
Point navCentre = {};
bool exploded = false, navAttempted = false;
constexpr float Left = Automap::ViewLeft, Top = Automap::ViewTop, Width = Automap::ViewWidth, Height = Automap::ViewHeight;

template<class T> struct Lump {
	const byte *data; int count;
	T operator[](int index) const { T value; memcpy(&value, data + sizeof(T) * index, sizeof(T)); return value; }
};
template<class T> bool GetLump(const byte *data, int length, const dheader_t &header, int index, Lump<T> &out) {
	const int offset = LittleLong(header.lumps[index].fileofs), bytes = LittleLong(header.lumps[index].filelen);
	if (offset < 0 || bytes < 0 || offset > length || bytes > length - offset || bytes % sizeof(T)) return false;
	out = {data + offset, int(bytes / sizeof(T))};
	return true;
}
Point Position(const drawVert_t &v) { return {{LittleFloat(v.xyz[0]), LittleFloat(v.xyz[1]), LittleFloat(v.xyz[2])}}; }

// Adapt the MP automap's BSP face/patch extraction and relative-height treatment.
// Keep the mesh renderer-independent; the old immediate-mode GL drawing is not reused.
bool LoadMesh(const char *name) {
	triangles.clear(); edges.clear(); navInput.clear(); nav = {}; navAttempted = exploded = false;
	explodedTriangles.clear(); explodedEdges.clear(); displayLinks.clear();
	void *buffer = nullptr;
	const int length = FS_ReadFile(name, &buffer);
	if (length < int(sizeof(dheader_t))) { if (buffer) FS_FreeFile(buffer); return false; }
	const byte *data = static_cast<const byte *>(buffer);
	dheader_t header; memcpy(&header, data, sizeof(header));
	Lump<drawVert_t> vertices; Lump<int> indices; Lump<dsurface_t> surfaces;
	Lump<dshader_t> shaders; Lump<dmodel_t> models;
	bool ok = LittleLong(header.ident) == BSP_IDENT && LittleLong(header.version) == BSP_VERSION &&
		GetLump(data, length, header, LUMP_DRAWVERTS, vertices) && GetLump(data, length, header, LUMP_DRAWINDEXES, indices) &&
		GetLump(data, length, header, LUMP_SURFACES, surfaces) && GetLump(data, length, header, LUMP_SHADERS, shaders) &&
		GetLump(data, length, header, LUMP_MODELS, models) && models.count > 0;
	std::map<std::array<float, 6>, Edge> outline;
	bool first = true;
	bool navSolid = false, display = true;
	auto add = [&](Point a, Point b, Point c, float nz) {
		for (const Point &p : {a,b,c}) for (float v : p) if (!std::isfinite(v) || std::abs(v) > MAX_WORLD_COORD) return;
		Point ab, ac, n;
		for (int j = 0; j < 3; ++j) { ab[j] = b[j] - a[j]; ac[j] = c[j] - a[j]; }
		CrossProduct(ab.data(), ac.data(), n.data());
		if (VectorNormalize(n.data()) == 0) return;
		if (navSolid) {
			// BSP patch winding can differ from its supplied surface normals.
			if (n[2]*nz<0) navInput.insert(navInput.end(),{a,c,b});
			else navInput.insert(navInput.end(),{a,b,c});
		}
		if (!display || nz < -0.1f) return; // Keep ceilings in Recast for clearance tests.
		if (nz > 0.15f) triangles.push_back({{a,b,c}, std::min({a[2],b[2],c[2]}), std::max({a[2],b[2],c[2]}),0});
		const Point points[] = {a,b,c};
		for (const auto &p : points) {
			for (int j = 0; j < 3; ++j) {
				if (first) minimum[j] = maximum[j] = p[j];
				minimum[j] = std::min(minimum[j], p[j]); maximum[j] = std::max(maximum[j], p[j]);
			}
			first = false;
		}
		for (int i = 0; i < 3; ++i) {
			Point p = points[i], q = points[(i+1)%3];
			if (q < p) std::swap(p,q);
			auto &edge = outline[{{p[0],p[1],p[2],q[0],q[1],q[2]}}];
			if (!edge.count) { edge.a = p; edge.b = q; edge.normal = n; }
			else if (std::abs(DotProduct(edge.normal.data(), n.data())) < 0.99f) edge.crease = true;
			++edge.count;
		}
	};
	if (ok) {
		const auto world = models[0];
		const int begin = LittleLong(world.firstSurface), count = LittleLong(world.numSurfaces);
		ok = begin >= 0 && count >= 0 && begin <= surfaces.count && count <= surfaces.count - begin;
		for (int i = begin; ok && i < begin + count; ++i) {
			const auto s = surfaces[i];
			const int shader = LittleLong(s.shaderNum), type = LittleLong(s.surfaceType);
			const int start = LittleLong(s.firstVert), num = LittleLong(s.numVerts);
			if (shader < 0 || shader >= shaders.count || start < 0 || num < 0 || start > vertices.count || num > vertices.count - start) { ok = false; break; }
			const int flags=LittleLong(shaders[shader].surfaceFlags);
			navSolid=(LittleLong(shaders[shader].contentFlags)&CONTENTS_SOLID) && !(flags&SURF_SKY);
			display=!(flags&(SURF_SKY|SURF_NODRAW));
			if (type == MST_PLANAR || type == MST_TRIANGLE_SOUP) {
				const int base = LittleLong(s.firstIndex), size = LittleLong(s.numIndexes);
				if (base < 0 || size < 0 || base > indices.count || size > indices.count - base || size % 3) { ok = false; break; }
				for (int j = 0; j < size; j += 3) {
					Point p[3]; float nz = 0;
					for (int k = 0; k < 3; ++k) {
						const int v = LittleLong(indices[base+j+k]);
						if (v < 0 || v >= num) { ok = false; break; }
						const auto vertex = vertices[start+v]; p[k] = Position(vertex); nz += LittleFloat(vertex.normal[2]) / 3;
					}
					if (!ok) break;
					add(p[0],p[1],p[2],nz);
				}
			} else if (type == MST_PATCH) {
				const int w = LittleLong(s.patchWidth), h = LittleLong(s.patchHeight);
				if (w < 3 || h < 3 || w > 64 || h > 64 || w*h != num || !(w&1) || !(h&1)) { ok = false; break; }
				for (int y = 0; y < h-2; y += 2) for (int x = 0; x < w-2; x += 2) {
					Point grid[5][5] = {}; float nz = 0;
					for (int j = 0; j < 3; ++j) for (int k = 0; k < 3; ++k) nz += LittleFloat(vertices[start+(y+j)*w+x+k].normal[2]) / 9;
					for (int v = 0; v <= 4; ++v) for (int u = 0; u <= 4; ++u) {
						const float a = u/4.0f, b = v/4.0f;
						const float bu[] = {(1-a)*(1-a),2*a*(1-a),a*a}, bv[] = {(1-b)*(1-b),2*b*(1-b),b*b};
						for (int j = 0; j < 3; ++j) for (int k = 0; k < 3; ++k) {
							const auto p = Position(vertices[start+(y+j)*w+x+k]);
							for (int axis = 0; axis < 3; ++axis) grid[v][u][axis] += p[axis] * bu[k] * bv[j];
						}
					}
					for (int v = 0; v < 4; ++v) for (int u = 0; u < 4; ++u) {
						add(grid[v][u],grid[v][u+1],grid[v+1][u],nz);
						add(grid[v+1][u],grid[v][u+1],grid[v+1][u+1],nz);
					}
				}
			}
		}
	}
	FS_FreeFile(buffer);
	if (!ok) { triangles.clear(); navInput.clear(); return false; }
	for (const auto &item : outline) if (item.second.count != 2 || item.second.crease) edges.push_back(item.second);
	Com_Printf("Automap mesh: %s floors=%d edges=%d\n", name, int(triangles.size()), int(edges.size()));
	return !triangles.empty();
}

struct Batch {
	std::vector<polyVert_t> vertices;
	std::vector<int> indices;
	int clip[4];
	Batch() {
		clip[0] = int(Left * cls.glconfig.vidWidth / 640); clip[1] = int(Top * cls.glconfig.vidHeight / 480);
		clip[2] = int(Width * cls.glconfig.vidWidth / 640); clip[3] = int(Height * cls.glconfig.vidHeight / 480);
	}
	void Flush() {
		if (!indices.empty()) re.DrawUiGeometry(int(vertices.size()), vertices.data(), int(indices.size()), indices.data(), clip, 0);
		vertices.clear(); indices.clear();
	}
	void Poly(const std::vector<Point> &points, const std::array<byte,4> &color) {
		if (vertices.size() + points.size() > REF_UI_MAX_VERTICES || indices.size() + (points.size()-2)*3 > REF_UI_MAX_INDICES) Flush();
		const int base = int(vertices.size());
		for (const auto &p : points) {
			polyVert_t v = {}; v.xyz[0] = p[0]; v.xyz[1] = p[1];
			// UI geometry uses premultiplied-alpha blending.
			for (int c = 0; c < 3; ++c) v.modulate[c] = byte((int(color[c])*color[3]+127)/255);
			v.modulate[3] = color[3];
			vertices.push_back(v);
		}
		for (int i = 1; i+1 < int(points.size()); ++i) indices.insert(indices.end(), {base,base+i,base+i+1});
	}
	void Line(Point a, Point b, const std::array<byte,4> &color, float width = 0.65f) {
		const float aspect = 640.0f * cls.glconfig.vidHeight / (480.0f * cls.glconfig.vidWidth);
		const float dx = (b[0]-a[0])/aspect, dy = b[1]-a[1], len = std::hypot(dx,dy);
		if (len < 0.01f) return;
		const float x = -dy/len*width*aspect/2, y = dx/len*width/2;
		Poly({{{a[0]+x,a[1]+y,0}},{{b[0]+x,b[1]+y,0}},{{b[0]-x,b[1]-y,0}},{{a[0]-x,a[1]-y,0}}}, color);
	}
};
Point Screen(const Point &p) {
	Point q = Automap::Project(p, centre, yaw, tilt);
	const float scale = Width / (2 * span);
	q[0] = Left + Width/2 + q[0]*scale;
	q[1] = Top + Height/2 + q[1]*scale * cls.glconfig.vidWidth * 480.0f / (cls.glconfig.vidHeight * 640.0f);
	return q;
}
void Label(float x, float y, const char *value, bool lift=false) {
	UiText::Style style; style.x=x; style.y=y; style.size=11; style.datapad=true; style.outline=false;
	style.color[0]=0.65f; style.color[1]=0.8f; style.color[2]=0.85f;
	if (lift) { style.color[0]=0.35f; style.color[1]=0.95f; style.color[2]=0.65f; }
#ifdef USE_RMLUI
	if (CL_RmlUiText(value, style, nullptr, true)) return;
#endif
	re.Font_DrawString(int(x), int(y), value, style.color, re.RegisterFont("ocr_a"), -1, 0.65f);
}
void North(Batch &batch) {
	const float aspect=640.0f*cls.glconfig.vidHeight/(480.0f*cls.glconfig.vidWidth);
	const auto north=Automap::Project({{0,1,0}},{{0,0,0}},yaw,tilt);
	const float length=std::hypot(north[0],north[1]);
	const Point origin={{Left+Width-22*aspect,Top+28,0}};
	const Point end={{origin[0]+north[0]/length*12*aspect,origin[1]+north[1]/length*12,0}};
	batch.Line(origin,end,{{170,205,215,255}},1);
	batch.Flush();
	Label(end[0]-3*aspect,end[1]-11,"N");
}
Point NavPosition(const Point &p,int floor) {
	Point q=Automap::Project(p,{{0,0,0}},yaw,tilt);
	for (int j=0;j<2;++j) q[j]+=floorOffsets[floor][j];
	return q;
}
void PartitionMap() {
	explodedTriangles.clear(); explodedEdges.clear();
	displayLinks=nav.DisplayLinks();
	for (int floor=0;floor<int(nav.floors.size());++floor) {
		const float low=floor ? (nav.floors[floor-1].height+nav.floors[floor].height)/2 : -MAX_WORLD_COORD;
		const float high=floor+1<int(nav.floors.size()) ? (nav.floors[floor].height+nav.floors[floor+1].height)/2 : MAX_WORLD_COORD;
		for (const auto &t : triangles) {
			if (t.low>=high || t.high<low) continue;
			auto poly=Automap::Clip(Automap::Clip({{t.p[0],t.p[1],t.p[2]},3},low,true),high,false);
			for (int i=1;i+1<poly.count;++i) {
				const auto &a=poly.points[0],&b=poly.points[i],&c=poly.points[i+1];
				explodedTriangles.push_back({{a,b,c},std::min({a[2],b[2],c[2]}),std::max({a[2],b[2],c[2]}),floor});
			}
		}
		for (auto edge : edges) {
			if (std::min(edge.a[2],edge.b[2])>=high || !Automap::ClipSegment(edge.a,edge.b,low,high)) continue;
			edge.floor=floor; explodedEdges.push_back(edge);
		}
	}
}
void NavLayout() {
	floorLow.assign(nav.floors.size(),{{1e30f,1e30f,0}});
	floorHigh.assign(nav.floors.size(),{{-1e30f,-1e30f,0}});
	floorOffsets.assign(nav.floors.size(),{{0,0,0}});
	auto include=[&](const Point &p,int floor) {
		const auto q=Automap::Project(p,{{0,0,0}},yaw,tilt);
		for (int j=0;j<2;++j) {
			floorLow[floor][j]=std::min(floorLow[floor][j],q[j]);
			floorHigh[floor][j]=std::max(floorHigh[floor][j],q[j]);
		}
	};
	for (const auto &face : explodedTriangles) for (const auto &p : face.p) include(p,face.floor);
	for (const auto &edge : explodedEdges) { include(edge.a,edge.floor); include(edge.b,edge.floor); }
	// Keep an empty BSP band finite if Recast found only non-drawn solid surfaces.
	for (const auto &face : nav.faces) for (const auto &p : face.points) include(p,face.floor);
	float area=0,widest=0;
	for (size_t i=0;i<nav.floors.size();++i) {
		const float w=floorHigh[i][0]-floorLow[i][0]+256,h=floorHigh[i][1]-floorLow[i][1]+256;
		area+=w*h; widest=std::max(widest,w);
	}
	const float aspect=cls.glconfig.vidWidth*480.0f/(cls.glconfig.vidHeight*640.0f);
	const float rowWidth=std::max(widest,std::sqrt(area*Width/(Height*aspect)));
	float x=0,y=0,rowHeight=0;
	for (int i=int(nav.floors.size())-1;i>=0;--i) {
		const float w=floorHigh[i][0]-floorLow[i][0]+256,h=floorHigh[i][1]-floorLow[i][1]+256;
		if (x>0 && x+w>rowWidth) { x=0; y+=rowHeight; rowHeight=0; }
		floorOffsets[i]={{x-floorLow[i][0],y-floorLow[i][1],0}};
		x+=w; rowHeight=std::max(rowHeight,h);
	}
}
void NavFit() {
	NavLayout();
	Point low={{1e30f,1e30f,0}},high={{-1e30f,-1e30f,0}};
	for (size_t i=0;i<nav.floors.size();++i) for (int j=0;j<2;++j) {
		low[j]=std::min(low[j],floorLow[i][j]+floorOffsets[i][j]);
		high[j]=std::max(high[j],floorHigh[i][j]+floorOffsets[i][j]);
	}
	for (int j=0;j<2;++j) navCentre[j]=(low[j]+high[j])/2;
	const float aspect=cls.glconfig.vidWidth*480.0f/(cls.glconfig.vidHeight*640.0f);
	span=std::max(128.0f,std::max(high[0]-low[0],(high[1]-low[1])*Width*aspect/Height)*0.55f);
}
Point NavScreen(const Point &p,int floor) {
	Point q=NavPosition(p,floor);
	const float scale=Width/(2*span);
	q[0]=Left+Width/2+(q[0]-navCentre[0])*scale;
	q[1]=Top+Height/2+(q[1]-navCentre[1])*scale*cls.glconfig.vidWidth*480.0f/(cls.glconfig.vidHeight*640.0f);
	return q;
}
void Centre() {
	centre = current.player;
	if (exploded) { navCentre=NavPosition(current.player,nav.FloorAt(current.player)); span=1024; }
}
std::array<byte,4> EdgeColor(const Edge &edge) {
	const bool vertical=std::hypot(edge.b[0]-edge.a[0],edge.b[1]-edge.a[1])<0.01f && std::abs(edge.b[2]-edge.a[2])>0.01f;
	return {{83,116,129,byte(vertical ? 255/6 : 255)}};
}
void Action() {
	const char *action = Cmd_Argv(1);
	if (!Q_stricmp(action,"explode") && valid) {
		if (!navAttempted) {
			navAttempted=true;
			const int start=Sys_Milliseconds();
			if (!nav.Build(navInput)) Com_Printf("Automap: navigation mesh unavailable; using whole-map view.\n");
			PartitionMap();
			Com_Printf("Automap navigation: polygons=%d floors=%d links=%d build_ms=%d\n",int(nav.faces.size()),int(nav.floors.size()),int(nav.links.size()),Sys_Milliseconds()-start);
		}
		if (!nav.floors.empty()) { exploded=!exploded; if (exploded) NavFit(); else { Centre(); span=1024; } }
		return;
	}
	if (exploded) {
		if (!Q_stricmp(action,"fit")) { NavFit(); return; }
		if (!Q_stricmp(action,"up") || !Q_stricmp(action,"down")) {
			int floor=0;
			for (int i=1;i<int(nav.floors.size());++i) if (std::abs(centre[2]-nav.floors[i].height)<std::abs(centre[2]-nav.floors[floor].height)) floor=i;
			floor=Com_Clampi(0,int(nav.floors.size())-1,floor+(!Q_stricmp(action,"up") ? 1 : -1));
			centre[2]=nav.floors[floor].height;
			for (int j=0;j<2;++j) navCentre[j]=(floorLow[floor][j]+floorHigh[floor][j])/2+floorOffsets[floor][j];
			return;
		}
	}
	if (!Q_stricmp(action,"zoomin")) span = std::max(128.0f, span/1.25f);
	else if (!Q_stricmp(action,"zoomout")) span = std::min(exploded ? 262144.0f : 16384.0f, span*1.25f);
	else if (!Q_stricmp(action,"up") || !Q_stricmp(action,"down")) return; // Floor selection is for the exploded view.
	else if (!Q_stricmp(action,"drag") && Cmd_Argc()==5 && valid) {
		const float dx = atof(Cmd_Argv(2)), dy = atof(Cmd_Argv(3));
		if (!std::isfinite(dx) || !std::isfinite(dy) || atoi(Cmd_Argv(4))) return;
		const float aspect = cls.glconfig.vidWidth * 480.0f / (cls.glconfig.vidHeight * 640.0f);
		if (exploded) {
			navCentre[0]-=dx*2*span/Width; navCentre[1]-=dy*2*span/(Width*aspect);
		}
		else {
			const auto delta = Automap::DragPan(dx,dy,span,yaw,tilt,aspect);
			centre[0] += delta[0]; centre[1] += delta[1];
		}
	}
	else if (!Q_stricmp(action,"left")) yaw -= 15;
	else if (!Q_stricmp(action,"right")) yaw += 15;
	else if (!Q_stricmp(action,"tilt")) { tilt = tilt ? 0 : 55; yaw = tilt ? 45 : 0; }
	else if (!Q_stricmp(action,"centre")) recenter = true;
	else if (!Q_stricmp(action,"control") && current.count) {
		centre = current.markers[nextControl % current.count].position;
		if (exploded) navCentre=NavPosition(centre,nav.FloorAt(centre));
		nextControl = (nextControl + 1) % current.count;
	}
	else if (!Q_stricmp(action,"lift") && current.liftCount) {
		centre=current.lifts[nextLift % current.liftCount].position;
		if (exploded) navCentre=NavPosition(centre,nav.FloorAt(centre));
		nextLift=(nextLift+1)%current.liftCount;
	}
	else if (!Q_stricmp(action,"fit")) {
		for (int i = 0; i < 3; ++i) centre[i] = (minimum[i]+maximum[i])/2;
		float x = 0, y = 0;
		for (int i=0;i<8;++i) {
			const auto p = Automap::Project({{i&1 ? maximum[0] : minimum[0], i&2 ? maximum[1] : minimum[1], i&4 ? maximum[2] : minimum[2]}},centre,yaw,tilt);
			x = std::max(x,std::abs(p[0])); y = std::max(y,std::abs(p[1]));
		}
		const float aspect = cls.glconfig.vidWidth * 480.0f / (cls.glconfig.vidHeight * 640.0f);
		span = Com_Clamp(128,16384,std::max(x,y*Width*aspect/Height)*1.1f);
	} else if (!Q_stricmpn(action,"pan",3)) {
		const float a = yaw * 0.01745329252f, step = span/8;
		float dx = 0, dy = 0;
		if (!Q_stricmp(action,"panleft")) dx = -step;
		if (!Q_stricmp(action,"panright")) dx = step;
		if (!Q_stricmp(action,"panup")) dy = step;
		if (!Q_stricmp(action,"pandown")) dy = -step;
		if (exploded) { navCentre[0]+=dx; navCentre[1]-=dy; }
		else { centre[0] += std::cos(a)*dx-std::sin(a)*dy; centre[1] += std::sin(a)*dx+std::cos(a)*dy; }
	} else Com_Printf("automap: explode zoomin zoomout up down left right tilt centre fit control lift panleft panright panup pandown\n");
	if (exploded && (!Q_stricmp(action,"left") || !Q_stricmp(action,"right") || !Q_stricmp(action,"tilt"))) NavFit();
}
void Status() {
	Com_Printf("automap exploded=%d nav_polygons=%d floors=%d connections=%d nav_centre=%.1f,%.1f\n",exploded,int(nav.faces.size()),int(nav.floors.size()),int(nav.links.size()),navCentre[0],navCentre[1]);
	Com_Printf("automap bsp_parts=%d bsp_edges=%d grouped_links=%d\n",int(explodedTriangles.size()),int(explodedEdges.size()),int(displayLinks.size()));
	if (exploded) for (size_t i=0;i<nav.floors.size();++i) Com_Printf("automap floor=%d elevation=%.1f bounds=%.1f,%.1f,%.1f,%.1f\n",int(i)+1,nav.floors[i].height,
		floorLow[i][0]+floorOffsets[i][0],floorLow[i][1]+floorOffsets[i][1],floorHigh[i][0]+floorOffsets[i][0],floorHigh[i][1]+floorOffsets[i][1]);
	Com_Printf("automap map=%s valid=%d triangles=%d edges=%d drawn=%d markers=%d shown=%d height=%.1f span=%.1f yaw=%.1f tilt=%.1f centre=%.1f,%.1f lifts=%d lifts_shown=%d\n",
		loadedMap.c_str(), valid, int(triangles.size()), int(edges.size()), visibleTriangles, current.count, visibleMarkers,
		centre[2], span, yaw, tilt, centre[0], centre[1],current.liftCount,visibleLifts);
	for (int i = 0; i < current.count; ++i) Com_Printf("automap control=%d enabled=%d position=%.1f,%.1f,%.1f\n",
		current.markers[i].entity, current.markers[i].enabled, current.markers[i].position[0], current.markers[i].position[1], current.markers[i].position[2]);
	for (int i=0;i<current.liftCount;++i) {
		const auto &lift=current.lifts[i];
		Com_Printf("automap lift=%d stops=%d position=%.1f,%.1f,%.1f\n",lift.entity,lift.count,lift.position[0],lift.position[1],lift.position[2]);
		for (int j=0;j<lift.count;++j) Com_Printf("automap lift=%d stop=%d at=%.1f,%.1f,%.1f\n",lift.entity,j,lift.stops[j][0],lift.stops[j][1],lift.stops[j][2]);
	}
}
void DrawExploded(Batch &batch) {
	const int playerFloor=nav.FloorAt(current.player);
	const float aspect=640.0f*cls.glconfig.vidHeight/(480.0f*cls.glconfig.vidWidth);
	std::vector<std::pair<float,int>> order;
	for (int i=0;i<int(explodedTriangles.size());++i) {
		const auto &t=explodedTriangles[i];
		Point p; for (int j=0;j<3;++j) p[j]=(t.p[0][j]+t.p[1][j]+t.p[2][j])/3;
		order.emplace_back(Automap::Project(p,centre,yaw,tilt)[2],i);
	}
	std::sort(order.rbegin(),order.rend());
	for (const auto &entry : order) {
		const auto &t=explodedTriangles[entry.second];
		const byte shade=byte(Com_Clamp(30,80,55+(t.low-nav.floors[t.floor].height)*0.25f));
		batch.Poly({NavScreen(t.p[0],t.floor),NavScreen(t.p[1],t.floor),NavScreen(t.p[2],t.floor)},{{byte(shade*0.6f),shade,byte(shade+16),255}});
		++visibleTriangles;
	}
	for (const auto &edge : explodedEdges) batch.Line(NavScreen(edge.a,edge.floor),NavScreen(edge.b,edge.floor),EdgeColor(edge));
	for (const auto &link : displayLinks) batch.Line(NavScreen(link.a,link.from),NavScreen(link.b,link.to),{{175,151,90,170}},1);
	for (int i=0;i<current.liftCount;++i) {
		const auto &lift=current.lifts[i];
		for (int j=0;j<Com_Clampi(0,Automap::MaxStops,lift.count);++j) {
			if (lift.ordered && j==0) continue;
			const Point a=lift.ordered && j>1 ? lift.stops[j-1] : lift.position,b=lift.stops[j];
			batch.Line(NavScreen(a,nav.FloorAt(a)),NavScreen(b,nav.FloorAt(b)),{{75,190,130,200}},1);
		}
	}
	auto dot=[&](const Point &world,const std::array<byte,4> &color,float radius) {
		const auto p=NavScreen(world,nav.FloorAt(world));
		if (p[0]<Left || p[0]>Left+Width || p[1]<Top || p[1]>Top+Height) return false;
		batch.Poly({{{p[0]-radius*aspect,p[1],0}},{{p[0],p[1]-radius,0}},{{p[0]+radius*aspect,p[1],0}},{{p[0],p[1]+radius,0}}},color);
		return true;
	};
	for (int i=0;i<current.count;++i) if (dot(current.markers[i].position,current.markers[i].enabled ? std::array<byte,4>{{255,193,70,255}} : std::array<byte,4>{{130,135,140,255}},3)) ++visibleMarkers;
	for (int i=0;i<current.liftCount;++i) if (dot(current.lifts[i].position,{{75,220,130,255}},4)) ++visibleLifts;
	Point tip=current.player,left=current.player,right=current.player;
	const float angle=current.heading*0.01745329252f,radius=span/40;
	tip[0]+=std::cos(angle)*radius; tip[1]+=std::sin(angle)*radius;
	left[0]+=std::cos(angle+2.5f)*radius*0.7f; left[1]+=std::sin(angle+2.5f)*radius*0.7f;
	right[0]+=std::cos(angle-2.5f)*radius*0.7f; right[1]+=std::sin(angle-2.5f)*radius*0.7f;
	batch.Poly({NavScreen(tip,playerFloor),NavScreen(left,playerFloor),NavScreen(right,playerFloor)},{{80,235,245,255}});
	North(batch);
	const float scale=Width/(2*span);
	std::vector<Point> labels;
	for (size_t i=0;i<nav.floors.size();++i) {
		const float x=Left+Width/2+(floorLow[i][0]+floorOffsets[i][0]-navCentre[0])*scale;
		const float y=Top+Height/2+(floorLow[i][1]+floorOffsets[i][1]-navCentre[1])*scale/aspect;
		if (x<Left || x>Left+Width-90 || y<Top || y>Top+Height-12) continue;
		bool overlap=false;
		for (const auto &p : labels) if (std::abs(p[0]-x)<90 && std::abs(p[1]-y)<12) overlap=true;
		if (!overlap) { Label(x,y,va("F%d  Z %.0f",int(i)+1,nav.floors[i].height)); labels.push_back({{x,y,0}}); }
	}
	Label(26,379,va("Exploded | %d floors | Player F%d | Tan: surface links  Green: possible lift routes",int(nav.floors.size()),playerFloor+1));
	Label(26,398,"X: whole map  Drag: pan  Wheel: zoom  PgUp/Dn: floor  Q/E: rotate  T: tilt  Home: player");
}
}

void CL_InitAutomap() {
	Cmd_AddCommand("automap", Action); Cmd_AddCommand("automap_status", Status);
}
void CL_ResetAutomap() { loadedMap.clear(); triangles.clear(); edges.clear(); explodedTriangles.clear(); explodedEdges.clear(); displayLinks.clear(); navInput.clear(); nav={}; floorOffsets.clear(); floorLow.clear(); floorHigh.clear(); exploded=navAttempted=false; current = {}; valid = false; nextControl = nextLift = 0; recenter = true; }

void CL_DrawAutomap(const Automap::Frame *frame) {
	if (!frame || !re.DrawUiGeometry) return;
	current = *frame; current.count = Com_Clampi(0, Automap::MaxMarkers, current.count); current.map[63] = 0;
	current.liftCount=Com_Clampi(0,Automap::MaxLifts,current.liftCount);
	if (loadedMap != current.map) {
		loadedMap = current.map; valid = LoadMesh(current.map); Centre(); span=1024; yaw=45; tilt=55; nextControl=nextLift=0;
	}
	if (recenter) { Centre(); recenter = false; }
	Batch batch;
	batch.Poly({{{Left,Top,0}},{{Left+Width,Top,0}},{{Left+Width,Top+Height,0}},{{Left,Top+Height,0}}}, {{8,13,18,255}});
	visibleTriangles = visibleMarkers = visibleLifts = 0;
	if (exploded) { DrawExploded(batch); return; }
	struct LiftLabel { Point p; bool up, down, unknown; };
	std::vector<LiftLabel> liftLabels;
	if (valid) {
		std::vector<std::pair<float,int>> order;
		for (int i = 0; i < int(triangles.size()); ++i) {
			const auto &t = triangles[i];
			Point p; for (int j=0;j<3;++j) p[j]=(t.p[0][j]+t.p[1][j]+t.p[2][j])/3;
			order.emplace_back(Automap::Project(p,centre,yaw,tilt)[2],i);
		}
		std::sort(order.rbegin(),order.rend());
		for (const auto &entry : order) {
			const auto &t = triangles[entry.second];
			const byte shade = byte(Com_Clamp(30,80,55+(t.low-centre[2])*0.25f));
			batch.Poly({Screen(t.p[0]),Screen(t.p[1]),Screen(t.p[2])}, {{byte(shade*0.6f),shade,byte(shade+16),255}}); ++visibleTriangles;
		}
		for (const auto &edge : edges) batch.Line(Screen(edge.a),Screen(edge.b),EdgeColor(edge));
		const float aspect=640.0f*cls.glconfig.vidHeight/(480.0f*cls.glconfig.vidWidth);
		for (int i=0;i<current.count;++i) {
			const auto &marker=current.markers[i];
			const auto p=Screen(marker.position);
			if (p[0]<Left || p[0]>Left+Width || p[1]<Top || p[1]>Top+Height) continue;
			const std::array<byte,4> color=marker.enabled ? std::array<byte,4>{{255,193,70,255}} : std::array<byte,4>{{130,135,140,255}};
			batch.Poly({{{p[0]-3*aspect,p[1]-3,0}},{{p[0]+3*aspect,p[1]-3,0}},{{p[0]+3*aspect,p[1]+3,0}},{{p[0]-3*aspect,p[1]+3,0}}},color);
			++visibleMarkers;
		}
		for (int i=0;i<current.liftCount;++i) {
			const auto &lift=current.lifts[i];
			const int count=Com_Clampi(0,Automap::MaxStops,lift.count);
			bool up=false, down=false;
			for (int j=0;j<count;++j) {
				if (lift.ordered && j==0) continue;
				Point a=lift.ordered && j>1 ? lift.stops[j-1] : lift.position,b=lift.stops[j];
				up|=b[2]>lift.position[2]+8; down|=b[2]<lift.position[2]-8;
				a=Screen(a); b=Screen(b);
				const std::array<byte,4> green={{75,190,130,255}};
				batch.Line(a,b,green,1);
				const float dx=(b[0]-a[0])/aspect,dy=b[1]-a[1],length=std::hypot(dx,dy);
				if (length>4) {
					batch.Line(b,{{b[0]-(dx*4-dy*2)/length*aspect,b[1]-(dy*4+dx*2)/length,0}},green,1);
					batch.Line(b,{{b[0]-(dx*4+dy*2)/length*aspect,b[1]-(dy*4-dx*2)/length,0}},green,1);
				}
			}
			const auto p=Screen(lift.position);
			if (p[0]<Left+8 || p[0]>Left+Width-8 || p[1]<Top+12 || p[1]>Top+Height-12) continue;
			batch.Poly({{{p[0]-5*aspect,p[1],0}},{{p[0],p[1]-5,0}},{{p[0]+5*aspect,p[1],0}},{{p[0],p[1]+5,0}}},{{28,70,48,255}});
			liftLabels.push_back({p,up,down,count==0}); ++visibleLifts;
		}
		Point tip=current.player, left=current.player, right=current.player;
		const float a=current.heading*0.01745329252f, radius=span/40;
		tip[0]+=std::cos(a)*radius; tip[1]+=std::sin(a)*radius;
		left[0]+=std::cos(a+2.5f)*radius*0.7f; left[1]+=std::sin(a+2.5f)*radius*0.7f;
		right[0]+=std::cos(a-2.5f)*radius*0.7f; right[1]+=std::sin(a-2.5f)*radius*0.7f;
		batch.Poly({Screen(tip),Screen(left),Screen(right)},{{80,235,245,255}});
	}
	const float aspect=640.0f*cls.glconfig.vidHeight/(480.0f*cls.glconfig.vidWidth);
	North(batch);
	for (const auto &label : liftLabels) {
		Label(label.p[0]-3*aspect,label.p[1]-5,label.unknown ? "?" : "L",true);
		if (label.up) Label(label.p[0]+5*aspect,label.p[1]-13,"^",true);
		if (label.down) Label(label.p[0]+5*aspect,label.p[1]+2,"v",true);
	}
	Label(26,379,valid ? va("Whole map | %s | Controls %d | Lifts %d | Gold: controls  Green: lifts",tilt ? "Isometric" : "Top-down",current.count,current.liftCount) : "Automap unavailable for this level");
	Label(26,398,"X: exploded  Drag: pan  Wheel: zoom  Q/E: rotate  T: tilt  Home: player");
}
