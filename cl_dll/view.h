//========= Copyright © 1996-2002, Valve LLC, All rights reserved. ============
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================

#pragma once

#define ORIGIN_BACKUP 64
#define ORIGIN_MASK (ORIGIN_BACKUP - 1)

#define CAM_MODE_RELAX 1
#define CAM_MODE_FOCUS 2

typedef struct
{
	float Origins[ORIGIN_BACKUP][3];
	float OriginTime[ORIGIN_BACKUP];

	float Angles[ORIGIN_BACKUP][3];
	float AngleTime[ORIGIN_BACKUP];

	int CurrentOrigin;
	int CurrentAngle;
} viewinterp_t;

typedef struct pitchdrift_s
{
	float pitchvel;
	int nodrift;
	float driftmove;
	double laststop;
} pitchdrift_t;

enum calcBobMode_t
{
	VB_COS,
	VB_SIN,
	VB_COS2,
	VB_SIN2
};

typedef struct clbob_s
{
	double bobtime;
	float bob;
	float lasttime;
} clbob_t;

inline Vector v_origin, v_angles, v_cl_angles, v_sim_org, v_lastAngles;

namespace view
{
	void CalcBob(struct ref_params_s* pparams, const float freq, const calcBobMode_t mode, clbob_s* curbob);

	float CalcRoll(Vector angles, Vector velocity, float rollangle, float rollspeed);
	void CalcViewRoll(struct ref_params_s* pparams, cl_entity_s *view);

	void StartPitchDrift();
	void StopPitchDrift();
	void DriftPitch(struct ref_params_s* pparams);

	void CalcGunAngle(struct ref_params_s* pparams);

	void AddIdle(struct ref_params_s* pparams);

	void CalcIntermissionRefdef(struct ref_params_s* pparams);
	void CalcViewModelLag(ref_params_t* pparams, Vector& origin, const Vector angles, const Vector original_angles);
	void CalcNormalRefdef(struct ref_params_s* pparams);

	void SmoothInterpolateAngles(float* startAngle, float* endAngle, float* finalAngle, float degreesPerSec);

	void GetChaseOrigin(float* angles, float* origin, float distance, float* returnvec);
	void GetSingleTargetCam(cl_entity_t* ent1, float* angle, float* origin);
	float MaxAngleBetweenAngles(float* a1, float* a2);

	void GetDoubleTargetsCam(cl_entity_t* ent1, cl_entity_t* ent2, float* angle, float* origin);

	void GetDirectedChasePosition(cl_entity_t* ent1, cl_entity_t* ent2, float* angle, float* origin);
	void GetChasePos(int target, float* cl_angles, float* origin, float* angles);
	void ResetChaseCam();
	void GetInEyePos(int target, float* origin, float* angles);
	void GetMapFreePosition(float* cl_angles, float* origin, float* angles);
	void GetMapChasePosition(int target, float* cl_angles, float* origin, float* angles);
	int FindViewModelByWeaponModel(int weaponindex);

	void CalcSpectatorRefdef(struct ref_params_s* pparams);
	void CalcRefdef(struct ref_params_s* pparams);
	void CalcPunchAngle(float frametime, float* ev_punchangle, float* punch);
	void PunchAxis(int axis, float punch);
	void Init();

	void UpdateJumpState(struct ref_params_s* pparams, cl_entity_s* view);
	void SetJumpState(int state);
	int GetJumpState();

	void RetractWeapon(struct ref_params_s* pparams, cl_entity_s* view);
	}
