/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#include "RHIFactory.h"
#include "System/Log/ILog.h"

// Backend includes
#include "OpenGL/GLDevice.h"
// #include "Metal/MetalDevice.h"  // Tier 4

namespace RHI {

std::unique_ptr<IRHIDevice> CreateDevice(Backend backend) {
	switch (backend) {
		case Backend::OpenGL:
			LOG("[RHI] Creating OpenGL device");
			return std::make_unique<GLDevice>();

		case Backend::Metal:
#ifdef __APPLE__
			// Metal backend will be implemented in Tier 4
			LOG_L(L_ERROR, "[RHI] Metal backend not yet implemented");
			return nullptr;
#else
			LOG_L(L_ERROR, "[RHI] Metal backend not available on this platform");
			return nullptr;
#endif
	}

	return nullptr;
}

Backend GetDefaultBackend() {
#if defined(__APPLE__) && defined(__aarch64__)
	// Metal is preferred on Apple Silicon, but not yet implemented
	// return Backend::Metal;
	return Backend::OpenGL;
#else
	return Backend::OpenGL;
#endif
}

bool IsBackendAvailable(Backend backend) {
	switch (backend) {
		case Backend::OpenGL:
			return true;
		case Backend::Metal:
#ifdef __APPLE__
			return false; // will be true after Tier 4
#else
			return false;
#endif
	}
	return false;
}

} // namespace RHI
