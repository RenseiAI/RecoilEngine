/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

/**
 * GL Light Handler - Dynamic Light Management
 *
 * RHI Migration Status: MIGRATED (Phase 5.7)
 * -------------------------------------------
 * Light data is packed into float4 arrays and uploaded as shader uniforms
 * each frame via SetUniform4v(). No FFP glLight* calls remain.
 */

#ifndef _GL_LIGHTHANDLER_H
#define _GL_LIGHTHANDLER_H

#include <vector>

#include "Light.h"

namespace Shader {
	struct IProgramObject;
}

namespace GL {
	struct LightHandler {
	public:
		LightHandler(): maxLights(0), numLights(0), lightHandle(0) {}
		~LightHandler() { Kill(); }

		void Init(unsigned int cfgMaxLights);
		void Kill() { lights.clear(); }
		void Update(Shader::IProgramObject*);

		unsigned int AddLight(const GL::Light&);
		unsigned int SetLight(unsigned int lgtIndex, const GL::Light&);

		GL::Light* GetLight(unsigned int lgtHandle);

		unsigned int GetMaxLights() const { return maxLights; }

	private:
		std::vector<GL::Light> lights;

		// Packed arrays for uniform upload (6 x vec4 arrays)
		std::vector<float4> lightPositions;
		std::vector<float4> lightAmbients;
		std::vector<float4> lightDiffuses;
		std::vector<float4> lightSpeculars;
		std::vector<float4> lightSpotParams;    // xyz=dir, w=cosCutoff
		std::vector<float4> lightAttenuations;  // x=radius (or constAtten), y=linear, z=quad

		unsigned int maxLights;
		unsigned int numLights;
		unsigned int lightHandle;
	};
}

#endif // _GL_LIGHTHANDLER_H
