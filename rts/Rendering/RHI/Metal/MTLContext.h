/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#ifndef MTL_RHI_CONTEXT_H
#define MTL_RHI_CONTEXT_H

/**
 * Metal Context Implementation
 *
 * Handles command buffer creation, render command encoding, and frame presentation.
 *
 * Metal API mapping:
 *   Frame begin              ->  Create MTLCommandBuffer
 *   BeginRenderPass          ->  Create MTLRenderCommandEncoder from descriptor
 *   Draw*                    ->  drawPrimitives / drawIndexedPrimitives
 *   EndRenderPass            ->  endEncoding on current encoder
 *   Frame end                ->  presentDrawable + commit command buffer
 *
 * Triple buffering:
 *   Metal requires explicit synchronization when triple buffering.
 *   We use dispatch_semaphore to limit in-flight frames to 3.
 *
 * Clear operations:
 *   Metal clears are specified via render pass load actions.
 *   For mid-pass clears, we end the pass and start a new one with clear.
 */

#include "Rendering/RHI/RHIContext.h"
#include <array>

#ifdef __OBJC__
#import <Metal/Metal.h>
#import <dispatch/dispatch.h>
#endif

namespace RHI {

class MTLDevice;
class MTLBuffer;
class MTLTexture;
class MTLShader;
class MTLFramebuffer;
class MTLPipeline;

class MTLContext : public IRHIContext {
public:
	explicit MTLContext(MTLDevice* device);
	~MTLContext() override;

	// Prevent copying
	MTLContext(const MTLContext&) = delete;
	MTLContext& operator=(const MTLContext&) = delete;

	// --- Render pass ---
	void BeginRenderPass(IRHIFramebuffer* framebuffer, const RenderPassDesc& desc) override;
	void BeginDefaultRenderPass(const RenderPassDesc& desc) override;
	void EndRenderPass() override;

	// --- Pipeline state ---
	void BindPipeline(IRHIPipeline* pipeline) override;

	// --- Resource binding ---
	void BindVertexBuffer(IRHIBuffer* buffer, uint32_t binding) override;
	void BindIndexBuffer(IRHIBuffer* buffer, IndexType indexType) override;
	void BindUniformBuffer(IRHIBuffer* buffer, uint32_t bindingPoint) override;
	void BindTexture(IRHITexture* texture, uint32_t unit) override;
	void BindShader(IRHIShader* shader) override;

	// --- Draw commands ---
	void Draw(PrimitiveType primitive, uint32_t vertexCount, uint32_t firstVertex) override;
	void DrawIndexed(PrimitiveType primitive, uint32_t indexCount, uint32_t firstIndex, int32_t vertexOffset) override;
	void DrawInstanced(PrimitiveType primitive, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) override;
	void DrawIndexedInstanced(PrimitiveType primitive, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) override;

	void DrawIndirect(PrimitiveType primitive, IRHIBuffer* buffer, size_t offset, uint32_t drawCount, uint32_t stride) override;
	void DrawIndexedIndirect(PrimitiveType primitive, IRHIBuffer* buffer, size_t offset, uint32_t drawCount, uint32_t stride, IndexType indexType) override;

	// --- Viewport / Scissor ---
	void SetViewport(const Viewport& viewport) override;
	void SetScissor(const ScissorRect& rect) override;
	void SetClipDistanceEnabled(uint32_t index, bool enabled) override;

	// --- Global state (Metal handles via pipeline descriptors — no-ops) ---
	void SetDepthTestEnabled(bool enabled) override {}
	void SetDepthFunc(CompareFunc func) override {}
	void SetClipControl(bool zeroToOne) override {}
	void SetSeamlessCubeMapsEnabled(bool enabled) override {}
	void SetMultisampleEnabled(bool enabled) override {}
	void SetSampleShading(bool enabled, float minRate) override {}

	// --- Clear ---
	void ClearColor(float r, float g, float b, float a) override;
	void ClearDepth(float depth) override;
	void ClearStencil(uint32_t value) override;
	void Clear(bool color, bool depth, bool stencil) override;

	// --- Framebuffer operations ---
	void BlitFramebuffer(IRHIFramebuffer* src, IRHIFramebuffer* dst,
	                     int srcX0, int srcY0, int srcX1, int srcY1,
	                     int dstX0, int dstY0, int dstX1, int dstY1,
	                     bool colorBit, bool depthBit) override;

	// --- Synchronization ---
	void Flush() override;
	void Finish() override;

#ifdef __OBJC__
	// --- Metal-specific ---

	/// Begin a new frame (called at start of frame rendering)
	void BeginFrame();

	/// End the current frame and present to screen
	void EndFrame();

	/// Get the current command buffer
	id<MTLCommandBuffer> GetCommandBuffer() const { return commandBuffer; }

	/// Get the current render command encoder
	id<MTLRenderCommandEncoder> GetRenderEncoder() const { return renderEncoder; }
#endif

private:
#ifdef __OBJC__
	void EnsureCommandBuffer();
	void EnsureRenderEncoder();
	void ApplyPipelineState();
	void BindCurrentResources();

	static MTLPrimitiveType ToMTLPrimitiveType(PrimitiveType type);
	static MTLIndexType ToMTLIndexType(IndexType type);

	id<MTLCommandBuffer>        commandBuffer = nil;
	id<MTLRenderCommandEncoder> renderEncoder = nil;

	// Triple buffering semaphore
	dispatch_semaphore_t frameSemaphore;
	static constexpr int MaxFramesInFlight = 3;
#else
	void* commandBuffer = nullptr;
	void* renderEncoder = nullptr;
	void* frameSemaphore = nullptr;
#endif

	MTLDevice*      device;

	// Current bound state
	MTLPipeline*    currentPipeline   = nullptr;
	MTLShader*      currentShader     = nullptr;
	MTLBuffer*      currentVertexBuffer = nullptr;
	MTLBuffer*      currentIndexBuffer  = nullptr;
	MTLFramebuffer* currentFramebuffer  = nullptr;
	IndexType       currentIndexType    = IndexType::UInt32;
	uint32_t        currentVertexBinding = 0;

	// Bound textures
	static constexpr uint32_t MaxTextureUnits = 32;
	std::array<MTLTexture*, MaxTextureUnits> boundTextures = {};

	// Bound uniform buffers
	static constexpr uint32_t MaxUniformBindings = 16;
	std::array<MTLBuffer*, MaxUniformBindings> boundUniformBuffers = {};

	// Clear state (accumulated for next render pass)
	struct ClearColor clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
	float      clearDepthValue = 1.0f;
	uint32_t   clearStencilValue = 0;
	bool       pendingColorClear = false;
	bool       pendingDepthClear = false;
	bool       pendingStencilClear = false;

	// Viewport and scissor
	Viewport    currentViewport;
	ScissorRect currentScissor;
	bool        scissorEnabled = false;

	// Render pass state
	bool        inRenderPass = false;
	RenderPassDesc currentPassDesc;
};

} // namespace RHI

#endif // MTL_RHI_CONTEXT_H
