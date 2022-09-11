#ifdef CLIENT_DLL
#include "hud.h"
#else
#include "enginecallback.h"
#endif

#include "Exports.h"

#include <algorithm>
#include <cmath>
#include "nlutils.h"
#include "event_api.h"
#include "r_efx.h"

// Utilities
Vector nlutils::VectorInvertPitch(const Vector in)
{
	return Vector(-in[0], in[1], in[2]);
}

Vector nlutils::VectorInvert(const Vector in, bool x, bool y, bool z)
{
	return Vector(x ? -in[0] : in[0], y ? -in[1] : in[1], z ? -in[2] : in[2]);
}
// Engine Function Wrappers

void nlutils::ConPrint(const char* format, ...)
{
	va_list argptr;
	static char string[1024];

	va_start(argptr, format);
	vsprintf(string, format, argptr);
	va_end(argptr);

#ifdef CLIENT_DLL
	gEngfuncs.Con_Printf(string);
#else
	g_engfuncs.pfnServerPrint(string);
#endif
}

void nlutils::ConDPrint(const char* format, ...)
{
	va_list argptr;
	static char string[1024];

	va_start(argptr, format);
	vsprintf(string, format, argptr);
	va_end(argptr);

#ifdef CLIENT_DLL
	gEngfuncs.Con_DPrintf(string);
#else
	g_engfuncs.pfnServerPrint(string);
#endif
}

cvar_s* nlutils::GetCvarPtr(const char* cv)
{
#ifdef CLIENT_DLL
	return gEngfuncs.pfnGetCvarPointer(cv);
#else
	return g_engfuncs.pfnCVarGetPointer(cv);
#endif
}

float nlutils::lerp(float val, float dest, float frametime)
{
	return std::lerp(val, dest, frametime);
}

float nlutils::clamp(float val, float min, float max)
{
	return std::clamp(val, min, max);
}

int nlutils::clamp(int val, int min, int max)
{
	return std::clamp(val, min, max);
}


#ifdef CLIENT_DLL
// Client Side Specific
extern void UpdateBeams(ref_params_t* pparams);
extern void UpdateLaserSpot(ref_params_t* pparams);
extern void UpdateFlashlight(ref_params_t* pparams);

extern BEAM* pBeam;
extern BEAM* pBeam2;
extern TEMPENTITY* pFlare; // Vit_amiN: egon's energy flare

extern TEMPENTITY* pLaserSpot;

const ref_params_t nlutils::GetViewParams()
{
	return ViewParams;
}

void nlutils::VidInit()
{
	pBeam = pBeam2 = nullptr;

	pFlare = pLaserSpot = nullptr;
}

void nlutils::CalcRefDef(ref_params_t* pparams)
{
	model_s* pmodel = GetViewModel()->model;
	static model_s* prevmodel = nullptr;

	ViewParams = *pparams;

	UpdateBeams(pparams);
	UpdateLaserSpot(pparams);
	UpdateFlashlight(pparams);
}

bool nlutils::IsThirdPerson()
{
	return (CL_IsThirdPerson() > 0);
}

bool nlutils::IsLocal(int idx)
{
	return gEngfuncs.GetLocalPlayer()->index == idx;
}

cl_entity_s* nlutils::GetViewModel()
{
	return gEngfuncs.GetViewModel();
}


#else
// Server Side Specific
#endif