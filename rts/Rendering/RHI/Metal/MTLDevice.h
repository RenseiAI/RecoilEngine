/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#ifndef MTL_RHI_DEVICE_H
#define MTL_RHI_DEVICE_H

/**
 * Metal Device Implementation
 *
 * Creates and manages the Metal device and command queue.
 * Handles SDL2-Metal integration via CAMetalLayer.
 *
 * Metal API mapping:
 *   MTLCreateSystemDefaultDevice()  ->  MTLDevice constructor
 *   device.newCommandQueue()        ->  MTLDevice constructor
 *   CAMetalLayer configuration      ->  SetupMetalLayer()
 *
 * Apple Silicon capabilities:
 *   - Unified memory architecture (storageModeShared for all buffers)
 *   - Apple GPU family 7+ features
 *   - Up to 16K texture dimensions
 *   - 32 texture slots per shader stage
 */

#include "Rendering/RHI/RHIDevice.h"
#include <climits>
#include <memory>

#ifdef __OBJC__
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

struct SDL_Window;
typedef void* SDL_MetalView;
#endif

namespace RHI {

class MTLContext;

class MTLDevice : public IRHIDevice {
public:
	MTLDevice();
	~MTLDevice() override;

	// Prevent copying
	MTLDevice(const MTLDevice&) = delete;
	MTLDevice& operator=(const MTLDevice&) = delete;

	// --- Backend identification ---
	Backend GetBackend() const override { return Backend::Metal; }
	const char* GetBackendName() const override { return "Metal"; }

	// --- Capability queries ---
	bool HaveGL4() const override { return true; }  // Metal exceeds GL4 capabilities
	bool SupportPersistentMapping() const override { return true; }  // storageModeShared
	bool SupportClipSpaceControl() const override { return true; }
	bool SupportSeamlessCubeMaps() const override { return true; }
	bool SupportMSAAFrameBuffer() const override { return true; }
	bool SupportExplicitAttribLoc() const override { return true; }
	bool SupportFragDepthLayout() const override { return true; }
	bool SupportRestartPrimitive() const override { return true; }

	int GetMaxTextureSize() const override;
	int GetMaxTextureSlots() const override { return 31; }  // Metal limit per stage
	float GetMaxTexAnisotropy() const override { return 16.0f; }
	int GetMaxDrawBuffers() const override { return 8; }
	int GetMaxUniformBufferBindings() const override { return 31; }
	int GetMaxUniformBufferSize() const override;
	int GetMaxStorageBufferBindings() const override { return 31; }
	int GetMaxStorageBufferSize() const override;
	int GetDepthBufferBitDepth() const override { return 32; }

	int GetMaxFragmentTextureSlots() const override { return 31; }
	int GetMaxCombinedTextureSlots() const override { return 31; }
	int GetMaxVaryings() const override { return 60; }
	int GetMaxVertexAttributes() const override { return 31; }
	int GetMaxRecommendedIndices() const override { return INT_MAX; }
	int GetMaxRecommendedVertices() const override { return INT_MAX; }
	bool SupportTextureQueryLOD() const override { return true; }

	bool SupportTimerQueries() const override;
	size_t GetAvailableVideoMemory() const override;

	// --- Fence sync ---
	FenceHandle CreateFence() override;
	bool WaitFence(FenceHandle fence, uint64_t timeoutNs) override;
	void DeleteFence(FenceHandle fence) override;

	// --- Version/debug info ---
	VersionInfo GetVersionInfo() const override;
	int GetFramebufferSampleCount() const override;

	// --- Debug output ---
	void SetDebugOutputEnabled(bool enabled, bool synchronous) override;
	void SetDebugMessageCallback(DebugMessageCallback callback, const void* userParam) override;
	void SetDebugMessageControl(uint32_t source, uint32_t type, uint32_t severity, bool enabled) override;
	void ClearErrors() override;

	// --- Timer queries ---
	uint32_t CreateTimerQuery() override;
	void DeleteTimerQuery(uint32_t query) override;
	void BeginTimerQuery(uint32_t query) override;
	void EndTimerQuery(uint32_t query) override;
	void TimestampQuery(uint32_t query) override;
	bool IsTimerQueryResultAvailable(uint32_t query) override;
	uint64_t GetTimerQueryResult(uint32_t query, bool wait) override;

	// --- Resource creation ---
	std::unique_ptr<IRHIBuffer> CreateBuffer(
		BufferType type,
		BufferUsage usage,
		size_t size,
		const void* initialData) override;

	std::unique_ptr<IRHITexture> CreateTexture(
		TextureType type,
		TextureFormat format,
		uint32_t width,
		uint32_t height,
		uint32_t depthOrLayers,
		uint32_t mipLevels,
		uint32_t sampleCount) override;

	std::unique_ptr<IRHITexture> CreateTextureFromExisting(
		uint32_t glTextureId,
		TextureType type,
		TextureFormat format,
		uint32_t width,
		uint32_t height,
		uint32_t depthOrLayers,
		uint32_t mipLevels) override;

	std::unique_ptr<IRHITexture> WrapExistingTexture(
		uint32_t glTextureId,
		TextureType type,
		TextureFormat format,
		uint32_t width,
		uint32_t height,
		uint32_t depthOrLayers,
		uint32_t mipLevels) override { return nullptr; }

	std::unique_ptr<IRHIShader> CreateShader(const std::string& name) override;

	std::unique_ptr<IRHIFramebuffer> CreateFramebuffer() override;

	std::unique_ptr<IRHIPipeline> CreatePipeline(const PipelineDesc& desc) override;

	// --- Window integration ---
	bool SetupWindowIntegration(SDL_Window* window) override;

	// --- Context ---
	IRHIContext* GetContext() override;

#ifdef __OBJC__
	// --- Metal-specific accessors ---
	id<MTLDevice> GetMTLDevice() const { return mtlDevice; }
	id<MTLCommandQueue> GetCommandQueue() const { return commandQueue; }
	CAMetalLayer* GetMetalLayer() const { return metalLayer; }

	/// Setup the Metal layer for a given SDL window.
	/// Must be called before rendering begins.
	bool SetupMetalLayer(SDL_Window* window);

	/// Get the current drawable for presentation.
	id<CAMetalDrawable> GetCurrentDrawable();

	/// Get the drawable texture for rendering to screen.
	id<MTLTexture> GetDrawableTexture();
#endif

	/// Check if the device is properly initialized
	bool IsValid() const { return deviceValid; }

private:
#ifdef __OBJC__
	id<MTLDevice>       mtlDevice      = nil;
	id<MTLCommandQueue> commandQueue   = nil;
	CAMetalLayer*       metalLayer     = nil;
	SDL_MetalView       metalView      = nullptr;
	id<CAMetalDrawable> currentDrawable = nil;
#else
	void*               mtlDevice      = nullptr;
	void*               commandQueue   = nullptr;
	void*               metalLayer     = nullptr;
	void*               metalView      = nullptr;
	void*               currentDrawable = nullptr;
#endif

	std::unique_ptr<MTLContext> context;
	bool deviceValid = false;

	// Device limits (queried at initialization)
	int maxTextureSize = 16384;
	int maxBufferSize = 256 * 1024 * 1024;  // 256 MB default

	// Timer query state
	struct TimerQuery {
		uint64_t startTime = 0;
		uint64_t endTime = 0;
		bool started = false;
		bool ended = false;
	};
	std::vector<TimerQuery> timerQueries;
	std::vector<uint32_t> freeQueryIds;
	uint32_t nextQueryId = 1;
};

} // namespace RHI

#endif // MTL_RHI_DEVICE_H
