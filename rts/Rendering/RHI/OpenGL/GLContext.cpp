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

static GLenum ToGLBlendFactor(BlendFactor factor) {
	switch (factor) {
		case BlendFactor::Zero:                  return GL_ZERO;
		case BlendFactor::One:                   return GL_ONE;
		case BlendFactor::SrcColor:              return GL_SRC_COLOR;
		case BlendFactor::OneMinusSrcColor:      return GL_ONE_MINUS_SRC_COLOR;
		case BlendFactor::DstColor:              return GL_DST_COLOR;
		case BlendFactor::OneMinusDstColor:      return GL_ONE_MINUS_DST_COLOR;
		case BlendFactor::SrcAlpha:              return GL_SRC_ALPHA;
		case BlendFactor::OneMinusSrcAlpha:      return GL_ONE_MINUS_SRC_ALPHA;
		case BlendFactor::DstAlpha:              return GL_DST_ALPHA;
		case BlendFactor::OneMinusDstAlpha:      return GL_ONE_MINUS_DST_ALPHA;
		case BlendFactor::ConstantColor:         return GL_CONSTANT_COLOR;
		case BlendFactor::OneMinusConstantColor: return GL_ONE_MINUS_CONSTANT_COLOR;
		case BlendFactor::ConstantAlpha:         return GL_CONSTANT_ALPHA;
		case BlendFactor::OneMinusConstantAlpha: return GL_ONE_MINUS_CONSTANT_ALPHA;
		case BlendFactor::SrcAlphaSaturate:      return GL_SRC_ALPHA_SATURATE;
	}
	return GL_ONE;
}

static GLenum ToGLCullMode(CullMode mode) {
	switch (mode) {
		case CullMode::None:  return GL_BACK; // dummy value, culling will be disabled
		case CullMode::Front: return GL_FRONT;
		case CullMode::Back:  return GL_BACK;
	}
	return GL_BACK;
}

static GLenum ToGLStencilOp(StencilOp op) {
	switch (op) {
		case StencilOp::Keep:      return GL_KEEP;
		case StencilOp::Zero:      return GL_ZERO;
		case StencilOp::Replace:   return GL_REPLACE;
		case StencilOp::IncrClamp: return GL_INCR;
		case StencilOp::DecrClamp: return GL_DECR;
		case StencilOp::Invert:    return GL_INVERT;
		case StencilOp::IncrWrap:  return GL_INCR_WRAP;
		case StencilOp::DecrWrap:  return GL_DECR_WRAP;
	}
	return GL_KEEP;
}

static GLenum ToGLBlendOp(BlendOp op) {
	switch (op) {
		case BlendOp::Add:             return GL_FUNC_ADD;
		case BlendOp::Subtract:        return GL_FUNC_SUBTRACT;
		case BlendOp::ReverseSubtract: return GL_FUNC_REVERSE_SUBTRACT;
		case BlendOp::Min:             return GL_MIN;
		case BlendOp::Max:             return GL_MAX;
	}
	return GL_FUNC_ADD;
}

static GLenum ToGLPolygonMode(PolygonMode mode) {
	switch (mode) {
		case PolygonMode::Fill:  return GL_FILL;
		case PolygonMode::Line:  return GL_LINE;
		case PolygonMode::Point: return GL_POINT;
	}
	return GL_FILL;
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

void GLContext::SetClipPlaneEquation(uint32_t index, const double* equation) {
	if (index >= IRHIContext::MaxClipDistances) return;
	// glClipPlane transforms the equation by the current MV matrix.
	// We want to pass eye-space equations directly, so set MV to identity.
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
	glClipPlane(GL_CLIP_PLANE0 + index, equation);
	glPopMatrix();
}

void GLContext::SetVertexAttribDivisor(uint32_t index, uint32_t divisor) {
	glVertexAttribDivisor(index, divisor);
}

// --- Vertex layout helpers ---

static bool IsIntegerFormat(VertexFormat fmt) {
	switch (fmt) {
		case VertexFormat::Int1:
		case VertexFormat::Int2:
		case VertexFormat::Int3:
		case VertexFormat::Int4:
			return true;
		default:
			return false;
	}
}

static void GetGLFormatInfo(VertexFormat fmt, GLenum& type, GLint& components, GLboolean& normalized) {
	normalized = GL_FALSE;
	switch (fmt) {
		case VertexFormat::Float1:     type = GL_FLOAT;          components = 1; break;
		case VertexFormat::Float2:     type = GL_FLOAT;          components = 2; break;
		case VertexFormat::Float3:     type = GL_FLOAT;          components = 3; break;
		case VertexFormat::Float4:     type = GL_FLOAT;          components = 4; break;
		case VertexFormat::UByte4:     type = GL_UNSIGNED_BYTE;  components = 4; break;
		case VertexFormat::UByte4Norm: type = GL_UNSIGNED_BYTE;  components = 4; normalized = GL_TRUE; break;
		case VertexFormat::Short2:     type = GL_SHORT;          components = 2; break;
		case VertexFormat::Short2Norm: type = GL_SHORT;          components = 2; normalized = GL_TRUE; break;
		case VertexFormat::Short4:     type = GL_SHORT;          components = 4; break;
		case VertexFormat::Short4Norm: type = GL_SHORT;          components = 4; normalized = GL_TRUE; break;
		case VertexFormat::Int1:       type = GL_INT;            components = 1; break;
		case VertexFormat::Int2:       type = GL_INT;            components = 2; break;
		case VertexFormat::Int3:       type = GL_INT;            components = 3; break;
		case VertexFormat::Int4:       type = GL_INT;            components = 4; break;
	}
}

void GLContext::SetVertexLayout(const VertexLayout& layout) {
	// Disable any previously enabled attributes first
	ClearVertexLayout();

	for (uint32_t i = 0; i < layout.attributeCount; ++i) {
		const auto& attr = layout.attributes[i];
		const uint32_t loc = attr.location;

		glEnableVertexAttribArray(loc);

		const void* offset = reinterpret_cast<const void*>(static_cast<uintptr_t>(attr.offset));

		if (IsIntegerFormat(attr.format)) {
			GLenum type; GLint components; GLboolean normalized;
			GetGLFormatInfo(attr.format, type, components, normalized);
			glVertexAttribIPointer(loc, components, type, layout.stride, offset);
		} else {
			GLenum type; GLint components; GLboolean normalized;
			GetGLFormatInfo(attr.format, type, components, normalized);
			glVertexAttribPointer(loc, components, type, normalized, layout.stride, offset);
		}

		if (attr.divisor != 0)
			glVertexAttribDivisor(loc, attr.divisor);

		enabledAttribMask |= (1u << loc);
	}
}

void GLContext::ClearVertexLayout() {
	uint32_t mask = enabledAttribMask;
	while (mask) {
		const uint32_t bit = mask & (~mask + 1); // isolate lowest set bit
		const uint32_t loc = __builtin_ctz(bit);
		glDisableVertexAttribArray(loc);
		glVertexAttribDivisor(loc, 0);
		mask &= ~bit;
	}
	enabledAttribMask = 0;
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
	bool colorBit, bool depthBit, bool filterLinear)
{
	GLint srcId = src ? src->GetNativeHandle() : 0;
	GLint dstId = dst ? dst->GetNativeHandle() : 0;

	GLbitfield mask = 0;
	if (colorBit) mask |= GL_COLOR_BUFFER_BIT;
	if (depthBit) mask |= GL_DEPTH_BUFFER_BIT;

	// Depth blits require GL_NEAREST; color-only blits use the caller's preference
	GLenum filter = (depthBit || !filterLinear) ? GL_NEAREST : GL_LINEAR;
	FBO::Blit(srcId, dstId,
		{srcX0, srcY0, srcX1, srcY1},
		{dstX0, dstY0, dstX1, dstY1},
		mask, filter);
}

// --- Dynamic state ---

void GLContext::SetDepthWriteEnabled(bool enabled) {
	glDepthMask(enabled ? GL_TRUE : GL_FALSE);
}

void GLContext::SetBlendEnabled(bool enabled) {
	if (enabled)
		glEnable(GL_BLEND);
	else
		glDisable(GL_BLEND);
}

void GLContext::SetBlendFunc(BlendFactor src, BlendFactor dst) {
	glBlendFunc(ToGLBlendFactor(src), ToGLBlendFactor(dst));
}

void GLContext::SetBlendFuncSeparate(BlendFactor srcColor, BlendFactor dstColor, BlendFactor srcAlpha, BlendFactor dstAlpha) {
	glBlendFuncSeparate(
		ToGLBlendFactor(srcColor),
		ToGLBlendFactor(dstColor),
		ToGLBlendFactor(srcAlpha),
		ToGLBlendFactor(dstAlpha)
	);
}

void GLContext::SetBlendEquation(BlendOp op) {
	glBlendEquation(ToGLBlendOp(op));
}

void GLContext::SetBlendEquationSeparate(BlendOp colorOp, BlendOp alphaOp) {
	glBlendEquationSeparate(ToGLBlendOp(colorOp), ToGLBlendOp(alphaOp));
}

void GLContext::SetCullFaceEnabled(bool enabled) {
	if (enabled)
		glEnable(GL_CULL_FACE);
	else
		glDisable(GL_CULL_FACE);
}

void GLContext::SetCullFace(CullMode mode) {
	if (mode == CullMode::None) {
		glDisable(GL_CULL_FACE);
	} else {
		glEnable(GL_CULL_FACE);
		glCullFace(ToGLCullMode(mode));
	}
}

void GLContext::SetColorMask(bool r, bool g, bool b, bool a) {
	glColorMask(r ? GL_TRUE : GL_FALSE,
	            g ? GL_TRUE : GL_FALSE,
	            b ? GL_TRUE : GL_FALSE,
	            a ? GL_TRUE : GL_FALSE);
}

void GLContext::SetPolygonOffset(bool enabled, float factor, float units) {
	if (enabled) {
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(factor, units);
	} else {
		glDisable(GL_POLYGON_OFFSET_FILL);
	}
}

void GLContext::SetLineWidth(float width) {
	glLineWidth(width);
}

void GLContext::SetPointSize(float size) {
	glPointSize(size);
	if (size > 0.0f)
		glEnable(GL_PROGRAM_POINT_SIZE);
	else
		glDisable(GL_PROGRAM_POINT_SIZE);
}

void GLContext::SetPolygonMode(PolygonMode mode) {
	glPolygonMode(GL_FRONT_AND_BACK, ToGLPolygonMode(mode));
}

void GLContext::SetStencilTestEnabled(bool enabled) {
	if (enabled)
		glEnable(GL_STENCIL_TEST);
	else
		glDisable(GL_STENCIL_TEST);
}

void GLContext::SetStencilFunc(CompareFunc func, int32_t ref, uint32_t mask) {
	glStencilFunc(ToGLCompareFunc(func), ref, mask);
}

void GLContext::SetStencilOp(StencilOp sfail, StencilOp dpfail, StencilOp dppass) {
	glStencilOp(ToGLStencilOp(sfail), ToGLStencilOp(dpfail), ToGLStencilOp(dppass));
}

void GLContext::SetStencilMask(uint32_t mask) {
	glStencilMask(mask);
}

void GLContext::SetDepthClampEnabled(bool enabled) {
	if (enabled)
		glEnable(GL_DEPTH_CLAMP);
	else
		glDisable(GL_DEPTH_CLAMP);
}

void GLContext::SetScissorTestEnabled(bool enabled) {
	if (enabled)
		glEnable(GL_SCISSOR_TEST);
	else
		glDisable(GL_SCISSOR_TEST);
}

void GLContext::SetLogicOpEnabled(bool enabled) {
	if (enabled)
		glEnable(GL_COLOR_LOGIC_OP);
	else
		glDisable(GL_COLOR_LOGIC_OP);
}

void GLContext::SetLogicOp(LogicOp op) {
	static constexpr GLenum glOps[] = {
		GL_CLEAR, GL_AND, GL_AND_REVERSE, GL_COPY,
		GL_AND_INVERTED, GL_NOOP, GL_XOR, GL_OR,
		GL_NOR, GL_EQUIV, GL_INVERT, GL_OR_REVERSE,
		GL_COPY_INVERTED, GL_OR_INVERTED, GL_NAND, GL_SET
	};
	glLogicOp(glOps[static_cast<uint8_t>(op)]);
}

void GLContext::SetProgramPointSizeEnabled(bool enabled) {
	if (enabled)
		glEnable(GL_PROGRAM_POINT_SIZE);
	else
		glDisable(GL_PROGRAM_POINT_SIZE);
}

// --- Sync ---

void GLContext::ReadPixels(int x, int y, int width, int height, uint32_t format, uint32_t type, void* data) {
	glReadPixels(x, y, width, height, format, type, data);
}

void GLContext::Flush()  { glFlush(); }
void GLContext::Finish() { glFinish(); }

} // namespace RHI
