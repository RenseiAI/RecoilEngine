/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

/**
 * RHI Migration Status: COMPLETE
 *
 * This file contains the ISky base interface. No RHI draw calls are made here;
 * derived classes (ModernSky, SkyBox) handle rendering.
 *
 * FFP fog (glFog*) removed in Phase 4.4 — fog is now explicit shader uniforms
 * via GetFogUniforms(). SetupFog() kept as empty stub for call-site compatibility.
 */

#include "ISky.h"
#include "NullSky.h"
#include "SkyBox.h"
#include "ModernSky.h"
#include "Game/Camera.h"
#include "Game/TraceRay.h"
#include "Map/MapInfo.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/Env/DebugCubeMapTexture.h"
#include "Rendering/GL/myGL.h"
#include "System/Config/ConfigHandler.h"
#include "System/Exceptions.h"
#include "System/SafeUtil.h"
#include "System/Log/ILog.h"

#include "System/Misc/TracyDefs.h"

CONFIG(bool, AdvSky).deprecated(true);

ISky::ISky()
	: skyColor(mapInfo->atmosphere.skyColor)
	, sunColor(mapInfo->atmosphere.sunColor)
	, cloudColor(mapInfo->atmosphere.cloudColor)
	, fogColor(mapInfo->atmosphere.fogColor)
	, skyAxisAngle(mapInfo->atmosphere.skyAxisAngle)
	, fogStart(mapInfo->atmosphere.fogStart)
	, fogEnd(mapInfo->atmosphere.fogEnd)
	, cloudDensity(mapInfo->atmosphere.cloudDensity)
	, skyLight(nullptr)
	, wireFrameMode(false)
	, updated(true)
{
	skyLight = new ISkyLight();
}

ISky::~ISky()
{
	RECOIL_DETAILED_TRACY_ZONE;
	spring::SafeDelete(skyLight);
}

std::unique_ptr<ISky> ISky::sky = nullptr;



void ISky::SetupFog() {
	RECOIL_DETAILED_TRACY_ZONE;
	// Fog parameters are now set as explicit shader uniforms (Phase 4.4).
	// glFog* calls removed — shaders read fogColor/fogParams uniforms directly.
}

void ISky::GetFogUniforms(float4& outColor, float4& outParams) const {
	outColor = float4(fogColor.x, fogColor.y, fogColor.z, 1.0f);
	const float start = camera->GetFarPlaneDist() * fogStart;
	const float end   = camera->GetFarPlaneDist() * fogEnd;
	outParams = float4(start, end, 0.0f, 1.0f / (end - start));
}

void ISky::SetSky()
{
	RECOIL_DETAILED_TRACY_ZONE;
	sky = nullptr; //break before make

	try {
		if (globalRendering->drawDebugCubeMap) {
			int2 dims = debugCubeMapTexture.GetDimensions();
			sky = std::make_unique<CSkyBox>(debugCubeMapTexture.GetId(), dims.x, dims.y);
		}
		else if (!mapInfo->atmosphere.skyBox.empty()) {
			sky = std::make_unique<CSkyBox>("maps/" + mapInfo->atmosphere.skyBox);
		}
		else {
			sky = std::make_unique<CModernSky>();
		}
	} catch (const content_error& ex) {
		LOG_L(L_ERROR, "[ISky::%s] error: %s (falling back to NullSky)", __func__, ex.what());
		sky = std::make_unique<CNullSky>();
	}

	if (!sky->IsValid()) {
		LOG_L(L_ERROR, "[ISky::%s] error creating %s (falling back to NullSky)", __func__, sky->GetName().c_str());
		sky = std::make_unique<CNullSky>();
	}
}

void ISky::SetSkyAxisAngle(const float4& skyAxisAngleRaw)
{
	auto axis = float3{ skyAxisAngleRaw.x, skyAxisAngleRaw.y, skyAxisAngleRaw.z };
	const float axisNorm = axis.Length();
	if (axisNorm < float3::nrm_eps())
		axis = FwdVector;
	else
		axis /= axisNorm;

	skyAxisAngle = float4{ axis, ClampRad(skyAxisAngleRaw.w) };
}

bool ISky::SunVisible(const float3 pos) const {
	RECOIL_DETAILED_TRACY_ZONE;
	const CUnit* hitUnit = nullptr;
	const CFeature* hitFeature = nullptr;

	// cast a ray *toward* the sun from <pos>
	// sun is visible if no terrain blocks it
	const float3& sunDir = skyLight->GetLightDir();
	const float sunDist = TraceRay::GuiTraceRay(pos, sunDir, camera->GetFarPlaneDist(), nullptr, hitUnit, hitFeature, false, true, false);

	return (sunDist < 0.0f || sunDist >= camera->GetFarPlaneDist());
}
