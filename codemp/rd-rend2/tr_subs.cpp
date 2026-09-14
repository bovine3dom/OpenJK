/*
===========================================================================
Copyright (C) 2010 James Canete (use.less01@gmail.com)

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
// tr_subs.c - common function replacements for modular renderer

#include "tr_local.h"

#ifdef REND2_SP
#include <climits>

namespace
{
struct SPAllocation
{
	SPAllocation *next;
	void *memory;
};

SPAllocation *rendererAllocations;
SPAllocation *worldAllocations;
SPAllocation *tempAllocations;

void *SP_Alloc(SPAllocation *&allocations, size_t size, qboolean zeroIt)
{
	if (size > INT_MAX - sizeof(SPAllocation) - 64)
		ri.Error(ERR_DROP, "rdsp-rend2: allocation too large (%zu bytes)", size);

	// The engine frees TAG_HUNKALLOC before it calls the renderer on map changes.
	// Keep these blocks on a separate list so GPU cleanup can run first.
	auto *block = static_cast<SPAllocation *>(ri.Malloc(
		static_cast<int>(sizeof(SPAllocation) + size + 64), TAG_GENERAL, zeroIt, 4));
	// SP's Malloc import ignores its alignment argument.
	block->memory = reinterpret_cast<void *>((reinterpret_cast<uintptr_t>(block + 1) + 63) & ~uintptr_t(63));
	block->next = allocations;
	allocations = block;
	return block->memory;
}

void SP_FreeAllocations(SPAllocation *&allocations)
{
	while (allocations)
	{
		SPAllocation *block = allocations;
		allocations = block->next;
		ri.Z_Free(block);
	}
}
}

void *R_SP_RendererAlloc(size_t size, qboolean zeroIt)
{
	return SP_Alloc(rendererAllocations, size, zeroIt);
}

void *R_SP_WorldAlloc(int size)
{
	return SP_Alloc(worldAllocations, size, qtrue);
}

void *R_SP_TempAlloc(size_t size)
{
	return SP_Alloc(tempAllocations, size, qfalse);
}

void R_SP_FreeTemp(void *memory)
{
	if (!memory)
		return;
	for (SPAllocation **link = &tempAllocations; *link; link = &(*link)->next)
	{
		SPAllocation *block = *link;
		if (block->memory == memory)
		{
			*link = block->next;
			ri.Z_Free(block);
			return;
		}
	}
	ri.Error(ERR_DROP, "rdsp-rend2: invalid temporary allocation");
}

void R_SP_ClearWorldAllocations()
{
	SP_FreeAllocations(worldAllocations);
}

void R_SP_ClearRendererAllocations()
{
	R_SP_ClearWorldAllocations();
	SP_FreeAllocations(tempAllocations);
	SP_FreeAllocations(rendererAllocations);
}

void Com_DPrintf(const char *format, ...)
{
	va_list args;
	char text[1024];
	va_start(args, format);
	Q_vsnprintf(text, sizeof(text), format, args);
	va_end(args);
	ri.Printf(PRINT_DEVELOPER, "%s", text);
}

void *R_Malloc(int size, memtag_t tag, qboolean zeroIt) { return ri.Malloc(size, tag, zeroIt, 4); }
void R_Free(void *memory) { ri.Z_Free(memory); }
int R_MemSize(memtag_t tag) { return ri.Z_MemSize(tag); }
void R_MorphMallocTag(void *memory, memtag_t tag) { ri.Z_MorphMallocTag(memory, tag); }
void *R_Hunk_Alloc(int size, qboolean zeroIt) { return R_SP_RendererAlloc(size, zeroIt); }
#endif


void QDECL Com_Printf( const char *msg, ... )
{
	va_list         argptr;
	char            text[1024];

	va_start(argptr, msg);
	Q_vsnprintf(text, sizeof(text), msg, argptr);
	va_end(argptr);

	ri.Printf(PRINT_ALL, "%s", text);
}

#ifndef REND2_SP
void QDECL Com_OPrintf( const char *msg, ... )
{
	va_list         argptr;
	char            text[1024];

	va_start(argptr, msg);
	Q_vsnprintf(text, sizeof(text), msg, argptr);
	va_end(argptr);

	ri.OPrintf("%s", text);
}
#endif

void QDECL Com_Error( int level, const char *error, ... )
{
	va_list         argptr;
	char            text[1024];

	va_start(argptr, error);
	Q_vsnprintf(text, sizeof(text), error, argptr);
	va_end(argptr);

	ri.Error(level, "%s", text);
}

// HUNK
#ifndef REND2_SP
void *Hunk_AllocateTempMemory( int size ) {
	return ri.Hunk_AllocateTempMemory( size );
}

void Hunk_FreeTempMemory( void *buf ) {
	ri.Hunk_FreeTempMemory( buf );
}

void *Hunk_Alloc( int size, ha_pref preference ) {
	return ri.Hunk_Alloc( size, preference );
}

int Hunk_MemoryRemaining( void ) {
	return ri.Hunk_MemoryRemaining();
}
#endif

// ZONE
void *Z_Malloc( int iSize, memtag_t eTag, qboolean bZeroit, int iAlign ) {
#ifdef REND2_SP
	return ri.Malloc( iSize, eTag, bZeroit, iAlign );
#else
	return ri.Z_Malloc( iSize, eTag, bZeroit, iAlign );
#endif
}

#ifdef REND2_SP
int Z_Free( void *ptr ) {
	return ri.Z_Free( ptr );
}
#else
void Z_Free( void *ptr ) {
	ri.Z_Free( ptr );
}
#endif

int Z_MemSize( memtag_t eTag ) {
	return ri.Z_MemSize( eTag );
}

void Z_MorphMallocTag( void *pvBuffer, memtag_t eDesiredTag ) {
	ri.Z_MorphMallocTag( pvBuffer, eDesiredTag );
}
