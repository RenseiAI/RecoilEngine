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

	virtual int GetMaxFragmentTextureSlots() const = 0;
	virtual int GetMaxCombinedTextureSlots() const = 0;
	virtual int GetMaxVaryings() const = 0;
	virtual int GetMaxVertexAttributes() const = 0;
	virtual int GetMaxRecommendedIndices() const = 0;
	virtual int GetMaxRecommendedVertices() const = 0;
	virtual bool SupportTextureQueryLOD() const = 0;

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

	// --- Fence sync ---
	virtual FenceHandle CreateFence() = 0;
	virtual bool WaitFence(FenceHandle fence, uint64_t timeoutNs = 1) = 0;
	virtual void DeleteFence(FenceHandle fence) = 0;

	// --- Version/debug info ---
	virtual VersionInfo GetVersionInfo() const = 0;
	virtual int GetFramebufferSampleCount() const = 0;

	// --- Debug output ---
	using DebugMessageCallback = void(*)(uint32_t source, uint32_t type, uint32_t id,
		uint32_t severity, const char* message, const void* userParam);
	virtual void SetDebugOutputEnabled(bool enabled, bool synchronous = true) = 0;
	virtual void SetDebugMessageCallback(DebugMessageCallback callback, const void* userParam = nullptr) = 0;
	/// Control which debug messages are reported. Pass 0 for don't-care on source/type/severity.
	/// On OpenGL: maps to glDebugMessageControl. On Metal: no-op (filtering done in callback).
	virtual void SetDebugMessageControl(uint32_t source, uint32_t type, uint32_t severity, bool enabled) = 0;
	virtual void ClearErrors() = 0;

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

	/// Wrap an existing GL texture ID in an IRHITexture (OpenGL backend only).
	/// The IRHITexture takes ownership and will delete the GL texture on destruction.
	/// Metal backend returns nullptr (not supported).
	virtual std::unique_ptr<IRHITexture> CreateTextureFromExisting(
		uint32_t glTextureId,
		TextureType type,
		TextureFormat format,
		uint32_t width,
		uint32_t height,
		uint32_t depthOrLayers = 1,
		uint32_t mipLevels = 1) = 0;

	virtual std::unique_ptr<IRHIShader> CreateShader(const std::string& name) = 0;

	virtual std::unique_ptr<IRHIFramebuffer> CreateFramebuffer() = 0;

	virtual std::unique_ptr<IRHIPipeline> CreatePipeline(const PipelineDesc& desc) = 0;

	// --- Context (command submission) ---
	virtual IRHIContext* GetContext() = 0;
};

} // namespace RHI

#endif // RHI_DEVICE_H
