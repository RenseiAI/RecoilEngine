/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#include "GLContext.h"
#include "GLBuffer.h"
#include "GLTexture.h"
#include "GLShader.h"
#include "GLFramebuffer.h"
#include "GLPipeline.h"
#include "Rendering/GL/myGL.h"

namespace RHI {

// --- Primitive type mapping ---

static GLenum ToGLPrimitive(PrimitiveType type) {
	switch (type) {
		case PrimitiveType::Points:        return GL_POINTS;
		case PrimitiveType::Lines:         return GL_LINES;
		case PrimitiveType::LineStrip:     return GL_LINE_STRIP;
		case PrimitiveType::Triangles:     return GL_TRIANGLES;
		case PrimitiveType::TriangleStrip: return GL_TRIANGLE_STRIP;
		case PrimitiveType::TriangleFan:   return GL_TRIANGLE_FAN;
	}
	return GL_TRIANGLES;
}

static GLenum ToGLIndexType(IndexType type) {
	switch (type) {
		case IndexType::UInt16: return GL_UNSIGNED_SHORT;
		case IndexType::UInt32: return GL_UNSIGNED_INT;
	}
	return GL_UNSIGNED_INT;
}

static size_t IndexTypeSize(IndexType type) {
	switch (type) {
		case IndexType::UInt16: return 2;
		case IndexType::UInt32: return 4;
	}
	return 4;
}

static GLenum ToGLCompareFunc(CompareFunc func) {
	switch (func) {
		case CompareFunc::Never:        return GL_NEVER;
		case CompareFunc::Less:         return GL_LESS;
		case CompareFunc::LessEqual:    return GL_LEQUAL;
		case CompareFunc::Equal:        return GL_EQUAL;
		case CompareFunc::NotEqual:     return GL_NOTEQUAL;
		case CompareFunc::GreaterEqual: return GL_GEQUAL;
		case CompareFunc::Greater:      return GL_GREATER;
		case CompareFunc::Always:       return GL_ALWAYS;
	}
	return GL_LEQUAL;
}

// --- Render pass (maps to FBO bind/unbind) ---

void GLContext::BeginRenderPass(IRHIFramebuffer* framebuffer, const RenderPassDesc& desc) {
	if (framebuffer)
		framebuffer->Bind();

	// Apply clear actions
	GLbitfield clearBits = 0;
	if (desc.colorAttachmentCount > 0 && desc.colorAttachments[0].loadAction == LoadAction::Clear) {
		const auto& cc = desc.colorAttachments[0].clearColor;
		glClearColor(cc.r, cc.g, cc.b, cc.a);
		clearBits |= GL_COLOR_BUFFER_BIT;
	}
	if (desc.hasDepth && desc.depthAttachment.loadAction == LoadAction::Clear) {
		glClearDepth(desc.depthAttachment.clearDepth);
		clearBits |= GL_DEPTH_BUFFER_BIT;
	}
	if (clearBits)
		glClear(clearBits);
}

void GLContext::BeginDefaultRenderPass(const RenderPassDesc& desc) {
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	GLbitfield clearBits = 0;
	if (desc.colorAttachmentCount > 0 && desc.colorAttachments[0].loadAction == LoadAction::Clear) {
		const auto& cc = desc.colorAttachments[0].clearColor;
		glClearColor(cc.r, cc.g, cc.b, cc.a);
		clearBits |= GL_COLOR_BUFFER_BIT;
	}
	if (desc.hasDepth && desc.depthAttachment.loadAction == LoadAction::Clear) {
		glClearDepth(desc.depthAttachment.clearDepth);
		clearBits |= GL_DEPTH_BUFFER_BIT;
	}
	if (clearBits)
		glClear(clearBits);
}

void GLContext::EndRenderPass() {
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// --- Pipeline ---

void GLContext::BindPipeline(IRHIPipeline* pipeline) {
	if (pipeline)
		pipeline->Bind();
}

// --- Resource binding ---

void GLContext::BindVertexBuffer(IRHIBuffer* buffer, uint32_t /*binding*/) {
	if (buffer) buffer->Bind();
}

void GLContext::BindIndexBuffer(IRHIBuffer* buffer, IndexType indexType) {
	currentIndexType = indexType;
	if (buffer) buffer->Bind();
}

void GLContext::BindUniformBuffer(IRHIBuffer* buffer, uint32_t bindingPoint) {
	if (buffer) {
		auto* glBuf = static_cast<GLBuffer*>(buffer);
		glBuf->BindRange(bindingPoint, 0, glBuf->GetSize());
	}
}

void GLContext::BindTexture(IRHITexture* texture, uint32_t unit) {
	if (texture) texture->Bind(unit);
}

void GLContext::BindShader(IRHIShader* shader) {
	if (shader) shader->Bind();
}

// --- Draw commands ---

void GLContext::Draw(PrimitiveType primitive, uint32_t vertexCount, uint32_t firstVertex) {
	glDrawArrays(ToGLPrimitive(primitive), firstVertex, vertexCount);
}

void GLContext::DrawIndexed(PrimitiveType primitive, uint32_t indexCount, uint32_t firstIndex, int32_t vertexOffset) {
	const GLenum glIdxType = ToGLIndexType(currentIndexType);
	const void* offset = reinterpret_cast<const void*>(static_cast<uintptr_t>(firstIndex * IndexTypeSize(currentIndexType)));

	if (vertexOffset != 0) {
		glDrawElementsBaseVertex(ToGLPrimitive(primitive), indexCount, glIdxType, offset, vertexOffset);
	} else {
		glDrawElements(ToGLPrimitive(primitive), indexCount, glIdxType, offset);
	}
}

void GLContext::DrawInstanced(PrimitiveType primitive, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t /*firstInstance*/) {
	glDrawArraysInstanced(ToGLPrimitive(primitive), firstVertex, vertexCount, instanceCount);
}

void GLContext::DrawIndexedInstanced(PrimitiveType primitive, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t /*firstInstance*/) {
	const GLenum glIdxType = ToGLIndexType(currentIndexType);
	const void* offset = reinterpret_cast<const void*>(static_cast<uintptr_t>(firstIndex * IndexTypeSize(currentIndexType)));
	glDrawElementsInstancedBaseVertex(ToGLPrimitive(primitive), indexCount, glIdxType, offset, instanceCount, vertexOffset);
}

void GLContext::DrawIndirect(PrimitiveType primitive, IRHIBuffer* buffer, size_t offset, uint32_t drawCount, uint32_t stride) {
	if (!buffer) return;
	buffer->Bind();
	glMultiDrawArraysIndirect(ToGLPrimitive(primitive), reinterpret_cast<const void*>(offset), drawCount, stride);
}

void GLContext::DrawIndexedIndirect(PrimitiveType primitive, IRHIBuffer* buffer, size_t offset, uint32_t drawCount, uint32_t stride, IndexType indexType) {
	if (!buffer) return;
	buffer->Bind();
	glMultiDrawElementsIndirect(ToGLPrimitive(primitive), ToGLIndexType(indexType), reinterpret_cast<const void*>(offset), drawCount, stride);
}

// --- Viewport / Scissor ---

void GLContext::SetViewport(const Viewport& vp) {
	glViewport(static_cast<GLint>(vp.x), static_cast<GLint>(vp.y),
	           static_cast<GLsizei>(vp.width), static_cast<GLsizei>(vp.height));
	glDepthRange(vp.minDepth, vp.maxDepth);
}

void GLContext::SetScissor(const ScissorRect& rect) {
	glScissor(rect.x, rect.y, rect.width, rect.height);
}

void GLContext::SetClipDistanceEnabled(uint32_t index, bool enabled) {
	if (index >= IRHIContext::MaxClipDistances) return;
	if (enabled)
		glEnable(GL_CLIP_DISTANCE0 + index);
	else
		glDisable(GL_CLIP_DISTANCE0 + index);
}

void GLContext::SetVertexAttribDivisor(uint32_t index, uint32_t divisor) {
	glVertexAttribDivisor(index, divisor);
}

// --- Global state ---

void GLContext::SetDepthTestEnabled(bool enabled) {
	if (enabled)
		glEnable(GL_DEPTH_TEST);
	else
		glDisable(GL_DEPTH_TEST);
}

void GLContext::SetDepthFunc(CompareFunc func) {
	glDepthFunc(ToGLCompareFunc(func));
}

void GLContext::SetClipControl(bool zeroToOne) {
	if (zeroToOne && GLAD_GL_ARB_clip_control)
		glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
}

void GLContext::SetSeamlessCubeMapsEnabled(bool enabled) {
	if (enabled)
		glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
	else
		glDisable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
}

void GLContext::SetMultisampleEnabled(bool enabled) {
	if (enabled)
		glEnable(GL_MULTISAMPLE);
	else
		glDisable(GL_MULTISAMPLE);
}

void GLContext::SetSampleShading(bool enabled, float minRate) {
	if (enabled) {
		glEnable(GL_SAMPLE_SHADING);
		glMinSampleShading(minRate);
	} else {
		glDisable(GL_SAMPLE_SHADING);
	}
}

// --- Clear ---

void GLContext::ClearColor(float r, float g, float b, float a) { glClearColor(r, g, b, a); }
void GLContext::ClearDepth(float depth) { glClearDepth(depth); }
void GLContext::ClearStencil(uint32_t value) { glClearStencil(value); }

void GLContext::Clear(bool color, bool depth, bool stencil) {
	GLbitfield bits = 0;
	if (color)   bits |= GL_COLOR_BUFFER_BIT;
	if (depth)   bits |= GL_DEPTH_BUFFER_BIT;
	if (stencil) bits |= GL_STENCIL_BUFFER_BIT;
	if (bits)    glClear(bits);
}

// --- Blit ---

void GLContext::BlitFramebuffer(
	IRHIFramebuffer* src, IRHIFramebuffer* dst,
	int srcX0, int srcY0, int srcX1, int srcY1,
	int dstX0, int dstY0, int dstX1, int dstY1,
	bool colorBit, bool depthBit)
{
	GLint srcId = src ? src->GetNativeHandle() : 0;
	GLint dstId = dst ? dst->GetNativeHandle() : 0;

	GLbitfield mask = 0;
	if (colorBit) mask |= GL_COLOR_BUFFER_BIT;
	if (depthBit) mask |= GL_DEPTH_BUFFER_BIT;

	FBO::Blit(srcId, dstId,
		{srcX0, srcY0, srcX1, srcY1},
		{dstX0, dstY0, dstX1, dstY1},
		mask, depthBit ? GL_NEAREST : GL_LINEAR);
}

// --- Sync ---

void GLContext::Flush()  { glFlush(); }
void GLContext::Finish() { glFinish(); }

} // namespace RHI
