// SPDX-License-Identifier: GPL-2.0-or-later
// Included by each SP renderer's tr_image.cpp after its local image definitions.
#pragma once

static image_t uiTextures[64];

image_t* R_GetUiTexture(qhandle_t handle) {
	return handle > 0 && handle <= int(ARRAY_LEN(uiTextures)) && uiTextures[handle - 1].texnum ? &uiTextures[handle - 1] : nullptr;
}

qhandle_t RE_CreateUiTexture(int width, int height, const byte* rgba) {
	if (!tr.registered || !rgba || width <= 0 || height <= 0 || width > glConfig.maxTextureSize || height > glConfig.maxTextureSize ||
		(width & (width - 1)) || (height & (height - 1))) return 0;
	int slot = 0;
	while (slot < int(ARRAY_LEN(uiTextures)) && uiTextures[slot].texnum) ++slot;
	if (slot == int(ARRAY_LEN(uiTextures))) return 0;
	R_IssuePendingRenderCommands();
	image_t& image = uiTextures[slot];
	image = {};
	image.width = width;
	image.height = height;
	image.internalFormat = GL_RGBA8;
	Q_strncpyz(image.imgName, "<RmlUi texture>", sizeof(image.imgName));
#ifdef REND2_SP
	const GLint wrap = GL_CLAMP_TO_EDGE;
	image.uploadWidth = width;
	image.uploadHeight = height;
	image.type = IMGTYPE_COLORALPHA;
	image.flags = IMGFLAG_CLAMPTOEDGE | IMGFLAG_NO_COMPRESSION | IMGFLAG_NOLIGHTSCALE;
	qglGenTextures(1, &image.texnum);
#else
	const GLint wrap = glConfig.clampToEdgeAvailable ? GL_CLAMP_TO_EDGE : GL_CLAMP;
	// Vanilla reserves names manually; share its counter to prevent collisions.
	image.texnum = 1024 + giTextureBindNum++;
	image.wrapClampMode = wrap;
#endif
	const int previousTmu = glState.currenttmu;
	GL_SelectTexture(0);
	qglBindTexture(GL_TEXTURE_2D, image.texnum);
	GLint alignment, rowLength;
	qglGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
	qglGetIntegerv(GL_UNPACK_ROW_LENGTH, &rowLength);
	qglPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	qglPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	// Font atlases must not pass through picmip, gamma, compression, or mipmap generation.
	qglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
	qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
	qglPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
	qglPixelStorei(GL_UNPACK_ROW_LENGTH, rowLength);
	qglBindTexture(GL_TEXTURE_2D, 0);
	glState.currenttextures[0] = 0;
	GL_SelectTexture(previousTmu);
	return slot + 1;
}

void RE_ReleaseUiTexture(qhandle_t handle) {
	image_t* image = R_GetUiTexture(handle);
	if (!image) return;
	// Backend commands own vertex data but borrow the texture until execution.
	R_IssuePendingRenderCommands();
	const int previousTmu = glState.currenttmu;
	for (int tmu = 0; tmu < int(ARRAY_LEN(glState.currenttextures)); ++tmu) {
		if (glState.currenttextures[tmu] != image->texnum) continue;
		GL_SelectTexture(tmu);
		qglBindTexture(GL_TEXTURE_2D, 0);
		glState.currenttextures[tmu] = 0;
	}
	qglDeleteTextures(1, &image->texnum);
	*image = {};
	GL_SelectTexture(previousTmu);
}

void R_DeleteUiTextures() {
	for (int i = 1; i <= int(ARRAY_LEN(uiTextures)); ++i) RE_ReleaseUiTexture(i);
}
