// leave this line at the top of all AI_xxxx.cpp files for PCH reasons...
#include "g_headers.h"


#include "b_local.h"
#include "g_nav.h"

gentity_t *CreateMissile( vec3_t org, vec3_t dir, float vel, int life, gentity_t *owner, qboolean altFire = qfalse );

void Seeker_Strafe( void );

#define VELOCITY_DECAY		0.7f

#define	MIN_MELEE_RANGE		320
#define	MIN_MELEE_RANGE_SQR	( MIN_MELEE_RANGE * MIN_MELEE_RANGE )

#define MIN_DISTANCE		80
#define MIN_DISTANCE_SQR	( MIN_DISTANCE * MIN_DISTANCE )

#define SEEKER_STRAFE_VEL	100
#define SEEKER_STRAFE_DIS	200
#define SEEKER_UPWARD_PUSH	32

#define SEEKER_FORWARD_BASE_SPEED	10
#define SEEKER_FORWARD_MULTIPLIER	2

#define SEEKER_SEEK_RADIUS			1024

//------------------------------------
void NPC_Seeker_Precache(void)
{
	G_SoundIndex("sound/chars/seeker/misc/fire.wav");
	G_SoundIndex( "sound/chars/seeker/misc/hiss.wav");
	G_EffectIndex( "env/small_explode");
}

//------------------------------------
void NPC_Seeker_Pain( gentity_t *self, gentity_t *inflictor, gentity_t *other, vec3_t point, int damage, int mod,int hitLoc ) 
{
	if ( !(self->svFlags & SVF_CUSTOM_GRAVITY ))
	{//void G_Damage( gentity_t *targ, gentity_t *inflictor, gentity_t *attacker, vec3_t dir, vec3_t point, int damage, int dflags, int mod, int hitLoc=HL_NONE );
		G_Damage( self, NULL, NULL, (float*)vec3_origin, (float*)vec3_origin, 999, 0, MOD_FALLING );
	}

	SaveNPCGlobals();
	SetNPCGlobals( self );
	Seeker_Strafe();
	RestoreNPCGlobals();
	NPC_Pain( self, inflictor, other, point, damage, mod );
}

//------------------------------------
void Seeker_MaintainHeight( void )
{	
	float	dif;

	// Update our angles regardless
	NPC_UpdateAngles( qtrue, qtrue );

	// If we have an enemy, we should try to hover at or a little below enemy eye level
	if ( NPC->enemy )
	{
		if (TIMER_Done( NPC, "heightChange" ))
		{
			TIMER_Set( NPC,"heightChange",Q_irand( 1000, 3000 ));

			// Find the height difference
			dif = (NPC->enemy->currentOrigin[2] +  Q_flrand( NPC->enemy->maxs[2]/2, NPC->enemy->maxs[2]+8 )) - NPC->currentOrigin[2]; 

			// cap to prevent dramatic height shifts
			if ( fabs( dif ) > 2 )
			{
				if ( fabs( dif ) > 24 )
				{
					dif = ( dif < 0 ? -24 : 24 );
				}

				NPC->client->ps.velocity[2] = (NPC->client->ps.velocity[2]+dif)/2;
			}
		}
	}
	else
	{
		gentity_t *goal = NULL;

		if ( NPCInfo->goalEntity )	// Is there a goal?
		{
			goal = NPCInfo->goalEntity;
		}
		else
		{
			goal = NPCInfo->lastGoalEntity;
		}
		if ( goal )
		{
			dif = goal->currentOrigin[2] - NPC->currentOrigin[2];

			if ( fabs( dif ) > 24 )
			{
				ucmd.upmove = ( ucmd.upmove < 0 ? -4 : 4 );
			}
			else
			{
				if ( NPC->client->ps.velocity[2] )
				{
					NPC->client->ps.velocity[2] *= VELOCITY_DECAY;

					if ( fabs( NPC->client->ps.velocity[2] ) < 2 )
					{
						NPC->client->ps.velocity[2] = 0;
					}
				}
			}
		}
	}

	// Apply friction
	if ( NPC->client->ps.velocity[0] )
	{
		NPC->client->ps.velocity[0] *= VELOCITY_DECAY;

		if ( fabs( NPC->client->ps.velocity[0] ) < 1 )
		{
			NPC->client->ps.velocity[0] = 0;
		}
	}

	if ( NPC->client->ps.velocity[1] )
	{
		NPC->client->ps.velocity[1] *= VELOCITY_DECAY;

		if ( fabs( NPC->client->ps.velocity[1] ) < 1 )
		{
			NPC->client->ps.velocity[1] = 0;
		}
	}
}

//------------------------------------
void Seeker_Strafe( void )
{
	int		side;
	vec3_t	end, right, dir;
	trace_t	tr;

	if ( random() > 0.7f || !NPC->enemy || !NPC->enemy->client )
	{
		// Do a regular style strafe
		AngleVectors( NPC->client->renderInfo.eyeAngles, NULL, right, NULL );

		// Pick a random strafe direction, then check to see if doing a strafe would be
		//	reasonably valid
		side = ( rand() & 1 ) ? -1 : 1;
		VectorMA( NPC->currentOrigin, SEEKER_STRAFE_DIS * side, right, end );

		gi.trace( &tr, NPC->currentOrigin, NULL, NULL, end, NPC->s.number, MASK_SOLID, (EG2_Collision)0, 0 );

		// Close enough
		if ( tr.fraction > 0.9f )
		{
			VectorMA( NPC->client->ps.velocity, SEEKER_STRAFE_VEL * side, right, NPC->client->ps.velocity );

			G_Sound( NPC, G_SoundIndex( "sound/chars/seeker/misc/hiss" ));

			// Add a slight upward push
			NPC->client->ps.velocity[2] += SEEKER_UPWARD_PUSH;

			NPCInfo->standTime = level.time + 1000 + random() * 500;
		}
	}
	else
	{
		// Do a strafe to try and keep on the side of their enemy
		AngleVectors( NPC->enemy->client->renderInfo.eyeAngles, dir, right, NULL );

		// Pick a random side
		side = ( rand() & 1 ) ? -1 : 1;
		VectorMA( NPC->enemy->currentOrigin, SEEKER_STRAFE_DIS * side, right, end );

		// then add a very small bit of random in front of/behind the player action
		VectorMA( end, crandom() * 25, dir, end );

		gi.trace( &tr, NPC->currentOrigin, NULL, NULL, end, NPC->s.number, MASK_SOLID, (EG2_Collision)0, 0 );

		// Close enough
		if ( tr.fraction > 0.9f )
		{
			VectorSubtract( tr.endpos, NPC->currentOrigin, dir );
			dir[2] *= 0.25; // do less upward change
			float dis = VectorNormalize( dir );

			// Try to move the desired enemy side
			VectorMA( NPC->client->ps.velocity, dis, dir, NPC->client->ps.velocity );

			G_Sound( NPC, G_SoundIndex( "sound/chars/seeker/misc/hiss" ));

			// Add a slight upward push
			NPC->client->ps.velocity[2] += SEEKER_UPWARD_PUSH;

			NPCInfo->standTime = level.time + 2500 + random() * 500;
		}
	}
}

//------------------------------------
void Seeker_Hunt( qboolean visible, qboolean advance )
{
	float	distance, speed;
	vec3_t	forward;

	NPC_FaceEnemy( qtrue );

	// If we're not supposed to stand still, pursue the player
	if ( NPCInfo->standTime < level.time )
	{
		// Only strafe when we can see the player
		if ( visible )
		{
			Seeker_Strafe();
			return;
		}
	}

	// If we don't want to advance, stop here
	if ( advance == qfalse )
	{
		return;
	}

	// Only try and navigate if the player is visible
	if ( visible == qfalse )
	{
		// Move towards our goal
		NPCInfo->goalEntity = NPC->enemy;
		NPCInfo->goalRadius = 24;

		// Get our direction from the navigator if we can't see our target
		if ( NPC_GetMoveDirection( forward, &distance ) == qfalse )
		{
			return;
		}
	}
	else
	{
		VectorSubtract( NPC->enemy->currentOrigin, NPC->currentOrigin, forward );
		distance = VectorNormalize( forward );
	}

	speed = SEEKER_FORWARD_BASE_SPEED + SEEKER_FORWARD_MULTIPLIER * g_spskill->integer;
	VectorMA( NPC->client->ps.velocity, speed, forward, NPC->client->ps.velocity );
}

//------------------------------------
void Seeker_Fire( void )
{
	vec3_t		dir, enemy_org, muzzle;
	gentity_t	*missile;

	CalcEntitySpot( NPC->enemy, SPOT_HEAD, enemy_org );
	VectorSubtract( enemy_org, NPC->currentOrigin, dir );
	VectorNormalize( dir );

	// move a bit forward in the direction we shall shoot in so that the bolt doesn't poke out the other side of the seeker
	VectorMA( NPC->currentOrigin, 15, dir, muzzle );

	missile = CreateMissile( muzzle, dir, 1000, 10000, NPC );

	G_PlayEffect( "blaster/muzzle_flash", NPC->currentOrigin, dir );

	missile->classname = "blaster";
	missile->s.weapon = WP_BLASTER;

	missile->damage = 5;
	missile->dflags = DAMAGE_DEATH_KNOCKBACK;
	missile->methodOfDeath = MOD_ENERGY;
	missile->clipmask = MASK_SHOT | CONTENTS_LIGHTSABER;
}

//------------------------------------
void Seeker_Ranged( qboolean visible, qboolean advance )
{
	if ( NPC->count > 0 )
	{
		if ( TIMER_Done( NPC, "attackDelay" ))	// Attack?
		{
			TIMER_Set( NPC, "attackDelay", Q_irand( 250, 2500 ));
			Seeker_Fire();
			NPC->count--;
		}
	}
	else
	{
		// out of ammo, so let it die...give it a push up so it can fall more and blow up on impact
//		NPC->client->ps.gravity = 900;
//		NPC->svFlags &= ~SVF_CUSTOM_GRAVITY;
//		NPC->client->ps.velocity[2] += 16;
		G_Damage( NPC, NPC, NPC, NULL, NULL, 999, 0, MOD_UNKNOWN );
	}

	if ( NPCInfo->scriptFlags & SCF_CHASE_ENEMIES )
	{
		Seeker_Hunt( visible, advance );
	}
} 

//------------------------------------
void Seeker_Attack( void )
{
	// Always keep a good height off the ground
	Seeker_MaintainHeight();

	// Rate our distance to the target, and our visibilty
	float		distance	= DistanceHorizontalSquared( NPC->currentOrigin, NPC->enemy->currentOrigin );	
	qboolean	visible		= NPC_ClearLOS( NPC->enemy );
	qboolean	advance		= (qboolean)(distance > MIN_DISTANCE_SQR);

	// If we cannot see our target, move to see it
	if ( visible == qfalse )
	{
		if ( NPCInfo->scriptFlags & SCF_CHASE_ENEMIES )
		{
			Seeker_Hunt( visible, advance );
			return;
		}
	}

	Seeker_Ranged( visible, advance );
}

//------------------------------------
void Seeker_FindEnemy( void )
{
	int			numFound;
	float		dis, bestDis = SEEKER_SEEK_RADIUS * SEEKER_SEEK_RADIUS + 1;
	vec3_t		mins, maxs;
	gentity_t	*entityList[MAX_GENTITIES], *ent, *best = NULL;

	VectorSet( maxs, SEEKER_SEEK_RADIUS, SEEKER_SEEK_RADIUS, SEEKER_SEEK_RADIUS );
	VectorScale( maxs, -1, mins );

	numFound = gi.EntitiesInBox( mins, maxs, entityList, MAX_GENTITIES );

	for ( int i = 0 ; i < numFound ; i++ ) 
	{
		ent = entityList[i];

		if ( ent->s.number == NPC->s.number || !ent->client || !ent->NPC || ent->health <= 0 || !ent->inuse )
		{
			continue;
		}

		if ( ent->client->playerTeam == NPC->client->playerTeam || ent->client->playerTeam == TEAM_NEUTRAL ) // don't attack same team or bots
		{
			continue;
		}

		// try to find the closest visible one
		if ( !NPC_ClearLOS( ent ))
		{
			continue;
		}

		dis = DistanceHorizontalSquared( NPC->currentOrigin, ent->currentOrigin );

		if ( dis <= bestDis )
		{
			bestDis = dis;
			best = ent;
		}
	}

	if ( best )
	{
		// used to offset seekers around a circle so they don't occupy the same spot.  This is not a fool-proof method.
		NPC->random = random() * 6.3f; // roughly 2pi

		NPC->enemy = best;
	}
}

//------------------------------------
void Seeker_FollowPlayer( void )
{
	Seeker_MaintainHeight();

	float	dis	= DistanceHorizontalSquared( NPC->currentOrigin, g_entities[0].currentOrigin );
	vec3_t	pt, dir;
	
	if ( dis < MIN_DISTANCE_SQR )
	{
		// generally circle the player closely till we take an enemy..this is our target point
		pt[0] = g_entities[0].currentOrigin[0] + cos( level.time * 0.001f + NPC->random ) * 56;
		pt[1] = g_entities[0].currentOrigin[1] + sin( level.time * 0.001f + NPC->random ) * 56;
		pt[2] = g_entities[0].currentOrigin[2] + 40;

		VectorSubtract( pt, NPC->currentOrigin, dir );
		VectorMA( NPC->client->ps.velocity, 0.8f, dir, NPC->client->ps.velocity );
	}
	else
	{
		if ( TIMER_Done( NPC, "seekerhiss" ))
		{
			TIMER_Set( NPC, "seekerhiss", 1000 + random() * 1000 );
			G_Sound( NPC, G_SoundIndex( "sound/chars/seeker/misc/hiss" ));
		}

		// Hey come back!
		NPCInfo->goalEntity = &g_entities[0];
		NPCInfo->goalRadius = 32;
		NPC_MoveToGoal( qtrue );
		NPC->owner = &g_entities[0];
	}

	if ( NPCInfo->enemyCheckDebounceTime < level.time )
	{
		// check twice a second to find a new enemy
		Seeker_FindEnemy();
		NPCInfo->enemyCheckDebounceTime = level.time + 500;
	}

	NPC_UpdateAngles( qtrue, qtrue );
}

//------------------------------------
void NPC_BSSeeker_Default( void )
{
	if ( in_camera )
	{
		// cameras make me commit suicide....
		G_Damage( NPC, NPC, NPC, NULL, NULL, 999, 0, MOD_UNKNOWN );
	}

	if ( NPC->random == 0.0f )
	{
		// used to offset seekers around a circle so they don't occupy the same spot.  This is not a fool-proof method.
		NPC->random = random() * 6.3f; // roughly 2pi
	}

	if ( NPC->enemy && NPC->enemy->health && NPC->enemy->inuse )
	{
		if ( NPC->enemy->s.number == 0 || ( NPC->enemy->client && NPC->enemy->client->NPC_class == CLASS_SEEKER ))
		{
			//hacked to never take the player as an enemy, even if the player shoots at it
			NPC->enemy = NULL;
		}
		else
		{
			Seeker_Attack();
			return;
		}
	}

	// In all other cases, follow the player and look for enemies to take on
	Seeker_FollowPlayer();
}