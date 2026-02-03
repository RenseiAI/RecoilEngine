/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#ifndef RHI_FACTORY_H
#define RHI_FACTORY_H

/**
 * RHI Factory
 *
 * Creates the appropriate backend device based on the requested backend type.
 * On macOS ARM64, both OpenGL and Metal are available.
 * On Linux/Windows, only OpenGL is available.
 *
 * Usage:
 *   auto device = RHI::CreateDevice(RHI::Backend::OpenGL);
 *   auto buffer = device->CreateBuffer(RHI::BufferType::Vertex, ...);
 */

#include <memory>
#include "RHITypes.h"
#include "RHIDevice.h"

namespace RHI {

/// Create a device for the specified backend.
/// Returns nullptr if the backend is not available on this platform.
std::unique_ptr<IRHIDevice> CreateDevice(Backend backend);

/// Get the default backend for the current platform.
/// Returns Metal on macOS ARM64, OpenGL elsewhere.
Backend GetDefaultBackend();

/// Query whether a backend is available on this platform.
bool IsBackendAvailable(Backend backend);

} // namespace RHI

#endif // RHI_FACTORY_H
