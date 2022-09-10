/***
*
*	Copyright (c) 1996-2002, Valve LLC. All rights reserved.
*	
*	This product contains software technology licensed from Id 
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc. 
*	All Rights Reserved.
*
*   Use, distribution, and modification of this source code and/or resulting
*   object code is restricted to non-commercial enhancements to products from
*   Valve LLC.  All other use, distribution, or modification is prohibited
*   without written permission from Valve LLC.
*
****/
// shared event functions
#include "hud.h"
#include "cl_util.h"
#include "const.h"
#include "entity_state.h"
#include "cl_entity.h"

#include "r_efx.h"

#include "eventscripts.h"
#include "event_api.h"
#include "pm_shared.h"

#include "view.h"

#define IS_FIRSTPERSON_SPEC (g_iUser1 == OBS_IN_EYE || (g_iUser1 && (gHUD.m_Spectator.m_pip->value == INSET_IN_EYE)))
/*
=================
GetEntity

Return's the requested cl_entity_t
=================
*/
struct cl_entity_s* GetEntity(int idx)
{
	return gEngfuncs.GetEntityByIndex(idx);
}

/*
=================
GetViewEntity

Return's the current weapon/view model
=================
*/
struct cl_entity_s* GetViewEntity()
{
	return gEngfuncs.GetViewModel();
}

/*
=================
EV_CreateTracer

Creates a tracer effect
=================
*/
void EV_CreateTracer(float* start, float* end)
{
	gEngfuncs.pEfxAPI->R_TracerEffect(start, end);
}

/*
=================
EV_IsPlayer

Is the entity's index in the player range?
=================
*/
bool EV_IsPlayer(int idx)
{
	if (idx >= 1 && idx <= gEngfuncs.GetMaxClients())
		return true;

	return false;
}

/*
=================
EV_IsLocal

Is the entity == the local player
=================
*/
bool EV_IsLocal(int idx)
{
	// check if we are in some way in first person spec mode
	if (IS_FIRSTPERSON_SPEC)
		return (g_iUser2 == idx);
	else
		return gEngfuncs.pEventAPI->EV_IsLocal(idx - 1) != 0;
}

/*
=================
EV_GetGunPosition

Figure out the height of the gun
=================
*/
void EV_GetGunPosition(event_args_t* args, float* pos, float* origin)
{
	int idx;

	idx = args->entindex;

	if (EV_IsPlayer(idx))
	{
		Vector view_ofs = VEC_VIEW;
		if (EV_IsLocal(idx) && !IS_FIRSTPERSON_SPEC)
		{
			gEngfuncs.pEventAPI->EV_LocalPlayerViewheight((float*)&view_ofs);
		}
		else if (args->ducking == 1)
		{
			view_ofs = VEC_DUCK_VIEW;
		}
		VectorAdd(origin, view_ofs, pos);
	}
}

/*
=================
EV_EjectBrass

Bullet shell casings
=================
*/
void EV_EjectBrass(float* origin, float* velocity, float rotation, int model, int soundtype)
{
	Vector endpos;
	VectorClear(endpos);
	endpos[1] = rotation;
	gEngfuncs.pEfxAPI->R_TempModel(origin, velocity, endpos, 2.5, model, soundtype);
}

/*
=================
EV_GetDefaultShellInfo

Determine where to eject shells from
=================
*/
void EV_GetDefaultShellInfo(event_args_t* args, float* origin, float* velocity, float* ShellVelocity, float* ShellOrigin, float* forward, float* right, float* up, float forwardScale, float upScale, float rightScale)
{
	int i;
	float fR, fU;

	int idx;

	idx = args->entindex;

	Vector view_ofs = VEC_VIEW;

	if ((!EV_IsLocal(idx) || nlutils::IsThirdPerson()) && EV_IsPlayer(idx))
	{
		if (args->ducking == 1)
		{
			view_ofs = VEC_DUCK_VIEW;
		}
	}

	fR = gEngfuncs.pfnRandomFloat(50, 70);
	fU = gEngfuncs.pfnRandomFloat(100, 150);

	for (i = 0; i < 3; i++)
	{
		if (EV_IsLocal(idx) && !nlutils::IsThirdPerson())
		{
			Vector v_forward, v_right, v_up;
			Vector v_forward2, v_right2, v_up2;
			Vector v_angles = nlutils::VectorInvertPitch(GetViewEntity()->angles);
			Vector v_angles2 = nlutils::ViewParams.viewangles;

			AngleVectors(v_angles, v_forward, v_right, v_up);
			AngleVectors(v_angles2, v_forward2, v_right2, v_up2);

			ShellVelocity[i] = velocity[i] + v_right[i] * fR + v_up[i] * fU + v_forward[i] * 25;
			ShellOrigin[i] = GetViewEntity()->origin[i] + v_up2[i] * upScale + v_forward2[i] * forwardScale + v_right2[i] * rightScale;
		}
		else
		{
			ShellVelocity[i] = velocity[i] + right[i] * fR + up[i] * fU + forward[i] * 25;
			ShellOrigin[i] = origin[i] + view_ofs[i] + up[i] * upScale + forward[i] * forwardScale + right[i] * rightScale;
		}
	}
}

/*
=================
EV_MuzzleFlash

Flag weapon/view model for muzzle flash
=================
*/
void EV_MuzzleFlash()
{
	// Add muzzle flash to current weapon model
	cl_entity_t* ent = GetViewEntity();
	if (!ent)
	{
		return;
	}

	// Or in the muzzle flash
	ent->curstate.effects |= EF_MUZZLEFLASH;
}
