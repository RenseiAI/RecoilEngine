/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#include "RHIFactory.h"
#include "System/Log/ILog.h"

// Backend includes
#include "OpenGL/GLDevice.h"
#ifdef __APPLE__
#include "Metal/MTLDevice.h"
#endif

namespace RHI {

std::unique_ptr<IRHIDevice> CreateDevice(Backend backend) {
	switch (backend) {
		case Backend::OpenGL:
			LOG("[RHI] Creating OpenGL device");
			return std::make_unique<GLDevice>();

		case Backend::Metal:
#ifdef __APPLE__
			LOG("[RHI] Creating Metal device");
			return std::make_unique<MTLDevice>();
#else
			LOG_L(L_ERROR, "[RHI] Metal backend not available on this platform");
			return nullptr;
#endif
	}

	return nullptr;
}

Backend GetDefaultBackend() {
#if defined(__APPLE__) && defined(__aarch64__)
	// Metal is preferred on Apple Silicon
	return Backend::Metal;
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
			return true;
#else
			return false;
#endif
	}
	return false;
}

static std::unique_ptr<IRHIDevice> globalDevice;

void InitDevice() {
	if (globalDevice)
		return;
	globalDevice = CreateDevice(GetDefaultBackend());
	if (!globalDevice)
		LOG_L(L_ERROR, "[RHI] Failed to create global device");
	else
		LOG("[RHI] Global device initialized (backend=%s)", globalDevice->GetBackendName());
}

void KillDevice() {
	globalDevice.reset();
}

IRHIDevice* GetDevice() {
	return globalDevice.get();
}

} // namespace RHI
