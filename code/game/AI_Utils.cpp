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

// These utilities are meant for strictly non-player, non-team NPCs.
// These functions are in their own file because they are only intended
// for use with NPCs who's logic has been overriden from the original
// AI code, and who's code resides in files with the AI_ prefix.

#include "b_local.h"
#include "g_nav.h"
#include "g_navigator.h"

#define	MAX_RADIUS_ENTS		128
#define	DEFAULT_RADIUS		45

extern cvar_t		*d_noGroupAI;
extern qboolean Q3_TaskIDPending( gentity_t *ent, taskID_t taskType );

qboolean AI_LocalGroupContact( gentity_t *source, gentity_t *recipient )
{
	if ( !source || !recipient || !source->inuse || !recipient->inuse
		|| !source->NPC || !recipient->NPC || !source->client || !recipient->client
		|| source->health <= 0 || recipient->health <= 0 || source->client->playerTeam != recipient->client->playerTeam )
		return qfalse;
	float distance = DistanceSquared( source->currentOrigin, recipient->currentOrigin );
	if ( distance > 512*512 )
		return qfalse;
	int from = NAV::GetNearestNode( source, true ), to = NAV::GetNearestNode( recipient, true );
	if ( from == WAYPOINT_NONE || to == WAYPOINT_NONE || !NAV::InSameRegion( source, recipient )
		|| !NAV::InSameRegion( recipient, source ) )
		return qfalse;
	qboolean los = G_ClearLOS( source, recipient );
	// Edge handles can span a door. Do not use them for the short sound route.
	return (los || (distance <= 256*256 && from > 0 && to > 0 && NAV::OnNeighboringPoints( from, to ))) ? qtrue : qfalse;
}

/*
-------------------------
AI_GetGroupSize
-------------------------
*/

int	AI_GetGroupSize( vec3_t origin, int radius, team_t playerTeam, gentity_t *avoid )
{
	gentity_t	*radiusEnts[ MAX_RADIUS_ENTS ];
	vec3_t		mins, maxs;
	int			numEnts, realCount = 0;

	//Setup the bbox to search in
	for ( int i = 0; i < 3; i++ )
	{
		mins[i] = origin[i] - radius;
		maxs[i] = origin[i] + radius;
	}

	//Get the number of entities in a given space
	numEnts = gi.EntitiesInBox( mins, maxs, radiusEnts, MAX_RADIUS_ENTS );

	//Cull this list
	for ( int j = 0; j < numEnts; j++ )
	{
		//Validate clients
		if ( radiusEnts[ j ]->client == NULL )
			continue;

		//Skip the requested avoid ent if present
		if ( ( avoid != NULL ) && ( radiusEnts[ j ] == avoid ) )
			continue;

		//Must be on the same team
		if ( radiusEnts[ j ]->client->playerTeam != playerTeam )
			continue;

		//Must be alive
		if ( radiusEnts[ j ]->health <= 0 )
			continue;

		realCount++;
	}

	return realCount;
}

//Overload

int AI_GetGroupSize( gentity_t *ent, int radius )
{
	if ( ( ent == NULL ) || ( ent->client == NULL ) )
		return -1;

	return AI_GetGroupSize( ent->currentOrigin, radius, ent->client->playerTeam, ent );
}

void AI_SetClosestBuddy( AIGroupInfo_t *group )
{
	int	i, j;
	int	dist, bestDist;

	for ( i = 0; i < group->numGroup; i++ )
	{
		group->member[i].closestBuddy = ENTITYNUM_NONE;

		bestDist = Q3_INFINITE;
		for ( j = 0; j < group->numGroup; j++ )
		{
			dist = DistanceSquared( g_entities[group->member[i].number].currentOrigin, g_entities[group->member[j].number].currentOrigin );
			if ( dist < bestDist )
			{
				bestDist = dist;
				group->member[i].closestBuddy = group->member[j].number;
			}
		}
	}
}

void AI_SortGroupByPathCostToEnemy( AIGroupInfo_t *group )
{
	AIGroupMember_t bestMembers[MAX_GROUP_MEMBERS];
	int				i, j, k;
	qboolean		sort = qfalse;

	if ( group->enemy && group->lastSeenEnemyTime > 0 && group->lastSeenEnemyTime <= level.time )
	{
		group->enemyWP = NAV::GetNearestNode( group->enemyLastSeenPos );
	}
	else
	{
		group->enemyWP = WAYPOINT_NONE;
	}

	for ( i = 0; i < group->numGroup; i++ )
	{
		group->member[i].waypoint = NAV::GetNearestNode( &g_entities[group->member[i].number] );
		if ( group->enemyWP == WAYPOINT_NONE || group->member[i].waypoint == WAYPOINT_NONE )
		{
			group->member[i].pathCostToEnemy = Q3_INFINITE;
		}
		else
		{
			group->member[i].pathCostToEnemy = NAV::EstimateCostToGoal( group->member[i].waypoint, group->enemyWP );
			sort = qtrue;
		}
	}
	//Now sort
	if ( sort )
	{
		//initialize bestMembers data
		for ( j = 0; j < group->numGroup; j++ )
		{
			bestMembers[j].number = ENTITYNUM_NONE;
		}

		for ( i = 0; i < group->numGroup; i++ )
		{
			for ( j = 0; j < group->numGroup; j++ )
			{
				if ( bestMembers[j].number != ENTITYNUM_NONE )
				{//slot occupied
					if ( group->member[i].pathCostToEnemy < bestMembers[j].pathCostToEnemy )
					{//this guy has a shorter path than the one currenly in this spot, bump him and put myself in here
						for ( k = i; k > j; k-- )
						{
							memcpy( &bestMembers[k], &bestMembers[k-1], sizeof( bestMembers[k] ) );
						}
						memcpy( &bestMembers[j], &group->member[i], sizeof( bestMembers[j] ) );
						break;
					}
				}
				else
				{//slot unoccupied, reached end of list, throw self in here
					memcpy( &bestMembers[j], &group->member[i], sizeof( bestMembers[j] ) );
					break;
				}
			}
		}

		//Okay, now bestMembers is a sorted list, just copy it into group->members
		for ( i = 0; i < group->numGroup; i++ )
		{
			memcpy( &group->member[i], &bestMembers[i], sizeof( group->member[i] ) );
		}
	}
}

qboolean AI_FindSelfInPreviousGroup( gentity_t *self )
{//go through other groups made this frame and see if any of those contain me already
	int	i, j;
	for ( i = 0; i < MAX_FRAME_GROUPS; i++ )
	{
		if ( level.groups[i].numGroup )//&& level.groups[i].enemy != NULL )
		{//check this one
			for ( j = 0; j < level.groups[i].numGroup; j++ )
			{
				if ( level.groups[i].member[j].number == self->s.number )
				{
					self->NPC->group = &level.groups[i];
					return qtrue;
				}
			}
		}
	}
	return qfalse;
}

void AI_InsertGroupMember( AIGroupInfo_t *group, gentity_t *member )
{
	int i;
	//okay, you know what?  Check this damn group and make sure we're not already in here!
	for ( i = 0; i < group->numGroup; i++ )
	{
		if ( group->member[i].number == member->s.number )
		{//already in here
			break;
		}
	}
	if ( i < group->numGroup )
	{//found him in group already
	}
	else
	{//add him in
		group->member[group->numGroup++].number = member->s.number;
		group->numState[member->NPC->squadState]++;
		Debug_Printf( debugNPCAI, DEBUG_LEVEL_INFO, "squad event=group_insert group=%d ent=%d members=%d state=%d\n", (int)(group-level.groups), member->s.number, group->numGroup, member->NPC->squadState );
	}
	if ( !group->commander || (member->NPC->rank > group->commander->NPC->rank) )
	{//keep track of highest rank
		group->commander = member;
	}
	member->NPC->group = group;
}

qboolean AI_TryJoinPreviousGroup( gentity_t *self )
{//go through other groups made this frame and see if any of those have the same enemy as me... if so, add me in!
	int	i;
	for ( i = 0; i < MAX_FRAME_GROUPS; i++ )
	{
		if ( level.groups[i].numGroup
			&& level.groups[i].numGroup < (MAX_GROUP_MEMBERS - 1)
			//&& level.groups[i].enemy != NULL
			&& level.groups[i].enemy == self->enemy )
		{//has members, not full and has my enemy
			if ( AI_ValidateGroupMember( &level.groups[i], self ) )
			{//I am a valid member for this group
				AI_InsertGroupMember( &level.groups[i], self );
				return qtrue;
			}
		}
	}
	return qfalse;
}

qboolean AI_GetNextEmptyGroup( gentity_t *self )
{
	if ( AI_FindSelfInPreviousGroup( self ) )
	{//already in one, no need to make a new one
		return qfalse;
	}

	if ( AI_TryJoinPreviousGroup( self ) )
	{//try to just put us in one that already exists
		return qfalse;
	}

	//okay, make a whole new one, then
	for ( int i = 0; i < MAX_FRAME_GROUPS; i++ )
	{
		if ( !level.groups[i].numGroup )
		{//make a new one
			self->NPC->group = &level.groups[i];
			return qtrue;
		}
	}

	//if ( i >= MAX_FRAME_GROUPS )
	{//WTF?  Out of groups!
		self->NPC->group = NULL;
		return qfalse;
	}
}

qboolean AI_ValidateNoEnemyGroupMember( AIGroupInfo_t *group, gentity_t *member )
{
	if ( !group )
	{
		return qfalse;
	}
	vec3_t center;
	if ( group->commander )
	{
		VectorCopy( group->commander->currentOrigin, center );
	}
	else
	{//hmm, just pick the first member
		if ( group->member[0].number < 0 || group->member[0].number >= ENTITYNUM_WORLD )
		{
			return qfalse;
		}
		VectorCopy( g_entities[group->member[0].number].currentOrigin, center );
	}
	//FIXME: maybe it should be based on the center of the mass of the group, not the commander?
	if ( DistanceSquared( center, member->currentOrigin ) > 147456/*384*384*/ )
	{
		return qfalse;
	}
	if ( !gi.inPVS( member->currentOrigin, center ) )
	{//not within PVS of the group enemy
		return qfalse;
	}
	return qtrue;
}

qboolean AI_CanReport( gentity_t *member )
{
	return member && member->inuse && member->NPC && member->client && member->health > 0
		&& !d_noGroupAI->integer
		&& !(member->svFlags & (SVF_IGNORE_ENEMIES|SVF_LOCKEDENEMY|SVF_ICARUS_FREEZE))
		&& (member->NPC->scriptFlags & SCF_LOOK_FOR_ENEMIES)
		&& !(member->NPC->scriptFlags & (SCF_IGNORE_ALERTS|SCF_FORCED_MARCH|SCF_NO_GROUPS))
		&& member->NPC->confusionTime <= level.time && member->NPC->charmedTime <= level.time
		&& member->NPC->controlledTime <= level.time && member->NPC->surrenderTime <= level.time
		&& member->NPC->behaviorState != BS_CINEMATIC && member->NPC->tempBehavior != BS_CINEMATIC
		&& member->NPC->defaultBehavior != BS_CINEMATIC && !Q3_TaskIDPending( member, TID_MOVE_NAV )
		&& member->NPC->behaviorState != BS_FLEE && member->NPC->tempBehavior != BS_FLEE
		&& !(member->client->ps.eFlags & (EF_FORCE_GRIPPED|EF_FORCE_DRAINED)) ? qtrue : qfalse;
}

qboolean AI_ValidateGroupMember( AIGroupInfo_t *group, gentity_t *member, qboolean report )
{
	//Validate ents
	if ( !group || member == NULL || !member->inuse )
		return qfalse;

	//Validate clients
	if ( member->client == NULL )
		return qfalse;

	//Validate NPCs
	if ( member->NPC == NULL )
		return qfalse;

	if ( report )
	{
		if ( !AI_CanReport(member) || !group->enemy || !group->enemy->inuse || group->enemy->health <= 0
			|| (group->enemy->flags & FL_NOTARGET) || !group->enemy->client
			|| group->enemy->client->playerTeam == group->team )
			return qfalse;
	}

	//must be aware
	if ( member->NPC->confusionTime > level.time )
		return qfalse;

	//must be allowed to join groups
	if ( member->NPC->scriptFlags&SCF_NO_GROUPS )
		return qfalse;

	//Must not be in another group
	if ( member->NPC->group != NULL && member->NPC->group != group
		&& (!report || member->NPC->group->enemy || (member->enemy && member->enemy != group->enemy)) )
	{//FIXME: if that group's enemy is mine, why not absorb that group into mine?
		return qfalse;
	}

	//Must be alive
	if ( member->health <= 0 )
		return qfalse;

	//can't be in an emplaced gun
	if( member->s.eFlags & EF_LOCKED_TO_WEAPON )
		return qfalse;

	if( member->s.eFlags & EF_HELD_BY_RANCOR )
		return qfalse;

	if( member->s.eFlags & EF_HELD_BY_SAND_CREATURE )
		return qfalse;

	if( member->s.eFlags & EF_HELD_BY_WAMPA )
		return qfalse;

	//Must be on the same team
	if ( member->client->playerTeam != group->team )
		return qfalse;

	//should have same enemy
	if ( member->enemy != group->enemy )
	{
		if ( member->enemy != NULL )
		{//he's fighting someone else, leave him out
			return qfalse;
		}
		if ( !report && !gi.inPVS( member->currentOrigin, group->enemy->currentOrigin ) )
		{//not within PVS of the group enemy
			return qfalse;
		}
	}
	else if ( group->enemy == NULL )
	{//if the group is a patrol group, only take those within the room and radius
		if ( !AI_ValidateNoEnemyGroupMember( group, member ) )
		{
			return qfalse;
		}
	}
	//must be actually in combat mode
	if ( !report && group->enemy && member->NPC->group != group
		&& (!member->enemy || member->NPC->enemyLastSeenTime <= 0
			|| level.time-member->NPC->enemyLastSeenTime > 7000
			|| !AI_LocalGroupContact( group->commander, member )) )
		return qfalse;
	if ( !TIMER_Done( member, "interrogating" ) )
		return qfalse;
	//FIXME: need to have a route to enemy and/or clear shot?
	return qtrue;
}

qboolean AI_ValidateTacticalMember( AIGroupInfo_t *group, gentity_t *member )
{
	if ( !AI_ValidateGroupMember( group, member, qtrue ) || member->client->ps.forcePowersKnown
		|| (member->NPC->aiFlags & NPCAI_BOSS_CHARACTER) )
		return qfalse;
	switch ( member->client->NPC_class )
	{
	case CLASS_STORMTROOPER: case CLASS_SWAMPTROOPER: case CLASS_IMPERIAL:
	case CLASS_REBEL: case CLASS_COMMANDO: case CLASS_BESPIN_COP:
	case CLASS_RODIAN: case CLASS_TRANDOSHAN: case CLASS_WEEQUAY: case CLASS_GRAN:
		break;
	default:
		return qfalse;
	}
	switch ( member->client->ps.weapon )
	{
	case WP_BLASTER: case WP_BLASTER_PISTOL: case WP_BRYAR_PISTOL: case WP_DISRUPTOR:
	case WP_BOWCASTER: case WP_REPEATER: case WP_DEMP2: case WP_FLECHETTE:
	case WP_ROCKET_LAUNCHER: case WP_CONCUSSION:
		return qtrue;
	case WP_NONE:
		return member->client->ps.stats[STAT_WEAPONS] & ((1<<WP_BLASTER)|(1<<WP_BLASTER_PISTOL)) ? qtrue : qfalse;
	default:
		return qfalse;
	}
}

void AI_DeleteSelfFromGroup( gentity_t *self );

/*
-------------------------
AI_GetGroup
-------------------------
*/
void AI_GetGroup( gentity_t *self )
{
	int	i;
	gentity_t	*member;//, *waiter;
	//int	waiters[MAX_WAITERS];

	if ( !self || !self->NPC )
	{
		return;
	}

	if ( d_noGroupAI->integer )
	{
		self->NPC->group = NULL;
		return;
	}

	if ( !self->client )
	{
		self->NPC->group = NULL;
		return;
	}

	if ( self->NPC->scriptFlags&SCF_NO_GROUPS )
	{
		self->NPC->group = NULL;
		return;
	}

	if ( self->enemy && !self->enemy->client )
	{
		self->NPC->group = NULL;
		return;
	}

	if ( AI_FindSelfInPreviousGroup( self ) )
	{//Keep group memory when this member loses sight.
		if ( !self->enemy || self->enemy == self->NPC->group->enemy )
		{
			return;
		}
		AI_DeleteSelfFromGroup( self );
	}

	if ( self->enemy && level.time - self->NPC->enemyLastSeenTime > 7000 )
	{
		self->NPC->group = NULL;
		return;
	}

	if ( !AI_GetNextEmptyGroup( self ) )
	{//either no more groups left or we're already in a group built earlier
		return;
	}

	//create a new one
	if ( self->enemy && !G_ClearLOS( self, self->enemy ) )
	{//An empty slot does not supply a sight record.
		self->NPC->group = NULL;
		return;
	}
	memset( self->NPC->group, 0, sizeof( AIGroupInfo_t ) );

	self->NPC->group->enemy = self->enemy;
	self->NPC->group->team = self->client->playerTeam;
	self->NPC->group->processed = qfalse;
	self->NPC->group->commander = self;
	self->NPC->group->memberValidateTime = level.time + 2000;
	self->NPC->group->activeMemberNum = 0;

	if ( self->NPC->group->enemy )
	{
		self->NPC->group->lastSeenEnemyTime = level.time;
		VectorCopy( self->NPC->group->enemy->currentOrigin, self->NPC->group->enemyLastSeenPos );
		self->NPC->enemyLastSeenTime = level.time;
		VectorCopy( self->enemy->currentOrigin, self->NPC->enemyLastSeenLocation );
	}

//	for ( i = 0, member = &g_entities[0]; i < globals.num_entities ; i++, member++)
	for ( i = 0; i < globals.num_entities ; i++)
	{
		if(!PInUse(i))
			continue;
		member = &g_entities[i];

		if ( !AI_ValidateGroupMember( self->NPC->group, member ) )
		{//FIXME: keep track of those who aren't angry yet and see if we should wake them after we assemble the core group
			continue;
		}

		//store it
		AI_InsertGroupMember( self->NPC->group, member );

		if ( self->NPC->group->numGroup >= (MAX_GROUP_MEMBERS - 1) )
		{//full
			break;
		}
	}

	/*
	//now go through waiters and see if any should join the group
	//NOTE:  Some should hang back and probably not attack, so we can ambush
	//NOTE: only do this if calling for reinforcements?
	for ( i = 0; i < numWaiters; i++ )
	{
		waiter = &g_entities[waiters[i]];

		for ( j = 0; j < self->NPC->group->numGroup; j++ )
		{
			member = &g_entities[self->NPC->group->member[j];

			if ( gi.inPVS( waiter->currentOrigin, member->currentOrigin ) )
			{//this waiter is within PVS of a current member
			}
		}
	}
	*/

	if ( self->NPC->group->numGroup <= 0 )
	{//none in group
		self->NPC->group = NULL;
		return;
	}

	AI_SortGroupByPathCostToEnemy( self->NPC->group );
	AI_SetClosestBuddy( self->NPC->group );
}

void AI_SetNewGroupCommander( AIGroupInfo_t *group )
{
	gentity_t *member = NULL;
	group->commander = NULL;
	for ( int i = 0; i < group->numGroup; i++ )
	{
		member = &g_entities[group->member[i].number];

		if ( !group->commander || (member && member->NPC && group->commander->NPC && member->NPC->rank > group->commander->NPC->rank) )
		{//keep track of highest rank
			group->commander = member;
		}
	}
}

void AI_DeleteGroupMember( AIGroupInfo_t *group, int memberNum )
{
	TIMER_Remove( &g_entities[group->member[memberNum].number], "reportAck" );
	ST_ClearTactic( &g_entities[group->member[memberNum].number] );
	if ( g_entities[group->member[memberNum].number].NPC )
	{
		g_entities[group->member[memberNum].number].NPC->movementSpeech = 0;
		g_entities[group->member[memberNum].number].NPC->movementSpeechChance = 0.0f;
		int state = g_entities[group->member[memberNum].number].NPC->squadState;
		if ( group->numState[state] > 0 )
			group->numState[state]--;
	}
	Debug_Printf( debugNPCAI, DEBUG_LEVEL_INFO, "squad event=group_delete group=%d ent=%d members_before=%d\n", (int)(group-level.groups), group->member[memberNum].number, group->numGroup );
	if ( group->commander && group->commander->s.number == group->member[memberNum].number )
	{
		group->commander = NULL;
	}
	if ( g_entities[group->member[memberNum].number].NPC )
	{
		g_entities[group->member[memberNum].number].NPC->group = NULL;
	}
	for ( int i = memberNum; i < (group->numGroup-1); i++ )
	{
		memcpy( &group->member[i], &group->member[i+1], sizeof( group->member[i] ) );
	}
	if ( memberNum < group->activeMemberNum )
	{
		group->activeMemberNum--;
		if ( group->activeMemberNum < 0 )
		{
			group->activeMemberNum = 0;
		}
	}
	group->numGroup--;
	if ( group->numGroup < 0 )
	{
		group->numGroup = 0;
	}
	AI_SetNewGroupCommander( group );
}

void AI_DeleteSelfFromGroup( gentity_t *self )
{
	//FIXME: if killed, keep track of how many in group killed?  To affect morale?
	for ( int i = 0; i < self->NPC->group->numGroup; i++ )
	{
		if ( self->NPC->group->member[i].number == self->s.number )
		{
			AI_DeleteGroupMember( self->NPC->group, i );
			return;
		}
	}
}

extern void ST_AggressionAdjust( gentity_t *self, int change );
extern void ST_MarkToCover( gentity_t *self );
extern void ST_StartFlee( gentity_t *self, gentity_t *enemy, vec3_t dangerPoint, int dangerLevel, int minTime, int maxTime );
void AI_GroupMemberKilled( gentity_t *self )
{
/*	AIGroupInfo_t *group = self->NPC->group;
	gentity_t	*member;
	qboolean	noflee = qfalse;

	if ( !group )
	{//what group?
		return;
	}
	if ( !self || !self->NPC || self->NPC->rank < RANK_ENSIGN )
	{//I'm not an officer, let's not really care for now
		return;
	}
	//temporarily drop group morale for a few seconds
	group->moraleAdjust -= self->NPC->rank;
	//go through and drop aggression on my teammates (more cover, worse aim)
	for ( int i = 0; i < group->numGroup; i++ )
	{
		member = &g_entities[group->member[i].number];
		if ( member == self )
		{
			continue;
		}
		if ( member->NPC->rank > RANK_ENSIGN )
		{//officers do not panic
			noflee = qtrue;
		}
		else
		{
			ST_AggressionAdjust( member, -1 );
			member->NPC->currentAim -= Q_irand( 0, 10 );//Q_irand( 0, 2);//drop their aim accuracy
		}
	}
	//okay, if I'm the group commander, make everyone else flee
	if ( group->commander != self )
	{//I'm not the commander... hmm, should maybe a couple flee... maybe those near me?
		return;
	}
	//now see if there is another of sufficient rank to keep them from fleeing
	if ( !noflee )
	{
		self->NPC->group->speechDebounceTime = 0;
		for ( int i = 0; i < group->numGroup; i++ )
		{
			member = &g_entities[group->member[i].number];
			if ( member == self )
			{
				continue;
			}
			if ( member->NPC->rank < RANK_ENSIGN )
			{//grunt
				if ( group->enemy && DistanceSquared( member->currentOrigin, group->enemy->currentOrigin ) < 65536 )//256*256
				{//those close to enemy run away!
					ST_StartFlee( member, group->enemy, member->currentOrigin, AEL_DANGER_GREAT, 3000, 5000 );
				}
				else if ( DistanceSquared( member->currentOrigin, self->currentOrigin ) < 65536 )
				{//those close to me run away!
					ST_StartFlee( member, group->enemy, member->currentOrigin, AEL_DANGER_GREAT, 3000, 5000 );
				}
				else
				{//else, maybe just a random chance
					if ( Q_irand( 0, self->NPC->rank ) > member->NPC->rank )
					{//lower rank they are, higher rank I am, more likely they are to flee
						ST_StartFlee( member, group->enemy, member->currentOrigin, AEL_DANGER_GREAT, 3000, 5000 );
					}
					else
					{
						ST_MarkToCover( member );
					}
				}
				member->NPC->currentAim -= Q_irand( 1, 15 ); //Q_irand( 1, 3 );//drop their aim accuracy even more
			}
			member->NPC->currentAim -= Q_irand( 1, 15 ); //Q_irand( 1, 3 );//drop their aim accuracy even more
		}
	}*/
}

void AI_GroupUpdateEnemyLastSeen( AIGroupInfo_t *group, vec3_t spot )
{
	if ( !group )
	{
		return;
	}
	group->lastSeenEnemyTime = level.time;
	VectorCopy( spot, group->enemyLastSeenPos );
}

void AI_GroupUpdateClearShotTime( AIGroupInfo_t *group )
{
	if ( !group )
	{
		return;
	}
	group->lastClearShotTime = level.time;
}

void AI_GroupUpdateSquadstates( AIGroupInfo_t *group, gentity_t *member, int newSquadState )
{
	if ( !group )
	{
		if ( member->NPC->squadState != newSquadState )
		{
			Debug_Printf( debugNPCAI, DEBUG_LEVEL_INFO, "squad event=state_change group=-1 ent=%d old=%d new=%d\n", member->s.number, member->NPC->squadState, newSquadState );
		}
		member->NPC->squadState = newSquadState;
		return;
	}

	for ( int i = 0; i < group->numGroup; i++ )
	{
		if ( group->member[i].number == member->s.number )
		{
			group->numState[member->NPC->squadState]--;
			if ( member->NPC->squadState != newSquadState )
			{
				Debug_Printf( debugNPCAI, DEBUG_LEVEL_INFO, "squad event=state_change group=%d ent=%d old=%d new=%d\n", (int)(group-level.groups), member->s.number, member->NPC->squadState, newSquadState );
			}
			member->NPC->squadState = newSquadState;
			group->numState[member->NPC->squadState]++;
			return;
		}
	}
}

qboolean AI_RefreshGroup( AIGroupInfo_t *group )
{
	gentity_t	*member;
	int			i;//, j;

	//see if we should merge with another group
	for ( i = 0; i < MAX_FRAME_GROUPS; i++ )
	{
		if ( &level.groups[i] == group )
		{
			break;
		}
		else
		{
			if ( level.groups[i].enemy == group->enemy && level.groups[i].team == group->team
				&& (!group->enemy || AI_LocalGroupContact( group->commander, level.groups[i].commander )) )
			{//2 groups with same enemy
				if ( level.groups[i].numGroup+group->numGroup < (MAX_GROUP_MEMBERS - 1) )
				{//combining the members would fit in one group
					if ( group->nextReportTime > level.groups[i].nextReportTime )
						level.groups[i].nextReportTime = group->nextReportTime;
					if ( group->enemy && group->lastSeenEnemyTime > level.groups[i].lastSeenEnemyTime )
					{
						level.groups[i].lastSeenEnemyTime = group->lastSeenEnemyTime;
						VectorCopy( group->enemyLastSeenPos, level.groups[i].enemyLastSeenPos );
					}
					qboolean deleteWhenDone = qtrue;
					//combine the members of mine into theirs
					for ( int j = 0; j < group->numGroup; j++ )
					{
						member = &g_entities[group->member[j].number];
						if ( level.groups[i].enemy == NULL )
						{//special case for groups without enemies, must be in range
							if ( !AI_ValidateNoEnemyGroupMember( &level.groups[i], member ) )
							{
								deleteWhenDone = qfalse;
								continue;
							}
						}
						//remove this member from this group
						AI_DeleteGroupMember( group, j );
						//keep marker at same place since we deleted this guy and shifted everyone up one
						j--;
						//add them to the earlier group
						AI_InsertGroupMember( &level.groups[i], member );
					}
					//return and delete this group
					if ( deleteWhenDone )
					{
						return qfalse;
					}
				}
			}
		}
	}
	//go through group and validate each membership
	group->commander = NULL;
	for ( i = 0; i < group->numGroup; i++ )
	{
		/*
		//this checks for duplicate copies of one member in a group
		for ( j = 0; j < group->numGroup; j++ )
		{
			if ( i != j )
			{
				if ( group->member[i].number == group->member[j].number )
				{
					break;
				}
			}
		}
		if ( j < group->numGroup )
		{//found a dupe!
			gi.Printf( S_COLOR_RED"ERROR: member %s(%d) a duplicate group member!!!\n", g_entities[group->member[i].number].targetname, group->member[i].number );
			AI_DeleteGroupMember( group, i );
			i--;
			continue;
		}
		*/
		member = &g_entities[group->member[i].number];

		//Must be alive
		if ( member->NPC && member->NPC->tacticRole
			&& (!AI_ValidateTacticalMember( group, member )
				|| (!(member->NPC->scriptFlags & SCF_CHASE_ENEMIES)
					&& !(g_squadPressureOverrides->integer && TIMER_Exists( member, "pressureMove" )))
				|| !member->enemy || member->enemy->s.number != member->NPC->tacticEnemy) )
		{
			ST_ClearTactic( member );
		}
		if ( member->health <= 0 )
		{
			AI_DeleteGroupMember( group, i );
			//keep marker at same place since we deleted this guy and shifted everyone up one
			i--;
		}
		else if ( group->memberValidateTime < level.time && !AI_ValidateGroupMember( group, member ) )
		{
			//remove this one from the group
			AI_DeleteGroupMember( group, i );
			//keep marker at same place since we deleted this guy and shifted everyone up one
			i--;
		}
		else
		{//membership is valid
			if ( !group->commander || member->NPC->rank > group->commander->NPC->rank )
			{//keep track of highest rank
				group->commander = member;
			}
		}
	}
	memset( group->numState, 0, sizeof( group->numState ) );
	for ( i = 0; i < group->numGroup; i++ )
		group->numState[g_entities[group->member[i].number].NPC->squadState]++;
	if ( group->memberValidateTime < level.time )
	{
		group->memberValidateTime = level.time + Q_irand( 500, 2500 );
	}
	//Now add any new guys as long as we're not full
	/*
	for ( i = 0, member = &g_entities[0]; i < globals.num_entities && group->numGroup < (MAX_GROUP_MEMBERS - 1); i++, member++)
	{
		if ( !AI_ValidateGroupMember( group, member ) )
		{//FIXME: keep track of those who aren't angry yet and see if we should wake them after we assemble the core group
			continue;
		}
		if ( member->NPC->group == group )
		{//DOH, already in our group
			continue;
		}

		//store it
		AI_InsertGroupMember( group, member );
	}
	*/

	//calc the morale of this group
	group->morale = group->moraleAdjust;
	for ( i = 0; i < group->numGroup; i++ )
	{
		member = &g_entities[group->member[i].number];
		if ( member->NPC->rank < RANK_ENSIGN )
		{//grunts
			group->morale++;
		}
		else
		{
			group->morale += member->NPC->rank;
		}
		if ( group->commander && debugNPCAI->integer )
		{
			G_DebugLine( group->commander->currentOrigin, member->currentOrigin, FRAMETIME, 0x00ff00ff, qtrue );
		}
	}
	if ( group->enemy )
	{//modify morale based on enemy health and weapon
		if ( group->enemy->health < 10 )
		{
			group->morale += 10;
		}
		else if ( group->enemy->health < 25 )
		{
			group->morale += 5;
		}
		else if ( group->enemy->health < 50 )
		{
			group->morale += 2;
		}
		switch( group->enemy->s.weapon )
		{
		case WP_SABER:
			group->morale -= 5;
			break;
		case WP_BRYAR_PISTOL:
		case WP_BLASTER_PISTOL:
			group->morale += 3;
			break;
		case WP_DISRUPTOR:
			group->morale += 2;
			break;
		case WP_REPEATER:
			group->morale -= 1;
			break;
		case WP_FLECHETTE:
			group->morale -= 2;
			break;
		case WP_ROCKET_LAUNCHER:
			group->morale -= 10;
			break;
		case WP_CONCUSSION:
			group->morale -= 12;
			break;
		case WP_THERMAL:
			group->morale -= 5;
			break;
		case WP_TRIP_MINE:
			group->morale -= 3;
			break;
		case WP_DET_PACK:
			group->morale -= 10;
			break;
		case WP_MELEE:			// Any ol' melee attack
			group->morale += 20;
			break;
		case WP_STUN_BATON:
			group->morale += 10;
			break;
		case WP_EMPLACED_GUN:
			group->morale -= 8;
			break;
		case WP_ATST_MAIN:
			group->morale -= 8;
			break;
		case WP_ATST_SIDE:
			group->morale -= 20;
			break;
		}
	}
	if ( group->moraleDebounce < level.time )
	{//slowly degrade whatever moraleAdjusters we may have
		if ( group->moraleAdjust > 0 )
		{
			group->moraleAdjust--;
		}
		else if ( group->moraleAdjust < 0 )
		{
			group->moraleAdjust++;
		}
		group->moraleDebounce = level.time + 1000;//FIXME: define?
	}
	//mark this group as not having been run this frame
	group->processed = qfalse;

	return (qboolean)(group->numGroup>0);
}

void AI_RestoreCombatPointReservations( void )
{
	// Combat-point occupancy is not saved. Rebuild it after all NPCs are loaded.
	for ( int cp = 0; cp < level.numCombatPoints; ++cp )
		level.combatPoints[cp].occupied = qfalse;
	for ( int i = 0; i < globals.num_entities; ++i )
	{
		gentity_t *owner = &g_entities[i];
		if ( !owner->inuse || !owner->NPC || owner->NPC->combatPoint < 0 )
			continue;
		if ( owner->health <= 0 || !NPC_ReserveCombatPoint(owner->NPC->combatPoint) )
		{
			// A duplicate or invalid saved claim must not release another NPC's point.
			owner->NPC->combatPoint = -1;
			ST_ClearTactic(owner);
		}
	}
}

void AI_UpdateGroups( void )
{
	if ( d_noGroupAI->integer )
	{
		return;
	}
	//Clear all Groups
	for ( int i = 0; i < MAX_FRAME_GROUPS; i++ )
	{
		if ( !level.groups[i].numGroup || AI_RefreshGroup( &level.groups[i] ) == qfalse )//level.groups[i].enemy == NULL ||
		{
			memset( &level.groups[i], 0, sizeof( level.groups[i] ) );
		}
	}
}

qboolean AI_GroupContainsEntNum( AIGroupInfo_t *group, int entNum )
{
	if ( !group )
	{
		return qfalse;
	}
	for ( int i = 0; i < group->numGroup; i++ )
	{
		if ( group->member[i].number == entNum )
		{
			return qtrue;
		}
	}
	return qfalse;
}
//Overload

/*
void AI_GetGroup( AIGroupInfo_t &group, gentity_t *ent, int radius )
{
	if ( ent->client == NULL )
		return;

	vec3_t	temp, angles;

	//FIXME: This is specialized code.. move?
	if ( ent->enemy )
	{
		VectorSubtract( ent->enemy->currentOrigin, ent->currentOrigin, temp );
		VectorNormalize( temp );	//FIXME: Needed?
		vectoangles( temp, angles );
	}
	else
	{
		VectorCopy( ent->currentAngles, angles );
	}

	AI_GetGroup( group, ent->currentOrigin, ent->currentAngles, DEFAULT_RADIUS, radius, ent->client->playerTeam, ent, ent->enemy );
}
*/

/*
-------------------------
AI_DistributeAttack
-------------------------
*/

#define	MAX_RADIUS_ENTS		128

gentity_t *AI_DistributeAttack( gentity_t *attacker, gentity_t *enemy, team_t team, int threshold )
{
	//Don't take new targets
	if ( NPC->svFlags & SVF_LOCKEDENEMY )
		return enemy;

	int	numSurrounding = AI_GetGroupSize( enemy->currentOrigin, 48, team, attacker );

	//First, see if we should look for the player
	if ( enemy != &g_entities[0] )
	{
		int	aroundPlayer = AI_GetGroupSize( g_entities[0].currentOrigin, 48, team, attacker );

		//See if we're above our threshold
		if ( aroundPlayer < threshold )
		{
			return &g_entities[0];
		}
	}

	//See if our current enemy is still ok
	if ( numSurrounding < threshold )
		return enemy;

	//Otherwise we need to take a new enemy if possible
	vec3_t	mins, maxs;

	//Setup the bbox to search in
	for ( int i = 0; i < 3; i++ )
	{
		mins[i] = enemy->currentOrigin[i] - 512;
		maxs[i] = enemy->currentOrigin[i] + 512;
	}

	//Get the number of entities in a given space
	gentity_t	*radiusEnts[ MAX_RADIUS_ENTS ];

	int numEnts = gi.EntitiesInBox( mins, maxs, radiusEnts, MAX_RADIUS_ENTS );

	//Cull this list
	for ( int j = 0; j < numEnts; j++ )
	{
		//Validate clients
		if ( radiusEnts[ j ]->client == NULL )
			continue;

		//Skip the requested avoid ent if present
		if ( radiusEnts[ j ] == enemy )
			continue;

		//Must be on the same team
		if ( radiusEnts[ j ]->client->playerTeam != enemy->client->playerTeam )
			continue;

		//Must be alive
		if ( radiusEnts[ j ]->health <= 0 )
			continue;

		//Must not be overwhelmed
		if ( AI_GetGroupSize( radiusEnts[j]->currentOrigin, 48, team, attacker ) > threshold )
			continue;

		return radiusEnts[j];
	}

	return NULL;
}
