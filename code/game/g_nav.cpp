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

#include "b_local.h"
#include "g_nav.h"
#include "g_navigator.h"
#include "g_functions.h"
#include "Q3_Interface.h"
#include <cerrno>
#include <cmath>
#include <cstdlib>

//Global navigator
//CNavigator		navigator;

extern qboolean G_EntIsUnlockedDoor( int entityNum );
extern qboolean G_EntIsDoor( int entityNum );
extern qboolean G_EntIsRemovableUsable( int entNum );
extern qboolean G_FindClosestPointOnLineSegment( const vec3_t start, const vec3_t end, const vec3_t from, vec3_t result );
extern void G_AddVoiceEvent( gentity_t *self, int event, int speakDebounceTime );
//For debug graphics
extern void CG_Line( vec3_t start, vec3_t end, vec3_t color, float alpha );
extern void CG_Cube( vec3_t mins, vec3_t maxs, vec3_t color, float alpha );
extern void CG_CubeOutline( vec3_t mins, vec3_t maxs, int time, unsigned int color, float alpha );
extern qboolean FlyingCreature( gentity_t *ent );


extern vec3_t NPCDEBUG_RED;


/*
-------------------------
NPC_SetMoveGoal
-------------------------
*/

void NPC_SetMoveGoal( gentity_t *ent, vec3_t point, int radius, qboolean isNavGoal, int combatPoint, gentity_t *targetEnt )
{
	//Must be an NPC
	if ( ent->NPC == NULL )
	{
		return;
	}

	if ( ent->NPC->tempGoal == NULL )
	{//must still have a goal
		return;
	}

	//Copy the origin
	//VectorCopy( point, ent->NPC->goalPoint );	//FIXME: Make it use this, and this alone!
	VectorCopy( point, ent->NPC->tempGoal->currentOrigin );

	//Copy the mins and maxs to the tempGoal
	VectorCopy( ent->mins, ent->NPC->tempGoal->mins );
	VectorCopy( ent->mins, ent->NPC->tempGoal->maxs );

	//FIXME: TESTING let's try making sure the tempGoal isn't stuck in the ground?
	if ( 0 )
	{
		trace_t	trace;
		vec3_t	bottom = {ent->NPC->tempGoal->currentOrigin[0],ent->NPC->tempGoal->currentOrigin[1],ent->NPC->tempGoal->currentOrigin[2]+ent->NPC->tempGoal->mins[2]};
		gi.trace( &trace, ent->NPC->tempGoal->currentOrigin, vec3_origin, vec3_origin, bottom, ent->s.number, ent->clipmask, (EG2_Collision)0, 0 );
		if ( trace.fraction < 1.0f )
		{//in the ground, raise it up
			ent->NPC->tempGoal->currentOrigin[2] -= ent->NPC->tempGoal->mins[2]*(1.0f-trace.fraction)-0.125f;
		}
	}

	ent->NPC->tempGoal->target = NULL;
	ent->NPC->tempGoal->clipmask = ent->clipmask;
	ent->NPC->tempGoal->svFlags &= ~SVF_NAVGOAL;
	if ( targetEnt && targetEnt->waypoint >= 0 )
	{
		ent->NPC->tempGoal->waypoint = targetEnt->waypoint;
	}
	else
	{
		ent->NPC->tempGoal->waypoint = WAYPOINT_NONE;
	}
	ent->NPC->tempGoal->noWaypointTime = 0;

	if ( isNavGoal )
	{
		assert(ent->NPC->tempGoal->owner);
		ent->NPC->tempGoal->svFlags |= SVF_NAVGOAL;
	}

	ent->NPC->tempGoal->combatPoint = combatPoint;
	ent->NPC->tempGoal->enemy = targetEnt;

	ent->NPC->goalEntity = ent->NPC->tempGoal;
	ent->NPC->goalRadius = radius;
	ent->NPC->aiFlags	&= ~NPCAI_STOP_AT_LOS;

	gi.linkentity( ent->NPC->goalEntity );
}
/*
-------------------------
waypoint_testDirection
-------------------------
*/

static float waypoint_testDirection( vec3_t origin, float yaw, float minDist )
{
	vec3_t	trace_dir, test_pos;
	vec3_t	maxs, mins;
	trace_t	tr;

	//Setup the mins and max
	VectorSet( maxs, DEFAULT_MAXS_0, DEFAULT_MAXS_1, DEFAULT_MAXS_2 );
	VectorSet( mins, DEFAULT_MINS_0, DEFAULT_MINS_1, DEFAULT_MINS_2 + STEPSIZE );

	//Get our test direction
	vec3_t	angles = { 0, yaw, 0 };
	AngleVectors( angles, trace_dir, NULL, NULL );

	//Move ahead
	VectorMA( origin, minDist, trace_dir, test_pos );

	gi.trace( &tr, origin, mins, maxs, test_pos, ENTITYNUM_NONE, ( CONTENTS_SOLID | CONTENTS_MONSTERCLIP | CONTENTS_BOTCLIP ), (EG2_Collision)0, 0 );

	return ( minDist * tr.fraction );	//return actual dist completed
}

/*
-------------------------
waypoint_getRadius
-------------------------
*/

static float waypoint_getRadius( gentity_t *ent )
{
	float	minDist = MAX_RADIUS_CHECK + 1; // (unsigned int) -1;
	float	dist;

	for ( int i = 0; i < YAW_ITERATIONS; i++ )
	{
		dist = waypoint_testDirection( ent->currentOrigin, ((360.0f/YAW_ITERATIONS) * i), minDist );

		if ( dist < minDist )
			minDist = dist;
	}

	return minDist + DEFAULT_MAXS_0;
}

/*QUAKED waypoint  (0.7 0.7 0) (-20 -20 -24) (20 20 45) SOLID_OK DROP_TO_FLOOR
a place to go.

SOLID_OK - only use if placing inside solid is unavoidable in map, but may be clear in-game (ie: at the bottom of a tall, solid lift that starts at the top position)
DROP_TO_FLOOR - will cause the point to auto drop to the floor

radius is automatically calculated in-world.
"targetJump" is a special edge that only guys who can jump will cross (so basically Jedi)
*/
extern int delayedShutDown;
void SP_waypoint ( gentity_t *ent )
{
		VectorSet(ent->mins, DEFAULT_MINS_0, DEFAULT_MINS_1, DEFAULT_MINS_2);
		VectorSet(ent->maxs, DEFAULT_MAXS_0, DEFAULT_MAXS_1, DEFAULT_MAXS_2);

		ent->contents = CONTENTS_TRIGGER;
		ent->clipmask = MASK_DEADSOLID;

		gi.linkentity( ent );

		ent->count = -1;
		ent->classname = "waypoint";

		if (ent->spawnflags&2)
		{
			ent->currentOrigin[2] += 128.0f;
		}

		if( !(ent->spawnflags&1) && G_CheckInSolid (ent, qtrue))
		{//if not SOLID_OK, and in solid
			ent->maxs[2] = CROUCH_MAXS_2;
			if(G_CheckInSolid (ent, qtrue))
			{
				gi.Printf(S_COLOR_RED"ERROR: Waypoint %s at %s in solid!\n", ent->targetname, vtos(ent->currentOrigin));
				assert(0 && "Waypoint in solid!");
//				if (!g_entities[ENTITYNUM_WORLD].s.radius){	//not a region
//					G_Error("Waypoint %s at %s in solid!\n", ent->targetname, vtos(ent->currentOrigin));
//				}
				delayedShutDown = level.time + 100;
				G_FreeEntity(ent);
				return;
			}
		}

		//G_SpawnString("targetJump", "", &ent->targetJump);
		ent->radius = waypoint_getRadius( ent );
		NAV::SpawnedPoint(ent);

		G_FreeEntity(ent);
		return;
}

/*QUAKED waypoint_small  (0.7 0.7 0) (-2 -2 -24) (2 2 32) SOLID_OK
SOLID_OK - only use if placing inside solid is unavoidable in map, but may be clear in-game (ie: at the bottom of a tall, solid lift that starts at the top position)
DROP_TO_FLOOR - will cause the point to auto drop to the floor
*/
void SP_waypoint_small (gentity_t *ent)
{
		VectorSet(ent->mins, -2, -2, DEFAULT_MINS_2);
		VectorSet(ent->maxs, 2, 2, DEFAULT_MAXS_2);

		ent->contents = CONTENTS_TRIGGER;
		ent->clipmask = MASK_DEADSOLID;

		gi.linkentity( ent );

		ent->count = -1;
		ent->classname = "waypoint";

		if ( !(ent->spawnflags&1) && G_CheckInSolid( ent, qtrue ) )
		{
			ent->maxs[2] = CROUCH_MAXS_2;
			if ( G_CheckInSolid( ent, qtrue ) )
			{
				gi.Printf(S_COLOR_RED"ERROR: Waypoint_small %s at %s in solid!\n", ent->targetname, vtos(ent->currentOrigin));
				assert(0);
#ifndef FINAL_BUILD
				if (!g_entities[ENTITYNUM_WORLD].s.radius){	//not a region
					G_Error("Waypoint_small %s at %s in solid!\n", ent->targetname, vtos(ent->currentOrigin));
				}
#endif
				G_FreeEntity(ent);
				return;
			}
		}

		ent->radius = 2;	// radius
		NAV::SpawnedPoint(ent);

		G_FreeEntity(ent);
		return;
}


/*QUAKED waypoint_navgoal (0.3 1 0.3) (-20 -20 -24) (20 20 40) SOLID_OK DROP_TO_FLOOR NO_AUTO_CONNECT
A waypoint for script navgoals
Not included in navigation data

DROP_TO_FLOOR - will cause the point to auto drop to the floor
NO_AUTO_CONNECT - will not automatically connect to any other points, you must then connect it by hand


SOLID_OK - only use if placing inside solid is unavoidable in map, but may be clear in-game (ie: at the bottom of a tall, solid lift that starts at the top position)

targetname - name you would use in script when setting a navgoal (like so:)

  For example: if you give this waypoint a targetname of "console", make an NPC go to it in a script like so:

  set ("navgoal", "console");

radius - how far from the navgoal an ent can be before it thinks it reached it - default is "0" which means no radius check, just have to touch it

*/

void SP_waypoint_navgoal( gentity_t *ent )
{
	int radius = ( ent->radius ) ? (ent->radius) : 12;

	VectorSet( ent->mins, -16, -16, -24 );
	VectorSet( ent->maxs, 16, 16, 32 );
	ent->s.origin[2] += 0.125;
	if ( !(ent->spawnflags&1) && G_CheckInSolid( ent, qfalse ) )
	{
		gi.Printf(S_COLOR_RED"ERROR: Waypoint_navgoal %s at %s in solid!\n", ent->targetname, vtos(ent->currentOrigin));
		assert(0);
#ifndef FINAL_BUILD
		if (!g_entities[ENTITYNUM_WORLD].s.radius){	//not a region
			G_Error("Waypoint_navgoal %s at %s in solid!\n", ent->targetname, vtos(ent->currentOrigin));
		}
#endif
	}
	TAG_Add( ent->targetname, NULL, ent->s.origin, ent->s.angles, radius, RTF_NAVGOAL );

	ent->classname = "navgoal";

	NAV::SpawnedPoint(ent, NAV::PT_GOALNODE);

	G_FreeEntity( ent );//can't do this, they need to be found later by some functions, though those could be fixed, maybe?
}

/*
-------------------------
Svcmd_Nav_f
-------------------------
*/

extern gentity_t *NPC_Spawn_Do( gentity_t *ent, qboolean fullSpawnNow );
extern void NPC_PrecacheByClassName( const char *type );
extern void G_SetWeapon( gentity_t *self, int weapon );
extern vec3_t playerMins, playerMaxs;

static struct
{
	gentity_t *actor;
	vec3_t points[8];
	int count, leg, timeout, started, reported;
} routeTest;

static void RouteTestFinish( const char *event, bool removing = false )
{
	gentity_t *actor = routeTest.actor;
	routeTest.actor = NULL; // Clear ownership before recursive frees.
	if ( !actor )
		return;
	gi.Printf( "routetest event=%s ent=%d\n", event, actor->s.number );
	NAV::ClearPath( actor );
	actor->NPC->goalEntity = actor->NPC->lastGoalEntity = NULL;
	actor->NPC->eventualGoal = actor->NPC->captureGoal = actor->NPC->watchTarget = NULL;
	gentity_t *goal = actor->NPC->tempGoal;
	actor->NPC->tempGoal = NULL;
	if ( goal )
		G_FreeEntity( goal );
	if ( !removing )
	{
		G_FreeEntity( actor );
		gi.Printf( "routetest event=cleanup remaining=0\n" );
	}
}

bool NAV_RouteTestFree( gentity_t *ent )
{
	if ( !routeTest.actor )
		return false;
	if ( ent == routeTest.actor )
	{
		RouteTestFinish( "failed reason=actor_removed", true );
		return true;
	}
	if ( ent == routeTest.actor->NPC->tempGoal )
	{
		// Death and script removal can free the goal before the actor.
		routeTest.actor->NPC->tempGoal = NULL;
		routeTest.actor->NPC->goalEntity = NULL;
	}
	return false;
}

void NAV_RouteTestReset( const char *reason )
{
	if ( reason && routeTest.actor )
		RouteTestFinish( va( "cancelled reason=%s", reason ) );
	// InitGame may follow an allocator reset; do not dereference old entities.
	routeTest.actor = NULL;
}

static void RouteTestGoal( void )
{
	gentity_t *actor = routeTest.actor;
	NAV::ClearPath( actor );
	actor->NPC->aiFlags &= ~NPCAI_TOUCHED_GOAL;
	NPC_SetMoveGoal( actor, routeTest.points[routeTest.leg], 8, qtrue );
	actor->NPC->tempGoal->lastWaypoint = WAYPOINT_NONE;
	routeTest.started = level.time;
	const float *p = routeTest.points[routeTest.leg];
	gi.Printf( "routetest event=goal leg=%d pos=%.3f,%.3f,%.3f\n", routeTest.leg, p[0], p[1], p[2] );
}

void NAV_RouteTestUpdate( void )
{
	gentity_t *actor = routeTest.actor;
	if ( !actor )
		return;
	if ( actor->health <= 0 || !actor->NPC->tempGoal )
	{
		RouteTestFinish( actor->health <= 0 ? "failed reason=dead" : "failed reason=goal_removed" );
		return;
	}
	const float *point = routeTest.points[routeTest.leg];
	if ( !VectorCompare( actor->NPC->tempGoal->currentOrigin, point ) )
	{
		RouteTestFinish( "failed reason=goal_replaced" );
		return;
	}
	float distance = Distance( actor->currentOrigin, point );
	if ( distance < 8 || G_BoundsOverlap( point, point, actor->absmin, actor->absmax ) )
	{
		gi.Printf( "routetest event=arrive leg=%d pos=%.3f,%.3f,%.3f distance=%.3f los=%d\n", routeTest.leg, actor->currentOrigin[0], actor->currentOrigin[1], actor->currentOrigin[2], distance, G_ClearLOS( actor, &g_entities[0] ) );
		if ( ++routeTest.leg == routeTest.count )
			RouteTestFinish( "complete" );
		else
			RouteTestGoal();
		return;
	}
	if ( level.time - routeTest.started >= routeTest.timeout )
	{
		RouteTestFinish( "failed reason=timeout" );
		return;
	}
	if ( level.time - routeTest.reported >= 1000 )
	{
		routeTest.reported = level.time;
		const float *p = actor->currentOrigin;
		trace_t trace;
		gi.trace( &trace, actor->currentOrigin, actor->mins, actor->maxs, routeTest.points[routeTest.leg], actor->s.number, MASK_NPCSOLID, (EG2_Collision)0, 0 );
		gi.Printf( "routetest event=position leg=%d pos=%.3f,%.3f,%.3f distance=%.3f nodes=%d trace_entity=%d fraction=%.3f los=%d\n",
			routeTest.leg, p[0], p[1], p[2], distance, NAV::PathNodesRemaining( actor ), trace.entityNum, trace.fraction, G_ClearLOS( actor, &g_entities[0] ) );
	}
}

static void RouteTestCommand( void )
{
	int argc = gi.argc();
	if ( argc == 3 && !Q_stricmp( gi.argv(2), "cancel" ) )
	{
		if ( routeTest.actor )
			RouteTestFinish( "cancelled" );
		else
			gi.Printf( "routetest event=cancelled remaining=0\n" );
		return;
	}
	vec3_t points[8];
	int count = (argc - 3) / 3;
	char *end;
	const char *arg = gi.argv(2);
	errno = 0;
	long timeout = strtol( arg, &end, 10 );
	bool valid = *arg && !*end && !errno && timeout >= 100 && timeout <= 30000;
	for ( const char *c = arg; *c; ++c )
		valid = valid && *c >= '0' && *c <= '9';
	valid = valid && argc >= 9 && argc <= 27 && (argc - 3) % 3 == 0;
	for ( int i = 0; valid && i < count * 3; ++i )
	{
		arg = gi.argv(3 + i);
		errno = 0;
		float value = strtof( arg, &end );
		valid = *arg && !*end && !errno && std::isfinite(value) && value >= MIN_WORLD_COORD && value <= MAX_WORLD_COORD;
		for ( const char *c = arg; *c; ++c )
			valid = valid && ((*c >= '0' && *c <= '9') || *c == '+' || *c == '-' || *c == '.' || *c == 'e' || *c == 'E');
		points[i / 3][i % 3] = value;
	}
	if ( !valid )
	{
		gi.Printf( "routetest event=rejected reason=arguments usage=nav test <100..30000-ms> <x y z> <x y z> ... (2..8 points)\n" );
		return;
	}
	if ( routeTest.actor )
	{
		gi.Printf( "routetest event=rejected reason=busy\n" );
		return;
	}
	vec3_t bottom;
	VectorCopy( points[0], bottom );
	bottom[2] -= 64;
	trace_t tr;
	gi.trace( &tr, points[0], playerMins, playerMaxs, bottom, ENTITYNUM_NONE, MASK_NPCSOLID, (EG2_Collision)0, 0 );
	if ( tr.startsolid || tr.allsolid || tr.fraction == 1 || tr.plane.normal[2] < 0.7f )
	{
		gi.Printf( "routetest event=failed reason=spawn_blocked remaining=0 entity=%d startsolid=%d fraction=%.3f normal_z=%.3f\n", tr.entityNum, tr.startsolid, tr.fraction, tr.plane.normal[2] );
		return;
	}
	NPC_PrecacheByClassName( "stormtrooper" );
	gentity_t *spawner = G_Spawn();
	spawner->classname = "NPC_routetest";
	spawner->NPC_type = "stormtrooper";
	spawner->NPC_targetname = "_route_test";
	spawner->count = 1;
	spawner->spawnflags = SFB_CINEMATIC;
	G_SetOrigin( spawner, points[0] );
	VectorCopy( points[0], spawner->s.origin );
	gentity_t *actor = NPC_Spawn_Do( spawner, qtrue );
	if ( !actor )
	{
		G_FreeEntity( spawner ); // Success consumes the spawner; failure does not.
		gi.Printf( "routetest event=failed reason=spawn_failed remaining=0\n" );
		return;
	}
	routeTest.actor = actor;
	if ( actor->e_ThinkFunc != thinkF_NPC_Think )
	{
		RouteTestFinish( "failed reason=spawn_failed" );
		return;
	}
	actor->flags |= FL_NOTARGET;
	actor->svFlags |= SVF_IGNORE_ENEMIES;
	actor->enemy = NULL;
	// The locomotion probe must not leave a weapon pickup when killed.
	G_SetWeapon( actor, WP_NONE );
	actor->s.weapon = WP_NONE;
	actor->NPC->scriptFlags = SCF_NO_GROUPS;
	actor->NPC->defaultBehavior = actor->NPC->behaviorState = BS_CINEMATIC;
	actor->NPC->tempBehavior = BS_DEFAULT;
	actor->NPC->lastGoalEntity = actor->NPC->eventualGoal = actor->NPC->captureGoal = actor->NPC->watchTarget = NULL;
	memcpy( routeTest.points, points, count * sizeof(vec3_t) );
	routeTest.count = count;
	routeTest.leg = 1;
	routeTest.timeout = (int)timeout;
	routeTest.reported = level.time;
	gi.Printf( "routetest event=start ent=%d pos=%.3f,%.3f,%.3f weapon=%d\n", actor->s.number, actor->currentOrigin[0], actor->currentOrigin[1], actor->currentOrigin[2], actor->s.weapon );
	RouteTestGoal();
}

static void MemoryCommand( void )
{
	const char *name = gi.argv(2);
	gentity_t *actor = G_Find( NULL, FOFS(targetname), name );
	if ( gi.argc() < 3 || gi.argc() > 5 || (gi.argc() == 5 && Q_stricmp(gi.argv(3), "enemy"))
		|| !name[0] || !actor || !actor->NPC || !actor->client
		|| G_Find( actor, FOFS(targetname), name ) )
	{
		gi.Printf( "aimemory event=rejected reason=unique_npc_required\n" );
		return;
	}
	if ( gi.argc() >= 4 )
	{
		const char *action = gi.argv(3);
		if ( Q_strncmp(name, "_memory_", 8) || Q3_TaskIDPending(actor, TID_MOVE_NAV) )
		{
			gi.Printf( "aimemory event=rejected reason=test_actor_required\n" );
			return;
		}
		if ( !Q_stricmp(action, "enemy") )
		{
			gentity_t *target = gi.argc() == 5 ? G_Find(NULL, FOFS(targetname), gi.argv(4)) : &g_entities[0];
			if ( !target || !target->client || target == actor
				|| (gi.argc() == 5 && G_Find(target, FOFS(targetname), gi.argv(4))) )
			{
				gi.Printf( "aimemory event=rejected reason=target\n" );
				return;
			}
			G_SetEnemy(actor, target);
		}
		else if ( !Q_stricmp(action, "hold") || !Q_stricmp(action, "chase") )
		{
			actor->NPC->scriptFlags |= SCF_DONT_FIRE;
			if ( !Q_stricmp(action, "hold") )
			{
				actor->NPC->scriptFlags &= ~SCF_CHASE_ENEMIES;
				actor->NPC->goalEntity = actor->NPC->lastGoalEntity = NULL;
				NPC_FreeCombatPoint(actor->NPC->combatPoint);
				actor->NPC->combatPoint = -1;
				NAV::ClearPath(actor);
				VectorClear(actor->client->ps.velocity);
				memset(&actor->NPC->last_ucmd, 0, sizeof(actor->NPC->last_ucmd));
			}
			else
				actor->NPC->scriptFlags |= SCF_CHASE_ENEMIES;
		}
		else
		{
			gi.Printf( "aimemory event=rejected reason=action\n" );
			return;
		}
		gi.Printf( "aimemory event=control name=%s action=%s\n", name, action );
		return;
	}
	AIGroupInfo_t *group = actor->NPC->group;
	const float *seen = actor->NPC->enemyLastSeenLocation;
	const float *shared = group ? group->enemyLastSeenPos : vec3_origin;
	const float *target = actor->enemy ? actor->enemy->currentOrigin : vec3_origin;
	gentity_t *goal = actor->NPC->goalEntity;
	const float *goalPos = goal ? goal->currentOrigin : vec3_origin;
	gi.Printf( "aimemory event=sample name=%s time=%d ent=%d enemy=%d pos=%.3f,%.3f,%.3f target=%.3f,%.3f,%.3f los=%d pvs=%d seen_time=%d seen=%.3f,%.3f,%.3f group=%d group_enemy=%d group_time=%d shared=%.3f,%.3f,%.3f clear_time=%d members=%d goal=%d goal_pos=%.3f,%.3f,%.3f home=%d\n",
		name, level.time, actor->s.number, actor->enemy ? actor->enemy->s.number : -1,
		actor->currentOrigin[0], actor->currentOrigin[1], actor->currentOrigin[2], target[0], target[1], target[2],
		actor->enemy ? G_ClearLOS(actor, actor->enemy) : 0, actor->enemy ? gi.inPVS(actor->currentOrigin, target) : 0,
		actor->NPC->enemyLastSeenTime, seen[0], seen[1], seen[2], group ? (int)(group-level.groups) : -1,
		group && group->enemy ? group->enemy->s.number : -1, group ? group->lastSeenEnemyTime : 0,
		shared[0], shared[1], shared[2], group ? group->lastClearShotTime : 0, group ? group->numGroup : 0, goal ? goal->s.number : -1,
		goalPos[0], goalPos[1], goalPos[2], actor->NPC->homeWp );
}

void Svcmd_Nav_f( void )
{
	const char	*cmd = gi.argv( 1 );

	if ( Q_stricmp( cmd, "memory" ) == 0 )
	{
		MemoryCommand();
	}
	else if ( Q_stricmp( cmd, "test" ) == 0 )
	{
		RouteTestCommand();
	}
	else if ( Q_stricmp( cmd, "show" ) == 0 )
	{
		cmd = gi.argv( 2 );

		if ( Q_stricmp( cmd, "all" ) == 0 )
		{
			NAVDEBUG_showNodes = !NAVDEBUG_showNodes;

			//NOTENOTE: This causes the two states to sync up if they aren't already
			NAVDEBUG_showCollision = NAVDEBUG_showNavGoals =
			NAVDEBUG_showCombatPoints = NAVDEBUG_showEnemyPath =
			NAVDEBUG_showEdges = NAVDEBUG_showNearest = NAVDEBUG_showRadius = NAVDEBUG_showNodes;
		}
		else if ( Q_stricmp( cmd, "nodes" ) == 0 )
		{
			NAVDEBUG_showNodes = !NAVDEBUG_showNodes;
		}
		else if ( Q_stricmp( cmd, "radius" ) == 0 )
		{
			NAVDEBUG_showRadius = !NAVDEBUG_showRadius;
		}
		else if ( Q_stricmp( cmd, "edges" ) == 0 )
		{
			NAVDEBUG_showEdges = !NAVDEBUG_showEdges;
		}
		else if ( Q_stricmp( cmd, "testpath" ) == 0 )
		{
			NAVDEBUG_showTestPath = !NAVDEBUG_showTestPath;
		}
		else if ( Q_stricmp( cmd, "enemypath" ) == 0 )
		{
			NAVDEBUG_showEnemyPath = !NAVDEBUG_showEnemyPath;
		}
		else if ( Q_stricmp( cmd, "combatpoints" ) == 0 )
		{
			NAVDEBUG_showCombatPoints = !NAVDEBUG_showCombatPoints;
		}
		else if ( Q_stricmp( cmd, "navgoals" ) == 0 )
		{
			NAVDEBUG_showNavGoals = !NAVDEBUG_showNavGoals;
		}
		else if ( Q_stricmp( cmd, "collision" ) == 0 )
		{
			NAVDEBUG_showCollision = !NAVDEBUG_showCollision;
		}
		else if ( Q_stricmp( cmd, "grid" ) == 0 )
		{
			NAVDEBUG_showGrid = !NAVDEBUG_showGrid;
		}
		else if ( Q_stricmp( cmd, "nearest" ) == 0 )
		{
			NAVDEBUG_showNearest = !NAVDEBUG_showNearest;
		}
		else if ( Q_stricmp( cmd, "lines" ) == 0 )
		{
			NAVDEBUG_showPointLines = !NAVDEBUG_showPointLines;
		}
	}
	else if ( Q_stricmp( cmd, "set" ) == 0 )
	{
		cmd = gi.argv( 2 );

		if ( Q_stricmp( cmd, "testgoal" ) == 0 )
		{
		//	NAVDEBUG_curGoal = navigator.GetNearestNode( &g_entities[0], g_entities[0].waypoint, NF_CLEAR_PATH, WAYPOINT_NONE );
		}
	}
	else if ( Q_stricmp( cmd, "goto" ) == 0 )
	{
		cmd = gi.argv( 2 );
		NAV::TeleportTo(&(g_entities[0]), cmd);
	}
	else if ( Q_stricmp( cmd, "gotonum" ) == 0 )
	{
		cmd = gi.argv( 2 );
		NAV::TeleportTo(&(g_entities[0]), atoi(cmd));
	}
	else if ( Q_stricmp( cmd, "totals" ) == 0 )
	{
		NAV::ShowStats();
	}
	else if ( Q_stricmp( cmd, "dump" ) == 0 )
	{
		NAV::DumpLocalGraph(g_entities[0].currentOrigin);
	}
	else
	{
		//Print the available commands
		Com_Printf("nav - valid commands\n---\n" );
		Com_Printf("memory <unique NPC targetname> [hold|chase|enemy [targetname]] - inspect sight memory; controls require _memory_ names\n" );
		Com_Printf("show\n - nodes\n - edges\n - testpath\n - enemypath\n - combatpoints\n - navgoals\n---\n");
		Com_Printf("goto\n ---\n" );
		Com_Printf("gotonum\n ---\n" );
		Com_Printf("totals\n ---\n" );
		Com_Printf("dump - print navigation within 1200 units of the player\n" );
		Com_Printf("set\n - testgoal\n---\n" );
	}
}

//
//JWEIER ADDITIONS START

bool	navCalculatePaths	= false;

bool	NAVDEBUG_showNodes			= false;
bool	NAVDEBUG_showRadius			= false;
bool	NAVDEBUG_showEdges			= false;
bool	NAVDEBUG_showTestPath		= false;
bool	NAVDEBUG_showEnemyPath		= false;
bool	NAVDEBUG_showCombatPoints	= false;
bool	NAVDEBUG_showNavGoals		= false;
bool	NAVDEBUG_showCollision		= false;
int		NAVDEBUG_curGoal			= 0;
bool	NAVDEBUG_showGrid			= false;
bool	NAVDEBUG_showNearest		= false;
bool	NAVDEBUG_showPointLines		= false;


//
//JWEIER ADDITIONS END
