// leave this as first line for PCH reasons...
//
#include "../server/exe_headers.h"



#include "server.h"
/*
Ghoul2 Insert Start
*/
#if !defined (MINIHEAP_H_INC)
	#include "../qcommon/miniheap.h"
#endif
/*
Ghoul2 Insert End
*/

serverStatic_t	svs;				// persistant server info
server_t		sv;					// local server
game_export_t	*ge;

cvar_t	*sv_fps;				// time rate for running non-clients
cvar_t	*sv_timeout;			// seconds without any message
cvar_t	*sv_zombietime;			// seconds to sink messages after disconnect
cvar_t	*sv_reconnectlimit;		// minimum seconds between connect messages
cvar_t	*sv_showloss;			// report when usercmds are lost
cvar_t	*sv_killserver;			// menu system can set to 1 to shut server down
cvar_t	*sv_mapname;
cvar_t	*sv_spawntarget;
cvar_t	*sv_mapChecksum;
cvar_t	*sv_serverid;
cvar_t	*sv_testsave;			// Run the savegame enumeration every game frame
cvar_t	*sv_compress_saved_games;	// compress the saved games on the way out (only affect saver, loader can read both)

// Additions for Speed Outcast
cvar_t	*sv_speedrunModeIL;
cvar_t	*sv_speedrunModeCheckpoint;
cvar_t	*sv_speedrunModeCheckpointSave;
cvar_t	*sv_timedCheckpointMinX;
cvar_t	*sv_timedCheckpointMinY;
cvar_t	*sv_timedCheckpointMinZ;
cvar_t	*sv_timedCheckpointMaxX;
cvar_t	*sv_timedCheckpointMaxY;
cvar_t	*sv_timedCheckpointMaxZ;

/*
=============================================================================

EVENT MESSAGES

=============================================================================
*/

/*
===============
SV_ExpandNewlines

Converts newlines to "\n" so a line prints nicer
===============
*/
char	*SV_ExpandNewlines( char *in ) {
	static	char	string[1024];
	int		l;

	l = 0;
	while ( *in && l < sizeof(string) - 3 ) {
		if ( *in == '\n' ) {
			string[l++] = '\\';
			string[l++] = 'n';
		} else {
			string[l++] = *in;
		}
		in++;
	}
	string[l] = 0;

	return string;
}

/*
======================
SV_AddServerCommand

The given command will be transmitted to the client, and is guaranteed to
not have future snapshot_t executed before it is executed
======================
*/
void SV_AddServerCommand( client_t *client, const char *cmd ) {
	int		index;

	// if we would be losing an old command that hasn't been acknowledged,
	// we must drop the connection
	if ( client->reliableSequence - client->reliableAcknowledge > MAX_RELIABLE_COMMANDS ) {
		SV_DropClient( client, "Server command overflow" );
		return;
	}
	client->reliableSequence++;
	index = client->reliableSequence & ( MAX_RELIABLE_COMMANDS - 1 );
	if ( client->reliableCommands[ index ] ) {
		Z_Free( client->reliableCommands[ index ] );
	}
	client->reliableCommands[ index ] = CopyString( cmd );
}


/*
=================
SV_SendServerCommand

Sends a reliable command string to be interpreted by 
the client game module: "cp", "print", "chat", etc
A NULL client will broadcast to all clients
=================
*/
void SV_SendServerCommand(client_t *cl, const char *fmt, ...) {
	va_list		argptr;
	byte		message[MAX_MSGLEN];
	int			len;
	client_t	*client;
	int			j;
	
	message[0] = svc_serverCommand;

	va_start (argptr,fmt);
	vsprintf ((char *)message+1, fmt,argptr);
	va_end (argptr);
	len = strlen( (char *)message ) + 1;

	if ( cl != NULL ) {
		SV_AddServerCommand( cl, (char *)message );
		return;
	}

	// send the data to all relevent clients
	for (j = 0, client = svs.clients; j < 1 ; j++, client++) {
		if ( client->state < CS_PRIMED ) {
			continue;
		}
		SV_AddServerCommand( client, (char *)message );
	}
}



/*
==============================================================================

CONNECTIONLESS COMMANDS

==============================================================================
*/

/*
================
SVC_Status

Responds with all the info that qplug or qspy can see about the server
and all connected players.  Used for getting detailed information after
the simple info query.
================
*/
void SVC_Status( netadr_t from ) {
	char	player[1024];
	char	status[MAX_MSGLEN];
	int		i;
	client_t	*cl;
	int		statusLength;
	int		playerLength;
	int		score;
	char	infostring[MAX_INFO_STRING];

	strcpy( infostring, Cvar_InfoString( CVAR_SERVERINFO ) );

	// echo back the parameter to status. so servers can use it as a challenge
	// to prevent timed spoofed reply packets that add ghost servers
	Info_SetValueForKey( infostring, "challenge", Cmd_Argv(1) );

	status[0] = 0;
	statusLength = 0;

	for (i=0 ; i < 1 ; i++) {
		cl = &svs.clients[i];
		if ( cl->state >= CS_CONNECTED ) {
			if ( cl->gentity && cl->gentity->client ) {
				score = cl->gentity->client->persistant[PERS_SCORE];
			} else {
				score = 0;
			}
			Com_sprintf (player, sizeof(player), "%i %i \"%s\"\n", 
				score, cl->ping, cl->name);
			playerLength = strlen(player);
			if (statusLength + playerLength >= sizeof(status) ) {
				break;		// can't hold any more
			}
			strcpy (status + statusLength, player);
			statusLength += playerLength;
		}
	}

	NET_OutOfBandPrint( NS_SERVER, from, "statusResponse\n%s\n%s", infostring, status );
}

/*
================
SVC_Info

Responds with a short info message that should be enough to determine
if a user is interested in a server to do a full status
================
*/
static void SVC_Info( netadr_t from ) {
	int		i, count;
	char	infostring[MAX_INFO_STRING];

	count = 0;
	for ( i = 0 ; i < 1 ; i++ ) {
		if ( svs.clients[i].state >= CS_CONNECTED ) {
			count++;
		}
	}

	infostring[0] = 0;

	// echo back the parameter to status. so servers can use it as a challenge
	// to prevent timed spoofed reply packets that add ghost servers
	Info_SetValueForKey( infostring, "challenge", Cmd_Argv(1) );

	Info_SetValueForKey( infostring, "protocol", va("%i", PROTOCOL_VERSION) );
	//Info_SetValueForKey( infostring, "hostname", sv_hostname->string );
	Info_SetValueForKey( infostring, "mapname", sv_mapname->string );
	Info_SetValueForKey( infostring, "clients", va("%i", count) );
	Info_SetValueForKey( infostring, "sv_maxclients", va("%i", 1) );

	NET_OutOfBandPrint( NS_SERVER, from, "infoResponse\n%s", infostring );
}


/*
=================
SV_ConnectionlessPacket

A connectionless packet has four leading 0xff
characters to distinguish it from a game channel.
Clients that are in the game can still send
connectionless packets.
=================
*/
static void SV_ConnectionlessPacket( netadr_t from, msg_t *msg ) {
	char	*s;
	char	*c;

	MSG_BeginReading( msg );
	MSG_ReadLong( msg );		// skip the -1 marker

	s = MSG_ReadStringLine( msg );

	Cmd_TokenizeString( s );

	c = Cmd_Argv(0);
	Com_DPrintf ("SV packet %s : %s\n", NET_AdrToString(from), c);

	if (!strcmp(c,"getstatus")) {
		SVC_Status( from  );
	} else if (!strcmp(c,"getinfo")) {
		SVC_Info( from );
	} else if (!strcmp(c,"connect")) {
		SV_DirectConnect( from );
	} else if (!strcmp(c,"disconnect")) {
		// if a client starts up a local server, we may see some spurious
		// server disconnect messages when their new server sees our final
		// sequenced messages to the old client
	} else {
		Com_DPrintf ("bad connectionless packet from %s:\n%s\n"
		, NET_AdrToString (from), s);
	}
}


//============================================================================

/*
=================
SV_ReadPackets
=================
*/
void SV_PacketEvent( netadr_t from, msg_t *msg ) {
	int			i;
	client_t	*cl;
	int			qport;

	// check for connectionless packet (0xffffffff) first
	if ( msg->cursize >= 4 && *(int *)msg->data == -1) {
		SV_ConnectionlessPacket( from, msg );
		return;
	}

	// read the qport out of the message so we can fix up
	// stupid address translating routers
	MSG_BeginReading( msg );
	MSG_ReadLong( msg );				// sequence number
	MSG_ReadLong( msg );				// sequence number
	qport = MSG_ReadShort( msg ) & 0xffff;

	// find which client the message is from
	for (i=0, cl=svs.clients ; i < 1 ; i++,cl++) {
		if (cl->state == CS_FREE) {
			continue;
		}
		if ( !NET_CompareBaseAdr( from, cl->netchan.remoteAddress ) ) {
			continue;
		}
		// it is possible to have multiple clients from a single IP
		// address, so they are differentiated by the qport variable
		if (cl->netchan.qport != qport) {
			continue;
		}

		// the IP port can't be used to differentiate them, because
		// some address translating routers periodically change UDP
		// port assignments
		if (cl->netchan.remoteAddress.port != from.port) {
			Com_Printf( "SV_ReadPackets: fixing up a translated port\n" );
			cl->netchan.remoteAddress.port = from.port;
		}

		// make sure it is a valid, in sequence packet
		if (Netchan_Process(&cl->netchan, msg)) {
			// zombie clients stil neet to do the Netchan_Process
			// to make sure they don't need to retransmit the final
			// reliable message, but they don't do any other processing
			if (cl->state != CS_ZOMBIE) {
				cl->lastPacketTime = sv.time;	// don't timeout
				cl->frames[ cl->netchan.incomingAcknowledged & PACKET_MASK ]
					.messageAcked = sv.time;
				SV_ExecuteClientMessage( cl, msg );
			}
		}
		return;
	}
	
	// if we received a sequenced packet from an address we don't reckognize,
	// send an out of band disconnect packet to it
	NET_OutOfBandPrint( NS_SERVER, from, "disconnect" );
}


/*
===================
SV_CalcPings

Updates the cl->ping variables
===================
*/
void SV_CalcPings (void) {
	int			i, j;
	client_t	*cl;
	int			total, count;
	int			delta;

	for (i=0 ; i < 1 ; i++) {
		cl = &svs.clients[i];
		if ( cl->state != CS_ACTIVE ) {
			continue;
		}
		if ( cl->gentity->svFlags & SVF_BOT ) {
			continue;
		}

		total = 0;
		count = 0;
		for ( j = 0 ; j < PACKET_BACKUP ; j++ ) {
			delta = cl->frames[j].messageAcked - cl->frames[j].messageSent;
			if ( delta >= 0 ) {
				count++;
				total += delta;
			}
		}
		if (!count) {
			cl->ping = 999;
		} else {
			cl->ping = total/count;
			if ( cl->ping > 999 ) {
				cl->ping = 999;
			}
		}

		// let the game dll know about the ping
		cl->gentity->client->ping = cl->ping;
	}
}

/*
==================
SV_CheckTimeouts

If a packet has not been received from a client for timeout->integer 
seconds, drop the conneciton.  Server time is used instead of
realtime to avoid dropping the local client while debugging.

When a client is normally dropped, the client_t goes into a zombie state
for a few seconds to make sure any final reliable message gets resent
if necessary
==================
*/
void SV_CheckTimeouts( void ) {
	int		i;
	client_t	*cl;
	int			droppoint;
	int			zombiepoint;

	droppoint = sv.time - 1000 * sv_timeout->integer;
	zombiepoint = sv.time - 1000 * sv_zombietime->integer;

	for (i=0,cl=svs.clients ; i < 1 ; i++,cl++) {
		// message times may be wrong across a changelevel
		if (cl->lastPacketTime > sv.time) {
			cl->lastPacketTime = sv.time;
		}

		if (cl->state == CS_ZOMBIE
		&& cl->lastPacketTime < zombiepoint) {
			cl->state = CS_FREE;	// can now be reused
			continue;
		}
		if ( cl->state >= CS_CONNECTED && cl->lastPacketTime < droppoint) {
			// wait several frames so a debugger session doesn't
			// cause a timeout
			if ( ++cl->timeoutCount > 5 ) {
				SV_DropClient (cl, "timed out"); 
				cl->state = CS_FREE;	// don't bother with zombie state
			}
		} else {
			cl->timeoutCount = 0;
		}
	}
}


/*
==================
SV_CheckPaused
==================
*/
qboolean SV_CheckPaused( void ) {
	if ( !cl_paused->integer ) {
		return qfalse;
	}

	sv_paused->integer = 1;
	return qtrue;
}

/*
==================
SV_Frame

Player movement occurs as a result of packet events, which
happen before SV_Frame is called
==================
*/
extern cvar_t	*cl_newClock;
void SV_Frame( int msec,float fractionMsec ) {
	int		frameMsec;
	int		startTime=0;

	// the menu kills the server with this cvar
	if ( sv_killserver->integer ) {
		SV_Shutdown ("Server was killed.\n");
		Cvar_Set( "sv_killserver", "0" );
		return;
	}

	if ( !com_sv_running->integer ) {
		return;
	}

	extern void SP_CheckForLanguageUpdates(void);
	SP_CheckForLanguageUpdates();	// will fast-return else load different language if menu changed it

	// allow pause if only the local client is connected
	if ( SV_CheckPaused() ) {
		return;
	}

	// go ahead and let time slip if the server really hitched badly
	if ( msec > 1000 ) {
		Com_DPrintf( "SV_Frame: Truncating msec of %i to 1000\n", msec );
		msec = 1000;
	}

	// if it isn't time for the next frame, do nothing
	if ( sv_fps->integer < 1 ) {
		Cvar_Set( "sv_fps", "10" );
	}
	frameMsec = 1000 / sv_fps->integer ;

	sv.timeResidual += msec;
	sv.timeResidualFraction+=fractionMsec;
	if (sv.timeResidualFraction>=1.0f)
	{
		sv.timeResidualFraction-=1.0f;
		if (cl_newClock&&cl_newClock->integer)
		{
			sv.timeResidual++;
		}
	}
	if ( sv.timeResidual < frameMsec ) {
		return;
	}

	// if time is about to hit the 32nd bit, restart the
	// level, which will force the time back to zero, rather
	// than checking for negative time wraparound everywhere.
	// 2giga-milliseconds = 23 days, so it won't be too often
	if ( sv.time > 0x70000000 ) {
		SV_Shutdown( "Restarting server due to time wrapping" );
		Com_Printf("You win.  if you can play this long and not die, you deserve to win.\n");
		return;
	}

	// update infostrings if anything has been changed
	if ( cvar_modifiedFlags & CVAR_SERVERINFO ) {
		SV_SetConfigstring( CS_SERVERINFO, Cvar_InfoString( CVAR_SERVERINFO ) );
		cvar_modifiedFlags &= ~CVAR_SERVERINFO;
	}
	if ( cvar_modifiedFlags & CVAR_SYSTEMINFO ) {
		SV_SetConfigstring( CS_SYSTEMINFO, Cvar_InfoString( CVAR_SYSTEMINFO ) );
		cvar_modifiedFlags &= ~CVAR_SYSTEMINFO;
	}

	if ( com_speeds->integer ) {
		startTime = Sys_Milliseconds ();
	}

//	SV_BotFrame( sv.time );

	// run the game simulation in chunks
	while ( sv.timeResidual >= frameMsec ) {
		sv.timeResidual -= frameMsec;
		sv.time += frameMsec;
		G2API_SetTime(sv.time,G2T_SV_TIME);

		// let everything in the world think and move
		ge->RunFrame( sv.time );
	}

	if ( com_speeds->integer ) {
		time_game = Sys_Milliseconds () - startTime;
	}

	SG_TestSave();	// returns immediately if not active, used for fake-save-every-cycle to test (mainly) Icarus disk code

	// check timeouts
	SV_CheckTimeouts ();

	// update ping based on the last known frame from all clients
	SV_CalcPings ();

	// send messages back to the clients
	SV_SendClientMessages ();
}

//============================================================================

