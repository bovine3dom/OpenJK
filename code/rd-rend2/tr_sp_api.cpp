#include "tr_local.h"
#include "tr_cache.h"
#include "tr_weather.h"

#undef ri
refimport_t ri;
Rend2Imports riRend2;

extern IGhoul2InfoArray &TheGhoul2InfoArray();
extern void G2API_AnimateG2Models(CGhoul2Info_v &, int, CRagDollUpdateParams *);
extern void G2API_SetRagDoll(CGhoul2Info_v &, CRagDollParams *);
extern qboolean G2API_GetRagBonePos(CGhoul2Info_v &, const char *, vec3_t, vec3_t, vec3_t, vec3_t);
extern qboolean G2API_RagEffectorKick(CGhoul2Info_v &, const char *, vec3_t);
extern qboolean G2API_RagEffectorGoal(CGhoul2Info_v &, const char *, vec3_t);
extern qboolean G2API_RagForceSolve(CGhoul2Info_v &, qboolean);
extern qboolean G2API_RagPCJGradientSpeed(CGhoul2Info_v &, const char *, float);
extern qboolean G2API_RagPCJConstraint(CGhoul2Info_v &, const char *, vec3_t, vec3_t);
extern qboolean G2API_SetBoneIKState(CGhoul2Info_v &, int, const char *, int, sharedSetBoneIKStateParams_t *);
extern qboolean G2API_IKMove(CGhoul2Info_v &, int, sharedIKMoveParams_t *);
#ifdef G2_PERFORMANCE_ANALYSIS
extern void G2Time_ResetTimers();
extern void G2Time_ReportTimers();
#endif

static bool serverRegistration;

static void RE_SP_SVModelInit()
{
	RE_BeginRegistration(&glConfig);
	serverRegistration = true;
}

static void RE_SP_BeginRegistration(glconfig_t *config)
{
	// SP registers server models before the client starts the renderer.
	if (serverRegistration && tr.registered)
		*config = glConfig;
	else
		RE_BeginRegistration(config);
	serverRegistration = false;
}

static void RE_SP_AddPolyToScene(qhandle_t shader, int numVerts, const polyVert_t *verts)
{
	RE_AddPolyToScene(shader, numVerts, verts, 1);
}

static void RE_SP_LerpTag(orientation_t *tag, qhandle_t model, int startFrame,
	int endFrame, float fraction, const char *name)
{
	R_LerpTag(tag, model, startFrame, endFrame, fraction, name);
}

static qboolean RE_SP_GetLighting(const vec3_t origin, vec3_t ambient,
	vec3_t directed, vec3_t direction)
{
	vec3_t point;
	VectorCopy(origin, point);
	if (R_LightForPoint(point, ambient, directed, direction))
		return qtrue;
	VectorSet(ambient, 255.0f, 255.0f, 255.0f);
	VectorCopy(ambient, directed);
	VectorCopy(tr.sunDirection, direction);
	return qfalse;
}

static qboolean RE_SP_InPVS(vec3_t first, vec3_t second)
{
	return R_inPVS(first, second, nullptr);
}

static void RE_SP_InitWorldEffects()
{
	R_IssuePendingRenderCommands();
	R_ShutdownWeatherSystem();
	R_InitWeatherSystem();
	if (tr.world)
		R_InitWeatherForMap();
}

static unsigned int RE_SP_ReadChar(char *text, int *advance, qboolean *trailingPunctuation)
{
	return AnyLanguage_ReadCharFromString(text, advance, trailingPunctuation);
}

static unsigned int RE_SP_ReadChar2(char **text, qboolean *trailingPunctuation)
{
	int advance;
	unsigned int character = AnyLanguage_ReadCharFromString(*text, &advance, trailingPunctuation);
	*text += advance;
	return character;
}

static qboolean allowScreenDissolve;

static void RE_SP_LevelLoadBegin(const char *mapName, ForceReload_e forceReload, qboolean allowDissolve)
{
	allowScreenDissolve = allowDissolve;
	if (forceReload == eForceReload_BSP || forceReload == eForceReload_ALL)
		ri.CM_DeleteCachedMap(qtrue);
	C_LevelLoadBegin(mapName, forceReload);
}

static void RE_SP_LevelLoadEnd()
{
	C_Models_LevelLoadEnd(qfalse);
	C_Images_LevelLoadEnd();
	ri.SND_RegisterAudio_LevelLoadEnd(qfalse);
	if (allowScreenDissolve)
		RE_InitDissolve(qfalse);
	ri.S_RestartMusic();
	*ri.gbAlreadyDoingLoad() = qfalse;
}

void R_ClearStuffToStopGhoul2CrashingThings()
{
	RE_Shutdown(qfalse, qfalse);
}

static float *RE_SP_DistortionAlpha() { return &tr_distortionAlpha; }
static float *RE_SP_DistortionStretch() { return &tr_distortionStretch; }
static qboolean *RE_SP_DistortionPrePost() { return &tr_distortionPrePost; }
static qboolean *RE_SP_DistortionNegate() { return &tr_distortionNegate; }

extern "C" Q_EXPORT refexport_t *QDECL GetRefAPI(int apiVersion, refimport_t *imports)
{
	static refexport_t re;
	if (!imports)
		return nullptr;
	if (apiVersion != REF_API_VERSION)
	{
		imports->Printf(PRINT_ALL, "rdsp-rend2: expected REF_API_VERSION %i, got %i\n",
			REF_API_VERSION, apiVersion);
		return nullptr;
	}

	ri = *imports;
	riRend2 = Rend2Imports(*imports);
	re = {};

#define REX(name) re.name = RE_##name
	REX(Shutdown);
	re.BeginRegistration = RE_SP_BeginRegistration;
	REX(RegisterModel);
	REX(RegisterSkin);
	REX(GetAnimationCFG);
	REX(RegisterShader);
	REX(RegisterShaderNoMip);
	re.LoadWorld = RE_LoadWorldMap;
	re.GetAtmosphere = RE_GetAtmosphere;
	re.ApplyAtmosphere = RE_ApplyAtmosphere;
	re.R_LoadImage = R_LoadImage;
	re.RegisterMedia_LevelLoadBegin = RE_SP_LevelLoadBegin;
	re.RegisterMedia_LevelLoadEnd = RE_SP_LevelLoadEnd;
	re.RegisterMedia_GetLevel = C_GetLevel;
	re.RegisterModels_LevelLoadEnd = C_Models_LevelLoadEnd;
	re.RegisterImages_LevelLoadEnd = C_Images_LevelLoadEnd;
	REX(SetWorldVisData);
	REX(EndRegistration);
	REX(ClearScene);
	REX(AddRefEntityToScene);
	re.AddPolyToScene = RE_SP_AddPolyToScene;
	re.GetLighting = RE_SP_GetLighting;
	REX(AddLightToScene);
	REX(RenderScene);
	REX(SetColor);
	re.DrawStretchPic = RE_StretchPic;
	re.DrawUiGeometry = RE_DrawUiGeometry;
	re.CreateUiTexture = RE_CreateUiTexture;
	re.ReleaseUiTexture = RE_ReleaseUiTexture;
	re.DrawRotatePic = RE_RotatePic;
	re.DrawRotatePic2 = RE_RotatePic2;
	REX(LAGoggles);
	REX(Scissor);
	re.DrawStretchRaw = RE_StretchRaw;
	REX(UploadCinematic);
	REX(BeginFrame);
	REX(EndFrame);
	REX(ProcessDissolve);
	REX(InitDissolve);
	REX(GetScreenShot);
	REX(TempRawImage_ReadFromFile);
	REX(TempRawImage_CleanUp);
	re.MarkFragments = R_MarkFragments;
	re.LerpTag = RE_SP_LerpTag;
	re.ModelBounds = R_ModelBounds;
	REX(GetLightStyle);
	REX(SetLightStyle);
	REX(GetBModelVerts);
	re.WorldEffectCommand = RE_WorldEffectCommand;
	REX(GetModelBounds);
	re.SVModelInit = RE_SP_SVModelInit;
	REX(RegisterFont);
	REX(Font_HeightPixels);
	REX(Font_VisualCenter);
	REX(Font_StrLenPixels);
	REX(Font_StrLenChars);
	REX(Font_DrawString);
	re.Language_IsAsian = Language_IsAsian;
	re.Language_UsesSpaces = Language_UsesSpaces;
	re.AnyLanguage_ReadCharFromString = RE_SP_ReadChar;
	re.AnyLanguage_ReadCharFromString2 = RE_SP_ReadChar2;
	re.R_InitWorldEffects = RE_SP_InitWorldEffects;
	re.R_ClearStuffToStopGhoul2CrashingThings = R_ClearStuffToStopGhoul2CrashingThings;
	re.R_inPVS = RE_SP_InPVS;
	re.tr_distortionAlpha = RE_SP_DistortionAlpha;
	re.tr_distortionStretch = RE_SP_DistortionStretch;
	re.tr_distortionPrePost = RE_SP_DistortionPrePost;
	re.tr_distortionNegate = RE_SP_DistortionNegate;
	re.GetWindVector = R_GetWindVector;
	re.GetWindGusting = R_GetWindGusting;
	re.IsOutside = R_IsOutside;
	re.IsOutsideCausingPain = R_IsOutsideCausingPain;
	re.GetChanceOfSaberFizz = R_GetChanceOfSaberFizz;
	re.IsShaking = R_IsShaking;
	re.AddWeatherZone = R_AddWeatherZone;
	re.SetTempGlobalFogColor = R_SetTempGlobalFogColor;
	REX(SetRangedFog);
#undef REX

	re.TheGhoul2InfoArray = TheGhoul2InfoArray;
#define G2EX(name) re.G2API_##name = G2API_##name
	G2EX(AddBolt);
	G2EX(AddBoltSurfNum);
	G2EX(AddSurface);
	G2EX(AnimateG2Models);
	G2EX(AttachEnt);
	G2EX(AttachG2Model);
	G2EX(CollisionDetect);
	G2EX(CleanGhoul2Models);
	G2EX(CopyGhoul2Instance);
	G2EX(DetachEnt);
	G2EX(DetachG2Model);
	G2EX(GetAnimFileName);
	G2EX(GetAnimFileNameIndex);
	G2EX(GetAnimFileInternalNameIndex);
	G2EX(GetAnimIndex);
	G2EX(GetAnimRange);
	G2EX(GetAnimRangeIndex);
	G2EX(GetBoneAnim);
	G2EX(GetBoneAnimIndex);
	G2EX(GetBoneIndex);
	G2EX(GetBoltMatrix);
	G2EX(GetGhoul2ModelFlags);
	G2EX(GetGLAName);
	G2EX(GetParentSurface);
	G2EX(GetRagBonePos);
	G2EX(GetSurfaceIndex);
	G2EX(GetSurfaceName);
	G2EX(GetSurfaceRenderStatus);
	G2EX(GetTime);
	G2EX(GiveMeVectorFromMatrix);
	G2EX(HaveWeGhoul2Models);
	G2EX(IKMove);
	G2EX(InitGhoul2Model);
	G2EX(IsPaused);
	G2EX(ListBones);
	G2EX(ListSurfaces);
	G2EX(LoadGhoul2Models);
	G2EX(LoadSaveCodeDestructGhoul2Info);
	G2EX(PauseBoneAnim);
	G2EX(PauseBoneAnimIndex);
	G2EX(PrecacheGhoul2Model);
	G2EX(RagEffectorGoal);
	G2EX(RagEffectorKick);
	G2EX(RagForceSolve);
	G2EX(RagPCJConstraint);
	G2EX(RagPCJGradientSpeed);
	G2EX(RemoveBolt);
	G2EX(RemoveBone);
	G2EX(RemoveGhoul2Model);
	G2EX(RemoveSurface);
	G2EX(SaveGhoul2Models);
	G2EX(SetAnimIndex);
	G2EX(SetBoneAnim);
	G2EX(SetBoneAnimIndex);
	G2EX(SetBoneAngles);
	G2EX(SetBoneAnglesIndex);
	G2EX(SetBoneAnglesMatrix);
	G2EX(SetBoneAnglesMatrixIndex);
	G2EX(SetBoneIKState);
	G2EX(SetGhoul2ModelFlags);
	G2EX(SetGhoul2ModelIndexes);
	G2EX(SetLodBias);
	G2EX(SetNewOrigin);
	G2EX(SetRagDoll);
	G2EX(SetRootSurface);
	G2EX(SetShader);
	G2EX(SetSkin);
	G2EX(SetSurfaceOnOff);
	G2EX(SetTime);
	G2EX(StopBoneAnim);
	G2EX(StopBoneAnimIndex);
	G2EX(StopBoneAngles);
	G2EX(StopBoneAnglesIndex);
#ifdef _G2_GORE
	G2EX(AddSkinGore);
	G2EX(ClearSkinGore);
#endif
#undef G2EX

#ifdef G2_PERFORMANCE_ANALYSIS
	re.G2Time_ResetTimers = G2Time_ResetTimers;
	re.G2Time_ReportTimers = G2Time_ReportTimers;
#endif
	return &re;
}
