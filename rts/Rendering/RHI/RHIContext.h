/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#ifndef RHI_CONTEXT_H
#define RHI_CONTEXT_H

/**
 * RHI Context Interface
 *
 * Maps GL command submission and state management:
 *   glDrawArrays / glDrawElements / glDrawElementsInstanced  ->  Draw*()
 *   glClear / glClearColor / glClearDepth                     ->  Clear*()
 *   glViewport / glScissor                                    ->  SetViewport() / SetScissor()
 *   FBO::Bind() + render + FBO::Unbind()                      ->  BeginRenderPass() / EndRenderPass()
 *
 * Metal requires explicit render pass boundaries; OpenGL backend
 * maps BeginRenderPass to FBO bind and EndRenderPass to FBO unbind.
 */

#include "RHITypes.h"

namespace RHI {

class IRHIBuffer;
class IRHITexture;
class IRHIShader;
class IRHIFramebuffer;
class IRHIPipeline;

class IRHIContext {
public:
	virtual ~IRHIContext() = default;

	// --- Render pass ---
	virtual void BeginRenderPass(IRHIFramebuffer* framebuffer, const RenderPassDesc& desc) = 0;
	virtual void BeginDefaultRenderPass(const RenderPassDesc& desc) = 0;
	virtual void EndRenderPass() = 0;

	// --- Pipeline state ---
	virtual void BindPipeline(IRHIPipeline* pipeline) = 0;

	// --- Resource binding ---
	virtual void BindVertexBuffer(IRHIBuffer* buffer, uint32_t binding = 0) = 0;
	virtual void BindIndexBuffer(IRHIBuffer* buffer, IndexType indexType = IndexType::UInt32) = 0;
	virtual void BindUniformBuffer(IRHIBuffer* buffer, uint32_t bindingPoint) = 0;
	virtual void BindTexture(IRHITexture* texture, uint32_t unit) = 0;
	virtual void BindShader(IRHIShader* shader) = 0;

	// --- Draw commands ---
	virtual void Draw(PrimitiveType primitive, uint32_t vertexCount, uint32_t firstVertex = 0) = 0;
	virtual void DrawIndexed(PrimitiveType primitive, uint32_t indexCount, uint32_t firstIndex = 0, int32_t vertexOffset = 0) = 0;
	virtual void DrawInstanced(PrimitiveType primitive, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex = 0, uint32_t firstInstance = 0) = 0;
	virtual void DrawIndexedInstanced(PrimitiveType primitive, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex = 0, int32_t vertexOffset = 0, uint32_t firstInstance = 0) = 0;

	/// Indirect draw commands (buffer contains DrawArraysIndirectCommand structs)
	/// Maps to glMultiDrawArraysIndirect
	virtual void DrawIndirect(PrimitiveType primitive, IRHIBuffer* buffer, size_t offset, uint32_t drawCount, uint32_t stride) = 0;

	/// Indirect indexed draw commands (buffer contains DrawElementsIndirectCommand structs)
	/// Maps to glMultiDrawElementsIndirect
	virtual void DrawIndexedIndirect(PrimitiveType primitive, IRHIBuffer* buffer, size_t offset, uint32_t drawCount, uint32_t stride, IndexType indexType = IndexType::UInt32) = 0;

	// --- Viewport / Scissor ---
	virtual void SetViewport(const Viewport& viewport) = 0;
	virtual void SetScissor(const ScissorRect& rect) = 0;

	// --- Clip distances ---
	/// Enable/disable a clip distance plane (GL_CLIP_DISTANCE0 + index)
	/// Metal: handled via [[clip_distance]] in shader output; this is a no-op on Metal
	virtual void SetClipDistanceEnabled(uint32_t index, bool enabled) = 0;
	static constexpr uint32_t MaxClipDistances = 8;

	// --- Vertex attribute ---
	virtual void SetVertexAttribDivisor(uint32_t index, uint32_t divisor) = 0;

	// --- Global state ---
	virtual void SetDepthTestEnabled(bool enabled) = 0;
	virtual void SetDepthFunc(CompareFunc func) = 0;
	virtual void SetClipControl(bool zeroToOne) = 0;
	virtual void SetSeamlessCubeMapsEnabled(bool enabled) = 0;
	virtual void SetMultisampleEnabled(bool enabled) = 0;
	virtual void SetSampleShading(bool enabled, float minRate = 0.0f) = 0;

	// --- Clear ---
	virtual void ClearColor(float r, float g, float b, float a) = 0;
	virtual void ClearDepth(float depth) = 0;
	virtual void ClearStencil(uint32_t value) = 0;
	virtual void Clear(bool color, bool depth, bool stencil) = 0;

	// --- Framebuffer operations ---
	virtual void BlitFramebuffer(
		IRHIFramebuffer* src, IRHIFramebuffer* dst,
		int srcX0, int srcY0, int srcX1, int srcY1,
		int dstX0, int dstY0, int dstX1, int dstY1,
		bool colorBit, bool depthBit) = 0;

	// --- Synchronization ---
	virtual void Flush() = 0;
	virtual void Finish() = 0;
};

} // namespace RHI

#endif // RHI_CONTEXT_H
