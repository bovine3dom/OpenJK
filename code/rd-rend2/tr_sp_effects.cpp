/*
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2013 - 2026, OpenJK contributors

This file is part of OpenJK and is distributed under the terms of the
GNU General Public License version 2 or any later version.
*/

#include "tr_local.h"
#include <algorithm>
#include <vector>

float tr_distortionAlpha = 1.0f;
float tr_distortionStretch = 0.0f;
qboolean tr_distortionPrePost = qfalse;
qboolean tr_distortionNegate = qfalse;

namespace
{
	byte *rawImage;
	FBO_t screenFbo = {};
	image_t *screenImage;
	bool goggles;
	bool scissorEnabled;
	GLint scissorBox[4];

	enum Wipe { RIGHT_TO_LEFT, LEFT_TO_RIGHT, TOP_TO_BOTTOM, BOTTOM_TO_TOP, CIRCLE_OUT, CIRCLE_IN };
	struct Dissolve
	{
		image_t *image = nullptr, *mask = nullptr;
		int width = 0, height = 0, startTime = 0;
		Wipe type = RIGHT_TO_LEFT;
		bool active = false, touched = false;
	} dissolve;

	void UpdateImage(image_t *&image, const char *name, byte *pixels, int width, int height)
	{
		if (!image)
			image = R_CreateImage(name, pixels, width, height, IMGTYPE_COLORALPHA,
				IMGFLAG_MUTABLE | IMGFLAG_CLAMPTOEDGE | IMGFLAG_NO_COMPRESSION | IMGFLAG_NOLIGHTSCALE, GL_RGBA8);
		else
		{
			GL_BindToTMU(image, 0);
			if (image->uploadWidth != width || image->uploadHeight != height)
				qglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
			else if (pixels)
				qglTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
			image->width = image->uploadWidth = width;
			image->height = image->uploadHeight = height;
		}
	}

	void FlushDrawing()
	{
		R_IssuePendingRenderCommands();
		if (tess.numIndexes)
			RB_EndSurface();
	}

	std::vector<byte> ReadScreen()
	{
		const bool pending = backEndData->commands.used != 0 || tess.numIndexes != 0;
		FlushDrawing();
		if (!screenImage || pending)
			R_SP_CaptureScreen(qfalse);
		if (!screenImage)
			return {};
		std::vector<byte> pixels(screenFbo.width * screenFbo.height * 4);
		FBO_t *previous = glState.currentFBO;
		GLint alignment, packBuffer, rowLength;
		qglGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
		qglGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &packBuffer);
		qglGetIntegerv(GL_PACK_ROW_LENGTH, &rowLength);
		qglBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
		qglPixelStorei(GL_PACK_ALIGNMENT, 1);
		qglPixelStorei(GL_PACK_ROW_LENGTH, 0);
		FBO_Bind(&screenFbo);
		qglReadPixels(0, 0, screenFbo.width, screenFbo.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
		qglPixelStorei(GL_PACK_ALIGNMENT, alignment);
		qglPixelStorei(GL_PACK_ROW_LENGTH, rowLength);
		qglBindBuffer(GL_PIXEL_PACK_BUFFER, packBuffer);
		FBO_Bind(previous);
		return pixels;
	}

	void Resample(const byte *source, int sw, int sh, byte *dest, int dw, int dh, bool flip)
	{
		for (int y = 0; y < dh; ++y)
			for (int x = 0; x < dw; ++x)
			{
				unsigned sum[3] = {};
				for (int sy = 0; sy < 3; ++sy)
					for (int sx = 0; sx < 4; ++sx)
					{
						const int ix = MIN(sw - 1, (int)((x + (sx + 0.5f) / 4) * sw / dw));
						const int iy = MIN(sh - 1, (int)((y + (sy + 0.5f) / 3) * sh / dh));
						for (int c = 0; c < 3; ++c)
							sum[c] += source[4 * (iy * sw + ix) + c];
					}
				byte *pixel = dest + 4 * ((flip ? dh - y - 1 : y) * dw + x);
				for (int c = 0; c < 3; ++c)
					pixel[c] = sum[c] / 12;
				pixel[3] = 255;
			}
	}

	void Blit(image_t *image, float x, float y, float w, float h, int state,
		bool alphaTest = false, bool vertical = false, bool flip = false)
	{
		vec4_t vertices[4] = { {x, y, 0, 1}, {x + w, y, 0, 1},
			{x + w, y + h, 0, 1}, {x, y + h, 0, 1} };
		vec2_t texcoords[4] = { {0, 0}, {1, 0}, {1, 1}, {0, 1} };
		for (vec2_t &tc : texcoords)
		{
			if (vertical)
				std::swap(tc[0], tc[1]);
			if (flip)
				tc[1] = 1.0f - tc[1];
		}
		GL_State(state);
		GL_Cull(CT_TWO_SIDED);
		GL_BindToTMU(image, TB_DIFFUSEMAP);
		GLSL_BindProgram(&tr.textureColorShader);
		GLSL_SetUniformMatrix4x4(&tr.textureColorShader, UNIFORM_MODELVIEWPROJECTIONMATRIX, glState.modelviewProjection);
		GLSL_SetUniformVec4(&tr.textureColorShader, UNIFORM_COLOR, colorWhite);
		GLSL_SetUniformInt(&tr.textureColorShader, UNIFORM_ALPHA_TEST_TYPE,
			alphaTest ? ALPHA_TEST_LT128 : ALPHA_TEST_NONE);
		RB_InstantQuad2(vertices, texcoords);
		tess.useInternalVBO = qtrue;
		tess.externalIBO = nullptr;
		GLSL_SetUniformInt(&tr.textureColorShader, UNIFORM_ALPHA_TEST_TYPE, ALPHA_TEST_NONE);
	}
}

void R_SP_CaptureScreen(qboolean finalFrame)
{
	if (!tr.registered || glConfig.vidWidth <= 0 || glConfig.vidHeight <= 0)
		return;
	FBO_t *previous = glState.currentFBO;
	UpdateImage(screenImage, "*spScreen", nullptr, glConfig.vidWidth, glConfig.vidHeight);
	if (!screenFbo.frameBuffer)
	{
		Q_strncpyz(screenFbo.name, "spScreen", sizeof(screenFbo.name));
		qglGenFramebuffers(1, &screenFbo.frameBuffer);
		FBO_Bind(&screenFbo);
		qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, screenImage->texnum, 0);
	}
	screenFbo.width = glConfig.vidWidth;
	screenFbo.height = glConfig.vidHeight;
	screenFbo.colorImage[0] = screenImage;
	FBO_t *source = finalFrame || backEnd.framePostProcessed ? nullptr : tr.renderFbo;
	if (source && tr.msaaResolveFbo)
	{
		FBO_FastBlit(source, nullptr, tr.msaaResolveFbo, nullptr, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		source = tr.msaaResolveFbo;
	}
	GLint box[4];
	qglGetIntegerv(GL_SCISSOR_BOX, box);
	qglScissor(0, 0, screenFbo.width, screenFbo.height);
	FBO_FastBlit(source, nullptr, &screenFbo, nullptr, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	qglScissor(box[0], box[1], box[2], box[3]);
	FBO_Bind(previous);
}

image_t *R_SP_ScreenImage()
{
	return screenImage;
}

FBO_t *R_SP_ScreenFBO()
{
	return screenFbo.frameBuffer && screenImage ? &screenFbo : nullptr;
}

void RE_GetScreenShot(byte *buffer, int width, int height)
{
	if (!tr.registered || !buffer || width <= 0 || height <= 0)
		return;
	std::vector<byte> pixels = ReadScreen();
	if (pixels.empty())
		return;
	Resample(pixels.data(), screenFbo.width, screenFbo.height, buffer, width, height, true);
	if (glConfig.deviceSupportsGamma)
		for (int i = 0; i < width * height; ++i)
			R_GammaCorrect(buffer + 4 * i, 3);
}

void RE_TempRawImage_CleanUp()
{
	if (rawImage)
		Z_Free(rawImage);
	rawImage = nullptr;
}

byte *RE_TempRawImage_ReadFromFile(const char *name, int *width, int *height, byte *resampled, qboolean flip)
{
	RE_TempRawImage_CleanUp();
	if (!name || !width || !height || (resampled && (*width <= 0 || *height <= 0)))
		return nullptr;
	int sw = 0, sh = 0;
	R_LoadImage(name, &rawImage, &sw, &sh);
	if (!rawImage || sw <= 0 || sh <= 0)
	{
		RE_TempRawImage_CleanUp();
		return nullptr;
	}
	if (resampled && (*width != sw || *height != sh))
	{
		Resample(rawImage, sw, sh, resampled, *width, *height, flip != qfalse);
		return resampled;
	}
	*width = sw;
	*height = sh;
	if (flip)
		for (int y = 0; y < sh / 2; ++y)
			for (int x = 0; x < sw * 4; ++x)
				std::swap(rawImage[y * sw * 4 + x], rawImage[(sh - y - 1) * sw * 4 + x]);
	return rawImage;
}

qboolean RE_InitDissolve(qboolean forceCircular)
{
	dissolve.active = false;
	if (!tr.registered)
		return qfalse;
	std::vector<byte> pixels = ReadScreen();
	if (pixels.empty())
		return qfalse;
	dissolve.width = screenFbo.width;
	dissolve.height = screenFbo.height;
	UpdateImage(dissolve.image, "*spDissolve", pixels.data(), dissolve.width, dissolve.height);
	dissolve.type = forceCircular ? CIRCLE_IN : (Wipe)Q_irand(RIGHT_TO_LEFT, CIRCLE_OUT);
	const char *mask = dissolve.type == CIRCLE_IN ? "gfx/2d/iris_mono_rev" :
		dissolve.type == CIRCLE_OUT ? "gfx/2d/iris_mono" : "textures/common/dissolve";
	dissolve.mask = R_FindImageFile(mask, IMGTYPE_COLORALPHA, IMGFLAG_CLAMPTOEDGE | IMGFLAG_NOLIGHTSCALE);
	dissolve.active = dissolve.mask != nullptr;
	dissolve.touched = false;
	return (qboolean)dissolve.active;
}

void RE_KillDissolve()
{
	dissolve.active = false;
}

qboolean RE_ProcessDissolve()
{
	if (!tr.registered || !dissolve.active)
		return qfalse;
	const int now = ri.Milliseconds();
	if (!dissolve.touched)
	{
		dissolve.startTime = now;
		dissolve.touched = true;
	}
	const float progress = Com_Clamp(0.0f, 1.0f, (now - dissolve.startTime) / 750.0f);
	if (progress >= 1.0f)
	{
		dissolve.active = false;
		return qfalse;
	}
	FlushDrawing();
	FBO_Bind(backEnd.framePostProcessed ? nullptr : tr.renderFbo);
	RB_SetGL2D();
	qglScissor(0, 0, glConfig.vidWidth, glConfig.vidHeight);
	GL_State(GLS_DEPTHMASK_TRUE);
	qglClearDepth(1.0);
	qglClear(GL_DEPTH_BUFFER_BIT);
	const int maskState = GLS_DEPTHMASK_TRUE | GLS_SRCBLEND_ZERO | GLS_DSTBLEND_ONE;
	const float sx = 640.0f / dissolve.width, sy = 480.0f / dissolve.height;
	if (dissolve.type <= BOTTOM_TO_TOP)
	{
		const bool vertical = dissolve.type >= TOP_TO_BOTTOM;
		const bool reverse = dissolve.type == RIGHT_TO_LEFT || dissolve.type == BOTTOM_TO_TOP;
		const float extent = vertical ? 480.0f : 640.0f;
		const float strip = dissolve.mask->width * (vertical ? sy : sx);
		const float boundary = reverse ? extent - (extent + strip) * progress : (extent + 2 * strip) * progress - strip;
		const float start = reverse ? boundary : boundary + strip;
		const float size = reverse ? strip : -strip;
		Blit(dissolve.mask, vertical ? 0 : start, vertical ? start : 0,
			vertical ? 640 : size, vertical ? size : 480, maskState, true, vertical);
		const float solidStart = reverse ? 0 : boundary + strip - 2;
		const float solidEnd = reverse ? boundary + 2 : extent;
		if (solidEnd > solidStart)
			Blit(tr.whiteImage, vertical ? 0 : solidStart, vertical ? solidStart : 0,
				vertical ? 640 : solidEnd - solidStart, vertical ? solidEnd - solidStart : 480, maskState);
	}
	else
	{
		const float radius = dissolve.width * 0.8f * (dissolve.type == CIRCLE_IN ? 1 - progress : progress);
		const float x = 320 - radius * sx, y = 240 - radius * sy;
		Blit(dissolve.mask, x, y, 2 * radius * sx, 2 * radius * sy, maskState, true);
		if (dissolve.type == CIRCLE_OUT)
		{
			Blit(tr.whiteImage, 0, 0, x + 2, 480, maskState);
			Blit(tr.whiteImage, 640 - x - 2, 0, x + 2, 480, maskState);
			Blit(tr.whiteImage, x - 2, 0, 644 - 2 * x, y + 2, maskState);
			Blit(tr.whiteImage, x - 2, 480 - y - 2, 644 - 2 * x, y + 2, maskState);
		}
	}
	Blit(dissolve.image, 0, 0, 640, 480, GLS_DEPTHFUNC_EQUAL, false, false, true);
	GL_State(GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA);
	R_SP_ApplyScissor();
	return qtrue;
}

void RE_LAGoggles()
{
	goggles = true;
}

int R_SP_SceneFlags(int flags)
{
	if (goggles && !(flags & RDF_SKYBOXPORTAL))
	{
		flags |= RDF_doLAGoggles | RDF_doFullbright;
		goggles = false;
	}
	return flags;
}

void R_SP_DrawGoggles()
{
	R_SP_CaptureScreen(qtrue);
	RB_SetGL2D();
	const vec4_t enabled = {1, 0, 0, 0}, disabled = {};
	GLSL_BindProgram(&tr.textureColorShader);
	GLSL_SetUniformVec4(&tr.textureColorShader, UNIFORM_ENABLETEXTURES, enabled);
	Blit(screenImage, 0, 0, 640, 480, GLS_DEPTHTEST_DISABLE, false, false, true);
	GLSL_SetUniformVec4(&tr.textureColorShader, UNIFORM_ENABLETEXTURES, disabled);
}

void R_SP_ApplyScissor()
{
	if (scissorEnabled)
		qglScissor(scissorBox[0], scissorBox[1], scissorBox[2], scissorBox[3]);
}

void R_SP_ResetScissor()
{
	scissorEnabled = false;
}

void RE_Scissor(float x, float y, float w, float h)
{
	if (!tr.registered)
		return;
	FlushDrawing();
	FBO_Bind(backEnd.framePostProcessed ? nullptr : tr.renderFbo);
	RB_SetGL2D();
	scissorEnabled = x >= 0 && w >= 0 && h >= 0;
	scissorBox[0] = (int)x;
	scissorBox[1] = (int)(glConfig.vidHeight - y - h);
	scissorBox[2] = MAX(0, (int)w);
	scissorBox[3] = MAX(0, (int)h);
	if (scissorEnabled)
		R_SP_ApplyScissor();
	else
		qglScissor(0, 0, glConfig.vidWidth, glConfig.vidHeight);
}

void R_SP_ShutdownEffects()
{
	RE_TempRawImage_CleanUp();
	if (screenFbo.frameBuffer)
	{
		if (glState.currentFBO == &screenFbo)
			FBO_Bind(nullptr);
		qglDeleteFramebuffers(1, &screenFbo.frameBuffer);
	}
	screenFbo = {};
	screenImage = nullptr;
	dissolve = Dissolve();
	goggles = scissorEnabled = false;
	tr_distortionAlpha = 1.0f;
	tr_distortionStretch = 0.0f;
	tr_distortionPrePost = tr_distortionNegate = qfalse;
}
