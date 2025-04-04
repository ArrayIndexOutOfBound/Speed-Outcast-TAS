// cg_consolecmds.c -- text commands typed in at the local console, or
// executed by a key binding

// this line must stay at top so the whole PCH thing works...
#include "cg_headers.h"

//#include "cg_local.h"
#include "cg_media.h"	//just for cgs....

void CG_TargetCommand_f( void );
extern qboolean	player_locked;
extern void CMD_CGCam_Disable( void );
void CG_NextInventory_f( void );
void CG_PrevInventory_f( void );
void CG_NextForcePower_f( void );
void CG_PrevForcePower_f( void );
void CG_LoadHud_f( void );


/*
=============
CG_Viewpos_f

Debugging command to print the current position
=============
*/
static void CG_Viewpos_f (void) {
	CG_Printf ("%s (%i %i %i) : %i\n", cgs.mapname, (int)cg.refdef.vieworg[0],
		(int)cg.refdef.vieworg[1], (int)cg.refdef.vieworg[2], 
		(int)cg.refdefViewAngles[YAW]);
}

void CG_WriteCam_f (void)
{
	char	text[1024];
	char	*targetname;
	static	int	numCams;

	numCams++;
	
	targetname = (char	*)CG_Argv(1);

	if( !targetname || !targetname[0] )
	{
		targetname = "nameme!";
	}

	CG_Printf( "Camera #%d ('%s') written to: ", numCams, targetname );
	sprintf( text, "//entity %d\n{\n\"classname\"	\"ref_tag\"\n\"targetname\"	\"%s\"\n\"origin\" \"%i %i %i\"\n\"angles\" \"%i %i %i\"\n\"fov\" \"%i\"\n}\n", numCams, targetname, (int)cg.refdef.vieworg[0], (int)cg.refdef.vieworg[1], (int)cg.refdef.vieworg[2], (int)cg.refdefViewAngles[0], (int)cg.refdefViewAngles[1], (int)cg.refdefViewAngles[2], cg_fov.integer );
	gi.WriteCam( text );
}

void Lock_Disable ( void )
{
	player_locked = qfalse;
}

extern float cg_zoomFov;	//from cg_view.cpp

void CG_ToggleBinoculars( void )
{
	if ( in_camera || !cg.snap )
	{
		return;
	}

	if ( cg.zoomMode == 0 || cg.zoomMode >= 2 ) // not zoomed or currently zoomed with the disruptor or LA goggles
	{
		if ( (cg.snap->ps.saberActive && cg.snap->ps.saberInFlight) || cg.snap->ps.stats[STAT_HEALTH] <= 0)
		{//can't select binoculars when throwing saber
			//FIXME: indicate this to the player
			return;
		}

		if ( cg.snap->ps.viewEntity || ( cg_entities[cg.snap->ps.clientNum].currentState.eFlags & ( EF_LOCKED_TO_WEAPON | EF_IN_ATST )))
		{
			// can't zoom when you have a viewEntity or driving an atst or in an emplaced gun
			return;
		}

		cg.zoomMode = 1;
		cg.zoomLocked = qfalse;

		if ( cg.snap->ps.batteryCharge )
		{
			// when you have batteries, you can actually zoom in
			cg_zoomFov = 40.0f;
		}
		else if ( cg.overrides.active & CG_OVERRIDE_FOV )
		{
			cg_zoomFov = cg.overrides.fov;
		}
		else
		{
			cg_zoomFov = cg_fov.value;
		}

		cgi_S_StartSound( NULL, cg.snap->ps.clientNum, CHAN_AUTO, cgs.media.zoomStart );
#ifdef _IMMERSION
		cgi_FF_Start( cgs.media.zoomStartForce, cg.snap->ps.clientNum );
#endif // _IMMERSION
	}
	else
	{
		cg.zoomMode = 0;
		cg.zoomTime = cg.time;
		cgi_S_StartSound( NULL, cg.snap->ps.clientNum, CHAN_AUTO, cgs.media.zoomEnd );
#ifdef _IMMERSION
		cgi_FF_Start( cgs.media.zoomEndForce, cg.snap->ps.clientNum );
#endif // _IMMERSION
	}
}

void CG_ToggleLAGoggles( void )
{
	if ( in_camera || !cg.snap)
	{
		return;
	}

	if ( cg.zoomMode == 0 || cg.zoomMode < 3 ) // not zoomed or currently zoomed with the disruptor or regular binoculars
	{
		if ( (cg.snap->ps.saberActive && cg.snap->ps.saberInFlight) || cg.snap->ps.stats[STAT_HEALTH] <= 0 )
		{//can't select binoculars when throwing saber
			//FIXME: indicate this to the player
			return;
		}

		if ( cg.snap->ps.viewEntity || ( cg_entities[cg.snap->ps.clientNum].currentState.eFlags & ( EF_LOCKED_TO_WEAPON | EF_IN_ATST )))
		{
			// can't zoom when you have a viewEntity or driving an atst or in an emplaced gun
			return;
		}

		cg.zoomMode = 3;
		cg.zoomLocked = qfalse;
		if ( cg.overrides.active & CG_OVERRIDE_FOV )
		{
			cg_zoomFov = cg.overrides.fov;
		}
		else
		{
			cg_zoomFov = cg_fov.value; // does not zoom!!
		}

		cgi_S_StartSound( NULL, cg.snap->ps.clientNum, CHAN_AUTO, cgs.media.zoomStart );
#ifdef _IMMERSION
		cgi_FF_Start( cgs.media.zoomStartForce, cg.snap->ps.clientNum );
#endif // _IMMERSION
	}
	else
	{
		cg.zoomMode = 0;
		cg.zoomTime = cg.time;
		cgi_S_StartSound( NULL, cg.snap->ps.clientNum, CHAN_AUTO, cgs.media.zoomEnd );
#ifdef _IMMERSION
		cgi_FF_Start( cgs.media.zoomEndForce, cg.snap->ps.clientNum );
#endif // _IMMERSION
	}
}

static void CG_InfoDown_f( void ) {
//	cg.showInformation = qtrue;
}

static void CG_InfoUp_f( void ) 
{
//	cg.showInformation = qfalse;
}

static void CG_SetJumpHelperColorBackground_f( void ) {
	if (cgi_Argc() != 5) {
		Com_Printf("Usage: jumpHelperColorBackground <red 0-1> <green 0-1> <blue 0-1> <alpha 0-1>\n" );
		Com_Printf("Current color is: %f %f %f %f\n",
		           cg_jumpHelperColorBackgroundR.value,
		           cg_jumpHelperColorBackgroundG.value,
		           cg_jumpHelperColorBackgroundB.value,
		           cg_jumpHelperColorBackgroundA.value);
		return;
	}
	cgi_Cvar_Set("cg_jumpHelperColorBackgroundR", CG_Argv(1));
	cgi_Cvar_Set("cg_jumpHelperColorBackgroundG", CG_Argv(2));
	cgi_Cvar_Set("cg_jumpHelperColorBackgroundB", CG_Argv(3));
	cgi_Cvar_Set("cg_jumpHelperColorBackgroundA", CG_Argv(4));
}

static void CG_SetJumpHelperColorCancel_f( void ) {
	if (cgi_Argc() != 5) {
		Com_Printf("Usage: jumpHelperColorCancel <red 0-1> <green 0-1> <blue 0-1> <alpha 0-1>\n" );
		Com_Printf("Current color is: %f %f %f %f\n",
		           cg_jumpHelperColorCancelR.value,
		           cg_jumpHelperColorCancelG.value,
		           cg_jumpHelperColorCancelB.value,
		           cg_jumpHelperColorCancelA.value);
		return;
	}
	cgi_Cvar_Set("cg_jumpHelperColorCancelR", CG_Argv(1));
	cgi_Cvar_Set("cg_jumpHelperColorCancelG", CG_Argv(2));
	cgi_Cvar_Set("cg_jumpHelperColorCancelB", CG_Argv(3));
	cgi_Cvar_Set("cg_jumpHelperColorCancelA", CG_Argv(4));
}

static void CG_SetJumpHelperColorCrouch_f( void ) {
	if (cgi_Argc() != 5) {
		Com_Printf("Usage: jumpHelperColorCrouch <red 0-1> <green 0-1> <blue 0-1> <alpha 0-1>\n" );
		Com_Printf("Current color is: %f %f %f %f\n",
		           cg_jumpHelperColorCrouchR.value,
		           cg_jumpHelperColorCrouchG.value,
		           cg_jumpHelperColorCrouchB.value,
		           cg_jumpHelperColorCrouchA.value);
		return;
	}
	cgi_Cvar_Set("cg_jumpHelperColorCrouchR", CG_Argv(1));
	cgi_Cvar_Set("cg_jumpHelperColorCrouchG", CG_Argv(2));
	cgi_Cvar_Set("cg_jumpHelperColorCrouchB", CG_Argv(3));
	cgi_Cvar_Set("cg_jumpHelperColorCrouchA", CG_Argv(4));
}

static void CG_SetJumpHelperColorCrouchExtend_f( void ) {
	if (cgi_Argc() != 5) {
		Com_Printf("Usage: jumpHelperColorCrouchExtend <red 0-1> <green 0-1> <blue 0-1> <alpha 0-1>\n" );
		Com_Printf("Current color is: %f %f %f %f\n",
		           cg_jumpHelperColorCrouchExtendR.value,
		           cg_jumpHelperColorCrouchExtendG.value,
		           cg_jumpHelperColorCrouchExtendB.value,
		           cg_jumpHelperColorCrouchExtendA.value);
		return;
	}
	cgi_Cvar_Set("cg_jumpHelperColorCrouchExtendR", CG_Argv(1));
	cgi_Cvar_Set("cg_jumpHelperColorCrouchExtendG", CG_Argv(2));
	cgi_Cvar_Set("cg_jumpHelperColorCrouchExtendB", CG_Argv(3));
	cgi_Cvar_Set("cg_jumpHelperColorCrouchExtendA", CG_Argv(4));
}

static void CG_SetJumpHelperColorCurrent_f( void ) {
	if (cgi_Argc() != 5) {
		Com_Printf("Usage: jumpHelperColorCurrent <red 0-1> <green 0-1> <blue 0-1> <alpha 0-1>\n" );
		Com_Printf("Current color is: %f %f %f %f\n",
		           cg_jumpHelperColorCurrentR.value,
		           cg_jumpHelperColorCurrentG.value,
		           cg_jumpHelperColorCurrentB.value,
		           cg_jumpHelperColorCurrentA.value);
		return;
	}
	cgi_Cvar_Set("cg_jumpHelperColorCurrentR", CG_Argv(1));
	cgi_Cvar_Set("cg_jumpHelperColorCurrentG", CG_Argv(2));
	cgi_Cvar_Set("cg_jumpHelperColorCurrentB", CG_Argv(3));
	cgi_Cvar_Set("cg_jumpHelperColorCurrentA", CG_Argv(4));
}

static void CG_SetJumpHelperColorExtend_f( void ) {
	if (cgi_Argc() != 5) {
		Com_Printf("Usage: jumpHelperColorExtend <red 0-1> <green 0-1> <blue 0-1> <alpha 0-1>\n" );
		Com_Printf("Current color is: %f %f %f %f\n",
		           cg_jumpHelperColorExtendR.value,
		           cg_jumpHelperColorExtendG.value,
		           cg_jumpHelperColorExtendB.value,
		           cg_jumpHelperColorExtendA.value);
		return;
	}
	cgi_Cvar_Set("cg_jumpHelperColorExtendR", CG_Argv(1));
	cgi_Cvar_Set("cg_jumpHelperColorExtendG", CG_Argv(2));
	cgi_Cvar_Set("cg_jumpHelperColorExtendB", CG_Argv(3));
	cgi_Cvar_Set("cg_jumpHelperColorExtendA", CG_Argv(4));
}

static void CG_SetJumpHelperColorOptimal_f( void ) {
	if (cgi_Argc() != 5) {
		Com_Printf("Usage: jumpHelperColorOptimal <red 0-1> <green 0-1> <blue 0-1> <alpha 0-1>\n" );
		Com_Printf("Current color is: %f %f %f %f\n",
		           cg_jumpHelperColorOptimalR.value,
		           cg_jumpHelperColorOptimalG.value,
		           cg_jumpHelperColorOptimalB.value,
		           cg_jumpHelperColorOptimalA.value);
		return;
	}
	cgi_Cvar_Set("cg_jumpHelperColorOptimalR", CG_Argv(1));
	cgi_Cvar_Set("cg_jumpHelperColorOptimalG", CG_Argv(2));
	cgi_Cvar_Set("cg_jumpHelperColorOptimalB", CG_Argv(3));
	cgi_Cvar_Set("cg_jumpHelperColorOptimalA", CG_Argv(4));
}

static void CG_SetSpeedColor_f( void ) {
	if (cgi_Argc() != 5) {
		Com_Printf("Usage: speedColor <red 0-1> <green 0-1> <blue 0-1> <alpha 0-1>\n" );
		Com_Printf("Current color is: %f %f %f %f\n",
		           cg_speedColorR.value,
		           cg_speedColorG.value,
		           cg_speedColorB.value,
		           cg_speedColorA.value);
		return;
	}
	cgi_Cvar_Set("cg_speedColorR", CG_Argv(1));
	cgi_Cvar_Set("cg_speedColorG", CG_Argv(2));
	cgi_Cvar_Set("cg_speedColorB", CG_Argv(3));
	cgi_Cvar_Set("cg_speedColorA", CG_Argv(4));
}

static void CG_SetStrafeHelperColorAccelerating_f( void ) {
	if (cgi_Argc() != 5) {
		Com_Printf("Usage: strafeHelperColorAccelerating <red 0-1> <green 0-1> <blue 0-1> <alpha 0-1>\n" );
		Com_Printf("Current color is: %f %f %f %f\n",
		           cg_strafeHelperColorAcceleratingR.value,
		           cg_strafeHelperColorAcceleratingG.value,
		           cg_strafeHelperColorAcceleratingB.value,
		           cg_strafeHelperColorAcceleratingA.value);
		return;
	}
	cgi_Cvar_Set("cg_strafeHelperColorAcceleratingR", CG_Argv(1));
	cgi_Cvar_Set("cg_strafeHelperColorAcceleratingG", CG_Argv(2));
	cgi_Cvar_Set("cg_strafeHelperColorAcceleratingB", CG_Argv(3));
	cgi_Cvar_Set("cg_strafeHelperColorAcceleratingA", CG_Argv(4));
}

static void CG_SetStrafeHelperColorOptimal_f( void ) {
	if (cgi_Argc() != 5) {
		Com_Printf("Usage: strafeHelperColorOptimal <red 0-1> <green 0-1> <blue 0-1> <alpha 0-1>\n" );
		Com_Printf("Current color is: %f %f %f %f\n",
		           cg_strafeHelperColorOptimalR.value,
		           cg_strafeHelperColorOptimalG.value,
		           cg_strafeHelperColorOptimalB.value,
		           cg_strafeHelperColorOptimalA.value);
		return;
	}
	cgi_Cvar_Set("cg_strafeHelperColorOptimalR", CG_Argv(1));
	cgi_Cvar_Set("cg_strafeHelperColorOptimalG", CG_Argv(2));
	cgi_Cvar_Set("cg_strafeHelperColorOptimalB", CG_Argv(3));
	cgi_Cvar_Set("cg_strafeHelperColorOptimalA", CG_Argv(4));
}

static void CG_SetStrafeHelperColorCenterMarker_f( void ) {
	if (cgi_Argc() != 5) {
		Com_Printf("Usage: strafeHelperColorCenterMarker <red 0-1> <green 0-1> <blue 0-1> <alpha 0-1>\n" );
		Com_Printf("Current color is: %f %f %f %f\n",
		           cg_strafeHelperColorCenterMarkerR.value,
		           cg_strafeHelperColorCenterMarkerG.value,
		           cg_strafeHelperColorCenterMarkerB.value,
		           cg_strafeHelperColorCenterMarkerA.value);
		return;
	}
	cgi_Cvar_Set("cg_strafeHelperColorCenterMarkerR", CG_Argv(1));
	cgi_Cvar_Set("cg_strafeHelperColorCenterMarkerG", CG_Argv(2));
	cgi_Cvar_Set("cg_strafeHelperColorCenterMarkerB", CG_Argv(3));
	cgi_Cvar_Set("cg_strafeHelperColorCenterMarkerA", CG_Argv(4));
}

static void CG_SetStrafeHelperColorSpeed_f( void ) {
	if (cgi_Argc() != 5) {
		Com_Printf("Usage: strafeHelperColorSpeed <red 0-1> <green 0-1> <blue 0-1> <alpha 0-1>\n" );
		Com_Printf("Current color is: %f %f %f %f\n",
		           cg_strafeHelperColorSpeedR.value,
		           cg_strafeHelperColorSpeedG.value,
		           cg_strafeHelperColorSpeedB.value,
		           cg_strafeHelperColorSpeedA.value);
		return;
	}
	cgi_Cvar_Set("cg_strafeHelperColorSpeedR", CG_Argv(1));
	cgi_Cvar_Set("cg_strafeHelperColorSpeedG", CG_Argv(2));
	cgi_Cvar_Set("cg_strafeHelperColorSpeedB", CG_Argv(3));
	cgi_Cvar_Set("cg_strafeHelperColorSpeedA", CG_Argv(4));
}


typedef struct {
	char	*cmd;
	void	(*function)(void);
} consoleCommand_t;

static consoleCommand_t	commands[] = {
	{ "testgun", CG_TestGun_f },
	{ "testmodel", CG_TestModel_f },
	{ "nextframe", CG_TestModelNextFrame_f },
	{ "prevframe", CG_TestModelPrevFrame_f },
	{ "nextskin", CG_TestModelNextSkin_f },
	{ "prevskin", CG_TestModelPrevSkin_f },
/*
Ghoul2 Insert Start
*/
	{ "testG2Model", CG_TestG2Model_f},
	{ "testsurface", CG_TestModelSurfaceOnOff_f },
	{ "testanglespre", CG_TestModelSetAnglespre_f},
	{ "testanglespost", CG_TestModelSetAnglespost_f},
	{ "testanimate", CG_TestModelAnimate_f},
	{ "testlistbones", CG_ListModelBones_f},
	{ "testlistsurfaces", CG_ListModelSurfaces_f},
/*
Ghoul2 Insert End
*/
	{ "viewpos", CG_Viewpos_f },
	{ "writecam", CG_WriteCam_f },
	{ "+info", CG_InfoDown_f },
	{ "-info", CG_InfoUp_f },
	{ "weapnext", CG_NextWeapon_f },
	{ "weapprev", CG_PrevWeapon_f },
	{ "weapon", CG_Weapon_f },
	{ "tcmd", CG_TargetCommand_f },
	{ "cam_disable", CMD_CGCam_Disable },	//gets out of camera mode for debuggin
	{ "cam_enable", CGCam_Enable },	//gets into camera mode for precise camera placement
	{ "lock_disable", Lock_Disable },	//player can move now
	{ "zoom", CG_ToggleBinoculars },
	{ "la_zoom", CG_ToggleLAGoggles },
	{ "invnext", CG_NextInventory_f },
	{ "invprev", CG_PrevInventory_f },
	{ "forcenext", CG_NextForcePower_f },
	{ "forceprev", CG_PrevForcePower_f },
	{ "loadhud", CG_LoadHud_f },
	{ "dpweapnext", CG_DPNextWeapon_f },
	{ "dpweapprev", CG_DPPrevWeapon_f },
	{ "dpinvnext", CG_DPNextInventory_f },
	{ "dpinvprev", CG_DPPrevInventory_f },
	{ "dpforcenext", CG_DPNextForcePower_f },
	{ "dpforceprev", CG_DPPrevForcePower_f },

	{ "jumpHelperColorBackground", CG_SetJumpHelperColorBackground_f },
	{ "jumpHelperColorCancel", CG_SetJumpHelperColorCancel_f },
	{ "jumpHelperColorCrouch", CG_SetJumpHelperColorCrouch_f },
	{ "jumpHelperColorCrouchExtend", CG_SetJumpHelperColorCrouchExtend_f },
	{ "jumpHelperColorCurrent", CG_SetJumpHelperColorCurrent_f },
	{ "jumpHelperColorExtend", CG_SetJumpHelperColorExtend_f },
	{ "jumpHelperColorOptimal", CG_SetJumpHelperColorOptimal_f },
	{ "speedColor", CG_SetSpeedColor_f },
	{ "strafeHelperColorAccelerating", CG_SetStrafeHelperColorAccelerating_f },
	{ "strafeHelperColorOptimal", CG_SetStrafeHelperColorOptimal_f },
	{ "strafeHelperColorCenterMarker", CG_SetStrafeHelperColorCenterMarker_f },
	{ "strafeHelperColorSpeed", CG_SetStrafeHelperColorSpeed_f },
};


//extern menuDef_t *menuScoreboard;
void Menu_Reset();	

void CG_LoadHud_f( void) 
{
	const char *hudSet;

//	cgi_UI_String_Init();

//	cgi_UI_Menu_Reset();
	
	hudSet = cg_hudFiles.string;
	if (hudSet[0] == '\0') 
	{
		hudSet = "ui/jk2hud.txt";
	}

	CG_LoadMenus(hudSet);
//	menuScoreboard = NULL;

}

/*
=================
CG_ConsoleCommand

The string has been tokenized and can be retrieved with
Cmd_Argc() / Cmd_Argv()
=================
*/
qboolean CG_ConsoleCommand( void ) {
	const char	*cmd;
	int		i;

	cmd = CG_Argv(0);

	for ( i = 0 ; i < sizeof( commands ) / sizeof( commands[0] ) ; i++ ) {
		if ( !Q_stricmp( cmd, commands[i].cmd ) ) {
			commands[i].function();
			return qtrue;
		}
	}

	return qfalse;
}


/*
=================
CG_InitConsoleCommands

Let the client system know about all of our commands
so it can perform tab completion
=================
*/
void CG_InitConsoleCommands( void ) {
	int		i;

	for ( i = 0 ; i < sizeof( commands ) / sizeof( commands[0] ) ; i++ ) {
		cgi_AddCommand( commands[i].cmd );
	}
}
