// cl_main.c  -- client main loop

// leave this as first line for PCH reasons...
//
#include "../server/exe_headers.h"


#include "client.h"
#include "client_ui.h"
#include <limits.h>
#ifdef _IMMERSION
#include "../ff/ff.h"
#include "../ff/cl_ff.h"
#else
#include "fffx.h"
#endif // _IMMERSION
#include "../ghoul2/g2.h"

#include "../speedrun/speedrun_timer_q3/timer.h"

#define	RETRANSMIT_TIMEOUT	3000	// time between connection packet retransmits

cvar_t	*cl_nodelta;
cvar_t	*cl_debugMove;

cvar_t	*cl_noprint;

cvar_t	*cl_timeout;
cvar_t	*cl_maxpackets;
cvar_t	*cl_packetdup;
cvar_t	*cl_timeNudge;
cvar_t	*cl_showTimeDelta;
cvar_t	*cl_newClock=0;

cvar_t	*cl_shownet;
cvar_t	*cl_avidemo;

cvar_t	*cl_pano;
cvar_t	*cl_panoNumShots;
cvar_t	*cl_skippingcin;
cvar_t	*cl_endcredits;

cvar_t	*cl_freelook;
cvar_t	*cl_sensitivity;

cvar_t	*cl_mouseAccel;
cvar_t	*cl_showMouseRate;
cvar_t  *cl_VideoQuality;
cvar_t	*cl_VidFadeUp;	// deliberately kept as "Vid" rather than "Video" so tab-matching matches only VideoQuality
cvar_t	*cl_VidFadeDown;
cvar_t	*cl_framerate;

cvar_t	*m_pitch;
cvar_t	*m_yaw;
cvar_t	*m_forward;
cvar_t	*m_side;
cvar_t	*m_filter;

cvar_t	*cl_activeAction;

cvar_t	*cl_updateInfoString;

cvar_t	*cl_ingameVideo;

clientActive_t		cl;
clientConnection_t	clc;
clientStatic_t		cls;

// Structure containing functions exported from refresh DLL
refexport_t	re;

ping_t	cl_pinglist[MAX_PINGREQUESTS];

void CL_ShutdownRef( void );
void CL_InitRef( void );
void CL_CheckForResend( void );

extern int*	 s_entityWavVol;
extern int*	 s_entityWavVol_back;

/*
=======================================================================

CLIENT RELIABLE COMMAND COMMUNICATION

=======================================================================
*/

/*
======================
CL_AddReliableCommand

The given command will be transmitted to the server, and is gauranteed to
not have future usercmd_t executed before it is executed
======================
*/
void CL_AddReliableCommand( const char *cmd ) {
	int		index;

	// if we would be losing an old command that hasn't been acknowledged,
	// we must drop the connection
	if ( clc.reliableSequence - clc.reliableAcknowledge > MAX_RELIABLE_COMMANDS ) {
		Com_Error( ERR_DROP, "Client command overflow" );
	}
	clc.reliableSequence++;
	index = clc.reliableSequence & ( MAX_RELIABLE_COMMANDS - 1 );
	if ( clc.reliableCommands[ index ] ) {
		Z_Free( clc.reliableCommands[ index ] );
	}
	clc.reliableCommands[ index ] = CopyString( cmd );
}

//======================================================================

/*
==================
CL_NextDemo

Called when a demo or cinematic finishes
If the "nextdemo" cvar is set, that command will be issued
==================
*/
void CL_NextDemo( void ) {
	char	v[MAX_STRING_CHARS];

	Q_strncpyz( v, Cvar_VariableString ("nextdemo"), sizeof(v) );
	v[MAX_STRING_CHARS-1] = 0;
	Com_DPrintf("CL_NextDemo: %s\n", v );
	if (!v[0]) {
		return;
	}

	Cvar_Set ("nextdemo","");
	Cbuf_AddText (v);
	Cbuf_AddText ("\n");
	Cbuf_Execute();
}

//======================================================================

/*
=================
CL_FlushMemory

Called by CL_MapLoading, CL_Connect_f, and CL_ParseGamestate the only
ways a client gets into a game
Also called by Com_Error
=================
*/
void CL_FlushMemory( void ) {

	// clear sounds (moved higher up within this func to avoid the odd sound stutter)
	S_DisableSounds();

	// unload the old VM
	CL_ShutdownCGame();

	CL_ShutdownUI();

	if ( re.Shutdown ) {
		re.Shutdown( qfalse );		// don't destroy window or context
	}

	cls.soundRegistered = qfalse;
	cls.rendererStarted = qfalse;
#ifdef _IMMERSION
	CL_ShutdownFF();
	cls.forceStarted = qfalse;
#endif // _IMMERSION
}

/*
=====================
CL_MapLoading

A local server is starting to load a map, so update the
screen to let the user know about it, then dump all client
memory on the hunk from cgame, ui, and renderer
=====================
*/
void CL_MapLoading( void ) {
	if ( !com_cl_running->integer ) {
		return;
	}

	Con_Close();
	cls.keyCatchers = 0;

	// if we are already connected to the local host, stay connected
	if ( cls.state >= CA_CONNECTED && !Q_stricmp( cls.servername, "localhost" ) )  {
		cls.state = CA_CONNECTED;		// so the connect screen is drawn
		memset( cls.updateInfoString, 0, sizeof( cls.updateInfoString ) );
//		memset( clc.serverMessage, 0, sizeof( clc.serverMessage ) );
		memset( &cl.gameState, 0, sizeof( cl.gameState ) );
		clc.lastPacketSentTime = -9999;
		SCR_UpdateScreen();
	} else {
		// clear nextmap so the cinematic shutdown doesn't execute it
		Cvar_Set( "nextmap", "" );
		CL_Disconnect();
		Q_strncpyz( cls.servername, "localhost", sizeof(cls.servername) );
		cls.state = CA_CHALLENGING;		// so the connect screen is drawn
		cls.keyCatchers = 0;
		SCR_UpdateScreen();
		clc.connectTime = -RETRANSMIT_TIMEOUT;
		NET_StringToAdr( cls.servername, &clc.serverAddress);
		// we don't need a challenge on the localhost

		CL_CheckForResend();
	}

	CL_FlushMemory();
}

/*
=====================
CL_ClearState

Called before parsing a gamestate
=====================
*/
void CL_ClearState (void) {
	CL_ShutdownCGame();

	S_StopAllSounds();

	memset( &cl, 0, sizeof( cl ) );
}


/*
=====================
CL_Disconnect

Called when a connection, or cinematic is being terminated.
Goes from a connected state to either a menu state or a console state
Sends a disconnect message to the server
This is also called on Com_Error and Com_Quit, so it shouldn't cause any errors
=====================
*/
void CL_Disconnect( void ) {
	int		i;

	if ( !com_cl_running || !com_cl_running->integer ) {
		return;
	}

	if (cls.uiStarted)
		UI_SetActiveMenu( NULL,NULL );

	SCR_StopCinematic ();
	S_ClearSoundBuffer();

	// send a disconnect message to the server
	// send it a few times in case one is dropped
	if ( cls.state >= CA_CONNECTED ) {
		CL_AddReliableCommand( "disconnect" );
		CL_WritePacket();
		CL_WritePacket();
		CL_WritePacket();
	}
	
	CL_ClearState ();

	// wipe the client connection
	for ( i = 0 ; i < MAX_RELIABLE_COMMANDS ; i++ ) {
		if ( clc.reliableCommands[i] ) {
			Z_Free( clc.reliableCommands[i] );
		}
	}
	memset( &clc, 0, sizeof( clc ) );

	cls.state = CA_DISCONNECTED;

	if ( cl_skippingcin->integer )
	{
		SpeedrunUnpauseTimer();
	}
	// allow cheats locally
	Cvar_Set( "timescale", "1" );//jic we were skipping
	Cvar_Set( "skippingCinematic", "0" );//jic we were skipping
}


/*
===================
CL_ForwardCommandToServer

adds the current command line as a clientCommand
things like godmode, noclip, etc, are commands directed to the server,
so when they are typed in at the console, they will need to be forwarded.
===================
*/
void CL_ForwardCommandToServer( void ) {
	char	*cmd;
	char	string[MAX_STRING_CHARS];

	cmd = Cmd_Argv(0);

	// ignore key up commands
	if ( cmd[0] == '-' ) {
		return;
	}

	if ( cls.state != CA_ACTIVE || cmd[0] == '+' ) {
		Com_Printf ("Unknown command \"%s\"\n", cmd);
		return;
	}

	if ( Cmd_Argc() > 1 ) {
		Com_sprintf( string, sizeof(string), "%s %s", cmd, Cmd_Args() );
	} else {
		Q_strncpyz( string, cmd, sizeof(string) );
	}

	CL_AddReliableCommand( string );
}


/*
======================================================================

CONSOLE COMMANDS

======================================================================
*/

/*
==================
CL_ForwardToServer_f
==================
*/
void CL_ForwardToServer_f( void ) {
	if ( cls.state != CA_ACTIVE ) {
		Com_Printf ("Not connected to a server.\n");
		return;
	}
	
	// don't forward the first argument
	if ( Cmd_Argc() > 1 ) {
		CL_AddReliableCommand( Cmd_Args() );
	}
}

/*
==================
CL_Setenv_f

Mostly for controlling voodoo environment variables
==================
*/
void CL_Setenv_f( void )
{
	int argc = Cmd_Argc();

	if ( argc > 2 )
	{
		char buffer[1024];
		int i;

		strcpy( buffer, Cmd_Argv(1) );
		strcat( buffer, "=" );

		for ( i = 2; i < argc; i++ )
		{
			strcat( buffer, Cmd_Argv( i ) );
			strcat( buffer, " " );
		}

		putenv( buffer );
	}
	else if ( argc == 2 )
	{
		char *env = getenv( Cmd_Argv(1) );

		if ( env )
		{
			Com_Printf( "%s=%s\n", Cmd_Argv(1), env );
		}
		else
		{
			Com_Printf( "%s undefined\n", Cmd_Argv(1), env );
		}
	}
}


/*
==================
CL_Disconnect_f
==================
*/
void CL_Disconnect_f( void ) {
	SCR_StopCinematic();	

	//FIXME:
	// TA codebase added additional CA_CINEMATIC check below, presumably so they could play cinematics
	//	in the menus when disconnected, although having the SCR_StopCinematic() call above is weird.
	// Either there's a bug, or the new version of that function conditionally-doesn't stop cinematics...
	//
	if ( cls.state != CA_DISCONNECTED && cls.state != CA_CINEMATIC ) {
		Com_Error (ERR_DISCONNECT, "Disconnected from server");
	}
}


/*
=================
CL_Vid_Restart_f

Restart the video subsystem
=================
*/
void CL_Vid_Restart_f( void ) {
	SpeedrunPauseTimer();

	S_StopAllSounds();		// don't let them loop during the restart
	S_BeginRegistration();	// all sound handles are now invalid
	CL_ShutdownRef();
	CL_ShutdownUI();
	CL_ShutdownCGame();
	CL_InitRef();

	cls.rendererStarted = qfalse;
	cls.uiStarted = qfalse;
	cls.cgameStarted = qfalse;
	cls.soundRegistered = qfalse;

#ifdef _IMMERSION
	CL_ShutdownFF();
	cls.forceStarted = qfalse;
#endif // _IMMERSION
	// unpause so the cgame definately gets a snapshot and renders a frame
	Cvar_Set( "cl_paused", "0" );
}

/*
=================
CL_Snd_Restart_f

Restart the sound subsystem
The cgame and game must also be forced to restart because
handles will be invalid
=================
*/
void CL_Snd_Restart_f( void ) {
	S_Shutdown();

	S_Init();

//	CL_Vid_Restart_f();

	extern qboolean	s_soundMuted;
	s_soundMuted = qfalse;		// we can play again

	S_RestartMusic();

	extern void S_ReloadAllUsedSounds(void);
	S_ReloadAllUsedSounds();

	extern void AS_ParseSets(void);
	AS_ParseSets();
}
#ifdef _IMMERSION
/*
=================
CL_FF_Restart_f
=================
*/
void CL_FF_Restart_f( void ) {

	if ( FF_IsInitialized() )
	{
		// Apply cvar changes w/o losing registered effects
		// Allows changing devices in-game without restarting the map
		if ( !FF_Init() )
			FF_Shutdown();	// error (shouldn't happen)
	}
	else if ( cls.state >= CA_PRIMED )	// maybe > CA_DISCONNECTED
	{
		// Restart map or menu
		CL_Vid_Restart_f();
	}
	else if ( cls.uiStarted )
	{
		// Restart menu
		CL_ShutdownUI();
		cls.forceStarted = qfalse;
	}
}
#endif // _IMMERSION
/*
==================
CL_Configstrings_f
==================
*/
void CL_Configstrings_f( void ) {
	int		i;
	int		ofs;

	if ( cls.state != CA_ACTIVE ) {
		Com_Printf( "Not connected to a server.\n");
		return;
	}

	for ( i = 0 ; i < MAX_CONFIGSTRINGS ; i++ ) {
		ofs = cl.gameState.stringOffsets[ i ];
		if ( !ofs ) {
			continue;
		}
		Com_Printf( "%4i: %s\n", i, cl.gameState.stringData + ofs );
	}
}

/*
==============
CL_Clientinfo_f
==============
*/
void CL_Clientinfo_f( void ) {
	Com_Printf( "--------- Client Information ---------\n" );
	Com_Printf( "state: %i\n", cls.state );
	Com_Printf( "Server: %s\n", cls.servername );
	Com_Printf ("User info settings:\n");
	Info_Print( Cvar_InfoString( CVAR_USERINFO ) );
	Com_Printf( "--------------------------------------\n" );
}


//====================================================================

void UI_UpdateConnectionString( char *string );

/*
=================
CL_CheckForResend

Resend a connect message if the last one has timed out
=================
*/
void CL_CheckForResend( void ) {
	int		port;
	char	info[MAX_INFO_STRING];

//	if ( cls.state == CA_CINEMATIC )  
	if ( cls.state == CA_CINEMATIC || CL_IsRunningInGameCinematic())
	{
		return;
	}

	// resend if we haven't gotten a reply yet
	if ( cls.state < CA_CONNECTING || cls.state > CA_CHALLENGING ) {
		return;
	}

	if ( cls.realtime - clc.connectTime < RETRANSMIT_TIMEOUT ) {
		return;
	}

	clc.connectTime = cls.realtime;	// for retransmit requests
	clc.connectPacketCount++;

	// requesting a challenge
	switch ( cls.state ) {
	case CA_CONNECTING:
		UI_UpdateConnectionString( va("(%i)", clc.connectPacketCount ) );

		NET_OutOfBandPrint(NS_CLIENT, clc.serverAddress, "getchallenge");
		break;

	case CA_CHALLENGING:
	// sending back the challenge
		port = Cvar_VariableIntegerValue("qport");

		UI_UpdateConnectionString( va("(%i)", clc.connectPacketCount ) );

		Q_strncpyz( info, Cvar_InfoString( CVAR_USERINFO ), sizeof( info ) );
		Info_SetValueForKey( info, "protocol", va("%i", PROTOCOL_VERSION ) );
		Info_SetValueForKey( info, "qport", va("%i", port ) );
		Info_SetValueForKey( info, "challenge", va("%i", clc.challenge ) );
		NET_OutOfBandPrint( NS_CLIENT, clc.serverAddress, "connect \"%s\"", info );
		// the most current userinfo has been sent, so watch for any
		// newer changes to userinfo variables
		cvar_modifiedFlags &= ~CVAR_USERINFO;
		break;

	default:
		Com_Error( ERR_FATAL, "CL_CheckForResend: bad cls.state" );
	}
}


/*
===================
CL_DisconnectPacket

Sometimes the server can drop the client and the netchan based
disconnect can be lost.  If the client continues to send packets
to the server, the server will send out of band disconnect packets
to the client so it doesn't have to wait for the full timeout period.
===================
*/
void CL_DisconnectPacket( netadr_t from ) {
	if ( cls.state != CA_ACTIVE ) {
		return;
	}

	// if not from our server, ignore it
	if ( !NET_CompareAdr( from, clc.netchan.remoteAddress ) ) {
		return;
	}

	// if we have received packets within three seconds, ignore it
	// (it might be a malicious spoof)
	if ( cls.realtime - clc.lastPacketTime < 3000 ) {
		return;
	}

	// drop the connection (FIXME: connection dropped dialog)
	Com_Printf( "Server disconnected for unknown reason\n" );
	CL_Disconnect();
}


/*
=================
CL_ConnectionlessPacket

Responses to broadcasts, etc
=================
*/
void CL_ConnectionlessPacket( netadr_t from, msg_t *msg ) {
	char	*s;
	char	*c;
	
	MSG_BeginReading( msg );
	MSG_ReadLong( msg );	// skip the -1

	s = MSG_ReadStringLine( msg );

	Cmd_TokenizeString( s );

	c = Cmd_Argv(0);

	Com_DPrintf ("CL packet %s: %s\n", NET_AdrToString(from), c);

	// challenge from the server we are connecting to
	if ( !strcmp(c, "challengeResponse") ) {
		if ( cls.state != CA_CONNECTING ) {
			Com_Printf( "Unwanted challenge response received.  Ignored.\n" );
		} else {
			// start sending challenge repsonse instead of challenge request packets
			clc.challenge = atoi(Cmd_Argv(1));
			cls.state = CA_CHALLENGING;
			clc.connectPacketCount = 0;
			clc.connectTime = -99999;

			// take this address as the new server address.  This allows
			// a server proxy to hand off connections to multiple servers
			clc.serverAddress = from;
		}
		return;
	}

	// server connection
	if ( !strcmp(c, "connectResponse") ) {
		if ( cls.state >= CA_CONNECTED ) {
			Com_Printf ("Dup connect received.  Ignored.\n");
			return;
		}
		if ( cls.state != CA_CHALLENGING ) {
			Com_Printf ("connectResponse packet while not connecting.  Ignored.\n");
			return;
		}
		if ( !NET_CompareBaseAdr( from, clc.serverAddress ) ) {
			Com_Printf( "connectResponse from a different address.  Ignored.\n" );
			Com_Printf( "%s should have been %s\n", NET_AdrToString( from ), 
				NET_AdrToString( clc.serverAddress ) );
			return;
		}
		Netchan_Setup (NS_CLIENT, &clc.netchan, from, Cvar_VariableIntegerValue( "qport" ) );
		cls.state = CA_CONNECTED;
		clc.lastPacketSentTime = -9999;		// send first packet immediately
		return;
	}

	// a disconnect message from the server, which will happen if the server
	// dropped the connection but it is still getting packets from us
	if (!strcmp(c, "disconnect")) {
		CL_DisconnectPacket( from );
		return;
	}

	// echo request from server
	if ( !strcmp(c, "echo") ) {
		NET_OutOfBandPrint( NS_CLIENT, from, "%s", Cmd_Argv(1) );
		return;
	}

	// print request from server
	if ( !strcmp(c, "print") ) {
		s = MSG_ReadString( msg );
		UI_UpdateConnectionMessageString( s );
		Com_Printf( "%s", s );
		return;
	}


	Com_DPrintf ("Unknown connectionless packet command.\n");
}


/*
=================
CL_PacketEvent

A packet has arrived from the main event loop
=================
*/
void CL_PacketEvent( netadr_t from, msg_t *msg ) {
	int		headerBytes;

	clc.lastPacketTime = cls.realtime;

	if ( msg->cursize >= 4 && *(int *)msg->data == -1 ) {
		CL_ConnectionlessPacket( from, msg );
		return;
	}

	if ( cls.state < CA_CONNECTED ) {
		return;		// can't be a valid sequenced packet
	}

	if ( msg->cursize < 8 ) {
		Com_Printf ("%s: Runt packet\n",NET_AdrToString( from ));
		return;
	}

	//
	// packet from server
	//
	if ( !NET_CompareAdr( from, clc.netchan.remoteAddress ) ) {
		Com_DPrintf ("%s:sequenced packet without connection\n"
			,NET_AdrToString( from ) );
		// FIXME: send a client disconnect?
		return;
	}

	if (!Netchan_Process( &clc.netchan, msg) ) {
		return;		// out of order, duplicated, etc
	}

	// the header is different lengths for reliable and unreliable messages
	headerBytes = msg->readcount;

	clc.lastPacketTime = cls.realtime;
	CL_ParseServerMessage( msg );
}

/*
==================
CL_CheckTimeout

==================
*/
void CL_CheckTimeout( void ) {
	//
	// check timeout
	//
	if ( ( !cl_paused->integer || !sv_paused->integer ) 
//		&& cls.state >= CA_CONNECTED && cls.state != CA_CINEMATIC
		&& cls.state >= CA_CONNECTED && (cls.state != CA_CINEMATIC && !CL_IsRunningInGameCinematic())
		&& cls.realtime - clc.lastPacketTime > cl_timeout->value*1000) {
		if (++cl.timeoutcount > 5) {	// timeoutcount saves debugger
			Com_Printf ("\nServer connection timed out.\n");
			CL_Disconnect ();
			return;
		}
	} else {
		cl.timeoutcount = 0;
	}
}


//============================================================================

/*
==================
CL_CheckUserinfo

==================
*/
void CL_CheckUserinfo( void ) {
	if ( cls.state < CA_CHALLENGING ) {
		return;
	}

	// send a reliable userinfo update if needed
	if ( cvar_modifiedFlags & CVAR_USERINFO ) {
		cvar_modifiedFlags &= ~CVAR_USERINFO;
		CL_AddReliableCommand( va("userinfo \"%s\"", Cvar_InfoString( CVAR_USERINFO ) ) );
	}

}


/*
==================
CL_Frame

==================
*/
extern cvar_t	*cl_newClock;
static unsigned int frameCount;
float avgFrametime=0.0;
void CL_Frame ( int msec,float fractionMsec ) {
	if ( !com_cl_running->integer ) {
		return;
	}

	// load the ref / ui / cgame if needed
	CL_StartHunkUsers();

	if ( cls.state == CA_DISCONNECTED && !( cls.keyCatchers & KEYCATCH_UI )
		&& !com_sv_running->integer ) {		
		// if disconnected, bring up the menu
		if (!CL_CheckPendingCinematic())	// this avoid having the menu flash for one frame before pending cinematics
		{
			UI_SetActiveMenu( "mainMenu",NULL );
		}
	}

	// if recording an avi, lock to a fixed fps
	if ( cl_avidemo->integer ) {
		// save the current screen
		if ( cls.state == CA_ACTIVE ) {
			if (cl_avidemo->integer > 0) {
				Cbuf_ExecuteText( EXEC_NOW, "screenshot silent\n" );
			} else {
				Cbuf_ExecuteText( EXEC_NOW, "screenshot_tga silent\n" );
			}
		}
		// fixed time for next frame
		if (cl_avidemo->integer > 0) {
			msec = 1000 / cl_avidemo->integer;
		} else {
			msec = 1000 / -cl_avidemo->integer;
		}
	}

	// save the msec before checking pause
	cls.realFrametime = msec;

	// decide the simulation time
	cls.frametime = msec;
	if(cl_framerate->integer)
	{
		avgFrametime+=msec;
		char mess[256];
		if(!(frameCount&0x1f))
		{
			sprintf(mess,"Frame rate=%f\n\n",1000.0f*(1.0/(avgFrametime/32.0f)));
	//		OutputDebugString(mess);
			Com_Printf(mess);
			avgFrametime=0.0f;
		}
		frameCount++;
	}
	cls.frametimeFraction=fractionMsec;
	cls.realtime += msec;
	cls.realtimeFraction+=fractionMsec;
	if (cls.realtimeFraction>=1.0f)
	{
		if (cl_newClock&&cl_newClock->integer)
		{
			cls.realtime++;
		}
		cls.realtimeFraction-=1.0f;
	}
	if ( cl_timegraph->integer ) {
		SCR_DebugGraph ( cls.realFrametime * 0.25, 0 );
	}

	// see if we need to update any userinfo
	CL_CheckUserinfo();

	// if we haven't gotten a packet in a long time,
	// drop the connection
	CL_CheckTimeout();

	// send intentions now
	CL_SendCmd();

	// resend a connection request if necessary
	CL_CheckForResend();

	// decide on the serverTime to render
	CL_SetCGameTime();

	if (cl_pano->integer && cls.state == CA_ACTIVE) {	//grab some panoramic shots
		int i = 1;
		int pref = cl_pano->integer;
		int oldnoprint = cl_noprint->integer;
		Con_Close();
		cl_noprint->integer = 1;	//hide the screen shot msgs
		for (; i <= cl_panoNumShots->integer; i++) {
			Cvar_SetValue( "pano", i );
			SCR_UpdateScreen();// update the screen
			Cbuf_ExecuteText( EXEC_NOW, va("screenshot %dpano%02d\n", pref, i) );	//grab this screen
		}
		Cvar_SetValue( "pano", 0 );	//done
		cl_noprint->integer = oldnoprint;
	}

	if (cl_skippingcin->integer && !cl_endcredits->integer) {
		if (cl_skippingcin->modified){
			S_StopSounds();		//kill em all but music	
			cl_skippingcin->modified=qfalse;
			Com_Printf (S_COLOR_YELLOW "....");
			SCR_UpdateScreen();
		}
	} else {
		// update the screen
		SCR_UpdateScreen();
	}
	// update audio
	S_Update();

#ifdef _IMMERSION
	FF_Update();
#endif // _IMMERSION
	// advance local effects for next frame
	SCR_RunCinematic();

	Con_RunConsole();

	cls.framecount++;
}


//============================================================================

/*
================
VID_Printf

DLL glue
================
*/
#define	MAXPRINTMSG	4096
void VID_Printf (int print_level, const char *fmt, ...)
{
	va_list		argptr;
	char		msg[MAXPRINTMSG];
	
	va_start (argptr,fmt);
	vsprintf (msg,fmt,argptr);
	va_end (argptr);

	if ( print_level == PRINT_ALL ) {
		Com_Printf ("%s", msg);
	} else if ( print_level == PRINT_WARNING ) {
		Com_Printf (S_COLOR_YELLOW "%s", msg);		// yellow
	} else if ( print_level == PRINT_DEVELOPER ) {
		Com_DPrintf (S_COLOR_RED"%s", msg);
	}
}



/*
============
CL_ShutdownRef
============
*/
void CL_ShutdownRef( void ) {
	if ( !re.Shutdown ) {
		return;
	}
	re.Shutdown( qtrue );
	memset( &re, 0, sizeof( re ) );
}

/*
============================
CL_StartHunkUsers

After the server has cleared the hunk, these will need to be restarted
This is the only place that any of these functions are called from
============================
*/
void CL_StartHunkUsers( void ) {
	if ( !com_cl_running->integer ) {
		return;
	}

	if ( !cls.rendererStarted ) {
		cls.rendererStarted = qtrue;
		re.BeginRegistration( &cls.glconfig );

		// load character sets
//		cls.charSetShader = re.RegisterShaderNoMip( "gfx/2d/bigchars" );
		cls.charSetShader = re.RegisterShaderNoMip( "gfx/2d/charsgrid_med" );
		cls.whiteShader = re.RegisterShader( "white" );
		cls.consoleShader = re.RegisterShader( "console" );
		g_console_field_width = cls.glconfig.vidWidth / SMALLCHAR_WIDTH - 2;
		kg.g_consoleField.widthInChars = g_console_field_width;
#ifndef _IMMERSION
		//-------
		//	The latest Immersion Force Feedback system initializes here, not through
		//	win32 input system. Therefore, the window handle is valid :)
		//-------

		// now that the renderer has started up we know that the global hWnd is now valid,
		//	so we can now go ahead and (re)setup the input stuff that needs hWnds for DI...
		//  (especially Force feedback)...
		//
		static qboolean bOnceOnly = qfalse;	// only do once, not every renderer re-start
		if (!bOnceOnly)
		{
			bOnceOnly = qtrue;
			extern void Sys_In_Restart_f( void );
			Sys_In_Restart_f();
		}
#endif // _IMMERSION
	}

	if ( !cls.soundStarted ) {
		cls.soundStarted = qtrue;
		S_Init();
	}

	if ( !cls.soundRegistered ) {
		cls.soundRegistered = qtrue;
		S_BeginRegistration();
	}

#ifdef _IMMERSION
	if ( !cls.forceStarted ) {
		cls.forceStarted = qtrue;
		CL_InitFF();
	}
#endif // _IMMERSION
	if ( !cls.uiStarted ) {
		cls.uiStarted = qtrue;
		CL_InitUI();
	}

//	if ( !cls.cgameStarted && cls.state > CA_CONNECTED && cls.state != CA_CINEMATIC ) {
	if ( !cls.cgameStarted && cls.state > CA_CONNECTED && (cls.state != CA_CINEMATIC && !CL_IsRunningInGameCinematic()) ) 
	{
		cls.cgameStarted = qtrue;
		CL_InitCGame();
	}
}

// this is a compile-helper function since Z_Malloc can now become a macro with __LINE__ etc
//
static void *CL_ZMalloc_Helper( int iSize, memtag_t eTag, qboolean bZeroit)
{
	return Z_Malloc( iSize, eTag, bZeroit );
}

/*
============
CL_InitRef
============
*/
void CL_InitRef( void ) {
	refimport_t	ri;
	refexport_t	*ret;

	Com_Printf( "----- Initializing Renderer ----\n" );

	ri.Cmd_AddCommand = Cmd_AddCommand;
	ri.Cmd_RemoveCommand = Cmd_RemoveCommand;
	ri.Cmd_Argc = Cmd_Argc;
	ri.Cmd_Argv = Cmd_Argv;
	ri.Cmd_ExecuteText = Cbuf_ExecuteText;
	ri.Printf = VID_Printf;
	ri.Error = Com_Error;
	ri.Milliseconds = Sys_Milliseconds;
	ri.flrand = Q_flrand;                      
	ri.Malloc = CL_ZMalloc_Helper;
	ri.Free = Z_Free;
	ri.Hunk_Clear = Hunk_ClearToMark;
	ri.Hunk_Alloc = Hunk_Alloc;
	ri.CM_DrawDebugSurface = CM_DrawDebugSurface;
	ri.FS_ReadFile = FS_ReadFile;
	ri.FS_FreeFile = FS_FreeFile;
	ri.FS_WriteFile = FS_WriteFile;
 	ri.FS_FreeFileList = FS_FreeFileList;
	ri.FS_ListFiles = FS_ListFiles;
	ri.FS_FileIsInPAK = FS_FileIsInPAK;
	ri.Cvar_Get = Cvar_Get;
	ri.Cvar_Set = Cvar_Set;
	ri.CM_PointContents = CM_PointContents;
	ri.ArgsBuffer = Cmd_ArgsBuffer;                    

	// cinematic stuff

	ri.CIN_UploadCinematic = CIN_UploadCinematic;
	ri.CIN_PlayCinematic = CIN_PlayCinematic;
	ri.CIN_RunCinematic = CIN_RunCinematic;


	ret = GetRefAPI( REF_API_VERSION, &ri );

	Com_Printf( "-------------------------------\n");

	if ( !ret ) {
		Com_Error (ERR_FATAL, "Couldn't initialize refresh" );
	}

	re = *ret;

	// unpause so the cgame definately gets a snapshot and renders a frame
	Cvar_Set( "cl_paused", "0" );
}


//===========================================================================================


/*
====================
CL_Init
====================
*/
void CL_Init( void ) {
	Com_Printf( "----- Client Initialization -----\n" );

	SP_Register("con_text", SP_REGISTER_REQUIRED);	//reference is CON_TEXT
	SP_Register("keynames", SP_REGISTER_REQUIRED);	// reference is KEYNAMES
	
	Con_Init ();

	CL_ClearState ();

	cls.state = CA_DISCONNECTED;	// no longer CA_UNINITIALIZED
	cls.keyCatchers = KEYCATCH_CONSOLE;
	cls.realtime = 0;
	cls.realtimeFraction=0.0f;	// fraction of a msec accumulated

	CL_InitInput ();

	//
	// register our variables
	//
	cl_noprint = Cvar_Get( "cl_noprint", "0", 0 );

	cl_timeout = Cvar_Get ("cl_timeout", "125", 0);

	cl_timeNudge = Cvar_Get ("cl_timeNudge", "0", CVAR_TEMP );
	cl_shownet = Cvar_Get ("cl_shownet", "0", CVAR_TEMP );
	cl_showTimeDelta = Cvar_Get ("cl_showTimeDelta", "0", CVAR_TEMP );
	cl_newClock = Cvar_Get ("cl_newClock", "1", 0);
	cl_activeAction = Cvar_Get( "activeAction", "", CVAR_TEMP );
	
	cl_avidemo = Cvar_Get ("cl_avidemo", "0", 0);
	cl_pano = Cvar_Get ("pano", "0", 0);
	cl_panoNumShots= Cvar_Get ("panoNumShots", "10", CVAR_ARCHIVE);
	cl_skippingcin = Cvar_Get ("skippingCinematic", "0", CVAR_ROM);
	cl_endcredits = Cvar_Get ("cg_endcredits", "0", 0);

	cl_yawspeed = Cvar_Get ("cl_yawspeed", "140", CVAR_ARCHIVE);
	cl_pitchspeed = Cvar_Get ("cl_pitchspeed", "140", CVAR_ARCHIVE);
	cl_anglespeedkey = Cvar_Get ("cl_anglespeedkey", "1.5", CVAR_ARCHIVE);

	cl_maxpackets = Cvar_Get ("cl_maxpackets", "30", CVAR_ARCHIVE );
	cl_packetdup = Cvar_Get ("cl_packetdup", "1", CVAR_ARCHIVE );

	cl_run = Cvar_Get ("cl_run", "1", CVAR_ARCHIVE);
	cl_sensitivity = Cvar_Get ("sensitivity", "5", CVAR_ARCHIVE);
	cl_mouseAccel = Cvar_Get ("cl_mouseAccel", "0", CVAR_ARCHIVE);
	cl_freelook = Cvar_Get( "cl_freelook", "1", CVAR_ARCHIVE );

	cl_showMouseRate = Cvar_Get ("cl_showmouserate", "0", 0);

	cl_ingameVideo = Cvar_Get ("cl_ingameVideo", "1", CVAR_ARCHIVE);
	cl_VideoQuality = Cvar_Get ("cl_VideoQuality", "0", CVAR_ARCHIVE);
	cl_VidFadeUp	= Cvar_Get ("cl_VidFadeUp", "1", CVAR_TEMP);
	cl_VidFadeDown	= Cvar_Get ("cl_VidFadeDown", "1", CVAR_TEMP);
	cl_framerate	= Cvar_Get ("cl_framerate", "0", CVAR_TEMP);

	// init autoswitch so the ui will have it correctly even
	// if the cgame hasn't been started
	Cvar_Get ("cg_autoswitch", "1", CVAR_ARCHIVE);

	m_pitch = Cvar_Get ("m_pitch", "0.022", CVAR_ARCHIVE);
	m_yaw = Cvar_Get ("m_yaw", "0.022", CVAR_ARCHIVE);
	m_forward = Cvar_Get ("m_forward", "0.25", CVAR_ARCHIVE);
	m_side = Cvar_Get ("m_side", "0.25", CVAR_ARCHIVE);
	m_filter = Cvar_Get ("m_filter", "0", CVAR_ARCHIVE);

	cl_updateInfoString = Cvar_Get( "cl_updateInfoString", "", CVAR_ROM );

	// userinfo
	Cvar_Get ("name", "Kyle", CVAR_USERINFO | CVAR_ARCHIVE );
	Cvar_Get ("snaps", "20", CVAR_USERINFO | CVAR_ARCHIVE );
	
	Cvar_Get ("sex", "male", CVAR_USERINFO | CVAR_ARCHIVE );
	Cvar_Get ("handicap", "100", CVAR_USERINFO | CVAR_ARCHIVE );

	//
	// register our commands
	//
	Cmd_AddCommand ("cmd", CL_ForwardToServer_f);
	Cmd_AddCommand ("configstrings", CL_Configstrings_f);
	Cmd_AddCommand ("clientinfo", CL_Clientinfo_f);
	Cmd_AddCommand ("snd_restart", CL_Snd_Restart_f);
	Cmd_AddCommand ("vid_restart", CL_Vid_Restart_f);
	Cmd_AddCommand ("disconnect", CL_Disconnect_f);
	Cmd_AddCommand ("cinematic", CL_PlayCinematic_f);
	Cmd_AddCommand ("ingamecinematic", CL_PlayInGameCinematic_f);
	Cmd_AddCommand ("setenv", CL_Setenv_f );
	Cmd_AddCommand ("uimenu", CL_GenericMenu_f);
	Cmd_AddCommand ("datapad", CL_DataPad_f);
	Cmd_AddCommand ("endscreendissolve", CL_EndScreenDissolve_f);
#ifdef _IMMERSION
	Cmd_AddCommand ("ff_restart", CL_FF_Restart_f);
#endif // _IMMERSION

	CL_InitRef();

	CL_StartHunkUsers();

	SCR_Init ();

	Cbuf_Execute ();
	
	Cvar_Set( "cl_running", "1" );

	Com_Printf( "----- Client Initialization Complete -----\n" );
}


/*
===============
CL_Shutdown

===============
*/
void CL_Shutdown( void ) {
	static qboolean recursive = qfalse;
	
	if ( !com_cl_running || !com_cl_running->integer ) {
		return;
	}

	Com_Printf( "----- CL_Shutdown -----\n" );

	if ( recursive ) {
		printf ("recursive shutdown\n");
		return;
	}
	recursive = qtrue;

	CL_ShutdownUI();
	CL_Disconnect();

	S_Shutdown();
	CL_ShutdownRef();

#ifdef _IMMERSION
	CL_ShutdownFF();
#endif // _IMMERSION
	Cmd_RemoveCommand ("cmd");
	Cmd_RemoveCommand ("configstrings");
	Cmd_RemoveCommand ("clientinfo");
	Cmd_RemoveCommand ("snd_restart");
	Cmd_RemoveCommand ("vid_restart");
	Cmd_RemoveCommand ("disconnect");
	Cmd_RemoveCommand ("cinematic");	
	Cmd_RemoveCommand ("ingamecinematic");
	Cmd_RemoveCommand ("setenv");
	Cmd_RemoveCommand ("pause");

	Cvar_Set( "cl_running", "0" );

	recursive = qfalse;

	memset( &cls, 0, sizeof( cls ) );

	Com_Printf( "-----------------------\n" );
}


/*
==================
CL_GetPing
==================
*/
void CL_GetPing( int n, char *adrstr, int *pingtime )
{
	const char*	str;
	int		time;

	if (!cl_pinglist[n].adr.port)
	{
		// empty slot
		adrstr[0] = '\0';
		*pingtime = 0;
		return;
	}

	str = NET_AdrToString( cl_pinglist[n].adr );
	strcpy( adrstr, str );

	time = cl_pinglist[n].time;
	if (!time)
	{
		// check for timeout
		time = cls.realtime - cl_pinglist[n].start;
		if (time < 500)
		{
			// not timed out yet
			time = 0;
		}
	}

	*pingtime = time;
}

/*
==================
CL_ClearPing
==================
*/
void CL_ClearPing( int n )
{
	if (n < 0 || n >= MAX_PINGREQUESTS)
		return;

	cl_pinglist[n].adr.port = 0;
}

/*
==================
CL_GetPingQueueCount
==================
*/
int CL_GetPingQueueCount( void )
{
	int		i;
	int		count;
	ping_t*	pingptr;

	count   = 0;
	pingptr = cl_pinglist;
	for (i=0; i<MAX_PINGREQUESTS; i++, pingptr++ )
		if (pingptr->adr.port)
			count++;

	return (count);
}

/*
==================
CL_GetFreePing
==================
*/
ping_t* CL_GetFreePing( void )
{
	ping_t*	pingptr;
	ping_t*	best;	
	int		oldest;
	int		i;
	int		time;

	pingptr = cl_pinglist;
	for (i=0; i<MAX_PINGREQUESTS; i++, pingptr++ )
	{
		// find free ping slot
		if (pingptr->adr.port)
		{
			if (!pingptr->time)
			{
				if (cls.realtime - pingptr->start < 500)
				{
					// still waiting for response
					continue;
				}
			}
			else if (pingptr->time < 500)
			{
				// results have not been queried
				continue;
			}
		}

		// clear it
		pingptr->adr.port = 0;
		return (pingptr);
	}

	// use oldest entry
	pingptr = cl_pinglist;
	best    = cl_pinglist;
	oldest  = INT_MIN;
	for (i=0; i<MAX_PINGREQUESTS; i++, pingptr++ )
	{
		// scan for oldest
		time = cls.realtime - pingptr->start;
		if (time > oldest)
		{
			oldest = time;
			best   = pingptr;
		}
	}

	return (best);
}
