/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
#include "tr_local.h"
#include "Textures/AreaTex.h"
#include "Textures/SearchTex.h"
#include "tr_allocator.h"
#include "glext.h"
#include <algorithm>

backEndData_t	*backEndData;
backEndState_t	backEnd;

static float	s_flipMatrix[16] = {
	// convert from our coordinate system (looking down X)
	// to OpenGL's coordinate system (looking down -Z)
	0, 0, -1, 0,
	-1, 0, 0, 0,
	0, 1, 0, 0,
	0, 0, 0, 1
};


/*
** GL_Bind
*/
void GL_Bind( image_t *image ) {
	int texnum;

	if ( !image ) {
		ri.Printf( PRINT_WARNING, "GL_Bind: NULL image\n" );
		texnum = tr.defaultImage->texnum;
	} else {
		texnum = image->texnum;
	}

	if ( r_nobind->integer && tr.dlightImage ) {		// performance evaluation option
		texnum = tr.dlightImage->texnum;
	}

	if ( glState.currenttextures[glState.currenttmu] != texnum ) {
		if ( image ) {
			image->frameUsed = tr.frameCount;
		}
		glState.currenttextures[glState.currenttmu] = texnum;
		if (image && image->flags & IMGFLAG_CUBEMAP)
			qglBindTexture( GL_TEXTURE_CUBE_MAP, texnum );
		else if (image->flags & IMGFLAG_3D)
			qglBindTexture(GL_TEXTURE_3D, texnum);
		else if (image->flags & IMGFLAG_2D_ARRAY)
			qglBindTexture(GL_TEXTURE_2D_ARRAY, texnum);
		else
			qglBindTexture( GL_TEXTURE_2D, texnum );
	}
}

/*
** GL_SelectTexture
*/
void GL_SelectTexture( int unit )
{
	if ( glState.currenttmu == unit )
	{
		return;
	}

	if (!(unit >= 0 && unit <= 31))
		ri.Error( ERR_DROP, "GL_SelectTexture: unit = %i", unit );

	qglActiveTexture( GL_TEXTURE0 + unit );

	glState.currenttmu = unit;
}

/*
** GL_BindToTMU
*/
void GL_BindToTMU( image_t *image, int tmu )
{
	int		texnum;
	int     oldtmu = glState.currenttmu;

	if (!image)
		texnum = 0;
	else
		texnum = image->texnum;

	if ( glState.currenttextures[tmu] != texnum ) {
		GL_SelectTexture( tmu );
		if (image)
			image->frameUsed = tr.frameCount;
		glState.currenttextures[tmu] = texnum;

		if (image && (image->flags & IMGFLAG_CUBEMAP))
			qglBindTexture( GL_TEXTURE_CUBE_MAP, texnum );
		else if (image && (image->flags & IMGFLAG_3D))
			qglBindTexture(GL_TEXTURE_3D, texnum);
		else if (image && (image->flags & IMGFLAG_2D_ARRAY))
			qglBindTexture(GL_TEXTURE_2D_ARRAY, texnum);
		else
			qglBindTexture( GL_TEXTURE_2D, texnum );
	}
}

/*
** GL_Cull
*/
void GL_Cull( int cullType ) {
	if ( glState.faceCulling == cullType ) {
		return;
	}

	if ( backEnd.projection2D )
		cullType = CT_TWO_SIDED;

	if ( cullType == CT_TWO_SIDED )
	{
		if ( glState.faceCulling != CT_TWO_SIDED )
			qglDisable( GL_CULL_FACE );
	}
	else
	{
		qboolean cullFront = (qboolean)(cullType == CT_FRONT_SIDED);

		if ( glState.faceCulling == CT_TWO_SIDED )
			qglEnable( GL_CULL_FACE );

		qglCullFace( cullFront ? GL_FRONT : GL_BACK);
	}

	glState.faceCulling = cullType;
}

void GL_DepthRange( float min, float max )
{
	if ( glState.minDepth == min && glState.maxDepth == max )
	{
		return;
	}

	qglDepthRange(min, max);
	glState.minDepth = min;
	glState.maxDepth = max;
}

/*
** GL_State
**
** This routine is responsible for setting the most commonly changed state
** in Q3.
*/
void GL_State( uint32_t stateBits )
{
	uint32_t diff = stateBits ^ glState.glStateBits;

	if ( !diff )
	{
		return;
	}

	//
	// check depthFunc bits
	//
	if ( diff & GLS_DEPTHFUNC_BITS )
	{
		if ( stateBits & GLS_DEPTHFUNC_EQUAL )
		{
			qglDepthFunc( GL_EQUAL );
		}
		else if ( stateBits & GLS_DEPTHFUNC_GREATER )
		{
			qglDepthFunc( GL_GREATER );
		}
		else if ( stateBits & GLS_DEPTHFUNC_LESS )
		{
			qglDepthFunc( GL_LESS );
		}
		else
		{
			qglDepthFunc( GL_LEQUAL );
		}
	}

	//
	// check blend bits
	//
	if ( diff & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) )
	{
		GLenum srcFactor = GL_ONE, dstFactor = GL_ONE;

		if ( stateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) )
		{
			switch ( stateBits & GLS_SRCBLEND_BITS )
			{
			case GLS_SRCBLEND_ZERO:
				srcFactor = GL_ZERO;
				break;
			case GLS_SRCBLEND_ONE:
				srcFactor = GL_ONE;
				break;
			case GLS_SRCBLEND_DST_COLOR:
				srcFactor = GL_DST_COLOR;
				break;
			case GLS_SRCBLEND_ONE_MINUS_DST_COLOR:
				srcFactor = GL_ONE_MINUS_DST_COLOR;
				break;
			case GLS_SRCBLEND_SRC_ALPHA:
				srcFactor = GL_SRC_ALPHA;
				break;
			case GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA:
				srcFactor = GL_ONE_MINUS_SRC_ALPHA;
				break;
			case GLS_SRCBLEND_DST_ALPHA:
				srcFactor = GL_DST_ALPHA;
				break;
			case GLS_SRCBLEND_ONE_MINUS_DST_ALPHA:
				srcFactor = GL_ONE_MINUS_DST_ALPHA;
				break;
			case GLS_SRCBLEND_ALPHA_SATURATE:
				srcFactor = GL_SRC_ALPHA_SATURATE;
				break;
			default:
				ri.Error( ERR_DROP, "GL_State: invalid src blend state bits" );
				break;
			}

			switch ( stateBits & GLS_DSTBLEND_BITS )
			{
			case GLS_DSTBLEND_ZERO:
				dstFactor = GL_ZERO;
				break;
			case GLS_DSTBLEND_ONE:
				dstFactor = GL_ONE;
				break;
			case GLS_DSTBLEND_SRC_COLOR:
				dstFactor = GL_SRC_COLOR;
				break;
			case GLS_DSTBLEND_ONE_MINUS_SRC_COLOR:
				dstFactor = GL_ONE_MINUS_SRC_COLOR;
				break;
			case GLS_DSTBLEND_SRC_ALPHA:
				dstFactor = GL_SRC_ALPHA;
				break;
			case GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA:
				dstFactor = GL_ONE_MINUS_SRC_ALPHA;
				break;
			case GLS_DSTBLEND_DST_ALPHA:
				dstFactor = GL_DST_ALPHA;
				break;
			case GLS_DSTBLEND_ONE_MINUS_DST_ALPHA:
				dstFactor = GL_ONE_MINUS_DST_ALPHA;
				break;
			default:
				ri.Error( ERR_DROP, "GL_State: invalid dst blend state bits" );
				break;
			}

			qglEnable( GL_BLEND );
			qglBlendFunc( srcFactor, dstFactor );
		}
		else
		{
			qglDisable( GL_BLEND );
		}
	}

	//
	// check colormask
	//
	if ( diff & GLS_COLORMASK_BITS )
	{
		if ( stateBits & GLS_COLORMASK_BITS )
		{
			qglColorMask( GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );
		}
		else
		{
			qglColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
		}
	}

	//
	// check stenciltest
	//
	if (diff & GLS_STENCILTEST_ENABLE)
	{
		if (stateBits & GLS_STENCILTEST_ENABLE)
		{
			qglEnable(GL_STENCIL_TEST);
		}
		else
		{
			qglDisable(GL_STENCIL_TEST);
		}
	}

	//
	// check depthmask
	//
	if ( diff & GLS_DEPTHMASK_TRUE )
	{
		if ( stateBits & GLS_DEPTHMASK_TRUE )
		{
			qglDepthMask( GL_TRUE );
		}
		else
		{
			qglDepthMask( GL_FALSE );
		}
	}

	//
	// fill/line mode
	//
	if ( diff & GLS_POLYMODE_LINE )
	{
		if ( stateBits & GLS_POLYMODE_LINE )
		{
			qglPolygonMode( GL_FRONT_AND_BACK, GL_LINE );
		}
		else
		{
			qglPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
		}
	}

	//
	// depthtest
	//
	if ( diff & GLS_DEPTHTEST_DISABLE )
	{
		if ( stateBits & GLS_DEPTHTEST_DISABLE )
		{
			qglDisable( GL_DEPTH_TEST );
		}
		else
		{
			qglEnable( GL_DEPTH_TEST );
		}
	}

	if ( diff & GLS_POLYGON_OFFSET_FILL )
	{
		if ( stateBits & GLS_POLYGON_OFFSET_FILL )
		{
			qglEnable( GL_POLYGON_OFFSET_FILL );
		}
		else
		{
			qglDisable( GL_POLYGON_OFFSET_FILL );
		}
	}

	glState.glStateBits = stateBits;
}

void GL_VertexAttribPointers(
		size_t numAttributes,
		vertexAttribute_t *attributes )
{
	assert(attributes != nullptr || numAttributes == 0);

	uint32_t newAttribs = 0;
	for ( int i = 0; i < numAttributes; i++ )
	{
		vertexAttribute_t& attrib = attributes[i];
		vertexAttribute_t& currentAttrib = glState.currentVaoAttribs[attrib.index];

		newAttribs |= (1 << attrib.index);
		if (memcmp(&currentAttrib, &attrib, sizeof(currentAttrib)) == 0)
		{
			// No change
			continue;
		}

		R_BindVBO(attrib.vbo);
		if ( attrib.integerAttribute )
		{
			qglVertexAttribIPointer(attrib.index,
				attrib.numComponents,
				attrib.type,
				attrib.stride,
				BUFFER_OFFSET(attrib.offset));
		}
		else
		{
			qglVertexAttribPointer(attrib.index,
				attrib.numComponents,
				attrib.type,
				attrib.normalize,
				attrib.stride,
				BUFFER_OFFSET(attrib.offset));
		}

		if (currentAttrib.stepRate != attrib.stepRate)
			qglVertexAttribDivisor(attrib.index, attrib.stepRate);

		currentAttrib = attrib;
	}

	uint32_t diff = newAttribs ^ glState.vertexAttribsState;
	if ( diff )
	{
		for ( int i = 0, j = 1; i < ATTR_INDEX_MAX; i++, j <<= 1 )
		{
			// FIXME: Use BitScanForward?
			if (diff & j)
			{
				if(newAttribs & j)
					qglEnableVertexAttribArray(i);
				else
					qglDisableVertexAttribArray(i);
			}
		}

		glState.vertexAttribsState = newAttribs;
	}
}

void GL_DrawIndexed(
		GLenum primitiveType,
		int numIndices,
		GLenum indexType,
		int offset,
		int numInstances,
		int baseVertex)
{
	assert(numInstances > 0);
	qglDrawElementsInstancedBaseVertex(
			primitiveType,
			numIndices,
			indexType,
			BUFFER_OFFSET(offset),
			numInstances,
			baseVertex);
}

void GL_MultiDrawIndexed(
		GLenum primitiveType,
		int *numIndices,
		glIndex_t **offsets,
		int numDraws)
{
	assert(numDraws > 0);
	qglMultiDrawElements(
			primitiveType,
			numIndices,
			GL_INDEX_TYPE,
			(const GLvoid **)offsets,
			numDraws);
}

void GL_Draw( GLenum primitiveType, int firstVertex, int numVertices, int numInstances )
{
	assert(numInstances > 0);
	qglDrawArraysInstanced(primitiveType, firstVertex, numVertices, numInstances);
}

void GL_SetProjectionMatrix(matrix_t matrix)
{
	Matrix16Copy(matrix, glState.projection);
	Matrix16Multiply(glState.projection, glState.modelview, glState.modelviewProjection);
}


void GL_SetModelviewMatrix(matrix_t matrix)
{
	Matrix16Copy(matrix, glState.modelview);
	Matrix16Multiply(glState.projection, glState.modelview, glState.modelviewProjection);
}


/*
================
RB_Hyperspace

A player has predicted a teleport, but hasn't arrived yet
================
*/
static void RB_Hyperspace( void ) {
	float c = ( backEnd.refdef.time & 255 ) / 255.0f;
	vec4_t v = { c, c, c, 1.0f };
	qglClearBufferfv( GL_COLOR, 0, v );
}


static void SetViewportAndScissor( void ) {
	GL_SetProjectionMatrix( backEnd.viewParms.projectionMatrix );

	// set the window clipping
	qglViewport( backEnd.viewParms.viewportX, backEnd.viewParms.viewportY,
		backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight );

	if ( !backEnd.viewParms.scissorX && !backEnd.viewParms.scissorY &&
			!backEnd.viewParms.scissorWidth && !backEnd.viewParms.scissorHeight )
	{
		qglScissor( backEnd.viewParms.viewportX, backEnd.viewParms.viewportY,
			backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight );
	}
	else
	{
		qglScissor( backEnd.viewParms.scissorX, backEnd.viewParms.scissorY,
			backEnd.viewParms.scissorWidth, backEnd.viewParms.scissorHeight );
	}
}

/*
=================
RB_BeginDrawingView

Any mirrored or portaled views have already been drawn, so prepare
to actually render the visible surfaces for this view
=================
*/
void RB_BeginDrawingView (void) {
	int clearBits = 0;

	// we will need to change the projection matrix before drawing
	// 2D images again
	backEnd.projection2D = qfalse;

	// FIXME: HUGE HACK: render to the screen fbo if we've already postprocessed the frame and aren't drawing more world
	// drawing more world check is in case of double renders, such as skyportals
	if (backEnd.viewParms.targetFbo == NULL)
	{
		if (!tr.renderFbo || (backEnd.framePostProcessed && (backEnd.refdef.rdflags & RDF_NOWORLDMODEL)))
		{
			FBO_Bind(NULL);
		}
		else
		{
			FBO_Bind(tr.renderFbo);
		}
	}
	else
	{
		FBO_Bind(backEnd.viewParms.targetFbo);
	}

	//
	// set the modelview matrix for the viewer
	//
	SetViewportAndScissor();

	// ensures that depth writes are enabled for the depth clear
	GL_State( GLS_DEFAULT );
	// clear relevant buffers
	clearBits = GL_DEPTH_BUFFER_BIT;

	if ( r_clear->integer )
	{
		clearBits |= GL_COLOR_BUFFER_BIT;
	}

	if ( r_measureOverdraw->integer || r_shadows->integer == 2 )
	{
		clearBits |= GL_STENCIL_BUFFER_BIT;
	}

	if ( r_fastsky->integer && !( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) )
	{
		clearBits |= GL_COLOR_BUFFER_BIT;	// FIXME: only if sky shaders have been used
#ifdef _DEBUG
		qglClearColor( 0.8f, 0.7f, 0.4f, 1.0f );	// FIXME: get color of sky
#else
		qglClearColor( 0.0f, 0.0f, 0.0f, 1.0f );	// FIXME: get color of sky
#endif
	}

	if (
#ifndef REND2_SP
		tr.refdef.rdflags & RDF_AUTOMAP ||
#endif
		!(backEnd.refdef.rdflags & RDF_NOWORLDMODEL))
	{
		if (tr.world && tr.world->globalFog)
		{
			const fog_t		*fog = tr.world->globalFog;

			clearBits |= GL_COLOR_BUFFER_BIT;
			qglClearColor(fog->parms.color[0], fog->parms.color[1], fog->parms.color[2], 1.0f);
		}
	}

	// dont clear color if we have a skyportal and it has been rendered
	if (tr.world && tr.world->skyboxportal == 1 && !(backEnd.viewParms.isSkyPortal))
		clearBits &= ~GL_COLOR_BUFFER_BIT;

	if (clearBits > 0 && !(backEnd.viewParms.flags & VPF_NOCLEAR))
		qglClear( clearBits );

	if (backEnd.viewParms.targetFbo == NULL)
	{
		// Clear the glow target
		float black[] = {0.0f, 0.0f, 0.0f, 1.0f};
		qglClearBufferfv (GL_COLOR, 1, black);
	}

	if ( ( backEnd.refdef.rdflags & RDF_HYPERSPACE ) )
	{
		RB_Hyperspace();
		return;
	}

	// we will only draw a sun if there was sky rendered in this view
	backEnd.skyRenderedThisView = qfalse;

	// clip to the plane of the portal
	if ( backEnd.viewParms.isPortal ) {
#if 0
		float	plane[4];
		double	plane2[4];

		plane[0] = backEnd.viewParms.portalPlane.normal[0];
		plane[1] = backEnd.viewParms.portalPlane.normal[1];
		plane[2] = backEnd.viewParms.portalPlane.normal[2];
		plane[3] = backEnd.viewParms.portalPlane.dist;

		plane2[0] = DotProduct (backEnd.viewParms.ori.axis[0], plane);
		plane2[1] = DotProduct (backEnd.viewParms.ori.axis[1], plane);
		plane2[2] = DotProduct (backEnd.viewParms.ori.axis[2], plane);
		plane2[3] = DotProduct (plane, backEnd.viewParms.ori.origin) - plane[3];
#endif
		GL_SetModelviewMatrix( s_flipMatrix );
	}

	if (backEnd.viewParms.flags & VPF_POINTSHADOW)
		qglPolygonOffset(r_shadowOffsetFactor->value, r_shadowOffsetUnits->value);
	else
		qglPolygonOffset(r_offsetFactor->value, r_offsetUnits->value);
}


#define	MAC_EVENT_PUMP_MSEC		5

void DrawItemSetVertexAttributes(
	DrawItem& drawItem,
	const vertexAttribute_t *attributes,
	uint32_t count,
	Allocator& allocator)
{
	drawItem.numAttributes = count;
	drawItem.attributes = ojkAllocArray<vertexAttribute_t>(allocator, count);
	memcpy(
		drawItem.attributes, attributes, sizeof(*drawItem.attributes) * count);
}

void DrawItemSetSamplerBindings(
	DrawItem& drawItem,
	const SamplerBinding *bindings,
	uint32_t count,
	Allocator& allocator)
{
	drawItem.numSamplerBindings = count;
	drawItem.samplerBindings = ojkAllocArray<SamplerBinding>(allocator, count);
	memcpy(
		drawItem.samplerBindings,
		bindings,
		sizeof(*drawItem.samplerBindings) * count);
}

void DrawItemSetUniformBlockBindings(
	DrawItem& drawItem,
	const UniformBlockBinding *bindings,
	uint32_t count,
	Allocator& allocator)
{
	drawItem.numUniformBlockBindings = count;
	drawItem.uniformBlockBindings = ojkAllocArray<UniformBlockBinding>(
		allocator, count);
	memcpy(
		drawItem.uniformBlockBindings,
		bindings,
		sizeof(*drawItem.uniformBlockBindings) * count);
}

UniformDataWriter::UniformDataWriter()
	: failed(false)
	, shaderProgram(nullptr)
	, scratch(scratchBuffer, sizeof(scratchBuffer), 1)
{
}

void UniformDataWriter::Start( shaderProgram_t *sp )
{
	shaderProgram = sp;
}

UniformDataWriter& UniformDataWriter::SetUniformInt( uniform_t uniform, int value )
{
	if ( shaderProgram->uniforms[uniform] == -1 )
		return *this;

	void *memory = scratch.Alloc(sizeof(UniformData) + sizeof(int));
	if ( !memory )
	{
		failed = true;
		return *this;
	}

	UniformData *header = static_cast<UniformData *>(memory);
	header->index = uniform;
	header->numElements = 1;

	int *data = reinterpret_cast<int *>(header + 1);
	*data = value;

	return *this;
}

UniformDataWriter& UniformDataWriter::SetUniformFloat( uniform_t uniform, float value )
{
	return SetUniformFloat(uniform, &value, 1);
}

UniformDataWriter& UniformDataWriter::SetUniformFloat( uniform_t uniform, float *values, size_t count )
{
	if ( shaderProgram->uniforms[uniform] == -1 )
		return *this;

	void *memory = scratch.Alloc(sizeof(UniformData) + sizeof(float)*count);
	if ( !memory )
	{
		failed = true;
		return *this;
	}

	UniformData *header = static_cast<UniformData *>(memory);
	header->index = uniform;
	header->numElements = count;
	memcpy(header + 1, values, sizeof(float) * count);

	return *this;
}

UniformDataWriter& UniformDataWriter::SetUniformVec2( uniform_t uniform, float x, float y )
{
	vec2_t values = {x, y};
	return SetUniformVec2(uniform, values);
}

UniformDataWriter& UniformDataWriter::SetUniformVec2( uniform_t uniform, const float *values, size_t count )
{
	if ( shaderProgram->uniforms[uniform] == -1 )
		return *this;

	void *memory = scratch.Alloc(sizeof(UniformData) + sizeof(vec2_t)*count);
	if ( !memory )
	{
		failed = true;
		return *this;
	}

	UniformData *header = static_cast<UniformData *>(memory);
	header->index = uniform;
	header->numElements = count;
	memcpy(header + 1, values, sizeof(vec2_t) * count);

	return *this;
}

UniformDataWriter& UniformDataWriter::SetUniformVec3( uniform_t uniform, float x, float y, float z )
{
	vec3_t values = {x, y, z};
	return SetUniformVec3(uniform, values);
}

UniformDataWriter& UniformDataWriter::SetUniformVec3( uniform_t uniform, const float *values, size_t count )
{
	if ( shaderProgram->uniforms[uniform] == -1 )
		return *this;

	void *memory = scratch.Alloc(sizeof(UniformData) + sizeof(vec3_t)*count);
	if ( !memory )
	{
		failed = true;
		return *this;
	}

	UniformData *header = static_cast<UniformData *>(memory);
	header->index = uniform;
	header->numElements = count;
	memcpy(header + 1, values, sizeof(vec3_t) * count);

	return *this;
}

UniformDataWriter& UniformDataWriter::SetUniformVec4( uniform_t uniform, float x, float y, float z, float w )
{
	vec4_t values = {x, y, z, w};
	return SetUniformVec4(uniform, values);
}

UniformDataWriter& UniformDataWriter::SetUniformVec4( uniform_t uniform, const float *values, size_t count )
{
	if ( shaderProgram->uniforms[uniform] == -1 )
		return *this;

	void *memory = scratch.Alloc(sizeof(UniformData) + sizeof(vec4_t)*count);
	if ( !memory )
	{
		failed = true;
		return *this;
	}

	UniformData *header = static_cast<UniformData *>(memory);
	header->index = uniform;
	header->numElements = count;
	memcpy(header + 1, values, sizeof(vec4_t) * count);

	return *this;
}

UniformDataWriter& UniformDataWriter::SetUniformMatrix4x3( uniform_t uniform, const float *matrix, size_t count )
{
	if ( shaderProgram->uniforms[uniform] == -1 )
		return *this;

	void *memory = scratch.Alloc(sizeof(UniformData) + sizeof(float)*12*count);
	if ( !memory )
	{
		failed = true;
		return *this;
	}

	UniformData *header = static_cast<UniformData *>(memory);
	header->index = uniform;
	header->numElements = count;
	memcpy(header + 1, matrix, sizeof(float) * 12 * count);

	return *this;
}

UniformDataWriter& UniformDataWriter::SetUniformMatrix4x4( uniform_t uniform, const float *matrix, size_t count )
{
	if ( shaderProgram->uniforms[uniform] == -1 )
		return *this;

	void *memory = scratch.Alloc(sizeof(UniformData) + sizeof(float)*16*count);
	if ( !memory )
	{
		failed = true;
		return *this;
	}

	UniformData *header = static_cast<UniformData *>(memory);
	header->index = uniform;
	header->numElements = count;
	memcpy(header + 1, matrix, sizeof(float) * 16 * count);

	return *this;
}

UniformData *UniformDataWriter::Finish( Allocator& destHeap )
{
	UniformData *endSentinel = ojkAlloc<UniformData>(scratch);
	if ( failed || !endSentinel )
	{
		return nullptr;
	}

	endSentinel->index = UNIFORM_COUNT;

	int uniformDataSize = (char *)scratch.Mark() - (char *)scratch.Base();

	// Copy scratch buffer to per-frame heap
	void *finalMemory = destHeap.Alloc(uniformDataSize);
	UniformData *result = static_cast<UniformData *>(finalMemory);
	memcpy(finalMemory, scratch.Base(), uniformDataSize);
	scratch.Reset();

	failed = false;
	shaderProgram = nullptr;

	return result;
}

SamplerBindingsWriter::SamplerBindingsWriter()
	: failed(false)
	, count(0)
{
}

SamplerBindingsWriter& SamplerBindingsWriter::AddStaticImage( image_t *image, int unit )
{
	SamplerBinding *binding = &scratch[count];
	if ( !binding )
	{
		failed = true;
		return *this;
	}

	binding->image = image;
	binding->slot = unit;
	binding->videoMapHandle = NULL_HANDLE;
	++count;

	return *this;
}

SamplerBindingsWriter& SamplerBindingsWriter::AddAnimatedImage( textureBundle_t *bundle, int unit )
{
	int index;
#ifdef REND2_SP
	if (bundle->isLightmap && (backEnd.refdef.rdflags & RDF_doFullbright))
		return AddStaticImage(tr.whiteImage, unit);
#endif

	if ( bundle->isVideoMap )
	{
		SamplerBinding *binding = &scratch[count];
		if ( !binding )
		{
			failed = true;
			return *this;
		}

		binding->image = nullptr;
		binding->slot = unit;
		binding->videoMapHandle = bundle->videoMapHandle + 1;
		++count;

		return *this;
	}

	if ( bundle->numImageAnimations <= 1 )
	{
		return AddStaticImage(bundle->image[0], unit);
	}

	if (backEnd.currentEntity->e.renderfx & RF_SETANIMINDEX )
	{
		index = backEnd.currentEntity->e.skinNum;
	}
	else
	{
		// it is necessary to do this messy calc to make sure animations line up
		// exactly with waveforms of the same frequency
		index = Q_ftol( tess.shaderTime * bundle->imageAnimationSpeed * FUNCTABLE_SIZE );
		index = Q_max(0, index >> FUNCTABLE_SIZE2);
	}

	if ( bundle->oneShotAnimMap )
	{
		index = Q_min(index, bundle->numImageAnimations - 1);
	}
	else
	{
		// loop
		index %= bundle->numImageAnimations;
	}

	return AddStaticImage(bundle->image[ index ], unit);
}

SamplerBinding *SamplerBindingsWriter::Finish( Allocator& destHeap, uint32_t* numBindings )
{
	if ( failed )
	{
		return nullptr;
	}

	SamplerBinding *result = ojkAllocArray<SamplerBinding>(destHeap, count);

	if ( numBindings )
	{
		*numBindings = count;
	}

	memcpy(result, scratch, sizeof(SamplerBinding)*count);
	failed = false;
	count = 0;
	return result;
}

struct Pass
{
	int maxDrawItems;
	int numDrawItems;
	DrawItem *drawItems;
	uint32_t *sortKeys;
};

static void RB_BindTextures( size_t numBindings, const SamplerBinding *bindings )
{
	for ( size_t i = 0; i < numBindings; ++i )
	{
		const SamplerBinding& binding = bindings[i];
		if ( binding.videoMapHandle )
		{
			int oldtmu = glState.currenttmu;
			GL_SelectTexture(binding.slot);
			ri.CIN_RunCinematic(binding.videoMapHandle - 1);
			ri.CIN_UploadCinematic(binding.videoMapHandle - 1);
			GL_SelectTexture(oldtmu);
		}
		else
		{
			GL_BindToTMU(binding.image, binding.slot);
		}
	}
}

static void RB_BindUniformBlocks(
	size_t numBindings,
	const UniformBlockBinding *bindings)
{
	for (size_t i = 0; i < numBindings; ++i)
	{
		const UniformBlockBinding& binding = bindings[i];
		if (binding.offset < 0)
			RB_BindUniformBlock(binding.ubo, binding.block, 0);
		else
			RB_BindUniformBlock(binding.ubo, binding.block, binding.offset);
	}
}

static void RB_SetRenderState(const RenderState& renderState)
{
	GL_Cull(renderState.cullType);
	GL_State(renderState.stateBits);
	GL_DepthRange(
		renderState.depthRange.minDepth,
		renderState.depthRange.maxDepth);

	if (renderState.transformFeedback)
	{
		qglEnable(GL_RASTERIZER_DISCARD);
		qglBeginTransformFeedback(GL_POINTS);
	}
}

static void RB_BindTransformFeedbackBuffer(const bufferBinding_t& binding)
{
	if (memcmp(&glState.currentXFBBO, &binding, sizeof(binding)) != 0)
	{
		if (binding.buffer != 0)
			qglBindBufferRange(
				GL_TRANSFORM_FEEDBACK_BUFFER,
				0,
				binding.buffer,
				binding.offset,
				binding.size);
		else
			qglBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, 0);

		glState.currentXFBBO = binding;
	}
}

static void RB_DrawItems(
	int numDrawItems,
	const DrawItem *drawItems,
	uint32_t *drawOrder)
{
	for ( int i = 0; i < numDrawItems; ++i )
	{
		const DrawItem& drawItem = drawItems[drawOrder[i]];

		if (drawItem.ibo != nullptr)
			R_BindIBO(drawItem.ibo);

		GLSL_BindProgram(drawItem.program);

		GL_VertexAttribPointers(drawItem.numAttributes, drawItem.attributes);
		RB_BindTextures(drawItem.numSamplerBindings, drawItem.samplerBindings);
		RB_BindUniformBlocks(
			drawItem.numUniformBlockBindings,
			drawItem.uniformBlockBindings);
		RB_BindTransformFeedbackBuffer(drawItem.transformFeedbackBuffer);

		GLSL_SetUniforms(drawItem.program, drawItem.uniformData);

		RB_SetRenderState(drawItem.renderState);
		if (drawItem.renderState.clampCubeFaces)
			qglDisable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

		switch ( drawItem.draw.type )
		{
			case DRAW_COMMAND_MULTI_INDEXED:
			{
				GL_MultiDrawIndexed(drawItem.draw.primitiveType,
					drawItem.draw.params.multiIndexed.numIndices,
					drawItem.draw.params.multiIndexed.firstIndices,
					drawItem.draw.params.multiIndexed.numDraws);
				break;
			}

			case DRAW_COMMAND_INDEXED:
			{
				GL_DrawIndexed(drawItem.draw.primitiveType,
					drawItem.draw.params.indexed.numIndices,
					drawItem.draw.params.indexed.indexType,
					drawItem.draw.params.indexed.firstIndex,
					drawItem.draw.numInstances,
					drawItem.draw.params.indexed.baseVertex);
				break;
			}

			case DRAW_COMMAND_ARRAYS:
			{
				GL_Draw(
					drawItem.draw.primitiveType,
					drawItem.draw.params.arrays.firstVertex,
					drawItem.draw.params.arrays.numVertices,
					drawItem.draw.numInstances);
				break;
			}

			default:
			{
				assert(!"Invalid or unhandled draw type");
				break;
			}
		}

		if (drawItem.renderState.transformFeedback)
		{
			qglEndTransformFeedback();
			qglDisable(GL_RASTERIZER_DISCARD);
		}
		if (drawItem.renderState.clampCubeFaces)
			qglEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
	}
}

void RB_AddDrawItem( Pass *pass, uint32_t sortKey, const DrawItem& drawItem )
{
	// There will be no pass if we are drawing a 2D object.
	if ( pass )
	{
		if ( pass->numDrawItems >= pass->maxDrawItems )
		{
			assert(!"Ran out of space for pass");
			return;
		}

		pass->sortKeys[pass->numDrawItems] = sortKey;
		pass->drawItems[pass->numDrawItems++] = drawItem;
	}
	else
	{
		uint32_t drawOrder[] = {0};
		RB_DrawItems(1, &drawItem, drawOrder);
	}
}

static Pass *RB_CreatePass( Allocator& allocator, int capacity )
{
	Pass *pass = ojkAlloc<Pass>(*backEndData->perFrameMemory);
	*pass = {};
	pass->maxDrawItems = capacity;
	pass->drawItems = ojkAllocArray<DrawItem>(allocator, pass->maxDrawItems);
	pass->sortKeys = ojkAllocArray<uint32_t>(allocator, pass->maxDrawItems);
	return pass;
}

static void RB_PrepareForEntity( int entityNum, float originalTime )
{
	if ( entityNum != REFENTITYNUM_WORLD )
	{
		backEnd.currentEntity = &backEnd.refdef.entities[entityNum];

		backEnd.refdef.floatTime = originalTime - backEnd.currentEntity->e.shaderTime;
	}
	else
	{
		backEnd.currentEntity = &tr.worldEntity;

		backEnd.refdef.floatTime = originalTime;
	}

#ifdef REND2_SP
	// CPU deforms need the current entity's axes, origin and local view origin.
	R_RotateForEntity(backEnd.currentEntity, &backEnd.viewParms, &backEnd.ori);
#endif

	// we have to reset the shaderTime as well otherwise image animations on
	// the world (like water) continue with the wrong frame
	tess.shaderTime = backEnd.refdef.floatTime - tess.shader->timeOffset;
}

static void RB_SubmitDrawSurfsForDepthFill(
	drawSurf_t *drawSurfs,
	int numDrawSurfs,
	float originalTime )
{
	shader_t *oldShader = nullptr;
	int oldEntityNum = -1;
	int oldDepthRange = 0;
#ifndef REND2_SP
	CBoneCache *oldBoneCache = nullptr;
#endif

	drawSurf_t *drawSurf = drawSurfs;
	for ( int i = 0; i < numDrawSurfs; i++, drawSurf++ )
	{
		shader_t *shader;
		int cubemapIndex;
		int postRender;
		int entityNum;

		R_DecomposeSort(drawSurf->sort, &entityNum, &shader, &cubemapIndex, &postRender);
		assert(shader != nullptr);
		if (backEnd.ssaoDepthLayer)
		{
			const bool weapon = entityNum != REFENTITYNUM_WORLD &&
				R_IsViewModel(backEnd.refdef.entities[entityNum].e);
			if (weapon != (backEnd.ssaoDepthLayer != backEndState_t::DEPTH_WORLD))
				continue;
		}

#ifdef REND2_SP
		if (postRender)
			continue;
#endif
		if (shader->useSimpleDepthShader == qtrue)
			shader = tr.defaultShader;

		if (shader->sort != SS_OPAQUE || shader->useDistortion)
		{
			// Don't draw yet, let's see what's to come
			continue;
		}

#ifndef REND2_SP
		if (*drawSurf->surface == SF_MDX)
		{
			if (((CRenderableSurface*)drawSurf->surface)->boneCache != oldBoneCache)
			{
				RB_EndSurface();
				RB_BeginSurface(shader, 0, 0);
				oldBoneCache = ((CRenderableSurface*)drawSurf->surface)->boneCache;
				tr.animationBoneUboOffset = RB_GetBoneUboOffset((CRenderableSurface*)drawSurf->surface);
			}
		}
#endif

		if ( shader == oldShader &&	entityNum == oldEntityNum )
		{
			// fast path, same as previous sort
			rb_surfaceTable[*drawSurf->surface](drawSurf->surface);
			continue;
		}

		// change the tess parameters if needed
		// a "entityMergable" shader is a shader that can have surfaces from
		// seperate entities merged into a single batch, like smoke and blood
		// puff sprites
		if ( shader != oldShader ||
				(entityNum != oldEntityNum && !shader->entityMergable) )
		{
			if ( oldShader != nullptr )
			{
				RB_EndSurface();
			}

			RB_BeginSurface(shader, 0, 0);
			backEnd.pc.c_surfBatches++;
			oldShader = shader;
		}

		// change the modelview matrix if needed
		if ( entityNum != oldEntityNum )
		{
			RB_PrepareForEntity(entityNum, originalTime);
			oldEntityNum = entityNum;
		}

		// add the triangles for this surface
		assert(drawSurf->surface != nullptr);
		rb_surfaceTable[*drawSurf->surface](drawSurf->surface);
	}

	// draw the contents of the last shader batch
	if ( oldShader != nullptr )
	{
		RB_EndSurface();
	}
}

static void RB_SubmitDrawSurfs(
	drawSurf_t *drawSurfs,
	int numDrawSurfs,
	float originalTime )
{
	shader_t *oldShader = nullptr;
	int oldEntityNum = -1;
	int oldFogNum = -1;
	int oldDepthRange = 0;
	int oldDlighted = 0;
	int oldPostRender = 0;
	int oldCubemapIndex = -1;
	bool oldSoftParticle = false;
	const float *oldGlassPlane = nullptr;
#ifndef REND2_SP
	CBoneCache *oldBoneCache = nullptr;
#endif

	drawSurf_t *drawSurf = drawSurfs;
	for ( int i = 0; i < numDrawSurfs; i++, drawSurf++ )
	{
		shader_t *shader;
		int cubemapIndex;
		int postRender;
		int entityNum;
		int fogNum;
		int dlighted;

		R_DecomposeSort(drawSurf->sort, &entityNum, &shader, &cubemapIndex, &postRender);
		assert(shader != nullptr);
		const bool softParticle = r_softParticles->integer && entityNum != REFENTITYNUM_WORLD &&
			R_IsSoftParticle(backEnd.refdef.entities[entityNum].e);
		fogNum = drawSurf->fogIndex;
		dlighted = drawSurf->dlightBits;
		const float *glassPlane = nullptr;
		if (shader->windowGlass && (*drawSurf->surface == SF_FACE || *drawSurf->surface == SF_TRIANGLES ||
			*drawSurf->surface == SF_GRID || *drawSurf->surface == SF_VBO_MESH))
		{
			const auto *mesh = (const srfBspSurface_t *)drawSurf->surface;
			if (VectorLengthSquared(mesh->glassPlane) > 0.0f) glassPlane = mesh->glassPlane;
		}
		if (backEnd.sssFill && (fogNum || !R_IsSkinSurface(shader, drawSurf->surface))) continue;
#ifdef REND2_SP
		if (backEnd.sssFill && entityNum != REFENTITYNUM_WORLD &&
			(backEnd.refdef.entities[entityNum].e.renderfx & (RF_FORCE_ENT_ALPHA | RF_ALPHA_FADE | RF_DISINTEGRATE1 | RF_DISINTEGRATE2))) continue;
		const bool refractive = shader->useDistortion || (entityNum != REFENTITYNUM_WORLD &&
			(backEnd.refdef.entities[entityNum].e.renderfx & RF_DISTORTION));
		if ((backEnd.refractionFill != qfalse) != refractive)
			continue;
#endif

#ifndef REND2_SP
		if (*drawSurf->surface == SF_MDX)
		{
			if (((CRenderableSurface*)drawSurf->surface)->boneCache != oldBoneCache)
			{
				RB_EndSurface();
				RB_BeginSurface(shader, fogNum, cubemapIndex);
				oldBoneCache = ((CRenderableSurface*)drawSurf->surface)->boneCache;
				tr.animationBoneUboOffset = RB_GetBoneUboOffset((CRenderableSurface*)drawSurf->surface);
			}
		}
#endif

		if (    shader == oldShader &&
				fogNum == oldFogNum &&
				postRender == oldPostRender &&
				cubemapIndex == oldCubemapIndex &&
				glassPlane == oldGlassPlane &&
				entityNum == oldEntityNum &&
				dlighted == oldDlighted &&
				backEnd.refractionFill == shader->useDistortion )
		{
			// fast path, same as previous sort
			rb_surfaceTable[*drawSurf->surface](drawSurf->surface);
			continue;
		}

		//
		// change the tess parameters if needed
		// a "entityMergable" shader is a shader that can have surfaces from seperate
		// entities merged into a single batch, like smoke and blood puff sprites
		if ( (shader != oldShader || softParticle != oldSoftParticle ||
				fogNum != oldFogNum ||
				dlighted != oldDlighted ||
				postRender != oldPostRender ||
				cubemapIndex != oldCubemapIndex ||
				glassPlane != oldGlassPlane ||
				(entityNum != oldEntityNum && (!shader->entityMergable || glassPlane))) )
		{
			if ( oldShader != nullptr )
			{
				RB_EndSurface();
			}

			RB_BeginSurface(shader, fogNum, cubemapIndex, glassPlane);
			backEnd.pc.c_surfBatches++;
			oldShader = shader;
			oldFogNum = fogNum;
			oldDlighted = dlighted;
			oldPostRender = postRender;
			oldCubemapIndex = cubemapIndex;
			oldSoftParticle = softParticle;
			oldGlassPlane = glassPlane;
		}

		if ( entityNum != oldEntityNum )
		{
			RB_PrepareForEntity(entityNum, originalTime);
			oldEntityNum = entityNum;
		}

		qboolean isDistortionShader = (qboolean)
			((shader->useDistortion == qtrue) || (backEnd.currentEntity && backEnd.currentEntity->e.renderfx & RF_DISTORTION));

		if (backEnd.refractionFill != isDistortionShader)
			continue;

		// ugly hack for now...
		// find better way to pass dlightbits
		tess.dlightBits = drawSurf->dlightBits;

		// add the triangles for this surface
		rb_surfaceTable[*drawSurf->surface](drawSurf->surface);
	}

	// draw the contents of the last shader batch
	if ( oldShader != nullptr )
	{
		RB_EndSurface();
	}
}

static void RB_SubmitRenderPass(
	Pass& renderPass,
	Allocator& allocator )
{
	uint32_t *drawOrder = ojkAllocArray<uint32_t>(
		allocator, renderPass.numDrawItems);

	uint32_t numDrawItems = renderPass.numDrawItems;
	for ( uint32_t i = 0; i < numDrawItems; ++i )
		drawOrder[i] = i;

	uint32_t *sortKeys = renderPass.sortKeys;
	std::sort(drawOrder, drawOrder + numDrawItems, [sortKeys]( uint32_t a, uint32_t b )
	{
		return sortKeys[a] < sortKeys[b];
	});

	RB_DrawItems(renderPass.numDrawItems, renderPass.drawItems, drawOrder);
}

/*
==================
RB_RenderDrawSurfList
==================
*/
static void RB_RenderDrawSurfList( drawSurf_t *drawSurfs, int numDrawSurfs )
{
	/*
	merging surfaces together that share the same shader (e.g. polys, patches)
	upload per frame data - but this might be the same between render passes?

	how about:
		tr.refdef.entities[]

		and .... entityCullInfo_t tr.refdef.entityCullInfo[]
		struct visibleEntity_t
		{
			uint32_t frustumMask; // bitfield of frustums which intersect
			EntityId entityId;
		};

		foreach ghoul2 model:
			transform bones

		foreach visibleEntity:
			upload per frame data

		for polygons:
			merge them, create new surface and upload data

		for patch meshes:
			merge them, create new surface and upload data


	each surface corresponds to something which has all of its gpu data uploaded
	*/

	int estimatedNumShaderStages = (backEnd.viewParms.flags & VPF_DEPTHSHADOW) ? 1 : 4;

	// Prepare memory for the current render pass
	void *allocMark = backEndData->perFrameMemory->Mark();
	assert(backEndData->currentPass == nullptr);
	backEndData->currentPass = RB_CreatePass(
		*backEndData->perFrameMemory, numDrawSurfs * estimatedNumShaderStages);

	// save original time for entity shader offsets
	float originalTime = backEnd.refdef.floatTime;
	FBO_t *fbo = glState.currentFBO;

	backEnd.currentEntity = &tr.worldEntity;
	backEnd.pc.c_surfaces += numDrawSurfs;

	if ( backEnd.depthFill )
	{
		RB_SubmitDrawSurfsForDepthFill(drawSurfs, numDrawSurfs, originalTime);
	}
	else
	{
		RB_SubmitDrawSurfs(drawSurfs, numDrawSurfs, originalTime);
	}

	// Do the drawing and release memory
	const bool sampleShading = qglMinSampleShading && tr.msaaResolveFbo &&
		fbo == tr.renderFbo && r_sampleShading->value > 0;
	if (sampleShading)
	{
		qglEnable(GL_SAMPLE_SHADING);
		qglMinSampleShading(Com_Clamp(0, 1, r_sampleShading->value));
	}
	RB_SubmitRenderPass(
		*backEndData->currentPass,
		*backEndData->perFrameMemory);
	if (sampleShading)
		qglDisable(GL_SAMPLE_SHADING);

	backEndData->perFrameMemory->ResetTo(allocMark);
	backEndData->currentPass = nullptr;

	// Reset things to how they were
	backEnd.refdef.floatTime = originalTime;
	FBO_Bind(fbo);
	GL_SetModelviewMatrix(backEnd.viewParms.world.modelViewMatrix);
}


/*
============================================================================

RENDER BACK END FUNCTIONS

============================================================================
*/

/*
================
RB_SetGL2D

================
*/
void	RB_SetGL2D (void) {
	matrix_t matrix;
	int width, height;

	if (backEnd.projection2D && backEnd.last2DFBO == glState.currentFBO)
		return;

	backEnd.projection2D = qtrue;
	backEnd.last2DFBO = glState.currentFBO;

	if (glState.currentFBO)
	{
		width = glState.currentFBO->width;
		height = glState.currentFBO->height;
	}
	else
	{
		width = glConfig.vidWidth;
		height = glConfig.vidHeight;
	}

	// set 2D virtual screen size
	qglViewport( 0, 0, width, height );
	qglScissor( 0, 0, width, height );

	Matrix16Ortho(0, 640, 480, 0, 0, 1, matrix);
	GL_SetProjectionMatrix(matrix);
	Matrix16Identity(matrix);
	GL_SetModelviewMatrix(matrix);

	GL_State( GLS_DEPTHTEST_DISABLE |
			  GLS_SRCBLEND_SRC_ALPHA |
			  GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );

	GL_Cull(CT_TWO_SIDED);

	// set time for 2D shaders
	backEnd.refdef.time = ri.Milliseconds();
	backEnd.refdef.floatTime = backEnd.refdef.time * 0.001f;

	// reset color scaling
	backEnd.refdef.colorScale = 1.0f;
#ifdef REND2_SP
	R_SP_ApplyScissor();
#endif
}


/*
=============
RE_StretchRaw

FIXME: not exactly backend
Stretches a raw 32 bit power of 2 bitmap image over the given screen rectangle.
Used for cinematics.
=============
*/
void RE_StretchRaw (int x, int y, int w, int h, int cols, int rows, const byte *data, int client, qboolean dirty) {
	int			i, j;
	int			start, end;
	vec4_t quadVerts[4];
	vec2_t texCoords[4];

	if ( !tr.registered ) {
		return;
	}
	R_IssuePendingRenderCommands();

	if ( tess.numIndexes ) {
		RB_EndSurface();
	}

	// we definately want to sync every frame for the cinematics
	qglFinish();

	start = 0;
	if ( r_speeds->integer ) {
		start = ri.Milliseconds();
	}

	// make sure rows and cols are powers of 2
	for ( i = 0 ; ( 1 << i ) < cols ; i++ ) {
	}
	for ( j = 0 ; ( 1 << j ) < rows ; j++ ) {
	}
	if ( ( 1 << i ) != cols || ( 1 << j ) != rows) {
		ri.Error (ERR_DROP, "Draw_StretchRaw: size not a power of 2: %i by %i", cols, rows);
	}

	RE_UploadCinematic (cols, rows, data, client, dirty);

	if ( r_speeds->integer ) {
		end = ri.Milliseconds();
		ri.Printf( PRINT_ALL, "qglTexSubImage2D %i, %i: %i msec\n", cols, rows, end - start );
	}

	// FIXME: HUGE hack
	if (!tr.renderFbo || backEnd.framePostProcessed)
	{
		FBO_Bind(NULL);
	}
	else
	{
		FBO_Bind(tr.renderFbo);
	}

	RB_SetGL2D();

	VectorSet4(quadVerts[0], x,     y,     0.0f, 1.0f);
	VectorSet4(quadVerts[1], x + w, y,     0.0f, 1.0f);
	VectorSet4(quadVerts[2], x + w, y + h, 0.0f, 1.0f);
	VectorSet4(quadVerts[3], x,     y + h, 0.0f, 1.0f);

	VectorSet2(texCoords[0], 0.5f / cols,          0.5f / rows);
	VectorSet2(texCoords[1], (cols - 0.5f) / cols, 0.5f / rows);
	VectorSet2(texCoords[2], (cols - 0.5f) / cols, (rows - 0.5f) / rows);
	VectorSet2(texCoords[3], 0.5f / cols,          (rows - 0.5f) / rows);

	GLSL_BindProgram(&tr.textureColorShader);

	GLSL_SetUniformMatrix4x4(&tr.textureColorShader, UNIFORM_MODELVIEWPROJECTIONMATRIX, glState.modelviewProjection);
	GLSL_SetUniformVec4(&tr.textureColorShader, UNIFORM_COLOR, colorWhite);

	RB_InstantQuad2(quadVerts, texCoords);
}

void RE_UploadCinematic (int cols, int rows, const byte *data, int client, qboolean dirty) {

	GL_Bind( tr.scratchImage[client] );

	// if the scratchImage isn't in the format we want, specify it as a new texture
	if ( cols != tr.scratchImage[client]->width || rows != tr.scratchImage[client]->height ) {
		tr.scratchImage[client]->width = tr.scratchImage[client]->uploadWidth = cols;
		tr.scratchImage[client]->height = tr.scratchImage[client]->uploadHeight = rows;
		qglTexImage2D( GL_TEXTURE_2D, 0, GL_RGB8, cols, rows, 0, GL_RGBA, GL_UNSIGNED_BYTE, data );
		qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	} else {
		if (dirty) {
			// otherwise, just subimage upload it so that drivers can tell we are going to be changing
			// it and don't try and do a texture compression
			qglTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, cols, rows, GL_RGBA, GL_UNSIGNED_BYTE, data );
		}
	}
}


/*
=============
RB_SetColor

=============
*/
static const void	*RB_SetColor( const void *data ) {
	const setColorCommand_t	*cmd;

	cmd = (const setColorCommand_t *)data;

	backEnd.color2D[0] = cmd->color[0];
	backEnd.color2D[1] = cmd->color[1];
	backEnd.color2D[2] = cmd->color[2];
	backEnd.color2D[3] = cmd->color[3];

	return (const void *)(cmd + 1);
}

/*
=============
RB_DrawUiGeometry
=============
*/
#ifdef REND2_SP
static const void *RB_DrawUiGeometry( const void *data ) {
	const uiGeometryCommand_t *cmd = (const uiGeometryCommand_t *)data;
	static_assert( REF_UI_MAX_VERTICES < SHADER_MAX_VERTEXES &&
		REF_UI_MAX_INDICES < SHADER_MAX_INDEXES, "UI geometry exceeds tess capacity" );
	if ( tess.numIndexes ) {
		RB_EndSurface();
	}
	// Use the same target and projection as RB_StretchPic.
	FBO_Bind( backEnd.framePostProcessed ? NULL : tr.renderFbo );
	RB_SetGL2D();
	GLint oldClip[4];
	qglGetIntegerv( GL_SCISSOR_BOX, oldClip );
	GLboolean oldClipEnabled = qglIsEnabled( GL_SCISSOR_TEST );
	if ( cmd->hasClip ) {
		int width = glState.currentFBO ? glState.currentFBO->width : glConfig.vidWidth;
		int height = glState.currentFBO ? glState.currentFBO->height : glConfig.vidHeight;
		// Clamp before conversion. Use double sums to prevent integer overflow.
		int x = Com_Clamp( 0, width, cmd->clip[0] );
		int y = Com_Clamp( 0, height, cmd->clip[1] );
		int right = Com_Clamp( 0, width, (double)cmd->clip[0] + cmd->clip[2] );
		int bottom = Com_Clamp( 0, height, (double)cmd->clip[1] + cmd->clip[3] );
		qglEnable( GL_SCISSOR_TEST );
		qglScissor( x, height - bottom, right - x, bottom - y );
	} else {
		qglDisable( GL_SCISSOR_TEST );
	}
	backEnd.currentEntity = &backEnd.entity2D;
	tr.uiGeometryShader->stages[0]->bundle[0].image[0] = cmd->texture;
	RB_BeginSurface( tr.uiGeometryShader, 0, 0 );
	for ( int i = 0; i < cmd->numVertices; ++i ) {
		tess.xyz[i][0] = cmd->vertices[i].xyz[0];
		tess.xyz[i][1] = cmd->vertices[i].xyz[1];
		tess.xyz[i][2] = 0;
		memcpy(tess.texCoords[i][0], cmd->vertices[i].st, sizeof(vec2_t));
		for ( int j = 0; j < 4; ++j ) {
			tess.vertexColors[i][j] = cmd->vertices[i].modulate[j] / 255.0f;
		}
	}
	for ( int i = 0; i < cmd->numIndices; ++i ) {
		tess.indexes[i] = cmd->indices[i];
	}
	tess.numVertexes = cmd->numVertices;
	tess.numIndexes = cmd->numIndices;
	RB_EndSurface();
	// Do not leave geometry pending when the scissor state is restored.
	tr.uiGeometryShader->stages[0]->bundle[0].image[0] = tr.whiteImage;
	tess.numVertexes = tess.numIndexes = 0;
	qglScissor( oldClip[0], oldClip[1], oldClip[2], oldClip[3] );
	if ( oldClipEnabled ) {
		qglEnable( GL_SCISSOR_TEST );
	} else {
		qglDisable( GL_SCISSOR_TEST );
	}
	return cmd + 1;
}
#endif

static const void *RB_StretchPic ( const void *data ) {
	const stretchPicCommand_t	*cmd;
	shader_t *shader;

	cmd = (const stretchPicCommand_t *)data;

	// FIXME: HUGE hack
	if (!tr.renderFbo || backEnd.framePostProcessed)
	{
		FBO_Bind(NULL);
	}
	else
	{
		FBO_Bind(tr.renderFbo);
	}

	RB_SetGL2D();

	shader = cmd->shader;
	if ( shader != tess.shader ) {
		if ( tess.numIndexes ) {
			RB_EndSurface();
		}
		backEnd.currentEntity = &backEnd.entity2D;
		RB_BeginSurface( shader, 0, 0 );
	}

	RB_CHECKOVERFLOW( 4, 6 );
	int numVerts = tess.numVertexes;
	int numIndexes = tess.numIndexes;

	tess.numVertexes += 4;
	tess.numIndexes += 6;

	tess.indexes[ numIndexes ] = numVerts + 3;
	tess.indexes[ numIndexes + 1 ] = numVerts + 0;
	tess.indexes[ numIndexes + 2 ] = numVerts + 2;
	tess.indexes[ numIndexes + 3 ] = numVerts + 2;
	tess.indexes[ numIndexes + 4 ] = numVerts + 0;
	tess.indexes[ numIndexes + 5 ] = numVerts + 1;

	VectorCopy4(backEnd.color2D, tess.vertexColors[ numVerts ]);
	VectorCopy4(backEnd.color2D, tess.vertexColors[ numVerts + 1 ]);
	VectorCopy4(backEnd.color2D, tess.vertexColors[ numVerts + 2 ]);
	VectorCopy4(backEnd.color2D, tess.vertexColors[ numVerts + 3 ]);

	tess.xyz[ numVerts ][0] = cmd->x;
	tess.xyz[ numVerts ][1] = cmd->y;
	tess.xyz[ numVerts ][2] = 0;

	tess.texCoords[ numVerts ][0][0] = cmd->s1;
	tess.texCoords[ numVerts ][0][1] = cmd->t1;

	tess.xyz[ numVerts + 1 ][0] = cmd->x + cmd->w;
	tess.xyz[ numVerts + 1 ][1] = cmd->y;
	tess.xyz[ numVerts + 1 ][2] = 0;

	tess.texCoords[ numVerts + 1 ][0][0] = cmd->s2;
	tess.texCoords[ numVerts + 1 ][0][1] = cmd->t1;

	tess.xyz[ numVerts + 2 ][0] = cmd->x + cmd->w;
	tess.xyz[ numVerts + 2 ][1] = cmd->y + cmd->h;
	tess.xyz[ numVerts + 2 ][2] = 0;

	tess.texCoords[ numVerts + 2 ][0][0] = cmd->s2;
	tess.texCoords[ numVerts + 2 ][0][1] = cmd->t2;

	tess.xyz[ numVerts + 3 ][0] = cmd->x;
	tess.xyz[ numVerts + 3 ][1] = cmd->y + cmd->h;
	tess.xyz[ numVerts + 3 ][2] = 0;

	tess.texCoords[ numVerts + 3 ][0][0] = cmd->s1;
	tess.texCoords[ numVerts + 3 ][0][1] = cmd->t2;

	return (const void *)(cmd + 1);
}

/*
=============
RB_DrawRotatePic
=============
*/
static const void *RB_RotatePic ( const void *data )
{
	const rotatePicCommand_t	*cmd;
	shader_t *shader;

	cmd = (const rotatePicCommand_t *)data;

	// FIXME: HUGE hack
	if (!tr.renderFbo || backEnd.framePostProcessed)
	{
		FBO_Bind(NULL);
	}
	else
	{
		FBO_Bind(tr.renderFbo);
	}

	RB_SetGL2D();

	shader = cmd->shader;
	if ( shader != tess.shader ) {
		if ( tess.numIndexes ) {
			RB_EndSurface();
		}
		backEnd.currentEntity = &backEnd.entity2D;
		RB_BeginSurface( shader, 0, 0 );
	}

	RB_CHECKOVERFLOW( 4, 6 );
	int numVerts = tess.numVertexes;
	int numIndexes = tess.numIndexes;

	float angle = DEG2RAD( cmd->a );
	float s = sinf( angle );
	float c = cosf( angle );

	matrix3_t m = {
		{ c, s, 0.0f },
		{ -s, c, 0.0f },
		{ cmd->x + cmd->w, cmd->y, 1.0f }
	};

	tess.numVertexes += 4;
	tess.numIndexes += 6;

	tess.indexes[ numIndexes ] = numVerts + 3;
	tess.indexes[ numIndexes + 1 ] = numVerts + 0;
	tess.indexes[ numIndexes + 2 ] = numVerts + 2;
	tess.indexes[ numIndexes + 3 ] = numVerts + 2;
	tess.indexes[ numIndexes + 4 ] = numVerts + 0;
	tess.indexes[ numIndexes + 5 ] = numVerts + 1;

	VectorCopy4(backEnd.color2D, tess.vertexColors[ numVerts ]);
	VectorCopy4(backEnd.color2D, tess.vertexColors[ numVerts + 1]);
	VectorCopy4(backEnd.color2D, tess.vertexColors[ numVerts + 2]);
	VectorCopy4(backEnd.color2D, tess.vertexColors[ numVerts + 3 ]);

	tess.xyz[ numVerts ][0] = m[0][0] * (-cmd->w) + m[2][0];
	tess.xyz[ numVerts ][1] = m[0][1] * (-cmd->w) + m[2][1];
	tess.xyz[ numVerts ][2] = 0;

	tess.texCoords[ numVerts ][0][0] = cmd->s1;
	tess.texCoords[ numVerts ][0][1] = cmd->t1;

	tess.xyz[ numVerts + 1 ][0] = m[2][0];
	tess.xyz[ numVerts + 1 ][1] = m[2][1];
	tess.xyz[ numVerts + 1 ][2] = 0;

	tess.texCoords[ numVerts + 1 ][0][0] = cmd->s2;
	tess.texCoords[ numVerts + 1 ][0][1] = cmd->t1;

	tess.xyz[ numVerts + 2 ][0] = m[1][0] * (cmd->h) + m[2][0];
	tess.xyz[ numVerts + 2 ][1] = m[1][1] * (cmd->h) + m[2][1];
	tess.xyz[ numVerts + 2 ][2] = 0;

	tess.texCoords[ numVerts + 2 ][0][0] = cmd->s2;
	tess.texCoords[ numVerts + 2 ][0][1] = cmd->t2;

	tess.xyz[ numVerts + 3 ][0] = m[0][0] * (-cmd->w) + m[1][0] * (cmd->h) + m[2][0];
	tess.xyz[ numVerts + 3 ][1] = m[0][1] * (-cmd->w) + m[1][1] * (cmd->h) + m[2][1];
	tess.xyz[ numVerts + 3 ][2] = 0;

	tess.texCoords[ numVerts + 3 ][0][0] = cmd->s1;
	tess.texCoords[ numVerts + 3 ][0][1] = cmd->t2;

	return (const void *)(cmd + 1);
}

/*
=============
RB_DrawRotatePic2
=============
*/
static const void *RB_RotatePic2 ( const void *data )
{
	const rotatePicCommand_t	*cmd;
	shader_t *shader;

	cmd = (const rotatePicCommand_t *)data;

	// FIXME: HUGE hack
	if (!tr.renderFbo || backEnd.framePostProcessed)
	{
		FBO_Bind(NULL);
	}
	else
	{
		FBO_Bind(tr.renderFbo);
	}

	RB_SetGL2D();

	shader = cmd->shader;
	if ( shader != tess.shader ) {
		if ( tess.numIndexes ) {
			RB_EndSurface();
		}
		backEnd.currentEntity = &backEnd.entity2D;
		RB_BeginSurface( shader, 0, 0 );
	}

	RB_CHECKOVERFLOW( 4, 6 );
	int numVerts = tess.numVertexes;
	int numIndexes = tess.numIndexes;

	float angle = DEG2RAD( cmd->a );
	float s = sinf( angle );
	float c = cosf( angle );

	matrix3_t m = {
		{ c, s, 0.0f },
		{ -s, c, 0.0f },
		{ cmd->x, cmd->y, 1.0f }
	};

	tess.numVertexes += 4;
	tess.numIndexes += 6;

	tess.indexes[ numIndexes ] = numVerts + 3;
	tess.indexes[ numIndexes + 1 ] = numVerts + 0;
	tess.indexes[ numIndexes + 2 ] = numVerts + 2;
	tess.indexes[ numIndexes + 3 ] = numVerts + 2;
	tess.indexes[ numIndexes + 4 ] = numVerts + 0;
	tess.indexes[ numIndexes + 5 ] = numVerts + 1;

	VectorCopy4(backEnd.color2D, tess.vertexColors[ numVerts ]);
	VectorCopy4(backEnd.color2D, tess.vertexColors[ numVerts + 1]);
	VectorCopy4(backEnd.color2D, tess.vertexColors[ numVerts + 2]);
	VectorCopy4(backEnd.color2D, tess.vertexColors[ numVerts + 3 ]);

	tess.xyz[ numVerts ][0] = m[0][0] * (-cmd->w * 0.5f) + m[1][0] * (-cmd->h * 0.5f) + m[2][0];
	tess.xyz[ numVerts ][1] = m[0][1] * (-cmd->w * 0.5f) + m[1][1] * (-cmd->h * 0.5f) + m[2][1];
	tess.xyz[ numVerts ][2] = 0;

	tess.texCoords[ numVerts ][0][0] = cmd->s1;
	tess.texCoords[ numVerts ][0][1] = cmd->t1;

	tess.xyz[ numVerts + 1 ][0] = m[0][0] * (cmd->w * 0.5f) + m[1][0] * (-cmd->h * 0.5f) + m[2][0];
	tess.xyz[ numVerts + 1 ][1] = m[0][1] * (cmd->w * 0.5f) + m[1][1] * (-cmd->h * 0.5f) + m[2][1];
	tess.xyz[ numVerts + 1 ][2] = 0;

	tess.texCoords[ numVerts + 1 ][0][0] = cmd->s2;
	tess.texCoords[ numVerts + 1 ][0][1] = cmd->t1;

	tess.xyz[ numVerts + 2 ][0] = m[0][0] * (cmd->w * 0.5f) + m[1][0] * (cmd->h * 0.5f) + m[2][0];
	tess.xyz[ numVerts + 2 ][1] = m[0][1] * (cmd->w * 0.5f) + m[1][1] * (cmd->h * 0.5f) + m[2][1];
	tess.xyz[ numVerts + 2 ][2] = 0;

	tess.texCoords[ numVerts + 2 ][0][0] = cmd->s2;
	tess.texCoords[ numVerts + 2 ][0][1] = cmd->t2;

	tess.xyz[ numVerts + 3 ][0] = m[0][0] * (-cmd->w * 0.5f) + m[1][0] * (cmd->h * 0.5f) + m[2][0];
	tess.xyz[ numVerts + 3 ][1] = m[0][1] * (-cmd->w * 0.5f) + m[1][1] * (cmd->h * 0.5f) + m[2][1];
	tess.xyz[ numVerts + 3 ][2] = 0;

	tess.texCoords[ numVerts + 3 ][0][0] = cmd->s1;
	tess.texCoords[ numVerts + 3 ][0][1] = cmd->t2;

	return (const void *)(cmd + 1);
}

/*
=============
RB_PrefilterEnvMap
=============
*/

static const void *RB_PrefilterEnvMap(const void *data) {

	const convolveCubemapCommand_t *cmd = (const convolveCubemapCommand_t *)data;

	// finish any 2D drawing if needed
	if (tess.numIndexes)
		RB_EndSurface();

	RB_SetGL2D();

	qglBindTexture(GL_TEXTURE_CUBE_MAP, tr.renderCubeImage->texnum);
	qglGenerateMipmap(GL_TEXTURE_CUBE_MAP);
	qglBindTexture(GL_TEXTURE_CUBE_MAP, 0);

	// Reuse bounded automatic-probe image slots across direct world reloads.
	if (cmd->cubemap->glass)
		cmd->cubemap->image = tr.glassProbeImages[cmd->cubemapId];
	if (!cmd->cubemap->image)
	{
		GLenum cubemapFormat = GL_RGBA8;
		if (r_hdr->integer)
		{
			cubemapFormat = GL_RGBA16F;
		}
		// FIX ME: Only allocate needed mip level!
		cmd->cubemap->image = R_CreateImage(
			va(cmd->cubemap->glass ? "*glassCube%d" : "*cubeMap%d", cmd->cubemapId),
			NULL,
			cmd->cubemap->glass ? 128 : CUBE_MAP_SIZE,
			cmd->cubemap->glass ? 128 : CUBE_MAP_SIZE,
			IMGTYPE_COLORALPHA,
			IMGFLAG_NO_COMPRESSION |
			IMGFLAG_CLAMPTOEDGE |
			IMGFLAG_MIPMAP |
			IMGFLAG_CUBEMAP,
			cubemapFormat);
		if (cmd->cubemap->glass)
			tr.glassProbeImages[cmd->cubemapId] = cmd->cubemap->image;
	}
	assert(cmd->cubemap->image);

	int width = cmd->cubemap->image->width;
	int height = cmd->cubemap->image->height;
	float roughnessMips = cmd->cubemap->glass ? 5.0f : (float)CUBE_MAP_ROUGHNESS_MIPS;

	for (int level = 0; level <= (int)roughnessMips; level++)
	{
		FBO_Bind(tr.filterCubeFbo);
		qglFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, cmd->cubemap->image->texnum, level);
		GL_BindToTMU(tr.renderCubeImage, TB_CUBEMAP);
		GLSL_BindProgram(&tr.prefilterEnvMapShader);

		qglViewport(0, 0, width, height);
		qglScissor(0, 0, width, height);

		vec4_t viewInfo;
		VectorSet4(viewInfo, 0, level, roughnessMips, level / roughnessMips);
		GLSL_SetUniformVec4(&tr.prefilterEnvMapShader, UNIFORM_VIEWINFO, viewInfo);
		RB_InstantTriangle();
		width = width / 2;
		height = height / 2;
	}

	return (const void *)(cmd + 1);
}


static bool RB_SSAOEnabledForView()
{
	return !backEnd.comparisonBaseline && r_ssao->integer && r_depthPrepass->integer && tr.world &&
		!(backEnd.refdef.rdflags & RDF_NOWORLDMODEL) &&
		!(backEnd.viewParms.flags & (VPF_DEPTHSHADOW | VPF_ORTHOGRAPHIC)) &&
		!backEnd.viewParms.isPortal && !backEnd.viewParms.isSkyPortal &&
		(!backEnd.viewParms.targetFbo || backEnd.viewParms.targetFbo == tr.renderFbo);
}

static bool RB_FullMainView()
{
	return !backEnd.viewParms.isPortal && !backEnd.viewParms.isSkyPortal &&
		(!backEnd.viewParms.targetFbo || backEnd.viewParms.targetFbo == tr.renderFbo) &&
		!(backEnd.refdef.rdflags & RDF_NOWORLDMODEL) &&
		backEnd.viewParms.viewportX == 0 && backEnd.viewParms.viewportY == 0 &&
		backEnd.viewParms.viewportWidth == glConfig.vidWidth && backEnd.viewParms.viewportHeight == glConfig.vidHeight;
}

static bool RB_CompareView(bool includeSky = false)
{
	const bool skyView = includeSky && backEnd.viewParms.isSkyPortal &&
		!(backEnd.refdef.rdflags & RDF_NOWORLDMODEL) &&
		(!backEnd.viewParms.targetFbo || backEnd.viewParms.targetFbo == tr.renderFbo) &&
		backEnd.viewParms.viewportX == 0 && backEnd.viewParms.viewportY == 0 &&
		backEnd.viewParms.viewportWidth == glConfig.vidWidth && backEnd.viewParms.viewportHeight == glConfig.vidHeight;
	return r_compareEnhancements->integer && !r_ssaoDebug->integer && !r_sssDebug->integer &&
		!r_smaaDebug->integer && (RB_FullMainView() || skyView);
}

#ifdef REND2_SP
static bool RB_CapsuleModel(const refEntity_t &e)
{
	if (e.ghoul2 && e.ghoul2->size() && (*e.ghoul2)[0].mModelindex >= 0)
	{
		const char *name = (*e.ghoul2)[0].mFileName;
		if (Q_strncmp(name, "models/players/", 15)) return false;
		// These are vehicles, scenery, or a spectral projection, not body casters.
		for (const char *excluded : {"rocks/", "lambdashuttle/", "tie_", "x-wing/", "z-95/", "swoop/", "marka_ragnos/"})
			if (!Q_strncmp(name + 15, excluded, strlen(excluded))) return false;
		return true;
	}
	const model_t *model = R_GetModelByHandle(e.hModel);
	return model && (!Q_stricmp(model->name, "models/players/mouse/lower.md3") ||
		!Q_stricmp(model->name, "models/players/remote_sp/lower.md3") ||
		!Q_stricmp(model->name, "models/items/remote.md3") ||
		!Q_strncmp(model->name, "models/players/droids/", 22));
}

static int RB_RigidCapsule(const refEntity_t &e, vec4_t *a, vec4_t *b)
{
	const model_t *model = R_GetModelByHandle(e.hModel);
	if (!model || model->type != MOD_MESH || !model->data.mdv[0] || !model->data.mdv[0]->numFrames) return 0;
	const auto *mesh = model->data.mdv[0];
	vec3_t mins, maxs, center, half;
	const int frame = Com_Clampi(0, mesh->numFrames - 1, e.frame);
	const int old = Com_Clampi(0, mesh->numFrames - 1, e.oldframe);
	int longest = 0;
	for (int axis = 0; axis < 3; ++axis)
	{
		mins[axis] = MIN(mesh->frames[frame].bounds[0][axis], mesh->frames[old].bounds[0][axis]);
		maxs[axis] = MAX(mesh->frames[frame].bounds[1][axis], mesh->frames[old].bounds[1][axis]);
		center[axis] = (mins[axis] + maxs[axis]) * 0.5f;
		half[axis] = (maxs[axis] - mins[axis]) * 0.5f;
		if (half[axis] > half[longest]) longest = axis;
	}
	const float radius = sqrtf(half[(longest+1)%3] * half[(longest+2)%3]);
	if (radius < 0.5f) return 0;
	const float length = MAX(0.0f, half[longest] - radius);
	VectorCopy(e.origin, a[0]);
	VectorCopy(e.origin, b[0]);
	float scale = 0;
	for (int axis = 0; axis < 3; ++axis)
	{
		VectorMA(a[0], center[axis] - (axis == longest ? length : 0), e.axis[axis], a[0]);
		VectorMA(b[0], center[axis] + (axis == longest ? length : 0), e.axis[axis], b[0]);
		scale = MAX(scale, VectorLength(e.axis[axis]));
	}
	a[0][3] = radius * scale * r_capsuleShadowRadius->value;
	return 1;
}

static int RB_HumanoidCapsules(const refEntity_t &e, CGhoul2Info_v &g2,
	const model_t *model, const int *active, vec4_t *a, vec4_t *b)
{
	static const char *bones[] = {"pelvis", "thoracic", "cranium", "lfemurYZ", "ltibia", "ltalus",
		"rfemurYZ", "rtibia", "rtalus", "lhumerus", "lradius", "lhand", "rhumerus", "rradius", "rhand"};
	static const int links[][2] = {{0,1},{1,2},{3,4},{4,5},{6,7},{7,8},{9,10},{10,11},{12,13},{13,14},{11,11},{14,14}};
	static const float radii[] = {7,5,4,3,4,3,3,2.5f,3,2.5f,2.5f,2.5f};
	static const char *parts[] = {"torso", "head", "l_leg", "l_leg", "r_leg", "r_leg",
		"l_arm", "l_arm", "r_arm", "r_arm", "l_hand", "r_hand"};
	const auto *offsets = reinterpret_cast<const mdxmHierarchyOffsets_t *>(model->mdxm + 1);
	bool visible[12] = {}, hips = false;
	for (int surface = 0; surface < model->mdxm->numSurfaces; ++surface)
	{
		if (!active[surface] || !static_cast<const mdxmSurface_t *>(G2_FindSurface(model, surface, 0))->numTriangles) continue;
		const auto *info = reinterpret_cast<const mdxmSurfHierarchy_t *>(reinterpret_cast<const byte *>(offsets) + offsets->offsets[surface]);
		if (strstr(info->name, "_cap_")) continue;
		hips |= !Q_strncmp(info->name, "hips", 4);
		for (int link = 0; link < 12; ++link)
			visible[link] |= !Q_strncmp(info->name, parts[link], strlen(parts[link]));
	}
	vec3_t points[15];
	bool valid[15] = {};
	for (int i = 0; i < 15; ++i)
	{
		const int bolt = G2API_AddBolt(&g2[0], bones[i]);
		if (bolt < 0) continue;
		mdxaBone_t matrix;
		valid[i] = G2API_GetBoltMatrix(g2, 0, bolt, &matrix, e.angles, e.origin,
			backEnd.refdef.time, nullptr, e.modelScale) != qfalse;
		G2API_RemoveBolt(&g2[0], bolt);
		if (valid[i])
			for (int axis = 0; axis < 3; ++axis) points[i][axis] = matrix.matrix[axis][3];
	}
	float scale = MAX(fabsf(e.modelScale[0]), MAX(fabsf(e.modelScale[1]), fabsf(e.modelScale[2])));
	if (!scale) scale = 1;
	scale *= r_capsuleShadowRadius->value;
	int count = 0;
	for (int link = 0; link < 12; ++link)
		if ((visible[link] || (link == 0 && hips)) && valid[links[link][0]] && valid[links[link][1]])
		{
			VectorCopy(points[link == 1 && !visible[0] ? 2 : links[link][0]], a[count]);
			a[count][3] = radii[link] * scale;
			VectorCopy(points[link == 0 && !visible[0] ? 0 : links[link][1]], b[count]);
			++count;
		}
	return count;
}
#endif

static void RB_RenderCapsules()
{
#ifdef REND2_SP
	if (!r_capsuleShadows->integer || r_capsuleShadowStrength->value <= 0 || !RB_FullMainView()) return;
	const bool timed = r_speeds->integer == 100;
	const int startTime = timed ? ri.Milliseconds() : 0;
	int prepareMsec = 0;
	const bool report = r_capsuleShadowDebug->integer != 0;
	if (report) ri.Cvar_Set("r_capsuleShadowDebug", "0");
	const auto &view = backEnd.viewParms;
	FBO_Bind(tr.screenSsaoFbo);
	qglViewport(0, 0, tr.screenSsaoFbo->width, tr.screenSsaoFbo->height);
	GL_State(GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO);
	GL_Cull(CT_TWO_SIDED);
	GLSL_BindProgram(&tr.capsuleShader);
	GL_BindToTMU(tr.hdrDepthImage, 0);
	const vec4_t info = {view.zFar / r_znear->value, view.zFar, tanf(DEG2RAD(view.fovX * 0.5f)), tanf(DEG2RAD(view.fovY * 0.5f))};
	GLSL_SetUniformVec4(&tr.capsuleShader, UNIFORM_VIEWINFO, info);
	GLSL_SetUniformVec3(&tr.capsuleShader, UNIFORM_VIEWORIGIN, view.ori.origin);
	GLSL_SetUniformVec3(&tr.capsuleShader, UNIFORM_VIEWFORWARD, view.ori.axis[0]);
	GLSL_SetUniformVec3(&tr.capsuleShader, UNIFORM_VIEWLEFT, view.ori.axis[1]);
	GLSL_SetUniformVec3(&tr.capsuleShader, UNIFORM_VIEWUP, view.ori.axis[2]);
	const vec4_t strength = {r_capsuleShadowStrength->value, r_capsuleShadowSoftness->value,
		r_capsuleShadowRange->value, float(r_capsuleShadowWalls->integer)};
	GLSL_SetUniformVec4(&tr.capsuleShader, UNIFORM_SSSPARAMS, strength);
	std::vector<const trRefEntity_t *> candidates;
	for (int i = 0; i < backEnd.refdef.num_entities; ++i)
	{
		const refEntity_t &e = backEnd.refdef.entities[i].e;
		if (e.reType == RT_MODEL &&
			!(e.renderfx & (RF_NOSHADOW | RF_DEPTHHACK | RF_FORCE_ENT_ALPHA | RF_ALPHA_FADE | RF_DISTORTION | RF_DISINTEGRATE1 | RF_DISINTEGRATE2)) &&
			Distance(e.origin, view.ori.origin) <= 1024 && RB_CapsuleModel(e))
			candidates.push_back(&backEnd.refdef.entities[i]);
	}
	std::sort(candidates.begin(), candidates.end(), [&view](const trRefEntity_t *a, const trRefEntity_t *b) {
		return DistanceSquared(a->e.origin, view.ori.origin) < DistanceSquared(b->e.origin, view.ori.origin);
	});
	int actors = 0;
	for (const trRefEntity_t *candidate : candidates)
	{
		if (actors >= 8) break;
		const int prepareStart = timed ? ri.Milliseconds() : 0;
		const refEntity_t &e = candidate->e;
		vec4_t a[12] = {}, b[12] = {};
		vec3_t mins, maxs;
		ClearBounds(mins, maxs);
		int count = 0;
		if (!e.ghoul2 || !e.ghoul2->size())
		{
			count = RB_RigidCapsule(e, a, b);
			if (report && count) ri.Printf(PRINT_ALL, "Capsules: %s root=rigid count=%d\n", R_GetModelByHandle(e.hModel)->name, count);
		}
		else
		{
			CGhoul2Info_v &g2 = *e.ghoul2;
			const char *gla = G2API_GetGLAName(&g2[0]);
			if (!gla) continue;
			// A detached limb retains the full skeleton. Follow the rendered surface tree.
			const model_t *model = g2[0].currentModel;
			if (!model || !model->mdxm) continue;
			std::vector<int> active(model->mdxm->numSurfaces, 0);
			G2_FindOverrideSurface(-1, g2[0].mSlist);
			G2_FindRecursiveSurface(model, g2[0].mSurfaceRoot, g2[0].mSlist, active.data());
			if (strstr(gla, "_humanoid"))
				count = RB_HumanoidCapsules(e, g2, model, active.data(), a, b);
			else
			{
				orientationr_t orientation;
				R_RotateForEntity(candidate, &view, &orientation);
				count = R_Ghoul2AutoCapsules(e, orientation.modelMatrix, active.data(), a, b);
			}
			if (report && count)
			{
				const auto *offsets = reinterpret_cast<const mdxmHierarchyOffsets_t *>(model->mdxm + 1);
				const auto *root = reinterpret_cast<const mdxmSurfHierarchy_t *>(reinterpret_cast<const byte *>(offsets) + offsets->offsets[g2[0].mSurfaceRoot]);
				ri.Printf(PRINT_ALL, "Capsules: %s root=%s count=%d\n", g2[0].mFileName, root->name, count);
			}
		}
		if (!count) continue;
		float maxRadius = 0;
		for (int i = 0; i < count; ++i)
		{
			AddPointToBounds(a[i], mins, maxs);
			AddPointToBounds(b[i], mins, maxs);
			maxRadius = MAX(maxRadius, a[i][3]);
			b[i][3] = 1.0f / MAX(DistanceSquared(a[i], b[i]), 0.001f);
		}
		// Bound the screen work to the actor and its short-range ground shadow.
		const float reach = maxRadius * (1 + strength[1]) + strength[2] * 0.6f * strength[1];
		for (int axis = 0; axis < 3; ++axis)
		{
			const float extent = reach + ((axis == 2 || r_capsuleShadowWalls->integer) ? strength[2] : 0);
			mins[axis] -= extent; maxs[axis] += extent;
		}
		matrix_t vp;
		Matrix16Multiply(view.projectionMatrix, view.world.modelViewMatrix, vp);
		float left = 1, right = -1, bottom = 1, top = -1;
		bool crossesNear = false, inFront = false;
		for (int corner = 0; corner < 8; ++corner)
		{
			vec3_t p;
			for (int axis = 0; axis < 3; ++axis) p[axis] = (corner & (1 << axis)) ? maxs[axis] : mins[axis];
			float w = vp[3]*p[0] + vp[7]*p[1] + vp[11]*p[2] + vp[15];
			if (w <= 0.01f) { crossesNear = true; continue; }
			inFront = true;
			float x = (vp[0]*p[0] + vp[4]*p[1] + vp[8]*p[2] + vp[12]) / w;
			float y = (vp[1]*p[0] + vp[5]*p[1] + vp[9]*p[2] + vp[13]) / w;
			left = MIN(left, x); right = MAX(right, x); bottom = MIN(bottom, y); top = MAX(top, y);
		}
		if (!inFront) continue;
		if (crossesNear) { left = bottom = -1; right = top = 1; }
		int x = int((Com_Clamp(-1,1,left)*0.5f+0.5f)*tr.screenSsaoFbo->width);
		int y = int((Com_Clamp(-1,1,bottom)*0.5f+0.5f)*tr.screenSsaoFbo->height);
		int x2 = int(ceilf((Com_Clamp(-1,1,right)*0.5f+0.5f)*tr.screenSsaoFbo->width));
		int y2 = int(ceilf((Com_Clamp(-1,1,top)*0.5f+0.5f)*tr.screenSsaoFbo->height));
		if (x2 <= x || y2 <= y) continue;
		if (timed) prepareMsec += ri.Milliseconds() - prepareStart;
		qglScissor(x, y, x2-x, y2-y);
		GLSL_SetUniformVec3(&tr.capsuleShader, UNIFORM_CAPSULEMINS, mins);
		GLSL_SetUniformVec3(&tr.capsuleShader, UNIFORM_CAPSULEMAXS, maxs);
		qglUniform4fv(tr.capsuleShader.uniforms[UNIFORM_CAPSULEA], 12, a[0]);
		qglUniform4fv(tr.capsuleShader.uniforms[UNIFORM_CAPSULEB], 12, b[0]);
		memcpy(tr.capsuleShader.uniformBuffer + tr.capsuleShader.uniformBufferOffsets[UNIFORM_CAPSULEA], a, sizeof(a));
		memcpy(tr.capsuleShader.uniformBuffer + tr.capsuleShader.uniformBufferOffsets[UNIFORM_CAPSULEB], b, sizeof(b));
		RB_InstantTriangle();
		++actors;
	}
	if (timed) ri.Printf(PRINT_ALL, "Capsule CPU: total=%d prepare=%d actors=%d\n", ri.Milliseconds() - startTime, prepareMsec, actors);
#endif
}

static void RB_RenderSSAO(image_t *depth, FBO_t *raw, FBO_t *filtered, float radius)
{
	gpuFrame_t *frame = &backEndData->frames[backEndData->realFrameNumber % MAX_FRAMES];
	const int layer = depth == tr.weaponDepthFloatImage ? 1 : 0;
	const bool timed = glRefConfig.timerQuery && r_speeds->integer == 100;
	if (timed)
	{
		frame->aoPending[layer] = true;
		qglQueryCounter(frame->aoQueries[layer * 2], GL_TIMESTAMP);
	}
	const float zmax = backEnd.viewParms.zFar;
	const float zmin = r_znear->value;
	const vec4_t viewInfo = { zmax / zmin, zmax, float(r_ssaoMethod->integer), 12.0f * radius };

	FBO_Bind(raw);

	qglViewport(0, 0, raw->width, raw->height);
	qglScissor(0, 0, raw->width, raw->height);

	GL_State( GLS_DEPTHTEST_DISABLE );

	GLSL_BindProgram(&tr.ssaoShader);
	GL_Cull(CT_TWO_SIDED);

	GL_BindToTMU(depth, TB_COLORMAP);
	GLSL_SetUniformVec4(&tr.ssaoShader, UNIFORM_VIEWINFO, viewInfo);
	// Preserve the old radius at 80-degree horizontal FOV and 4:3 aspect.
	const float reference = tanf(DEG2RAD(40.0f));
	const vec4_t params = {r_ssaoMethod->integer ? 1.0f + Com_Clamp(0, 3, r_gtaoQuality->integer) : 0.0f, Com_Clamp(0.05f, 4.0f, radius),
		reference / tanf(DEG2RAD(backEnd.viewParms.fovX * 0.5f)),
		0.75f * reference / tanf(DEG2RAD(backEnd.viewParms.fovY * 0.5f))};
	GLSL_SetUniformVec4(&tr.ssaoShader, UNIFORM_SSAOPARAMS, params);
	GLSL_SetUniformInt(&tr.ssaoShader, UNIFORM_SSAODEBUG, 0);

	RB_InstantTriangle();

	FBO_Bind(tr.aoScratchFbo[1]);

	qglViewport(0, 0, tr.aoScratchFbo[1]->width, tr.aoScratchFbo[1]->height);
	qglScissor(0, 0, tr.aoScratchFbo[1]->width, tr.aoScratchFbo[1]->height);

	GLSL_BindProgram(&tr.depthBlurShader[0]);

	GL_BindToTMU(raw->colorImage[0], TB_COLORMAP);
	GL_BindToTMU(depth, TB_LIGHTMAP);
	GLSL_SetUniformVec4(&tr.depthBlurShader[0], UNIFORM_VIEWINFO, viewInfo);
	const vec4_t denoiseParams = {float(r_gtaoHalfRes->integer && r_gtaoDenoise->integer), 0, 0, 0};
	GLSL_SetUniformVec4(&tr.depthBlurShader[0], UNIFORM_SSAOPARAMS, denoiseParams);

	RB_InstantTriangle();

	const bool upsample = r_ssaoMethod->integer && r_gtaoHalfRes->integer && r_gtaoUpsample->integer;
	FBO_t *vertical = upsample ? tr.aoScratchFbo[0] : filtered;
	FBO_Bind(vertical);
	qglViewport(0, 0, vertical->width, vertical->height);
	qglScissor(0, 0, vertical->width, vertical->height);

	GLSL_BindProgram(&tr.depthBlurShader[1]);

	GL_BindToTMU(tr.aoScratchImage[1], TB_COLORMAP);
	GL_BindToTMU(depth, TB_LIGHTMAP);
	GLSL_SetUniformVec4(&tr.depthBlurShader[1], UNIFORM_VIEWINFO, viewInfo);
	GLSL_SetUniformVec4(&tr.depthBlurShader[1], UNIFORM_SSAOPARAMS, denoiseParams);

	RB_InstantTriangle();
	if (upsample)
	{
		FBO_Bind(filtered);
		qglViewport(0, 0, filtered->width, filtered->height);
		qglScissor(0, 0, filtered->width, filtered->height);
		GL_BindToTMU(tr.aoScratchImage[0], TB_COLORMAP);
		const vec4_t upsampleInfo = {viewInfo[0], viewInfo[1], 2.0f, viewInfo[3]};
		GLSL_SetUniformVec4(&tr.depthBlurShader[1], UNIFORM_VIEWINFO, upsampleInfo);
		RB_InstantTriangle();
	}
	if (timed)
		qglQueryCounter(frame->aoQueries[layer * 2 + 1], GL_TIMESTAMP);
}

static void RB_RenderDepthOnly( drawSurf_t *drawSurfs, int numDrawSurfs )
{
	backEnd.depthFill = qtrue;
	qglColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	RB_RenderDrawSurfList(drawSurfs, numDrawSurfs);
	qglColorMask(
		!backEnd.colorMask[0],
		!backEnd.colorMask[1],
		!backEnd.colorMask[2],
		!backEnd.colorMask[3]);
	backEnd.depthFill = qfalse;
	if (backEnd.ssaoDepthLayer >= backEndState_t::DEPTH_WEAPON_AO)
		return;

	if (glState.currentFBO != tr.renderFbo ||
		(backEnd.refdef.rdflags & RDF_NOWORLDMODEL) ||
		(backEnd.viewParms.flags & (VPF_DEPTHSHADOW | VPF_ORTHOGRAPHIC)) ||
		backEnd.viewParms.isPortal || backEnd.viewParms.isSkyPortal)
	{
		return;
	}

	// Only resolve the main pass depth
	if (tr.msaaResolveFbo)
	{
		FBO_FastBlit(
			tr.renderFbo, NULL,
			tr.msaaResolveFbo, NULL,
			GL_DEPTH_BUFFER_BIT,
			GL_NEAREST);
	}
	else if (tr.renderFbo == NULL)
	{
		// If we're rendering directly to the screen, copy the depth to a texture
		GL_BindToTMU(tr.renderDepthImage, 0);
		qglCopyTexImage2D(
			GL_TEXTURE_2D, 0,
			GL_DEPTH_COMPONENT24, 0,
			0, glConfig.vidWidth,
			glConfig.vidHeight, 0);
	}

	if (RB_SSAOEnabledForView())
	{
		// need the depth in a texture we can do GL_LINEAR sampling on, so
		// copy it to an HDR image
		vec4i_t srcBox;
		VectorSet4(srcBox, 0, tr.renderDepthImage->height, tr.renderDepthImage->width, -tr.renderDepthImage->height);
		FBO_BlitFromTexture(
			tr.renderDepthImage,
			srcBox,
			nullptr,
			tr.hdrDepthFbo,
			nullptr,
			nullptr,
			nullptr, 0);
	}
	if (r_softParticles->integer)
	{
		vec4i_t box = {0, tr.renderDepthImage->height, tr.renderDepthImage->width, -tr.renderDepthImage->height};
		FBO_BlitFromTexture(tr.renderDepthImage, box, nullptr, tr.softDepthFbo, nullptr, nullptr, nullptr, 0);
		backEnd.softDepthViewParm = backEnd.viewParms.currentViewParm;
	}
}

static void RB_RenderMainPass( drawSurf_t *drawSurfs, int numDrawSurfs )
{
	if ( backEnd.viewParms.flags & VPF_DEPTHSHADOW )
	{
		return;
	}

	RB_RenderDrawSurfList(drawSurfs, numDrawSurfs);

	if (r_drawSun->integer)
	{
		RB_DrawSun(0.1, tr.sunShader);
	}

	if (r_drawSunRays->integer)
	{
		FBO_t *oldFbo = glState.currentFBO;
		FBO_Bind(tr.sunRaysFbo);

		qglClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
		qglClear( GL_COLOR_BUFFER_BIT );

		tr.sunFlareQueryActive[tr.sunFlareQueryIndex] = qtrue;
		qglBeginQuery(GL_SAMPLES_PASSED, tr.sunFlareQuery[tr.sunFlareQueryIndex]);

		RB_DrawSun(0.3, tr.sunFlareShader);

		qglEndQuery(GL_SAMPLES_PASSED);

		FBO_Bind(oldFbo);
	}

	// darken down any stencil shadows
	RB_ShadowFinish();

	RB_RenderFlares();
}

static void RB_RenderAllDepthRelatedPasses( drawSurf_t *drawSurfs, int numDrawSurfs )
{
	if ( backEnd.refdef.rdflags & RDF_NOWORLDMODEL )
	{
		return;
	}

	if ( !r_depthPrepass->integer && !(backEnd.viewParms.flags & VPF_DEPTHSHADOW) )
	{
		return;
	}

	FBO_t *oldFbo = glState.currentFBO;

	if (backEnd.viewParms.flags & VPF_DEPTHCLAMP)
	{
		qglEnable(GL_DEPTH_CLAMP);
	}

	const bool ssao = RB_SSAOEnabledForView() && oldFbo == tr.renderFbo;
	backEnd.ssaoDepthLayer = ssao ? backEndState_t::DEPTH_WORLD : backEndState_t::DEPTH_ALL;
	RB_RenderDepthOnly(drawSurfs, numDrawSurfs);

	if (ssao)
	{
		RB_RenderSSAO(tr.hdrDepthImage, tr.ssaoRawFbo, tr.screenSsaoFbo, r_ssaoRadius->value);
		RB_RenderCapsules();
		backEnd.ssaoViewParm = backEnd.viewParms.currentViewParm;
		bool hasWeapon = false;
		for (int i = 0; i < numDrawSurfs && !hasWeapon; ++i)
		{
			int entity, cube, post;
			shader_t *shader;
			R_DecomposeSort(drawSurfs[i].sort, &entity, &shader, &cube, &post);
			hasWeapon = entity != REFENTITYNUM_WORLD && R_IsViewModel(backEnd.refdef.entities[entity].e);
		}
		if (hasWeapon)
		{
			if (r_ssaoViewModel->integer || r_ssaoDebug->integer == 3)
			{
				FBO_Bind(tr.weaponDepthFbo);
				qglScissor(0, 0, glConfig.vidWidth, glConfig.vidHeight);
				GL_State(GLS_DEFAULT);
				qglClearDepth(1.0);
				qglClear(GL_DEPTH_BUFFER_BIT);
				SetViewportAndScissor();
				backEnd.ssaoDepthLayer = backEndState_t::DEPTH_WEAPON_AO;
				RB_RenderDepthOnly(drawSurfs, numDrawSurfs);
				backEnd.ssaoWeaponViewParm = backEnd.viewParms.currentViewParm;
				if (r_ssaoViewModel->integer)
				{
					vec4i_t box = {0, tr.weaponDepthImage->height, tr.weaponDepthImage->width, -tr.weaponDepthImage->height};
					FBO_BlitFromTexture(tr.weaponDepthImage, box, nullptr, tr.weaponDepthFloatFbo, nullptr, nullptr, nullptr, 0);
					RB_RenderSSAO(tr.weaponDepthFloatImage, tr.aoScratchFbo[0], tr.weaponSsaoFbo, r_ssaoViewModelRadius->value);
					backEnd.ssaoWeaponReady = true;
				}
			}
			// Keep the original compressed depth for the visible weapon and world occlusion.
			FBO_Bind(oldFbo);
			SetViewportAndScissor();
			backEnd.ssaoDepthLayer = backEndState_t::DEPTH_WEAPON_NATIVE;
			RB_RenderDepthOnly(drawSurfs, numDrawSurfs);
		}
	}
	backEnd.ssaoDepthLayer = backEndState_t::DEPTH_ALL;

	// reset viewport and scissor
	FBO_Bind(oldFbo);
	SetViewportAndScissor();

	if (backEnd.viewParms.flags & VPF_DEPTHCLAMP)
	{
		qglDisable(GL_DEPTH_CLAMP);
	}
}

static void RB_UpdateCameraConstants(gpuFrame_t *frame)
{
	for (int i = 0; i < tr.numCachedViewParms; i++)
	{
		backEnd.viewParms = tr.cachedViewParms[i];
		const float zmax = tr.cachedViewParms[i].zFar;
		const float zmin = tr.cachedViewParms[i].zNear;

		const float ymax = zmax * tanf(backEnd.viewParms.fovY * M_PI / 360.0f);
		const float xmax = zmax * tanf(backEnd.viewParms.fovX * M_PI / 360.0f);

		vec3_t viewBasis[3];

		VectorNormalize(tr.cachedViewParms[i].ori.axis[0]);
		VectorNormalize(tr.cachedViewParms[i].ori.axis[1]);
		VectorNormalize(tr.cachedViewParms[i].ori.axis[2]);

		VectorScale(tr.cachedViewParms[i].ori.axis[0], zmax, viewBasis[0]);
		VectorScale(tr.cachedViewParms[i].ori.axis[1], xmax, viewBasis[1]);
		VectorScale(tr.cachedViewParms[i].ori.axis[2], ymax, viewBasis[2]);

		CameraBlock cameraBlock = {};
		Matrix16Multiply(
			tr.cachedViewParms[i].projectionMatrix,
			tr.cachedViewParms[i].world.modelViewMatrix,
			cameraBlock.viewProjectionMatrix);
		VectorSet4(cameraBlock.viewInfo, zmax / zmin, zmax, 0.0f, 0.0f);
		VectorCopy(viewBasis[0], cameraBlock.viewForward);
		VectorCopy(viewBasis[1], cameraBlock.viewLeft);
		VectorCopy(viewBasis[2], cameraBlock.viewUp);
		VectorCopy(tr.cachedViewParms[i].ori.origin, cameraBlock.viewOrigin);

		tr.cameraUboOffsets[tr.cachedViewParms[i].currentViewParm] = RB_AppendConstantsData(
			frame, &cameraBlock, sizeof(cameraBlock));
	}
}

static void RB_UpdateSceneConstants(gpuFrame_t *frame, const trRefdef_t *refdef)
{
	SceneBlock sceneBlock = {};
	VectorCopy4(refdef->sunDir, sceneBlock.primaryLightOrigin);
	VectorCopy(refdef->sunAmbCol, sceneBlock.primaryLightAmbient);
	VectorCopy(refdef->sunCol, sceneBlock.primaryLightColor);
	if (tr.world != nullptr)
		sceneBlock.globalFogIndex = tr.world->globalFogIndex - 1;
	else
		sceneBlock.globalFogIndex = -1;
	sceneBlock.currentTime = refdef->floatTime;
	sceneBlock.frameTime = refdef->frameTime;

	tr.sceneUboOffset = RB_AppendConstantsData(
		frame, &sceneBlock, sizeof(sceneBlock));
}

static void RB_UpdateLightsConstants(gpuFrame_t *frame, const trRefdef_t *refdef)
{
	LightsBlock lightsBlock = {};
	Matrix16Copy(refdef->torchVP, lightsBlock.torchVP);
	VectorCopy4(refdef->torchOrigin, lightsBlock.torchOrigin);
	VectorCopy4(refdef->torchDirection, lightsBlock.torchDirection);
	VectorCopy4(refdef->torchParams, lightsBlock.torchParams);

	memcpy(lightsBlock.shadowVP1, refdef->sunShadowMvp[0], sizeof(matrix_t));
	memcpy(lightsBlock.shadowVP2, refdef->sunShadowMvp[1], sizeof(matrix_t));
	memcpy(lightsBlock.shadowVP3, refdef->sunShadowMvp[2], sizeof(matrix_t));

	lightsBlock.numLights = MIN(refdef->num_dlights, MAX_DLIGHTS);
	for (int i = 0; i < lightsBlock.numLights; ++i)
	{
		const dlight_t *dlight = refdef->dlights + i;
		LightsBlock::Light *lightData = lightsBlock.lights + i;

		VectorSet4(
			lightData->origin,
			dlight->origin[0],
			dlight->origin[1],
			dlight->origin[2],
			1.0f);
		VectorCopy(dlight->color, lightData->color);
		lightData->radius = dlight->radius;
	}
	// TODO: Add pshadow data

	tr.lightsUboOffset = RB_AppendConstantsData(
		frame, &lightsBlock, sizeof(lightsBlock));
}

static void RB_UpdateFogsConstants(gpuFrame_t *frame)
{
	FogsBlock fogsBlock = {};
	if (tr.world == nullptr)
	{
		fogsBlock.numFogs = 0;
	}
	else
	{
		fogsBlock.numFogs = tr.world->numfogs - 1; // Don't reserve fog 0 as 'null'
	}

	for (int i = 0; i < fogsBlock.numFogs; ++i)
	{
		const fog_t *fog = tr.world->fogs + i + 1;
		FogsBlock::Fog *fogData = fogsBlock.fogs + i;

		VectorCopy4(fog->surface, fogData->plane);
		VectorCopy4(fog->color, fogData->color);
		fogData->depthToOpaque = sqrtf(-logf(1.0f / 255.0f)) / fog->parms.depthForOpaque;
#ifdef REND2_SP
		if (i + 1 == tr.world->globalFogIndex && tr.rangedFog > 0.0f)
			fogData->depthToOpaque = sqrtf(-logf(1.0f / 255.0f)) / tr.rangedFog;
#endif
		fogData->hasPlane = fog->hasSurface;
	}

	tr.fogsUboOffset = RB_AppendConstantsData(
		frame, &fogsBlock, sizeof(fogsBlock));
}

static void RB_UpdateEntityLightConstants(
	EntityBlock& entityBlock,
	const trRefEntity_t *refEntity)
{
	static const float normalizeFactor = 1.0f / 255.0f;

	VectorScale(refEntity->ambientLight, normalizeFactor, entityBlock.ambientLight);
	VectorScale(refEntity->directedLight, normalizeFactor, entityBlock.directedLight);
#ifdef REND2_SP
	if (tr.refdef.rdflags & RDF_doFullbright)
	{
		VectorSet(entityBlock.ambientLight, 1.0f, 1.0f, 1.0f);
		VectorClear(entityBlock.directedLight);
	}
#endif
	VectorCopy(refEntity->lightDir, entityBlock.lightOrigin);

	vec3_t lightDir;
	float lightRadius;
	VectorCopy(refEntity->modelLightDir, lightDir);
	lightRadius = 300.0f;
	if (r_shadows->integer == 2)
	{
		lightDir[2] = 0.0f;
		VectorNormalize(lightDir);
		VectorSet(lightDir, lightDir[0] * 0.3f, lightDir[1] * 0.3f, 1.0f);
		lightRadius = refEntity->e.lightingOrigin[2] - refEntity->e.shadowPlane + 64.0f;
	}

	VectorCopy(lightDir, entityBlock.modelLightDir);
	entityBlock.lightOrigin[3] = 0.0f;
	entityBlock.lightRadius = lightRadius;
}

static void RB_UpdateEntityMatrixConstants(
	EntityBlock& entityBlock,
	const trRefEntity_t *refEntity)
{
	orientationr_t ori;
	R_RotateForEntity(refEntity, &backEnd.viewParms, &ori);
	Matrix16Copy(ori.modelMatrix, entityBlock.modelMatrix);
	VectorCopy(ori.viewOrigin, entityBlock.localViewOrigin);
}

static void RB_UpdateEntityModelConstants(
	EntityBlock& entityBlock,
	const trRefEntity_t *refEntity)
{
	static const float normalizeFactor = 1.0f / 255.0f;

	entityBlock.fxVolumetricBase = -1.0f;
	if (refEntity->e.renderfx & RF_VOLUMETRIC)
		entityBlock.fxVolumetricBase = refEntity->e.shaderRGBA[0] * normalizeFactor;

	entityBlock.vertexLerp = 0.0f;
	if (refEntity->e.oldframe || refEntity->e.frame)
		if (refEntity->e.oldframe != refEntity->e.frame)
			entityBlock.vertexLerp = refEntity->e.backlerp;

	entityBlock.entityTime = 0.0f;
	if (refEntity != &tr.worldEntity)
		entityBlock.entityTime = -refEntity->e.shaderTime;
}

static void RB_UpdateSkyEntityConstants(gpuFrame_t *frame, const trRefdef_t *refdef)
{
	EntityBlock skyEntityBlock = {};
	skyEntityBlock.fxVolumetricBase = -1.0f;

	if (tr.world && tr.world->skyboxportal)
		Matrix16Translation(tr.skyPortalParms.ori.origin, skyEntityBlock.modelMatrix);
	else
		Matrix16Translation(refdef->vieworg, skyEntityBlock.modelMatrix);
	tr.skyEntityUboOffset = RB_AppendConstantsData(
		frame, &skyEntityBlock, sizeof(skyEntityBlock));
}

static void ComputeDeformValues(
	//const trRefEntity_t *refEntity,
	const shader_t *shader,
	deform_t *type,
	genFunc_t *waveFunc,
	vec4_t deformParams0,
	vec4_t deformParams1)
{
	// u_DeformGen
	*type = DEFORM_NONE;
	*waveFunc = GF_NONE;

	// Handled via the CGEN_DISINTEGRATION_2 color gen, as both just happen at the same time
	/*if (refEntity->e.renderfx & RF_DISINTEGRATE2)
	{
		*type = DEFORM_DISINTEGRATION;
		return;
	}*/

	if (!ShaderRequiresCPUDeforms(shader))
	{
		// only support the first one
		const deformStage_t  *ds = &shader->deforms[0];

		switch (ds->deformation)
		{
		case DEFORM_WAVE:
			*type = DEFORM_WAVE;
			*waveFunc = ds->deformationWave.func;

			deformParams0[0] = ds->deformationWave.base;
			deformParams0[1] = ds->deformationWave.amplitude;
			deformParams0[2] = ds->deformationWave.phase;
			deformParams0[3] = ds->deformationWave.frequency;
			deformParams1[0] = ds->deformationSpread;
			deformParams1[1] = 0.0f;
			deformParams1[2] = 0.0f;
			break;

		case DEFORM_BULGE:
			*type = DEFORM_BULGE;

			deformParams0[0] = 0.0f;
			deformParams0[1] = ds->bulgeHeight; // amplitude
			deformParams0[2] = ds->bulgeWidth;  // phase
			deformParams0[3] = ds->bulgeSpeed;  // frequency
			deformParams1[0] = 0.0f;
			deformParams1[1] = 0.0f;
			deformParams1[2] = 0.0f;

			if (ds->bulgeSpeed == 0.0f && ds->bulgeWidth == 0.0f)
				*type = DEFORM_BULGE_UNIFORM;

			break;

		case DEFORM_MOVE:
			*type = DEFORM_MOVE;
			*waveFunc = ds->deformationWave.func;

			deformParams0[0] = ds->deformationWave.base;
			deformParams0[1] = ds->deformationWave.amplitude;
			deformParams0[2] = ds->deformationWave.phase;
			deformParams0[3] = ds->deformationWave.frequency;
			deformParams1[0] = ds->moveVector[0];
			deformParams1[1] = ds->moveVector[1];
			deformParams1[2] = ds->moveVector[2];

			break;

		case DEFORM_NORMALS:
			*type = DEFORM_NORMALS;

			deformParams0[0] = 0.0f;
			deformParams0[1] = ds->deformationWave.amplitude; // amplitude
			deformParams0[2] = 0.0f;  // phase
			deformParams0[3] = ds->deformationWave.frequency;  // frequency
			deformParams1[0] = 0.0f;
			deformParams1[1] = 0.0f;
			deformParams1[2] = 0.0f;
			break;

		case DEFORM_PROJECTION_SHADOW:
			*type = DEFORM_PROJECTION_SHADOW;
			/*
			deformParams0[0] = backEnd.ori.axis[0][2];
			deformParams0[1] = backEnd.ori.axis[1][2];
			deformParams0[2] = backEnd.ori.axis[2][2];
			deformParams0[3] = backEnd.ori.origin[2] - refEntity->e.shadowPlane;

			vec3_t lightDir;
			VectorCopy(refEntity->modelLightDir, lightDir);
			lightDir[2] = 0.0f;
			VectorNormalize(lightDir);
			VectorSet(lightDir, lightDir[0] * 0.3f, lightDir[1] * 0.3f, 1.0f);

			deformParams1[0] = lightDir[0];
			deformParams1[1] = lightDir[1];
			deformParams1[2] = lightDir[2];*/
			break;

		default:
			break;
		}
	}
}

void RB_AddShaderToShaderInstanceUBO(shader_t *shader)
{
	if (shader->numDeforms != 1 && !shader->portalRange)
	{
		shader->ShaderInstanceUboOffset = -1;
		return;
	}

	ShaderInstanceBlock shaderInstanceBlock = {};
	ComputeDeformValues(
		shader,
		(deform_t *)&shaderInstanceBlock.deformType,
		(genFunc_t *)&shaderInstanceBlock.deformFunc,
		shaderInstanceBlock.deformParams0,
		shaderInstanceBlock.deformParams1);
	shaderInstanceBlock.portalRange = shader->portalRange;
	shaderInstanceBlock.time = -shader->timeOffset;

	shader->ShaderInstanceUboOffset = RB_AddShaderInstanceBlock((void*)&shaderInstanceBlock);
}

static void RB_UpdateEntityConstants(
	gpuFrame_t *frame, const trRefdef_t *refdef)
{
	memset(tr.entityUboOffsets, 0, sizeof(tr.entityUboOffsets));
	for (int i = 0; i < refdef->num_entities; i++)
	{
		trRefEntity_t *ent = &refdef->entities[i];

		R_SetupEntityLighting(refdef, ent);

		EntityBlock entityBlock = {};
		RB_UpdateEntityLightConstants(entityBlock, ent);
		RB_UpdateEntityMatrixConstants(entityBlock, ent);
		RB_UpdateEntityModelConstants(entityBlock, ent);

		tr.entityUboOffsets[i] = RB_AppendConstantsData(
			frame, &entityBlock, sizeof(entityBlock));
	}

	const trRefEntity_t *ent = &tr.worldEntity;
	EntityBlock entityBlock = {};
	RB_UpdateEntityLightConstants(entityBlock, ent);
	RB_UpdateEntityMatrixConstants(entityBlock, ent);
	RB_UpdateEntityModelConstants(entityBlock, ent);

	tr.entityUboOffsets[REFENTITYNUM_WORLD] = RB_AppendConstantsData(
		frame, &entityBlock, sizeof(entityBlock));

	RB_UpdateSkyEntityConstants(frame, refdef);
}

#ifndef REND2_SP
static void RB_UpdateGhoul2Constants(gpuFrame_t *frame, const trRefdef_t *refdef)
{
	for (int i = 0; i < refdef->num_entities; i++)
	{
		const trRefEntity_t *ent = &refdef->entities[i];
		if (ent->e.reType != RT_MODEL)
			continue;

		model_t *model = R_GetModelByHandle(ent->e.hModel);
		if (!model)
			continue;

		switch (model->type)
		{
		case MOD_MDXM:
		case MOD_BAD:
		{
			// Transform Bones and upload them
			RB_TransformBones(ent, refdef, backEndData->realFrameNumber, frame);
		}
		break;

		default:
			break;
		}
	}
}
#endif

void RB_UpdateConstants(const trRefdef_t *refdef)
{
	backEnd.comparisonBaseline = false;
	backEnd.sssFill = false;
	backEnd.softDepthViewParm = -1;
	// Cached view indices are reused for each scene, including empty scenes.
	backEnd.ssaoViewParm = -1;
	backEnd.ssaoWeaponViewParm = -1;
	backEnd.ssaoWeaponReady = false;
	backEnd.ssaoDepthLayer = backEndState_t::DEPTH_ALL;
	gpuFrame_t *frame = backEndData->currentFrame;
	RB_BeginConstantsUpdate(frame);

	RB_UpdateCameraConstants(frame);
	RB_UpdateSceneConstants(frame, refdef);
	RB_UpdateLightsConstants(frame, refdef);
	RB_UpdateFogsConstants(frame);
#ifndef REND2_SP
	RB_UpdateGhoul2Constants(frame, refdef);
#endif
	RB_UpdateEntityConstants(frame, refdef);

	RB_EndConstantsUpdate(frame);
}

/*
=============
RB_DrawBuffer

=============
*/
static const void	*RB_DrawBuffer( const void *data ) {
	const drawBufferCommand_t	*cmd;

	cmd = (const drawBufferCommand_t *)data;

	// finish any 2D drawing if needed
	if(tess.numIndexes)
		RB_EndSurface();

	return (const void *)(cmd + 1);
}

/*
===============
RB_ShowImages

Draw all the images to the screen, on top of whatever
was there.  This is used to test for texture thrashing.

Also called by RE_EndRegistration
===============
*/
void RB_ShowImages( void ) {
	int		i;
	image_t	*image;
	float	x, y, w, h;
	int		start, end;

	RB_SetGL2D();

	qglClear( GL_COLOR_BUFFER_BIT );

	qglFinish();

	start = ri.Milliseconds();

	image = tr.images;
	for ( i=0 ; i < tr.numImages; i++, image = image->poolNext ) {
		w = glConfig.vidWidth / 20;
		h = glConfig.vidHeight / 15;
		x = i % 20 * w;
		y = i / 20 * h;

		// show in proportional size in mode 2
		if ( r_showImages->integer == 2 ) {
			w *= image->uploadWidth / 512.0f;
			h *= image->uploadHeight / 512.0f;
		}

		{
			vec4_t quadVerts[4];

			GL_Bind(image);

			VectorSet4(quadVerts[0], x, y, 0, 1);
			VectorSet4(quadVerts[1], x + w, y, 0, 1);
			VectorSet4(quadVerts[2], x + w, y + h, 0, 1);
			VectorSet4(quadVerts[3], x, y + h, 0, 1);

			RB_InstantQuad(quadVerts);
		}
	}

	qglFinish();

	end = ri.Milliseconds();
	ri.Printf( PRINT_ALL, "%i msec to draw all images\n", end - start );

}

/*
=============
RB_ColorMask

=============
*/
static const void *RB_ColorMask(const void *data)
{
	const colorMaskCommand_t *cmd = (colorMaskCommand_t *)data;

	// finish any 2D drawing if needed
	RB_EndSurface();

	// reverse color mask, so 0 0 0 0 is the default
	backEnd.colorMask[0] = (qboolean)(!cmd->rgba[0]);
	backEnd.colorMask[1] = (qboolean)(!cmd->rgba[1]);
	backEnd.colorMask[2] = (qboolean)(!cmd->rgba[2]);
	backEnd.colorMask[3] = (qboolean)(!cmd->rgba[3]);

	qglColorMask(cmd->rgba[0], cmd->rgba[1], cmd->rgba[2], cmd->rgba[3]);

	return (const void *)(cmd + 1);
}

/*
=============
RB_ClearDepth

=============
*/
static const void *RB_ClearDepth(const void *data)
{
	const clearDepthCommand_t *cmd = (clearDepthCommand_t *)data;

	// finish any 2D drawing if needed
	if(tess.numIndexes)
		RB_EndSurface();

	// texture swapping test
	if (r_showImages->integer)
		RB_ShowImages();

	if (!tr.renderFbo || backEnd.framePostProcessed)
	{
		FBO_Bind(NULL);
	}
	else
	{
		FBO_Bind(tr.renderFbo);
	}

	qglClear(GL_DEPTH_BUFFER_BIT);

	// if we're doing MSAA, clear the depth texture for the resolve buffer
	if (tr.msaaResolveFbo)
	{
		FBO_Bind(tr.msaaResolveFbo);
		qglClear(GL_DEPTH_BUFFER_BIT);
	}


	return (const void *)(cmd + 1);
}


#ifdef REND2_SP
void RB_FlushScreenshot()
{
	if (!backEndData || !backEndData->currentFrame)
		return;
	screenshotCommand_t& screenshot = backEndData->currentFrame->screenshotCommand;
	if (!screenshot.fileName)
		return;

	if (!R_SP_ScreenFBO())
		R_SP_CaptureScreen(qfalse);
	FBO_t *previous = glState.currentFBO;
	FBO_Bind(R_SP_ScreenFBO());
	RB_TakeScreenshotCmd(&screenshot);
	screenshot.fileName = nullptr;
	FBO_Bind(previous);
}
#endif

/*
=============
RB_SwapBuffers

=============
*/
static const void	*RB_SwapBuffers( const void *data ) {
	const swapBuffersCommand_t	*cmd;

	// finish any 2D drawing if needed
	if ( tess.numIndexes ) {
		RB_EndSurface();
	}

	ResetGhoul2RenderableSurfaceHeap();
#ifdef REND2_SP
	R_ReportGhoul2Work();
#endif

	// texture swapping test
	if ( r_showImages->integer ) {
		RB_ShowImages();
	}

	cmd = (const swapBuffersCommand_t *)data;

	// we measure overdraw by reading back the stencil buffer and
	// counting up the number of increments that have happened
	if ( r_measureOverdraw->integer ) {
		int i;
		long sum = 0;
		unsigned char *stencilReadback;

		stencilReadback = (unsigned char *)ri.Hunk_AllocateTempMemory( glConfig.vidWidth * glConfig.vidHeight );
		qglReadPixels( 0, 0, glConfig.vidWidth, glConfig.vidHeight, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, stencilReadback );

		for ( i = 0; i < glConfig.vidWidth * glConfig.vidHeight; i++ ) {
			sum += stencilReadback[i];
		}

		backEnd.pc.c_overDraw += sum;
		ri.Hunk_FreeTempMemory( stencilReadback );
	}

	if (!backEnd.framePostProcessed)
	{
		if (tr.msaaResolveFbo && r_hdr->integer)
		{
			// Resolving an RGB16F MSAA FBO to the screen messes with the brightness, so resolve to an RGB16F FBO first
			FBO_FastBlit(tr.renderFbo, NULL, tr.msaaResolveFbo, NULL, GL_COLOR_BUFFER_BIT, GL_NEAREST);
			FBO_FastBlit(tr.msaaResolveFbo, NULL, NULL, NULL, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		}
		else if (tr.renderFbo)
		{
			FBO_FastBlit(tr.renderFbo, NULL, NULL, NULL, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		}
	}

	if ( tr.numFramesToCapture > 0 )
	{
		tr.numFramesToCapture--;
		if ( !tr.numFramesToCapture )
		{
			ri.Printf( PRINT_ALL, "Frames captured\n" );
			ri.FS_FCloseFile(tr.debugFile);
			tr.debugFile = 0;
		}
	}

#ifdef REND2_SP
	R_SP_CaptureScreen(qtrue);
	RB_FlushScreenshot();
#endif
	R_NewFrameSync();

	GLimp_LogComment( "***************** RB_SwapBuffers *****************\n\n\n" );

	ri.WIN_Present( &window );

	return (const void *)(cmd + 1);
}

/*
=============
RB_PostProcess

=============
*/
static void RB_CreatePostTarget(image_t **image, FBO_t **fbo, const char *name, int format)
{
	*image = R_CreateImage(name, nullptr, glConfig.vidWidth, glConfig.vidHeight, IMGTYPE_COLORALPHA,
		IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE | IMGFLAG_NOLIGHTSCALE, format);
	*fbo = FBO_Create((*image)->imgName, glConfig.vidWidth, glConfig.vidHeight);
	FBO_Bind(*fbo);
	FBO_AttachTextureImage(*image, 0);
	FBO_SetupDrawBuffers();
	R_CheckFBO(*fbo);
}

static void RB_BuildLocalFog()
{
	backEnd.localFogReady = false;
	if (!r_localFog->integer || !tr.world || !tr.world->numLocalFogs || backEnd.comparisonBaseline ||
		(backEnd.refdef.rdflags & RDF_NOWORLDMODEL) ||
		(backEnd.viewParms.flags & (VPF_DEPTHSHADOW | VPF_POINTSHADOW)) ||
		(backEnd.viewParms.isPortal && !backEnd.viewParms.isSkyPortal)) return;
	const trRefdef_t &view = backEnd.refdef;
	const vec2_t fov = {view.fov_x, view.fov_y};
	if (tr.localFogImage && tr.localFogFrame == backEndData->realFrameNumber &&
		!memcmp(tr.localFogViewOrigin, view.vieworg, sizeof(vec3_t)) &&
		!memcmp(tr.localFogViewAxis, view.viewaxis, sizeof(view.viewaxis)) &&
		!memcmp(tr.localFogFov, fov, sizeof(fov)))
	{
		backEnd.localFogReady = true;
		return;
	}
	FBO_t *oldFbo = glState.currentFBO;
	if (!tr.localFogImage)
	{
		tr.localFogImage = R_CreateImage("*localFog", nullptr, 64, 36 * 33, IMGTYPE_COLORALPHA,
			IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE | IMGFLAG_NOLIGHTSCALE, GL_RGBA16F);
		tr.localFogFbo = FBO_Create("*localFog", 64, 36 * 33);
		FBO_Bind(tr.localFogFbo);
		FBO_AttachTextureImage(tr.localFogImage, 0);
		FBO_SetupDrawBuffers();
		R_CheckFBO(tr.localFogFbo);
		ri.Printf(PRINT_ALL, "Local fog grid: 64x36x33, 24 steps, %d volumes\n", tr.world->numLocalFogs);
	}
	FBO_Bind(tr.localFogFbo);
	qglViewport(0, 0, 64, 36 * 33);
	qglScissor(0, 0, 64, 36 * 33);
	GL_State(GLS_DEPTHTEST_DISABLE);
	GL_Cull(CT_TWO_SIDED);
	shaderProgram_t *sp = &tr.localFogShader;
	GLSL_BindProgram(sp);
	const vec4_t info = {0, 0, tanf(DEG2RAD(view.fov_x * 0.5f)), tanf(DEG2RAD(view.fov_y * 0.5f))};
	GLSL_SetUniformVec4(sp, UNIFORM_VIEWINFO, info);
	GLSL_SetUniformVec3(sp, UNIFORM_VIEWORIGIN, view.vieworg);
	GLSL_SetUniformVec3(sp, UNIFORM_VIEWFORWARD, view.viewaxis[0]);
	GLSL_SetUniformVec3(sp, UNIFORM_VIEWLEFT, view.viewaxis[1]);
	GLSL_SetUniformVec3(sp, UNIFORM_VIEWUP, view.viewaxis[2]);
	qglUniform4fv(sp->uniforms[UNIFORM_VOLUMEMINS], 4, tr.world->localFogMins[0]);
	qglUniform4fv(sp->uniforms[UNIFORM_VOLUMEMAXS], 4, tr.world->localFogMaxs[0]);
	qglUniform4fv(sp->uniforms[UNIFORM_VOLUMECOLOR], 4, tr.world->localFogColor[0]);
	vec4_t torch = {};
	if (r_torchShadows->integer && tr.torchShadowImage && view.torchParams[2] > 0)
	{
		VectorCopy4(view.torchOrigin, torch);
		GL_BindToTMU(tr.torchShadowImage, TB_TORCHSHADOWMAP);
		GLSL_SetUniformMatrix4x4(sp, UNIFORM_SHADOWMVP, view.torchVP);
		GLSL_SetUniformVec4(sp, UNIFORM_VOLUMETORCHDIRECTION, view.torchDirection);
		GLSL_SetUniformVec4(sp, UNIFORM_VOLUMETORCHPARAMS, view.torchParams);
	}
	GLSL_SetUniformVec4(sp, UNIFORM_VOLUMETORCHORIGIN, torch);
	RB_InstantTriangle();
	FBO_Bind(oldFbo);
	SetViewportAndScissor();
	Matrix16Identity(tr.localFogMatrix);
	for (int axis = 0; axis < 3; ++axis)
	{
		tr.localFogMatrix[axis*4] = -view.viewaxis[1][axis] / info[2];
		tr.localFogMatrix[axis*4+1] = view.viewaxis[2][axis] / info[3];
		tr.localFogMatrix[axis*4+2] = view.viewaxis[0][axis];
	}
	for (int row = 0; row < 3; ++row)
		tr.localFogMatrix[12+row] = -(view.vieworg[0] * tr.localFogMatrix[row] +
			view.vieworg[1] * tr.localFogMatrix[4+row] + view.vieworg[2] * tr.localFogMatrix[8+row]);
	tr.localFogFrame = backEndData->realFrameNumber;
	VectorCopy(view.vieworg, tr.localFogViewOrigin);
	memcpy(tr.localFogViewAxis, view.viewaxis, sizeof(view.viewaxis));
	VectorCopy2(fov, tr.localFogFov);
	backEnd.localFogReady = true;
}

static FBO_t *RB_SkinDiffusion(FBO_t *scene)
{
	if (backEnd.comparisonBaseline) return scene;
	if ((!r_sssDebug->integer && (!r_sss->value || !r_sssRadius->value)) || !RB_FullMainView()) return scene;
	bool eligible = false;
	for (int i = backEnd.refdef.fistDrawSurf; i < backEnd.refdef.numDrawSurfs && !eligible; ++i)
	{
		int entity, cube, post;
		shader_t *shader;
		R_DecomposeSort(backEnd.refdef.drawSurfs[i].sort, &entity, &shader, &cube, &post);
		eligible = R_IsSkinSurface(shader, backEnd.refdef.drawSurfs[i].surface);
	}
	if (!eligible && !r_sssDebug->integer) return scene;
	if (!tr.sssFbo)
	{
		RB_CreatePostTarget(&tr.sssImage, &tr.sssFbo, "*skinIrradiance", GL_RGBA16F);
		tr.sssAlbedoImage = R_CreateImage("*skinAlbedo", nullptr, glConfig.vidWidth, glConfig.vidHeight,
			IMGTYPE_COLORALPHA, IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE | IMGFLAG_NOLIGHTSCALE, GL_RGBA16F);
		FBO_AttachTextureImage(tr.sssAlbedoImage, 1);
		FBO_SetupDrawBuffers();
		R_CheckFBO(tr.sssFbo);
		for (int i = 0; i < 2; ++i)
			RB_CreatePostTarget(&tr.sssBlurImage[i], &tr.sssBlurFbo[i], va("*skinBlur%d", i), GL_RGBA16F);
		RB_CreatePostTarget(&tr.sssCompositeImage, &tr.sssCompositeFbo, "*skinComposite", GL_RGBA16F);
	}
	FBO_Bind(tr.sssFbo);
	qglViewport(0, 0, glConfig.vidWidth, glConfig.vidHeight);
	qglScissor(0, 0, glConfig.vidWidth, glConfig.vidHeight);
	const vec4_t zero = {};
	qglClearBufferfv(GL_COLOR, 0, zero);
	qglClearBufferfv(GL_COLOR, 1, zero);
	backEnd.sssFill = true;
	RB_RenderDrawSurfList(backEnd.refdef.drawSurfs + backEnd.refdef.fistDrawSurf,
		backEnd.refdef.numDrawSurfs - backEnd.refdef.fistDrawSurf);
	backEnd.sssFill = false;
	GL_State(GLS_DEPTHTEST_DISABLE);
	GL_Cull(CT_TWO_SIDED);
	GLSL_BindProgram(&tr.sssShader);
	const vec4_t info = {backEnd.viewParms.zFar / r_znear->value, backEnd.viewParms.zFar,
		tanf(DEG2RAD(backEnd.viewParms.fovX * 0.5f)), tanf(DEG2RAD(backEnd.viewParms.fovY * 0.5f))};
	GLSL_SetUniformVec4(&tr.sssShader, UNIFORM_VIEWINFO, info);
	GL_BindToTMU(tr.renderDepthImage, 1);
	GL_BindToTMU(tr.sssAlbedoImage, 2);
	GL_BindToTMU(tr.sssImage, 3);
	for (int i = 0; i < 2; ++i)
	{
		FBO_Bind(tr.sssBlurFbo[i]);
		GL_BindToTMU(i ? tr.sssBlurImage[0] : tr.sssImage, 0);
		const vec4_t params = {i ? 0.0f : 1.0f, i ? 1.0f : 0.0f, r_sssRadius->value, 0};
		GLSL_SetUniformVec4(&tr.sssShader, UNIFORM_SSSPARAMS, params);
		RB_InstantTriangle();
	}
	FBO_Bind(tr.sssCompositeFbo);
	GL_BindToTMU(tr.sssBlurImage[1], 0);
	GL_BindToTMU(scene->colorImage[0], 1);
	const vec4_t params = {r_sssDebugGain->value, 0, r_sssRadius->value > 0 ? r_sss->value : 0.0f, 1.0f + r_sssDebug->integer};
	GLSL_SetUniformVec4(&tr.sssShader, UNIFORM_SSSPARAMS, params);
	RB_InstantTriangle();
	return tr.sssCompositeFbo;
}

static void RB_SMAA()
{
	if (backEnd.comparisonBaseline) return;
	if (!r_smaa->integer || !RB_FullMainView() || r_ssaoDebug->integer || r_sssDebug->integer) return;
	if (!tr.smaaFbo[0])
	{
		for (int i = 0; i < 3; ++i)
			RB_CreatePostTarget(&tr.smaaImage[i], &tr.smaaFbo[i], va("*smaa%d", i), GL_RGBA8);
		std::vector<byte> area(AREATEX_WIDTH * AREATEX_HEIGHT * 4, 255);
		for (int i = 0; i < AREATEX_WIDTH * AREATEX_HEIGHT; ++i)
		{ area[4*i] = areaTexBytes[2*i]; area[4*i+1] = areaTexBytes[2*i+1]; }
		tr.smaaAreaImage = R_CreateImage("*smaaArea", area.data(), AREATEX_WIDTH, AREATEX_HEIGHT,
			IMGTYPE_COLORALPHA, IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE | IMGFLAG_NOLIGHTSCALE, GL_RGBA8);
		std::vector<byte> search(SEARCHTEX_WIDTH * SEARCHTEX_HEIGHT * 4, 255);
		for (int i = 0; i < SEARCHTEX_WIDTH * SEARCHTEX_HEIGHT; ++i) search[4*i] = searchTexBytes[i];
		tr.smaaSearchImage = R_CreateImage("*smaaSearch", search.data(), SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT,
			IMGTYPE_COLORALPHA, IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE | IMGFLAG_NOLIGHTSCALE, GL_RGBA8);
	}
	FBO_FastBlit(nullptr, nullptr, tr.smaaFbo[0], nullptr, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	GL_State(GLS_DEPTHTEST_DISABLE);
	GL_Cull(CT_TWO_SIDED);
	qglViewport(0, 0, glConfig.vidWidth, glConfig.vidHeight);
	qglScissor(0, 0, glConfig.vidWidth, glConfig.vidHeight);
	const vec2_t invSize = {1.0f / glConfig.vidWidth, 1.0f / glConfig.vidHeight};
	const vec4_t zero = {};
	for (int i = 0; i < 3; ++i)
	{
		FBO_Bind(i == 2 ? nullptr : tr.smaaFbo[i+1]);
		if (i != 2) qglClearBufferfv(GL_COLOR, 0, zero);
		GLSL_BindProgram(&tr.smaaShader[i]);
		GLSL_SetUniformVec2(&tr.smaaShader[i], UNIFORM_INVTEXRES, invSize);
		GL_BindToTMU(i == 1 ? tr.smaaImage[1] : tr.smaaImage[0], 0);
		GL_BindToTMU(i == 1 ? tr.smaaAreaImage : tr.smaaImage[2], 1);
		GL_BindToTMU(tr.smaaSearchImage, 2);
		RB_InstantTriangle();
	}
	if (r_smaaDebug->integer)
		FBO_FastBlit(tr.smaaFbo[r_smaaDebug->integer], nullptr, nullptr, nullptr, GL_COLOR_BUFFER_BIT, GL_NEAREST);
}

static const void *RB_DrawSurfs(const void *data);

const void *RB_PostProcess(const void *data)
{
	const postProcessCommand_t *cmd = (const postProcessCommand_t *)data;
	FBO_t *srcFbo;
	vec4i_t srcBox, dstBox;
	qboolean autoExposure;

	// finish any 2D drawing if needed
	if(tess.numIndexes)
		RB_EndSurface();

	if (cmd)
	{
		backEnd.refdef = cmd->refdef;
		backEnd.viewParms = cmd->viewParms;
	}

	srcFbo = tr.renderFbo;
	if (tr.msaaResolveFbo)
	{
		// Resolve the MSAA before anything else
		// Can't resolve just part of the MSAA FBO, so multiple views will suffer a performance hit here
		FBO_FastBlit(tr.renderFbo, NULL, tr.msaaResolveFbo, NULL, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
		srcFbo = tr.msaaResolveFbo;

		if ( r_dynamicGlow->integer )
		{
			FBO_FastBlitIndexed(tr.renderFbo, tr.msaaResolveFbo, 1, 1, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
		}
	}

	srcFbo = RB_SkinDiffusion(srcFbo);
	const bool skinDebug = r_sssDebug->integer > 0 && r_sssDebug->integer < 5 && srcFbo == tr.sssCompositeFbo;
	dstBox[0] = backEnd.viewParms.viewportX;
	dstBox[1] = backEnd.viewParms.viewportY;
	dstBox[2] = backEnd.viewParms.viewportWidth;
	dstBox[3] = backEnd.viewParms.viewportHeight;

	if (r_dynamicGlow->integer)
	{
		RB_BloomDownscale(tr.glowImage, tr.glowFboScaled[0]);
		int numPasses = Com_Clampi(1, ARRAY_LEN(tr.glowFboScaled), r_dynamicGlowPasses->integer);
		for ( int i = 1; i < numPasses; i++ )
			RB_BloomDownscale(tr.glowFboScaled[i - 1], tr.glowFboScaled[i]);

		for ( int i = numPasses - 2; i >= 0; i-- )
			RB_BloomUpscale(tr.glowFboScaled[i + 1], tr.glowFboScaled[i]);
	}
	srcBox[0] = backEnd.viewParms.viewportX;
	srcBox[1] = backEnd.viewParms.viewportY;
	srcBox[2] = backEnd.viewParms.viewportWidth;
	srcBox[3] = backEnd.viewParms.viewportHeight;

	if (srcFbo)
	{
		if (!backEnd.comparisonBaseline && r_hdr->integer && (r_toneMap->integer || r_forceToneMap->integer))
		{
			autoExposure = (qboolean)(r_autoExposure->integer || r_forceAutoExposure->integer);
			RB_ToneMap(srcFbo, srcBox, NULL, dstBox, autoExposure);
		}
		else if (backEnd.comparisonBaseline || r_cameraExposure->value == 0.0f)
		{
			FBO_FastBlit(srcFbo, srcBox, NULL, dstBox, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		}
		else
		{
			vec4_t color;

			color[0] =
			color[1] =
			color[2] = pow(2, r_cameraExposure->value); //exp2(r_cameraExposure->value);
			color[3] = 1.0f;

			FBO_Blit(srcFbo, srcBox, NULL, NULL, dstBox, NULL, color, 0);
		}

		// Copy depth buffer to the backbuffer for depth culling refractive surfaces
		FBO_FastBlit(tr.renderFbo, srcBox, NULL, dstBox, GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT, GL_NEAREST);
	}

	if (!backEnd.comparisonBaseline && r_drawSunRays->integer)
		RB_SunRays(NULL, srcBox, NULL, dstBox);

#if 0
	if (backEnd.refdef.blurFactor > 0.0f)
		RB_BokehBlur(NULL, srcBox, NULL, dstBox, backEnd.refdef.blurFactor);
#endif

	if (r_debugWeather->integer == 2)
	{
		FBO_BlitFromTexture(tr.weatherDepthImage, NULL, NULL, NULL, nullptr, NULL, NULL, 0);
	}

	if (0)
	{
		vec4i_t dstBox;
		VectorSet4(dstBox, 256, glConfig.vidHeight - 256, 256, 256);
		FBO_BlitFromTexture(tr.renderDepthImage, NULL, NULL, NULL, dstBox, NULL, NULL, 0);
		VectorSet4(dstBox, 512, glConfig.vidHeight - 256, 256, 256);
		FBO_BlitFromTexture(tr.screenShadowImage, NULL, NULL, NULL, dstBox, NULL, NULL, 0);
	}

	if (0)
	{
		vec4i_t dstBox;
		VectorSet4(dstBox, 256, glConfig.vidHeight - 256, 256, 256);
		FBO_BlitFromTexture(tr.sunRaysImage, NULL, NULL, NULL, dstBox, NULL, NULL, 0);
	}

#if 0
	if (r_cubeMapping->integer && tr.numCubemaps)
	{
		vec4i_t dstBox;
		int cubemapIndex = R_CubemapForPoint( backEnd.viewParms.ori.origin );

		if (cubemapIndex)
		{
			VectorSet4(dstBox, 0, glConfig.vidHeight - 256, 256, 256);
			//FBO_BlitFromTexture(tr.renderCubeImage, NULL, NULL, NULL, dstBox, &tr.testcubeShader, NULL, 0);
			FBO_BlitFromTexture(tr.cubemaps[cubemapIndex - 1], NULL, NULL, NULL, dstBox, &tr.testcubeShader, NULL, 0);
		}
	}
#endif

#ifdef REND2_SP
	if (tr_distortionPrePost)
		R_SP_CaptureScreen(qtrue);
#endif
	if (!backEnd.comparisonBaseline && r_dynamicGlow->integer != 0)
	{
		// Composite the glow/bloom texture
		int blendFunc = 0;
		vec4_t color = { 1.0f, 1.0f, 1.0f, 1.0f };

		if ( r_dynamicGlow->integer == 2 )
		{
			// Debug output
			blendFunc = GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO;
		}
		else if ( r_dynamicGlowSoft->integer )
		{
			blendFunc = GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE_MINUS_SRC_COLOR;
			color[0] = color[1] = color[2] = r_dynamicGlowIntensity->value;
		}
		else
		{
			blendFunc = GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE;
			color[0] = color[1] = color[2] = r_dynamicGlowIntensity->value;
		}

		FBO_BlitFromTexture (tr.glowFboScaled[0]->colorImage[0], NULL, NULL, NULL, NULL, NULL, color, blendFunc);
	}

	backEnd.framePostProcessed = qtrue;
	FBO_Bind(NULL);
#ifdef REND2_SP
	if (!tr_distortionPrePost)
		R_SP_CaptureScreen(qtrue);
#endif
	backEnd.refractionFill = qtrue;
	RB_RenderDrawSurfList(
		backEnd.refdef.drawSurfs + backEnd.refdef.fistDrawSurf,
		backEnd.refdef.numDrawSurfs - tr.refdef.fistDrawSurf);
	backEnd.refractionFill = qfalse;
#ifdef REND2_SP
	if (backEnd.refdef.rdflags & RDF_doLAGoggles)
		R_SP_DrawGoggles();
#endif

	// Diagnostic values must not be tone mapped or covered by scene effects.
	if (skinDebug)
		FBO_FastBlit(tr.sssCompositeFbo, nullptr, nullptr, nullptr, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	RB_SMAA();
	if (RB_SSAOEnabledForView() &&
		backEnd.ssaoViewParm == backEnd.viewParms.currentViewParm &&
		(r_ssaoDebug->integer >= 1 && r_ssaoDebug->integer <= 4))
	{
		FBO_t *aoFbo = r_ssaoDebug->integer == 1 ? tr.ssaoRawFbo : tr.screenSsaoFbo;
		if (r_ssaoDebug->integer == 4)
			aoFbo = backEnd.ssaoWeaponReady ? tr.weaponSsaoFbo : nullptr;
		FBO_t *oldFbo = glState.currentFBO;
		const uint32_t oldState = glState.glStateBits;
		const int oldCull = glState.faceCulling;
		GLint viewport[4], scissor[4];
		qglGetIntegerv(GL_VIEWPORT, viewport);
		qglGetIntegerv(GL_SCISSOR_BOX, scissor);
		if (r_ssaoDebug->integer == 3)
		{
			FBO_Bind(nullptr);
			qglViewport(0, 0, glConfig.vidWidth, glConfig.vidHeight);
			qglScissor(dstBox[0], dstBox[1], dstBox[2], dstBox[3]);
			GL_State(GLS_DEPTHTEST_DISABLE);
			GL_Cull(CT_TWO_SIDED);
			GLSL_BindProgram(&tr.ssaoShader);
			GL_BindToTMU(backEnd.ssaoWeaponViewParm == backEnd.viewParms.currentViewParm
				? tr.weaponDepthImage : tr.whiteImage, TB_COLORMAP);
			GLSL_SetUniformInt(&tr.ssaoShader, UNIFORM_SSAODEBUG, 1);
			RB_InstantTriangle();
			GLSL_SetUniformInt(&tr.ssaoShader, UNIFORM_SSAODEBUG, 0);
		}
		else if (aoFbo)
		{
			srcBox[0] = dstBox[0] * aoFbo->width / glConfig.vidWidth;
			srcBox[1] = dstBox[1] * aoFbo->height / glConfig.vidHeight;
			srcBox[2] = dstBox[2] * aoFbo->width / glConfig.vidWidth;
			srcBox[3] = dstBox[3] * aoFbo->height / glConfig.vidHeight;
			FBO_Blit(aoFbo, srcBox, NULL, NULL, dstBox, NULL, NULL, 0);
		}
		else
			FBO_BlitFromTexture(tr.whiteImage, nullptr, nullptr, nullptr, dstBox, nullptr, nullptr, 0);
		FBO_Bind(oldFbo);
		GL_State(oldState);
		GL_Cull(oldCull);
		qglViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
		qglScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
	}

	if (!backEnd.comparisonBaseline && r_localFog->integer == 2 && backEnd.localFogReady)
		FBO_Blit(tr.localFogFbo, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0);
	if (!backEnd.comparisonBaseline && r_compareEnhancements->integer == 2 && RB_CompareView())
	{
		// Preserve this frame's completed enhanced image before replaying its draw list.
		if (!tr.comparisonFbo)
			RB_CreatePostTarget(&tr.comparisonImage, &tr.comparisonFbo, "*enhancementComparison", GL_RGBA8);
		FBO_FastBlit(nullptr, nullptr, tr.comparisonFbo, nullptr, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		drawSurfsCommand_t draw = {};
		draw.refdef = backEnd.refdef;
		draw.viewParms = backEnd.viewParms;
		draw.drawSurfs = backEnd.refdef.drawSurfs + backEnd.refdef.fistDrawSurf;
		draw.numDrawSurfs = backEnd.refdef.numDrawSurfs - backEnd.refdef.fistDrawSurf;
		postProcessCommand_t post = {};
		post.refdef = backEnd.refdef;
		post.viewParms = backEnd.viewParms;
		backEnd.comparisonBaseline = true;
		if (backEnd.comparisonSky && backEnd.comparisonSkyFrame == backEndData->realFrameNumber)
			RB_DrawSurfs(backEnd.comparisonSky);
		RB_DrawSurfs(&draw);
		RB_PostProcess(&post);
		const int half = glConfig.vidWidth / 2;
		vec4i_t right = {half, 0, glConfig.vidWidth - half, glConfig.vidHeight};
		FBO_FastBlit(tr.comparisonFbo, right, nullptr, right, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	}
	backEnd.comparisonBaseline = false;
	backEnd.comparisonSky = nullptr;
	return (const void *)(cmd + 1);
}

static const void *RB_BeginTimedBlock( const void *data )
{
	const beginTimedBlockCommand_t *cmd = (const beginTimedBlockCommand_t *)data;
	if ( glRefConfig.timerQuery )
	{
		gpuFrame_t *currentFrame = &backEndData->frames[backEndData->realFrameNumber % MAX_FRAMES];
		gpuTimer_t *timer = currentFrame->timers + currentFrame->numTimers++;

		if ( cmd->timerHandle >= 0 && currentFrame->numTimers <= MAX_GPU_TIMERS )
		{
			gpuTimedBlock_t *timedBlock = currentFrame->timedBlocks + cmd->timerHandle;
			timedBlock->beginTimer = timer->queryName;
			timedBlock->name = cmd->name;

			currentFrame->numTimedBlocks++;

			qglQueryCounter( timer->queryName, GL_TIMESTAMP );
		}
	}

	return (const void *)(cmd + 1);
}

static const void *RB_EndTimedBlock( const void *data )
{
	const endTimedBlockCommand_t *cmd = (const endTimedBlockCommand_t *)data;
	if ( glRefConfig.timerQuery )
	{
		gpuFrame_t *currentFrame = &backEndData->frames[backEndData->realFrameNumber % MAX_FRAMES];
		gpuTimer_t *timer = currentFrame->timers + currentFrame->numTimers++;

		if ( cmd->timerHandle >= 0 && currentFrame->numTimers <= MAX_GPU_TIMERS )
		{
			gpuTimedBlock_t *timedBlock = currentFrame->timedBlocks + cmd->timerHandle;
			timedBlock->endTimer = timer->queryName;

			qglQueryCounter( timer->queryName, GL_TIMESTAMP );
		}
	}

	return (const void *)(cmd + 1);
}


/*
=============
RB_DrawSurfs

=============
*/
static const void *RB_DrawSurfs(const void *data) {
	const drawSurfsCommand_t	*cmd;

	// finish any 2D drawing if needed
	if (tess.numIndexes) {
		RB_EndSurface();
	}

	cmd = (const drawSurfsCommand_t *)data;
	backEnd.softDepthViewParm = -1;
	backEnd.localFogReady = false;

	backEnd.ssaoViewParm = -1;
	backEnd.ssaoWeaponViewParm = -1;
	backEnd.ssaoWeaponReady = false;
	backEnd.ssaoDepthLayer = backEndState_t::DEPTH_ALL;
	backEnd.refdef = cmd->refdef;
	backEnd.viewParms = cmd->viewParms;

	// clear the z buffer, set the modelview, etc
	if (!backEnd.comparisonBaseline)
		backEnd.comparisonBaseline = r_compareEnhancements->integer == 1 && RB_CompareView(true);
	if (!backEnd.comparisonBaseline && r_compareEnhancements->integer == 2 &&
		backEnd.viewParms.isSkyPortal && RB_CompareView(true) && cmd->numDrawSurfs > 0)
	{
		// The frontend reuses its surface list for the main view. Preserve the
		// portal list in this frame's arena so comparison can replay both views.
		Allocator &allocator = *backEndData->perFrameMemory;
		backEnd.comparisonSky = ojkAllocArray<drawSurfsCommand_t>(allocator, 1);
		*backEnd.comparisonSky = *cmd;
		backEnd.comparisonSky->drawSurfs = ojkAllocArray<drawSurf_t>(allocator, cmd->numDrawSurfs);
		memcpy(backEnd.comparisonSky->drawSurfs, cmd->drawSurfs, cmd->numDrawSurfs * sizeof(drawSurf_t));
		backEnd.comparisonSky->refdef.drawSurfs = backEnd.comparisonSky->drawSurfs;
		backEnd.comparisonSky->refdef.fistDrawSurf = 0;
		backEnd.comparisonSky->refdef.numDrawSurfs = cmd->numDrawSurfs;
		backEnd.comparisonSkyFrame = backEndData->realFrameNumber;
	}
	RB_BeginDrawingView();

	if (cmd->numDrawSurfs > 0)
	{
		RB_RenderAllDepthRelatedPasses(cmd->drawSurfs, cmd->numDrawSurfs);
		RB_BuildLocalFog();

		RB_RenderMainPass(cmd->drawSurfs, cmd->numDrawSurfs);
	}

	return (const void *)(cmd + 1);
}


/*
====================
RB_ExecuteRenderCommands
====================
*/
void RB_ExecuteRenderCommands( const void *data ) {
	int		t1, t2;

	t1 = ri.Milliseconds ();

	while ( 1 ) {
		data = PADP(data, sizeof(void *));

		switch ( *(const int *)data ) {
		case RC_SET_COLOR:
			data = RB_SetColor( data );
			break;
		case RC_STRETCH_PIC:
			data = RB_StretchPic( data );
			break;
#ifdef REND2_SP
		case RC_UI_GEOMETRY:
			data = RB_DrawUiGeometry( data );
			break;
#endif
		case RC_ROTATE_PIC:
			data = RB_RotatePic( data );
			break;
		case RC_ROTATE_PIC2:
			data = RB_RotatePic2( data );
			break;
		case RC_DRAW_SURFS:
			data = RB_DrawSurfs( data );
			break;
		case RC_DRAW_BUFFER:
			data = RB_DrawBuffer( data );
			break;
		case RC_SWAP_BUFFERS:
			data = RB_SwapBuffers( data );
			break;
		case RC_SCREENSHOT:
#ifdef REND2_SP
		{
			// Console captures can precede the scene. Read the completed frame at swap.
			const screenshotCommand_t *cmd = (const screenshotCommand_t *)data;
			backEndData->currentFrame->screenshotCommand = *cmd;
			data = cmd + 1;
		}
			break;
#else
			data = RB_TakeScreenshotCmd( data );
			break;
#endif
		case RC_VIDEOFRAME:
			data = RB_TakeVideoFrameCmd( data );
			break;
		case RC_COLORMASK:
			data = RB_ColorMask(data);
			break;
		case RC_CLEARDEPTH:
			data = RB_ClearDepth(data);
			break;
		case RC_CONVOLVECUBEMAP:
			data = RB_PrefilterEnvMap( data );
			break;
		case RC_POSTPROCESS:
			data = RB_PostProcess(data);
			break;
		case RC_BEGIN_TIMED_BLOCK:
			data = RB_BeginTimedBlock(data);
			break;
		case RC_END_TIMED_BLOCK:
			data = RB_EndTimedBlock(data);
			break;
		case RC_END_OF_LIST:
		default:
			// finish any 2D drawing if needed
			if(tess.numIndexes)
				RB_EndSurface();

			// stop rendering
			t2 = ri.Milliseconds ();
			backEnd.pc.msec = t2 - t1;
			return;
		}
	}

}
