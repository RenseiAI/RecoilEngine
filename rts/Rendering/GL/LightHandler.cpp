/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

/**
 * GL Light Handler - Implementation
 *
 * RHI Migration Status: MIGRATED (Phase 5.7)
 * See LightHandler.h for full migration notes.
 *
 * Light data is packed into float4 arrays and uploaded as shader uniforms
 * each frame. No FFP glLight* calls remain.
 */

#include "LightHandler.h"
#include "Game/GlobalUnsynced.h"
#include "Rendering/Shaders/Shader.h"
#include "Sim/Misc/GlobalSynced.h"
#include "Sim/Misc/LosHandler.h"
#include "Sim/Projectiles/Projectile.h"
#include "System/MathConstants.h"

#include "System/Misc/TracyDefs.h"

#include <cmath>

//automatically initialized to zeros
static constexpr float4 ZeroVector4;

void GL::LightHandler::Init(unsigned int cfgMaxLights) {
	maxLights = cfgMaxLights;

	lights.resize(maxLights);

	// Resize uniform data arrays
	lightPositions.resize(maxLights, ZeroVector4);
	lightAmbients.resize(maxLights, ZeroVector4);
	lightDiffuses.resize(maxLights, ZeroVector4);
	lightSpeculars.resize(maxLights, ZeroVector4);
	lightSpotParams.resize(maxLights, ZeroVector4);
	lightAttenuations.resize(maxLights, ZeroVector4);

	for (unsigned int i = 0; i < maxLights; i++) {
		lights[i].SetID(i);
	}
}


unsigned int GL::LightHandler::AddLight(const GL::Light& light) {
	RECOIL_DETAILED_TRACY_ZONE;
	if (light.GetTTL() == 0 || light.GetRadius() <= 0.0f)
		return -1u;
	if ((light.GetIntensityWeight()).SqLength() <= 0.01f)
		return -1u;

	const auto it = std::find_if(lights.begin(), lights.end(), [&](const GL::Light& lgt) { return (lgt.GetTTL() == 0); });

	if (it == lights.end()) {
		// all are claimed; find the lowest-priority light we can evict
		unsigned int minPriorityValue = light.GetPriority();
		unsigned int minPriorityIndex = -1u;

		for (unsigned int n = 0; n < lights.size(); n++) {
			const GL::Light& lgt = lights[n];

			if (lgt.GetPriority() < minPriorityValue) {
				minPriorityValue = lgt.GetPriority();
				minPriorityIndex = n;
			}
		}

		if (minPriorityIndex != -1u)
			return (SetLight(minPriorityIndex, light));

		// no available light to replace
		return -1u;
	}

	return (SetLight(it - lights.begin(), light));
}

unsigned int GL::LightHandler::SetLight(unsigned int lgtIndex, const GL::Light& light) {
	RECOIL_DETAILED_TRACY_ZONE;
	const unsigned int lightID = lights[lgtIndex].GetID();

	// clear any previous dependence this light might have
	lights[lgtIndex].ClearDeathDependencies();

	lights[lgtIndex] = light;
	lights[lgtIndex].SetID(lightID);
	lights[lgtIndex].SetUID(lightHandle++);
	lights[lgtIndex].SetRelativeTime(0);
	lights[lgtIndex].SetAbsoluteTime(gs->frameNum);

	return (lights[lgtIndex].GetUID());
}

GL::Light* GL::LightHandler::GetLight(unsigned int lgtHandle) {
	RECOIL_DETAILED_TRACY_ZONE;
	const auto it = std::find_if(lights.begin(), lights.end(), [&](const GL::Light& lgt) { return (lgt.GetUID() == lgtHandle); });

	if (it != lights.end())
		return &(*it);

	return nullptr;
}


void GL::LightHandler::Update(Shader::IProgramObject* shader) {
	RECOIL_DETAILED_TRACY_ZONE;
	if (lights.size() != numLights) {
		numLights = lights.size();
	}

	if (numLights == 0)
		return;

	// float3 sumWeight;
	float3 maxWeight = OnesVector * 0.01f;

	for (const GL::Light& light: lights) {
		if (light.GetTTL() == 0)
			continue;

		// sumWeight += light.GetIntensityWeight();
		maxWeight = float3::max(maxWeight, light.GetIntensityWeight());
	}

	for (unsigned int i = 0; i < numLights; i++) {
		GL::Light& light = lights[i];

		// dead light, zero its contribution
		if (light.GetTTL() == 0) {
			lightPositions[i]   = ZeroVector4;
			lightAmbients[i]    = ZeroVector4;
			lightDiffuses[i]    = ZeroVector4;
			lightSpeculars[i]   = ZeroVector4;
			lightSpotParams[i]  = ZeroVector4;
			lightAttenuations[i] = ZeroVector4;
			continue;
		}

		if (light.GetAbsoluteTime() != gs->frameNum) {
			light.SetRelativeTime(light.GetRelativeTime() + 1);
			light.SetAbsoluteTime(gs->frameNum);
			light.DecayColors();
			light.ClampColors();
		}

		// rescale by max (not sum!), otherwise 1) the intensity would
		// change if any light is added or removed when all have equal
		// weight and 2) a non-uniform set would cause a reduction for
		// all
		const float3 weight              = light.GetIntensityWeight() / maxWeight;
		const float4 weightedAmbientCol  = light.GetAmbientColor()  * weight.x;
		const float4 weightedDiffuseCol  = light.GetDiffuseColor()  * weight.y;
		const float4 weightedSpecularCol = light.GetSpecularColor() * weight.z;

		float4 lightPos = light.GetPosition();
		float4 lightDir = light.GetDirection(); // w=0, make sure to pick mat::oper*(float4)

		if (light.GetTrackObject() != nullptr) {
			switch (light.GetTrackType()) {
				case GL::Light::TRACK_TYPE_UNIT: {
					const CSolidObject* so = static_cast<const CSolidObject*>(light.GetTrackObject());

					if (light.LocalSpace()) {
						lightPos = so->GetObjectSpaceDrawPos(lightPos);
						lightDir = so->GetObjectSpaceVec(lightDir);
					} else {
						lightPos = so->drawPos;
						lightDir = so->frontdir;
					}
				} break;
				case GL::Light::TRACK_TYPE_PROJ: {
					const CProjectile* po = static_cast<const CProjectile*>(light.GetTrackObject());

					if (light.LocalSpace()) {
						const CMatrix44f m = po->GetTransformMatrix(false);

						lightPos = m * lightPos;
						lightDir = m * lightDir;
					} else {
						lightPos = po->drawPos;
						lightDir = po->dir;
					}
				} break;
				default: {} break;
			}
		}

		if (light.GetRelativeTime() > light.GetTTL()) {
			// mark light as dead, zero its contribution
			light.SetTTL(0);
			lightPositions[i]   = ZeroVector4;
			lightAmbients[i]    = ZeroVector4;
			lightDiffuses[i]    = ZeroVector4;
			lightSpeculars[i]   = ZeroVector4;
			lightSpotParams[i]  = ZeroVector4;
			lightAttenuations[i] = ZeroVector4;
			continue;
		}

		// Pack light data into uniform arrays
		lightPositions[i] = lightPos;

		if (gu->spectatingFullView || light.IgnoreLOS() || losHandler->InLos(lightPos, gu->myAllyTeam)) {
			// light is visible
			lightAmbients[i]  = weightedAmbientCol;
			lightDiffuses[i]  = weightedDiffuseCol;
			lightSpeculars[i] = weightedSpecularCol;
		} else {
			// zero contribution from this light if not in LOS
			lightAmbients[i]  = ZeroVector4;
			lightDiffuses[i]  = ZeroVector4;
			lightSpeculars[i] = ZeroVector4;
		}

		// spotDirection (xyz) + cos(fov) as spotCosCutoff (w)
		lightSpotParams[i] = float4(lightDir.x, lightDir.y, lightDir.z,
			std::cos(light.GetFOV() * math::DEG_TO_RAD));

		// Pack attenuation: x=radius (or constAtten in OGL_SPEC), y=linear, z=quad
		lightAttenuations[i] = float4(light.GetRadius(), 0.0f, 0.0f, 0.0f);
		#if (OGL_SPEC_ATTENUATION == 1)
		lightAttenuations[i] = float4(
			light.GetAttenuation().x,
			light.GetAttenuation().y,
			light.GetAttenuation().z,
			0.0f);
		#endif
	}

	// Upload all 6 uniform arrays to the active shader
	shader->SetUniform4v("dynLightPosition",    static_cast<int>(maxLights), &lightPositions[0].x);
	shader->SetUniform4v("dynLightAmbient",     static_cast<int>(maxLights), &lightAmbients[0].x);
	shader->SetUniform4v("dynLightDiffuse",     static_cast<int>(maxLights), &lightDiffuses[0].x);
	shader->SetUniform4v("dynLightSpecular",    static_cast<int>(maxLights), &lightSpeculars[0].x);
	shader->SetUniform4v("dynLightSpotParams",  static_cast<int>(maxLights), &lightSpotParams[0].x);
	shader->SetUniform4v("dynLightAttenuation", static_cast<int>(maxLights), &lightAttenuations[0].x);
}
