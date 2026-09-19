/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
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

#include "../cgame/cg_local.h"
#include "Q3_Interface.h"

#include "g_local.h"
#include "g_jolt.h"
#include "wp_saber.h"
#include "g_functions.h"
#include "objectives.h"

extern void G_NextTestAxes( void );
extern void G_ChangePlayerModel( gentity_t *ent, const char *newModel );
extern void G_InitPlayerFromCvars( gentity_t *ent );
extern void Q3_SetViewEntity(int entID, const char *name);
extern qboolean G_ClearViewEntity( gentity_t *ent );
extern void G_Knockdown( gentity_t *self, gentity_t *attacker, const vec3_t pushDir, float strength, qboolean breakSaberLock );

extern void WP_SetSaber( gentity_t *ent, int saberNum, const char *saberName );
extern void WP_RemoveSaber( gentity_t *ent, int saberNum );
extern saber_colors_t TranslateSaberColor( const char *name );
extern qboolean WP_SaberBladeUseSecondBladeStyle( saberInfo_t *saber, int bladeNum );
extern qboolean WP_UseFirstValidSaberStyle( gentity_t *ent, int *saberAnimLevel );

extern void G_SetWeapon( gentity_t *self, int wp );
extern stringID_table_t WPTable[];

extern cvar_t	*g_char_model;
extern cvar_t	*g_char_skin_head;
extern cvar_t	*g_char_skin_torso;
extern cvar_t	*g_char_skin_legs;
extern cvar_t	*g_char_color_red;
extern cvar_t	*g_char_color_green;
extern cvar_t	*g_char_color_blue;
extern cvar_t	*g_saber;
extern cvar_t	*g_saber2;
extern cvar_t	*g_saber_color;
extern cvar_t	*g_saber2_color;

/*
===================
Svcmd_EntityList_f
===================
*/
void	Svcmd_EntityList_f (void) {
	int			e;
	gentity_t		*check;

	check = g_entities;
	for (e = 0; e < globals.num_entities ; e++, check++) {
		if ( !check->inuse ) {
			continue;
		}
		gi.Printf("%3i:", e);
		switch ( check->s.eType ) {
		case ET_GENERAL:
			gi.Printf( "ET_GENERAL          " );
			break;
		case ET_PLAYER:
			gi.Printf( "ET_PLAYER           " );
			break;
		case ET_ITEM:
			gi.Printf( "ET_ITEM             " );
			break;
		case ET_MISSILE:
			gi.Printf( "ET_MISSILE          " );
			break;
		case ET_MOVER:
			gi.Printf( "ET_MOVER            " );
			break;
		case ET_BEAM:
			gi.Printf( "ET_BEAM             " );
			break;
		case ET_PORTAL:
			gi.Printf( "ET_PORTAL           " );
			break;
		case ET_SPEAKER:
			gi.Printf( "ET_SPEAKER          " );
			break;
		case ET_PUSH_TRIGGER:
			gi.Printf( "ET_PUSH_TRIGGER     " );
			break;
		case ET_TELEPORT_TRIGGER:
			gi.Printf( "ET_TELEPORT_TRIGGER " );
			break;
		case ET_INVISIBLE:
			gi.Printf( "ET_INVISIBLE        " );
			break;
		case ET_THINKER:
			gi.Printf( "ET_THINKER          " );
			break;
		case ET_CLOUD:
			gi.Printf( "ET_CLOUD            " );
			break;
		case ET_TERRAIN:
			gi.Printf( "ET_TERRAIN          " );
			break;
		default:
			gi.Printf( "%-3i                ", check->s.eType );
			break;
		}

		if ( check->classname ) {
			gi.Printf("%s", check->classname);
		}
		gi.Printf("\n");
	}
}

//---------------------------
extern void G_StopCinematicSkip( void );
extern void G_StartCinematicSkip( void );
extern void ExitEmplacedWeapon( gentity_t *ent );
static void Svcmd_ExitView_f( void )
{
extern cvar_t	*g_skippingcin;
	static int exitViewDebounce = 0;
	if ( exitViewDebounce > level.time )
	{
		return;
	}
	exitViewDebounce = level.time + 500;
	if ( in_camera )
	{//see if we need to exit an in-game cinematic
		if ( g_skippingcin->integer )	// already doing cinematic skip?
		{// yes...   so stop skipping...
			G_StopCinematicSkip();
		}
		else
		{// no... so start skipping...
			G_StartCinematicSkip();
		}
	}
	else if ( !G_ClearViewEntity( player ) )
	{//didn't exit control of a droid or turret
		//okay, now try exiting emplaced guns or AT-ST's
		if ( player->s.eFlags & EF_LOCKED_TO_WEAPON )
		{//get out of emplaced gun
			ExitEmplacedWeapon( player );
		}
		else if ( player->client && player->client->NPC_class == CLASS_ATST )
		{//a player trying to get out of his ATST
			GEntity_UseFunc( player->activator, player, player );
		}
	}
}

gentity_t *G_GetSelfForPlayerCmd( void )
{
	if ( g_entities[0].client->ps.viewEntity > 0
		&& g_entities[0].client->ps.viewEntity < ENTITYNUM_WORLD
		&& g_entities[g_entities[0].client->ps.viewEntity].client
		&& g_entities[g_entities[0].client->ps.viewEntity].s.weapon == WP_SABER )
	{//you're controlling another NPC
		return (&g_entities[g_entities[0].client->ps.viewEntity]);
	}
	else
	{
		return (&g_entities[0]);
	}
}

static void Svcmd_Saber_f()
{
	const char *saber = gi.argv(1);
	const char *saber2 = gi.argv(2);
	char name[MAX_CVAR_VALUE_STRING] = {0};

	if ( gi.argc() < 2 )
	{
		gi.Printf( "Usage: saber <saber1> <saber2>\n" );
		gi.Cvar_VariableStringBuffer( "g_saber", name, sizeof(name) );
		gi.Printf("g_saber is set to %s\n", name);
		gi.Cvar_VariableStringBuffer( "g_saber2", name, sizeof(name) );
		if ( name[0] )
			gi.Printf("g_saber2 is set to %s\n", name);
		return;
	}

	if ( !g_entities[0].client || !saber || !saber[0] )
	{
		return;
	}

	gi.cvar_set( "g_saber", saber );
	WP_SetSaber( &g_entities[0], 0, saber );
	if ( saber2 && saber2[0] && !(g_entities[0].client->ps.saber[0].saberFlags&SFL_TWO_HANDED) )
	{//want to use a second saber and first one is not twoHanded
		gi.cvar_set( "g_saber2", saber2 );
		WP_SetSaber( &g_entities[0], 1, saber2 );
	}
	else
	{
		gi.cvar_set( "g_saber2", "" );
		WP_RemoveSaber( &g_entities[0], 1 );
	}
}

static void Svcmd_SaberBlade_f()
{
	if ( gi.argc() < 2 )
	{
		gi.Printf( "USAGE: saberblade <sabernum> <bladenum> [0 = off, 1 = on, no arg = toggle]\n" );
		return;
	}
	if ( &g_entities[0] == NULL || g_entities[0].client == NULL )
	{
		return;
	}
	int sabernum = atoi(gi.argv(1)) - 1;
	if ( sabernum < 0 || sabernum > 1 )
	{
		return;
	}
	if ( sabernum > 0 && !g_entities[0].client->ps.dualSabers )
	{
		return;
	}
	//FIXME: what if don't even have a single saber at all?
	int bladenum = atoi(gi.argv(2)) - 1;
	if ( bladenum < 0 || bladenum >= g_entities[0].client->ps.saber[sabernum].numBlades )
	{
		return;
	}
	qboolean turnOn;
	if ( gi.argc() > 2 )
	{//explicit
		turnOn = (qboolean)(atoi(gi.argv(3))!=0);
	}
	else
	{//toggle
		turnOn = (qboolean)!g_entities[0].client->ps.saber[sabernum].blade[bladenum].active;
	}

	g_entities[0].client->ps.SaberBladeActivate( sabernum, bladenum, turnOn );
}

static void Svcmd_SaberColor_f()
{//FIXME: just list the colors, each additional listing sets that blade
	int saberNum = atoi(gi.argv(1));
	const char *color[MAX_BLADES];
	int bladeNum;

	for ( bladeNum = 0; bladeNum < MAX_BLADES; bladeNum++ )
	{
		color[bladeNum] = gi.argv(2+bladeNum);
	}

	if ( saberNum < 1 || saberNum > 2 || gi.argc() < 3 )
	{
		gi.Printf( "Usage:  saberColor <saberNum> <blade1 color> <blade2 color> ... <blade8 color>\n" );
		gi.Printf( "valid saberNums:  1 or 2\n" );
		gi.Printf( "valid colors:  red, orange, yellow, green, blue, and purple\n" );

		return;
	}
	saberNum--;

	gentity_t *self = G_GetSelfForPlayerCmd();

	for ( bladeNum = 0; bladeNum < MAX_BLADES; bladeNum++ )
	{
		if ( !color[bladeNum] || !color[bladeNum][0] )
		{
			break;
		}
		else
		{
			self->client->ps.saber[saberNum].blade[bladeNum].color = TranslateSaberColor( color[bladeNum] );
		}
	}

	if ( saberNum == 0 )
	{
		gi.cvar_set( "g_saber_color", color[0] );
	}
	else if ( saberNum == 1 )
	{
		gi.cvar_set( "g_saber2_color", color[0] );
	}
}

struct SetForceCmd {
	const char *desc;
	const char *cmdname;
	const int maxlevel;
};

SetForceCmd SetForceTable[NUM_FORCE_POWERS] = {
	{ "forceHeal",			"setForceHeal",			FORCE_LEVEL_3			},
	{ "forceJump",			"setForceJump",			FORCE_LEVEL_3			},
	{ "forceSpeed",			"setForceSpeed",		FORCE_LEVEL_3			},
	{ "forcePush",			"setForcePush",			FORCE_LEVEL_3			},
	{ "forcePull",			"setForcePull",			FORCE_LEVEL_3			},
	{ "forceMindTrick",		"setForceMindTrick",	FORCE_LEVEL_4			},
	{ "forceGrip",			"setForceGrip",			FORCE_LEVEL_3			},
	{ "forceLightning",		"setForceLightning",	FORCE_LEVEL_3			},
	{ "saberThrow",			"setSaberThrow",		FORCE_LEVEL_3			},
	{ "saberDefense",		"setSaberDefense",		FORCE_LEVEL_3			},
	{ "saberOffense",		"setSaberOffense",		SS_NUM_SABER_STYLES-1	},
	{ "forceRage",			"setForceRage",			FORCE_LEVEL_3			},
	{ "forceProtect",		"setForceProtect",		FORCE_LEVEL_3			},
	{ "forceAbsorb",		"setForceAbsorb",		FORCE_LEVEL_3			},
	{ "forceDrain",			"setForceDrain",		FORCE_LEVEL_3			},
	{ "forceSight",			"setForceSight",		FORCE_LEVEL_3			},
};

static void Svcmd_ForceSetLevel_f( int forcePower )
{
	if ( !&g_entities[0] || !g_entities[0].client )
	{
		return;
	}
	const char *newVal = gi.argv(1);
	if ( !VALIDSTRING( newVal ) )
	{
		gi.Printf( "Current %s level is %d\n", SetForceTable[forcePower].desc, g_entities[0].client->ps.forcePowerLevel[forcePower] );
		gi.Printf( "Usage:  %s <level> (0 - %i)\n", SetForceTable[forcePower].cmdname, SetForceTable[forcePower].maxlevel );
		return;
	}
	int val = atoi(newVal);
	if ( val > FORCE_LEVEL_0 )
	{
		g_entities[0].client->ps.forcePowersKnown |= ( 1 << forcePower );
	}
	else
	{
		g_entities[0].client->ps.forcePowersKnown &= ~( 1 << forcePower );
	}
	g_entities[0].client->ps.forcePowerLevel[forcePower] = val;
	if ( g_entities[0].client->ps.forcePowerLevel[forcePower] < FORCE_LEVEL_0 )
	{
		g_entities[0].client->ps.forcePowerLevel[forcePower] = FORCE_LEVEL_0;
	}
	else if ( g_entities[0].client->ps.forcePowerLevel[forcePower] > SetForceTable[forcePower].maxlevel )
	{
		g_entities[0].client->ps.forcePowerLevel[forcePower] = SetForceTable[forcePower].maxlevel;
	}
}

extern qboolean PM_SaberInStart( int move );
extern qboolean PM_SaberInTransition( int move );
extern qboolean PM_SaberInAttack( int move );
extern qboolean WP_SaberCanTurnOffSomeBlades( saberInfo_t *saber );
void Svcmd_SaberAttackCycle_f( void )
{
	if ( !&g_entities[0] || !g_entities[0].client )
	{
		return;
	}

	gentity_t *self = G_GetSelfForPlayerCmd();
	if ( self->s.weapon != WP_SABER )
	{// saberAttackCycle button also switches to saber
		gi.SendConsoleCommand("weapon 1" );
		return;
	}

	if ( self->client->ps.dualSabers )
	{//can't cycle styles with dualSabers, so just toggle second saber on/off
		if ( WP_SaberCanTurnOffSomeBlades( &self->client->ps.saber[1] ) )
		{//can turn second saber off
			if ( self->client->ps.saber[1].ActiveManualOnly() )
			{//turn it off
				qboolean skipThisBlade;
				for ( int bladeNum = 0; bladeNum < self->client->ps.saber[1].numBlades; bladeNum++ )
				{
					skipThisBlade = qfalse;
					if ( WP_SaberBladeUseSecondBladeStyle( &self->client->ps.saber[1], bladeNum ) )
					{//check to see if we should check the secondary style's flags
						if ( (self->client->ps.saber[1].saberFlags2&SFL2_NO_MANUAL_DEACTIVATE2) )
						{
							skipThisBlade = qtrue;
						}
					}
					else
					{//use the primary style's flags
						if ( (self->client->ps.saber[1].saberFlags2&SFL2_NO_MANUAL_DEACTIVATE) )
						{
							skipThisBlade = qtrue;
						}
					}
					if ( !skipThisBlade )
					{
						self->client->ps.saber[1].BladeActivate( bladeNum, qfalse );
						G_SoundIndexOnEnt( self, CHAN_WEAPON, self->client->ps.saber[1].soundOff );
					}
				}
			}
			else if ( !self->client->ps.saber[0].ActiveManualOnly() )
			{//first one is off, too, so just turn that one on
				if ( !self->client->ps.saberInFlight )
				{//but only if it's in your hand!
					self->client->ps.saber[0].Activate();
				}
			}
			else
			{//turn on the second one
				self->client->ps.saber[1].Activate();
			}
			return;
		}
	}
	else if ( self->client->ps.saber[0].numBlades > 1
		&& WP_SaberCanTurnOffSomeBlades( &self->client->ps.saber[0] ) )//self->client->ps.saber[0].type == SABER_STAFF )
	{//can't cycle styles with saberstaff, so just toggles saber blades on/off
		if ( self->client->ps.saberInFlight )
		{//can't turn second blade back on if it's in the air, you naughty boy!
			return;
		}
		/*
		if ( self->client->ps.saber[0].singleBladeStyle == SS_NONE )
		{//can't use just one blade?
			return;
		}
		*/
		qboolean playedSound = qfalse;
		if ( !self->client->ps.saber[0].blade[0].active )
		{//first one is not even on
			//turn only it on
			self->client->ps.SaberBladeActivate( 0, 0, qtrue );
			return;
		}

		qboolean skipThisBlade;
		for ( int bladeNum = 1; bladeNum < self->client->ps.saber[0].numBlades; bladeNum++ )
		{
			if ( !self->client->ps.saber[0].blade[bladeNum].active )
			{//extra is off, turn it on
				self->client->ps.saber[0].BladeActivate( bladeNum, qtrue );
			}
			else
			{//turn extra off
				skipThisBlade = qfalse;
				if ( WP_SaberBladeUseSecondBladeStyle( &self->client->ps.saber[1], bladeNum ) )
				{//check to see if we should check the secondary style's flags
					if ( (self->client->ps.saber[1].saberFlags2&SFL2_NO_MANUAL_DEACTIVATE2) )
					{
						skipThisBlade = qtrue;
					}
				}
				else
				{//use the primary style's flags
					if ( (self->client->ps.saber[1].saberFlags2&SFL2_NO_MANUAL_DEACTIVATE) )
					{
						skipThisBlade = qtrue;
					}
				}
				if ( !skipThisBlade )
				{
					self->client->ps.saber[0].BladeActivate( bladeNum, qfalse );
					if ( !playedSound )
					{
						G_SoundIndexOnEnt( self, CHAN_WEAPON, self->client->ps.saber[0].soundOff );
						playedSound = qtrue;
					}
				}
			}
		}
		return;
	}

	int allowedStyles;
	if ( G_IsOutcast() )
	{
		// JO maps Saber Offense ranks to the classic style progression:
		// rank 1 is medium, rank 2 adds fast, and rank 3 adds strong.
		allowedStyles = 1 << SS_MEDIUM;
		if ( self->client->ps.forcePowerLevel[FP_SABER_OFFENSE] >= FORCE_LEVEL_2 )
		{
			allowedStyles |= 1 << SS_FAST;
		}
		if ( self->client->ps.forcePowerLevel[FP_SABER_OFFENSE] >= FORCE_LEVEL_3 )
		{
			allowedStyles |= 1 << SS_STRONG;
		}
	}
	else
	{
		allowedStyles = self->client->ps.saberStylesKnown;
	}

	if ( self->client->ps.dualSabers
		&& self->client->ps.saber[0].Active()
		&& self->client->ps.saber[1].Active() )
	{
		allowedStyles |= (1<<SS_DUAL);
		for ( int styleNum = SS_NONE+1; styleNum < SS_NUM_SABER_STYLES; styleNum++ )
		{
			if ( styleNum == SS_TAVION
				&& ((self->client->ps.saber[0].stylesLearned&(1<<SS_TAVION))||(self->client->ps.saber[1].stylesLearned&(1<<SS_TAVION)))//was given this style by one of my sabers
				&& !(self->client->ps.saber[0].stylesForbidden&(1<<SS_TAVION))
				&& !(self->client->ps.saber[1].stylesForbidden&(1<<SS_TAVION)) )
			{//if have both sabers on, allow tavion only if one of our sabers specifically wanted to use it... (unless specifically forbidden)
			}
			else if ( styleNum == SS_DUAL
				&& !(self->client->ps.saber[0].stylesForbidden&(1<<SS_DUAL))
				&& !(self->client->ps.saber[1].stylesForbidden&(1<<SS_DUAL)) )
			{//if have both sabers on, only dual style is allowed (unless specifically forbidden)
			}
			else
			{
				allowedStyles &= ~(1<<styleNum);
			}
		}
	}

	if ( !allowedStyles )
	{
		return;
	}

	int	saberAnimLevel;
	if ( !self->s.number )
	{
		saberAnimLevel = cg.saberAnimLevelPending;
	}
	else
	{
		saberAnimLevel = self->client->ps.saberAnimLevel;
	}
	saberAnimLevel++;
	int sanityCheck = 0;
	while ( self->client->ps.saberAnimLevel != saberAnimLevel
		&& !(allowedStyles&(1<<saberAnimLevel))
		&& sanityCheck < SS_NUM_SABER_STYLES+1 )
	{
		saberAnimLevel++;
		if ( saberAnimLevel > SS_STAFF )
		{
			saberAnimLevel = SS_FAST;
		}
		sanityCheck++;
	}

	if ( !(allowedStyles&(1<<saberAnimLevel)) )
	{
		return;
	}

	WP_UseFirstValidSaberStyle( self, &saberAnimLevel );
	if ( !self->s.number )
	{
		cg.saberAnimLevelPending = saberAnimLevel;
	}
	else
	{
		self->client->ps.saberAnimLevel = saberAnimLevel;
	}

#ifndef FINAL_BUILD
	switch ( saberAnimLevel )
	{
	case SS_FAST:
		gi.Printf( S_COLOR_BLUE "Lightsaber Combat Style: Fast\n" );
		//LIGHTSABERCOMBATSTYLE_FAST
		break;
	case SS_MEDIUM:
		gi.Printf( S_COLOR_YELLOW "Lightsaber Combat Style: Medium\n" );
		//LIGHTSABERCOMBATSTYLE_MEDIUM
		break;
	case SS_STRONG:
		gi.Printf( S_COLOR_RED "Lightsaber Combat Style: Strong\n" );
		//LIGHTSABERCOMBATSTYLE_STRONG
		break;
	case SS_DESANN:
		gi.Printf( S_COLOR_CYAN "Lightsaber Combat Style: Desann\n" );
		//LIGHTSABERCOMBATSTYLE_DESANN
		break;
	case SS_TAVION:
		gi.Printf( S_COLOR_MAGENTA "Lightsaber Combat Style: Tavion\n" );
		//LIGHTSABERCOMBATSTYLE_TAVION
		break;
	case SS_DUAL:
		gi.Printf( S_COLOR_MAGENTA "Lightsaber Combat Style: Dual\n" );
		//LIGHTSABERCOMBATSTYLE_TAVION
		break;
	case SS_STAFF:
		gi.Printf( S_COLOR_MAGENTA "Lightsaber Combat Style: Staff\n" );
		//LIGHTSABERCOMBATSTYLE_TAVION
		break;
	}
	//gi.Printf("\n");
#endif
}

qboolean G_ReleaseEntity( gentity_t *grabber )
{
	if ( grabber && grabber->client && grabber->client->ps.heldClient < ENTITYNUM_WORLD )
	{
		gentity_t *heldClient = &g_entities[grabber->client->ps.heldClient];
		grabber->client->ps.heldClient = ENTITYNUM_NONE;
		if ( heldClient && heldClient->client )
		{
			heldClient->client->ps.heldByClient = ENTITYNUM_NONE;

			heldClient->owner = NULL;
		}
		return qtrue;
	}
	return qfalse;
}

void G_GrabEntity( gentity_t *grabber, const char *target )
{
	if ( !grabber || !grabber->client )
	{
		return;
	}
	gentity_t	*heldClient = G_Find( NULL, FOFS(targetname), (char *)target );
	if ( heldClient && heldClient->client && heldClient != grabber )//don't grab yourself, it's not polite
	{//found him
		grabber->client->ps.heldClient = heldClient->s.number;
		heldClient->client->ps.heldByClient = grabber->s.number;

		heldClient->owner = grabber;
	}
}

static void Svcmd_ICARUS_f( void )
{
	Quake3Game()->Svcmd();
}

template <int32_t power>
static void Svcmd_ForceSetLevel_f(void)
{
	Svcmd_ForceSetLevel_f(power);
}

static void Svcmd_SetForceAll_f(void)
{
	for ( int i = FP_HEAL; i < NUM_FORCE_POWERS; i++ )
	{
		Svcmd_ForceSetLevel_f( i );
	}

	if( gi.argc() > 1 )
	{
		for ( int i = SS_NONE+1; i < SS_NUM_SABER_STYLES; i++ )
		{
			g_entities[0].client->ps.saberStylesKnown |= (1<<i);
		}
	}
}

static void Svcmd_SetSaberAll_f(void)
{
	Svcmd_ForceSetLevel_f( FP_SABERTHROW );
	Svcmd_ForceSetLevel_f( FP_SABER_DEFENSE );
	Svcmd_ForceSetLevel_f( FP_SABER_OFFENSE );
	for ( int i = SS_NONE+1; i < SS_NUM_SABER_STYLES; i++ )
	{
		g_entities[0].client->ps.saberStylesKnown |= (1<<i);
	}
}

static void Svcmd_RunScript_f(void)
{
	const char *cmd2 = gi.argv(1);

	if ( cmd2 && cmd2[0] )
	{
		const char *cmd3 = gi.argv(2);
		if ( cmd3 && cmd3[0] )
		{
			gentity_t *found = NULL;
			if ( (found = G_Find(NULL, FOFS(targetname), cmd2 ) ) != NULL )
			{
				Quake3Game()->RunScript( found, cmd3 );
			}
			else
			{
				//can't find cmd2
				gi.Printf( S_COLOR_RED "runscript: can't find targetname %s\n", cmd2 );
			}
		}
		else
		{
			Quake3Game()->RunScript( &g_entities[0], cmd2 );
		}
	}
	else
	{
		gi.Printf( S_COLOR_RED "usage: runscript <ent targetname> scriptname\n" );
	}
}

static void Svcmd_PlayerTeam_f(void)
{
	const char *cmd2 = gi.argv(1);

	if ( !*cmd2 || !cmd2[0] )
	{
		gi.Printf( S_COLOR_RED "'playerteam' - change player team, requires a team name!\n" );
		gi.Printf( S_COLOR_RED "Current team is: %s\n", GetStringForID( TeamTable, g_entities[0].client->playerTeam ) );
		gi.Printf( S_COLOR_RED "Valid team names are:\n");
		for ( int n = (TEAM_FREE + 1); n < TEAM_NUM_TEAMS; n++ )
		{
			gi.Printf( S_COLOR_RED "%s\n", GetStringForID( TeamTable, n ) );
		}
	}
	else
	{
		team_t	team;

		team = (team_t)GetIDForString( TeamTable, cmd2 );
		if ( team == (team_t)-1 )
		{
			gi.Printf( S_COLOR_RED "'playerteam' unrecognized team name %s!\n", cmd2 );
			gi.Printf( S_COLOR_RED "Current team is: %s\n", GetStringForID( TeamTable, g_entities[0].client->playerTeam ) );
			gi.Printf( S_COLOR_RED "Valid team names are:\n");
			for ( int n = TEAM_FREE; n < TEAM_NUM_TEAMS; n++ )
			{
				gi.Printf( S_COLOR_RED "%s\n", GetStringForID( TeamTable, n ) );
			}
		}
		else
		{
			g_entities[0].client->playerTeam = team;
			//FIXME: convert Imperial, Malon, Hirogen and Klingon to Scavenger?
		}
	}
}

static void Svcmd_Control_f(void)
{
	const char	*cmd2 = gi.argv(1);
	if ( !*cmd2 || !cmd2[0] )
	{
		if ( !G_ClearViewEntity( &g_entities[0] ) )
		{
			gi.Printf( S_COLOR_RED "control <NPC_targetname>\n", cmd2 );
		}
	}
	else
	{
		Q3_SetViewEntity( 0, cmd2 );
	}
}

static void Svcmd_Grab_f(void)
{
	const char	*cmd2 = gi.argv(1);
	if ( !*cmd2 || !cmd2[0] )
	{
		if ( !G_ReleaseEntity( &g_entities[0] ) )
		{
			gi.Printf( S_COLOR_RED "grab <NPC_targetname>\n", cmd2 );
		}
	}
	else
	{
		G_GrabEntity( &g_entities[0], cmd2 );
	}
}

static void Svcmd_Knockdown_f(void)
{
	G_Knockdown( &g_entities[0], &g_entities[0], vec3_origin, 300, qtrue );
}

static void Svcmd_PlayerModel_f(void)
{
	if ( gi.argc() == 1 )
	{
		gi.Printf( S_COLOR_RED "USAGE: playerModel <NPC Name>\n       playerModel <g2model> <skinhead> <skintorso> <skinlower>\n       playerModel player (builds player from customized menu settings)" S_COLOR_WHITE "\n" );
		gi.Printf( "playerModel = %s ", va("%s %s %s %s\n", g_char_model->string, g_char_skin_head->string, g_char_skin_torso->string, g_char_skin_legs->string ) );
	}
	else if ( gi.argc() == 2 )
	{
		G_ChangePlayerModel( &g_entities[0], gi.argv(1) );
	}
	else if (  gi.argc() == 5 )
	{
		//instead of setting it directly via a command, we now store it in cvars
		//G_ChangePlayerModel( &g_entities[0], va("%s|%s|%s|%s", gi.argv(1), gi.argv(2), gi.argv(3), gi.argv(4)) );
		gi.cvar_set("g_char_model", gi.argv(1) );
		gi.cvar_set("g_char_skin_head", gi.argv(2) );
		gi.cvar_set("g_char_skin_torso", gi.argv(3) );
		gi.cvar_set("g_char_skin_legs", gi.argv(4) );
		G_InitPlayerFromCvars( &g_entities[0] );
	}
}

static void Svcmd_PlayerTint_f(void)
{
	if ( gi.argc() == 4 )
	{
		g_entities[0].client->renderInfo.customRGBA[0] = atoi(gi.argv(1));
		g_entities[0].client->renderInfo.customRGBA[1] = atoi(gi.argv(2));
		g_entities[0].client->renderInfo.customRGBA[2] = atoi(gi.argv(3));
		gi.cvar_set("g_char_color_red", gi.argv(1) );
		gi.cvar_set("g_char_color_green", gi.argv(2) );
		gi.cvar_set("g_char_color_blue", gi.argv(3) );
	}
	else
	{
		gi.Printf( S_COLOR_RED "USAGE: playerTint <red 0 - 255> <green 0 - 255> <blue 0 - 255>\n" );
		gi.Printf( "playerTint = %s\n", va("%d %d %d", g_char_color_red->integer, g_char_color_green->integer, g_char_color_blue->integer ) );
	}
}

static void Svcmd_IKnowKungfu_f(void)
{
	gi.cvar_set( "g_debugMelee", "1" );
	G_SetWeapon( &g_entities[0], WP_MELEE );
	for ( int i = FP_FIRST; i < NUM_FORCE_POWERS; i++ )
	{
		g_entities[0].client->ps.forcePowersKnown |= ( 1 << i );
		if ( i == FP_TELEPATHY )
		{
			g_entities[0].client->ps.forcePowerLevel[i] = FORCE_LEVEL_4;
		}
		else
		{
			g_entities[0].client->ps.forcePowerLevel[i] = FORCE_LEVEL_3;
		}
	}
}

static void Svcmd_MissionStatsStatus_f(void)
{
	if (!level.clients) return;
	const auto &s = level.clients[0].sess.missionStats;
	gi.Printf("missionstats live map=%s kills=%d secrets=%d total=%d shots=%d hits=%d push=%d jump=%d thrown=%d blocks=%d time=%d\n",
		level.mapname, s.enemiesKilled, s.secretsFound, s.totalSecrets, s.shotsFired, s.hits,
		s.forceUsed[FP_PUSH], s.forceUsed[FP_LEVITATION], s.saberThrownCnt, s.saberBlocksCnt, level.time);
	char map[MAX_QPATH], favorite[256], secrets[128], accuracy[64];
	gi.Cvar_VariableStringBuffer("ui_stats_map", map, sizeof(map));
	gi.Cvar_VariableStringBuffer("ui_stats_fave", favorite, sizeof(favorite));
	gi.Cvar_VariableStringBuffer("ui_stats_jo_secrets", secrets, sizeof(secrets));
	gi.Cvar_VariableStringBuffer("ui_stats_accuracy", accuracy, sizeof(accuracy));
	gi.Printf("missionstats snapshot source=%s visible=%d kills=%d secrets=\"%s\" shots=%d hits=%d accuracy=\"%s\" favorite=%d label=\"%s\" saber=%d push=%d jump=%d thrown=%d blocks=%d\n",
		map[0] ? map : "none", gi.Cvar_VariableIntegerValue("cg_missionstatusscreen"),
		gi.Cvar_VariableIntegerValue("ui_stats_enemieskilled"), secrets, gi.Cvar_VariableIntegerValue("ui_stats_shots"),
		gi.Cvar_VariableIntegerValue("ui_stats_hits"), accuracy, gi.Cvar_VariableIntegerValue("ui_stats_fave_weapon"), favorite,
		gi.Cvar_VariableIntegerValue("ui_stats_saber"), gi.Cvar_VariableIntegerValue("ui_stats_push"),
		gi.Cvar_VariableIntegerValue("ui_stats_jump"), gi.Cvar_VariableIntegerValue("ui_stats_thrown"), gi.Cvar_VariableIntegerValue("ui_stats_blocks"));
}

static void Svcmd_CampaignStatus_f(void)
{
	const gentity_t *pl = &g_entities[0];
	if (!pl->client) return;
	const playerState_t &ps = pl->client->ps;
	gi.Printf("campaign=%s map=%s camera=%d health=%d weapon=%d weapons=%d force=%d ammo=%d origin=%.1f,%.1f,%.1f\n",
		G_IsOutcast() ? "jo" : "ja", level.mapname, in_camera, ps.stats[STAT_HEALTH],
		ps.weapon, ps.stats[STAT_WEAPONS], ps.forcePowersKnown, ps.ammo[AMMO_BLASTER],
		ps.origin[0], ps.origin[1], ps.origin[2]);
	gi.Printf("equipment goggles=%d selected=%d zoom=%d battery=%d mounted=%d gun_health=%d gun_max=%d\n",
		ps.inventory[INV_LIGHTAMP_GOGGLES], cg.inventorySelect, cg.zoomMode, ps.batteryCharge,
		(ps.eFlags & EF_LOCKED_TO_WEAPON) != 0, pl->owner ? pl->owner->health : 0,
		pl->owner ? pl->owner->max_health : 0);
	gi.Printf("forcelevels push=%d pull=%d jump=%d speed=%d heal=%d grip=%d mindtrick=%d lightning=%d saber=%d defense=%d throw=%d\n",
		ps.forcePowerLevel[FP_PUSH], ps.forcePowerLevel[FP_PULL], ps.forcePowerLevel[FP_LEVITATION],
		ps.forcePowerLevel[FP_SPEED], ps.forcePowerLevel[FP_HEAL], ps.forcePowerLevel[FP_GRIP],
		ps.forcePowerLevel[FP_TELEPATHY], ps.forcePowerLevel[FP_LIGHTNING], ps.forcePowerLevel[FP_SABER_OFFENSE],
		ps.forcePowerLevel[FP_SABER_DEFENSE], ps.forcePowerLevel[FP_SABERTHROW]);
	gi.Printf("world contents=%d\n", gi.pointcontents(ps.origin, pl->s.number));
	extern char *G_GetLocationForEnt(gentity_t *ent);
	const char *location = G_GetLocationForEnt(&g_entities[0]);
	gi.Printf("location=%s\n", location ? location : "none");
	for (int i = 0; i < objectiveCount; ++i)
		if (pl->client->sess.mission_objectives[i].display || (gi.argc() == 2 && !Q_stricmp(gi.argv(1), "all")))
			gi.Printf("objective=%s status=%d\n", objectiveTable[i].name,
				pl->client->sess.mission_objectives[i].status);
}

static void Svcmd_MoverStatus_f(void)
{
	gentity_t *ent = G_Find(NULL, FOFS(targetname), gi.argv(1));
	if (!ent || !ent->bmodel)
	{
		gi.Printf("mover name=%s absent=1\n", gi.argv(1));
		return;
	}
	gi.Printf("mover name=%s origin=%.2f,%.2f,%.2f angles=%.2f,%.2f,%.2f active=%d nav=%d\n", gi.argv(1),
		ent->currentOrigin[0], ent->currentOrigin[1], ent->currentOrigin[2],
		ent->currentAngles[0], ent->currentAngles[1], ent->currentAngles[2], !(ent->svFlags & SVF_INACTIVE), Q3_TaskIDPending(ent, TID_MOVE_NAV));
}

static void Svcmd_GalakTest_f(void)
{
	if (gi.argc() < 2 || gi.argc() > 4)
	{
		gi.Printf("Usage: galak_test <targetname> [damage [generator]]\n");
		return;
	}
	gentity_t *ent = NULL;
	while ((ent = G_Find(ent, FOFS(targetname), gi.argv(1))) != NULL)
	{
		if (!ent->client || !ent->NPC || ent->client->NPC_class != CLASS_GALAKMECH) continue;
		if (gi.argc() >= 3)
		{
			const int damage = atoi(gi.argv(2));
			if (damage < 1 || damage > 10000) return;
			G_Damage(ent, &g_entities[0], &g_entities[0], NULL, ent->currentOrigin,
				damage, 0, MOD_BLASTER,
				!Q_stricmp(gi.argv(3), "generator") ? HL_GENERIC1 : HL_CHEST);
		}
		int missiles = 0;
		for (int i = 0; i < globals.num_entities; ++i)
			if (g_entities[i].inuse && g_entities[i].s.eType == ET_MISSILE && g_entities[i].owner == ent) ++missiles;
		gi.Printf("galak name=%s health=%d armor=%d generator=%d recharge=%d enemy=%d missiles=%d\n",
			ent->targetname, ent->health, ent->client->ps.stats[STAT_ARMOR], ent->locationDamage[HL_GENERIC1],
			ent->NPC->investigateDebounceTime, ent->enemy ? ent->enemy->s.number : -1, missiles);
		return;
	}
	gi.Printf("galak name=%s absent=1\n", gi.argv(1));
}

static void Svcmd_SurfaceStatus_f(void)
{
	if (gi.argc() < 3)
	{
		gi.Printf("Usage: surface_status <targetname> <surface> [...]\n");
		return;
	}
	gentity_t *ent = NULL;
	while ((ent = G_Find(ent, FOFS(targetname), gi.argv(1))) != NULL)
	{
		if (ent->playerModel < 0 || ent->playerModel >= ent->ghoul2.size()) continue;
		for (int i = 2; i < gi.argc(); ++i)
			gi.Printf("surface name=%s surface=%s index=%d flags=%d\n", ent->targetname, gi.argv(i),
				gi.G2API_GetSurfaceIndex(&ent->ghoul2[ent->playerModel], gi.argv(i)),
				gi.G2API_GetSurfaceRenderStatus(&ent->ghoul2[ent->playerModel], gi.argv(i)));
		return;
	}
	gi.Printf("surface name=%s absent=1\n", gi.argv(1));
}

static void Svcmd_CinematicStatus_f(void)
{
	if (gi.argc() != 2)
	{
		gi.Printf("Usage: cinematic_status <targetname>\n");
		return;
	}
	extern stringID_table_t animTable[];
	gentity_t *ent = NULL;
	while ((ent = G_Find(ent, FOFS(targetname), gi.argv(1))) != NULL)
	{
		if (!ent->client || !ent->NPC) continue;
		const playerState_t &ps = ent->client->ps;
		gi.Printf("cinematic name=%s time=%d camera=%d ent=%d origin=%.2f,%.2f,%.2f velocity=%.2f,%.2f,%.2f ground=%d legs=%s torso=%s legs_timer=%d torso_timer=%d nav=%d lower=%d upper=%d both=%d voice=%d behavior=%d noclip=%d\n",
			ent->targetname, level.time, in_camera, ent->s.number,
			ps.origin[0], ps.origin[1], ps.origin[2], ps.velocity[0], ps.velocity[1], ps.velocity[2], ps.groundEntityNum,
			GetStringForID(animTable, ps.legsAnim), GetStringForID(animTable, ps.torsoAnim), ps.legsAnimTimer, ps.torsoAnimTimer,
			Q3_TaskIDPending(ent, TID_MOVE_NAV), Q3_TaskIDPending(ent, TID_ANIM_LOWER), Q3_TaskIDPending(ent, TID_ANIM_UPPER),
			Q3_TaskIDPending(ent, TID_ANIM_BOTH), Q3_TaskIDPending(ent, TID_CHAN_VOICE), ent->NPC->behaviorState, ent->client->noclip);
		gi.Printf("cinematic_combat name=%s health=%d enemy=%d weapon=%d force=%d max_force=%d known=%d active_force=%d blades=%d blade_active=%d blade_length=%.1f hilt=%d yaw=%.2f desired_yaw=%.2f body_yaw=%.2f script_flags=%d\n",
			ent->targetname, ent->health, ent->enemy ? ent->enemy->s.number : -1, ps.weapon,
			ps.forcePower, ps.forcePowerMax, ps.forcePowersKnown, ps.forcePowersActive,
			ps.saber[0].numBlades, ps.saber[0].blade[0].active, ps.saber[0].blade[0].length, ent->weaponModel[0],
			ps.viewangles[YAW], ent->NPC->desiredYaw, ent->client->renderInfo.legsYaw, ent->NPC->scriptFlags);
		gi.Printf("cinematic_saber name=%s color=%d\n", ent->targetname, ps.saber[0].blade[0].color);
		if (ent->NPC->goalEntity)
			gi.Printf("cinematic_goal name=%s origin=%.2f,%.2f,%.2f radius=%d waypoint=%d speed=%d\n", ent->targetname,
				ent->NPC->goalEntity->currentOrigin[0], ent->NPC->goalEntity->currentOrigin[1], ent->NPC->goalEntity->currentOrigin[2],
				ent->NPC->goalRadius, ent->NPC->goalEntity->waypoint, ps.speed);
		if (ent->playerModel >= 0 && ent->playerModel < ent->ghoul2.size() && ent->rootBone >= 0)
		{
			float frame = 0, speed = 0;
			int start = 0, end = 0, flags = 0;
			if (gi.G2API_GetBoneAnimIndex(&ent->ghoul2[ent->playerModel], ent->rootBone, level.time, &frame, &start, &end, &flags, &speed, NULL))
				gi.Printf("cinematic_bone name=%s frame=%.2f start=%d end=%d flags=%d speed=%.2f\n", ent->targetname, frame, start, end, flags, speed);
		}
		const int setIndex = ent->client->clientInfo.animFileIndex;
		if (setIndex >= 0 && setIndex < level.numKnownAnimFileSets && ps.legsAnim >= 0 && ps.legsAnim < MAX_ANIMATIONS
			&& ps.torsoAnim >= 0 && ps.torsoAnim < MAX_ANIMATIONS)
		{
			const animFileSet_t &set = level.knownAnimFileSets[setIndex];
			gi.Printf("cinematic_set name=%s profile=%s legs_first=%d legs_frames=%d torso_first=%d torso_frames=%d\n",
				ent->targetname, set.filename, set.animations[ps.legsAnim].firstFrame, set.animations[ps.legsAnim].numFrames,
				set.animations[ps.torsoAnim].firstFrame, set.animations[ps.torsoAnim].numFrames);
		}
		for (int i = 0; i < ent->ghoul2.size(); ++i)
			if (i != ent->playerModel && ent->ghoul2[i].mModelindex >= 0)
				gi.Printf("cinematic_prop name=%s slot=%d model=%s\n", ent->targetname, i, ent->ghoul2[i].mFileName);
		return;
	}
	gi.Printf("cinematic name=%s time=%d camera=%d absent=1\n", gi.argv(1), level.time, in_camera);
}

static void Svcmd_Secrets_f(void)
{
	const gentity_t *pl = &g_entities[0];
	if(pl->client->sess.missionStats.totalSecrets < 1)
	{
		gi.Printf( "There are" S_COLOR_RED " NO " S_COLOR_WHITE "secrets on this map!\n" );
	}
	else if(pl->client->sess.missionStats.secretsFound == pl->client->sess.missionStats.totalSecrets)
	{
		gi.Printf( "You've found all " S_COLOR_GREEN "%i" S_COLOR_WHITE " secrets on this map!\n", pl->client->sess.missionStats.secretsFound );
	}
	else
	{
		gi.Printf( "You've found " S_COLOR_GREEN "%i" S_COLOR_WHITE " out of " S_COLOR_GREEN "%i" S_COLOR_WHITE " secrets!\n", pl->client->sess.missionStats.secretsFound, pl->client->sess.missionStats.totalSecrets );
	}
}

// PADAWAN - g_spskill 0 + cg_crosshairForceHint 1 + handicap 100
// JEDI - g_spskill 1 + cg_crosshairForceHint 1 + handicap 100
// JEDI KNIGHT - g_spskill 2 + cg_crosshairForceHint 0 + handicap 100
// JEDI MASTER - g_spskill 2 + cg_crosshairForceHint 0 + handicap 50

extern cvar_t *g_spskill;
static void Svcmd_Difficulty_f(void)
{
	if(gi.argc() == 1)
	{
		if(g_spskill->integer == 0)
		{
			gi.Printf( S_COLOR_GREEN "Current Difficulty: Padawan" S_COLOR_WHITE "\n" );
		}
		else if(g_spskill->integer == 1)
		{
			gi.Printf( S_COLOR_GREEN "Current Difficulty: Jedi" S_COLOR_WHITE "\n" );
		}
		else if(g_spskill->integer == 2)
		{
			int crosshairHint = gi.Cvar_VariableIntegerValue("cg_crosshairForceHint");
			int handicap = gi.Cvar_VariableIntegerValue("handicap");
			if(handicap == 100 && crosshairHint == 0)
			{
				gi.Printf( S_COLOR_GREEN "Current Difficulty: Jedi Knight" S_COLOR_WHITE "\n" );
			}
			else if(handicap == 50 && crosshairHint == 0)
			{
				gi.Printf( S_COLOR_GREEN "Current Difficulty: Jedi Master" S_COLOR_WHITE "\n" );
			}
			else
			{
				gi.Printf( S_COLOR_GREEN "Current Difficulty: Jedi Knight (Custom)" S_COLOR_WHITE "\n" );
				gi.Printf( S_COLOR_GREEN "Crosshair Force Hint: %i" S_COLOR_WHITE "\n", crosshairHint != 0 ? 1 : 0 );
				gi.Printf( S_COLOR_GREEN "Handicap: %i" S_COLOR_WHITE "\n", handicap );
			}
		}
		else
		{
			gi.Printf( S_COLOR_RED "Invalid difficulty cvar set! g_spskill (%i) [0-2] is valid range only" S_COLOR_WHITE "\n", g_spskill->integer );
		}
	}
}

#define CMD_NONE				(0x00000000u)
#define CMD_CHEAT				(0x00000001u)
#define CMD_ALIVE				(0x00000002u)

typedef struct svcmd_s {
	const char	*name;
	void		(*func)(void);
	uint32_t	flags;
} svcmd_t;

static int svcmdcmp( const void *a, const void *b ) {
	return Q_stricmp( (const char *)a, ((svcmd_t*)b)->name );
}

// FIXME some of these should be made CMD_ALIVE too!
static svcmd_t svcmds[] = {
	{ "campaign_status", Svcmd_CampaignStatus_f, CMD_NONE },
	{ "missionstats_status", Svcmd_MissionStatsStatus_f, CMD_NONE },
	{ "jo_prepare", G_JoPreparationCommand, CMD_NONE },
	{ "cinematic_status", Svcmd_CinematicStatus_f, CMD_NONE },
	{ "surface_status", Svcmd_SurfaceStatus_f, CMD_NONE },
	{ "galak_test", Svcmd_GalakTest_f, CMD_CHEAT },
	{ "mover_status", Svcmd_MoverStatus_f, CMD_NONE },
#ifdef USE_JOLT_REACTIONS
	{ "jolt_select", G_JoltSelect_f, CMD_CHEAT | CMD_ALIVE },
	{ "jolt_status", G_JoltStatus_f, CMD_NONE },
	{ "jolt_hit", G_JoltHit_f, CMD_CHEAT | CMD_ALIVE },
	{ "jolt_knockdown", G_JoltKnockdown_f, CMD_CHEAT | CMD_ALIVE },
	{ "jolt_impulse", G_JoltImpulse_f, CMD_CHEAT | CMD_ALIVE },
	{ "jolt_control", G_JoltControl_f, CMD_CHEAT | CMD_ALIVE },
	{ "jolt_shoot", G_JoltShoot_f, CMD_CHEAT | CMD_ALIVE },
	{ "jolt_balance", G_JoltBalance_f, CMD_CHEAT | CMD_ALIVE },
	{ "jolt_push", G_JoltPush_f, CMD_CHEAT | CMD_ALIVE },
	{ "jolt_demo", G_JoltDemo_f, CMD_CHEAT | CMD_ALIVE },
	{ "jolt_blast", G_JoltBlast_f, CMD_CHEAT | CMD_ALIVE },
#endif
	{ "entitylist",					Svcmd_EntityList_f,							CMD_NONE },
	{ "game_memory",				Svcmd_GameMem_f,							CMD_NONE },

	{ "nav",						Svcmd_Nav_f,								CMD_CHEAT },
	{ "npc",						Svcmd_NPC_f,								CMD_CHEAT },
	{ "use",						Svcmd_Use_f,								CMD_CHEAT },
	{ "ICARUS",						Svcmd_ICARUS_f,								CMD_CHEAT },

	{ "saberColor",					Svcmd_SaberColor_f,							CMD_CHEAT },
	{ "saber",						Svcmd_Saber_f,								CMD_CHEAT },
	{ "saberBlade",					Svcmd_SaberBlade_f,							CMD_CHEAT },

	{ "setForceJump",				Svcmd_ForceSetLevel_f<FP_LEVITATION>,		CMD_CHEAT },
	{ "setSaberThrow",				Svcmd_ForceSetLevel_f<FP_SABERTHROW>,		CMD_CHEAT },
	{ "setForceHeal",				Svcmd_ForceSetLevel_f<FP_HEAL>,				CMD_CHEAT },
	{ "setForcePush",				Svcmd_ForceSetLevel_f<FP_PUSH>,				CMD_CHEAT },
	{ "setForcePull",				Svcmd_ForceSetLevel_f<FP_PULL>,				CMD_CHEAT },
	{ "setForceSpeed",				Svcmd_ForceSetLevel_f<FP_SPEED>,			CMD_CHEAT },
	{ "setForceGrip",				Svcmd_ForceSetLevel_f<FP_GRIP>,				CMD_CHEAT },
	{ "setForceLightning",			Svcmd_ForceSetLevel_f<FP_LIGHTNING>,		CMD_CHEAT },
	{ "setMindTrick",				Svcmd_ForceSetLevel_f<FP_TELEPATHY>,		CMD_CHEAT },
	{ "setSaberDefense",			Svcmd_ForceSetLevel_f<FP_SABER_DEFENSE>,	CMD_CHEAT },
	{ "setSaberOffense",			Svcmd_ForceSetLevel_f<FP_SABER_OFFENSE>,	CMD_CHEAT },
	{ "setForceRage",				Svcmd_ForceSetLevel_f<FP_RAGE>,				CMD_CHEAT },
	{ "setForceDrain",				Svcmd_ForceSetLevel_f<FP_DRAIN>,			CMD_CHEAT },
	{ "setForceProtect",			Svcmd_ForceSetLevel_f<FP_PROTECT>,			CMD_CHEAT },
	{ "setForceAbsorb",				Svcmd_ForceSetLevel_f<FP_ABSORB>,			CMD_CHEAT },
	{ "setForceSight",				Svcmd_ForceSetLevel_f<FP_SEE>,				CMD_CHEAT },
	{ "setForceAll",				Svcmd_SetForceAll_f,						CMD_CHEAT },
	{ "setSaberAll",				Svcmd_SetSaberAll_f,						CMD_CHEAT },

	{ "saberAttackCycle",			Svcmd_SaberAttackCycle_f,					CMD_NONE },

	{ "runscript",					Svcmd_RunScript_f,							CMD_CHEAT },

	{ "playerTeam",					Svcmd_PlayerTeam_f,							CMD_CHEAT },

	{ "control",					Svcmd_Control_f,							CMD_CHEAT },
	{ "grab",						Svcmd_Grab_f,								CMD_CHEAT },
	{ "knockdown",					Svcmd_Knockdown_f,							CMD_CHEAT },

	{ "playerModel",				Svcmd_PlayerModel_f,						CMD_NONE },
	{ "playerTint",					Svcmd_PlayerTint_f,							CMD_NONE },

	{ "nexttestaxes",				G_NextTestAxes,								CMD_NONE },

	{ "exitview",					Svcmd_ExitView_f,							CMD_NONE },

	{ "iknowkungfu",				Svcmd_IKnowKungfu_f,						CMD_CHEAT },

	{ "secrets",					Svcmd_Secrets_f,							CMD_NONE },
	{ "difficulty",					Svcmd_Difficulty_f,							CMD_NONE },

	//{ "say",						Svcmd_Say_f,						qtrue },
	//{ "toggleallowvote",			Svcmd_ToggleAllowVote_f,			qfalse },
	//{ "toggleuserinfovalidation",	Svcmd_ToggleUserinfoValidation_f,	qfalse },
};
static const size_t numsvcmds = ARRAY_LEN( svcmds );

/*
=================
ConsoleCommand
=================
*/
qboolean	ConsoleCommand( void ) {
	const char *cmd = gi.argv(0);
	const svcmd_t *command = (const svcmd_t *)Q_LinearSearch( cmd, svcmds, numsvcmds, sizeof( svcmds[0] ), svcmdcmp );

	if ( !command )
		return qfalse;

	if ( (command->flags & CMD_CHEAT)
		&& !g_cheats->integer )
	{
		gi.Printf( "Cheats are not enabled on this server.\n" );
		return qtrue;
	}
	else if ( (command->flags & CMD_ALIVE)
		&& (g_entities[0].health <= 0) )
	{
		gi.Printf( "You must be alive to use this command.\n" );
		return qtrue;
	}
	else
		command->func();
	return qtrue;
}
