/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

/**
 * GL Light Handler - Dynamic Light Management
 *
 * RHI Migration Status: REQUIRES ARCHITECTURAL REDESIGN
 * ------------------------------------------------------
 * Uses OpenGL fixed-function pipeline lighting which has NO equivalent
 * in Metal, Vulkan, or GL Core 3.2+.
 *
 * Current GL calls: glGetIntegerv(GL_MAX_LIGHTS), glEnable/glDisable(GL_LIGHT*),
 * glLightfv(POSITION/AMBIENT/DIFFUSE/SPECULAR/SPOT_DIRECTION), glLightf(CUTOFF/ATTENUATION)
 *
 * Migration Strategy (NOT YET IMPLEMENTED):
 *   1. Create uniform buffer with struct GPULightData[] + numActiveLights
 *   2. Upload via IRHIBuffer::Upload() each frame
 *   3. Bind UBO in shaders via IRHIContext::BindUniformBuffer()
 *   4. Sample light data in fragment shaders instead of FFP
 *
 * Until migration: Only functions with OpenGL backend.
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
		LightHandler(): baseLight(0), maxLights(0), numLights(0), lightHandle(0) {}
		~LightHandler() { Kill(); }

		void Init(unsigned int, unsigned int);
		void Kill() { lights.clear(); }
		void Update(Shader::IProgramObject*);

		unsigned int AddLight(const GL::Light&);
		unsigned int SetLight(unsigned int lgtIndex, const GL::Light&);

		GL::Light* GetLight(unsigned int lgtHandle);

		unsigned int GetBaseLight() const { return baseLight; }
		unsigned int GetMaxLights() const { return maxLights; }

	private:
		std::vector<GL::Light> lights;

		unsigned int baseLight;
		unsigned int maxLights;
		unsigned int numLights;
		unsigned int lightHandle;
	};
}

#endif // _GL_LIGHTHANDLER_H
