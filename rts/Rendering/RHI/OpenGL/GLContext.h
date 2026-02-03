/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#ifndef GL_RHI_CONTEXT_H
#define GL_RHI_CONTEXT_H

#include "Rendering/RHI/RHIContext.h"

namespace RHI {

class GLContext : public IRHIContext {
public:
	GLContext() = default;
	~GLContext() override = default;

	void BeginRenderPass(IRHIFramebuffer* framebuffer, const RenderPassDesc& desc) override;
	void BeginDefaultRenderPass(const RenderPassDesc& desc) override;
	void EndRenderPass() override;

	void BindPipeline(IRHIPipeline* pipeline) override;

	void BindVertexBuffer(IRHIBuffer* buffer, uint32_t binding) override;
	void BindIndexBuffer(IRHIBuffer* buffer, IndexType indexType) override;
	void BindUniformBuffer(IRHIBuffer* buffer, uint32_t bindingPoint) override;
	void BindTexture(IRHITexture* texture, uint32_t unit) override;
	void BindShader(IRHIShader* shader) override;

	void Draw(PrimitiveType primitive, uint32_t vertexCount, uint32_t firstVertex) override;
	void DrawIndexed(PrimitiveType primitive, uint32_t indexCount, uint32_t firstIndex, int32_t vertexOffset) override;
	void DrawInstanced(PrimitiveType primitive, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) override;
	void DrawIndexedInstanced(PrimitiveType primitive, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) override;

	void SetViewport(const Viewport& viewport) override;
	void SetScissor(const ScissorRect& rect) override;

	void ClearColor(float r, float g, float b, float a) override;
	void ClearDepth(float depth) override;
	void ClearStencil(uint32_t value) override;
	void Clear(bool color, bool depth, bool stencil) override;

	void BlitFramebuffer(IRHIFramebuffer* src, IRHIFramebuffer* dst, int srcX0, int srcY0, int srcX1, int srcY1, int dstX0, int dstY0, int dstX1, int dstY1, bool colorBit, bool depthBit) override;

	void Flush() override;
	void Finish() override;

private:
	IndexType currentIndexType = IndexType::UInt32;
};

} // namespace RHI

#endif // GL_RHI_CONTEXT_H
