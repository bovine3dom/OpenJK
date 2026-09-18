#pragma once

#include "qcommon/q_shared.h"
#include "rd-common/tr_public.h"

enum ha_pref { h_high, h_low, h_dontcare };
constexpr int CVAR_NONE = 0;
constexpr memtag_t TAG_GENERAL = TAG_R_TERRAIN;
constexpr memtag_t TAG_GRIDMESH = TAG_R_TERRAIN;

void *R_SP_RendererAlloc(size_t size, qboolean zeroIt = qtrue);
void *R_SP_WorldAlloc(int size);
void *R_SP_TempAlloc(size_t size);
void R_SP_FreeTemp(void *memory);
// Release GPU resources before these allocations.
void R_SP_ClearWorldAllocations();
void R_SP_ClearRendererAllocations();

// Only private renderer code uses this facade. The engine ABI stays unchanged.
struct Rend2Imports : refimport_t
{
	Rend2Imports() = default;
	explicit Rend2Imports(const refimport_t &imports) : refimport_t(imports) {}

	cvar_t *Cvar_Get(const char *name, const char *value, int flags, const char *description = nullptr) const
	{
		cvar_t *cvar = refimport_t::Cvar_Get(name, value, flags);
		if (description && description[0]) refimport_t::Cvar_SetDescription(name, description);
		return cvar;
	}

	void Cmd_AddCommand(const char *name, xcommand_t command, const char * = nullptr) const
	{
		refimport_t::Cmd_AddCommand(name, command);
	}

	int CIN_PlayCinematic(const char *name, int x, int y, int width, int height,
		int bits, const char *audioFile = nullptr) const
	{
		return refimport_t::CIN_PlayCinematic(name, x, y, width, height, bits, audioFile);
	}

	int FS_FileIsInPAK(const char *name, int *checksum = nullptr) const
	{
		// SP provides membership, not a checksum. SP must not use MP pure checks.
		if (checksum)
			*checksum = -1;
		return refimport_t::FS_FileIsInPAK(name);
	}

	void *Hunk_Alloc(size_t size, ha_pref = h_low) const { return R_SP_RendererAlloc(size); }
	int Z_Free(void *memory) const { return memory ? refimport_t::Z_Free(memory) : 0; }
	void *Hunk_AllocateTempMemory(size_t size) const { return R_SP_TempAlloc(size); }
	void Hunk_FreeTempMemory(void *memory) const { R_SP_FreeTemp(memory); }
	qboolean Sys_LowPhysicalMemory() const { return LowPhysicalMemory(); }
};

extern Rend2Imports riRend2;

inline void *Hunk_Alloc(int size, qboolean zeroIt)
{
	return R_SP_RendererAlloc(size, zeroIt);
}

inline void *Hunk_Alloc(int size, ha_pref)
{
	return R_SP_RendererAlloc(size);
}
