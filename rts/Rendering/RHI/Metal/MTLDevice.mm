/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#import "MTLDevice.h"
#import "MTLContext.h"
#import "MTLBuffer.h"
#import "MTLTexture.h"
#import "MTLShader.h"
#import "MTLFramebuffer.h"
#import "MTLPipeline.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <SDL2/SDL.h>
#import <SDL2/SDL_metal.h>

#include <dispatch/dispatch.h>
#include <mach/mach_time.h>
#include "System/Log/ILog.h"

namespace RHI {

MTLDevice::MTLDevice() {
	// Create the system default Metal device
	mtlDevice = MTLCreateSystemDefaultDevice();
	if (!mtlDevice) {
		LOG_L(L_ERROR, "[MTLDevice] Failed to create Metal device - no Metal-capable GPU found");
		return;
	}

	// Log device info
	LOG("[MTLDevice] Created Metal device: %s", [[mtlDevice name] UTF8String]);

	// Query device limits
	if (@available(macOS 10.15, *)) {
		// Apple Silicon supports very large textures
		if ([mtlDevice supportsFamily:MTLGPUFamilyApple7]) {
			maxTextureSize = 16384;
		} else if ([mtlDevice supportsFamily:MTLGPUFamilyApple6]) {
			maxTextureSize = 16384;
		} else {
			maxTextureSize = 8192;
		}
	} else {
		maxTextureSize = 8192;
	}

	// Maximum buffer size
	maxBufferSize = static_cast<int>([mtlDevice maxBufferLength]);

	// Create command queue
	commandQueue = [mtlDevice newCommandQueue];
	if (!commandQueue) {
		LOG_L(L_ERROR, "[MTLDevice] Failed to create command queue");
		return;
	}

	// Create the context
	context = std::make_unique<MTLContext>(this);

	deviceValid = true;
	LOG("[MTLDevice] Metal device initialized successfully");
	LOG("[MTLDevice]   Max texture size: %d", maxTextureSize);
	LOG("[MTLDevice]   Max buffer size: %d MB", maxBufferSize / (1024 * 1024));
}

MTLDevice::~MTLDevice() {
	// Release in reverse order
	context.reset();

	currentDrawable = nil;

	if (metalView) {
		SDL_Metal_DestroyView(metalView);
		metalView = nullptr;
	}

	metalLayer = nil;
	commandQueue = nil;
	mtlDevice = nil;

	LOG("[MTLDevice] Metal device destroyed");
}

bool MTLDevice::SetupMetalLayer(SDL_Window* window) {
	if (!window) {
		LOG_L(L_ERROR, "[MTLDevice] Cannot setup Metal layer: null window");
		return false;
	}

	// Create SDL Metal view
	metalView = SDL_Metal_CreateView(window);
	if (!metalView) {
		LOG_L(L_ERROR, "[MTLDevice] Failed to create SDL Metal view: %s", SDL_GetError());
		return false;
	}

	// Get the CAMetalLayer from the view
	metalLayer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(metalView);
	if (!metalLayer) {
		LOG_L(L_ERROR, "[MTLDevice] Failed to get CAMetalLayer from view");
		SDL_Metal_DestroyView(metalView);
		metalView = nullptr;
		return false;
	}

	// Configure the layer
	metalLayer.device = mtlDevice;
	metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
	metalLayer.framebufferOnly = YES;

	// Enable display sync (vsync)
	if (@available(macOS 10.13, *)) {
		metalLayer.displaySyncEnabled = YES;
	}

	// Get window size for layer
	int width, height;
	SDL_GetWindowSize(window, &width, &height);
	metalLayer.drawableSize = CGSizeMake(width, height);

	LOG("[MTLDevice] Metal layer configured: %dx%d", width, height);
	return true;
}

id<CAMetalDrawable> MTLDevice::GetCurrentDrawable() {
	if (!metalLayer) {
		return nil;
	}

	if (!currentDrawable) {
		currentDrawable = [metalLayer nextDrawable];
	}
	return currentDrawable;
}

id<MTLTexture> MTLDevice::GetDrawableTexture() {
	id<CAMetalDrawable> drawable = GetCurrentDrawable();
	return drawable ? drawable.texture : nil;
}

int MTLDevice::GetMaxTextureSize() const {
	return maxTextureSize;
}

int MTLDevice::GetMaxUniformBufferSize() const {
	// Metal allows up to 4KB for setVertexBytes, larger needs buffers
	return 4 * 1024;
}

int MTLDevice::GetMaxStorageBufferSize() const {
	return maxBufferSize;
}

bool MTLDevice::SupportTimerQueries() const {
	// Metal supports GPU timestamps on Apple Silicon
	if (@available(macOS 10.15, *)) {
		return [mtlDevice supportsCounterSampling:MTLCounterSamplingPointAtStageBoundary];
	}
	return false;
}

size_t MTLDevice::GetAvailableVideoMemory() const {
	// On Apple Silicon, memory is unified
	// Return recommended working set size as an approximation
	if (@available(macOS 10.13, *)) {
		return static_cast<size_t>([mtlDevice recommendedMaxWorkingSetSize]);
	}
	return 0;
}

// --- Fence sync ---

FenceHandle MTLDevice::CreateFence() {
	// Metal uses dispatch_semaphore for CPU-GPU sync.
	// Create a signaled semaphore (value 1) that GPU work can signal.
	dispatch_semaphore_t sem = dispatch_semaphore_create(0);
	// Signal it immediately so first wait succeeds
	dispatch_semaphore_signal(sem);
	return reinterpret_cast<FenceHandle>((__bridge_retained void*)sem);
}

bool MTLDevice::WaitFence(FenceHandle fence, uint64_t timeoutNs) {
	if (!fence) return true;
	dispatch_semaphore_t sem = (__bridge dispatch_semaphore_t)fence;
	dispatch_time_t timeout = dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(timeoutNs));
	long result = dispatch_semaphore_wait(sem, timeout);
	if (result == 0) {
		// Re-signal so subsequent waits also succeed (fence is level-triggered, not edge)
		dispatch_semaphore_signal(sem);
		return true;
	}
	return false;
}

void MTLDevice::DeleteFence(FenceHandle fence) {
	if (fence) {
		// Release the retained reference
		dispatch_semaphore_t sem = (__bridge_transfer dispatch_semaphore_t)fence;
		(void)sem; // ARC will release
	}
}

// --- Version/debug info ---

VersionInfo MTLDevice::GetVersionInfo() const {
	VersionInfo info;
	info.vendor = "Apple";
	if (mtlDevice) {
		info.renderer = [[mtlDevice name] UTF8String];
	}
	info.version = "Metal";
	info.shadingLanguageVersion = "MSL 2.4";
	return info;
}

int MTLDevice::GetFramebufferSampleCount() const {
	// Query from the metal layer or default to 1 (no MSAA by default)
	return 1;
}

// --- Debug output ---

void MTLDevice::SetDebugOutputEnabled(bool enabled, bool synchronous) {
	// Metal uses Metal Validation Layer and GPU Capture, not runtime debug output
	(void)enabled;
	(void)synchronous;
}

void MTLDevice::SetDebugMessageCallback(DebugMessageCallback callback, const void* userParam) {
	// Metal doesn't have a debug message callback mechanism like OpenGL
	(void)callback;
	(void)userParam;
}

void MTLDevice::ClearErrors() {
	// Metal doesn't have an error state like glGetError()
}

// Timer query implementation using Metal timestamps
uint32_t MTLDevice::CreateTimerQuery() {
	uint32_t id;
	if (!freeQueryIds.empty()) {
		id = freeQueryIds.back();
		freeQueryIds.pop_back();
		timerQueries[id - 1] = TimerQuery{};
	} else {
		id = nextQueryId++;
		timerQueries.push_back(TimerQuery{});
	}
	return id;
}

void MTLDevice::DeleteTimerQuery(uint32_t query) {
	if (query > 0 && query <= timerQueries.size()) {
		timerQueries[query - 1] = TimerQuery{};
		freeQueryIds.push_back(query);
	}
}

void MTLDevice::BeginTimerQuery(uint32_t query) {
	if (query > 0 && query <= timerQueries.size()) {
		auto& q = timerQueries[query - 1];
		q.startTime = mach_absolute_time();
		q.started = true;
		q.ended = false;
	}
}

void MTLDevice::EndTimerQuery(uint32_t query) {
	if (query > 0 && query <= timerQueries.size()) {
		auto& q = timerQueries[query - 1];
		if (q.started) {
			q.endTime = mach_absolute_time();
			q.ended = true;
		}
	}
}

void MTLDevice::TimestampQuery(uint32_t query) {
	// Record an absolute timestamp (CPU-based on Metal).
	// Stores timestamp in startTime; GetTimerQueryResult returns it as nanoseconds.
	if (query > 0 && query <= timerQueries.size()) {
		auto& q = timerQueries[query - 1];
		q.startTime = mach_absolute_time();
		q.started = true;
		q.ended = true;  // Timestamp queries are immediately available
		q.endTime = 0;   // Mark as timestamp query (not elapsed)
	}
}

bool MTLDevice::IsTimerQueryResultAvailable(uint32_t query) {
	// CPU timing is always immediately available
	if (query > 0 && query <= timerQueries.size()) {
		return timerQueries[query - 1].ended;
	}
	return false;
}

uint64_t MTLDevice::GetTimerQueryResult(uint32_t query, bool wait) {
	(void)wait;  // CPU timing doesn't need to wait

	if (query > 0 && query <= timerQueries.size()) {
		auto& q = timerQueries[query - 1];
		if (q.ended) {
			// Convert mach_absolute_time units to nanoseconds
			static mach_timebase_info_data_t timebaseInfo;
			if (timebaseInfo.denom == 0) {
				mach_timebase_info(&timebaseInfo);
			}
			if (q.endTime == 0) {
				// Timestamp query: return absolute timestamp in nanoseconds
				return q.startTime * timebaseInfo.numer / timebaseInfo.denom;
			}
			// Elapsed time query: return difference in nanoseconds
			uint64_t elapsed = q.endTime - q.startTime;
			return elapsed * timebaseInfo.numer / timebaseInfo.denom;
		}
	}
	return 0;
}

// --- Resource creation ---

std::unique_ptr<IRHIBuffer> MTLDevice::CreateBuffer(
	BufferType type,
	BufferUsage usage,
	size_t size,
	const void* initialData)
{
	if (!deviceValid) return nullptr;
	return std::make_unique<MTLBuffer>(this, type, usage, size, initialData);
}

std::unique_ptr<IRHITexture> MTLDevice::CreateTexture(
	TextureType type,
	TextureFormat format,
	uint32_t width,
	uint32_t height,
	uint32_t depthOrLayers,
	uint32_t mipLevels,
	uint32_t sampleCount)
{
	if (!deviceValid) return nullptr;
	return std::make_unique<MTLTexture>(this, type, format, width, height, depthOrLayers, mipLevels, sampleCount);
}

std::unique_ptr<IRHIShader> MTLDevice::CreateShader(const std::string& name) {
	if (!deviceValid) return nullptr;
	return std::make_unique<MTLShader>(this, name);
}

std::unique_ptr<IRHIFramebuffer> MTLDevice::CreateFramebuffer() {
	if (!deviceValid) return nullptr;
	return std::make_unique<MTLFramebuffer>(this);
}

std::unique_ptr<IRHIPipeline> MTLDevice::CreatePipeline(const PipelineDesc& desc) {
	if (!deviceValid) return nullptr;
	return std::make_unique<MTLPipeline>(this, desc);
}

IRHIContext* MTLDevice::GetContext() {
	return context.get();
}

} // namespace RHI
