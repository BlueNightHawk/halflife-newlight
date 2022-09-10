// ========================================================
// BLUENIGHTHAWK 2022
//
// Mini-particle system
// To be turned into a full particle system in the future
// Work In Progress
// ========================================================

#include <memory.h>

#include "hud.h"
#include "cl_util.h"
#include "const.h"
#include "entity_types.h"
#include "studio_event.h" // def. of mstudioevent_t
#include "r_efx.h"
#include "event_api.h"
#include "pm_defs.h"
#include "pmtrace.h"
#include "pm_shared.h"
#include "Exports.h"

#include "particleman.h"
#include "com_model.h"
#include "r_studioint.h"

extern engine_studio_api_s IEngineStudio;

extern IParticleMan* g_pParticleMan;

void CParticleSystem::Update()
{
	m_flTime = gEngfuncs.GetClientTime();
	m_flTimeDelta = V_max(m_flTime - m_flOldTime, 0);

	UpdateGunParticles();

	m_flOldTime = m_flTime;
}

void CParticleSystem::UpdateGunParticles()
{
	view = nlutils::GetViewModel();
	AngleVectors(nlutils::VectorInvertPitch(view->angles), forward, right, up);

	if (m_pCachedmodel != view->model)
	{
		// Reset all values
		m_flSmokeCreateDelay = m_flSmokeTime = m_flDelayBeforeSmoke = 0.0f;
		m_iGunAttachment = 0;
		m_iGunParticles = 0;
	}

	if ((m_iGunParticles & GUNPARTICLES_GUNSMOKETRAIL) != 0)
	{
		SmokeTrail();
	}
	if ((m_iGunParticles & GUNPARTICLES_GUNSMOKE) != 0)
	{
		StartMuzzleSmoke();
	}

	m_pCachedmodel = view->model;
}

CBaseParticle *CParticleSystem::CreateParticle(const int particle, particleinfo_t *info)
{
	if (!info || !*info)
		return nullptr;

	CBaseParticle *pParticle = g_pParticleMan->CreateParticle(info->origin, g_vecZero, info->model, info->scale, info->renderamt / 255.0f, info->classname);

	if (pParticle)
	{
		pParticle->m_vAngles = info->angles;
		pParticle->m_vVelocity = info->velocity;
		pParticle->m_vAVelocity = info->avelocity;
		pParticle->m_iRendermode = info->rendermode;
		pParticle->m_flBrightness = info->renderamt;
		pParticle->m_vColor = info->rendercolor;
		pParticle->m_flDieTime = info->die;
		pParticle->m_iFrame = info->frame;
		pParticle->m_iNumFrames = info->maxframes;
		pParticle->m_iFramerate = info->framerate;
		pParticle->m_flGravity = info->gravity;
		pParticle->SetCollisionFlags(info->particleflags);
		pParticle->SetLightFlag(info->lightflags);
		pParticle->SetCullFlag(info->cullflags);
		pParticle->SetRenderFlag(info->renderflags);
	}
	return pParticle;
}

void CParticleSystem::CreateGunParticle(const int particle, int idx)
{
	m_iGunAttachment = idx;
	m_iGunParticles |= particle;
}

void CParticleSystem::SmokeTrail()
{
	if (m_flDelayBeforeSmoke > m_flTime || m_flSmokeCreateDelay > m_flTime)
		return;

	if (m_flSmokeTime < m_flTime)
	{
		m_iGunParticles &= ~GUNPARTICLES_GUNSMOKETRAIL;
		return;
	}

	float alphamultiplier = (m_flSmokeTime - m_flTime);

	particleinfo_t prtcl("sprites/stmbal1.spr");
	if (prtcl)
	{
		prtcl.origin = view->attachment[m_iGunAttachment];
		prtcl.classname = "";
		prtcl.velocity = Vector(nlutils::ViewParams.simvel) + up * 3 + Vector(gEngfuncs.pfnRandomFloat(-0.40, 0.40), gEngfuncs.pfnRandomFloat(-0.40, 0.40), 
			gEngfuncs.pfnRandomFloat(-0.40, 0.40));
		prtcl.particleflags = TRI_ANIMATEDIE;
		prtcl.lightflags = LIGHT_NONE;
		prtcl.cullflags = 0;
		prtcl.renderflags = RENDER_FACEPLAYER;
		prtcl.rendermode = kRenderTransAdd;
		prtcl.rendercolor = Vector(255, 255, 255);
		prtcl.renderamt = (0.01f * alphamultiplier) * 255;
		prtcl.scale = gEngfuncs.pfnRandomFloat(0.05, 0.08) * 25.0f;
		prtcl.die = m_flTime + 5.5f;
		prtcl.gravity = 0.0f;
		prtcl.framerate = 5;

		CreateParticle(0, &prtcl);
	}

	m_flSmokeCreateDelay = m_flTime + 0.035f;
}
void CParticleSystem::StartSmokeTrail(int idx, float flDelayBeforeSmoke, float flSmokeTime)
{
	m_iGunParticles |= GUNPARTICLES_GUNSMOKETRAIL | GUNPARTICLES_GUNSMOKE;
	m_iGunAttachment = idx;
	m_flDelayBeforeSmoke = flDelayBeforeSmoke;
	m_flSmokeTime = flSmokeTime;

	gEngfuncs.pEfxAPI->R_SparkShower(view->attachment[idx]);

	// Sparks
	particleinfo_t prtcl("sprites/smallspark.spr");

	if (!prtcl)
		return;

	prtcl.origin = view->attachment[m_iGunAttachment];
	prtcl.classname = "";
	prtcl.particleflags = TRI_COLLIDEKILL;
	prtcl.lightflags = LIGHT_NONE;
	prtcl.cullflags = 0;
	prtcl.renderflags = RENDER_FACEPLAYER;
	prtcl.rendermode = kRenderTransAdd;
	prtcl.rendercolor = Vector(255, 128, 128);
	prtcl.renderamt = 150;
	prtcl.scale = 1.0f;
	prtcl.die = m_flTime + 5.0f;
	prtcl.gravity = 1.1f;

	for (int i = 0; i < gEngfuncs.pfnRandomLong(4, 8); i++)
	{
		float randX, randY, randZ;
		Vector velocity;
		randX = gEngfuncs.pfnRandomFloat(-60, 60);
		randY = gEngfuncs.pfnRandomFloat(-60, 60);
		randZ = gEngfuncs.pfnRandomFloat(80, 125);

		velocity = Vector(randX, randY, randZ);

		prtcl.velocity = velocity;

		CreateParticle(0, &prtcl);
	}
}

void CParticleSystem::StartMuzzleSmoke()
{
	particleinfo_t prtcl("sprites/stmbal1.spr");

	if (prtcl)
	{
		prtcl.origin = view->attachment[m_iGunAttachment];
		prtcl.classname = "";
		prtcl.velocity = g_vecZero;
		prtcl.particleflags = TRI_ANIMATEDIE;
		prtcl.lightflags = LIGHT_NONE;
		prtcl.cullflags = 0;
		prtcl.renderflags = RENDER_FACEPLAYER;
		prtcl.rendermode = kRenderTransAdd;
		prtcl.rendercolor = Vector(255, 255, 255);
		prtcl.renderamt = gEngfuncs.pfnRandomFloat(0.05, 0.085) * 255;
		prtcl.scale = 15.0f;
		prtcl.die = m_flTime + 1.5f;
		prtcl.gravity = 0.0f;
		prtcl.framerate = 30;

		CreateParticle(0, &prtcl);
	}

	m_iGunParticles &= ~GUNPARTICLES_GUNSMOKE;
}

void particleinfo_s::InitSprite(const char* sprite)
{
	model_s* pmodel = IEngineStudio.Mod_ForName(sprite, 1);
	if (!pmodel)
		return;

	maxframes = pmodel->numframes;

	if (maxframes <= 0)
		maxframes = 1;

	this->model = pmodel;
}