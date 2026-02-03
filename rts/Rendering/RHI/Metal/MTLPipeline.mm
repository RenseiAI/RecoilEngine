/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#import "MTLPipeline.h"
#import "MTLDevice.h"
#import "MTLShader.h"

#import <Metal/Metal.h>

#include "System/Log/ILog.h"

namespace RHI {

MTLBlendFactor MTLPipeline::ToMTLBlendFactor(BlendFactor factor) {
	switch (factor) {
		case BlendFactor::Zero:                  return MTLBlendFactorZero;
		case BlendFactor::One:                   return MTLBlendFactorOne;
		case BlendFactor::SrcColor:              return MTLBlendFactorSourceColor;
		case BlendFactor::OneMinusSrcColor:      return MTLBlendFactorOneMinusSourceColor;
		case BlendFactor::DstColor:              return MTLBlendFactorDestinationColor;
		case BlendFactor::OneMinusDstColor:      return MTLBlendFactorOneMinusDestinationColor;
		case BlendFactor::SrcAlpha:              return MTLBlendFactorSourceAlpha;
		case BlendFactor::OneMinusSrcAlpha:      return MTLBlendFactorOneMinusSourceAlpha;
		case BlendFactor::DstAlpha:              return MTLBlendFactorDestinationAlpha;
		case BlendFactor::OneMinusDstAlpha:      return MTLBlendFactorOneMinusDestinationAlpha;
		case BlendFactor::ConstantColor:         return MTLBlendFactorBlendColor;
		case BlendFactor::OneMinusConstantColor: return MTLBlendFactorOneMinusBlendColor;
		case BlendFactor::ConstantAlpha:         return MTLBlendFactorBlendAlpha;
		case BlendFactor::OneMinusConstantAlpha: return MTLBlendFactorOneMinusBlendAlpha;
		case BlendFactor::SrcAlphaSaturate:      return MTLBlendFactorSourceAlphaSaturated;
	}
	return MTLBlendFactorOne;
}

MTLBlendOperation MTLPipeline::ToMTLBlendOp(BlendOp op) {
	switch (op) {
		case BlendOp::Add:             return MTLBlendOperationAdd;
		case BlendOp::Subtract:        return MTLBlendOperationSubtract;
		case BlendOp::ReverseSubtract: return MTLBlendOperationReverseSubtract;
		case BlendOp::Min:             return MTLBlendOperationMin;
		case BlendOp::Max:             return MTLBlendOperationMax;
	}
	return MTLBlendOperationAdd;
}

MTLCompareFunction MTLPipeline::ToMTLCompareFunc(CompareFunc func) {
	switch (func) {
		case CompareFunc::Never:        return MTLCompareFunctionNever;
		case CompareFunc::Less:         return MTLCompareFunctionLess;
		case CompareFunc::LessEqual:    return MTLCompareFunctionLessEqual;
		case CompareFunc::Equal:        return MTLCompareFunctionEqual;
		case CompareFunc::NotEqual:     return MTLCompareFunctionNotEqual;
		case CompareFunc::GreaterEqual: return MTLCompareFunctionGreaterEqual;
		case CompareFunc::Greater:      return MTLCompareFunctionGreater;
		case CompareFunc::Always:       return MTLCompareFunctionAlways;
	}
	return MTLCompareFunctionLess;
}

MTLStencilOperation MTLPipeline::ToMTLStencilOp(StencilOp op) {
	switch (op) {
		case StencilOp::Keep:      return MTLStencilOperationKeep;
		case StencilOp::Zero:      return MTLStencilOperationZero;
		case StencilOp::Replace:   return MTLStencilOperationReplace;
		case StencilOp::IncrClamp: return MTLStencilOperationIncrementClamp;
		case StencilOp::DecrClamp: return MTLStencilOperationDecrementClamp;
		case StencilOp::Invert:    return MTLStencilOperationInvert;
		case StencilOp::IncrWrap:  return MTLStencilOperationIncrementWrap;
		case StencilOp::DecrWrap:  return MTLStencilOperationDecrementWrap;
	}
	return MTLStencilOperationKeep;
}

MTLCullMode MTLPipeline::ToMTLCullMode(CullMode mode) {
	switch (mode) {
		case CullMode::None:  return MTLCullModeNone;
		case CullMode::Front: return MTLCullModeFront;
		case CullMode::Back:  return MTLCullModeBack;
	}
	return MTLCullModeBack;
}

MTLWinding MTLPipeline::ToMTLWinding(FrontFace face) {
	// Metal and OpenGL have opposite conventions:
	// OpenGL default CCW = front face
	// Metal default CW = front face
	switch (face) {
		case FrontFace::CounterClockwise: return MTLWindingCounterClockwise;
		case FrontFace::Clockwise:        return MTLWindingClockwise;
	}
	return MTLWindingCounterClockwise;
}

MTLTriangleFillMode MTLPipeline::ToMTLFillMode(PolygonMode mode) {
	switch (mode) {
		case PolygonMode::Fill:  return MTLTriangleFillModeFill;
		case PolygonMode::Line:  return MTLTriangleFillModeLines;
		case PolygonMode::Point: return MTLTriangleFillModeLines;  // Point mode not directly supported
	}
	return MTLTriangleFillModeFill;
}

MTLPipeline::MTLPipeline(MTLDevice* device, const PipelineDesc& desc)
	: device(device)
	, desc(desc)
{
	if (!device || !device->IsValid()) {
		LOG_L(L_ERROR, "[MTLPipeline] Cannot create pipeline: invalid device");
		return;
	}

	// Create depth-stencil state
	CreateDepthStencilState();
}

MTLPipeline::~MTLPipeline() {
	pipelineCache.clear();
	depthStencilState = nil;
}

void MTLPipeline::CreateDepthStencilState() {
	MTLDepthStencilDescriptor* dsDesc = [[MTLDepthStencilDescriptor alloc] init];

	// Depth state
	dsDesc.depthWriteEnabled = desc.depthStencil.depthWriteEnabled;
	if (desc.depthStencil.depthTestEnabled) {
		dsDesc.depthCompareFunction = ToMTLCompareFunc(desc.depthStencil.depthFunc);
	} else {
		dsDesc.depthCompareFunction = MTLCompareFunctionAlways;
	}

	// Stencil state
	if (desc.depthStencil.stencilEnabled) {
		MTLStencilDescriptor* stencilDesc = [[MTLStencilDescriptor alloc] init];
		stencilDesc.stencilCompareFunction = ToMTLCompareFunc(desc.depthStencil.stencilFunc);
		stencilDesc.stencilFailureOperation = ToMTLStencilOp(desc.depthStencil.stencilFailOp);
		stencilDesc.depthFailureOperation = ToMTLStencilOp(desc.depthStencil.stencilDepthFailOp);
		stencilDesc.depthStencilPassOperation = ToMTLStencilOp(desc.depthStencil.stencilPassOp);
		stencilDesc.readMask = desc.depthStencil.stencilReadMask;
		stencilDesc.writeMask = desc.depthStencil.stencilWriteMask;

		// Use same settings for front and back
		dsDesc.frontFaceStencil = stencilDesc;
		dsDesc.backFaceStencil = stencilDesc;
	}

	depthStencilState = [device->GetMTLDevice() newDepthStencilStateWithDescriptor:dsDesc];
}

id<MTLRenderPipelineState> MTLPipeline::GetRenderPipelineState(MTLShader* shader,
                                                                MTLPixelFormat colorFormat,
                                                                MTLPixelFormat depthFormat) {
	if (!shader || !shader->IsValid()) {
		return nil;
	}

	// Check cache
	uintptr_t key = reinterpret_cast<uintptr_t>(shader);
	auto it = pipelineCache.find(key);
	if (it != pipelineCache.end()) {
		return it->second;
	}

	// Create render pipeline descriptor
	MTLRenderPipelineDescriptor* pipelineDesc = [[MTLRenderPipelineDescriptor alloc] init];

	// Set shader functions
	pipelineDesc.vertexFunction = shader->GetVertexFunction();
	pipelineDesc.fragmentFunction = shader->GetFragmentFunction();

	// Color attachment configuration
	MTLRenderPipelineColorAttachmentDescriptor* colorAttachment = pipelineDesc.colorAttachments[0];
	colorAttachment.pixelFormat = colorFormat;

	// Blend state
	if (desc.blend.enabled) {
		colorAttachment.blendingEnabled = YES;
		colorAttachment.sourceRGBBlendFactor = ToMTLBlendFactor(desc.blend.srcColor);
		colorAttachment.destinationRGBBlendFactor = ToMTLBlendFactor(desc.blend.dstColor);
		colorAttachment.rgbBlendOperation = ToMTLBlendOp(desc.blend.colorOp);
		colorAttachment.sourceAlphaBlendFactor = ToMTLBlendFactor(desc.blend.srcAlpha);
		colorAttachment.destinationAlphaBlendFactor = ToMTLBlendFactor(desc.blend.dstAlpha);
		colorAttachment.alphaBlendOperation = ToMTLBlendOp(desc.blend.alphaOp);
	} else {
		colorAttachment.blendingEnabled = NO;
	}

	// Color write mask
	MTLColorWriteMask writeMask = MTLColorWriteMaskNone;
	if (desc.blend.colorMask[0]) writeMask |= MTLColorWriteMaskRed;
	if (desc.blend.colorMask[1]) writeMask |= MTLColorWriteMaskGreen;
	if (desc.blend.colorMask[2]) writeMask |= MTLColorWriteMaskBlue;
	if (desc.blend.colorMask[3]) writeMask |= MTLColorWriteMaskAlpha;
	colorAttachment.writeMask = writeMask;

	// Depth format
	pipelineDesc.depthAttachmentPixelFormat = depthFormat;

	// Compile the pipeline
	NSError* error = nil;
	id<MTLRenderPipelineState> pipelineState =
		[device->GetMTLDevice() newRenderPipelineStateWithDescriptor:pipelineDesc
		                                                       error:&error];

	if (!pipelineState) {
		LOG_L(L_ERROR, "[MTLPipeline] Failed to create render pipeline state: %s",
		      [[error localizedDescription] UTF8String]);
		return nil;
	}

	// Cache and return
	pipelineCache[key] = pipelineState;
	return pipelineState;
}

void MTLPipeline::ApplyRasterizerState(id<MTLRenderCommandEncoder> encoder) {
	if (!encoder) {
		return;
	}

	// Cull mode and front face
	[encoder setCullMode:ToMTLCullMode(desc.rasterizer.cullMode)];
	[encoder setFrontFacingWinding:ToMTLWinding(desc.rasterizer.frontFace)];

	// Fill mode (wireframe)
	[encoder setTriangleFillMode:ToMTLFillMode(desc.rasterizer.polygonMode)];

	// Depth bias (polygon offset)
	if (desc.rasterizer.polygonOffsetEnabled) {
		[encoder setDepthBias:desc.rasterizer.polygonOffsetUnits
		           slopeScale:desc.rasterizer.polygonOffsetFactor
		               clamp:0.0f];
	} else {
		[encoder setDepthBias:0.0f slopeScale:0.0f clamp:0.0f];
	}

	// Depth clip mode
	if (@available(macOS 10.11, *)) {
		MTLDepthClipMode clipMode = desc.rasterizer.depthClampEnabled
			? MTLDepthClipModeClamp
			: MTLDepthClipModeClip;
		[encoder setDepthClipMode:clipMode];
	}
}

void MTLPipeline::Bind() {
	// In Metal, pipeline state is applied to the command encoder, not globally.
	// This method is a no-op; actual binding happens in MTLContext when encoding commands.
}

} // namespace RHI
