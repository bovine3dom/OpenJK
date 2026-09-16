// SPDX-License-Identifier: GPL-2.0-or-later
#include "client.h"
#include "../qcommon/qfiles.h"
#include "qcommon/automap.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <vector>

namespace {
using Automap::Point;
struct Triangle { Point p[3]; float low, high; };
struct Edge { Point a, b, normal; int count = 0; bool crease = false; };
std::vector<Triangle> triangles;
std::vector<Edge> edges;
Automap::Frame current = {};
Point centre = {}, minimum = {}, maximum = {};
float span = 1024, yaw = 45, tilt = 55;
std::string loadedMap;
int visibleTriangles, visibleMarkers;
int nextControl;
int nextLift, visibleLifts;
bool valid;
bool recenter = true;
cvar_t *sliceHeight;
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
	triangles.clear(); edges.clear();
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
	auto add = [&](Point a, Point b, Point c, float nz) {
		if (nz < -0.1f) return; // Remove ceilings, as in the MP height-aware map.
		for (const Point &p : {a,b,c}) for (float v : p) if (!std::isfinite(v) || std::abs(v) > MAX_WORLD_COORD) return;
		Point ab, ac, n;
		for (int j = 0; j < 3; ++j) { ab[j] = b[j] - a[j]; ac[j] = c[j] - a[j]; }
		CrossProduct(ab.data(), ac.data(), n.data());
		if (VectorNormalize(n.data()) == 0) return;
		if (nz > 0.15f) triangles.push_back({{a,b,c}, std::min({a[2],b[2],c[2]}), std::max({a[2],b[2],c[2]})});
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
			if (LittleLong(shaders[shader].surfaceFlags) & (SURF_SKY | SURF_NODRAW)) continue;
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
	if (!ok) { triangles.clear(); return false; }
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
			for (int c = 0; c < 4; ++c) v.modulate[c] = color[c];
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
void Centre() { centre = current.player; }
void Action() {
	const char *action = Cmd_Argv(1);
	const float halfSlice = sliceHeight->value * 0.5f;
	if (!Q_stricmp(action,"zoomin")) span = std::max(128.0f, span/1.25f);
	else if (!Q_stricmp(action,"zoomout")) span = std::min(16384.0f, span*1.25f);
	else if (!Q_stricmp(action,"up")) centre[2] = std::min(maximum[2]+halfSlice, centre[2]+64);
	else if (!Q_stricmp(action,"down")) centre[2] = std::max(minimum[2]-halfSlice, centre[2]-64);
	else if (!Q_stricmp(action,"wider")) Cvar_SetValue("ui_automapSliceHeight",std::min(4096.0f,sliceHeight->value+64));
	else if (!Q_stricmp(action,"narrower")) Cvar_SetValue("ui_automapSliceHeight",std::max(64.0f,sliceHeight->value-64));
	else if (!Q_stricmp(action,"drag") && Cmd_Argc()==5 && valid) {
		const float dx = atof(Cmd_Argv(2)), dy = atof(Cmd_Argv(3));
		if (!std::isfinite(dx) || !std::isfinite(dy)) return;
		if (atoi(Cmd_Argv(4))) centre[2] = Com_Clamp(minimum[2]-halfSlice,maximum[2]+halfSlice,centre[2]-dy*4);
		else {
			const float aspect = cls.glconfig.vidWidth * 480.0f / (cls.glconfig.vidHeight * 640.0f);
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
		nextControl = (nextControl + 1) % current.count;
	}
	else if (!Q_stricmp(action,"lift") && current.liftCount) {
		centre=current.lifts[nextLift % current.liftCount].position;
		nextLift=(nextLift+1)%current.liftCount;
	}
	else if (!Q_stricmp(action,"fit")) {
		for (int i = 0; i < 2; ++i) centre[i] = (minimum[i]+maximum[i])/2;
		float x = 0, y = 0;
		for (int i=0;i<4;++i) {
			const auto p = Automap::Project({{i&1 ? maximum[0] : minimum[0], i&2 ? maximum[1] : minimum[1], centre[2]}},centre,yaw,tilt);
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
		centre[0] += std::cos(a)*dx-std::sin(a)*dy; centre[1] += std::sin(a)*dx+std::cos(a)*dy;
	} else Com_Printf("automap: zoomin zoomout up down wider narrower left right tilt centre fit control lift panleft panright panup pandown\n");
}
void Status() {
	Com_Printf("automap map=%s valid=%d triangles=%d edges=%d drawn=%d markers=%d shown=%d height=%.1f span=%.1f yaw=%.1f tilt=%.1f centre=%.1f,%.1f slice=%.1f lifts=%d lifts_shown=%d\n",
		loadedMap.c_str(), valid, int(triangles.size()), int(edges.size()), visibleTriangles, current.count, visibleMarkers,
		centre[2], span, yaw, tilt, centre[0], centre[1], sliceHeight->value,current.liftCount,visibleLifts);
	for (int i = 0; i < current.count; ++i) Com_Printf("automap control=%d enabled=%d position=%.1f,%.1f,%.1f\n",
		current.markers[i].entity, current.markers[i].enabled, current.markers[i].position[0], current.markers[i].position[1], current.markers[i].position[2]);
	for (int i=0;i<current.liftCount;++i) {
		const auto &lift=current.lifts[i];
		Com_Printf("automap lift=%d stops=%d position=%.1f,%.1f,%.1f\n",lift.entity,lift.count,lift.position[0],lift.position[1],lift.position[2]);
		for (int j=0;j<lift.count;++j) Com_Printf("automap lift=%d stop=%d at=%.1f,%.1f,%.1f\n",lift.entity,j,lift.stops[j][0],lift.stops[j][1],lift.stops[j][2]);
	}
}
}

void CL_InitAutomap() {
	sliceHeight=Cvar_Get("ui_automapSliceHeight","256",CVAR_ARCHIVE);
	Cvar_CheckRange(sliceHeight,64,4096,qtrue);
	Cmd_AddCommand("automap", Action); Cmd_AddCommand("automap_status", Status);
}
void CL_ResetAutomap() { loadedMap.clear(); triangles.clear(); edges.clear(); current = {}; valid = false; nextControl = nextLift = 0; recenter = true; }

void CL_DrawAutomap(const Automap::Frame *frame) {
	if (!frame || !re.DrawUiGeometry) return;
	current = *frame; current.count = Com_Clampi(0, Automap::MaxMarkers, current.count); current.map[63] = 0;
	current.liftCount=Com_Clampi(0,Automap::MaxLifts,current.liftCount);
	if (loadedMap != current.map) {
		loadedMap = current.map; valid = LoadMesh(current.map); Centre(); span=1024; yaw=45; tilt=55; nextControl=nextLift=0;
	}
	if (recenter) { Centre(); recenter = false; }
	const float halfSlice = sliceHeight->value * 0.5f;
	Batch batch;
	batch.Poly({{{Left,Top,0}},{{Left+Width,Top,0}},{{Left+Width,Top+Height,0}},{{Left,Top+Height,0}}}, {{8,13,18,255}});
	visibleTriangles = visibleMarkers = visibleLifts = 0;
	struct LiftLabel { Point p; bool up, down, unknown; };
	std::vector<LiftLabel> liftLabels;
	if (valid) {
		std::vector<std::pair<float,int>> order;
		for (int i = 0; i < int(triangles.size()); ++i) {
			const auto &t = triangles[i]; if (t.low > centre[2]+halfSlice || t.high < centre[2]-halfSlice) continue;
			Point p; for (int j=0;j<3;++j) p[j]=(t.p[0][j]+t.p[1][j]+t.p[2][j])/3;
			order.emplace_back(Automap::Project(p,centre,yaw,tilt)[2],i);
		}
		std::sort(order.rbegin(),order.rend());
		for (const auto &entry : order) {
			const auto &t = triangles[entry.second];
			Automap::Polygon poly = {{t.p[0],t.p[1],t.p[2]},3};
			poly = Automap::Clip(Automap::Clip(poly,centre[2]-halfSlice,true),centre[2]+halfSlice,false);
			if (poly.count < 3) continue;
			std::vector<Point> points; for (int i=0;i<poly.count;++i) points.push_back(Screen(poly.points[i]));
			const byte shade = byte(Com_Clamp(30,80,55+(t.low-centre[2])*0.25f));
			batch.Poly(points, {{byte(shade*0.6f),shade,byte(shade+16),255}}); ++visibleTriangles;
		}
		for (const auto &edge : edges) {
			Point a=edge.a,b=edge.b;
			if (a[2] > b[2]) std::swap(a,b);
			if (a[2] > centre[2]+halfSlice || b[2] < centre[2]-halfSlice) continue;
			const Point original=a;
			if (a[2]<centre[2]-halfSlice) for (int j=0;j<3;++j) a[j]=original[j]+(b[j]-original[j])*(centre[2]-halfSlice-original[2])/(b[2]-original[2]);
			if (b[2]>centre[2]+halfSlice) { const Point end=b; for (int j=0;j<3;++j) b[j]=original[j]+(end[j]-original[j])*(centre[2]+halfSlice-original[2])/(end[2]-original[2]); }
			batch.Line(Screen(a),Screen(b),{{83,116,129,255}});
		}
		const float aspect=640.0f*cls.glconfig.vidHeight/(480.0f*cls.glconfig.vidWidth);
		for (int i=0;i<current.count;++i) {
			const auto &marker=current.markers[i]; if (std::abs(marker.position[2]-centre[2])>halfSlice) continue;
			const auto p=Screen(marker.position);
			if (p[0]<Left || p[0]>Left+Width || p[1]<Top || p[1]>Top+Height) continue;
			const std::array<byte,4> color=marker.enabled ? std::array<byte,4>{{255,193,70,255}} : std::array<byte,4>{{130,135,140,255}};
			batch.Poly({{{p[0]-3*aspect,p[1]-3,0}},{{p[0]+3*aspect,p[1]-3,0}},{{p[0]+3*aspect,p[1]+3,0}},{{p[0]-3*aspect,p[1]+3,0}}},color);
			++visibleMarkers;
		}
		for (int i=0;i<current.liftCount;++i) {
			const auto &lift=current.lifts[i];
			const int count=Com_Clampi(0,Automap::MaxStops,lift.count);
			Point anchor=lift.position;
			bool available=std::abs(anchor[2]-centre[2])<=halfSlice, up=false, down=false;
			for (int j=0;j<count;++j) {
				if (lift.ordered && j==0) continue;
				Point a=lift.ordered && j>1 ? lift.stops[j-1] : lift.position,b=lift.stops[j];
				up|=b[2]>lift.position[2]+8; down|=b[2]<lift.position[2]-8;
				if (std::min(a[2],b[2])>centre[2]+halfSlice || std::max(a[2],b[2])<centre[2]-halfSlice) continue;
				if (!available && std::abs(b[2]-a[2])>0.01f) {
					const float t=Com_Clamp(0,1,(centre[2]-a[2])/(b[2]-a[2]));
					for (int k=0;k<3;++k) anchor[k]=a[k]+t*(b[k]-a[k]);
					available=true;
				}
				const Point start=a,end=b;
				if (std::abs(b[2]-a[2])>0.01f) {
					const float t0=Com_Clamp(0,1,(Com_Clamp(centre[2]-halfSlice,centre[2]+halfSlice,a[2])-a[2])/(b[2]-a[2]));
					const float t1=Com_Clamp(0,1,(Com_Clamp(centre[2]-halfSlice,centre[2]+halfSlice,b[2])-a[2])/(b[2]-a[2]));
					for (int k=0;k<3;++k) { a[k]=start[k]+t0*(end[k]-start[k]); b[k]=start[k]+t1*(end[k]-start[k]); }
				}
				a=Screen(a); b=Screen(b);
				const std::array<byte,4> green={{75,190,130,255}};
				batch.Line(a,b,green,1);
				const float dx=(b[0]-a[0])/aspect,dy=b[1]-a[1],length=std::hypot(dx,dy);
				if (length>4) {
					batch.Line(b,{{b[0]-(dx*4-dy*2)/length*aspect,b[1]-(dy*4+dx*2)/length,0}},green,1);
					batch.Line(b,{{b[0]-(dx*4+dy*2)/length*aspect,b[1]-(dy*4-dx*2)/length,0}},green,1);
				}
			}
			if (!available) continue;
			const auto p=Screen(anchor);
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
	const auto north=Automap::Project({{0,1,0}},{{0,0,0}},yaw,tilt);
	const float length=std::hypot(north[0],north[1]);
	const Point origin={{Left+22*aspect,Top+28,0}};
	const Point end={{origin[0]+north[0]/length*12*aspect,origin[1]+north[1]/length*12,0}};
	batch.Line(origin,end,{{170,205,215,255}},1);
	batch.Flush();
	for (const auto &label : liftLabels) {
		Label(label.p[0]-3*aspect,label.p[1]-5,label.unknown ? "?" : "L",true);
		if (label.up) Label(label.p[0]+5*aspect,label.p[1]-13,"^",true);
		if (label.down) Label(label.p[0]+5*aspect,label.p[1]+2,"v",true);
	}
	Label(end[0]-3*aspect,end[1]-11,"N");
	Label(26,379,valid ? va("Height %.0f | Slice %.0f | %s | Controls %d | Lifts %d | Gold: controls  Green: lifts",centre[2],sliceHeight->value,tilt ? "Isometric" : "Top-down",current.count,current.liftCount) : "Automap unavailable for this level");
	Label(26,398,"Drag: pan  Right-drag: height  Wheel: zoom  [ ]: slice  Q/E: rotate  T: tilt  Home: player  L: lift");
}
