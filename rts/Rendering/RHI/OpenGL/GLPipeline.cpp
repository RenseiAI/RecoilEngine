/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#include "GLPipeline.h"
#include "Rendering/GL/myGL.h"

namespace RHI {

// --- Conversion helpers ---

static GLenum ToGLBlendFactor(BlendFactor f) {
	switch (f) {
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

static GLenum ToGLCompareFunc(CompareFunc f) {
	switch (f) {
		case CompareFunc::Never:        return GL_NEVER;
		case CompareFunc::Less:         return GL_LESS;
		case CompareFunc::LessEqual:    return GL_LEQUAL;
		case CompareFunc::Equal:        return GL_EQUAL;
		case CompareFunc::NotEqual:     return GL_NOTEQUAL;
		case CompareFunc::GreaterEqual: return GL_GEQUAL;
		case CompareFunc::Greater:      return GL_GREATER;
		case CompareFunc::Always:       return GL_ALWAYS;
	}
	return GL_LESS;
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

static GLenum ToGLPolygonMode(PolygonMode m) {
	switch (m) {
		case PolygonMode::Fill:  return GL_FILL;
		case PolygonMode::Line:  return GL_LINE;
		case PolygonMode::Point: return GL_POINT;
	}
	return GL_FILL;
}

// --- GLPipeline ---

GLPipeline::GLPipeline(const PipelineDesc& d)
	: desc(d)
{
}

void GLPipeline::Bind() {
	// --- Blend state ---
	const auto& blend = desc.blend;
	if (blend.enabled) {
		glEnable(GL_BLEND);
		glBlendFuncSeparate(
			ToGLBlendFactor(blend.srcColor), ToGLBlendFactor(blend.dstColor),
			ToGLBlendFactor(blend.srcAlpha), ToGLBlendFactor(blend.dstAlpha));
		glBlendEquationSeparate(ToGLBlendOp(blend.colorOp), ToGLBlendOp(blend.alphaOp));
	} else {
		glDisable(GL_BLEND);
	}
	glColorMask(blend.colorMask[0], blend.colorMask[1], blend.colorMask[2], blend.colorMask[3]);

	// --- Depth/Stencil state ---
	const auto& ds = desc.depthStencil;
	if (ds.depthTestEnabled) {
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(ToGLCompareFunc(ds.depthFunc));
	} else {
		glDisable(GL_DEPTH_TEST);
	}
	glDepthMask(ds.depthWriteEnabled ? GL_TRUE : GL_FALSE);

	if (ds.stencilEnabled) {
		glEnable(GL_STENCIL_TEST);
		glStencilFunc(ToGLCompareFunc(ds.stencilFunc), ds.stencilRef, ds.stencilReadMask);
		glStencilMask(ds.stencilWriteMask);
		glStencilOp(ToGLStencilOp(ds.stencilFailOp), ToGLStencilOp(ds.stencilDepthFailOp), ToGLStencilOp(ds.stencilPassOp));
	} else {
		glDisable(GL_STENCIL_TEST);
	}

	// --- Rasterizer state ---
	const auto& rast = desc.rasterizer;

	if (rast.cullMode != CullMode::None) {
		glEnable(GL_CULL_FACE);
		glCullFace(rast.cullMode == CullMode::Front ? GL_FRONT : GL_BACK);
	} else {
		glDisable(GL_CULL_FACE);
	}
	glFrontFace(rast.frontFace == FrontFace::CounterClockwise ? GL_CCW : GL_CW);

	glPolygonMode(GL_FRONT_AND_BACK, ToGLPolygonMode(rast.polygonMode));

	if (rast.scissorEnabled)
		glEnable(GL_SCISSOR_TEST);
	else
		glDisable(GL_SCISSOR_TEST);

	if (rast.depthClampEnabled)
		glEnable(GL_DEPTH_CLAMP);
	else
		glDisable(GL_DEPTH_CLAMP);

	if (rast.polygonOffsetFactor != 0.0f || rast.polygonOffsetUnits != 0.0f) {
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(rast.polygonOffsetFactor, rast.polygonOffsetUnits);
	} else {
		glDisable(GL_POLYGON_OFFSET_FILL);
	}

	glLineWidth(rast.lineWidth);
}

} // namespace RHI
