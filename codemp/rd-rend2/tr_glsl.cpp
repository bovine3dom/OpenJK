/*
===========================================================================
Copyright (C) 2006-2009 Robert Beckebans <trebor_7@users.sourceforge.net>

This file is part of XreaL source code.

XreaL source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

XreaL source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with XreaL source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// tr_glsl.c
#include "tr_local.h"
#include "tr_allocator.h"
#include "glsl_shaders.h"
#include <map>
#include <tuple>
#include <utility>
#include <vector>

#ifdef REND2_SP
static const memtag_t shaderProgramTag = TAG_SHADERTEXT;
#else
static const memtag_t shaderProgramTag = TAG_GENERAL;
#endif

void GLSL_BindNullProgram(void);

const uniformBlockInfo_t uniformBlocksInfo[UNIFORM_BLOCK_COUNT] = {
	{ 0, "Camera", sizeof(CameraBlock) },
	{ 1, "Scene", sizeof(SceneBlock) },
	{ 2, "Lights", sizeof(LightsBlock) },
	{ 3, "Fogs", sizeof(FogsBlock) },
	{ 4, "Entity", sizeof(EntityBlock) },
	{ 5, "ShaderInstance", sizeof(ShaderInstanceBlock) },
	{ 6, "Bones", sizeof(SkeletonBoneMatricesBlock) },
	{ 10, "SurfaceSprite", sizeof(SurfaceSpriteBlock) },
};

typedef struct uniformInfo_s
{
	const char *name;
	int type;
	int size;
}
uniformInfo_t;

// These must be in the same order as in uniform_t in tr_local.h.
static uniformInfo_t uniformsInfo[] =
{
	{ "u_DiffuseMap",  GLSL_INT, 1 },
	{ "u_LightMap",    GLSL_INT, 1 },
	{ "u_NormalMap",   GLSL_INT, 1 },
	{ "u_DeluxeMap",   GLSL_INT, 1 },
	{ "u_SpecularMap", GLSL_INT, 1 },
	{ "u_SSAOMap",     GLSL_INT, 1 },
	{ "u_SSAOAmbientOnly", GLSL_INT, 1 },
	{ "u_SSAOParams", GLSL_VEC4, 1 },
	{ "u_SSAODebug", GLSL_INT, 1 },

	{ "u_TextureMap", GLSL_INT, 1 },
	{ "u_LevelsMap",  GLSL_INT, 1 },
	{ "u_CubeMap",    GLSL_INT, 1 },
	{ "u_EnvBrdfMap", GLSL_INT, 1 },

	{ "u_ScreenImageMap", GLSL_INT, 1 },
	{ "u_ScreenDepthMap", GLSL_INT, 1 },

	{ "u_ShadowMap",  GLSL_INT, 1 },
	{ "u_ShadowMap2", GLSL_INT, 1 },

	{ "u_ShadowMvp",  GLSL_MAT4x4, 1 },
	{ "u_ShadowMvp2", GLSL_MAT4x4, 1 },
	{ "u_ShadowMvp3", GLSL_MAT4x4, 1 },

	{ "u_EnableTextures", GLSL_VEC4, 1 },

	{ "u_DiffuseTexMatrix",  GLSL_VEC4, 1 },
	{ "u_DiffuseTexOffTurb", GLSL_VEC4, 1 },

	{ "u_TCGen0",        GLSL_INT, 1 },
	{ "u_TCGen0Vector0", GLSL_VEC3, 1 },
	{ "u_TCGen0Vector1", GLSL_VEC3, 1 },
	{ "u_TCGen1",        GLSL_INT, 1 },

	{ "u_ColorGen",  GLSL_INT, 1 },
	{ "u_AlphaGen",  GLSL_INT, 1 },
	{ "u_Color",     GLSL_VEC4, 1 },
	{ "u_BaseColor", GLSL_VEC4, 1 },
	{ "u_VertColor", GLSL_VEC4, 1 },

	{ "u_DlightInfo",     GLSL_VEC4, 1 },
	{ "u_LightForward",   GLSL_VEC3, 1 },
	{ "u_LightUp",        GLSL_VEC3, 1 },
	{ "u_LightRight",     GLSL_VEC3, 1 },
	{ "u_LightOrigin",    GLSL_VEC4, 1 },
	{ "u_ModelLightDir",  GLSL_VEC3, 1 },
	{ "u_LightRadius",    GLSL_FLOAT, 1 },
	{ "u_AmbientLight",   GLSL_VEC3, 1 },
	{ "u_DirectedLight",  GLSL_VEC3, 1 },
	{ "u_Disintegration", GLSL_VEC4, 1 },
	{ "u_LightMask",    GLSL_INT, 1 },
	{ "u_FogIndex",    GLSL_INT, 1 },

	{ "u_FogColorMask", GLSL_VEC4, 1 },
	{ "u_HazeColor", GLSL_VEC4, 1 },
	{ "u_HazeParams", GLSL_VEC4, 1 },
	{ "u_HazeMins", GLSL_VEC4, 1 },
	{ "u_HazeMaxs", GLSL_VEC4, 1 },
	{ "u_HazeOrigin", GLSL_VEC3, 1 },
	{ "u_AtmosphereMap", GLSL_INT, 1 },
	{ "u_AtmosphereParams", GLSL_VEC4, 1 },
	{ "u_AtmosphereSun", GLSL_VEC4, 1 },
	{ "u_AtmosphereCloudColor", GLSL_VEC3, 1 },
	{ "u_VolumeMap", GLSL_INT, 1 },
	{ "u_VolumeParams", GLSL_VEC4, 1 },
	{ "u_VolumeMatrix", GLSL_MAT4x4, 1 },
	{ "u_VolumeMins", GLSL_VEC4, 4 },
	{ "u_VolumeMaxs", GLSL_VEC4, 4 },
	{ "u_VolumeColor", GLSL_VEC4, 4 },
	{ "u_VolumeTorchOrigin", GLSL_VEC4, 1 },
	{ "u_VolumeTorchDirection", GLSL_VEC4, 1 },
	{ "u_VolumeTorchParams", GLSL_VEC4, 1 },

	{ "u_ModelMatrix",               GLSL_MAT4x4, 1 },
	{ "u_ModelViewProjectionMatrix", GLSL_MAT4x4, 1 },

	{ "u_Time",          GLSL_FLOAT, 1 },
	{ "u_VertexLerp" ,   GLSL_FLOAT, 1 },
	{ "u_NormalScale",   GLSL_VEC4, 1 },
	{ "u_SpecularScale", GLSL_VEC4, 1 },
	{ "u_MaterialParams", GLSL_VEC4, 1 },
	{ "u_GlassParams", GLSL_VEC4, 1 },
	{ "u_GlassDebug", GLSL_INT, 1 },
	{ "u_CubeMapMins", GLSL_VEC3, 1 },
	{ "u_CubeMapMaxs", GLSL_VEC3, 1 },
	{ "u_SoftParticleParams", GLSL_VEC4, 1 },
	{ "u_SSSParams", GLSL_VEC4, 1 },
	{ "u_SkinBounds", GLSL_VEC4, 1 },
	{ "u_TorchShadowMap", GLSL_INT, 1 },
	{ "u_TorchEnabled", GLSL_INT, 1 },
	{ "u_CapsuleA", GLSL_VEC4, 12 },
	{ "u_CapsuleB", GLSL_VEC4, 12 },
	{ "u_CapsuleMins", GLSL_VEC3, 1 },
	{ "u_CapsuleMaxs", GLSL_VEC3, 1 },
	{ "u_ParallaxBias",  GLSL_FLOAT, 1 },

	{ "u_ViewInfo",				GLSL_VEC4, 1 },
	{ "u_ViewOrigin",			GLSL_VEC3, 1 },
	{ "u_LocalViewOrigin",		GLSL_VEC3, 1 },
	{ "u_ViewForward",			GLSL_VEC3, 1 },
	{ "u_ViewLeft",				GLSL_VEC3, 1 },
	{ "u_ViewUp",				GLSL_VEC3, 1 },

	{ "u_InvTexRes",           GLSL_VEC2, 1 },
	{ "u_AutoExposureMinMax",  GLSL_VEC2, 1 },
	{ "u_ToneMinAvgMaxLinear", GLSL_VEC3, 1 },

	{ "u_CubeMapInfo", GLSL_VEC4, 1 },

	{ "u_AlphaTestType",		GLSL_INT, 1 },

	{ "u_FXVolumetricBase",		GLSL_FLOAT, 1 },
	{ "u_MapZExtents",			GLSL_VEC2, 1 },
	{ "u_ZoneOffset",			GLSL_VEC2, 9 },
	{ "u_EnvForce",				GLSL_VEC3, 1 },
	{ "u_RandomOffset",			GLSL_VEC4, 1 },
	{ "u_ChunkParticles",		GLSL_INT, 1 },
#ifdef REND2_SP
	{ "u_SPWindCount", GLSL_INT, 1 },
	{ "u_SPWindMins", GLSL_VEC3, 10 },
	{ "u_SPWindMaxs", GLSL_VEC3, 10 },
	{ "u_SPWindVelocity", GLSL_VEC3, 10 },
#endif
};

static void GLSL_PrintProgramInfoLog(GLuint object, qboolean developerOnly)
{
	char msgPart[1024];
	int maxLength = 0;
	int printLevel = developerOnly ? PRINT_DEVELOPER : PRINT_ALL;

	qglGetProgramiv(object, GL_INFO_LOG_LENGTH, &maxLength);

	if (maxLength <= 0)
	{
		ri.Printf(printLevel, "No compile log.\n");
		return;
	}

	ri.Printf(printLevel, "compile log:\n");

	if (maxLength < 1023)
	{
		qglGetProgramInfoLog(object, maxLength, &maxLength, msgPart);

		msgPart[maxLength + 1] = '\0';

		ri.Printf(printLevel, "%s\n", msgPart);
	}
	else
	{
		char *msg = (char *)Z_Malloc(maxLength, TAG_SHADERTEXT);

		qglGetProgramInfoLog(object, maxLength, &maxLength, msg);

		for(int i = 0; i < maxLength; i += 1023)
		{
			Q_strncpyz(msgPart, msg + i, sizeof(msgPart));

			ri.Printf(printLevel, "%s\n", msgPart);
		}

		Z_Free(msg);
	}
}

static void GLSL_PrintShaderInfoLog(GLuint object, qboolean developerOnly)
{
	char           *msg;
	static char     msgPart[1024];
	int             maxLength = 0;
	int             i;
	int             printLevel = developerOnly ? PRINT_DEVELOPER : PRINT_ALL;

	qglGetShaderiv(object, GL_INFO_LOG_LENGTH, &maxLength);

	if (maxLength <= 0)
	{
		ri.Printf(printLevel, "No compile log.\n");
		return;
	}

	ri.Printf(printLevel, "compile log:\n");

	if (maxLength < 1023)
	{
		qglGetShaderInfoLog(object, maxLength, &maxLength, msgPart);

		msgPart[maxLength + 1] = '\0';

		ri.Printf(printLevel, "%s\n", msgPart);
	}
	else
	{
		msg = (char *)Z_Malloc(maxLength, TAG_SHADERTEXT);

		qglGetShaderInfoLog(object, maxLength, &maxLength, msg);

		for(i = 0; i < maxLength; i += 1024)
		{
			Q_strncpyz(msgPart, msg + i, sizeof(msgPart));

			ri.Printf(printLevel, "%s\n", msgPart);
		}

		Z_Free(msg);
	}
}

static void GLSL_PrintShaderSource(GLuint shader)
{
	int maxLength = 0;
	qglGetShaderiv(shader, GL_SHADER_SOURCE_LENGTH, &maxLength);

	if ( maxLength == 0 )
	{
		Com_Printf("No shader source available to output\n");
		return;
	}

	char *msg = (char *)Z_Malloc(maxLength, TAG_SHADERTEXT);
	qglGetShaderSource(shader, maxLength, nullptr, msg);

	for (int i = 0; i < maxLength; i += 1023)
	{
		char msgPart[1024];
		Q_strncpyz(msgPart, msg + i, sizeof(msgPart));
		ri.Printf(PRINT_ALL, "%s\n", msgPart);
	}

	Z_Free(msg);
}

static size_t GLSL_GetShaderHeader(
	GLenum shaderType,
	const GLcharARB *extra,
	int firstLineNumber,
	char *dest,
	size_t size)
{
	float fbufWidthScale, fbufHeightScale;

	dest[0] = '\0';

	Q_strcat(dest, size, "#version 150 core\n");
#ifdef REND2_SP
	Q_strcat(dest, size, "#define REND2_SP\n");
#endif

	Q_strcat(dest, size,
					"#ifndef M_PI\n"
					"#define M_PI 3.14159265358979323846\n"
					"#endif\n");

	Q_strcat(dest, size,
					 va("#ifndef deformGen_t\n"
						"#define deformGen_t\n"
						"#define DEFORM_NONE %i\n"
						"#define DEFORM_WAVE %i\n"
						"#define DEFORM_NORMALS %i\n"
						"#define DEFORM_BULGE %i\n"
						"#define DEFORM_BULGE_UNIFORM %i\n"
						"#define DEFORM_MOVE %i\n"
						"#define DEFORM_PROJECTION_SHADOW %i\n"
						"#define DEFORM_DISINTEGRATION %i\n"
						"#define WF_NONE %i\n"
						"#define WF_SIN %i\n"
						"#define WF_SQUARE %i\n"
						"#define WF_TRIANGLE %i\n"
						"#define WF_SAWTOOTH %i\n"
						"#define WF_INVERSE_SAWTOOTH %i\n"
						"#endif\n",
						DEFORM_NONE,
						DEFORM_WAVE,
						DEFORM_NORMALS,
						DEFORM_BULGE,
						DEFORM_BULGE_UNIFORM,
						DEFORM_MOVE,
						DEFORM_PROJECTION_SHADOW,
						DEFORM_DISINTEGRATION,
						GF_NONE,
						GF_SIN,
						GF_SQUARE,
						GF_TRIANGLE,
						GF_SAWTOOTH,
						GF_INVERSE_SAWTOOTH));

	Q_strcat(dest, size,
					 va("#ifndef tcGen_t\n"
						"#define tcGen_t\n"
						"#define TCGEN_LIGHTMAP %i\n"
						"#define TCGEN_LIGHTMAP1 %i\n"
						"#define TCGEN_LIGHTMAP2 %i\n"
						"#define TCGEN_LIGHTMAP3 %i\n"
						"#define TCGEN_TEXTURE %i\n"
						"#define TCGEN_ENVIRONMENT_MAPPED %i\n"
						"#define TCGEN_FOG %i\n"
						"#define TCGEN_VECTOR %i\n"
						"#endif\n",
						TCGEN_LIGHTMAP,
						TCGEN_LIGHTMAP1,
						TCGEN_LIGHTMAP2,
						TCGEN_LIGHTMAP3,
						TCGEN_TEXTURE,
						TCGEN_ENVIRONMENT_MAPPED,
						TCGEN_FOG,
						TCGEN_VECTOR));

	Q_strcat(dest, size,
					 va("#ifndef colorGen_t\n"
						"#define colorGen_t\n"
						"#define CGEN_LIGHTING_DIFFUSE %i\n"
						"#define CGEN_DISINTEGRATION_1 %i\n"
						"#define CGEN_DISINTEGRATION_2 %i\n"
						"#endif\n",
						CGEN_LIGHTING_DIFFUSE,
						CGEN_DISINTEGRATION_1,
						CGEN_DISINTEGRATION_2));

	Q_strcat(dest, size,
					 va("#ifndef alphaGen_t\n"
						"#define alphaGen_t\n"
						"#define AGEN_LIGHTING_SPECULAR %i\n"
						"#define AGEN_PORTAL %i\n"
						"#endif\n",
						AGEN_LIGHTING_SPECULAR,
						AGEN_PORTAL));

	Q_strcat(dest, size,
					 va("#define ALPHA_TEST_GT0 %d\n"
						"#define ALPHA_TEST_LT128 %d\n"
						"#define ALPHA_TEST_GE128 %d\n"
						"#define ALPHA_TEST_GE192 %d\n",
						ALPHA_TEST_GT0,
						ALPHA_TEST_LT128,
						ALPHA_TEST_GE128,
						ALPHA_TEST_GE192));

	Q_strcat(dest, size,
					va("#define MAX_G2_BONES %i\n#define MAX_MAP_FOGS %i\n",
						MAX_G2_BONES, MAX_MAP_FOGS));

	fbufWidthScale = (float)glConfig.vidWidth;
	fbufHeightScale = (float)glConfig.vidHeight;
	Q_strcat(dest, size,
					 va("#ifndef r_FBufScale\n"
						"#define r_FBufScale vec2(%f, %f)\n"
						"#endif\n",
						fbufWidthScale,
						fbufHeightScale));

	if (r_cubeMapping->integer || r_glassProbes->integer)
	{
		Q_strcat(dest, size, va("#define CUBEMAP_RESOLUTION float(%i)\n", CUBE_MAP_SIZE));
		Q_strcat(dest, size, va("#define ROUGHNESS_MIPS float(%i)\n", CUBE_MAP_ROUGHNESS_MIPS));
	}

	if (r_ssao->integer)
		Q_strcat(dest, size, "#define USE_SSAO\n");

	if (r_deluxeSpecular->value > 0.000001f)
	{
		Q_strcat(dest, size, va("#define r_deluxeSpecular %f\n", r_deluxeSpecular->value));
	}

	if (r_hdr->integer && (r_toneMap->integer || r_forceToneMap->integer))
		Q_strcat(dest, size, "#define USE_TONEMAPPING\n");

	if (extra)
	{
		Q_strcat(dest, size, extra);
	}

	if (shaderType == GL_FRAGMENT_SHADER)
	{
		Q_strcat(dest, size, "\n");
		Q_strcat(dest, size, fallback_hazeProgram.shaders[0].source);
	}

	// OK we added a lot of stuff but if we do something bad in the GLSL
	// shaders then we want the proper line so we have to reset the line
	// counting
	Q_strcat(dest, size, va("\n#line %d\n", firstLineNumber - 1));

	return strlen(dest);
}

static bool GLSL_IsGPUShaderCompiled (GLuint shader)
{
	GLint compiled;
	qglGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
	return (compiled == GL_TRUE);
}

static GLuint GLSL_CompileGPUShader(
	const GLchar *buffer,
	int size,
	GLenum shaderType)
{
	GLuint shader = qglCreateShader(shaderType);
	if ( shader == 0 )
	{
		return 0;
	}

	qglShaderSource(shader, 1, &buffer, &size);
	qglCompileShader(shader);

	if ( !GLSL_IsGPUShaderCompiled(shader) )
	{
		GLSL_PrintShaderSource(shader);
		GLSL_PrintShaderInfoLog(shader, qfalse);

		qglDeleteShader(shader);

		return 0;
	}

	return shader;
}

static const char *GLSL_GetShaderFileSuffix( GLenum shaderType )
{
	static struct
	{
		GLenum shaderType;
		const char *extension;
	} shaderToFileExtensionSuffix[] = {
		{ GL_VERTEX_SHADER, "vp" },
		{ GL_FRAGMENT_SHADER, "fp" },
		{ GL_GEOMETRY_SHADER, "gp" },
	};

	for ( const auto& suffix : shaderToFileExtensionSuffix )
	{
		if ( shaderType == suffix.shaderType )
		{
			return suffix.extension;
		}
	}

	return nullptr;
}

static size_t GLSL_LoadGPUShaderSource(
	const char *name,
	const char *fallback,
	GLenum shaderType,
	char *dest,
	int destSize)
{
	const char *shaderSuffix = GLSL_GetShaderFileSuffix(shaderType);
	assert(shaderSuffix != nullptr);

	char filename[MAX_QPATH];
	Com_sprintf(filename, sizeof(filename), "glsl/%s_%s.glsl", name, shaderSuffix);

	int shaderTextLen = 0;
	GLcharARB *buffer = nullptr;
	if ( r_externalGLSL->integer )
	{
		shaderTextLen = ri.FS_ReadFile(filename, (void **)&buffer);
	}

	const char *shaderText = nullptr;
	if ( !buffer )
	{
		if ( fallback )
		{
			ri.Printf(PRINT_DEVELOPER, "...loading built-in '%s'\n", filename);
			shaderText = fallback;
			shaderTextLen = strlen(shaderText);
			ri.Printf(PRINT_DEVELOPER, "...loading '%s'\n", filename);
		}
		else
		{
			ri.Printf(PRINT_DEVELOPER, "couldn't load '%s'\n", filename);
			return 0;
		}
	}
	else
	{
		ri.Printf(PRINT_DEVELOPER, "...loading '%s'\n", filename);
		shaderText = buffer;
	}

	int result = 0;
	if ( destSize >= (shaderTextLen + 1) )
	{
		Q_strncpyz(dest, shaderText, destSize);
		result = strlen(dest);
	}

	if ( buffer )
	{
		ri.FS_FreeFile(buffer);
	}

	return result;
}

static bool GLSL_LinkProgram(GLuint program)
{
	qglLinkProgram(program);

	GLint linked;
	qglGetProgramiv(program, GL_LINK_STATUS, &linked);
	if ( linked != GL_TRUE )
	{
		GLSL_PrintProgramInfoLog(program, qfalse);
		ri.Printf(PRINT_ALL, "\n");
		return false;
	}
	return true;
}

static void GLSL_ShowProgramUniforms(GLuint program)
{
	int             i, count, size;
	GLenum			type;
	char            uniformName[1000];

	// install the executables in the program object as part of current state.
	qglUseProgram(program);

	// check for GL Errors

	// query the number of active uniforms
	qglGetProgramiv(program, GL_ACTIVE_UNIFORMS, &count);

	// Loop over each of the active uniforms, and set their value
	for(i = 0; i < count; i++)
	{
		qglGetActiveUniform(program, i, sizeof(uniformName), NULL, &size, &type, uniformName);

		ri.Printf(PRINT_DEVELOPER, "active uniform: '%s'\n", uniformName);
	}

	qglUseProgram(0);
}

static void GLSL_BindShaderInterface( shaderProgram_t *program )
{
	static const char *shaderInputNames[] = {
		"attr_Position",  // ATTR_INDEX_POSITION
		"attr_TexCoord0",  // ATTR_INDEX_TEXCOORD0
		"attr_TexCoord1",  // ATTR_INDEX_TEXCOORD1
		"attr_TexCoord2",  // ATTR_INDEX_TEXCOORD2
		"attr_TexCoord3",  // ATTR_INDEX_TEXCOORD3
		"attr_TexCoord4",  // ATTR_INDEX_TEXCOORD4
		"attr_Tangent",  // ATTR_INDEX_TANGENT
		"attr_Normal",  // ATTR_INDEX_NORMAL
		"attr_Color",  // ATTR_INDEX_COLOR
		"attr_LightDirection",  // ATTR_INDEX_LIGHTDIRECTION
		"attr_BoneIndexes",  // ATTR_INDEX_BONE_INDEXES
		"attr_BoneWeights",  // ATTR_INDEX_BONE_WEIGHTS
		"attr_Position2",  // ATTR_INDEX_POSITION2
		"attr_Tangent2",  // ATTR_INDEX_TANGENT2
		"attr_Normal2",  // ATTR_INDEX_NORMAL2
	};

	static const char *xfbVarNames[XFB_VAR_COUNT] = {
		"var_Position",
		"var_Velocity",
	};

	static const char *shaderOutputNames[] = {
		"out_Color",  // Color output
		"out_Glow",  // Glow output
	};

	const uint32_t attribs = program->attribs;
	if (attribs != 0)
	{
		for ( int attribIndex = 0; attribIndex < ATTR_INDEX_MAX; ++attribIndex )
		{
			if ( !(attribs & (1u << attribIndex)) )
			{
				continue;
			}

			qglBindAttribLocation(program->program, attribIndex, shaderInputNames[attribIndex]);
		}
	}

	for ( int outputIndex = 0; outputIndex < ARRAY_LEN(shaderOutputNames); ++outputIndex )
	{
		qglBindFragDataLocation(program->program, outputIndex, shaderOutputNames[outputIndex]);
	}

	const uint32_t xfbVars = program->xfbVariables;
	if (xfbVars != 0)
	{
		size_t activeXfbVarsCount = 0;
		const char *activeXfbVarNames[XFB_VAR_COUNT] = {};

		for (uint32_t xfbVarIndex = 0; xfbVarIndex < XFB_VAR_COUNT; ++xfbVarIndex)
		{
			if ((xfbVars & (1u << xfbVarIndex)) != 0)
			{
				activeXfbVarNames[activeXfbVarsCount++] = xfbVarNames[xfbVarIndex];
			}
		}

		qglTransformFeedbackVaryings(
			program->program, activeXfbVarsCount, activeXfbVarNames, GL_INTERLEAVED_ATTRIBS);
	}
}

GLenum ToGLShaderType( GPUShaderType type )
{
	switch ( type )
	{
		case GPUSHADER_VERTEX:
			return GL_VERTEX_SHADER;

		case GPUSHADER_FRAGMENT:
			return GL_FRAGMENT_SHADER;

		case GPUSHADER_GEOMETRY:
			return GL_GEOMETRY_SHADER;

		default:
			assert(!"Invalid shader type");
			return 0;
	}

	return 0;
}

struct ShaderProgramKey
{
	std::string name;
	uint32_t attribs;
	uint32_t xfbVariables;
	std::vector<std::pair<GLenum, std::string>> stages;

	bool operator<(const ShaderProgramKey& other) const
	{
		return std::tie(name, attribs, xfbVariables, stages) <
			std::tie(other.name, other.attribs, other.xfbVariables, other.stages);
	}
};

// Binding names and fragment outputs in GLSL_BindShaderInterface are fixed.
// Retained entries own GL and CPU state; active entries only track tr records.
// Consume each match once so programs never share mutable uniform state.
static std::multimap<ShaderProgramKey, shaderProgram_t> retainedPrograms;
static std::vector<std::pair<ShaderProgramKey, shaderProgram_t *>> activePrograms;

void GLSL_DeleteGPUShader(shaderProgram_t *program);

static void GLSL_ClearRetainedPrograms()
{
	for (auto& entry : retainedPrograms)
		GLSL_DeleteGPUShader(&entry.second);
	retainedPrograms.clear();
}

class ShaderProgramBuilder
{
	public:
		ShaderProgramBuilder();
		~ShaderProgramBuilder();

		ShaderProgramBuilder(const ShaderProgramBuilder&) = delete;
		ShaderProgramBuilder& operator=(const ShaderProgramBuilder&) = delete;

		void Start(
			const char *name,
			const uint32_t attribs,
			const uint32_t xfbVariables);
		bool AddShader(const GPUShaderDesc& shaderDesc, const char *extra);
		bool Build(shaderProgram_t *program);
		void Reset();
		void PrintCacheStats() const;

	private:
		static const size_t MAX_SHADER_SOURCE_LEN = 16384;

		void ReleaseShaders();

		ShaderProgramKey key;
		GLuint program;
		GLuint shaderNames[GPUSHADER_TYPE_COUNT];
		size_t numShaderNames;
		std::string shaderSource;
		size_t programsLinked = 0;
		size_t cacheHits = 0;
};

ShaderProgramBuilder::ShaderProgramBuilder()
	: program(0)
	, shaderNames()
	, numShaderNames(0)
	, shaderSource(MAX_SHADER_SOURCE_LEN, '\0')
{
}

ShaderProgramBuilder::~ShaderProgramBuilder()
{
	Reset();
}

void ShaderProgramBuilder::Reset()
{
	if ( program )
	{
		ReleaseShaders();
		qglDeleteProgram(program);
		program = 0;
	}
	key.stages.clear();
}

void ShaderProgramBuilder::PrintCacheStats() const
{
	ri.Printf(PRINT_ALL, "GLSL programs: %zu linked, %zu reused, %zu unused released\n",
		programsLinked, cacheHits, retainedPrograms.size());
}

void ShaderProgramBuilder::Start(
	const char *name,
	const uint32_t attribs,
	const uint32_t xfbVariables)
{
	key.name = name;
	key.attribs = attribs;
	key.xfbVariables = xfbVariables;
	key.stages.clear();
}

bool ShaderProgramBuilder::AddShader( const GPUShaderDesc& shaderDesc, const char *extra )
{
	static const int MAX_ATTEMPTS = 3;
	const GLenum apiShader = ToGLShaderType(shaderDesc.type);

	size_t sourceLen = 0;
	size_t headerLen = 0;
	int attempts = 0;
	while ( sourceLen == 0 && attempts < MAX_ATTEMPTS )
	{
		headerLen = GLSL_GetShaderHeader(
			apiShader,
			extra,
			shaderDesc.firstLineNumber,
			&shaderSource[0],
			shaderSource.size());

		sourceLen = GLSL_LoadGPUShaderSource(
				key.name.c_str(),
				shaderDesc.source,
				apiShader,
				&shaderSource[headerLen],
				shaderSource.size() - headerLen);

		if ( sourceLen == 0 )
		{
			shaderSource.resize(shaderSource.size() * 2);
		}

		++attempts;
	}

	if ( sourceLen == 0 )
	{
		ri.Printf(
			PRINT_ALL,
			"ShaderProgramBuilder::AddShader: Failed to allocate enough memory for "
			"shader '%s'\n",
			key.name.c_str());

		return false;
	}

	key.stages.emplace_back(apiShader,
		std::string(shaderSource.c_str(), sourceLen + headerLen));

	return true;
}

bool ShaderProgramBuilder::Build( shaderProgram_t *shaderProgram )
{
	const auto cached = retainedPrograms.find(key);
	if (cached != retainedPrograms.end())
	{
		*shaderProgram = cached->second;
		retainedPrograms.erase(cached);
		activePrograms.emplace_back(std::move(key), shaderProgram);
		++cacheHits;
		return true;
	}

	program = qglCreateProgram();
	if (!program)
	{
		GLSL_ClearRetainedPrograms();
		return false;
	}
	for (const auto& stage : key.stages)
	{
		const GLuint shader = GLSL_CompileGPUShader(
			stage.second.c_str(), stage.second.size(), stage.first);
		if (!shader)
		{
			Reset();
			GLSL_ClearRetainedPrograms();
			ri.Error(ERR_FATAL, "Couldn't compile shader '%s'", key.name.c_str());
			return false;
		}
		qglAttachShader(program, shader);
		shaderNames[numShaderNames++] = shader;
	}

	const size_t nameBufferSize = key.name.size() + 1;
	shaderProgram->name = (char *)Z_Malloc(nameBufferSize, shaderProgramTag);
	Q_strncpyz(shaderProgram->name, key.name.c_str(), nameBufferSize);

	shaderProgram->program = program;
	shaderProgram->attribs = key.attribs;
	shaderProgram->xfbVariables = key.xfbVariables;

	GLSL_BindShaderInterface(shaderProgram);
	if (!GLSL_LinkProgram(shaderProgram->program))
	{
		Reset();
		shaderProgram->program = 0;
		Z_Free(shaderProgram->name);
		shaderProgram->name = nullptr;
		GLSL_ClearRetainedPrograms();
		ri.Error(ERR_FATAL, "shaders failed to link");
		return false;
	}

	ReleaseShaders();
	program = 0;
	++programsLinked;
#ifdef REND2_SP
	activePrograms.emplace_back(std::move(key), shaderProgram);
#endif

	return true;
}

void ShaderProgramBuilder::ReleaseShaders()
{
	for ( size_t i = 0; i < numShaderNames; ++i )
	{
		qglDetachShader(program, shaderNames[i]);
		qglDeleteShader(shaderNames[i]);
	}

	numShaderNames = 0;
}

static bool GLSL_LoadGPUShader(
	ShaderProgramBuilder& builder,
	shaderProgram_t *program,
	const char *name,
	const uint32_t attribs,
	const uint32_t xfbVariables,
	const GLcharARB *extra,
	const GPUProgramDesc& programDesc)
{
	// Header construction uses va(). Preserve caller-supplied defines across it
	// and across all shader stages and source-buffer growth attempts.
	const std::string defines = extra ? extra : "";
	builder.Start(name, attribs, xfbVariables);
	for ( int i = 0; i < programDesc.numShaders; ++i )
	{
		const GPUShaderDesc& shaderDesc = programDesc.shaders[i];
		if ( !builder.AddShader(shaderDesc, defines.c_str()) )
		{
			builder.Reset();
			GLSL_ClearRetainedPrograms();
			return false;
		}
	}
	return builder.Build(program);
}

void GLSL_InitUniforms(shaderProgram_t *program)
{
	// Retained CPU values still match this program's GL uniform state.
	if (program->uniforms)
		return;

	program->uniforms = (GLint *)Z_Malloc(
			UNIFORM_COUNT * sizeof(*program->uniforms), shaderProgramTag);
	program->uniformBufferOffsets = (short *)Z_Malloc(
			UNIFORM_COUNT * sizeof(*program->uniformBufferOffsets), shaderProgramTag);

	GLint *uniforms = program->uniforms;
	int size = 0;
	for (int i = 0; i < UNIFORM_COUNT; i++)
	{
		uniforms[i] = qglGetUniformLocation(program->program, uniformsInfo[i].name);
		if (uniforms[i] == -1)
			continue;

		program->uniformBufferOffsets[i] = size;
		switch(uniformsInfo[i].type)
		{
			case GLSL_INT:
				size += sizeof(GLint) * uniformsInfo[i].size;
				break;
			case GLSL_FLOAT:
				size += sizeof(GLfloat) * uniformsInfo[i].size;
				break;
			case GLSL_VEC2:
				size += sizeof(float) * 2 * uniformsInfo[i].size;
				break;
			case GLSL_VEC3:
				size += sizeof(float) * 3 * uniformsInfo[i].size;
				break;
			case GLSL_VEC4:
				size += sizeof(float) * 4 * uniformsInfo[i].size;
				break;
			case GLSL_MAT4x3:
				size += sizeof(float) * 12 * uniformsInfo[i].size;
				break;
			case GLSL_MAT4x4:
				size += sizeof(float) * 16 * uniformsInfo[i].size;
				break;
			default:
				break;
		}
	}

	program->uniformBuffer = (char *)Z_Malloc(size, TAG_SHADERTEXT, qtrue);

	program->uniformBlocks = 0;
	for ( int i = 0; i < UNIFORM_BLOCK_COUNT; ++i )
	{
		const GLuint blockIndex = qglGetUniformBlockIndex(
			program->program, uniformBlocksInfo[i].name);
		if (blockIndex == GL_INVALID_INDEX)
			continue;
		if (r_verbose->integer)
			ri.Printf(
				PRINT_DEVELOPER,
				"Binding block %d (name '%s', size %zu bytes) to slot %d\n",
				blockIndex,
				uniformBlocksInfo[i].name,
				uniformBlocksInfo[i].size,
				uniformBlocksInfo[i].slot);
		qglUniformBlockBinding(
			program->program, blockIndex, uniformBlocksInfo[i].slot);
		program->uniformBlocks |= (1u << i);
	}

	// The remaining reflection is diagnostic, not required for binding.
	if (!r_verbose->integer)
		return;

	GLint numActiveUniformBlocks = 0;
	qglGetProgramiv(program->program, GL_ACTIVE_UNIFORM_BLOCKS, &numActiveUniformBlocks);
	ri.Printf(PRINT_DEVELOPER, "..num uniform blocks: %d\n", numActiveUniformBlocks);
	for (int i = 0; i < numActiveUniformBlocks; ++i)
	{
		char blockName[512];
		qglGetActiveUniformBlockName(
			program->program,
			i,
			sizeof(blockName),
			nullptr,
			blockName);

		GLint blockSize = 0;
		qglGetActiveUniformBlockiv(
			program->program, i, GL_UNIFORM_BLOCK_DATA_SIZE, &blockSize);

		ri.Printf(PRINT_DEVELOPER, "..block %d: %s (%d bytes)\n", i, blockName, blockSize);
		GLint numMembers = 0;
		qglGetActiveUniformBlockiv(
			program->program, i, GL_UNIFORM_BLOCK_ACTIVE_UNIFORMS, &numMembers);

		if (numMembers > 0)
		{
			GLuint memberIndices[128];
			qglGetActiveUniformBlockiv(
				program->program,
				i,
				GL_UNIFORM_BLOCK_ACTIVE_UNIFORM_INDICES,
				(GLint *)memberIndices);

			GLint memberOffsets[128];
			qglGetActiveUniformsiv(
				program->program,
				numMembers,
				memberIndices,
				GL_UNIFORM_OFFSET,
				memberOffsets);

			GLint memberTypes[128];
			qglGetActiveUniformsiv(
				program->program,
				numMembers,
				memberIndices,
				GL_UNIFORM_TYPE,
				memberTypes);

			for (int j = 0; j < numMembers; ++j)
			{
				char memberName[512];
				qglGetActiveUniformName(
					program->program,
					memberIndices[j],
					sizeof(memberName),
					nullptr,
					memberName);

				ri.Printf(PRINT_DEVELOPER, "....uniform '%s'\n", memberName);
				ri.Printf(PRINT_DEVELOPER, "......offset: %d\n", memberOffsets[j]);
				switch (memberTypes[j])
				{
				case GL_FLOAT:
					ri.Printf(PRINT_DEVELOPER, "......type: float\n");
					break;
				case GL_FLOAT_VEC2:
					ri.Printf(PRINT_DEVELOPER, "......type: vec2\n");
					break;
				case GL_FLOAT_VEC3:
					ri.Printf(PRINT_DEVELOPER, "......type: vec3\n");
					break;
				case GL_FLOAT_VEC4:
					ri.Printf(PRINT_DEVELOPER, "......type: vec4\n");
					break;
				case GL_INT:
					ri.Printf(PRINT_DEVELOPER, "......type: int\n");
					break;
				default:
					ri.Printf(PRINT_DEVELOPER, "......type: other\n");
					break;
				}
			}
		}
	}
}

void GLSL_FinishGPUShader(shaderProgram_t *program)
{
#if defined(_DEBUG)
	if (r_verbose->integer)
		GLSL_ShowProgramUniforms(program->program);
	GL_CheckErrors();
#endif
}

void GLSL_SetUniforms( shaderProgram_t *program, UniformData *uniformData )
{
	if (uniformData == nullptr)
		return;

	UniformData *data = uniformData;
	if (data == nullptr)
		return;

	while ( data->index != UNIFORM_COUNT )
	{
		switch ( uniformsInfo[data->index].type )
		{
			case GLSL_INT:
			{
				assert(data->numElements == 1);
				GLint *value = (GLint *)(data + 1);
				GLSL_SetUniformInt(program, data->index, *value);
				data = reinterpret_cast<UniformData *>(value + data->numElements);
				break;
			}

			case GLSL_FLOAT:
			{
				GLfloat *value = (GLfloat *)(data + 1);
				GLSL_SetUniformFloatN(program, data->index, value, data->numElements);
				data = reinterpret_cast<UniformData *>(value + data->numElements);
				break;
			}

			case GLSL_VEC2:
			{
				GLfloat *value = (GLfloat *)(data + 1);
				GLSL_SetUniformVec2N(program, data->index, value, data->numElements);
				data = reinterpret_cast<UniformData *>(value + data->numElements*2);
				break;
			}

			case GLSL_VEC3:
			{
				GLfloat *value = (GLfloat *)(data + 1);
#ifdef REND2_SP
				if (data->numElements > 1)
				{
					assert(data->numElements <= uniformsInfo[data->index].size);
					if (program->uniforms[data->index] != -1)
					{
						qglUniform3fv(program->uniforms[data->index], data->numElements, value);
						memcpy(program->uniformBuffer + program->uniformBufferOffsets[data->index],
							value, data->numElements * sizeof(vec3_t));
					}
				}
				else
#else
				assert(data->numElements == 1);
#endif
				GLSL_SetUniformVec3(program, data->index, value);
				data = reinterpret_cast<UniformData *>(value + data->numElements*3);
				break;
			}

			case GLSL_VEC4:
			{
				assert(data->numElements == 1);
				GLfloat *value = (GLfloat *)(data + 1);
				GLSL_SetUniformVec4(program, data->index, value);
				data = reinterpret_cast<UniformData *>(value + data->numElements*4);
				break;
			}

			case GLSL_MAT4x3:
			{
				GLfloat *value = (GLfloat *)(data + 1);
				GLSL_SetUniformMatrix4x3(program, data->index, value, data->numElements);
				data = reinterpret_cast<UniformData *>(value + data->numElements*12);
				break;
			}

			case GLSL_MAT4x4:
			{
				GLfloat *value = (GLfloat *)(data + 1);
				GLSL_SetUniformMatrix4x4(program, data->index, value, data->numElements);
				data = reinterpret_cast<UniformData *>(value + data->numElements*16);
				break;
			}

			default:
			{
				assert(!"Invalid uniform data type");
				return;
			}
		}
	}
}

void GLSL_SetUniformInt(shaderProgram_t *program, int uniformNum, GLint value)
{
	GLint *uniforms = program->uniforms;
	GLint *compare = (GLint *)(program->uniformBuffer + program->uniformBufferOffsets[uniformNum]);

	if (uniforms[uniformNum] == -1)
		return;

	if (uniformsInfo[uniformNum].type != GLSL_INT)
	{
		ri.Printf( PRINT_WARNING, "GLSL_SetUniformInt: wrong type for uniform %i in program %s\n", uniformNum, program->name);
		return;
	}

	if (value == *compare)
	{
		return;
	}

	*compare = value;

	qglUniform1i(uniforms[uniformNum], value);
}

void GLSL_SetUniformFloat(shaderProgram_t *program, int uniformNum, GLfloat value)
{
	GLint *uniforms = program->uniforms;
	GLfloat *compare = (GLfloat *)(program->uniformBuffer + program->uniformBufferOffsets[uniformNum]);

	if (uniforms[uniformNum] == -1)
		return;

	if (uniformsInfo[uniformNum].type != GLSL_FLOAT)
	{
		ri.Printf( PRINT_WARNING, "GLSL_SetUniformFloat: wrong type for uniform %i in program %s\n", uniformNum, program->name);
		return;
	}

	if (value == *compare)
	{
		return;
	}

	*compare = value;

	qglUniform1f(uniforms[uniformNum], value);
}

void GLSL_SetUniformVec2(shaderProgram_t *program, int uniformNum, const vec2_t v)
{
	GLint *uniforms = program->uniforms;
	float *compare = (float *)(program->uniformBuffer + program->uniformBufferOffsets[uniformNum]);

	if (uniforms[uniformNum] == -1)
		return;

	if (uniformsInfo[uniformNum].type != GLSL_VEC2)
	{
		ri.Printf( PRINT_WARNING, "GLSL_SetUniformVec2: wrong type for uniform %i in program %s\n", uniformNum, program->name);
		return;
	}

	if (v[0] == compare[0] && v[1] == compare[1])
	{
		return;
	}

	compare[0] = v[0];
	compare[1] = v[1];

	qglUniform2f(uniforms[uniformNum], v[0], v[1]);
}

void GLSL_SetUniformVec2N(shaderProgram_t *program, int uniformNum, const float *v, int numVec2s)
{
	GLint *uniforms = program->uniforms;
	float *compare = (float *)(program->uniformBuffer + program->uniformBufferOffsets[uniformNum]);

	if (uniforms[uniformNum] == -1)
		return;

	if (uniformsInfo[uniformNum].type != GLSL_VEC2)
	{
		ri.Printf(PRINT_WARNING, "GLSL_SetUniformVec2: wrong type for uniform %i in program %s\n", uniformNum, program->name);
		return;
	}

	if (uniformsInfo[uniformNum].size < numVec2s)
	{
		ri.Printf(PRINT_WARNING, "GLSL_SetUniformVec2N: uniform %i only has %d elements! Tried to set %d\n",
			uniformNum,
			uniformsInfo[uniformNum].size,
			numVec2s);
		return;
	}

	if (memcmp(compare, v, sizeof(vec2_t) * numVec2s) == 0)
	{
		return;
	}

	memcpy(compare, v, sizeof(vec2_t) * numVec2s);

	qglUniform2fv(uniforms[uniformNum], numVec2s, v);
}

void GLSL_SetUniformVec3(shaderProgram_t *program, int uniformNum, const vec3_t v)
{
	GLint *uniforms = program->uniforms;
	float *compare = (float *)(program->uniformBuffer + program->uniformBufferOffsets[uniformNum]);

	if (uniforms[uniformNum] == -1)
		return;

	if (uniformsInfo[uniformNum].type != GLSL_VEC3)
	{
		ri.Printf( PRINT_WARNING, "GLSL_SetUniformVec3: wrong type for uniform %i in program %s\n", uniformNum, program->name);
		return;
	}

	if (VectorCompare(v, compare))
	{
		return;
	}

	VectorCopy(v, compare);

	qglUniform3f(uniforms[uniformNum], v[0], v[1], v[2]);
}

void GLSL_SetUniformVec4(shaderProgram_t *program, int uniformNum, const vec4_t v)
{
	GLint *uniforms = program->uniforms;
	float *compare = (float *)(program->uniformBuffer + program->uniformBufferOffsets[uniformNum]);

	if (uniforms[uniformNum] == -1)
		return;

	if (uniformsInfo[uniformNum].type != GLSL_VEC4)
	{
		ri.Printf( PRINT_WARNING, "GLSL_SetUniformVec4: wrong type for uniform %i in program %s\n", uniformNum, program->name);
		return;
	}

	if (VectorCompare4(v, compare))
	{
		return;
	}

	VectorCopy4(v, compare);

	qglUniform4f(uniforms[uniformNum], v[0], v[1], v[2], v[3]);
}

void GLSL_SetUniformFloatN(shaderProgram_t *program, int uniformNum, const float *v, int numFloats)
{
	GLint *uniforms = program->uniforms;
	float *compare = (float *)(program->uniformBuffer + program->uniformBufferOffsets[uniformNum]);

	if (uniforms[uniformNum] == -1)
		return;

	if (uniformsInfo[uniformNum].type != GLSL_FLOAT)
	{
		ri.Printf( PRINT_WARNING, "GLSL_SetUniformFloatN: wrong type for uniform %i in program %s\n", uniformNum, program->name);
		return;
	}

	if (uniformsInfo[uniformNum].size < numFloats)
	{
		ri.Printf( PRINT_WARNING, "GLSL_SetUniformFloatN: uniform %i only has %d elements! Tried to set %d\n",
					uniformNum,
					uniformsInfo[uniformNum].size,
					numFloats );
		return;
	}

	if ( memcmp( compare, v, sizeof( float ) * numFloats ) == 0 )
	{
		return;
	}

	memcpy( compare, v, sizeof( float ) * numFloats );

	qglUniform1fv(uniforms[uniformNum], numFloats, v);
}

void GLSL_SetUniformMatrix4x3(shaderProgram_t *program, int uniformNum, const float *matrix, int numElements)
{
	GLint *uniforms = program->uniforms;
	float *compare;

	if (uniforms[uniformNum] == -1)
		return;

	if (uniformsInfo[uniformNum].type != GLSL_MAT4x3)
	{
		ri.Printf( PRINT_WARNING, "GLSL_SetUniformMatrix4x3: wrong type for uniform %i in program %s\n", uniformNum, program->name);
		return;
	}

	if (uniformsInfo[uniformNum].size < numElements)
		return;

	compare = (float *)(program->uniformBuffer + program->uniformBufferOffsets[uniformNum]);
	if (memcmp (matrix, compare, sizeof (float) * 12 * numElements) == 0)
	{
		return;
	}

	Com_Memcpy (compare, matrix, sizeof (float) * 12 * numElements);

	qglUniformMatrix4x3fv(uniforms[uniformNum], numElements, GL_FALSE, matrix);
}

void GLSL_SetUniformMatrix4x4(shaderProgram_t *program, int uniformNum, const float *matrix, int numElements)
{
	GLint *uniforms = program->uniforms;
	float *compare;

	if (uniforms[uniformNum] == -1)
		return;

	if (uniformsInfo[uniformNum].type != GLSL_MAT4x4)
	{
		ri.Printf( PRINT_WARNING, "GLSL_SetUniformMatrix4x4: wrong type for uniform %i in program %s\n", uniformNum, program->name);
		return;
	}

	if (uniformsInfo[uniformNum].size < numElements)
		return;

	compare = (float *)(program->uniformBuffer + program->uniformBufferOffsets[uniformNum]);
	if (memcmp (matrix, compare, sizeof (float) * 16 * numElements) == 0)
	{
		return;
	}

	Com_Memcpy (compare, matrix, sizeof (float) * 16 * numElements);

	qglUniformMatrix4fv(uniforms[uniformNum], numElements, GL_FALSE, matrix);
}

void GLSL_DeleteGPUShader(shaderProgram_t *program)
{
	if(program->program)
	{
		qglDeleteProgram(program->program);

		Z_Free (program->name);
		Z_Free (program->uniformBuffer);
		Z_Free (program->uniformBufferOffsets);
		Z_Free (program->uniforms);

		Com_Memset(program, 0, sizeof(*program));
	}
}

static bool GLSL_IsValidPermutationForGeneric (int shaderCaps)
{
#ifdef REND2_SP
	if ( (shaderCaps & GENERICDEF_USE_VERTEX_ANIMATION) &&
			(shaderCaps & GENERICDEF_USE_SKELETAL_ANIMATION) )
		return false;
#endif // REND2_SP
	return true;
}

static bool GLSL_IsValidPermutationForFog (int shaderCaps)
{
#ifdef REND2_SP
	if ( (shaderCaps & FOGDEF_USE_VERTEX_ANIMATION) &&
			(shaderCaps & FOGDEF_USE_SKELETAL_ANIMATION) )
		return false;
#endif // REND2_SP
	return true;
}

static bool GLSL_IsValidPermutationForLight (int lightType, int shaderCaps)
{
	if ((shaderCaps & LIGHTDEF_USE_PARALLAXMAP) && !r_parallaxMapping->integer)
		return false;

	if (!lightType && (shaderCaps & LIGHTDEF_USE_PARALLAXMAP))
		return false;

#ifdef REND2_SP
	if ( (shaderCaps & LIGHTDEF_USE_SKELETAL_ANIMATION) &&
			(shaderCaps & LIGHTDEF_USE_VERTEX_ANIMATION) )
		return false;
#endif // REND2_SP
	return true;
}

Block *FindBlock( const char *name, Block *blocks, size_t numBlocks )
{
	for ( size_t i = 0; i < numBlocks; ++i )
	{
		Block *block = blocks + i;
		if ( Q_stricmpn(block->blockHeaderTitle, name, block->blockHeaderTitleLength) == 0 )
		{
			return block;
		}
	}

	return nullptr;
}

void GLSL_InitSplashScreenShader()
{
	const char *vs =
		"#version 150 core\n"
		"out vec2 var_TexCoords;\n"
		"void main() {\n"
		"  vec2 position = vec2(2.0 * float(gl_VertexID & 2) - 1.0, 4.0 * float(gl_VertexID & 1) - 1.0);\n"
		"  gl_Position = vec4(position, 0.0, 1.0);\n"
		"  var_TexCoords = vec2(position.x * 0.5 + 0.5, 2.0 - (position.y * 0.5 + 0.5));\n"
		"}";

	const char *fs =
		"#version 150 core\n"
		"uniform sampler2D u_SplashTexture;\n"
		"in vec2 var_TexCoords;\n"
		"out vec4 out_Color;\n"
		"void main() {\n"
		"  out_Color = texture(u_SplashTexture, var_TexCoords);\n"
		"}";

	GLuint vshader = qglCreateShader(GL_VERTEX_SHADER);
	qglShaderSource(vshader, 1, &vs, NULL);
	qglCompileShader(vshader);

	GLuint fshader = qglCreateShader(GL_FRAGMENT_SHADER);
	qglShaderSource(fshader, 1, &fs, NULL);
	qglCompileShader(fshader);

	GLuint program = qglCreateProgram();
	qglAttachShader(program, vshader);
	qglAttachShader(program, fshader);
	qglLinkProgram(program);
	qglDetachShader(program, vshader);
	qglDetachShader(program, fshader);
	qglDeleteShader(vshader);
	qglDeleteShader(fshader);

	size_t splashLen = strlen("splash");
	tr.splashScreenShader.program = program;
	tr.splashScreenShader.name = (char *)Z_Malloc(splashLen + 1, shaderProgramTag);
	Q_strncpyz(tr.splashScreenShader.name, "splash", splashLen + 1);
}

static const GPUProgramDesc *LoadProgramSource(
	const char *programName, Allocator& allocator, const GPUProgramDesc& fallback )
{
	const GPUProgramDesc *result = &fallback;

	if ( r_externalGLSL->integer )
	{
		char *buffer = nullptr;
		char programPath[MAX_QPATH];
		Com_sprintf(programPath, sizeof(programPath), "glsl/%s.glsl", programName);

		long size = ri.FS_ReadFile(programPath, (void **)&buffer);
		if (size > 0 && buffer)
		{
			GPUProgramDesc *externalProgramDesc = ojkAlloc<GPUProgramDesc>(allocator);
			*externalProgramDesc = ParseProgramSource(allocator, buffer);
			result = externalProgramDesc;
		}
		if (buffer)
			ri.FS_FreeFile(buffer);
	}

	return result;
}

static int GLSL_LoadGPUProgramGeneric(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc )
{
	int numPrograms = 0;
	Allocator allocator(scratchAlloc.Base(), scratchAlloc.GetSize());

	char extradefines[1200];
	const GPUProgramDesc *programDesc =
		LoadProgramSource("generic", allocator, fallback_genericProgram);
	for ( int i = 0; i < GENERICDEF_COUNT; i++ )
	{
		if (!GLSL_IsValidPermutationForGeneric(i))
		{
			continue;
		}

		uint32_t attribs = ATTR_POSITION | ATTR_TEXCOORD0 | ATTR_NORMAL | ATTR_COLOR;
		extradefines[0] = '\0';

		if (i & GENERICDEF_USE_DEFORM_VERTEXES)
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_DEFORM_VERTEXES\n");

		if (i & GENERICDEF_USE_TCGEN_AND_TCMOD)
		{
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_TCGEN\n");
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_TCMOD\n");
		}
#ifdef REND2_SP
		if (i & GENERICDEF_USE_VERTEX_ANIMATION)
		{
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_VERTEX_ANIMATION\n");
			attribs |= ATTR_POSITION2 | ATTR_NORMAL2;
		}
#endif // REND2_SP
		if (i & GENERICDEF_USE_SKELETAL_ANIMATION)
		{
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_SKELETAL_ANIMATION\n");
			attribs |= ATTR_BONE_INDEXES | ATTR_BONE_WEIGHTS;
		}

		if (i & GENERICDEF_USE_FOG)
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_FOG\n");

		if (i & GENERICDEF_USE_RGBAGEN)
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_RGBAGEN\n");

		if (i & GENERICDEF_USE_GLOW_BUFFER)
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_GLOW_BUFFER\n");

		if (i & GENERICDEF_USE_ALPHA_TEST)
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_ALPHA_TEST\n");

		if (!GLSL_LoadGPUShader(builder, &tr.genericShader[i], "generic", attribs, NO_XFB_VARS,
				extradefines, *programDesc))
		{
			ri.Error(ERR_FATAL, "Could not load generic shader!");
		}

		GLSL_InitUniforms(&tr.genericShader[i]);

		qglUseProgram(tr.genericShader[i].program);
		GLSL_SetUniformInt(&tr.genericShader[i], UNIFORM_DIFFUSEMAP, TB_DIFFUSEMAP);
		GLSL_SetUniformInt(&tr.genericShader[i], UNIFORM_SCREENDEPTHMAP, TB_SHADOWMAP);
		GLSL_SetUniformInt(&tr.genericShader[i], UNIFORM_LIGHTMAP,   TB_LIGHTMAP);
		qglUseProgram(0);

		GLSL_FinishGPUShader(&tr.genericShader[i]);

		++numPrograms;
	}

	return numPrograms;
}

static int GLSL_LoadGPUProgramGlass(ShaderProgramBuilder &builder, Allocator &scratchAlloc)
{
	Allocator allocator(scratchAlloc.Base(), scratchAlloc.GetSize());
	const GPUProgramDesc *source = LoadProgramSource("glass", allocator, fallback_genericProgram);
	int count = 0;
	for (int i = 0; i < 12; ++i)
	{
		if ((i & 2) && !r_cubeMapping->integer && !r_glassProbes->integer) continue;
		char defines[256] = "#define USE_GLASS\n#define USE_TCGEN\n#define USE_TCMOD\n#define USE_RGBAGEN\n";
		uint32_t attribs = ATTR_POSITION | ATTR_NORMAL | ATTR_TEXCOORD0 | ATTR_COLOR;
#ifdef REND2_SP
		if (i / 4 == 1)
		{
			Q_strcat(defines, sizeof(defines), "#define USE_VERTEX_ANIMATION\n");
			attribs |= ATTR_POSITION2 | ATTR_NORMAL2;
		}
#else
		if (i / 4 == 1) continue;
#endif
		if (i / 4 == 2)
		{
			Q_strcat(defines, sizeof(defines), "#define USE_SKELETAL_ANIMATION\n");
			attribs |= ATTR_BONE_INDEXES | ATTR_BONE_WEIGHTS;
		}
		if (i & 1) Q_strcat(defines, sizeof(defines), "#define USE_FOG\n");
		if (i & 2) Q_strcat(defines, sizeof(defines), "#define USE_CUBEMAP\n");
		shaderProgram_t *program = &tr.glassShader[i];
		if (!GLSL_LoadGPUShader(builder, program, "glass", attribs,
			NO_XFB_VARS, defines, *source))
			ri.Error(ERR_FATAL, "Could not load glass shader!");
		GLSL_InitUniforms(program);
		qglUseProgram(program->program);
		GLSL_SetUniformInt(program, UNIFORM_DIFFUSEMAP, TB_DIFFUSEMAP);
		GLSL_SetUniformInt(program, UNIFORM_SCREENDEPTHMAP, TB_SHADOWMAP);
		GLSL_SetUniformInt(program, UNIFORM_CUBEMAP, TB_CUBEMAP);
		qglUseProgram(0);
		GLSL_FinishGPUShader(program);
		++count;
	}
	return count;
}

static int GLSL_LoadGPUProgramFogPass(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc )
{
	int numPrograms = 0;
	Allocator allocator(scratchAlloc.Base(), scratchAlloc.GetSize());

	char extradefines[1200];
	const GPUProgramDesc *programDesc =
		LoadProgramSource("fogpass", allocator, fallback_fogpassProgram);
	for (int i = 0; i < FOGDEF_COUNT; i++)
	{
		if (!GLSL_IsValidPermutationForFog(i))
		{
			continue;
		}

		uint32_t attribs =
			(ATTR_POSITION | ATTR_NORMAL | ATTR_TEXCOORD0);
		extradefines[0] = '\0';

		if (i & FOGDEF_USE_DEFORM_VERTEXES)
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_DEFORM_VERTEXES\n");
#ifdef REND2_SP
		if (i & FOGDEF_USE_VERTEX_ANIMATION)
		{
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_VERTEX_ANIMATION\n");
			attribs |= ATTR_POSITION2 | ATTR_NORMAL2;
		}
#endif // REND2_SP
		if (i & FOGDEF_USE_SKELETAL_ANIMATION)
		{
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_SKELETAL_ANIMATION\n");
			attribs |= ATTR_BONE_INDEXES | ATTR_BONE_WEIGHTS;
		}

		if (i & FOGDEF_USE_FALLBACK_GLOBAL_FOG)
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_FALLBACK_GLOBAL_FOG\n");

		if (i & FOGDEF_USE_ALPHA_TEST)
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_ALPHA_TEST\n");

		if (!GLSL_LoadGPUShader(builder, &tr.fogShader[i], "fogpass", attribs, NO_XFB_VARS,
				extradefines, *programDesc))
		{
			ri.Error(ERR_FATAL, "Could not load fogpass shader!");
		}

		GLSL_InitUniforms(&tr.fogShader[i]);

		qglUseProgram(tr.fogShader[i].program);
		if (i & FOGDEF_USE_ALPHA_TEST)
			GLSL_SetUniformInt(&tr.fogShader[i], UNIFORM_DIFFUSEMAP, 0);
		qglUseProgram(0);

		GLSL_FinishGPUShader(&tr.fogShader[i]);

		++numPrograms;
	}

	return numPrograms;
}

static int GLSL_LoadGPUProgramRefraction(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc)
{
	int numPrograms = 0;
	Allocator allocator(scratchAlloc.Base(), scratchAlloc.GetSize());

	char extradefines[1200];
	const GPUProgramDesc *programDesc =
		LoadProgramSource("refraction", allocator, fallback_refractionProgram);
	for (int i = 0; i < REFRACTIONDEF_COUNT; i++)
	{
		uint32_t attribs = ATTR_POSITION | ATTR_TEXCOORD0 | ATTR_NORMAL | ATTR_COLOR;
		extradefines[0] = '\0';

		if (i & REFRACTIONDEF_USE_DEFORM_VERTEXES)
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_DEFORM_VERTEXES\n");

		if (i & REFRACTIONDEF_USE_TCGEN_AND_TCMOD)
		{
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_TCGEN\n");
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_TCMOD\n");
		}
#ifdef REND2_SP
		if (i & REFRACTIONDEF_USE_VERTEX_ANIMATION)
		{
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_VERTEX_ANIMATION\n");
			attribs |= ATTR_POSITION2 | ATTR_NORMAL2;
		}
#endif // REND2_SP
		if (i & REFRACTIONDEF_USE_SKELETAL_ANIMATION)
		{
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_SKELETAL_ANIMATION\n");
			attribs |= ATTR_BONE_INDEXES | ATTR_BONE_WEIGHTS;
		}

		if (i & REFRACTIONDEF_USE_RGBAGEN)
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_RGBAGEN\n");

		if (i & REFRACTIONDEF_USE_ALPHA_TEST)
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_ALPHA_TEST\n");

		if (i & REFRACTIONDEF_USE_SRGB_TRANSFORM)
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_LINEAR_LIGHT\n");

		if (!GLSL_LoadGPUShader(builder, &tr.refractionShader[i], "refraction", attribs, NO_XFB_VARS,
			extradefines, *programDesc))
		{
			ri.Error(ERR_FATAL, "Could not load refraction shader!");
		}

		GLSL_InitUniforms(&tr.refractionShader[i]);

		qglUseProgram(tr.refractionShader[i].program);
		GLSL_SetUniformInt(&tr.refractionShader[i], UNIFORM_TEXTUREMAP, TB_COLORMAP);
		GLSL_SetUniformInt(&tr.refractionShader[i], UNIFORM_LEVELSMAP, TB_LEVELSMAP);
		GLSL_SetUniformInt(&tr.refractionShader[i], UNIFORM_SCREENDEPTHMAP, TB_SHADOWMAP);
		qglUseProgram(0);

		GLSL_FinishGPUShader(&tr.refractionShader[i]);

		++numPrograms;
	}

	return numPrograms;
}

static int GLSL_LoadGPUProgramLightAll(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc )
{
	int numPrograms = 0;
	Allocator allocator(scratchAlloc.Base(), scratchAlloc.GetSize());

	char extradefines[1200];
	const GPUProgramDesc *programDesc =
		LoadProgramSource("lightall", allocator, fallback_lightallProgram);
	const bool useFastLight =
		(!r_normalMapping->integer && !r_specularMapping->integer);
	for ( int i = 0; i < LIGHTDEF_COUNT; i++ )
	{
		int lightType = i & LIGHTDEF_LIGHTTYPE_MASK;

		// skip impossible combos
		if (!GLSL_IsValidPermutationForLight (lightType, i))
			continue;

		uint32_t attribs = ATTR_POSITION | ATTR_TEXCOORD0 | ATTR_COLOR | ATTR_NORMAL;

		extradefines[0] = '\0';

		if (r_hdr->integer && !glRefConfig.floatLightmap)
			Q_strcat(extradefines, sizeof(extradefines), "#define RGBM_LIGHTMAP\n");

		if (lightType)
		{
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_LIGHT\n");

			if (useFastLight)
				Q_strcat(extradefines, sizeof(extradefines), "#define USE_FAST_LIGHT\n");

			if (r_dlightMode->integer >= 2)
				Q_strcat(extradefines, sizeof(extradefines), "#define USE_DSHADOWS\n");

			switch (lightType)
			{
				case LIGHTDEF_USE_LIGHTMAP:
				{
					Q_strcat(extradefines, sizeof(extradefines), "#define USE_LIGHTMAP\n");

					if (r_deluxeMapping->integer && !useFastLight)
						Q_strcat(extradefines, sizeof(extradefines), "#define USE_DELUXEMAP\n");

					attribs |= ATTR_TEXCOORD1 | ATTR_LIGHTDIRECTION;
					break;
				}

				case LIGHTDEF_USE_LIGHT_VECTOR:
				{
					Q_strcat(extradefines, sizeof(extradefines), "#define USE_LIGHT_VECTOR\n");
					break;
				}

				case LIGHTDEF_USE_LIGHT_VERTEX:
				{
					Q_strcat(extradefines, sizeof(extradefines), "#define USE_LIGHT_VERTEX\n");
					attribs |= ATTR_LIGHTDIRECTION;
					break;
				}

				default:
					break;
			}

			if (r_normalMapping->integer)
			{
				Q_strcat(extradefines, sizeof(extradefines), "#define USE_NORMALMAP\n");

				if ((i & LIGHTDEF_USE_PARALLAXMAP) && r_parallaxMapping->integer)
					Q_strcat(extradefines, sizeof(extradefines), "#define USE_PARALLAXMAP\n");

				attribs |= ATTR_TANGENT;
			}

			if (r_specularMapping->integer)
			{
				Q_strcat(extradefines, sizeof(extradefines), "#define USE_SPECULARMAP\n");
				if (i & LIGHTDEF_USE_SPEC_GLOSS)
					Q_strcat(extradefines, sizeof(extradefines), "#define USE_SPECGLOSS\n");
			}

			if (r_cubeMapping->integer)
				Q_strcat(extradefines, sizeof(extradefines), "#define USE_CUBEMAP\n");
		}

		if (r_sunlightMode->integer)
		{
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_SHADOWMAP\n");

			if (r_sunlightMode->integer == 1)
				Q_strcat(extradefines, sizeof(extradefines), "#define SHADOWMAP_MODULATE\n");
			else if (r_sunlightMode->integer == 2)
				Q_strcat(extradefines, sizeof(extradefines), "#define USE_PRIMARY_LIGHT\n");

			if (r_shadowFilter->integer >= 1)
				Q_strcat(extradefines, sizeof(extradefines), "#define USE_SHADOW_FILTER\n");

			if (r_shadowFilter->integer >= 2)
				Q_strcat(extradefines, sizeof(extradefines), "#define USE_SHADOW_FILTER2\n");

			Q_strcat(
				extradefines, sizeof(extradefines),
				va("#define r_shadowMapSize %d\n", r_shadowMapSize->integer));
			Q_strcat(
				extradefines, sizeof(extradefines),
				va("#define r_shadowCascadeZFar %f\n", r_shadowCascadeZFar->value));
		}

		if (i & LIGHTDEF_USE_TCGEN_AND_TCMOD)
		{
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_TCGEN\n");
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_TCMOD\n");
		}

		if (i & LIGHTDEF_USE_CLOTH_BRDF)
		{
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_CLOTH_BRDF\n");
		}
#ifdef REND2_SP
		if (i & LIGHTDEF_USE_VERTEX_ANIMATION)
		{
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_VERTEX_ANIMATION\n");
			attribs |= ATTR_POSITION2 | ATTR_NORMAL2;

			if (r_normalMapping->integer)
				attribs |= ATTR_TANGENT2;
		}
		else
#endif // REND2_SP
		if (i & LIGHTDEF_USE_SKELETAL_ANIMATION)
		{
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_SKELETAL_ANIMATION\n");
			attribs |= ATTR_BONE_INDEXES | ATTR_BONE_WEIGHTS;
		}

		if (i & LIGHTDEF_USE_ALPHA_TEST)
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_ALPHA_TEST\n");

		if (i & LIGHTDEF_USE_GLOW_BUFFER)
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_GLOW_BUFFER\n");

		if (!GLSL_LoadGPUShader(builder, &tr.lightallShader[i], "lightall", attribs, NO_XFB_VARS,
				extradefines, *programDesc))
		{
			ri.Error(ERR_FATAL, "Could not load lightall shader!");
		}

		GLSL_InitUniforms(&tr.lightallShader[i]);

		qglUseProgram(tr.lightallShader[i].program);
		GLSL_SetUniformInt(&tr.lightallShader[i], UNIFORM_DIFFUSEMAP,  TB_DIFFUSEMAP);
		GLSL_SetUniformInt(&tr.lightallShader[i], UNIFORM_LIGHTMAP,    TB_LIGHTMAP);
		GLSL_SetUniformInt(&tr.lightallShader[i], UNIFORM_NORMALMAP,   TB_NORMALMAP);
		GLSL_SetUniformInt(&tr.lightallShader[i], UNIFORM_DELUXEMAP,   TB_DELUXEMAP);
		GLSL_SetUniformInt(&tr.lightallShader[i], UNIFORM_SPECULARMAP, TB_SPECULARMAP);
		GLSL_SetUniformInt(&tr.lightallShader[i], UNIFORM_SHADOWMAP,   TB_SHADOWMAP);
		GLSL_SetUniformInt(&tr.lightallShader[i], UNIFORM_CUBEMAP,     TB_CUBEMAP);
		GLSL_SetUniformInt(&tr.lightallShader[i], UNIFORM_ENVBRDFMAP,  TB_ENVBRDFMAP);
		GLSL_SetUniformInt(&tr.lightallShader[i], UNIFORM_SHADOWMAP2,  TB_SHADOWMAPARRAY);
		GLSL_SetUniformInt(&tr.lightallShader[i], UNIFORM_SSAOMAP,     TB_SSAOMAP);
		GLSL_SetUniformInt(&tr.lightallShader[i], UNIFORM_SCREENDEPTHMAP, TB_SKINDEPTHMAP);
		GLSL_SetUniformInt(&tr.lightallShader[i], UNIFORM_TORCHSHADOWMAP, TB_TORCHSHADOWMAP);
		qglUseProgram(0);

		GLSL_FinishGPUShader(&tr.lightallShader[i]);

		++numPrograms;
	}

	return numPrograms;
}

static int GLSL_LoadGPUProgramBasicWithDefinitions(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc,
	shaderProgram_t *shaderProgram,
	const char *programName,
	const GPUProgramDesc& programFallback,
	const char *extraDefines,
	const uint32_t attribs = ATTR_POSITION | ATTR_TEXCOORD0,
	const uint32_t xfbVariables = NO_XFB_VARS)
{
	Allocator allocator(scratchAlloc.Base(), scratchAlloc.GetSize());

	const GPUProgramDesc *programDesc =
		LoadProgramSource(programName, allocator, programFallback);
	if (!GLSL_LoadGPUShader(
			builder,
			shaderProgram,
			programName,
			attribs,
			xfbVariables,
			extraDefines,
			*programDesc))
	{
		ri.Error(ERR_FATAL, "Could not load %s shader!", programName);
	}

	return 1;
}

static int GLSL_LoadGPUProgramBasic(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc,
	shaderProgram_t *shaderProgram,
	const char *programName,
	const GPUProgramDesc& programFallback,
	const uint32_t attribs = ATTR_POSITION | ATTR_TEXCOORD0,
	const uint32_t xfbVariables = NO_XFB_VARS)
{
	return GLSL_LoadGPUProgramBasicWithDefinitions(
		builder,
		scratchAlloc,
		shaderProgram,
		programName,
		programFallback,
		nullptr,
		attribs,
		xfbVariables);
}

static int GLSL_LoadGPUProgramTextureColor(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc )
{
	GLSL_LoadGPUProgramBasic(
		builder,
		scratchAlloc,
		&tr.textureColorShader,
		"texturecolor",
		fallback_texturecolorProgram);

	GLSL_InitUniforms(&tr.textureColorShader);

	qglUseProgram(tr.textureColorShader.program);
	GLSL_SetUniformInt(&tr.textureColorShader, UNIFORM_TEXTUREMAP, TB_DIFFUSEMAP);
	qglUseProgram(0);

	GLSL_FinishGPUShader(&tr.textureColorShader);

	return 1;
}

static int GLSL_LoadGPUProgramPShadow(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc )
{
	const char *extradefines = "#define USE_PCF\n#define USE_DISCARD\n";

	GLSL_LoadGPUProgramBasicWithDefinitions(
		builder,
		scratchAlloc,
		&tr.pshadowShader,
		"pshadow",
		fallback_pshadowProgram,
		extradefines,
		ATTR_POSITION | ATTR_NORMAL);

	GLSL_InitUniforms(&tr.pshadowShader);

	qglUseProgram(tr.pshadowShader.program);
	GLSL_SetUniformInt(&tr.pshadowShader, UNIFORM_SHADOWMAP, TB_DIFFUSEMAP);
	qglUseProgram(0);

	GLSL_FinishGPUShader(&tr.pshadowShader);

	return 1;
}

static int GLSL_LoadGPUProgramVShadow(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc)
{
	Allocator allocator(scratchAlloc.Base(), scratchAlloc.GetSize());

	char extradefines[1200];
	const GPUProgramDesc *programDesc =
		LoadProgramSource("shadowvolume", allocator, fallback_shadowvolumeProgram);
	extradefines[0] = '\0';
#ifdef REND2_SP
	const uint32_t attribs = ATTR_POSITION;
#else
	const uint32_t attribs = ATTR_POSITION | ATTR_BONE_INDEXES | ATTR_BONE_WEIGHTS;
	Q_strcat(extradefines, sizeof(extradefines), "#define USE_SKELETAL_ANIMATION\n");
#endif

	if (!GLSL_LoadGPUShader(builder, &tr.volumeShadowShader, "shadowvolume", attribs, NO_XFB_VARS,
		extradefines, *programDesc))
	{
		ri.Error(ERR_FATAL, "Could not load shadowvolume shader!");
	}

	GLSL_InitUniforms(&tr.volumeShadowShader);
	GLSL_FinishGPUShader(&tr.volumeShadowShader);

	return 1;
}

static int GLSL_LoadGPUProgramDownscale4x(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc )
{
	GLSL_LoadGPUProgramBasic(
		builder,
		scratchAlloc,
		&tr.down4xShader,
		"down4x",
		fallback_down4xProgram);

	GLSL_InitUniforms(&tr.down4xShader);

	qglUseProgram(tr.down4xShader.program);
	GLSL_SetUniformInt(&tr.down4xShader, UNIFORM_TEXTUREMAP, TB_DIFFUSEMAP);
	qglUseProgram(0);

	GLSL_FinishGPUShader(&tr.down4xShader);

	return 1;
}

static int GLSL_LoadGPUProgramBokeh(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc )
{
	GLSL_LoadGPUProgramBasic(
		builder,
		scratchAlloc,
		&tr.bokehShader,
		"bokeh",
		fallback_bokehProgram);

	GLSL_InitUniforms(&tr.bokehShader);

	qglUseProgram(tr.bokehShader.program);
	GLSL_SetUniformInt(&tr.bokehShader, UNIFORM_TEXTUREMAP, TB_DIFFUSEMAP);
	qglUseProgram(0);

	GLSL_FinishGPUShader(&tr.bokehShader);

	return 1;
}

static int GLSL_LoadGPUProgramTonemap(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc )
{
	Allocator allocator(scratchAlloc.Base(), scratchAlloc.GetSize());
	char extradefines[1200];
	const GPUProgramDesc *programDesc =
		LoadProgramSource("tonemap", allocator, fallback_tonemapProgram);
	const uint32_t attribs = ATTR_POSITION | ATTR_TEXCOORD0;

	extradefines[0] = '\0';
	if (!GLSL_LoadGPUShader(builder, &tr.tonemapShader[0], "tonemap", attribs, NO_XFB_VARS,
		extradefines, *programDesc))
	{
		ri.Error(ERR_FATAL, "Could not load tonemap shader!");
	}

	Q_strcat(extradefines, sizeof(extradefines), "#define USE_LINEAR_LIGHT\n");
	if (!GLSL_LoadGPUShader(builder, &tr.tonemapShader[1], "tonemap", attribs, NO_XFB_VARS,
		extradefines, *programDesc))
	{
		ri.Error(ERR_FATAL, "Could not load tonemap shader!");
	}

	for (int i = 0; i < 2; i++)
	{
		GLSL_InitUniforms(&tr.tonemapShader[i]);
		qglUseProgram(tr.tonemapShader[i].program);
		GLSL_SetUniformInt(&tr.tonemapShader[i], UNIFORM_TEXTUREMAP, TB_COLORMAP);
		GLSL_SetUniformInt(&tr.tonemapShader[i], UNIFORM_LEVELSMAP, TB_LEVELSMAP);
		qglUseProgram(0);
		GLSL_FinishGPUShader(&tr.tonemapShader[i]);
	}
	return 2;
}

static int GLSL_LoadGPUProgramCalcLuminanceLevel(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc )
{
	int numPrograms = 0;
	Allocator allocator(scratchAlloc.Base(), scratchAlloc.GetSize());

	char extradefines[1200];
	const GPUProgramDesc *programDesc =
		LoadProgramSource("calclevels4x", allocator, fallback_calclevels4xProgram);
	for ( int i = 0; i < 2; i++ )
	{
		const uint32_t attribs = ATTR_POSITION | ATTR_TEXCOORD0;
		extradefines[0] = '\0';

		if (!i)
			Q_strcat(extradefines, sizeof(extradefines), "#define FIRST_PASS\n");

		if (!GLSL_LoadGPUShader(builder, &tr.calclevels4xShader[i], "calclevels4x", attribs,
				NO_XFB_VARS, extradefines, *programDesc))
		{
			ri.Error(ERR_FATAL, "Could not load calclevels4x shader!");
		}

		GLSL_InitUniforms(&tr.calclevels4xShader[i]);

		qglUseProgram(tr.calclevels4xShader[i].program);
		GLSL_SetUniformInt(&tr.calclevels4xShader[i], UNIFORM_TEXTUREMAP, TB_DIFFUSEMAP);
		qglUseProgram(0);

		GLSL_FinishGPUShader(&tr.calclevels4xShader[i]);

		++numPrograms;
	}

	return numPrograms;
}

static int GLSL_LoadGPUProgramSSAO(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc )
{
	GLSL_LoadGPUProgramBasic(
		builder,
		scratchAlloc,
		&tr.ssaoShader,
		"ssao",
		fallback_ssaoProgram);

	GLSL_InitUniforms(&tr.ssaoShader);

	qglUseProgram(tr.ssaoShader.program);
	GLSL_SetUniformInt(&tr.ssaoShader, UNIFORM_SCREENDEPTHMAP, TB_COLORMAP);
	qglUseProgram(0);

	GLSL_FinishGPUShader(&tr.ssaoShader);

	return 1;
}

static int GLSL_LoadEnhancementPrograms(ShaderProgramBuilder& builder, Allocator& scratch)
{
	Allocator source(scratch.Base(), scratch.GetSize());
	const GPUProgramDesc *desc = LoadProgramSource("smaa", source, fallback_smaaProgram);
	for (int i = 0; i < 3; ++i)
	{
		shaderProgram_t *program = &tr.smaaShader[i];
		if (!GLSL_LoadGPUShader(builder, program, "smaa", 0, NO_XFB_VARS, va("#define SMAA_PASS %d\n", i), *desc))
			ri.Error(ERR_FATAL, "Could not load SMAA shader");
		GLSL_InitUniforms(program);
		qglUseProgram(program->program);
		GLSL_SetUniformInt(program, UNIFORM_SCREENIMAGEMAP, 0);
		GLSL_SetUniformInt(program, UNIFORM_DIFFUSEMAP, 1);
		GLSL_SetUniformInt(program, UNIFORM_NORMALMAP, 2);
		GLSL_FinishGPUShader(program);
	}
	GLSL_LoadGPUProgramBasic(builder, scratch, &tr.capsuleShader, "capsule", fallback_capsuleProgram);
	GLSL_InitUniforms(&tr.capsuleShader);
	qglUseProgram(tr.capsuleShader.program);
	GLSL_SetUniformInt(&tr.capsuleShader, UNIFORM_SCREENDEPTHMAP, 0);
	GLSL_FinishGPUShader(&tr.capsuleShader);
	GLSL_LoadGPUProgramBasic(builder, scratch, &tr.sssShader, "skin_diffusion", fallback_skin_diffusionProgram);
	GLSL_InitUniforms(&tr.sssShader);
	qglUseProgram(tr.sssShader.program);
	GLSL_SetUniformInt(&tr.sssShader, UNIFORM_SCREENIMAGEMAP, 0);
	GLSL_SetUniformInt(&tr.sssShader, UNIFORM_SCREENDEPTHMAP, 1);
	GLSL_SetUniformInt(&tr.sssShader, UNIFORM_DIFFUSEMAP, 2);
	GLSL_SetUniformInt(&tr.sssShader, UNIFORM_NORMALMAP, 3);
	GLSL_FinishGPUShader(&tr.sssShader);
	qglUseProgram(0);
	return 5;
}

static int GLSL_LoadGPUProgramPrefilterEnvMap(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc)
{
	GLSL_LoadGPUProgramBasic(
		builder,
		scratchAlloc,
		&tr.prefilterEnvMapShader,
		"prefilterEnvMap",
		fallback_prefilterEnvMapProgram);

	GLSL_InitUniforms(&tr.prefilterEnvMapShader);

	qglUseProgram(tr.prefilterEnvMapShader.program);
	GLSL_SetUniformInt(&tr.prefilterEnvMapShader, UNIFORM_CUBEMAP, TB_CUBEMAP);
	qglUseProgram(0);

	GLSL_FinishGPUShader(&tr.prefilterEnvMapShader);

	return 1;
}

static int GLSL_LoadGPUProgramDepthBlur(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc )
{
	int numPrograms = 0;
	Allocator allocator(scratchAlloc.Base(), scratchAlloc.GetSize());

	char extradefines[1200];
	const GPUProgramDesc *programDesc =
		LoadProgramSource("depthBlur", allocator, fallback_depthblurProgram);
	for ( int i = 0; i < 2; i++ )
	{
		const uint32_t attribs = ATTR_POSITION | ATTR_TEXCOORD0;
		extradefines[0] = '\0';

		if (i & 1)
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_VERTICAL_BLUR\n");
		else
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_HORIZONTAL_BLUR\n");


		if (!GLSL_LoadGPUShader(builder, &tr.depthBlurShader[i], "depthBlur", attribs, NO_XFB_VARS,
				extradefines, *programDesc))
		{
			ri.Error(ERR_FATAL, "Could not load depthBlur shader!");
		}

		GLSL_InitUniforms(&tr.depthBlurShader[i]);

		qglUseProgram(tr.depthBlurShader[i].program);
		GLSL_SetUniformInt(&tr.depthBlurShader[i], UNIFORM_SCREENIMAGEMAP, TB_COLORMAP);
		GLSL_SetUniformInt(&tr.depthBlurShader[i], UNIFORM_SCREENDEPTHMAP, TB_LIGHTMAP);
		qglUseProgram(0);

		GLSL_FinishGPUShader(&tr.depthBlurShader[i]);

		++numPrograms;
	}

	return numPrograms;
}

static int GLSL_LoadGPUProgramGaussianBlur(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc )
{
	Allocator allocator(scratchAlloc.Base(), scratchAlloc.GetSize());

	char extradefines[1200];
	const GPUProgramDesc *programDesc =
		LoadProgramSource("gaussian_blur", allocator, fallback_gaussian_blurProgram);
	const uint32_t attribs = 0;

	extradefines[0] = '\0';
	Q_strcat (extradefines, sizeof (extradefines), "#define BLUR_X");

	if (!GLSL_LoadGPUShader(builder, &tr.gaussianBlurShader[0], "gaussian_blur", attribs,
			NO_XFB_VARS, extradefines, *programDesc))
	{
		ri.Error(ERR_FATAL, "Could not load gaussian_blur (X-direction) shader!");
	}

	if (!GLSL_LoadGPUShader(builder, &tr.gaussianBlurShader[1], "gaussian_blur", attribs,
			NO_XFB_VARS, nullptr, *programDesc))
	{
		ri.Error(ERR_FATAL, "Could not load gaussian_blur (Y-direction) shader!");
	}

	int numPrograms = 0;
	for ( int i = 0; i < 2; i++ )
	{
		GLSL_InitUniforms(&tr.gaussianBlurShader[i]);
		GLSL_FinishGPUShader(&tr.gaussianBlurShader[i]);
		++numPrograms;
	}

	return numPrograms;
}

static int GLSL_LoadGPUProgramDynamicGlowUpsample(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc )
{
	GLSL_LoadGPUProgramBasic(
		builder,
		scratchAlloc,
		&tr.dglowUpsample,
		"dglow_upsample",
		fallback_dglow_upsampleProgram,
		0);

	GLSL_InitUniforms(&tr.dglowUpsample);
	GLSL_FinishGPUShader(&tr.dglowUpsample);
	return 1;
}

static int GLSL_LoadGPUProgramDynamicGlowDownsample(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc )
{
	GLSL_LoadGPUProgramBasic(
		builder,
		scratchAlloc,
		&tr.dglowDownsample,
		"dglow_downsample",
		fallback_dglow_downsampleProgram,
		0);

	GLSL_InitUniforms(&tr.dglowDownsample);
	GLSL_FinishGPUShader(&tr.dglowDownsample);
	return 1;
}

static int GLSL_LoadGPUProgramSurfaceSprites(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc )
{
	int numPrograms = 0;
	Allocator allocator(scratchAlloc.Base(), scratchAlloc.GetSize());

	char extradefines[1200];
	const GPUProgramDesc *programDesc =
		LoadProgramSource("surface_sprites", allocator, fallback_surface_spritesProgram);
	const uint32_t attribs = ATTR_POSITION | ATTR_POSITION2 | ATTR_NORMAL | ATTR_COLOR;
	for ( int i = 0; i < SSDEF_COUNT; ++i )
	{
		extradefines[0] = '\0';

		if ( (i & SSDEF_FACE_CAMERA) && (i & SSDEF_FACE_UP) )
			continue;

		if ( i & SSDEF_FACE_CAMERA )
			Q_strcat(extradefines, sizeof(extradefines),
					"#define FACE_CAMERA\n");
		else if ( i & SSDEF_FACE_UP )
			Q_strcat(extradefines, sizeof(extradefines),
					"#define FACE_UP\n");
		else if (i & SSDEF_FLATTENED)
			Q_strcat(extradefines, sizeof(extradefines),
				"#define FACE_FLATTENED\n");

		if (i & SSDEF_FX_SPRITE)
			Q_strcat(extradefines, sizeof(extradefines),
				"#define FX_SPRITE\n");

		if ( i & SSDEF_USE_FOG )
			Q_strcat(extradefines, sizeof(extradefines),
				"#define USE_FOG\n");

		if ( i & SSDEF_ALPHA_TEST )
			Q_strcat(extradefines, sizeof(extradefines),
					"#define ALPHA_TEST\n");

		if (i & SSDEF_ADDITIVE)
			Q_strcat(extradefines, sizeof(extradefines),
				"#define ADDITIVE_BLEND\n");

		shaderProgram_t *program = tr.spriteShader + i;
		if (!GLSL_LoadGPUShader(builder, program, "surface_sprites", attribs, NO_XFB_VARS,
				extradefines, *programDesc))
		{
			ri.Error(ERR_FATAL, "Could not load surface sprites shader!");
		}

		GLSL_InitUniforms(program);
		GLSL_FinishGPUShader(program);
		++numPrograms;
	}

	return numPrograms;
}

static int GLSL_LoadGPUProgramWeather(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc )
{
	GLSL_LoadGPUProgramBasic(
		builder,
		scratchAlloc,
		&tr.weatherShader,
		"weather",
		fallback_weatherProgram,
		ATTR_POSITION | ATTR_COLOR);

	GLSL_InitUniforms(&tr.weatherShader);
	qglUseProgram(tr.weatherShader.program);
	GLSL_SetUniformInt(&tr.weatherShader, UNIFORM_SHADOWMAP, TB_SHADOWMAP);
	GLSL_SetUniformInt(&tr.weatherShader, UNIFORM_DIFFUSEMAP, TB_DIFFUSEMAP);
	qglUseProgram(0);
	GLSL_FinishGPUShader(&tr.weatherShader);

	GLSL_LoadGPUProgramBasic(
		builder,
		scratchAlloc,
		&tr.weatherUpdateShader,
		"weatherUpdate",
		fallback_weatherUpdateProgram,
		ATTR_POSITION | ATTR_COLOR,
		(1u << XFB_VAR_POSITION) | (1u << XFB_VAR_VELOCITY));

	GLSL_InitUniforms(&tr.weatherUpdateShader);
	GLSL_FinishGPUShader(&tr.weatherUpdateShader);

	return 2;
}

void GLSL_LoadGPUShaders()
{
#if 0
	// vertex size = 48 bytes
	VertexFormat bspVertexFormat = {
		{
			{ 3, false, GL_FLOAT, false, 0 }, // position
			{ 2, false, GL_HALF_FLOAT, false, 12 }, // tc0
			{ 2, false, GL_HALF_FLOAT, false, 16 }, // tc1
			{ 2, false, GL_HALF_FLOAT, false, 20 }, // tc2
			{ 2, false, GL_HALF_FLOAT, false, 24 }, // tc3
			{ 2, false, GL_HALF_FLOAT, false, 28 }, // tc4
			{ 4, false, GL_UNSIGNED_INT_2_10_10_10_REV, true, 32 }, // tangent
			{ 4, false, GL_UNSIGNED_INT_2_10_10_10_REV, true, 36 }, // normal
			{ 4, false, GL_FLOAT, false, 40 }, // color
			{ 4, false, GL_UNSIGNED_INT_2_10_10_10_REV, true, 44 }, // light dir
		}
	};

	// vertex size = 32 bytes
	VertexFormat rectVertexFormat = {
		{
			{ 3, false, GL_FLOAT, false, 0 }, // position
			{ 2, false, GL_HALF_FLOAT, false, 12 }, // tc0
			{ 4, false, GL_FLOAT, false, 16 } // color
		}
	};

	// vertex size = 32 bytes
	VertexFormat g2VertexFormat = {
		{
			{ 3, false, GL_FLOAT, false, 0 }, // position
			{ 2, false, GL_HALF_FLOAT, false, 12 }, // tc0
			{ 4, false, GL_UNSIGNED_INT_2_10_10_10_REV, true, 16 }, // tangent
			{ 4, false, GL_UNSIGNED_INT_2_10_10_10_REV, true, 20 }, // normal
			{ 4, true,  GL_UNSIGNED_BYTE, false, 24 }, // bone indices
			{ 4, false, GL_UNSIGNED_BYTE, true, 28 }, // bone weights
		}
	};

	// vertex size = 44 bytes
	VertexFormat md3VertexFormat = {
		{
			{ 3, false, GL_FLOAT, false, 0 }, // position
			{ 2, false, GL_HALF_FLOAT, false, 12 }, // tc0
			{ 4, false, GL_UNSIGNED_INT_2_10_10_10_REV, true, 16 }, // tangent
			{ 4, false, GL_UNSIGNED_INT_2_10_10_10_REV, true, 20 }, // normal
			{ 3, false,p GL_FLOAT, false, 24 }, // pos2
			{ 4, false, GL_UNSIGNED_INT_2_10_10_10_REV, true, 36 }, // tangent
			{ 4, false, GL_UNSIGNED_INT_2_10_10_10_REV, true, 40 }, // normal
		}
	};
#endif

	ri.Printf(PRINT_ALL, "------- GLSL_InitGPUShaders -------\n");

	R_IssuePendingRenderCommands();

	int startTime = ri.Milliseconds();

	Allocator allocator(512 * 1024);
	ShaderProgramBuilder builder;

	int numGenShaders = 0;
	int numLightShaders = 0;
	int numEtcShaders = 0;
	numGenShaders += GLSL_LoadGPUProgramGeneric(builder, allocator);
	numEtcShaders += GLSL_LoadGPUProgramGlass(builder, allocator);
	numLightShaders += GLSL_LoadGPUProgramLightAll(builder, allocator);
	numEtcShaders += GLSL_LoadGPUProgramFogPass(builder, allocator);
	numEtcShaders += GLSL_LoadGPUProgramRefraction(builder, allocator);
	numEtcShaders += GLSL_LoadGPUProgramTextureColor(builder, allocator);
	GLSL_LoadGPUProgramBasic(builder, allocator, &tr.skyCubeShader, "skycube", fallback_skycubeProgram, ATTR_POSITION);
	GLSL_InitUniforms(&tr.skyCubeShader);
	qglUseProgram(tr.skyCubeShader.program);
	GLSL_SetUniformInt(&tr.skyCubeShader, UNIFORM_DIFFUSEMAP, TB_DIFFUSEMAP);
	qglUseProgram(0);
	GLSL_FinishGPUShader(&tr.skyCubeShader);
	// Fail at load time on every driver if an external or built-in sky shader
	// omits an attachment. Otherwise some drivers bloom the entire sky.
	if (qglGetFragDataLocation(tr.skyCubeShader.program, "out_Color") != 0 ||
		qglGetFragDataLocation(tr.skyCubeShader.program, "out_Glow") != 1)
		ri.Error(ERR_DROP, "Sky shader must write scene color and glow outputs");
	GLSL_LoadGPUProgramBasic(builder, allocator, &tr.localFogShader, "localfog", fallback_localfogProgram, 0);
	GLSL_InitUniforms(&tr.localFogShader);
	qglUseProgram(tr.localFogShader.program);
	GLSL_SetUniformInt(&tr.localFogShader, UNIFORM_TORCHSHADOWMAP, TB_TORCHSHADOWMAP);
	qglUseProgram(0);
	GLSL_FinishGPUShader(&tr.localFogShader);
	numEtcShaders += GLSL_LoadGPUProgramPShadow(builder, allocator);
	numEtcShaders += GLSL_LoadGPUProgramVShadow(builder, allocator);
	numEtcShaders += GLSL_LoadGPUProgramDownscale4x(builder, allocator);
	numEtcShaders += GLSL_LoadGPUProgramBokeh(builder, allocator);
	numEtcShaders += GLSL_LoadGPUProgramTonemap(builder, allocator);
	numEtcShaders += GLSL_LoadGPUProgramCalcLuminanceLevel(builder, allocator);
	numEtcShaders += GLSL_LoadGPUProgramSSAO(builder, allocator);
	numEtcShaders += GLSL_LoadEnhancementPrograms(builder, allocator);
	if (r_cubeMapping->integer || r_glassProbes->integer)
		numEtcShaders += GLSL_LoadGPUProgramPrefilterEnvMap(builder, allocator);
	numEtcShaders += GLSL_LoadGPUProgramDepthBlur(builder, allocator);
	numEtcShaders += GLSL_LoadGPUProgramGaussianBlur(builder, allocator);
	numEtcShaders += GLSL_LoadGPUProgramDynamicGlowUpsample(builder, allocator);
	numEtcShaders += GLSL_LoadGPUProgramDynamicGlowDownsample(builder, allocator);
	numEtcShaders += GLSL_LoadGPUProgramSurfaceSprites(builder, allocator);
	numEtcShaders += GLSL_LoadGPUProgramWeather(builder, allocator);
#ifdef REND2_SP
	GLSL_LoadGPUProgramBasic(builder, allocator, &tr.g2ValidateShader, "g2validate", fallback_g2validateProgram,
		ATTR_POSITION | ATTR_BONE_INDEXES | ATTR_BONE_WEIGHTS, (1u << XFB_VAR_POSITION));
	GLSL_InitUniforms(&tr.g2ValidateShader);
	GLSL_FinishGPUShader(&tr.g2ValidateShader);
	++numEtcShaders;
#endif

	builder.PrintCacheStats();
	builder.Reset();
	GLSL_ClearRetainedPrograms();
	ri.Printf(PRINT_ALL, "loaded %i GLSL shaders (%i gen %i light %i etc) in %5.2f seconds\n",
		numGenShaders + numLightShaders + numEtcShaders, numGenShaders, numLightShaders,
		numEtcShaders, (ri.Milliseconds() - startTime) / 1000.0);
}

void GLSL_ShutdownGPUShaders(qboolean destroyWindow)
{
	int i;

	ri.Printf(PRINT_ALL, "------- GLSL_ShutdownGPUShaders -------\n");

	if (tr.globalVao)
		for ( int i = 0; i < ATTR_INDEX_MAX; i++ )
			qglDisableVertexAttribArray(i);

	GLSL_BindNullProgram();
	qglUseProgram(0);

#ifdef REND2_SP
	if (!destroyWindow)
	{
		for (auto& entry : activePrograms)
		{
			shaderProgram_t *program = entry.second;
			if (program->program && program->uniforms &&
				program->uniformBufferOffsets && program->uniformBuffer)
			{
				retainedPrograms.emplace(std::move(entry.first), *program);
				*program = {};
			}
		}
	}
	else
#endif
		GLSL_ClearRetainedPrograms();
	activePrograms.clear();

	GLSL_DeleteGPUShader(&tr.splashScreenShader);
	for (auto &program : tr.glassShader) GLSL_DeleteGPUShader(&program);

	for ( i = 0; i < GENERICDEF_COUNT; i++)
		GLSL_DeleteGPUShader(&tr.genericShader[i]);

	for (i = 0; i < REFRACTIONDEF_COUNT; i++)
		GLSL_DeleteGPUShader(&tr.refractionShader[i]);

	GLSL_DeleteGPUShader(&tr.textureColorShader);
	GLSL_DeleteGPUShader(&tr.skyCubeShader);
	GLSL_DeleteGPUShader(&tr.localFogShader);

	for ( i = 0; i < FOGDEF_COUNT; i++)
		GLSL_DeleteGPUShader(&tr.fogShader[i]);

	for ( i = 0; i < LIGHTDEF_COUNT; i++)
		GLSL_DeleteGPUShader(&tr.lightallShader[i]);

	GLSL_DeleteGPUShader(&tr.pshadowShader);
	GLSL_DeleteGPUShader(&tr.volumeShadowShader);
	GLSL_DeleteGPUShader(&tr.down4xShader);
	GLSL_DeleteGPUShader(&tr.bokehShader);

	for (i = 0; i < 2; ++i)
		GLSL_DeleteGPUShader(&tr.tonemapShader[i]);

	for ( i = 0; i < 2; i++)
		GLSL_DeleteGPUShader(&tr.calclevels4xShader[i]);

	GLSL_DeleteGPUShader(&tr.ssaoShader);
	for (auto &program : tr.smaaShader) GLSL_DeleteGPUShader(&program);
	GLSL_DeleteGPUShader(&tr.sssShader);
	GLSL_DeleteGPUShader(&tr.capsuleShader);

	for ( i = 0; i < 2; i++)
		GLSL_DeleteGPUShader(&tr.depthBlurShader[i]);

	GLSL_DeleteGPUShader(&tr.testcubeShader);
	GLSL_DeleteGPUShader(&tr.prefilterEnvMapShader);

	for (i = 0; i < 2; ++i)
		GLSL_DeleteGPUShader(&tr.gaussianBlurShader[i]);

	GLSL_DeleteGPUShader(&tr.glowCompositeShader);
	GLSL_DeleteGPUShader(&tr.dglowDownsample);
	GLSL_DeleteGPUShader(&tr.dglowUpsample);

	for (i = 0; i < SSDEF_COUNT; ++i)
		GLSL_DeleteGPUShader(&tr.spriteShader[i]);

	GLSL_DeleteGPUShader(&tr.weatherUpdateShader);
#ifdef REND2_SP
	GLSL_DeleteGPUShader(&tr.g2ValidateShader);
#endif
	GLSL_DeleteGPUShader(&tr.weatherShader);

	glState.currentProgram = 0;
	qglUseProgram(0);
}

void GLSL_BindProgram(shaderProgram_t * program)
{
	if(!program)
	{
		GLSL_BindNullProgram();
		return;
	}

	if(r_logFile->integer)
	{
		// don't just call LogComment, or we will get a call to va() every frame!
		GLimp_LogComment(va("--- GL_BindProgram( %s ) ---\n", program->name));
	}

	if(glState.currentProgram != program)
	{
		qglUseProgram(program->program);
		glState.currentProgram = program;
		backEnd.pc.c_glslShaderBinds++;
	}
}


void GLSL_BindNullProgram(void)
{
	if(r_logFile->integer)
	{
		GLimp_LogComment("--- GL_BindNullProgram ---\n");
	}

	if(glState.currentProgram)
	{
		qglUseProgram(0);
		glState.currentProgram = NULL;
	}
}

void GLSL_VertexAttribsState(uint32_t stateBits, VertexArraysProperties *vertexArraysOut)
{
	VertexArraysProperties vertexArraysLocal;
	VertexArraysProperties *vertexArrays = vertexArraysOut;

	if ( !vertexArrays )
	{
		vertexArrays = &vertexArraysLocal;
	}

	if ( tess.useInternalVBO )
	{
		CalculateVertexArraysProperties(stateBits, vertexArrays);
		for ( int i = 0; i < vertexArrays->numVertexArrays; i++ )
		{
			int attributeIndex = vertexArrays->enabledAttributes[i];
			vertexArrays->offsets[attributeIndex] += backEndData->currentFrame->dynamicVboCommitOffset;
		}
	}
	else
	{
		CalculateVertexArraysFromVBO(stateBits, glState.currentVBO, vertexArrays);
	}

	GLSL_VertexAttribPointers(vertexArrays);

}

void GL_VertexArraysToAttribs(
	vertexAttribute_t *attribs,
	size_t attribsCount,
	const VertexArraysProperties *vertexArrays)
{
	assert(attribsCount == ATTR_INDEX_MAX);

	static const struct
	{
		int numComponents;
		GLboolean integerAttribute;
		GLenum type;
		GLboolean normalize;
	} attributes[ATTR_INDEX_MAX] = {
		{ 3, GL_FALSE, GL_FLOAT, GL_FALSE }, // position
		{ 2, GL_FALSE, GL_FLOAT, GL_FALSE }, // tc0
		{ 2, GL_FALSE, GL_FLOAT, GL_FALSE }, // tc1
		{ 2, GL_FALSE, GL_FLOAT, GL_FALSE }, // tc2
		{ 2, GL_FALSE, GL_FLOAT, GL_FALSE }, // tc3
		{ 2, GL_FALSE, GL_FLOAT, GL_FALSE }, // tc4
		{ 4, GL_FALSE, GL_UNSIGNED_INT_2_10_10_10_REV, GL_TRUE }, // tangent
		{ 4, GL_FALSE, GL_UNSIGNED_INT_2_10_10_10_REV, GL_TRUE }, // normal
		{ 4, GL_FALSE, GL_FLOAT, GL_FALSE }, // color
		{ 4, GL_FALSE, GL_UNSIGNED_INT_2_10_10_10_REV, GL_TRUE }, // light direction
		{ 4, GL_TRUE,  GL_UNSIGNED_BYTE, GL_FALSE }, // bone indices
		{ 4, GL_FALSE, GL_UNSIGNED_BYTE, GL_TRUE }, // bone weights
#ifdef REND2_SP
		{ 3, GL_FALSE, GL_FLOAT, GL_FALSE }, // pos2
		{ 4, GL_FALSE, GL_UNSIGNED_INT_2_10_10_10_REV, GL_TRUE }, // tangent2
		{ 4, GL_FALSE, GL_UNSIGNED_INT_2_10_10_10_REV, GL_TRUE }, // normal2
#endif // REND2_SP
	};

	for ( int i = 0; i < vertexArrays->numVertexArrays; i++ )
	{
		int attributeIndex = vertexArrays->enabledAttributes[i];
		vertexAttribute_t& attrib = attribs[i];

		attrib.vbo = glState.currentVBO;
		attrib.index = attributeIndex;
		attrib.numComponents = attributes[attributeIndex].numComponents;
		attrib.integerAttribute = attributes[attributeIndex].integerAttribute;
		attrib.type = attributes[attributeIndex].type;
		attrib.normalize = attributes[attributeIndex].normalize;
#ifdef REND2_SP
		if (attributeIndex == ATTR_INDEX_BONE_WEIGHTS && vertexArrays->sizes[attributeIndex] == sizeof(vec4_t))
		{
			attrib.type = GL_FLOAT;
			attrib.normalize = GL_FALSE;
		}
#endif
		attrib.stride = vertexArrays->strides[attributeIndex];
		attrib.offset = vertexArrays->offsets[attributeIndex];
		attrib.stepRate = 0;
	}
}

void GLSL_VertexAttribPointers(const VertexArraysProperties *vertexArrays)
{
	// don't just call LogComment, or we will get a call to va() every frame!
	if (r_logFile->integer)
	{
		GLimp_LogComment("--- GL_VertexAttribPointers() ---\n");
	}

	vertexAttribute_t attribs[ATTR_INDEX_MAX] = {};
	GL_VertexArraysToAttribs(attribs, ARRAY_LEN(attribs), vertexArrays);
	GL_VertexAttribPointers(vertexArrays->numVertexArrays, attribs);
}


shaderProgram_t *GLSL_GetGenericShaderProgram(int stage)
{
	shaderStage_t *pStage = tess.xstages[stage];
	int shaderAttribs = 0;

	if ( pStage->alphaTestType != ALPHA_TEST_NONE )
		shaderAttribs |= GENERICDEF_USE_ALPHA_TEST;

	if (backEnd.currentEntity->e.renderfx & (RF_DISINTEGRATE1 | RF_DISINTEGRATE2))
		shaderAttribs |= GENERICDEF_USE_RGBAGEN;

	if (backEnd.currentEntity->e.renderfx & RF_DISINTEGRATE2)
		shaderAttribs |= GENERICDEF_USE_DEFORM_VERTEXES;

	switch (pStage->rgbGen)
	{
		case CGEN_LIGHTING_DIFFUSE:
			shaderAttribs |= GENERICDEF_USE_RGBAGEN;
			break;
		default:
			break;
	}

	switch (pStage->alphaGen)
	{
		case AGEN_LIGHTING_SPECULAR:
		case AGEN_PORTAL:
			shaderAttribs |= GENERICDEF_USE_RGBAGEN;
			break;
		default:
			break;
	}

	if (tess.fogNum &&
		pStage->adjustColorsForFog != ACFF_NONE &&
		r_drawfog->integer)
		shaderAttribs |= GENERICDEF_USE_FOG;

	if (pStage->bundle[0].tcGen != TCGEN_TEXTURE)
	{
		shaderAttribs |= GENERICDEF_USE_TCGEN_AND_TCMOD;
	}

	if (tess.shader->numDeforms && !ShaderRequiresCPUDeforms(tess.shader))
	{
		shaderAttribs |= GENERICDEF_USE_DEFORM_VERTEXES;
	}
#ifdef REND2_SP
	if (glState.vertexAnimation)
	{
		shaderAttribs |= GENERICDEF_USE_VERTEX_ANIMATION;
	}
#endif // REND2_SP
	if (glState.skeletalAnimation)
	{
		shaderAttribs |= GENERICDEF_USE_SKELETAL_ANIMATION;
	}

	if (pStage->bundle[0].numTexMods)
	{
		shaderAttribs |= GENERICDEF_USE_TCGEN_AND_TCMOD;
	}

	if (pStage->glow)
	{
		shaderAttribs |= GENERICDEF_USE_GLOW_BUFFER;
	}

	return &tr.genericShader[shaderAttribs];
}
