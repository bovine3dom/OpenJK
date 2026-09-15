/*
===========================================================================
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2013 - 2015, OpenJK contributors

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

//g_objectives.cpp
//reads in ext_data\objectives.dat to objectives[]

#include "g_local.h"
#include "g_items.h"

#define	G_OBJECTIVES_CPP

#include "objectives.h"
#include "qcommon/ojk_saved_game_helper.h"

qboolean	missionInfo_Updated;

stringID_table_t *objectiveTable = academyObjectiveTable;
int objectiveCount = MAX_OBJECTIVES;

void OBJ_InitCampaign(void)
{
	objectiveTable = academyObjectiveTable;
	objectiveCount = MAX_OBJECTIVES;
	if (!G_IsOutcast()) return;

	// Slot zero remains reserved for JA's light-side state. Keep the save layout.
	static stringID_table_t outcastObjectives[MAX_MISSION_OBJ + 1];
	static char names[MAX_MISSION_OBJ][MAX_QPATH];
	memset(outcastObjectives, 0, sizeof(outcastObjectives));
	outcastObjectives[0].name = "LIGHTSIDE_OBJ";
	objectiveCount = 1;
	char *buffer = nullptr;
	if (gi.FS_ReadFile("ext_data/jo/objectives.dat", (void **)&buffer) <= 0)
		gi.Error(ERR_DROP, "Missing JO objective data; run import-jo.py");
	const char *cursor = buffer;
	COM_BeginParseSession();
	while (const char *token = COM_ParseExt(&cursor, qtrue))
	{
		if (!token[0]) break;
		if (objectiveCount >= MAX_MISSION_OBJ || strlen(token) >= MAX_QPATH)
			gi.Error(ERR_DROP, "Invalid JO objective data");
		Q_strncpyz(names[objectiveCount], token, MAX_QPATH);
		outcastObjectives[objectiveCount].name = names[objectiveCount];
		outcastObjectives[objectiveCount].id = objectiveCount;
		++objectiveCount;
	}
	COM_EndParseSession();
	gi.FS_FreeFile(buffer);
	outcastObjectives[objectiveCount].name = "";
	objectiveTable = outcastObjectives;
}


/*
============
OBJ_SetPendingObjectives
============
*/
void OBJ_SetPendingObjectives(gentity_t *ent)
{
	int i;

	for (i=0;i<objectiveCount;++i)
	{
		if ((ent->client->sess.mission_objectives[i].status == OBJECTIVE_STAT_PENDING) &&
			(ent->client->sess.mission_objectives[i].display))
		{
			ent->client->sess.mission_objectives[i].status = OBJECTIVE_STAT_FAILED;
		}
	}
}

/*
============
OBJ_SaveMissionObjectives
============
*/
void OBJ_SaveMissionObjectives( gclient_t *client )
{
	ojk::SavedGameHelper saved_game(
		::gi.saved_game);

	saved_game.write_chunk(
		INT_ID('O', 'B', 'J', 'T'),
		client->sess.mission_objectives);
}


/*
============
OBJ_SaveObjectiveData
============
*/
void OBJ_SaveObjectiveData(void)
{
	gclient_t *client;

	client = &level.clients[0];

	OBJ_SaveMissionObjectives( client );
}

/*
============
OBJ_LoadMissionObjectives
============
*/
void OBJ_LoadMissionObjectives( gclient_t *client )
{
	ojk::SavedGameHelper saved_game(
		::gi.saved_game);

	saved_game.read_chunk(
		INT_ID('O', 'B', 'J', 'T'),
		client->sess.mission_objectives);
}


/*
============
OBJ_LoadObjectiveData
============
*/
void OBJ_LoadObjectiveData(void)
{
	gclient_t *client;

	client = &level.clients[0];

	OBJ_LoadMissionObjectives( client );
}
