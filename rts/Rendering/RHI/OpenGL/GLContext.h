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

	void DrawIndirect(PrimitiveType primitive, IRHIBuffer* buffer, size_t offset, uint32_t drawCount, uint32_t stride) override;
	void DrawIndexedIndirect(PrimitiveType primitive, IRHIBuffer* buffer, size_t offset, uint32_t drawCount, uint32_t stride, IndexType indexType) override;

	void SetViewport(const Viewport& viewport) override;
	void SetScissor(const ScissorRect& rect) override;
	void SetClipDistanceEnabled(uint32_t index, bool enabled) override;
	void SetClipPlaneEquation(uint32_t index, const double* equation) override;

	void SetVertexAttribDivisor(uint32_t index, uint32_t divisor) override;

	void SetDepthTestEnabled(bool enabled) override;
	void SetDepthFunc(CompareFunc func) override;
	void SetClipControl(bool zeroToOne) override;
	void SetSeamlessCubeMapsEnabled(bool enabled) override;
	void SetMultisampleEnabled(bool enabled) override;
	void SetSampleShading(bool enabled, float minRate) override;

	void SetDepthWriteEnabled(bool enabled) override;
	void SetBlendEnabled(bool enabled) override;
	void SetBlendFunc(BlendFactor src, BlendFactor dst) override;
	void SetBlendFuncSeparate(BlendFactor srcColor, BlendFactor dstColor, BlendFactor srcAlpha, BlendFactor dstAlpha) override;
	void SetBlendEquation(BlendOp op) override;
	void SetBlendEquationSeparate(BlendOp colorOp, BlendOp alphaOp) override;
	void SetCullFaceEnabled(bool enabled) override;
	void SetCullFace(CullMode mode) override;
	void SetColorMask(bool r, bool g, bool b, bool a) override;
	void SetPolygonOffset(bool enabled, float factor, float units) override;
	void SetLineWidth(float width) override;
	void SetPointSize(float size) override;
	void SetPolygonMode(PolygonMode mode) override;
	void SetStencilTestEnabled(bool enabled) override;
	void SetStencilFunc(CompareFunc func, int32_t ref, uint32_t mask) override;
	void SetStencilOp(StencilOp sfail, StencilOp dpfail, StencilOp dppass) override;
	void SetStencilMask(uint32_t mask) override;
	void SetDepthClampEnabled(bool enabled) override;
	void SetScissorTestEnabled(bool enabled) override;
	void SetLogicOpEnabled(bool enabled) override;
	void SetLogicOp(LogicOp op) override;
	void SetProgramPointSizeEnabled(bool enabled) override;

	void ClearColor(float r, float g, float b, float a) override;
	void ClearDepth(float depth) override;
	void ClearStencil(uint32_t value) override;
	void Clear(bool color, bool depth, bool stencil) override;

	void BlitFramebuffer(IRHIFramebuffer* src, IRHIFramebuffer* dst, int srcX0, int srcY0, int srcX1, int srcY1, int dstX0, int dstY0, int dstX1, int dstY1, bool colorBit, bool depthBit, bool filterLinear) override;

	void Flush() override;
	void Finish() override;

private:
	IndexType currentIndexType = IndexType::UInt32;
};

} // namespace RHI

#endif // GL_RHI_CONTEXT_H
