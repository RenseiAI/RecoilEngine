/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#ifndef RHI_DEVICE_H
#define RHI_DEVICE_H

/**
 * RHI Device Interface
 *
 * Maps CGlobalRendering capability queries and resource creation:
 *   globalRendering->haveGL4                    ->  IRHIDevice::HaveGL4()
 *   globalRendering->supportPersistentMapping   ->  IRHIDevice::SupportPersistentMapping()
 *   new VBO() / FBO() / GLSLProgramObject()     ->  IRHIDevice::Create*()
 *
 * The device is the factory for all GPU resources.
 */

#include <memory>
#include <string>
#include "RHITypes.h"

namespace RHI {

class IRHIBuffer;
class IRHITexture;
class IRHIShader;
class IRHIFramebuffer;
class IRHIPipeline;
class IRHIContext;

class IRHIDevice {
public:
	virtual ~IRHIDevice() = default;

	// --- Backend identification ---
	virtual Backend GetBackend() const = 0;
	virtual const char* GetBackendName() const = 0;

	// --- Capability queries (maps CGlobalRendering fields) ---
	virtual bool HaveGL4() const = 0;
	virtual bool SupportPersistentMapping() const = 0;
	virtual bool SupportClipSpaceControl() const = 0;
	virtual bool SupportSeamlessCubeMaps() const = 0;
	virtual bool SupportMSAAFrameBuffer() const = 0;
	virtual bool SupportExplicitAttribLoc() const = 0;
	virtual bool SupportFragDepthLayout() const = 0;
	virtual bool SupportRestartPrimitive() const = 0;

	virtual int GetMaxTextureSize() const = 0;
	virtual int GetMaxTextureSlots() const = 0;
	virtual float GetMaxTexAnisotropy() const = 0;
	virtual int GetMaxDrawBuffers() const = 0;
	virtual int GetMaxUniformBufferBindings() const = 0;
	virtual int GetMaxUniformBufferSize() const = 0;
	virtual int GetMaxStorageBufferBindings() const = 0;
	virtual int GetMaxStorageBufferSize() const = 0;
	virtual int GetDepthBufferBitDepth() const = 0;

	// --- Additional capability queries ---
	virtual bool SupportTimerQueries() const = 0;
	virtual size_t GetAvailableVideoMemory() const = 0;  // Returns bytes, 0 if unavailable

	// --- Timer queries ---
	/// Create a timer query object. Returns native handle (GLuint on OpenGL).
	virtual uint32_t CreateTimerQuery() = 0;
	virtual void DeleteTimerQuery(uint32_t query) = 0;
	virtual void BeginTimerQuery(uint32_t query) = 0;
	virtual void EndTimerQuery(uint32_t query) = 0;
	/// Record a GPU timestamp into the query (maps to glQueryCounter on GL).
	virtual void TimestampQuery(uint32_t query) = 0;
	/// Check if a query result is available without blocking.
	virtual bool IsTimerQueryResultAvailable(uint32_t query) = 0;
	/// Returns elapsed time in nanoseconds, or 0 if not available yet
	virtual uint64_t GetTimerQueryResult(uint32_t query, bool wait = true) = 0;

	// --- Resource creation ---
	virtual std::unique_ptr<IRHIBuffer> CreateBuffer(
		BufferType type,
		BufferUsage usage,
		size_t size,
		const void* initialData = nullptr) = 0;

	virtual std::unique_ptr<IRHITexture> CreateTexture(
		TextureType type,
		TextureFormat format,
		uint32_t width,
		uint32_t height,
		uint32_t depthOrLayers = 1,
		uint32_t mipLevels = 1,
		uint32_t sampleCount = 1) = 0;

	virtual std::unique_ptr<IRHIShader> CreateShader(const std::string& name) = 0;

	virtual std::unique_ptr<IRHIFramebuffer> CreateFramebuffer() = 0;

	virtual std::unique_ptr<IRHIPipeline> CreatePipeline(const PipelineDesc& desc) = 0;

	// --- Context (command submission) ---
	virtual IRHIContext* GetContext() = 0;
};

} // namespace RHI

#endif // RHI_DEVICE_H
