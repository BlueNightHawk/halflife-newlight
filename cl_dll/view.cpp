// view/refresh setup functions

#include "hud.h"
#include "cl_util.h"

#include "in_defs.h" // PITCH YAW ROLL
#include "pm_shared.h"
#include "pm_defs.h"
#include "event_api.h"
#include "hltv.h"
#include "Exports.h"
#include "view.h"

#include "r_studioint.h"
#include "com_model.h"
#include "kbutton.h"

extern engine_studio_api_t IEngineStudio;

extern kbutton_t in_mlook;

extern float vJumpOrigin[3];
extern float vJumpAngles[3];

/*
The view is allowed to move slightly from it's true position for bobbing,
but if it exceeds 8 pixels linear distance (spherical, not box), the list of
entities sent from the server may not include everything in the pvs, especially
when crossing a water boudnary.
*/

extern cvar_t* cl_forwardspeed;
extern cvar_t* chase_active;
extern cvar_t *scr_ofsx, *scr_ofsy, *scr_ofsz;
extern cvar_t* cl_vsmoothing;
extern cvar_t* cl_rollangle;
extern cvar_t* cl_rollspeed;
extern cvar_t* cl_bobtilt;

// These cvars are not registered (so users can't cheat), so set the ->value field directly
// Register these cvars in V_Init() if needed for easy tweaking
const cvar_t v_iyaw_cycle = {"v_iyaw_cycle", "2", 0, 2};
const cvar_t v_iroll_cycle = {"v_iroll_cycle", "0.5", 0, 0.5};
const cvar_t v_ipitch_cycle = {"v_ipitch_cycle", "1", 0, 1};
const cvar_t v_iyaw_level = {"v_iyaw_level", "0.3", 0, 0.3};
const cvar_t v_iroll_level = {"v_iroll_level", "0.1", 0, 0.1};
const cvar_t v_ipitch_level = {"v_ipitch_level", "0.3", 0, 0.3};

cvar_t* scr_ofsx;
cvar_t* scr_ofsy;
cvar_t* scr_ofsz;

cvar_t* v_centermove;
cvar_t* v_centerspeed;

cvar_t* cl_bobcycle;
cvar_t* cl_bob;
cvar_t* cl_bobup;
cvar_t* cl_waterdist;
cvar_t* cl_chasedist;

static float v_frametime, v_lastDistance;
static float v_cameraRelaxAngle = 5.0f;
static float v_cameraFocusAngle = 35.0f;
static int v_cameraMode = CAM_MODE_FOCUS;
static bool v_resetCamera = true;

static int v_jumpstate = 0;

static Vector ev_punchangle, ev_punch;
static Vector cl_jumpangle, cl_jumppunch;

float v_idlescale; // used by TFC for concussion grenade effect
static pitchdrift_t pd;

extern int PM_GetPhysEntInfo(int ent);
extern float Distance(const float* v1, const float* v2);

void view::UpdateJumpState(struct ref_params_s* pparams, cl_entity_s *view)
{
	cl_entity_s* pl = gEngfuncs.GetEntityByIndex(pparams->viewentity);

	static float flFallVelocity = 0.0f;
	static float flModelElevation = 0.0f;
	static float flModelElevationTime = 0.0f;

	bool onground = pparams->onground > 0;

	VectorAdd(view->angles, cl_jumpangle, view->angles);
	VectorAdd(pparams->viewangles, nlutils::VectorInvert(cl_jumpangle,false) * 0.25, pparams->viewangles);

	view->angles[0] += flModelElevation;
	view->angles[1] -= flModelElevation;

	if (!onground)
		flFallVelocity = -pparams->simvel[2];

	if (pparams->waterlevel == 0 && pl->curstate.movetype == MOVETYPE_WALK)
	{
		CalcPunchAngle(pparams->frametime, (float*)&cl_jumpangle, (float*)&cl_jumppunch);

		if (v_jumpstate == 0 && pparams->onground == 0 && flFallVelocity > 50)
		{
			v_jumpstate = 2;
		}

		if (v_jumpstate == 1)
		{
			cl_jumppunch[0] = -1.8 * 20;
			cl_jumppunch[1] = 1.5 * 20;

			v_jumpstate = 2;

			flModelElevationTime = pparams->time + 0.21;
		}
		else if (v_jumpstate == 2 && onground)
		{
			cl_jumppunch[0] = (-flModelElevation) * 15;
			cl_jumppunch[1] = (flModelElevation)*15;

			v_jumpstate = 0;

			flModelElevationTime = 0;
			flFallVelocity = 0.0f;
		}

		if (v_jumpstate == 2 && flModelElevationTime < pparams->time)
		{
			flModelElevation = nlutils::lerp(flModelElevation, 10.0f, pparams->frametime * 1.0f);
		}
		else if (v_jumpstate == 0)
		{
			flModelElevation = nlutils::lerp(flModelElevation, 0.0f, pparams->frametime * 5.0f);
		}
	}
	else
	{
		if (pparams->waterlevel != 0)
		{
			CalcPunchAngle(pparams->frametime * 0.55f, (float*)&cl_jumpangle, (float*)&cl_jumppunch);


			if (v_jumpstate == 2)
			{
				cl_jumppunch[0] = (-flModelElevation) * 10;
				cl_jumppunch[1] = (flModelElevation)*10;

				flModelElevationTime = 0;
				flFallVelocity = 0.0f;
			}
		}
		else
		{
			CalcPunchAngle(pparams->frametime, (float*)&cl_jumpangle, (float*)&cl_jumppunch);
		}
		v_jumpstate = 0;

		flModelElevation = nlutils::lerp(flModelElevation, 0.0f, pparams->frametime * 5.0f);
	}
}

void view::SetJumpState(int state)
{
	v_jumpstate = state;
}

int view::GetJumpState()
{
	return v_jumpstate;
}

// Quakeworld bob code, this fixes jitters in the mutliplayer since the clock (pparams->time) isn't quite linear
void view::CalcBob(struct ref_params_s* pparams, const float freq, const calcBobMode_t mode, clbob_s *curbob)
{
	float cycle;
	Vector vel;
	float bob = 0.0f;

	if (pparams->onground == -1 ||
		pparams->time == curbob->lasttime)
	{
		// just use old value
		return; // bob;
	}

	curbob->lasttime = pparams->time;

	curbob->bobtime += pparams->frametime * freq;
	cycle = curbob->bobtime - (int)(curbob->bobtime / cl_bobcycle->value) * cl_bobcycle->value;
	cycle /= cl_bobcycle->value;

	if (cycle < cl_bobup->value)
	{
		cycle = M_PI * cycle / cl_bobup->value;
	}
	else
	{
		cycle = M_PI + M_PI * (cycle - cl_bobup->value) / (1.0 - cl_bobup->value);
	}

	// bob is proportional to simulated velocity in the xy plane
	// (don't count Z, or jumping messes it up)
	VectorCopy(pparams->simvel, vel);
	vel[2] = 0;

    bob = sqrt(vel[0] * vel[0] + vel[1] * vel[1]) * cl_bob->value;

	if (mode == VB_SIN)
		bob = bob * 0.3 + bob * 0.7 * sin(cycle);
	else if (mode == VB_COS)
		bob = bob * 0.3 + bob * 0.7 * cos(cycle);
	else if (mode == VB_SIN2)
		bob = bob * 0.3 + bob * 0.7 * sin(cycle) * sin(cycle);
	else if (mode == VB_COS2)
		bob = bob * 0.3 + bob * 0.7 * cos(cycle) * cos(cycle);

	bob = V_min(bob, 4);
	bob = V_max(bob, -7);

	curbob->bob = nlutils::lerp(curbob->bob, bob, pparams->frametime * 15.0f);
}

/*
===============
CalcRoll
Used by view and sv_user
===============
*/
float view::CalcRoll(Vector angles, Vector velocity, float rollangle, float rollspeed)
{
	float sign;
	float side;
	float value;
	Vector forward, right, up;

	AngleVectors(angles, forward, right, up);

	side = DotProduct(velocity, right);
	sign = side < 0 ? -1 : 1;
	side = fabs(side);

	value = rollangle;
	if (side < rollspeed)
	{
		side = side * value / rollspeed;
	}
	else
	{
		side = value;
	}
	return side * sign;
}


void view::StartPitchDrift()
{
	if (pd.laststop == gEngfuncs.GetClientTime())
	{
		return; // something else is keeping it from drifting
	}

	if (0 != pd.nodrift || 0 == pd.pitchvel)
	{
		pd.pitchvel = v_centerspeed->value;
		pd.nodrift = 0;
		pd.driftmove = 0;
	}
}

void view::StopPitchDrift()
{
	pd.laststop = gEngfuncs.GetClientTime();
	pd.nodrift = 1;
	pd.pitchvel = 0;
}

/*
===============
view::DriftPitch

Moves the client pitch angle towards idealpitch sent by the server.

If the user is adjusting pitch manually, either with lookup/lookdown,
mlook and mouse, or klook and keyboard, pitch drifting is constantly stopped.
===============
*/
void view::DriftPitch(struct ref_params_s* pparams)
{
	float delta, move;

	if (0 != gEngfuncs.IsNoClipping() || 0 == pparams->onground || 0 != pparams->demoplayback || 0 != pparams->spectator)
	{
		pd.driftmove = 0;
		pd.pitchvel = 0;
		return;
	}

	// don't count small mouse motion
	if (0 != pd.nodrift)
	{
		if (v_centermove->value > 0 && (in_mlook.state & 1) == 0)
		{
			// this is for lazy players. if they stopped, looked around and then continued
			// to move the view will be centered automatically if they move more than
			// v_centermove units.

			if (fabs(pparams->cmd->forwardmove) < cl_forwardspeed->value)
				pd.driftmove = 0;
			else
				pd.driftmove += pparams->frametime;

			if (pd.driftmove > v_centermove->value)
			{
				StartPitchDrift();
			}
			else
			{
				return; // player didn't move enough
			}
		}

		return; // don't drift view
	}

	delta = pparams->idealpitch - pparams->cl_viewangles[PITCH];

	if (0 == delta)
	{
		pd.pitchvel = 0;
		return;
	}

	move = pparams->frametime * pd.pitchvel;

	pd.pitchvel *= (1.0f + (pparams->frametime * 0.25f)); // get faster by time

	if (delta > 0)
	{
		if (move > delta)
		{
			pd.pitchvel = 0;
			move = delta;
		}
		pparams->cl_viewangles[PITCH] += move;
	}
	else if (delta < 0)
	{
		if (move > -delta)
		{
			pd.pitchvel = 0;
			move = -delta;
		}
		pparams->cl_viewangles[PITCH] -= move;
	}
}

/* 
============================================================================== 
						VIEW RENDERING 
============================================================================== 
*/

/*
==================
view::CalcGunAngle
==================
*/
void view::CalcGunAngle(struct ref_params_s* pparams)
{
	cl_entity_t* viewent;

	viewent = gEngfuncs.GetViewModel();
	if (!viewent)
		return;

	for (int i = 0; i < 2; i++)
		pparams->crosshairangle[i] = nlutils::lerp(pparams->crosshairangle[i], gHUD.m_flCrosshairOfs[i], pparams->frametime * 5.0f);

	viewent->angles[YAW] = pparams->viewangles[YAW] + pparams->crosshairangle[YAW];
	viewent->angles[PITCH] = -pparams->viewangles[PITCH] + pparams->crosshairangle[PITCH] * 0.25;
	viewent->angles[ROLL] -= v_idlescale * sin(pparams->time * v_iroll_cycle.value) * v_iroll_level.value;

	// don't apply all of the v_ipitch to prevent normally unseen parts of viewmodel from coming into view.
	viewent->angles[PITCH] -= v_idlescale * sin(pparams->time * v_ipitch_cycle.value) * (v_ipitch_level.value * 0.5);
	viewent->angles[YAW] -= v_idlescale * sin(pparams->time * v_iyaw_cycle.value) * v_iyaw_level.value;

	VectorCopy(viewent->angles, viewent->curstate.angles);
	VectorCopy(viewent->angles, viewent->latched.prevangles);
}

/*
==============
view::AddIdle

Idle swaying
==============
*/
void view::AddIdle(struct ref_params_s* pparams)
{
	pparams->viewangles[ROLL] += v_idlescale * sin(pparams->time * v_iroll_cycle.value) * v_iroll_level.value;
	pparams->viewangles[PITCH] += v_idlescale * sin(pparams->time * v_ipitch_cycle.value) * v_ipitch_level.value;
	pparams->viewangles[YAW] += v_idlescale * sin(pparams->time * v_iyaw_cycle.value) * v_iyaw_level.value;
}


/*
==============
view::CalcViewRoll

Roll is induced by movement and damage
==============
*/
void view::CalcViewRoll(struct ref_params_s* pparams, cl_entity_s* view)
{
	static float interp_roll = 0;
	cl_entity_t* viewentity;

	viewentity = gEngfuncs.GetEntityByIndex(pparams->viewentity);
	if (!viewentity)
		return;

	float roll = CalcRoll(viewentity->angles, pparams->simvel, cl_rollangle->value, cl_rollspeed->value);

	interp_roll = nlutils::lerp(interp_roll, roll, pparams->frametime * 17.0f);

	view->angles[ROLL] += interp_roll * 1.5f;

	if (pparams->health <= 0 && (pparams->viewheight[2] != 0))
	{
		// only roll the view if the player is dead and the viewheight[2] is nonzero
		// this is so deadcam in multiplayer will work.
		pparams->viewangles[ROLL] = 80; // dead view angle
		return;
	}
}


/*
==================
view::CalcIntermissionRefdef

==================
*/
void view::CalcIntermissionRefdef(struct ref_params_s* pparams)
{
	cl_entity_t *ent, *view;
	float old;

	// ent is the player model ( visible when out of body )
	ent = gEngfuncs.GetLocalPlayer();

	// view is the weapon model (only visible from inside body )
	view = gEngfuncs.GetViewModel();

	VectorCopy(pparams->simorg, pparams->vieworg);
	VectorCopy(pparams->cl_viewangles, pparams->viewangles);

	view->model = NULL;

	// allways idle in intermission
	old = v_idlescale;
	v_idlescale = 1;

	AddIdle(pparams);

	if (0 != gEngfuncs.IsSpectateOnly())
	{
		// in HLTV we must go to 'intermission' position by ourself
		VectorCopy(gHUD.m_Spectator.m_cameraOrigin, pparams->vieworg);
		VectorCopy(gHUD.m_Spectator.m_cameraAngles, pparams->viewangles);
	}

	v_idlescale = old;

	v_cl_angles = pparams->cl_viewangles;
	v_origin = pparams->vieworg;
	v_angles = pparams->viewangles;
}

void view::CalcViewModelLag(ref_params_t* pparams, Vector& origin, const Vector angles, const Vector original_angles)
{
	const float m_flWeaponLag = 1.5f;

	static Vector m_vecLastFacing;
	Vector vOriginalOrigin = origin;
	Vector vOriginalAngles = angles;

	// Calculate our drift
	Vector forward, right, up;
	AngleVectors(angles, forward, right, up);

	if (pparams->frametime != 0.0f) // not in paused
	{
		Vector vDifference;

		vDifference = forward - m_vecLastFacing;

		float flSpeed = 5.0f;

		// If we start to lag too far behind, we'll increase the "catch up" speed.
		// Solves the problem with fast cl_yawspeed, m_yaw or joysticks rotating quickly.
		// The old code would slam lastfacing with origin causing the viewmodel to pop to a new position
		float flDiff = vDifference.Length();
		if ((flDiff > m_flWeaponLag) && (m_flWeaponLag > 0.0f))
		{
			float flScale = flDiff / m_flWeaponLag;
			flSpeed *= flScale;
		}

		// FIXME:  Needs to be predictable?
		m_vecLastFacing = m_vecLastFacing + vDifference * (flSpeed * pparams->frametime);
		// Make sure it doesn't grow out of control!!!
		m_vecLastFacing = m_vecLastFacing.Normalize();
		origin = origin + (vDifference * -1.0f) * 3.0f;
	}

	AngleVectors(original_angles, forward, right, up);

	float pitch = original_angles[PITCH];

	if (pitch > 180.0f)
	{
		pitch -= 360.0f;
	}
	else if (pitch < -180.0f)
	{
		pitch += 360.0f;
	}

	if (m_flWeaponLag <= 0.0f)
	{
		origin = vOriginalOrigin;
	}
	else
	{
		// FIXME: These are the old settings that caused too many exposed polys on some models
		origin = origin + forward * (-pitch * 0.012f);
		origin = origin + right * (-pitch * 0.02f);
		origin = origin + up * (-pitch * 0.005f);
	}
}


void view::RetractWeapon(struct ref_params_s* pparams, cl_entity_s* view)
{
	cl_entity_s* pplayer = gEngfuncs.GetLocalPlayer();
	static float flRetract = 0.0f;
	static float flRetract2 = 0.0f;
	Vector vecSrc, vecEnd;
	int idx = pparams->viewentity;
	pmtrace_t tr;

	if ((pplayer->curstate.eflags & EFLAG_PLAYERKICK) == 0)
	{
		vecSrc = pparams->vieworg;
		vecEnd = vecSrc + Vector(pparams->forward) * 30;

		gEngfuncs.pEventAPI->EV_SetUpPlayerPrediction(0, 1);

		// Store off the old count
		gEngfuncs.pEventAPI->EV_PushPMStates();

		// Now add in all of the players.
		gEngfuncs.pEventAPI->EV_SetSolidPlayers(idx - 1);

		gEngfuncs.pEventAPI->EV_SetTraceHull(2);
		gEngfuncs.pEventAPI->EV_PlayerTrace(vecSrc, vecEnd, PM_STUDIO_BOX, -1, &tr);

		flRetract = nlutils::lerp(flRetract, (1 - tr.fraction), pparams->frametime * 8.0f);
		flRetract2 = nlutils::lerp(flRetract2, 0.0, pparams->frametime * 10.0f);

		gEngfuncs.pEventAPI->EV_PopPMStates();
	}
	else
	{
		flRetract = nlutils::lerp(flRetract, 0, pparams->frametime * 8.0f);
		flRetract2 = nlutils::lerp(flRetract2, 1.0f, pparams->frametime * 10.0f);
	}

	view->origin = view->origin - Vector(pparams->forward) * (flRetract * 8.5f);
	view->angles[1] += (flRetract * 8.5f);

	view->origin = view->origin - Vector(pparams->up) * (flRetract * 8.5f);
	view->angles[0] -= (flRetract2 * 8.5f);
	view->angles[1] -= (flRetract2 * 8.5f);
}

/*
==================
view::CalcRefdef

==================
*/
void view::CalcNormalRefdef(struct ref_params_s* pparams)
{
	cl_entity_t *ent, *view;
	int i;
	Vector angles;
	float waterOffset;
	static viewinterp_t ViewInterp;

	static float oldz = 0;
	static float lasttime;

	Vector camAngles, camForward, camRight, camUp;
	cl_entity_t* pwater;

	static Vector viewheight = VEC_VIEW;

	static clbob_s bobup, bobright;

	DriftPitch(pparams);

	if (0 != gEngfuncs.IsSpectateOnly())
	{
		ent = gEngfuncs.GetEntityByIndex(g_iUser2);
	}
	else
	{
		// ent is the player model ( visible when out of body )
		ent = gEngfuncs.GetLocalPlayer();
	}

	// interpolate
	if (pparams->viewheight[2] == 28.0f && viewheight[2] < 28.0f)
	{
		viewheight[2] = nlutils::lerp(viewheight[2], 28.0f, pparams->frametime * 17.0f);
	}
	else
	{
		viewheight[2] = pparams->viewheight[2];
	}


	// view is the weapon model (only visible from inside body )
	view = gEngfuncs.GetViewModel();

	// transform the view offset by the model's matrix to get the offset from
	// model origin for the view
	CalcBob(pparams, 0.75f, VB_SIN, &bobright); // right
	CalcBob(pparams, 1.50f, VB_SIN, &bobup); // up

	// refresh position
	VectorCopy(pparams->simorg, pparams->vieworg);
	VectorAdd(pparams->vieworg, viewheight, pparams->vieworg);

	VectorCopy(pparams->cl_viewangles, pparams->viewangles);

	gEngfuncs.V_CalcShake();
	gEngfuncs.V_ApplyShake(pparams->vieworg, pparams->viewangles, 1.0);

	// never let view origin sit exactly on a node line, because a water plane can
	// dissapear when viewed with the eye exactly on it.
	// FIXME, we send origin at 1/128 now, change this?
	// the server protocol only specifies to 1/16 pixel, so add 1/32 in each axis

	pparams->vieworg[0] += 1.0 / 32;
	pparams->vieworg[1] += 1.0 / 32;
	pparams->vieworg[2] += 1.0 / 32;

	// Check for problems around water, move the viewer artificially if necessary
	// -- this prevents drawing errors in GL due to waves

	waterOffset = 0;
	if (pparams->waterlevel >= 2)
	{
		int i, contents, waterDist, waterEntity;
		Vector point;
		waterDist = cl_waterdist->value;

		if (0 != pparams->hardware)
		{
			waterEntity = gEngfuncs.PM_WaterEntity(pparams->simorg);
			if (waterEntity >= 0 && waterEntity < pparams->max_entities)
			{
				pwater = gEngfuncs.GetEntityByIndex(waterEntity);
				if (pwater && (pwater->model != NULL))
				{
					waterDist += (pwater->curstate.scale * 16); // Add in wave height
				}
			}
		}
		else
		{
			waterEntity = 0; // Don't need this in software
		}

		VectorCopy(pparams->vieworg, point);

		// Eyes are above water, make sure we're above the waves
		if (pparams->waterlevel == 2)
		{
			point[2] -= waterDist;
			for (i = 0; i < waterDist; i++)
			{
				contents = gEngfuncs.PM_PointContents(point, NULL);
				if (contents > CONTENTS_WATER)
					break;
				point[2] += 1;
			}
			waterOffset = (point[2] + waterDist) - pparams->vieworg[2];
		}
		else
		{
			// eyes are under water.  Make sure we're far enough under
			point[2] += waterDist;

			for (i = 0; i < waterDist; i++)
			{
				contents = gEngfuncs.PM_PointContents(point, NULL);
				if (contents <= CONTENTS_WATER)
					break;
				point[2] -= 1;
			}
			waterOffset = (point[2] - waterDist) - pparams->vieworg[2];
		}
	}

	pparams->vieworg[2] += waterOffset;

	AddIdle(pparams);

	// offsets
	VectorCopy(pparams->cl_viewangles, angles);

	AngleVectors(angles, pparams->forward, pparams->right, pparams->up);

	// don't allow cheats in multiplayer
	if (pparams->maxclients <= 1)
	{
		for (i = 0; i < 3; i++)
		{
			pparams->vieworg[i] += scr_ofsx->value * pparams->forward[i] + scr_ofsy->value * pparams->right[i] + scr_ofsz->value * pparams->up[i];
		}
	}

	// Treating cam_ofs[2] as the distance
	if (0 != CL_IsThirdPerson())
	{
		Vector ofs;

		ofs[0] = ofs[1] = ofs[2] = 0.0;

		CL_CameraOffset((float*)&ofs);

		VectorCopy(ofs, camAngles);
		camAngles[ROLL] = 0;

		AngleVectors(camAngles, camForward, camRight, camUp);

		for (i = 0; i < 3; i++)
		{
			pparams->vieworg[i] += -ofs[2] * camForward[i];
		}
	}

	// Give gun our viewangles
	VectorCopy(pparams->cl_viewangles, view->angles);

	// set up gun position
	CalcGunAngle(pparams);

	// Use predicted origin as view origin.
	VectorCopy(pparams->simorg, view->origin);
	view->origin[2] += (waterOffset);
	VectorAdd(view->origin, viewheight, view->origin);

	// Let the viewmodel shake at about 10% of the amplitude
	gEngfuncs.V_ApplyShake(view->origin, view->angles, 0.9);

	for (i = 0; i < 3; i++)
	{
		//view->origin[i] += bobright.bob * 0.33 * pparams->right[i];
		view->origin[i] -= bobup.bob * 0.30 * pparams->up[i];
		view->origin[i] -= 1 * pparams->up[i];
	}

	//view->angles[0] += bobup.bob * 0.19;
	view->angles[1] -= bobright.bob * 0.73;
	//view->angles[2] += bobup.bob * 0.13;

	pparams->viewangles[0] += bobup.bob * 0.14;
	pparams->viewangles[1] += bobright.bob * 0.17;

	UpdateJumpState(pparams, view);
	CalcViewRoll(pparams, view);
	CalcViewModelLag(pparams, view->origin, pparams->viewangles, pparams->cl_viewangles);
	RetractWeapon(pparams, view);

	VectorCopy(view->angles, view->curstate.angles);

	// fudge position around to keep amount of weapon visible
	// roughly equal with different FOV
	if (pparams->viewsize == 110)
	{
		view->origin[2] += 1;
	}
	else if (pparams->viewsize == 100)
	{
		view->origin[2] += 2;
	}
	else if (pparams->viewsize == 90)
	{
		view->origin[2] += 1;
	}
	else if (pparams->viewsize == 80)
	{
		view->origin[2] += 0.5;
	}

	// Add in the punchangle, if any
	VectorAdd(pparams->viewangles, pparams->punchangle, pparams->viewangles);

	// Include client side punch, too
	VectorAdd(pparams->viewangles, (float*)&ev_punchangle, pparams->viewangles);

	CalcPunchAngle(pparams->frametime, (float*)&ev_punchangle, (float *)&ev_punch);

	// smooth out stair step ups
#if 1
	if (0 == pparams->smoothing && 0 != pparams->onground && pparams->simorg[2] - oldz > 0)
	{
		float steptime;

		steptime = pparams->time - lasttime;
		if (steptime < 0)
			//FIXME		I_Error ("steptime < 0");
			steptime = 0;

		oldz += steptime * 150;
		if (oldz > pparams->simorg[2])
			oldz = pparams->simorg[2];
		if (pparams->simorg[2] - oldz > 18)
			oldz = pparams->simorg[2] - 18;
		pparams->vieworg[2] += oldz - pparams->simorg[2];
		view->origin[2] += oldz - pparams->simorg[2];
	}
	else
	{
		oldz = pparams->simorg[2];
	}
#endif

	{
		static float lastorg[3];
		Vector delta;

		VectorSubtract(pparams->simorg, lastorg, delta);

		if (Length(delta) != 0.0)
		{
			VectorCopy(pparams->simorg, ViewInterp.Origins[ViewInterp.CurrentOrigin & ORIGIN_MASK]);
			ViewInterp.OriginTime[ViewInterp.CurrentOrigin & ORIGIN_MASK] = pparams->time;
			ViewInterp.CurrentOrigin++;

			VectorCopy(pparams->simorg, lastorg);
		}
	}

	// Smooth out whole view in multiplayer when on trains, lifts
	if (cl_vsmoothing && 0 != cl_vsmoothing->value &&
		(0 != pparams->smoothing && (pparams->maxclients > 1)))
	{
		int foundidx;
		int i;
		float t;

		if (cl_vsmoothing->value < 0.0)
		{
			gEngfuncs.Cvar_SetValue("cl_vsmoothing", 0.0);
		}

		t = pparams->time - cl_vsmoothing->value;

		for (i = 1; i < ORIGIN_MASK; i++)
		{
			foundidx = ViewInterp.CurrentOrigin - 1 - i;
			if (ViewInterp.OriginTime[foundidx & ORIGIN_MASK] <= t)
				break;
		}

		if (i < ORIGIN_MASK && ViewInterp.OriginTime[foundidx & ORIGIN_MASK] != 0.0)
		{
			// Interpolate
			Vector delta;
			double frac;
			double dt;
			Vector neworg;

			dt = ViewInterp.OriginTime[(foundidx + 1) & ORIGIN_MASK] - ViewInterp.OriginTime[foundidx & ORIGIN_MASK];
			if (dt > 0.0)
			{
				frac = (t - ViewInterp.OriginTime[foundidx & ORIGIN_MASK]) / dt;
				frac = V_min(1.0, frac);
				VectorSubtract(ViewInterp.Origins[(foundidx + 1) & ORIGIN_MASK], ViewInterp.Origins[foundidx & ORIGIN_MASK], delta);
				VectorMA(ViewInterp.Origins[foundidx & ORIGIN_MASK], frac, delta, neworg);

				// Dont interpolate large changes
				if (Length(delta) < 64)
				{
					VectorSubtract(neworg, pparams->simorg, delta);

					VectorAdd(pparams->simorg, delta, pparams->simorg);
					VectorAdd(pparams->vieworg, delta, pparams->vieworg);
					VectorAdd(view->origin, delta, view->origin);
				}
			}
		}
	}

	// Store off v_angles before munging for third person
	v_angles = pparams->viewangles;
	v_lastAngles = pparams->viewangles;
	//	v_cl_angles = pparams->cl_viewangles;	// keep old user mouse angles !
	if (0 != CL_IsThirdPerson())
	{
		VectorCopy(camAngles, pparams->viewangles);
	}

	//Apply this at all times
	{
		float pitch = pparams->viewangles[0];

		// Normalize angles
		if (pitch > 180)
			pitch -= 360.0;
		else if (pitch < -180)
			pitch += 360;

		// Player pitch is inverted
		pitch /= -3.0;

		// Slam local player's pitch value
		ent->angles[0] = pitch;
		ent->curstate.angles[0] = pitch;
		ent->prevstate.angles[0] = pitch;
		ent->latched.prevangles[0] = pitch;
	}

	// override all previous settings if the viewent isn't the client
	if (pparams->viewentity > pparams->maxclients)
	{
		cl_entity_t* viewentity;
		viewentity = gEngfuncs.GetEntityByIndex(pparams->viewentity);
		if (viewentity)
		{
			VectorCopy(viewentity->origin, pparams->vieworg);
			VectorCopy(viewentity->angles, pparams->viewangles);

			// Store off overridden viewangles
			v_angles = pparams->viewangles;
		}
	}

	lasttime = pparams->time;

	v_origin = pparams->vieworg;
}

void view::SmoothInterpolateAngles(float* startAngle, float* endAngle, float* finalAngle, float degreesPerSec)
{
	float absd, frac, d, threshhold;

	NormalizeAngles(startAngle);
	NormalizeAngles(endAngle);

	for (int i = 0; i < 3; i++)
	{
		d = endAngle[i] - startAngle[i];

		if (d > 180.0f)
		{
			d -= 360.0f;
		}
		else if (d < -180.0f)
		{
			d += 360.0f;
		}

		absd = fabs(d);

		if (absd > 0.01f)
		{
			frac = degreesPerSec * v_frametime;

			threshhold = degreesPerSec / 4;

			if (absd < threshhold)
			{
				float h = absd / threshhold;
				h *= h;
				frac *= h; // slow down last degrees
			}

			if (frac > absd)
			{
				finalAngle[i] = endAngle[i];
			}
			else
			{
				if (d > 0)
					finalAngle[i] = startAngle[i] + frac;
				else
					finalAngle[i] = startAngle[i] - frac;
			}
		}
		else
		{
			finalAngle[i] = endAngle[i];
		}
	}

	NormalizeAngles(finalAngle);
}

// Get the origin of the Observer based around the target's position and angles
void view::GetChaseOrigin(float* angles, float* origin, float distance, float* returnvec)
{
	Vector vecEnd;
	Vector forward;
	Vector vecStart;
	pmtrace_t* trace;
	int maxLoops = 8;

	int ignoreent = -1; // first, ignore no entity

	cl_entity_t* ent = NULL;

	// Trace back from the target using the player's view angles
	AngleVectors(angles, forward, NULL, NULL);

	VectorScale(forward, -1, forward);

	VectorCopy(origin, vecStart);

	VectorMA(vecStart, distance, forward, vecEnd);

	while (maxLoops > 0)
	{
		trace = gEngfuncs.PM_TraceLine(vecStart, vecEnd, PM_TRACELINE_PHYSENTSONLY, 2, ignoreent);

		// WARNING! trace->ent is is the number in physent list not the normal entity number

		if (trace->ent <= 0)
			break; // we hit the world or nothing, stop trace

		ent = gEngfuncs.GetEntityByIndex(PM_GetPhysEntInfo(trace->ent));

		if (ent == NULL)
			break;

		// hit non-player solid BSP , stop here
		if (ent->curstate.solid == SOLID_BSP && 0 == ent->player)
			break;

		// if close enought to end pos, stop, otherwise continue trace
		if (Distance(trace->endpos, vecEnd) < 1.0f)
		{
			break;
		}
		else
		{
			ignoreent = trace->ent; // ignore last hit entity
			VectorCopy(trace->endpos, vecStart);
		}

		maxLoops--;
	}

	/*	if ( ent )
	{
		gEngfuncs.Con_Printf("Trace loops %i , entity %i, model %s, solid %i\n",(8-maxLoops),ent->curstate.number, ent->model->name , ent->curstate.solid ); 
	} */

	VectorMA(trace->endpos, 4, trace->plane.normal, returnvec);

	v_lastDistance = Distance(trace->endpos, origin); // real distance without offset
}

void view::GetSingleTargetCam(cl_entity_t* ent1, float* angle, float* origin)
{
	float newAngle[3];
	float newOrigin[3];

	int flags = gHUD.m_Spectator.m_iObserverFlags;

	// see is target is a dead player
	bool deadPlayer = 0 != ent1->player && (ent1->curstate.solid == SOLID_NOT);

	float dfactor = (flags & DRC_FLAG_DRAMATIC) != 0 ? -1.0f : 1.0f;

	float distance = 112.0f + (16.0f * dfactor); // get close if dramatic;

	// go away in final scenes or if player just died
	if ((flags & DRC_FLAG_FINAL) != 0)
		distance *= 2.0f;
	else if (deadPlayer)
		distance *= 1.5f;

	// let v_lastDistance float smoothly away
	v_lastDistance += v_frametime * 32.0f; // move unit per seconds back

	if (distance > v_lastDistance)
		distance = v_lastDistance;

	VectorCopy(ent1->origin, newOrigin);

	if (0 != ent1->player)
	{
		if (deadPlayer)
			newOrigin[2] += 2; //laying on ground
		else
			newOrigin[2] += 17; // head level of living player
	}
	else
		newOrigin[2] += 8; // object, tricky, must be above bomb in CS

	// we have no second target, choose view direction based on
	// show front of primary target
	VectorCopy(ent1->angles, newAngle);

	// show dead players from front, normal players back
	if ((flags & DRC_FLAG_FACEPLAYER) != 0)
		newAngle[1] += 180.0f;


	newAngle[0] += 12.5f * dfactor; // lower angle if dramatic

	// if final scene (bomb), show from real high pos
	if ((flags & DRC_FLAG_FINAL) != 0)
		newAngle[0] = 22.5f;

	// choose side of object/player
	if ((flags & DRC_FLAG_SIDE) != 0)
		newAngle[1] += 22.5f;
	else
		newAngle[1] -= 22.5f;

	SmoothInterpolateAngles(v_lastAngles, newAngle, angle, 120.0f);

	// HACK, if player is dead don't clip against his dead body, can't check this
	GetChaseOrigin(angle, newOrigin, distance, origin);
}

float view::MaxAngleBetweenAngles(float* a1, float* a2)
{
	float d, maxd = 0.0f;

	NormalizeAngles(a1);
	NormalizeAngles(a2);

	for (int i = 0; i < 3; i++)
	{
		d = a2[i] - a1[i];
		if (d > 180)
		{
			d -= 360;
		}
		else if (d < -180)
		{
			d += 360;
		}

		d = fabs(d);

		if (d > maxd)
			maxd = d;
	}

	return maxd;
}

void view::GetDoubleTargetsCam(cl_entity_t* ent1, cl_entity_t* ent2, float* angle, float* origin)
{
	float newAngle[3];
	float newOrigin[3];
	float tempVec[3];

	int flags = gHUD.m_Spectator.m_iObserverFlags;

	float dfactor = (flags & DRC_FLAG_DRAMATIC) != 0 ? -1.0f : 1.0f;

	float distance = 112.0f + (16.0f * dfactor); // get close if dramatic;

	// go away in final scenes or if player just died
	if ((flags & DRC_FLAG_FINAL) != 0)
		distance *= 2.0f;

	// let v_lastDistance float smoothly away
	v_lastDistance += v_frametime * 32.0f; // move unit per seconds back

	if (distance > v_lastDistance)
		distance = v_lastDistance;

	VectorCopy(ent1->origin, newOrigin);

	if (0 != ent1->player)
		newOrigin[2] += 17; // head level of living player
	else
		newOrigin[2] += 8; // object, tricky, must be above bomb in CS

	// get new angle towards second target
	VectorSubtract(ent2->origin, ent1->origin, newAngle);

	VectorAngles(newAngle, newAngle);
	newAngle[0] = -newAngle[0];

	// set angle diffrent in Dramtaic scenes
	newAngle[0] += 12.5f * dfactor; // lower angle if dramatic

	if ((flags & DRC_FLAG_SIDE) != 0)
		newAngle[1] += 22.5f;
	else
		newAngle[1] -= 22.5f;

	float d = MaxAngleBetweenAngles(v_lastAngles, newAngle);

	if ((d < v_cameraFocusAngle) && (v_cameraMode == CAM_MODE_RELAX))
	{
		// difference is to small and we are in relax camera mode, keep viewangles
		VectorCopy(v_lastAngles, newAngle);
	}
	else if ((d < v_cameraRelaxAngle) && (v_cameraMode == CAM_MODE_FOCUS))
	{
		// we catched up with our target, relax again
		v_cameraMode = CAM_MODE_RELAX;
	}
	else
	{
		// target move too far away, focus camera again
		v_cameraMode = CAM_MODE_FOCUS;
	}

	// and smooth view, if not a scene cut
	if (v_resetCamera || (v_cameraMode == CAM_MODE_RELAX))
	{
		VectorCopy(newAngle, angle);
	}
	else
	{
		SmoothInterpolateAngles(v_lastAngles, newAngle, angle, 180.0f);
	}

	GetChaseOrigin(newAngle, newOrigin, distance, origin);

	// move position up, if very close at target
	if (v_lastDistance < 64.0f)
		origin[2] += 16.0f * (1.0f - (v_lastDistance / 64.0f));

	// calculate angle to second target
	VectorSubtract(ent2->origin, origin, tempVec);
	VectorAngles(tempVec, tempVec);
	tempVec[0] = -tempVec[0];

	/* take middle between two viewangles
	InterpolateAngles( newAngle, tempVec, newAngle, 0.5f); */
}

void view::GetDirectedChasePosition(cl_entity_t* ent1, cl_entity_t* ent2, float* angle, float* origin)
{

	if (v_resetCamera)
	{
		v_lastDistance = 4096.0f;
		// v_cameraMode = CAM_MODE_FOCUS;
	}

	if ((ent2 == (cl_entity_t*)0xFFFFFFFF) || (0 != ent1->player && (ent1->curstate.solid == SOLID_NOT)))
	{
		// we have no second target or player just died
		GetSingleTargetCam(ent1, angle, origin);
	}
	else if (ent2)
	{
		// keep both target in view
		GetDoubleTargetsCam(ent1, ent2, angle, origin);
	}
	else
	{
		// second target disappeard somehow (dead)

		// keep last good viewangle
		float newOrigin[3];

		int flags = gHUD.m_Spectator.m_iObserverFlags;

		float dfactor = (flags & DRC_FLAG_DRAMATIC) != 0 ? -1.0f : 1.0f;

		float distance = 112.0f + (16.0f * dfactor); // get close if dramatic;

		// go away in final scenes or if player just died
		if ((flags & DRC_FLAG_FINAL) != 0)
			distance *= 2.0f;

		// let v_lastDistance float smoothly away
		v_lastDistance += v_frametime * 32.0f; // move unit per seconds back

		if (distance > v_lastDistance)
			distance = v_lastDistance;

		VectorCopy(ent1->origin, newOrigin);

		if (0 != ent1->player)
			newOrigin[2] += 17; // head level of living player
		else
			newOrigin[2] += 8; // object, tricky, must be above bomb in CS

		GetChaseOrigin(angle, newOrigin, distance, origin);
	}

	VectorCopy(angle, v_lastAngles);
}

void view::GetChasePos(int target, float* cl_angles, float* origin, float* angles)
{
	cl_entity_t* ent = NULL;

	if (0 != target)
	{
		ent = gEngfuncs.GetEntityByIndex(target);
	};

	if (!ent)
	{
		// just copy a save in-map position
		VectorCopy(vJumpAngles, angles);
		VectorCopy(vJumpOrigin, origin);
		return;
	}



	if (0 != gHUD.m_Spectator.m_autoDirector->value)
	{
		if (0 != g_iUser3)
			GetDirectedChasePosition(ent, gEngfuncs.GetEntityByIndex(g_iUser3),
				angles, origin);
		else
			GetDirectedChasePosition(ent, (cl_entity_t*)0xFFFFFFFF,
				angles, origin);
	}
	else
	{
		if (cl_angles == NULL) // no mouse angles given, use entity angles ( locked mode )
		{
			VectorCopy(ent->angles, angles);
			angles[0] *= -1;
		}
		else
			VectorCopy(cl_angles, angles);


		VectorCopy(ent->origin, origin);

		VectorAdd(origin, VEC_VIEW, origin); // some offset

		GetChaseOrigin(angles, origin, cl_chasedist->value, origin);
	}

	v_resetCamera = false;
}

void view::ResetChaseCam()
{
	v_resetCamera = true;
}


void view::GetInEyePos(int target, float* origin, float* angles)
{
	if (0 == target)
	{
		// just copy a save in-map position
		VectorCopy(vJumpAngles, angles);
		VectorCopy(vJumpOrigin, origin);
		return;
	};


	cl_entity_t* ent = gEngfuncs.GetEntityByIndex(target);

	if (!ent)
		return;

	VectorCopy(ent->origin, origin);
	VectorCopy(ent->angles, angles);

	angles[PITCH] *= -3.0f; // see CL_ProcessEntityUpdate()

	if (ent->curstate.solid == SOLID_NOT)
	{
		angles[ROLL] = 80; // dead view angle
		VectorAdd(origin, VEC_DEAD_VIEW, origin);
	}
	else if (ent->curstate.usehull == 1)
	{
		VectorAdd(origin, VEC_DUCK_VIEW, origin);
	}
	else
		// exacty eye position can't be caluculated since it depends on
		// client values like cl_bobcycle, this offset matches the default values
		VectorAdd(origin, VEC_VIEW, origin);
}

void view::GetMapFreePosition(float* cl_angles, float* origin, float* angles)
{
	Vector forward;
	Vector zScaledTarget;

	VectorCopy(cl_angles, angles);

	// modify angles since we don't wanna see map's bottom
	angles[0] = 51.25f + 38.75f * (angles[0] / 90.0f);

	zScaledTarget[0] = gHUD.m_Spectator.m_mapOrigin[0];
	zScaledTarget[1] = gHUD.m_Spectator.m_mapOrigin[1];
	zScaledTarget[2] = gHUD.m_Spectator.m_mapOrigin[2] * ((90.0f - angles[0]) / 90.0f);


	AngleVectors(angles, forward, NULL, NULL);

	VectorNormalize(forward);

	VectorMA(zScaledTarget, -(4096.0f / gHUD.m_Spectator.m_mapZoom), forward, origin);
}

void view::GetMapChasePosition(int target, float* cl_angles, float* origin, float* angles)
{
	Vector forward;

	if (0 != target)
	{
		cl_entity_t* ent = gEngfuncs.GetEntityByIndex(target);

		if (0 != gHUD.m_Spectator.m_autoDirector->value)
		{
			// this is done to get the angles made by director mode
			GetChasePos(target, cl_angles, origin, angles);
			VectorCopy(ent->origin, origin);

			// keep fix chase angle horizontal
			angles[0] = 45.0f;
		}
		else
		{
			VectorCopy(cl_angles, angles);
			VectorCopy(ent->origin, origin);

			// modify angles since we don't wanna see map's bottom
			angles[0] = 51.25f + 38.75f * (angles[0] / 90.0f);
		}
	}
	else
	{
		// keep out roaming position, but modify angles
		VectorCopy(cl_angles, angles);
		angles[0] = 51.25f + 38.75f * (angles[0] / 90.0f);
	}

	origin[2] *= ((90.0f - angles[0]) / 90.0f);
	angles[2] = 0.0f; // don't roll angle (if chased player is dead)

	AngleVectors(angles, forward, NULL, NULL);

	VectorNormalize(forward);

	VectorMA(origin, -1536, forward, origin);
}

int view::FindViewModelByWeaponModel(int weaponindex)
{

	static const char* modelmap[][2] = {

		{"models/p_crossbow.mdl", "models/v_crossbow.mdl"},
		{"models/p_crowbar.mdl", "models/v_crowbar.mdl"},
		{"models/p_egon.mdl", "models/v_egon.mdl"},
		{"models/p_gauss.mdl", "models/v_gauss.mdl"},
		{"models/p_9mmhandgun.mdl", "models/v_9mmhandgun.mdl"},
		{"models/p_grenade.mdl", "models/v_grenade.mdl"},
		{"models/p_hgun.mdl", "models/v_hgun.mdl"},
		{"models/p_9mmAR.mdl", "models/v_9mmAR.mdl"},
		{"models/p_357.mdl", "models/v_357.mdl"},
		{"models/p_rpg.mdl", "models/v_rpg.mdl"},
		{"models/p_shotgun.mdl", "models/v_shotgun.mdl"},
		{"models/p_squeak.mdl", "models/v_squeak.mdl"},
		{"models/p_tripmine.mdl", "models/v_tripmine.mdl"},
		{"models/p_satchel_radio.mdl", "models/v_satchel_radio.mdl"},
		{"models/p_satchel.mdl", "models/v_satchel.mdl"},
		{NULL, NULL}};

	struct model_s* weaponModel = IEngineStudio.GetModelByIndex(weaponindex);

	if (weaponModel)
	{
		int len = strlen(weaponModel->name);
		int i = 0;

		while (modelmap[i] != NULL)
		{
			if (!strnicmp(weaponModel->name, modelmap[i][0], len))
			{
				return gEngfuncs.pEventAPI->EV_FindModelIndex(modelmap[i][1]);
			}
			i++;
		}

		return 0;
	}
	else
		return 0;
}


/*
==================
view::CalcSpectatorRefdef

==================
*/
void view::CalcSpectatorRefdef(struct ref_params_s* pparams)
{
	static Vector velocity(0.0f, 0.0f, 0.0f);

	static int lastWeaponModelIndex = 0;
	static int lastViewModelIndex = 0;

	cl_entity_t* ent = gEngfuncs.GetEntityByIndex(g_iUser2);

	pparams->onlyClientDraw = 0;

	// refresh position
	VectorCopy(pparams->simorg, v_sim_org);

	// get old values
	VectorCopy(pparams->cl_viewangles, v_cl_angles);
	VectorCopy(pparams->viewangles, v_angles);
	VectorCopy(pparams->vieworg, v_origin);

	if ((g_iUser1 == OBS_IN_EYE || gHUD.m_Spectator.m_pip->value == INSET_IN_EYE) && ent)
	{
		// calculate player velocity
		float timeDiff = ent->curstate.msg_time - ent->prevstate.msg_time;

		if (timeDiff > 0)
		{
			Vector distance;
			VectorSubtract(ent->prevstate.origin, ent->curstate.origin, distance);
			VectorScale(distance, 1 / timeDiff, distance);

			velocity[0] = velocity[0] * 0.9f + distance[0] * 0.1f;
			velocity[1] = velocity[1] * 0.9f + distance[1] * 0.1f;
			velocity[2] = velocity[2] * 0.9f + distance[2] * 0.1f;

			VectorCopy(velocity, pparams->simvel);
		}

		// predict missing client data and set weapon model ( in HLTV mode or inset in eye mode )
		if (0 != gEngfuncs.IsSpectateOnly())
		{
			GetInEyePos(g_iUser2, pparams->simorg, pparams->cl_viewangles);

			pparams->health = 1;

			cl_entity_t* gunModel = gEngfuncs.GetViewModel();

			if (lastWeaponModelIndex != ent->curstate.weaponmodel)
			{
				// weapon model changed

				lastWeaponModelIndex = ent->curstate.weaponmodel;
				lastViewModelIndex = FindViewModelByWeaponModel(lastWeaponModelIndex);
				if (0 != lastViewModelIndex)
				{
					gEngfuncs.pfnWeaponAnim(0, 0); // reset weapon animation
				}
				else
				{
					// model not found
					gunModel->model = NULL; // disable weapon model
					lastWeaponModelIndex = lastViewModelIndex = 0;
				}
			}

			if (0 != lastViewModelIndex)
			{
				gunModel->model = IEngineStudio.GetModelByIndex(lastViewModelIndex);
				gunModel->curstate.modelindex = lastViewModelIndex;
				gunModel->curstate.frame = 0;
				gunModel->curstate.colormap = 0;
				gunModel->index = g_iUser2;
			}
			else
			{
				gunModel->model = NULL; // disable weaopn model
			}
		}
		else
		{
			// only get viewangles from entity
			VectorCopy(ent->angles, pparams->cl_viewangles);
			pparams->cl_viewangles[PITCH] *= -3.0f; // see CL_ProcessEntityUpdate()
		}
	}

	v_frametime = pparams->frametime;

	if (pparams->nextView == 0)
	{
		// first renderer cycle, full screen

		switch (g_iUser1)
		{
		case OBS_CHASE_LOCKED:
			GetChasePos(g_iUser2, NULL, v_origin, v_angles);
			break;

		case OBS_CHASE_FREE:
			GetChasePos(g_iUser2, v_cl_angles, v_origin, v_angles);
			break;

		case OBS_ROAMING:
			VectorCopy(v_cl_angles, v_angles);
			VectorCopy(v_sim_org, v_origin);

			// override values if director is active
			gHUD.m_Spectator.GetDirectorCamera(v_origin, v_angles);
			break;

		case OBS_IN_EYE:
			CalcNormalRefdef(pparams);
			break;

		case OBS_MAP_FREE:
			pparams->onlyClientDraw = 1;
			GetMapFreePosition(v_cl_angles, v_origin, v_angles);
			break;

		case OBS_MAP_CHASE:
			pparams->onlyClientDraw = 1;
			GetMapChasePosition(g_iUser2, v_cl_angles, v_origin, v_angles);
			break;
		}

		if (0 != gHUD.m_Spectator.m_pip->value)
			pparams->nextView = 1; // force a second renderer view

		gHUD.m_Spectator.m_iDrawCycle = 0;
	}
	else
	{
		// second renderer cycle, inset window

		// set inset parameters
		pparams->viewport[0] = XRES(gHUD.m_Spectator.m_OverviewData.insetWindowX); // change viewport to inset window
		pparams->viewport[1] = YRES(gHUD.m_Spectator.m_OverviewData.insetWindowY);
		pparams->viewport[2] = XRES(gHUD.m_Spectator.m_OverviewData.insetWindowWidth);
		pparams->viewport[3] = YRES(gHUD.m_Spectator.m_OverviewData.insetWindowHeight);
		pparams->nextView = 0; // on further view

		// override some settings in certain modes
		switch ((int)gHUD.m_Spectator.m_pip->value)
		{
		case INSET_CHASE_FREE:
			GetChasePos(g_iUser2, v_cl_angles, v_origin, v_angles);
			break;

		case INSET_IN_EYE:
			CalcNormalRefdef(pparams);
			break;

		case INSET_MAP_FREE:
			pparams->onlyClientDraw = 1;
			GetMapFreePosition(v_cl_angles, v_origin, v_angles);
			break;

		case INSET_MAP_CHASE:
			pparams->onlyClientDraw = 1;

			if (g_iUser1 == OBS_ROAMING)
				GetMapChasePosition(0, v_cl_angles, v_origin, v_angles);
			else
				GetMapChasePosition(g_iUser2, v_cl_angles, v_origin, v_angles);

			break;
		}

		gHUD.m_Spectator.m_iDrawCycle = 1;
	}

	// write back new values into pparams
	VectorCopy(v_cl_angles, pparams->cl_viewangles);
	VectorCopy(v_angles, pparams->viewangles)
	VectorCopy(v_origin, pparams->vieworg);
}


void view::CalcRefdef(struct ref_params_s* pparams)
{
	//	RecClCalcRefdef(pparams);

	// intermission / finale rendering
	if (0 != pparams->intermission)
	{
		CalcIntermissionRefdef(pparams);
	}
	else if (0 != pparams->spectator || 0 != g_iUser1) // g_iUser true if in spectator mode
	{
		CalcSpectatorRefdef(pparams);
	}
	else if (0 == pparams->paused)
	{
		CalcNormalRefdef(pparams);
	}

	nlutils::CalcRefDef(pparams);
}

void DLLEXPORT V_CalcRefdef(struct ref_params_s* pparams)
{
	view::CalcRefdef(pparams);
}

/*
=============
view::DropPunchAngle

=============
*/
void view::CalcPunchAngle(float frametime, float* ev_punchangle, float *punch)
{
	float damping;
	float springForceMagnitude;

	constexpr auto PUNCH_DAMPING = 10.0f;		  // bigger number makes the response more damped, smaller is less damped
		// currently the system will overshoot, with larger damping values it won't
	constexpr auto PUNCH_SPRING_CONSTANT = 75.0f; // bigger number increases the speed at which the view corrects


	if (Length(ev_punchangle) > 0.001 || Length(punch) > 0.001)
	{
		VectorMA(ev_punchangle, frametime, punch, ev_punchangle);
		damping = 1 - (PUNCH_DAMPING * frametime);

		if (damping < 0)
		{
			damping = 0;
		}
		VectorScale(punch, damping, punch);

		// torsional spring
		// UNDONE: Per-axis spring constant?
		springForceMagnitude = PUNCH_SPRING_CONSTANT * frametime;
		springForceMagnitude = nlutils::clamp(springForceMagnitude, 0.0f, 2.0f);
		VectorMA(punch, -springForceMagnitude, ev_punchangle, punch);

		// don't wrap around
		ev_punchangle[0] = nlutils::clamp(ev_punchangle[0], -15.0f, 15.0f);
		ev_punchangle[1] = nlutils::clamp(ev_punchangle[1], -179.0f, 179.0f);
		ev_punchangle[2] = nlutils::clamp(ev_punchangle[2], -15.0f, 15.0f);
	}
}

/*
=============
view::PunchAxis

Client side punch effect
=============
*/
void view::PunchAxis(int axis, float punch)
{
	ev_punch[axis] = punch * 20;
}

/*
=============
view::Init
=============
*/
void view::Init()
{
	gEngfuncs.pfnAddCommand("centerview", StartPitchDrift);

	scr_ofsx = gEngfuncs.pfnRegisterVariable("scr_ofsx", "0", 0);
	scr_ofsy = gEngfuncs.pfnRegisterVariable("scr_ofsy", "0", 0);
	scr_ofsz = gEngfuncs.pfnRegisterVariable("scr_ofsz", "0", 0);

	v_centermove = gEngfuncs.pfnRegisterVariable("v_centermove", "0.15", 0);
	v_centerspeed = gEngfuncs.pfnRegisterVariable("v_centerspeed", "500", 0);

	cl_bobcycle = gEngfuncs.pfnRegisterVariable("cl_bobcycle", "0.8", 0); // best default for my experimental gun wag (sjb)
	cl_bob = gEngfuncs.pfnRegisterVariable("cl_bob", "0.01", 0);		  // best default for my experimental gun wag (sjb)
	cl_bobup = gEngfuncs.pfnRegisterVariable("cl_bobup", "0.5", 0);
	cl_waterdist = gEngfuncs.pfnRegisterVariable("cl_waterdist", "4", 0);
	cl_chasedist = gEngfuncs.pfnRegisterVariable("cl_chasedist", "112", 0);
}


//#define TRACE_TEST
#if defined(TRACE_TEST)

extern float in_fov;
/*
====================
CalcFov
====================
*/
float CalcFov(float fov_x, float width, float height)
{
	float a;
	float x;

	if (fov_x < 1 || fov_x > 179)
		fov_x = 90; // error, set to 90

	x = width / tan(fov_x / 360 * M_PI);

	a = atan(height / x);

	a = a * 360 / M_PI;

	return a;
}

int hitent = -1;

void V_Move(int mx, int my)
{
	float fov;
	float fx, fy;
	float dx, dy;
	float c_x, c_y;
	float dX, dY;
	Vector forward, up, right;
	Vector newangles;

	Vector farpoint;
	pmtrace_t tr;

	fov = CalcFov(in_fov, (float)ScreenWidth, (float)ScreenHeight);

	c_x = (float)ScreenWidth / 2.0;
	c_y = (float)ScreenHeight / 2.0;

	dx = (float)mx - c_x;
	dy = (float)my - c_y;

	// Proportion we moved in each direction
	fx = dx / c_x;
	fy = dy / c_y;

	dX = fx * in_fov / 2.0;
	dY = fy * fov / 2.0;

	newangles = v_angles;

	newangles[YAW] -= dX;
	newangles[PITCH] += dY;

	// Now rotate v_forward around that point
	AngleVectors(newangles, forward, right, up);

	farpoint = v_origin + 8192 * forward;

	// Trace
	tr = *(gEngfuncs.PM_TraceLine((float*)&v_origin, (float*)&farpoint, PM_TRACELINE_PHYSENTSONLY, 2 /*point sized hull*/, -1));

	if (tr.fraction != 1.0 && tr.ent != 0)
	{
		hitent = PM_GetPhysEntInfo(tr.ent);
		PM_ParticleLine((float*)&v_origin, (float*)&tr.endpos, 5, 1.0, 0.0);
	}
	else
	{
		hitent = -1;
	}
}

#endif
