#pragma once

#include "cvardef.h"
#ifdef CLIENT_DLL
#include "nlparticles.h"
#endif
namespace nlutils
{
	// Private members
	inline ref_params_t ViewParams;

	// Utilities
	Vector VectorInvertPitch(const Vector in);
	Vector VectorInvert(const Vector in, bool x = true, bool y = true, bool z = true);

	// Engine Function Wrappers
	void ConPrint(const char* format, ...);
	void ConDPrint(const char* format, ...);

	cvar_s* GetCvarPtr(const char* cv);

	float lerp(float val, float dest, float frametime);
	float clamp(float val, float min, float max);
	int clamp(int val, int min, int max);
#ifdef CLIENT_DLL
	const ref_params_t GetViewParams();
	void VidInit();
	void CalcRefDef(ref_params_t* pparams);

	bool IsThirdPerson();

	cl_entity_s* GetViewModel();

	inline int iCurrentWeapon;
	inline int GetCurrentWeapon()
	{
		return iCurrentWeapon;
	}
#else
	// Server Side Specific
#endif
}

namespace nlcvars
{
	// engine cvars
	inline cvar_t* r_decals;
	inline cvar_t* voice_clientdebug;
	inline cvar_t* voice_modenable;
	inline void InitCvars()
	{
		r_decals = nlutils::GetCvarPtr("r_decals");
		voice_clientdebug = nlutils::GetCvarPtr("voice_clientdebug");
		voice_modenable = nlutils::GetCvarPtr("voice_modenable");
	}
}