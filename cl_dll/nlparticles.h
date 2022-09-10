#pragma once

#include "particleman.h"

constexpr auto GUNPARTICLES_GUNSMOKE = 1;
constexpr auto GUNPARTICLES_GUNSMOKETRAIL = 2;

typedef struct particleinfo_s
{
	particleinfo_s()
	{

	}

	particleinfo_s(const char *name)
	{
		InitSprite(name);
	}

	operator bool() { return (model != nullptr); }

	const char* classname;

	Vector origin;
	Vector angles;

	float frame;
	float scale;
	int maxframes;

	// Render information
	int rendermode;
	int renderamt;
	Vector rendercolor;

	float framerate;
	Vector velocity;
	Vector avelocity;

	// Friction, for prediction.
	float friction;
	// Gravity multiplier
	float gravity;

	model_s *model;

	int particleflags;
	int cullflags;
	int lightflags;
	int renderflags;

	float die;

	void InitSprite(const char* sprite);
} particleinfo_t;

class CParticleSystem
{
public:
	void Init();
	void Update();
	void UpdateGunParticles();

	CBaseParticle* CreateParticle(const int particle, particleinfo_t *info);
	void CreateGunParticle(const int particle, int idx);

	// Presets
	void StartSmokeTrail(const int idx, const float flDelayBeforeSmoke, const float flSmokeTime);
	void StartMuzzleSmoke();

private:
	model_s* m_pCachedmodel;
	cl_entity_t* view;
	Vector forward, right, up;
	float m_flTime, m_flOldTime, m_flTimeDelta;
	int m_iGunParticles = 0;
	int m_iGunAttachment = 0;

	// Smoketrail
	void SmokeTrail();
	float m_flDelayBeforeSmoke;
	float m_flSmokeTime;
	float m_flSmokeCreateDelay;
};

inline CParticleSystem particle_system;