/*
===========================================================================
Copyright (C) 2016, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

#include "tr_weather.h"
#include <utility>
#include <vector>
#include <cmath>

#ifdef REND2_SP
namespace
{
	struct SPWeatherZone
	{
		vec3_t mins, maxs, velocity;
	};
	struct SPWeatherState
	{
		std::vector<SPWeatherZone> zones, localWind;
		bool shake = false, fogOverride = false;
		float pain = 0.0f;
		vec3_t fogColor = {};
		unsigned windFrame = ~0u;
	} spWeather;
	std::vector<weatherBrushes_t> spWeatherBrushes;
	weatherBrushType_t spWeatherBrushType;

	bool InWeatherZone(const SPWeatherZone &zone, const vec3_t point)
	{
		return point[0] >= zone.mins[0] && point[0] <= zone.maxs[0] &&
			point[1] >= zone.mins[1] && point[1] <= zone.maxs[1] &&
			point[2] >= zone.mins[2] && point[2] <= zone.maxs[2];
	}

	void SPWindVelocity(const vec3_t point, vec3_t velocity);
}
#endif

namespace
{
	const int CHUNK_COUNT = 9;  // in 3x3 arrangement
	const float CHUNK_EXTENDS = 2000.f;
	const float HALF_CHUNK_EXTENDS = CHUNK_EXTENDS * 0.5f;

	struct rainVertex_t
	{
		vec3_t position;
		vec3_t velocity;
	};

	void RB_UpdateWindObject( windObject_t *wo )
	{
		if (wo->targetVelocityTimeRemaining == 0)
		{
			if (Q_flrand(0.f, 1.f) < wo->chanceOfDeadTime)
			{
				wo->targetVelocityTimeRemaining = Q_flrand(wo->deadTimeMinMax[0], wo->deadTimeMinMax[1]);
				VectorSet(wo->targetVelocity, 0.0f, 0.0f, 0.0f);
			}
			else
			{
				wo->targetVelocityTimeRemaining = Q_flrand(1000.f, 2500.f);
				VectorSet(
					wo->targetVelocity,
					Q_flrand(wo->minVelocity[0], wo->maxVelocity[0]),
					Q_flrand(wo->minVelocity[1], wo->maxVelocity[1]),
					Q_flrand(wo->minVelocity[2], wo->maxVelocity[2]));
			}
			return;
		}

		wo->targetVelocityTimeRemaining--;
		vec3_t deltaVelocity;
		VectorSubtract(wo->targetVelocity, wo->currentVelocity, deltaVelocity);
		float	DeltaVelocityLen = VectorNormalize(deltaVelocity);
#ifdef REND2_SP
		DeltaVelocityLen = MIN(DeltaVelocityLen, 0.01f);
#endif
		if (DeltaVelocityLen > 10.f)
		{
			DeltaVelocityLen = 10.f;
		}
		VectorScale(deltaVelocity, DeltaVelocityLen, deltaVelocity);
		VectorAdd(wo->currentVelocity, deltaVelocity, wo->currentVelocity);
	}

	void GenerateRainModel( weatherObject_t& ws, const int maxParticleCount )
	{
		const int mapExtentZ = (int)(tr.world->bmodels[0].bounds[1][2] - tr.world->bmodels[0].bounds[0][2]);
#ifdef REND2_SP
		const int PARTICLE_COUNT = MAX(maxParticleCount, (int)(maxParticleCount * mapExtentZ / CHUNK_EXTENDS));
#else
		const int PARTICLE_COUNT = (int)(maxParticleCount * mapExtentZ / CHUNK_EXTENDS);
#endif
		std::vector<rainVertex_t> rainVertices(PARTICLE_COUNT * CHUNK_COUNT);

		for ( int i = 0; i < rainVertices.size(); ++i )
		{
			rainVertex_t& vertex = rainVertices[i];
			vertex.position[0] = Q_flrand(-HALF_CHUNK_EXTENDS, HALF_CHUNK_EXTENDS);
			vertex.position[1] = Q_flrand(-HALF_CHUNK_EXTENDS, HALF_CHUNK_EXTENDS);
			vertex.position[2] = Q_flrand(tr.world->bmodels[0].bounds[0][2], tr.world->bmodels[0].bounds[1][2]);
			vertex.velocity[0] = 0.0f; //Q_flrand(0.0f, 0.0f);
			vertex.velocity[1] = 0.0f; //Q_flrand(0.0f, 0.0f);
			vertex.velocity[2] = 0.0f; //Q_flrand(-1.0f, 0.0f);
		}

		ws.lastVBO = R_CreateVBO(
			nullptr,
			sizeof(rainVertex_t) * rainVertices.size(),
			VBO_USAGE_XFB);
		ws.vbo = R_CreateVBO(
			(byte *)rainVertices.data(),
			sizeof(rainVertex_t) * rainVertices.size(),
			VBO_USAGE_XFB);
		ws.vboLastUpdateFrame = 0;

		ws.attribsTemplate[0].index = ATTR_INDEX_POSITION;
		ws.attribsTemplate[0].numComponents = 3;
		ws.attribsTemplate[0].offset = offsetof(rainVertex_t, position);
		ws.attribsTemplate[0].stride = sizeof(rainVertex_t);
		ws.attribsTemplate[0].type = GL_FLOAT;
		ws.attribsTemplate[0].vbo = nullptr;

		ws.attribsTemplate[1].index = ATTR_INDEX_COLOR;
		ws.attribsTemplate[1].numComponents = 3;
		ws.attribsTemplate[1].offset = offsetof(rainVertex_t, velocity);
		ws.attribsTemplate[1].stride = sizeof(rainVertex_t);
		ws.attribsTemplate[1].type = GL_FLOAT;
		ws.attribsTemplate[1].vbo = nullptr;
	}

	bool intersectPlane(const vec3_t n, const float dist, const vec3_t l0, const vec3_t l, float &t)
	{
		// assuming vectors are all normalized
		float denom = DotProduct(n, l);
		if (denom > 1e-6) {
			t = -(DotProduct(l0, n) + dist) / denom;
			return (t >= 0);
		}

		return false;
	}

	void GenerateDepthMap()
	{
		R_IssuePendingRenderCommands();
		R_InitNextFrame();
		RE_BeginFrame(STEREO_CENTER);

		vec3_t mapSize;
		vec3_t halfMapSize;
		VectorSubtract(
			tr.world->bmodels[0].bounds[0],
			tr.world->bmodels[0].bounds[1],
			mapSize);
		VectorScale(mapSize, -0.5f, halfMapSize);
		mapSize[2] = 0.0f;

		const vec3_t forward = {0.0f, 0.0f, -1.0f};
		const vec3_t left = {0.0f, 1.0f, 0.0f};
		const vec3_t up = {-1.0f, 0.0f, 0.0f};

		vec3_t viewOrigin;
		VectorMA(tr.world->bmodels[0].bounds[1], 0.5f, mapSize, viewOrigin);
		viewOrigin[2] = tr.world->bmodels[0].bounds[1][2];

		orientationr_t orientation;
		R_SetOrientationOriginAndAxis(orientation, viewOrigin, forward, left, up);

		const vec3_t viewBounds[2] = {
			{ 0.0f, -halfMapSize[1], -halfMapSize[0] },
			{ halfMapSize[2] * 2.0f, halfMapSize[1], halfMapSize[0] }
		};

		R_SetupViewParmsForOrthoRendering(
			tr.weatherDepthFbo->width,
			tr.weatherDepthFbo->height,
			tr.weatherDepthFbo,
			VPF_DEPTHCLAMP | VPF_DEPTHSHADOW | VPF_ORTHOGRAPHIC | VPF_NOVIEWMODEL,
			orientation,
			viewBounds);
		Matrix16Multiply(
			tr.viewParms.projectionMatrix,
			tr.viewParms.world.modelViewMatrix,
			tr.weatherSystem->weatherMVP);

		if (tr.weatherSystem->numWeatherBrushes > 0)
		{
			FBO_Bind(tr.weatherDepthFbo);

			qglViewport(0, 0, tr.weatherDepthFbo->width, tr.weatherDepthFbo->height);
			qglScissor(0, 0, tr.weatherDepthFbo->width, tr.weatherDepthFbo->height);

			if (tr.weatherSystem->weatherBrushType == WEATHER_BRUSHES_OUTSIDE) // used outside brushes
			{
				qglClearDepth(0.0f);
				GL_State(GLS_DEPTHMASK_TRUE | GLS_DEPTHFUNC_GREATER);
			}
			else // used inside brushes
			{
				qglClearDepth(1.0f);
				GL_State(GLS_DEPTHMASK_TRUE);
			}

			qglClear(GL_DEPTH_BUFFER_BIT);
			qglClearDepth(1.0f);
			qglEnable(GL_DEPTH_CLAMP);

			GL_Cull(CT_TWO_SIDED);
			vec4_t color = { 0.0f, 0.0f, 0.0f, 1.0f };
			backEnd.currentEntity = &tr.worldEntity;

			vec3_t stepSize = {
				abs(mapSize[0]) / tr.weatherDepthFbo->width,
				abs(mapSize[1]) / tr.weatherDepthFbo->height,
				0.0,
			};

			vec3_t up = {
				stepSize[0] * 0.5f,
				0.0f,
				0.0f
			};
			vec3_t left = {
				0.0f,
				stepSize[1] * 0.5f,
				0.0f
			};
			vec3_t traceVec = {
				0.0f,
				0.0f,
				-1.0f
			};

			for (int i = 0; i < tr.weatherSystem->numWeatherBrushes; i++)
			{
				RE_BeginFrame(STEREO_CENTER);
				weatherBrushes_t *currentWeatherBrush = &tr.weatherSystem->weatherBrushes[i];

				// RBSP brushes actually store their bounding box in the first 6 planes! Nice
				vec3_t mins = {
					-currentWeatherBrush->planes[0][3],
					-currentWeatherBrush->planes[2][3],
					-currentWeatherBrush->planes[4][3],
				};
				vec3_t maxs = {
					currentWeatherBrush->planes[1][3],
					currentWeatherBrush->planes[3][3],
					currentWeatherBrush->planes[5][3],
				};

				ivec2_t numSteps = {
					int((maxs[0] - mins[0]) / stepSize[0]) + 2,
					int((maxs[1] - mins[1]) / stepSize[1]) + 2
				};

				vec2_t rayOrigin = {
					tr.world->bmodels[0].bounds[0][0] + (floorf((mins[0] - tr.world->bmodels[0].bounds[0][0]) / stepSize[0]) + 0.5f) * stepSize[0],
					tr.world->bmodels[0].bounds[0][1] + (floorf((mins[1] - tr.world->bmodels[0].bounds[0][1]) / stepSize[1]) + 0.5f) * stepSize[1]
				};

				for (int y = 0; y < (int)numSteps[1]; y++)
				{
					for (int x = 0; x < (int)numSteps[0]; x++)
					{
						vec3_t rayPos = {
							rayOrigin[0] + x * stepSize[0],
							rayOrigin[1] + y * stepSize[1],
							tr.world->bmodels[0].bounds[1][2]
						};

						// Find intersection point with the brush
						float t = 0.0f;
						for (int j = 0; j < currentWeatherBrush->numPlanes; j++)
						{
							vec3_t plane_normal;
							float plane_dist;
							if (tr.weatherSystem->weatherBrushType == WEATHER_BRUSHES_OUTSIDE)
							{
								plane_normal[0] = currentWeatherBrush->planes[j][0];
								plane_normal[1] = currentWeatherBrush->planes[j][1];
								plane_normal[2] = currentWeatherBrush->planes[j][2];
								plane_dist = -currentWeatherBrush->planes[j][3];
							}
							else
							{
								plane_normal[0] = -currentWeatherBrush->planes[j][0];
								plane_normal[1] = -currentWeatherBrush->planes[j][1];
								plane_normal[2] = -currentWeatherBrush->planes[j][2];
								plane_dist = currentWeatherBrush->planes[j][3];
							}

							float dist = 0.0f;
							if (intersectPlane(plane_normal, plane_dist, rayPos, traceVec, dist))
								t = MAX(t, dist);
						}

						bool hit = true;
						rayPos[2] -= t;

						// Now test if the intersected point is actually on the brush
						for (int j = 0; j < currentWeatherBrush->numPlanes; j++)
						{
							vec4_t *plane = &currentWeatherBrush->planes[j];
							vec3_t normal = {
								currentWeatherBrush->planes[j][0],
								currentWeatherBrush->planes[j][1],
								currentWeatherBrush->planes[j][2]
							};
							if (DotProduct(rayPos, normal) > currentWeatherBrush->planes[j][3] + 1e-3)
								hit = false;
						}

						if (!hit)
							continue;

						// Just draw it when batch is full
						if (tess.numVertexes + 4 >= SHADER_MAX_VERTEXES || tess.numIndexes + 6 >= SHADER_MAX_INDEXES)
						{
							RB_UpdateVBOs(ATTR_POSITION);
							GLSL_VertexAttribsState(ATTR_POSITION, NULL);
							GLSL_BindProgram(&tr.textureColorShader);
							GLSL_SetUniformMatrix4x4(
								&tr.textureColorShader,
								UNIFORM_MODELVIEWPROJECTIONMATRIX,
								tr.weatherSystem->weatherMVP);
							R_DrawElementsVBO(tess.numIndexes, tess.firstIndex, tess.minIndex, tess.maxIndex);

							RB_CommitInternalBufferData();

							tess.numIndexes = 0;
							tess.numVertexes = 0;
							tess.firstIndex = 0;
							tess.multiDrawPrimitives = 0;
							tess.externalIBO = nullptr;
						}

						RB_AddQuadStamp(rayPos, left, up, color);
					}
				}
				R_NewFrameSync();
			}

			// draw remaining quads
			RB_UpdateVBOs(ATTR_POSITION);
			GLSL_VertexAttribsState(ATTR_POSITION, NULL);
			GLSL_BindProgram(&tr.textureColorShader);
			GLSL_SetUniformMatrix4x4(
				&tr.textureColorShader,
				UNIFORM_MODELVIEWPROJECTIONMATRIX,
				tr.weatherSystem->weatherMVP);
			R_DrawElementsVBO(tess.numIndexes, tess.firstIndex, tess.minIndex, tess.maxIndex);

			RB_CommitInternalBufferData();

			tess.numIndexes = 0;
			tess.numVertexes = 0;
			tess.firstIndex = 0;
			tess.multiDrawPrimitives = 0;
			tess.externalIBO = nullptr;

			qglDisable(GL_DEPTH_CLAMP);
		}

		RE_BeginFrame(STEREO_CENTER);

		if (tr.weatherSystem->numWeatherBrushes > 0)
			tr.viewParms.flags |= VPF_NOCLEAR;

		tr.refdef.numDrawSurfs = 0;
		tr.refdef.drawSurfs = backEndData->drawSurfs;

		tr.refdef.num_entities = 0;
		tr.refdef.entities = backEndData->entities;

		tr.refdef.num_dlights = 0;
		tr.refdef.dlights = backEndData->dlights;

		tr.refdef.fistDrawSurf = 0;

		tr.skyPortalEntities = 0;

		tr.viewParms.targetFbo = tr.weatherDepthFbo;
		tr.viewParms.currentViewParm = 0;
		Com_Memcpy(&tr.cachedViewParms[0], &tr.viewParms, sizeof(viewParms_t));
		tr.numCachedViewParms = 1;

		RB_UpdateConstants(&tr.refdef);

		R_GenerateDrawSurfs(&tr.viewParms, &tr.refdef);
		R_SortAndSubmitDrawSurfs(tr.refdef.drawSurfs, tr.refdef.numDrawSurfs);

		R_IssuePendingRenderCommands();
		tr.refdef.numDrawSurfs = 0;
		tr.numCachedViewParms = 0;

		RE_EndScene();

		R_NewFrameSync();
	}

	void RB_SimulateWeather(weatherObject_t *ws, vec2_t *zoneOffsets, int zoneIndex)
	{
		if (ws->vboLastUpdateFrame == backEndData->realFrameNumber ||
			tr.weatherSystem->frozen)
		{
			// Already simulated for this frame
			return;
		}

		// Intentionally switched. Previous frame's VBO would be in ws.vbo and
		// this frame's VBO would be ws.lastVBO.
		VBO_t *lastRainVBO = ws->vbo;
		VBO_t *rainVBO = ws->lastVBO;

		Allocator& frameAllocator = *backEndData->perFrameMemory;

		DrawItem item = {};
		item.renderState.transformFeedback = true;
		item.transformFeedbackBuffer = {rainVBO->vertexesVBO, 0, rainVBO->vertexesSize};
		item.program = &tr.weatherUpdateShader;

		const size_t numAttribs = ARRAY_LEN(ws->attribsTemplate);
		item.numAttributes = numAttribs;
		item.attributes = ojkAllocArray<vertexAttribute_t>(
			*backEndData->perFrameMemory, numAttribs);
		memcpy(
			item.attributes,
			ws->attribsTemplate,
			sizeof(*item.attributes) * numAttribs);
		item.attributes[0].vbo = lastRainVBO;
		item.attributes[1].vbo = lastRainVBO;

		UniformDataWriter uniformDataWriter;
		uniformDataWriter.Start(&tr.weatherUpdateShader);

		const vec2_t mapZExtents = {
			tr.world->bmodels[0].bounds[0][2],
			tr.world->bmodels[0].bounds[1][2]
		};
		const float frictionInverse = 0.7f;
		vec3_t envForce = {
			tr.weatherSystem->windDirection[0] * frictionInverse,
			tr.weatherSystem->windDirection[1] * frictionInverse,
			-ws->gravity * frictionInverse
		};
#ifdef REND2_SP
		SPWindVelocity(nullptr, envForce);
		envForce[2] -= ws->gravity;
		VectorScale(envForce, frictionInverse, envForce);
		vec3_t center;
		for (int i = 0; i < 3; ++i)
			center[i] = floorf(backEnd.viewParms.ori.origin[i] / CHUNK_EXTENDS + 0.5f) * CHUNK_EXTENDS;
		center[2] = 0.0f;
		uniformDataWriter.SetUniformVec3(UNIFORM_VIEWORIGIN, center);
		uniformDataWriter.SetUniformInt(UNIFORM_SPWINDCOUNT, spWeather.localWind.size());
		if (!spWeather.localWind.empty())
		{
			vec3_t mins[MAX_WINDOBJECTS], maxs[MAX_WINDOBJECTS], velocities[MAX_WINDOBJECTS];
			for (size_t i = 0; i < spWeather.localWind.size(); ++i)
			{
				VectorCopy(spWeather.localWind[i].mins, mins[i]);
				VectorCopy(spWeather.localWind[i].maxs, maxs[i]);
				VectorScale(spWeather.localWind[i].velocity, frictionInverse, velocities[i]);
			}
			uniformDataWriter.SetUniformVec3(UNIFORM_SPWINDMINS, mins[0], spWeather.localWind.size());
			uniformDataWriter.SetUniformVec3(UNIFORM_SPWINDMAXS, maxs[0], spWeather.localWind.size());
			uniformDataWriter.SetUniformVec3(UNIFORM_SPWINDVELOCITY, velocities[0], spWeather.localWind.size());
		}
#endif
		vec4_t randomOffset = {
			Q_flrand(-4.0f, 4.0f),
			Q_flrand(-4.0f, 4.0f),
			Q_flrand(0.0f, 1.0f),
			tr.world->bmodels[0].bounds[1][2] - backEnd.viewParms.ori.origin[2]
		};
		uniformDataWriter.SetUniformVec2(UNIFORM_MAPZEXTENTS, mapZExtents);
		uniformDataWriter.SetUniformVec3(UNIFORM_ENVFORCE, envForce);
		uniformDataWriter.SetUniformVec4(UNIFORM_RANDOMOFFSET, randomOffset);
		uniformDataWriter.SetUniformVec2(UNIFORM_ZONEOFFSET, (float*)zoneOffsets, 9);
		uniformDataWriter.SetUniformInt(UNIFORM_CHUNK_PARTICLES, ws->particleCount);

		item.uniformData = uniformDataWriter.Finish(*backEndData->perFrameMemory);

		const GLuint currentFrameUbo = backEndData->currentFrame->ubo;
		const UniformBlockBinding uniformBlockBindings[] = {
			{ currentFrameUbo, tr.sceneUboOffset, UNIFORM_BLOCK_SCENE }
		};
		DrawItemSetUniformBlockBindings(
			item, uniformBlockBindings, frameAllocator);

		item.draw.type = DRAW_COMMAND_ARRAYS;
		item.draw.numInstances = 1;
		item.draw.primitiveType = GL_POINTS;
		item.draw.params.arrays.numVertices = ws->particleCount * CHUNK_COUNT;

		// This is a bit dodgy. Push this towards the front of the queue so we
		// guarantee this happens before the actual drawing
		const uint32_t key = RB_CreateSortKey(item, 0, SS_ENVIRONMENT);
		RB_AddDrawItem(backEndData->currentPass, key, item);

		ws->vboLastUpdateFrame = backEndData->realFrameNumber;
		std::swap(ws->lastVBO, ws->vbo);
	}
}

void R_InitWeatherForMap()
{
#ifdef REND2_SP
	if (!tr.world || !tr.weatherSystem)
		return;
#endif
	for (int i = 0; i < NUM_WEATHER_TYPES; i++)
		if (tr.weatherSystem->weatherSlots[i].active)
			GenerateRainModel(tr.weatherSystem->weatherSlots[i], maxWeatherTypeParticles[i]);
	GenerateDepthMap();
}

void R_InitWeatherSystem()
{
#ifdef REND2_SP
	if (tr.weatherSystem)
		R_ShutdownWeatherSystem();
	spWeather = SPWeatherState();
#endif
	Com_Printf("Initializing weather system\n");
	tr.weatherSystem =
		(weatherSystem_t *)Z_Malloc(sizeof(*tr.weatherSystem), TAG_R_TERRAIN, qtrue);
#ifdef REND2_SP
	if (tr.world)
	{
		tr.weatherSystem->numWeatherBrushes = spWeatherBrushes.size();
		tr.weatherSystem->weatherBrushType = spWeatherBrushType;
		for (size_t i = 0; i < spWeatherBrushes.size(); ++i)
			tr.weatherSystem->weatherBrushes[i] = spWeatherBrushes[i];
	}
	else
		spWeatherBrushes.clear();
#endif
	tr.weatherSystem->weatherSurface.surfaceType = SF_WEATHER;
	tr.weatherSystem->frozen = false;
	tr.weatherSystem->activeWeatherTypes = 0;
	tr.weatherSystem->constWindDirection[0] = .0f;
	tr.weatherSystem->constWindDirection[1] = .0f;

	for (int i = 0; i < NUM_WEATHER_TYPES; i++)
		tr.weatherSystem->weatherSlots[i].active = false;
}

void R_ShutdownWeatherSystem()
{
	if (tr.weatherSystem != nullptr)
	{
#ifdef REND2_SP
		R_IssuePendingRenderCommands();
		R_BindNullVBO();
		for (weatherObject_t &slot : tr.weatherSystem->weatherSlots)
		{
			R_SP_DeleteVBO(slot.vbo);
			R_SP_DeleteVBO(slot.lastVBO);
		}
		// Keep BSP masks when the game resets effects without unloading the map.
		spWeatherBrushes.assign(tr.weatherSystem->weatherBrushes,
			tr.weatherSystem->weatherBrushes + tr.weatherSystem->numWeatherBrushes);
		spWeatherBrushType = tr.weatherSystem->weatherBrushType;
		spWeather = SPWeatherState();
#endif
		Com_Printf("Shutting down weather system\n");

		Z_Free(tr.weatherSystem);
		tr.weatherSystem = nullptr;
	}
	else
	{
		ri.Printf(PRINT_DEVELOPER,
			"Weather system shutdown requested, but it is already shut down.\n");
	}
}

/*
===============
WE_ParseVector
===============
*/
qboolean WE_ParseVector(const char **text, int count, float *v) {
	char	*token;
	int		i;

	// FIXME: spaces are currently required after parens, should change parseext...
	token = COM_ParseExt(text, qfalse);
	if (strcmp(token, "(")) {
		ri.Printf(PRINT_WARNING, "WARNING: missing parenthesis in weather effect\n");
		return qfalse;
	}

	for (i = 0; i < count; i++) {
		token = COM_ParseExt(text, qfalse);
		if (!token[0]) {
			ri.Printf(PRINT_WARNING, "WARNING: missing vector element in weather effect\n");
			return qfalse;
		}
		v[i] = atof(token);
	}

	token = COM_ParseExt(text, qfalse);
	if (strcmp(token, ")")) {
		ri.Printf(PRINT_WARNING, "WARNING: missing parenthesis in weather effect\n");
		return qfalse;
	}

	return qtrue;
}

void R_AddWeatherBrush(uint8_t numPlanes, vec4_t *planes)
{
#ifdef REND2_SP
	if (!tr.weatherSystem || numPlanes < 4 || numPlanes > 64)
		return;
#endif
	if (tr.weatherSystem->numWeatherBrushes >= (MAX_WEATHER_ZONES * 2))
	{
		ri.Printf(PRINT_WARNING, "Max weather brushes hit. Skipping new inside/outside brush\n");
		return;
	}
	tr.weatherSystem->weatherBrushes[tr.weatherSystem->numWeatherBrushes].numPlanes = numPlanes;
	memcpy(tr.weatherSystem->weatherBrushes[tr.weatherSystem->numWeatherBrushes].planes, planes, numPlanes * sizeof(vec4_t));

	tr.weatherSystem->numWeatherBrushes++;
}

void RE_WorldEffectCommand(const char *command)
{
#ifdef REND2_SP
	if (!tr.weatherSystem)
		R_InitWeatherSystem();
#endif
	if (!command)
	{
		return;
	}

#ifdef REND2_SP
	COM_ParseSession session;
#else
	COM_BeginParseSession("RE_WorldEffectCommand");
#endif

	const char	*token;//, *origCommand;

	token = COM_ParseExt(&command, qfalse);

	if (!token)
	{
		return;
	}

	//Die - clean up the whole weather system -rww
	if (Q_stricmp(token, "die") == 0)
	{
		for (int i = 0; i < NUM_WEATHER_TYPES; i++)
			tr.weatherSystem->weatherSlots[i].active = false;
		tr.weatherSystem->activeWeatherTypes = 0;
		tr.weatherSystem->frozen = false;
#ifdef REND2_SP
		tr.weatherSystem->activeWindObjects = 0;
		VectorClear(tr.weatherSystem->constWindDirection);
		VectorClear(tr.weatherSystem->windDirection);
		spWeather.localWind.clear();
		spWeather.pain = 0.0f;
		spWeather.shake = false;
#endif
		return;
	}

	// Clear - Removes All Particle Clouds And Wind Zones
	//----------------------------------------------------
	else if (Q_stricmp(token, "clear") == 0)
	{
		for (int i = 0; i < NUM_WEATHER_TYPES; i++)
			tr.weatherSystem->weatherSlots[i].active = false;
		tr.weatherSystem->activeWeatherTypes = 0;
		tr.weatherSystem->activeWindObjects = 0;
		tr.weatherSystem->frozen = false;
#ifdef REND2_SP
		VectorClear(tr.weatherSystem->constWindDirection);
		VectorClear(tr.weatherSystem->windDirection);
		spWeather.localWind.clear();
#endif
	}

	// Freeze / UnFreeze - Stops All Particle Motion Updates
	//--------------------------------------------------------
	else if (Q_stricmp(token, "freeze") == 0)
	{
		tr.weatherSystem->frozen = !tr.weatherSystem->frozen;
	}

	//// Add a zone
	////---------------
	else if (Q_stricmp(token, "zone") == 0)
	{
#ifdef REND2_SP
		vec3_t mins, maxs;
		if (WE_ParseVector(&command, 3, mins) && WE_ParseVector(&command, 3, maxs))
			R_AddWeatherZone(mins, maxs);
#else
		ri.Printf(PRINT_DEVELOPER, "Weather zones aren't used in rend2, but inside/outside brushes\n");
#endif
	}

#ifdef REND2_SP
	else if (Q_stricmp(token, "windzone") == 0)
	{
		SPWeatherZone zone = {};
		if (spWeather.localWind.size() >= MAX_WINDOBJECTS ||
			!WE_ParseVector(&command, 3, zone.mins) || !WE_ParseVector(&command, 3, zone.maxs))
			return;
		if (!WE_ParseVector(&command, 3, zone.velocity))
			VectorSet(zone.velocity, 0.0f, 800.0f, 0.0f);
		for (int i = 0; i < 3; ++i)
			if (zone.mins[i] > zone.maxs[i])
				std::swap(zone.mins[i], zone.maxs[i]);
		VectorScale(zone.velocity, 0.001f, zone.velocity);
		spWeather.localWind.push_back(zone);
	}
#endif

	// Basic Wind
	//------------
	else if (Q_stricmp(token, "wind") == 0)
	{
#ifdef REND2_SP
		if (tr.weatherSystem->activeWindObjects >= MAX_WINDOBJECTS)
			return;
#endif
		windObject_t *currentWindObject = &tr.weatherSystem->windSlots[tr.weatherSystem->activeWindObjects];
		currentWindObject->chanceOfDeadTime = 0.3f;
		currentWindObject->deadTimeMinMax[0] = 1000.0f;
		currentWindObject->deadTimeMinMax[1] = 3000.0f;
		currentWindObject->maxVelocity[0] = 1.5f;
		currentWindObject->maxVelocity[1] = 1.5f;
		currentWindObject->maxVelocity[2] = 0.01f;
		currentWindObject->minVelocity[0] = -1.5f;
		currentWindObject->minVelocity[1] = -1.5f;
		currentWindObject->minVelocity[2] = -0.01f;
		currentWindObject->targetVelocityTimeRemaining = 0;
		tr.weatherSystem->activeWindObjects++;
	}

	// Constant Wind
	//---------------
	else if (Q_stricmp(token, "constantwind") == 0)
	{
		vec3_t parsedWind;
		vec3_t defaultWind = { 0.f, 0.8f, 0.f };
		if (!WE_ParseVector(&command, 3, parsedWind))
			VectorAdd(
				tr.weatherSystem->constWindDirection,
				defaultWind,
				tr.weatherSystem->constWindDirection);
		else
			VectorMA(
				tr.weatherSystem->constWindDirection,
				0.001f,
				parsedWind,
				tr.weatherSystem->constWindDirection);
	}

	// Gusting Wind
	//--------------
	else if (Q_stricmp(token, "gustingwind") == 0)
	{
#ifdef REND2_SP
		if (tr.weatherSystem->activeWindObjects >= MAX_WINDOBJECTS)
			return;
#endif
		windObject_t *currentWindObject = &tr.weatherSystem->windSlots[tr.weatherSystem->activeWindObjects];
		currentWindObject->chanceOfDeadTime = 0.3f;
		currentWindObject->deadTimeMinMax[0] = 2000.0f;
		currentWindObject->deadTimeMinMax[1] = 4000.0f;
		currentWindObject->maxVelocity[0] = 3.0f;
		currentWindObject->maxVelocity[1] = 3.0f;
		currentWindObject->maxVelocity[2] = 0.1f;
		currentWindObject->minVelocity[0] = -3.0f;
		currentWindObject->minVelocity[1] = -3.0f;
		currentWindObject->minVelocity[2] = -0.1f;
		currentWindObject->targetVelocityTimeRemaining = 0;
		tr.weatherSystem->activeWindObjects++;
	}

	// Create A Rain Storm
	//---------------------
	else if (Q_stricmp(token, "lightrain") == 0)
	{
		/*nCloud.Initialize(500, "gfx/world/rain.jpg", 3);
		nCloud.mHeight = 80.0f;
		nCloud.mWidth = 1.2f;
		nCloud.mGravity = 2000.0f;
		nCloud.mFilterMode = 1;
		nCloud.mBlendMode = 1;
		nCloud.mFade = 100.0f;
		nCloud.mColor = 0.5f;
		nCloud.mOrientWithVelocity = true;
		nCloud.mWaterParticles = true;*/
		if (!tr.weatherSystem->weatherSlots[WEATHER_RAIN].active)
			tr.weatherSystem->activeWeatherTypes++;

		tr.weatherSystem->weatherSlots[WEATHER_RAIN].particleCount = 1000;
		tr.weatherSystem->weatherSlots[WEATHER_RAIN].active = true;
		tr.weatherSystem->weatherSlots[WEATHER_RAIN].gravity = 2.0f;
		tr.weatherSystem->weatherSlots[WEATHER_RAIN].fadeDistance = 6000.f;

		tr.weatherSystem->weatherSlots[WEATHER_RAIN].size[0] = 1.5f;
		tr.weatherSystem->weatherSlots[WEATHER_RAIN].size[1] = 14.0f;

		tr.weatherSystem->weatherSlots[WEATHER_RAIN].velocityOrientationScale = 1.0f;

		imgType_t type = IMGTYPE_COLORALPHA;
		int flags = IMGFLAG_CLAMPTOEDGE;
		tr.weatherSystem->weatherSlots[WEATHER_RAIN].drawImage = R_FindImageFile("gfx/world/rain.jpg", type, flags);

		VectorSet4(tr.weatherSystem->weatherSlots[WEATHER_RAIN].color, 0.5f, 0.5f, 0.5f, 0.5f);
		VectorScale(
			tr.weatherSystem->weatherSlots[WEATHER_RAIN].color,
			0.5f,
			tr.weatherSystem->weatherSlots[WEATHER_RAIN].color);
	}

	// Create A Rain Storm
	//---------------------
	else if (Q_stricmp(token, "rain") == 0)
	{
		/*nCloud.Initialize(1000, "gfx/world/rain.jpg", 3);
		nCloud.mHeight = 80.0f;
		nCloud.mWidth = 1.2f;
		nCloud.mGravity = 2000.0f;
		nCloud.mFilterMode = 1;
		nCloud.mBlendMode = 1;
		nCloud.mFade = 100.0f;
		nCloud.mColor = 0.5f;
		nCloud.mOrientWithVelocity = true;
		nCloud.mWaterParticles = true;*/
		if (!tr.weatherSystem->weatherSlots[WEATHER_RAIN].active)
			tr.weatherSystem->activeWeatherTypes++;

		tr.weatherSystem->weatherSlots[WEATHER_RAIN].particleCount = 2000;
		tr.weatherSystem->weatherSlots[WEATHER_RAIN].active = true;
		tr.weatherSystem->weatherSlots[WEATHER_RAIN].gravity = 2.0f;
		tr.weatherSystem->weatherSlots[WEATHER_RAIN].fadeDistance = 6000.f;

		tr.weatherSystem->weatherSlots[WEATHER_RAIN].size[0] = 1.5f;
		tr.weatherSystem->weatherSlots[WEATHER_RAIN].size[1] = 14.0f;

		tr.weatherSystem->weatherSlots[WEATHER_RAIN].velocityOrientationScale = 1.0f;

		imgType_t type = IMGTYPE_COLORALPHA;
		int flags = IMGFLAG_CLAMPTOEDGE;
		tr.weatherSystem->weatherSlots[WEATHER_RAIN].drawImage = R_FindImageFile("gfx/world/rain.jpg", type, flags);

		VectorSet4(tr.weatherSystem->weatherSlots[WEATHER_RAIN].color, 0.5f, 0.5f, 0.5f, 0.5f);
		VectorScale(
			tr.weatherSystem->weatherSlots[WEATHER_RAIN].color,
			0.5f,
			tr.weatherSystem->weatherSlots[WEATHER_RAIN].color);
	}

	// Create A Rain Storm
	//---------------------
	else if (Q_stricmp(token, "acidrain") == 0)
	{
#ifdef REND2_SP
		spWeather.pain = 0.1f;
#endif
		/*nCloud.Initialize(1000, "gfx/world/rain.jpg", 3);
		nCloud.mHeight = 80.0f;
		nCloud.mWidth = 2.0f;
		nCloud.mGravity = 2000.0f;
		nCloud.mFilterMode = 1;
		nCloud.mBlendMode = 1;
		nCloud.mFade = 100.0f;

		nCloud.mColor[0] = 0.34f;
		nCloud.mColor[1] = 0.70f;
		nCloud.mColor[2] = 0.34f;
		nCloud.mColor[3] = 0.70f;

		nCloud.mOrientWithVelocity = true;
		nCloud.mWaterParticles = true;*/
		if (!tr.weatherSystem->weatherSlots[WEATHER_RAIN].active)
			tr.weatherSystem->activeWeatherTypes++;

		tr.weatherSystem->weatherSlots[WEATHER_RAIN].particleCount = 2000;
		tr.weatherSystem->weatherSlots[WEATHER_RAIN].active = true;
		tr.weatherSystem->weatherSlots[WEATHER_RAIN].gravity = 2.0f;
		tr.weatherSystem->weatherSlots[WEATHER_RAIN].fadeDistance = 6000.0f;

		tr.weatherSystem->weatherSlots[WEATHER_RAIN].size[0] = 2.0f;
		tr.weatherSystem->weatherSlots[WEATHER_RAIN].size[1] = 14.0f;

		tr.weatherSystem->weatherSlots[WEATHER_RAIN].velocityOrientationScale = 1.0f;

		imgType_t type = IMGTYPE_COLORALPHA;
		int flags = IMGFLAG_CLAMPTOEDGE;
		tr.weatherSystem->weatherSlots[WEATHER_RAIN].drawImage = R_FindImageFile("gfx/world/rain.jpg", type, flags);

		VectorSet4(tr.weatherSystem->weatherSlots[WEATHER_RAIN].color, 0.34f, 0.7f, 0.34f, 0.7f);
		VectorScale(
			tr.weatherSystem->weatherSlots[WEATHER_RAIN].color,
			0.7f,
			tr.weatherSystem->weatherSlots[WEATHER_RAIN].color);
	}

	// Create A Rain Storm
	//---------------------
	else if (Q_stricmp(token, "heavyrain") == 0)
	{
		/*nCloud.Initialize(1000, "gfx/world/rain.jpg", 3);
		nCloud.mHeight = 80.0f;
		nCloud.mWidth = 1.2f;
		nCloud.mGravity = 2800.0f;
		nCloud.mFilterMode = 1;
		nCloud.mBlendMode = 1;
		nCloud.mFade = 15.0f;
		nCloud.mColor = 0.5f;
		nCloud.mOrientWithVelocity = true;
		nCloud.mWaterParticles = true;*/
		if (!tr.weatherSystem->weatherSlots[WEATHER_RAIN].active)
			tr.weatherSystem->activeWeatherTypes++;

		tr.weatherSystem->weatherSlots[WEATHER_RAIN].particleCount = 5000;
		tr.weatherSystem->weatherSlots[WEATHER_RAIN].active = true;
		tr.weatherSystem->weatherSlots[WEATHER_RAIN].gravity = 2.8f;
		tr.weatherSystem->weatherSlots[WEATHER_RAIN].fadeDistance = 6000.0f;

		tr.weatherSystem->weatherSlots[WEATHER_RAIN].size[0] = 1.5f;
		tr.weatherSystem->weatherSlots[WEATHER_RAIN].size[1] = 14.0f;

		tr.weatherSystem->weatherSlots[WEATHER_RAIN].velocityOrientationScale = 1.0f;

		imgType_t type = IMGTYPE_COLORALPHA;
		int flags = IMGFLAG_CLAMPTOEDGE;
		tr.weatherSystem->weatherSlots[WEATHER_RAIN].drawImage = R_FindImageFile("gfx/world/rain", type, flags);

		VectorSet4(tr.weatherSystem->weatherSlots[WEATHER_RAIN].color, 0.5f, 0.5f, 0.5f, 0.5f);
		VectorScale(
			tr.weatherSystem->weatherSlots[WEATHER_RAIN].color,
			0.5f,
			tr.weatherSystem->weatherSlots[WEATHER_RAIN].color);
	}

	// Create A Snow Storm
	//---------------------
	else if (Q_stricmp(token, "snow") == 0)
	{
		/*nCloud.Initialize(1000, "gfx/effects/snowflake1.bmp");
		nCloud.mBlendMode = 1;
		nCloud.mRotationChangeNext = 0;
		nCloud.mColor = 0.75f;
		nCloud.mWaterParticles = true;*/
		if (!tr.weatherSystem->weatherSlots[WEATHER_SNOW].active)
			tr.weatherSystem->activeWeatherTypes++;

		tr.weatherSystem->weatherSlots[WEATHER_SNOW].particleCount = 1000;
		tr.weatherSystem->weatherSlots[WEATHER_SNOW].active = true;
		tr.weatherSystem->weatherSlots[WEATHER_SNOW].gravity = 0.3f;
		tr.weatherSystem->weatherSlots[WEATHER_SNOW].fadeDistance = 6000.0f;

		tr.weatherSystem->weatherSlots[WEATHER_SNOW].size[0] = 1.5f;
		tr.weatherSystem->weatherSlots[WEATHER_SNOW].size[1] = 1.5f;

		tr.weatherSystem->weatherSlots[WEATHER_SNOW].velocityOrientationScale = 0.0f;

		imgType_t type = IMGTYPE_COLORALPHA;
		int flags = IMGFLAG_CLAMPTOEDGE;
		tr.weatherSystem->weatherSlots[WEATHER_SNOW].drawImage = R_FindImageFile("gfx/effects/snowflake1", type, flags);

		VectorSet4(tr.weatherSystem->weatherSlots[WEATHER_SNOW].color, 0.75f, 0.75f, 0.75f, 0.75f);
		VectorScale(
			tr.weatherSystem->weatherSlots[WEATHER_SNOW].color,
			0.75f,
			tr.weatherSystem->weatherSlots[WEATHER_SNOW].color);
	}

	// Create A Some stuff
	//---------------------
	else if (Q_stricmp(token, "spacedust") == 0)
	{
		/*nCloud.Initialize(count, "gfx/effects/snowpuff1.tga");
		nCloud.mHeight = 1.2f;
		nCloud.mWidth = 1.2f;
		nCloud.mGravity = 0.0f;
		nCloud.mBlendMode = 1;
		nCloud.mRotationChangeNext = 0;
		nCloud.mColor = 0.75f;
		nCloud.mWaterParticles = true;
		nCloud.mMass.mMax = 30.0f;
		nCloud.mMass.mMin = 10.0f;
		nCloud.mSpawnRange.mMins[0] = -1500.0f;
		nCloud.mSpawnRange.mMins[1] = -1500.0f;
		nCloud.mSpawnRange.mMins[2] = -1500.0f;
		nCloud.mSpawnRange.mMaxs[0] = 1500.0f;
		nCloud.mSpawnRange.mMaxs[1] = 1500.0f;
		nCloud.mSpawnRange.mMaxs[2] = 1500.0f;*/
		int count;
		token = COM_ParseExt(&command, qfalse);
		count = atoi(token);
#ifdef REND2_SP
		count = Com_Clampi(0, maxWeatherTypeParticles[WEATHER_SPACEDUST], count);
#endif

		if (!tr.weatherSystem->weatherSlots[WEATHER_SPACEDUST].active)
			tr.weatherSystem->activeWeatherTypes++;

		tr.weatherSystem->weatherSlots[WEATHER_SPACEDUST].particleCount = count;
		tr.weatherSystem->weatherSlots[WEATHER_SPACEDUST].active = true;
		tr.weatherSystem->weatherSlots[WEATHER_SPACEDUST].gravity = 0.0f;
		tr.weatherSystem->weatherSlots[WEATHER_SPACEDUST].fadeDistance = 3000.f;

		tr.weatherSystem->weatherSlots[WEATHER_SPACEDUST].size[0] = 2.5f;
		tr.weatherSystem->weatherSlots[WEATHER_SPACEDUST].size[1] = 2.5f;

		tr.weatherSystem->weatherSlots[WEATHER_SPACEDUST].velocityOrientationScale = 0.0f;

		imgType_t type = IMGTYPE_COLORALPHA;
		int flags = IMGFLAG_CLAMPTOEDGE;
		tr.weatherSystem->weatherSlots[WEATHER_SPACEDUST].drawImage = R_FindImageFile("gfx/effects/snowpuff1", type, flags);

		VectorSet4(tr.weatherSystem->weatherSlots[WEATHER_SPACEDUST].color, 0.75f, 0.75f, 0.75f, 0.75f);
		VectorScale(
			tr.weatherSystem->weatherSlots[WEATHER_SPACEDUST].color,
			0.75f,
			tr.weatherSystem->weatherSlots[WEATHER_SPACEDUST].color);
	}

	// Create A Sand Storm
	//---------------------
	else if (Q_stricmp(token, "sand") == 0)
	{
		/*nCloud.Initialize(400, "gfx/effects/alpha_smoke2b.tga");

		nCloud.mGravity = 0;
		nCloud.mWidth = 70;
		nCloud.mHeight = 70;
		nCloud.mColor[0] = 0.9f;
		nCloud.mColor[1] = 0.6f;
		nCloud.mColor[2] = 0.0f;
		nCloud.mColor[3] = 0.5f;
		nCloud.mFade = 5.0f;
		nCloud.mMass.mMax = 30.0f;
		nCloud.mMass.mMin = 10.0f;
		nCloud.mSpawnRange.mMins[2] = -150;
		nCloud.mSpawnRange.mMaxs[2] = 150;

		nCloud.mRotationChangeNext = 0;*/
		if (!tr.weatherSystem->weatherSlots[WEATHER_SAND].active)
			tr.weatherSystem->activeWeatherTypes++;

		tr.weatherSystem->weatherSlots[WEATHER_SAND].particleCount = 400;
		tr.weatherSystem->weatherSlots[WEATHER_SAND].active = true;
		tr.weatherSystem->weatherSlots[WEATHER_SAND].gravity = 0.0f;
		tr.weatherSystem->weatherSlots[WEATHER_SAND].fadeDistance = 2400.f;

		tr.weatherSystem->weatherSlots[WEATHER_SAND].size[0] = 300.f;
		tr.weatherSystem->weatherSlots[WEATHER_SAND].size[1] = 300.f;

		tr.weatherSystem->weatherSlots[WEATHER_SAND].velocityOrientationScale = 0.0f;

		imgType_t type = IMGTYPE_COLORALPHA;
		int flags = IMGFLAG_CLAMPTOEDGE;
		tr.weatherSystem->weatherSlots[WEATHER_SAND].drawImage = R_FindImageFile("gfx/effects/alpha_smoke2b", type, flags);

		VectorSet4(tr.weatherSystem->weatherSlots[WEATHER_SAND].color, 0.9f, 0.6f, 0.0f, 0.5f);
	}

	// Create Blowing Clouds Of Fog
	//------------------------------
	else if (Q_stricmp(token, "fog") == 0)
	{
		/*nCloud.Initialize(60, "gfx/effects/alpha_smoke2b.tga");
		nCloud.mBlendMode = 1;
		nCloud.mGravity = 0;
		nCloud.mWidth = 70;
		nCloud.mHeight = 70;
		nCloud.mColor = 0.2f;
		nCloud.mFade = 5.0f;
		nCloud.mMass.mMax = 30.0f;
		nCloud.mMass.mMin = 10.0f;
		nCloud.mSpawnRange.mMins[2] = -150;
		nCloud.mSpawnRange.mMaxs[2] = 150;

		nCloud.mRotationChangeNext = 0;*/
		if (!tr.weatherSystem->weatherSlots[WEATHER_FOG].active)
			tr.weatherSystem->activeWeatherTypes++;

		tr.weatherSystem->weatherSlots[WEATHER_FOG].particleCount = 60;
		tr.weatherSystem->weatherSlots[WEATHER_FOG].active = true;
		tr.weatherSystem->weatherSlots[WEATHER_FOG].gravity = 0.0f;
		tr.weatherSystem->weatherSlots[WEATHER_FOG].fadeDistance = 2400.f;

		tr.weatherSystem->weatherSlots[WEATHER_FOG].size[0] = 300.f;
		tr.weatherSystem->weatherSlots[WEATHER_FOG].size[1] = 300.f;

		tr.weatherSystem->weatherSlots[WEATHER_FOG].velocityOrientationScale = 0.0f;

		imgType_t type = IMGTYPE_COLORALPHA;
		int flags = IMGFLAG_CLAMPTOEDGE;
		tr.weatherSystem->weatherSlots[WEATHER_FOG].drawImage = R_FindImageFile("gfx/effects/alpha_smoke2b", type, flags);

		VectorSet4(tr.weatherSystem->weatherSlots[WEATHER_FOG].color, 0.2f, 0.2f, 0.2f, 0.2f);
		VectorScale(tr.weatherSystem->weatherSlots[WEATHER_FOG].color, 0.2f, tr.weatherSystem->weatherSlots[WEATHER_FOG].color);
	}

	// Create Heavy Rain Particle Cloud
	//-----------------------------------
	else if (Q_stricmp(token, "heavyrainfog") == 0)
	{
		/*nCloud.Initialize(70, "gfx/effects/alpha_smoke2b.tga");
		nCloud.mBlendMode = 1;
		nCloud.mGravity = 0;
		nCloud.mWidth = 100;
		nCloud.mHeight = 100;
		nCloud.mColor = 0.3f;
		nCloud.mFade = 1.0f;
		nCloud.mMass.mMax = 10.0f;
		nCloud.mMass.mMin = 5.0f;

		nCloud.mSpawnRange.mMins = -(nCloud.mSpawnPlaneDistance*1.25f);
		nCloud.mSpawnRange.mMaxs = (nCloud.mSpawnPlaneDistance*1.25f);
		nCloud.mSpawnRange.mMins[2] = -150;
		nCloud.mSpawnRange.mMaxs[2] = 150;

		nCloud.mRotationChangeNext = 0;*/
		if (!tr.weatherSystem->weatherSlots[WEATHER_FOG].active)
			tr.weatherSystem->activeWeatherTypes++;

		tr.weatherSystem->weatherSlots[WEATHER_FOG].particleCount = 70;
		tr.weatherSystem->weatherSlots[WEATHER_FOG].active = true;
		tr.weatherSystem->weatherSlots[WEATHER_FOG].gravity = 0.0f;
		tr.weatherSystem->weatherSlots[WEATHER_FOG].fadeDistance = 2400.f;

		tr.weatherSystem->weatherSlots[WEATHER_FOG].size[0] = 300.f;
		tr.weatherSystem->weatherSlots[WEATHER_FOG].size[1] = 300.f;

		tr.weatherSystem->weatherSlots[WEATHER_FOG].velocityOrientationScale = 0.0f;

		imgType_t type = IMGTYPE_COLORALPHA;
		int flags = IMGFLAG_CLAMPTOEDGE;
		tr.weatherSystem->weatherSlots[WEATHER_FOG].drawImage = R_FindImageFile("gfx/effects/alpha_smoke2b", type, flags);

		VectorSet4(tr.weatherSystem->weatherSlots[WEATHER_FOG].color, 0.3f, 0.3f, 0.3f, 0.3f);
		VectorScale(tr.weatherSystem->weatherSlots[WEATHER_FOG].color, 0.3f, tr.weatherSystem->weatherSlots[WEATHER_FOG].color);
	}

	// Create Blowing Clouds Of Fog
	//------------------------------
	else if (Q_stricmp(token, "light_fog") == 0)
	{
		/*nCloud.Initialize(40, "gfx/effects/alpha_smoke2b.tga");
		nCloud.mBlendMode = 1;
		nCloud.mGravity = 0;
		nCloud.mWidth = 100;
		nCloud.mHeight = 100;
		nCloud.mColor[0] = 0.19f;
		nCloud.mColor[1] = 0.6f;
		nCloud.mColor[2] = 0.7f;
		nCloud.mColor[3] = 0.12f;
		nCloud.mFade = 0.10f;
		nCloud.mMass.mMax = 30.0f;
		nCloud.mMass.mMin = 10.0f;
		nCloud.mSpawnRange.mMins[2] = -150;
		nCloud.mSpawnRange.mMaxs[2] = 150;

		nCloud.mRotationChangeNext = 0;*/
		if (!tr.weatherSystem->weatherSlots[WEATHER_FOG].active)
			tr.weatherSystem->activeWeatherTypes++;

		tr.weatherSystem->weatherSlots[WEATHER_FOG].particleCount = 70;
		tr.weatherSystem->weatherSlots[WEATHER_FOG].active = true;
		tr.weatherSystem->weatherSlots[WEATHER_FOG].gravity = 0.0f;
		tr.weatherSystem->weatherSlots[WEATHER_FOG].fadeDistance = 2000.f;

		tr.weatherSystem->weatherSlots[WEATHER_FOG].size[0] = 300.f;
		tr.weatherSystem->weatherSlots[WEATHER_FOG].size[1] = 300.f;

		tr.weatherSystem->weatherSlots[WEATHER_FOG].velocityOrientationScale = 0.0f;

		imgType_t type = IMGTYPE_COLORALPHA;
		int flags = IMGFLAG_CLAMPTOEDGE;
		tr.weatherSystem->weatherSlots[WEATHER_FOG].drawImage = R_FindImageFile("gfx/effects/alpha_smoke2b", type, flags);

		VectorSet4(tr.weatherSystem->weatherSlots[WEATHER_FOG].color, 0.19f, 0.6f, 0.7f, 0.12f);
		VectorScale(tr.weatherSystem->weatherSlots[WEATHER_FOG].color, 0.12f, tr.weatherSystem->weatherSlots[WEATHER_FOG].color);
	}

	else if (Q_stricmp(token, "outsideshake") == 0)
	{
#ifdef REND2_SP
		spWeather.shake = !spWeather.shake;
#else
		ri.Printf(PRINT_DEVELOPER, "outsideshake isn't supported in MP\n");
#endif
	}
	else if (Q_stricmp(token, "outsidepain") == 0)
	{
#ifdef REND2_SP
		const char *value = COM_ParseExt(&command, qfalse);
		spWeather.pain = value[0] ? MAX(0.0f, (float)atof(value)) : !spWeather.pain;
#else
		ri.Printf(PRINT_DEVELOPER, "outsidepain isn't supported in MP\n");
#endif
	}
	else
	{
		ri.Printf(PRINT_ALL, "Weather Effect: Please enter a valid command.\n");
		ri.Printf(PRINT_ALL, "	die\n");
		ri.Printf(PRINT_ALL, "	clear\n");
		ri.Printf(PRINT_ALL, "	freeze\n");
		ri.Printf(PRINT_ALL, "	zone (mins) (maxs)\n");
		ri.Printf(PRINT_ALL, "	wind\n");
		ri.Printf(PRINT_ALL, "	constantwind (velocity)\n");
		ri.Printf(PRINT_ALL, "	gustingwind\n");
		//ri.Printf(PRINT_ALL, "	windzone (mins) (maxs) (velocity)\n");
		ri.Printf(PRINT_ALL, "	lightrain\n");
		ri.Printf(PRINT_ALL, "	rain\n");
		ri.Printf(PRINT_ALL, "	acidrain\n");
		ri.Printf(PRINT_ALL, "	heavyrain\n");
		ri.Printf(PRINT_ALL, "	snow\n");
		ri.Printf(PRINT_ALL, "	spacedust (count)\n");
		ri.Printf(PRINT_ALL, "	sand\n");
		ri.Printf(PRINT_ALL, "	fog\n");
		ri.Printf(PRINT_ALL, "	heavyrainfog\n");
		ri.Printf(PRINT_ALL, "	light_fog\n");
		ri.Printf(PRINT_ALL, "	outsideshake\n"); // not available in MP
		ri.Printf(PRINT_ALL, "	outsidepain\n"); // not available in MP
	}
}

void R_WorldEffect_f(void)
{
	char temp[2048] = { 0 };
	ri.Cmd_ArgsBuffer(temp, sizeof(temp));
	RE_WorldEffectCommand(temp);
}

void R_AddWeatherSurfaces()
{
	assert(tr.weatherSystem);

	if (tr.weatherSystem->activeWeatherTypes == 0 &&
		r_debugWeather->integer == 0)
		return;

	R_AddDrawSurf(
		(surfaceType_t *)&tr.weatherSystem->weatherSurface,
		REFENTITYNUM_WORLD,
		tr.weatherInternalShader,
		0, /* fogIndex */
		qfalse, /* dlightMap */
		qfalse, /* postRender */
		0 /* cubemapIndex */
	);
}

void RB_SurfaceWeather( srfWeather_t *surf )
{
	assert(tr.weatherSystem);

	weatherSystem_t& ws = *tr.weatherSystem;
	assert(surf == &ws.weatherSurface);

	RB_EndSurface();

	const float numMinZonesX = std::floor((abs(tr.world->bmodels[0].bounds[0][0]) / CHUNK_EXTENDS) + 0.5f);
	const float numMinZonesY = std::floor((abs(tr.world->bmodels[0].bounds[0][1]) / CHUNK_EXTENDS) + 0.5f);
	vec3_t viewOrigin;
	VectorCopy(backEnd.viewParms.ori.origin, viewOrigin);
	float centerZoneOffsetX =
		std::floor((viewOrigin[0] / CHUNK_EXTENDS) + 0.5f);
	float centerZoneOffsetY =
		std::floor((viewOrigin[1] / CHUNK_EXTENDS) + 0.5f);

	vec2_t zoneOffsets[9];
	GLint  zoneMapping[9];
	int		centerZoneIndex;
	{
		int chunkIndex = 0;
		int currentIndex = 0;
		for (int y = -1; y <= 1; ++y)
		{
			for (int x = -1; x <= 1; ++x, ++currentIndex)
			{
				chunkIndex  = ((int(centerZoneOffsetX + numMinZonesX) + x + 1) % 3 + 3) % 3;
				chunkIndex += (((int(centerZoneOffsetY + numMinZonesY) + y + 1) % 3 + 3) % 3) * 3;
				VectorSet2(
					zoneOffsets[chunkIndex],
					x,
					y);
				zoneMapping[currentIndex] = chunkIndex;
				if (x == 0 && y == 0)
					centerZoneIndex = currentIndex;
			}
		}
	}

#ifndef REND2_SP
	// Get current global wind vector
	VectorCopy(tr.weatherSystem->constWindDirection, tr.weatherSystem->windDirection);
	for (int i = 0; i < tr.weatherSystem->activeWindObjects; i++)
	{
		windObject_t *windObject = &tr.weatherSystem->windSlots[i];
		RB_UpdateWindObject(windObject);
		VectorAdd(windObject->currentVelocity, tr.weatherSystem->windDirection, tr.weatherSystem->windDirection);
	}
#endif

	Allocator& frameAllocator = *backEndData->perFrameMemory;

	// Simulate and render all the weather zones
	for (int weatherType = 0; weatherType < NUM_WEATHER_TYPES; weatherType++)
	{
		weatherObject_t *weatherObject = &ws.weatherSlots[weatherType];
		if (!weatherObject->active)
			continue;

		if (weatherObject->vbo == nullptr)
			GenerateRainModel(
				tr.weatherSystem->weatherSlots[weatherType],
				maxWeatherTypeParticles[weatherType]);

		RB_SimulateWeather(weatherObject, &zoneOffsets[0], centerZoneIndex);

		vec4_t viewInfo = {
			weatherObject->size[0],
			weatherObject->size[1],
			weatherObject->velocityOrientationScale,
			weatherObject->fadeDistance
		};

		int stateBits = weatherType == WEATHER_SAND ?
			GLS_DEPTHFUNC_LESS | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA :
			GLS_DEPTHFUNC_LESS | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE;

		DrawItem item = {};

		item.renderState.stateBits = stateBits;
		item.renderState.cullType = CT_TWO_SIDED;
		item.renderState.depthRange = { 0.0f, 1.0f };
		item.program = &tr.weatherShader;

		const size_t numAttribs = ARRAY_LEN(weatherObject->attribsTemplate);
		item.numAttributes = numAttribs;
		item.attributes = ojkAllocArray<vertexAttribute_t>(
			*backEndData->perFrameMemory, numAttribs);
		memcpy(
			item.attributes,
			weatherObject->attribsTemplate,
			sizeof(*item.attributes) * numAttribs);
		item.attributes[0].vbo = weatherObject->vbo;
		item.attributes[1].vbo = weatherObject->vbo;

		item.draw.type = DRAW_COMMAND_ARRAYS;
		item.draw.numInstances = 1;
		item.draw.primitiveType = GL_POINTS;
		item.draw.params.arrays.numVertices = weatherObject->particleCount;

		//TODO: Cull non visable zones
		int currentIndex = 0;
		for (int y = -1; y <= 1; ++y)
		{
			for (int x = -1; x <= 1; ++x, ++currentIndex)
			{
				const GLuint currentFrameUbo = backEndData->currentFrame->ubo;
				const UniformBlockBinding uniformBlockBindings[] = {
					{ currentFrameUbo, tr.cameraUboOffsets[tr.viewParms.currentViewParm], UNIFORM_BLOCK_CAMERA }
				};
				DrawItemSetUniformBlockBindings(
					item, uniformBlockBindings, frameAllocator);

				UniformDataWriter uniformDataWriter;
				uniformDataWriter.Start(&tr.weatherShader);
				uniformDataWriter.SetUniformVec2(
					UNIFORM_ZONEOFFSET,
					(centerZoneOffsetX + x) * CHUNK_EXTENDS,
					(centerZoneOffsetY + y) * CHUNK_EXTENDS);
				uniformDataWriter.SetUniformFloat(UNIFORM_TIME, backEnd.refdef.frameTime);
				uniformDataWriter.SetUniformVec4(UNIFORM_COLOR, weatherObject->color);
				uniformDataWriter.SetUniformVec4(UNIFORM_VIEWINFO, viewInfo);
				uniformDataWriter.SetUniformMatrix4x4(UNIFORM_SHADOWMVP, tr.weatherSystem->weatherMVP);
				item.uniformData = uniformDataWriter.Finish(*backEndData->perFrameMemory);

				SamplerBindingsWriter samplerBindingsWriter;
				samplerBindingsWriter.AddStaticImage(tr.weatherDepthImage, TB_SHADOWMAP);
				if (weatherObject->drawImage != NULL)
					samplerBindingsWriter.AddStaticImage(weatherObject->drawImage, TB_DIFFUSEMAP);
				else
					samplerBindingsWriter.AddStaticImage(tr.whiteImage, TB_DIFFUSEMAP);

				item.samplerBindings = samplerBindingsWriter.Finish(
					frameAllocator, &item.numSamplerBindings);

				item.draw.params.arrays.firstVertex = weatherObject->particleCount * zoneMapping[currentIndex];

				uint32_t key = RB_CreateSortKey(item, 15, SS_SEE_THROUGH);
				RB_AddDrawItem(backEndData->currentPass, key, item);
			}
		}
	}
}

#ifdef REND2_SP
namespace
{
	void SPWindVelocity(const vec3_t point, vec3_t velocity)
	{
		VectorClear(velocity);
		if (!tr.weatherSystem)
			return;
		weatherSystem_t &ws = *tr.weatherSystem;
		if (spWeather.windFrame != backEndData->realFrameNumber)
		{
			spWeather.windFrame = backEndData->realFrameNumber;
			VectorCopy(ws.constWindDirection, ws.windDirection);
			for (int i = 0; i < ws.activeWindObjects; ++i)
			{
				if (!ws.frozen)
					RB_UpdateWindObject(&ws.windSlots[i]);
				VectorAdd(ws.windDirection, ws.windSlots[i].currentVelocity, ws.windDirection);
			}
			ws.windSpeed = VectorLength(ws.windDirection) * 1000.0f;
		}
		VectorCopy(ws.windDirection, velocity);
		if (point)
			for (const SPWeatherZone &zone : spWeather.localWind)
				if (InWeatherZone(zone, point))
					VectorAdd(velocity, zone.velocity, velocity);
	}
}

bool R_GetWindVector(vec3_t windVector, vec3_t atPoint)
{
	SPWindVelocity(atPoint, windVector);
	VectorNormalize(windVector);
	return tr.weatherSystem != nullptr;
}

bool R_GetWindGusting(vec3_t atPoint)
{
	vec3_t velocity;
	SPWindVelocity(atPoint, velocity);
	return VectorLength(velocity) > 1.0f;
}

void R_AddWeatherZone(vec3_t mins, vec3_t maxs)
{
	if (spWeather.zones.size() >= MAX_WEATHER_ZONES)
		return;
	SPWeatherZone zone = {};
	for (int i = 0; i < 3; ++i)
	{
		zone.mins[i] = MIN(mins[i], maxs[i]);
		zone.maxs[i] = MAX(mins[i], maxs[i]);
	}
	spWeather.zones.push_back(zone);
}

bool R_IsOutside(vec3_t pos)
{
	if (!tr.world || !tr.weatherSystem || !pos)
		return false;
	const bool outsideBrushes = tr.weatherSystem->weatherBrushType == WEATHER_BRUSHES_OUTSIDE;
	bool inZone = spWeather.zones.empty();
	for (const SPWeatherZone &zone : spWeather.zones)
		inZone |= InWeatherZone(zone, pos);
	if (!inZone)
		return !outsideBrushes;
	const int contents = ri.CM_PointContents(pos, 0);
	if (contents & (CONTENTS_SOLID | CONTENTS_WATER))
		return false;
	return outsideBrushes ? (contents & CONTENTS_OUTSIDE) != 0 : (contents & CONTENTS_INSIDE) == 0;
}

bool R_IsShaking(vec3_t pos)
{
	return spWeather.shake && R_IsOutside(pos);
}

float R_IsOutsideCausingPain(vec3_t pos)
{
	return R_IsOutside(pos) ? spWeather.pain : 0.0f;
}

float R_GetChanceOfSaberFizz()
{
	if (!tr.weatherSystem)
		return 0.0f;
	float chance = 0.0f;
	int count = 0;
	for (int i = WEATHER_RAIN; i <= WEATHER_SPACEDUST; ++i)
		if (tr.weatherSystem->weatherSlots[i].active)
		{
			chance += tr.weatherSystem->weatherSlots[i].gravity / 20.0f;
			++count;
		}
	return count ? chance / count : 0.0f;
}

bool R_SetTempGlobalFogColor(vec3_t color)
{
	if (!tr.world || !tr.world->globalFog || !color)
		return false;
	R_IssuePendingRenderCommands();
	fog_t &fog = tr.world->fogs[tr.world->globalFogIndex];
	if (color[0] || color[1] || color[2])
	{
		if (!spWeather.fogOverride)
			VectorCopy(fog.parms.color, spWeather.fogColor);
		spWeather.fogOverride = true;
		VectorCopy(color, fog.parms.color);
	}
	else if (spWeather.fogOverride)
	{
		VectorCopy(spWeather.fogColor, fog.parms.color);
		spWeather.fogOverride = false;
	}
	VectorScale(fog.parms.color, tr.identityLight, fog.color);
	fog.color[3] = 1.0f;
	return true;
}
#endif
