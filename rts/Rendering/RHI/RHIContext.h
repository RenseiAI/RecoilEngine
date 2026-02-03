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

	// --- Viewport / Scissor ---
	virtual void SetViewport(const Viewport& viewport) = 0;
	virtual void SetScissor(const ScissorRect& rect) = 0;

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
