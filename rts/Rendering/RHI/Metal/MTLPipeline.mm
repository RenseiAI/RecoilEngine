/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#import "MTLPipeline.h"
#import "MTLDevice.h"
#import "MTLShader.h"
#import "MTLFramebuffer.h"

#import <Metal/Metal.h>

#include <signal.h>
#include <setjmp.h>

#include "System/Log/ILog.h"

// Metal's release-mode validation may call abort() for certain vertex
// descriptor / shader combinations that it considers programmer errors.
// Since abort() sends SIGABRT which is not catchable by @try/@catch,
// we use sigsetjmp/siglongjmp to recover from the abort.
static thread_local sigjmp_buf sPSOJumpBuf;
static thread_local bool sPSOGuardActive = false;

static void MTLPSOAbortHandler(int sig) {
	if (sPSOGuardActive) {
		siglongjmp(sPSOJumpBuf, 1);
	}
	// If the guard isn't active, re-raise to get normal crash handling
	signal(SIGABRT, SIG_DFL);
	raise(SIGABRT);
}

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

MTLVertexFormat MTLPipeline::ToMTLVertexFormat(VertexFormat format) {
	switch (format) {
		case VertexFormat::Float1:     return MTLVertexFormatFloat;
		case VertexFormat::Float2:     return MTLVertexFormatFloat2;
		case VertexFormat::Float3:     return MTLVertexFormatFloat3;
		case VertexFormat::Float4:     return MTLVertexFormatFloat4;
		case VertexFormat::UByte4:     return MTLVertexFormatUChar4;
		case VertexFormat::UByte4Norm: return MTLVertexFormatUChar4Normalized;
		case VertexFormat::Short2:     return MTLVertexFormatShort2;
		case VertexFormat::Short2Norm: return MTLVertexFormatShort2Normalized;
		case VertexFormat::Short4:     return MTLVertexFormatShort4;
		case VertexFormat::Short4Norm: return MTLVertexFormatShort4Normalized;
		case VertexFormat::Int1:       return MTLVertexFormatInt;
		case VertexFormat::Int2:       return MTLVertexFormatInt2;
		case VertexFormat::Int3:       return MTLVertexFormatInt3;
		case VertexFormat::Int4:       return MTLVertexFormatInt4;
		case VertexFormat::UInt1:      return MTLVertexFormatUInt;
		case VertexFormat::UInt2:      return MTLVertexFormatUInt2;
		case VertexFormat::UInt3:      return MTLVertexFormatUInt3;
		case VertexFormat::UInt4:      return MTLVertexFormatUInt4;
	}
	return MTLVertexFormatFloat4;
}

size_t MTLPipeline::HashVertexLayout(const VertexLayout& layout) {
	size_t hash = layout.stride;
	hash ^= static_cast<size_t>(layout.attributeCount) << 16;
	for (uint32_t i = 0; i < layout.attributeCount; ++i) {
		const auto& a = layout.attributes[i];
		hash ^= (static_cast<size_t>(a.location) * 2654435761u);
		hash ^= (static_cast<size_t>(a.offset) << 8);
		hash ^= (static_cast<size_t>(a.format) << 24);
		hash ^= (static_cast<size_t>(a.divisor) << 4);
	}
	return hash;
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

void MTLPipeline::UpdateDesc(const PipelineDesc& newDesc) {
	desc = newDesc;
	// PSO cache keys include blend state + pixel formats, so cached entries
	// remain valid even after desc changes. Only depth-stencil and rasterizer
	// state (applied separately to the encoder) need rebuilding.
	CreateDepthStencilState();
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
                                                                MTLPixelFormat depthFormat,
                                                                const VertexLayout* vertexLayout,
                                                                const BlendState* blendOverride,
                                                                uint32_t instanceStride,
                                                                MTLFramebuffer* framebuffer) {
	if (!shader || !shader->IsValid()) {
		return nil;
	}

	// Use dynamic blend override if provided, otherwise pipeline's own blend state
	const BlendState& blend = blendOverride ? *blendOverride : desc.blend;

	// Cache key: shader pointer XOR vertex layout hash XOR blend state hash XOR format hashes
	uintptr_t key = reinterpret_cast<uintptr_t>(shader);
	if (vertexLayout)
		key ^= HashVertexLayout(*vertexLayout);
	// Include blend state in cache key so different blend modes get different PSOs
	{
		uintptr_t blendKey = static_cast<uintptr_t>(blend.enabled);
		blendKey = (blendKey << 4) ^ static_cast<uintptr_t>(blend.srcColor);
		blendKey = (blendKey << 4) ^ static_cast<uintptr_t>(blend.dstColor);
		blendKey = (blendKey << 4) ^ static_cast<uintptr_t>(blend.srcAlpha);
		blendKey = (blendKey << 4) ^ static_cast<uintptr_t>(blend.dstAlpha);
		blendKey = (blendKey << 2) ^ static_cast<uintptr_t>(blend.colorOp);
		blendKey = (blendKey << 2) ^ static_cast<uintptr_t>(blend.alphaOp);
		// colorMask affects PSO write mask — must be included in cache key
		blendKey = (blendKey << 1) ^ static_cast<uintptr_t>(blend.colorMask[0]);
		blendKey = (blendKey << 1) ^ static_cast<uintptr_t>(blend.colorMask[1]);
		blendKey = (blendKey << 1) ^ static_cast<uintptr_t>(blend.colorMask[2]);
		blendKey = (blendKey << 1) ^ static_cast<uintptr_t>(blend.colorMask[3]);
		key ^= (blendKey * 0x9e3779b97f4a7c15ULL); // golden ratio hash mix
	}
	// Include color and depth pixel formats so PSOs for different render targets don't collide
	key ^= (static_cast<uintptr_t>(colorFormat) * 2246822519ULL);
	key ^= (static_cast<uintptr_t>(depthFormat) * 3266489917ULL);
	// Include instance stride so per-vertex vs per-instance layout differences produce distinct PSOs
	key ^= (static_cast<uintptr_t>(instanceStride) * 1640531527ULL);
	// Include MRT color attachment count + per-attachment formats for distinct PSOs
	const uint32_t fboColorCount = framebuffer ? framebuffer->GetColorAttachmentCount() : 1;
	key ^= (static_cast<uintptr_t>(fboColorCount) * 4197999714ULL);
	if (framebuffer && fboColorCount > 1) {
		for (uint32_t i = 1; i < fboColorCount; i++) {
			key ^= (static_cast<uintptr_t>(framebuffer->GetColorPixelFormat(i)) * (2246822519ULL + i));
		}
	}

	auto it = pipelineCache.find(key);
	if (it != pipelineCache.end()) {
		return it->second;
	}

	LOG("[MTLPipeline] PSO cache miss for shader '%s' (colorFmt=%lu depthFmt=%lu colorAttachments=%u vtxLayout=%d blend=%d/%d/%d/%d/%d/%d mask=%d%d%d%d)",
	    shader->GetName().c_str(), (unsigned long)colorFormat, (unsigned long)depthFormat,
	    fboColorCount,
	    vertexLayout ? (int)vertexLayout->attributeCount : -1,
	    (int)blend.enabled, (int)blend.srcColor, (int)blend.dstColor,
	    (int)blend.srcAlpha, (int)blend.dstAlpha, (int)blend.colorOp,
	    (int)blend.colorMask[0], (int)blend.colorMask[1],
	    (int)blend.colorMask[2], (int)blend.colorMask[3]);

	// Validate device
	id<MTLDevice> mtlDevice = device ? device->GetMTLDevice() : nil;
	if (!mtlDevice) {
		LOG_L(L_ERROR, "[MTLPipeline] nil MTLDevice (device=%p)", (void*)device);
		return nil;
	}

	// Create render pipeline descriptor
	MTLRenderPipelineDescriptor* pipelineDesc = [[MTLRenderPipelineDescriptor alloc] init];
	pipelineDesc.label = [NSString stringWithUTF8String:shader->GetName().c_str()];

	// Set shader functions
	id<MTLFunction> vtxFn = shader->GetVertexFunction();
	id<MTLFunction> fragFn = shader->GetFragmentFunction();
	if (!vtxFn) {
		LOG_L(L_ERROR, "[MTLPipeline] nil vertex function for shader '%s'", shader->GetName().c_str());
		return nil;
	}
	pipelineDesc.vertexFunction = vtxFn;
	pipelineDesc.fragmentFunction = fragFn;

	// Color write mask (shared across all color attachments)
	MTLColorWriteMask writeMask = MTLColorWriteMaskNone;
	if (blend.colorMask[0]) writeMask |= MTLColorWriteMaskRed;
	if (blend.colorMask[1]) writeMask |= MTLColorWriteMaskGreen;
	if (blend.colorMask[2]) writeMask |= MTLColorWriteMaskBlue;
	if (blend.colorMask[3]) writeMask |= MTLColorWriteMaskAlpha;

	// Color attachment configuration — MRT support
	// When a framebuffer is provided, configure each color attachment with
	// its actual pixel format. Otherwise use the single colorFormat for attachment 0.
	for (uint32_t i = 0; i < fboColorCount; i++) {
		MTLRenderPipelineColorAttachmentDescriptor* ca = pipelineDesc.colorAttachments[i];
		ca.pixelFormat = (framebuffer && fboColorCount > 1)
			? framebuffer->GetColorPixelFormat(i) : colorFormat;

		if (blend.enabled) {
			ca.blendingEnabled = YES;
			ca.sourceRGBBlendFactor = ToMTLBlendFactor(blend.srcColor);
			ca.destinationRGBBlendFactor = ToMTLBlendFactor(blend.dstColor);
			ca.rgbBlendOperation = ToMTLBlendOp(blend.colorOp);
			ca.sourceAlphaBlendFactor = ToMTLBlendFactor(blend.srcAlpha);
			ca.destinationAlphaBlendFactor = ToMTLBlendFactor(blend.dstAlpha);
			ca.alphaBlendOperation = ToMTLBlendOp(blend.alphaOp);
		} else {
			ca.blendingEnabled = NO;
		}
		ca.writeMask = writeMask;
	}

	// Vertex descriptor — maps RHI VertexLayout to Metal vertex descriptor
	// Use buffer index 30 for vertex data to avoid conflicts with SPIRV-Cross
	// auto-assigned buffer indices (which start from 0 for uniform blocks).
	static constexpr uint32_t kVertexBufferIndex = 30;

	if (vertexLayout && vertexLayout->attributeCount > 0) {
		MTLVertexDescriptor* vd = [[MTLVertexDescriptor alloc] init];

		// Separate per-vertex and per-instance attributes into different buffer layouts
		bool hasPerInstance = false;
		for (uint32_t i = 0; i < vertexLayout->attributeCount; ++i) {
			const auto& attr = vertexLayout->attributes[i];
			MTLVertexFormat mtlFmt = ToMTLVertexFormat(attr.format);
			vd.attributes[attr.location].format = mtlFmt;
			vd.attributes[attr.location].offset = attr.offset;

			if (attr.divisor > 0) {
				vd.attributes[attr.location].bufferIndex = kVertexBufferIndex - 1;
				hasPerInstance = true;
			} else {
				vd.attributes[attr.location].bufferIndex = kVertexBufferIndex;
			}
		}

		vd.layouts[kVertexBufferIndex].stride = vertexLayout->stride;
		vd.layouts[kVertexBufferIndex].stepFunction = MTLVertexStepFunctionPerVertex;
		vd.layouts[kVertexBufferIndex].stepRate = 1;

		if (hasPerInstance) {
			// Use the separate instance stride — the per-instance buffer (index 29)
			// has a different stride from the per-vertex buffer (index 30).
			// If instanceStride wasn't provided, fall back to vertexLayout->stride
			// (old behavior) to avoid zero-stride which Metal rejects.
			const uint32_t instStride = (instanceStride > 0) ? instanceStride : vertexLayout->stride;
			vd.layouts[kVertexBufferIndex - 1].stride = instStride;
			vd.layouts[kVertexBufferIndex - 1].stepFunction = MTLVertexStepFunctionPerInstance;
			vd.layouts[kVertexBufferIndex - 1].stepRate = 1;
		}

		pipelineDesc.vertexDescriptor = vd;
	}

	// Depth format
	pipelineDesc.depthAttachmentPixelFormat = depthFormat;

	// Pre-validate: Metal may abort() (SIGABRT, not catchable by @try/@catch)
	// when the vertex descriptor + shader combination is invalid.
	if (vertexLayout && vertexLayout->attributeCount > 0 && vtxFn) {
		NSArray<MTLAttribute*>* stageInputs = vtxFn.stageInputAttributes;

		if (!stageInputs || stageInputs.count == 0) {
			// Shader doesn't use [[stage_in]] — omit vertex descriptor
			pipelineDesc.vertexDescriptor = nil;
		} else {
			// Check that all active stage inputs have matching vertex descriptor entries
			NSMutableSet<NSNumber*>* configuredLocs = [NSMutableSet set];
			for (uint32_t i = 0; i < vertexLayout->attributeCount; ++i) {
				[configuredLocs addObject:@(vertexLayout->attributes[i].location)];
			}

			for (MTLAttribute* attr in stageInputs) {
				if (attr.isActive && ![configuredLocs containsObject:@((uint32_t)attr.attributeIndex)]) {
					LOG_L(L_WARNING, "[MTLPipeline] Shader '%s' vtx func requires attr loc %lu "
					      "not in layout (%u attrs) — skipping PSO",
					      shader->GetName().c_str(), (unsigned long)attr.attributeIndex,
					      vertexLayout->attributeCount);
					pipelineCache[key] = nil;
					return nil;
				}
			}
		}
	}

	// Compile the pipeline.
	// Metal may abort() for certain shader/descriptor combos. We protect
	// against this with a SIGABRT handler + sigsetjmp/siglongjmp, plus
	// @try/@catch for ObjC exceptions.
	NSError* error = nil;
	id<MTLRenderPipelineState> pipelineState = nil;

	// Install SIGABRT guard
	struct sigaction oldAction;
	struct sigaction newAction;
	memset(&newAction, 0, sizeof(newAction));
	newAction.sa_handler = MTLPSOAbortHandler;
	sigemptyset(&newAction.sa_mask);
	newAction.sa_flags = 0;
	sigaction(SIGABRT, &newAction, &oldAction);
	sPSOGuardActive = true;

	if (sigsetjmp(sPSOJumpBuf, 1) == 0) {
		@try {
			pipelineState = [mtlDevice newRenderPipelineStateWithDescriptor:pipelineDesc
			                                                          error:&error];
		} @catch (NSException* exception) {
			LOG_L(L_ERROR, "[MTLPipeline] Exception creating PSO for %s: %s — %s",
			      shader->GetName().c_str(),
			      [[exception name] UTF8String],
			      [[exception reason] UTF8String]);
			pipelineState = nil;
		}
	} else {
		// Recovered from SIGABRT — Metal aborted during PSO creation
		LOG_L(L_ERROR, "[MTLPipeline] Metal aborted during PSO creation for '%s' "
		      "(vtxLayout=%u stride=%u) — recovered via signal handler",
		      shader->GetName().c_str(),
		      vertexLayout ? vertexLayout->attributeCount : 0,
		      vertexLayout ? vertexLayout->stride : 0);
		pipelineState = nil;
		// Cache the failure to prevent repeated aborts for the same combo
		sPSOGuardActive = false;
		sigaction(SIGABRT, &oldAction, nullptr);
		pipelineCache[key] = nil;
		return nil;
	}

	// Restore original signal handler
	sPSOGuardActive = false;
	sigaction(SIGABRT, &oldAction, nullptr);

	if (!pipelineState) {
		LOG_L(L_ERROR, "[MTLPipeline] Failed to create PSO for %s: %s",
		      shader->GetName().c_str(),
		      error ? [[error localizedDescription] UTF8String] : "unknown error");
		// Dump vertex attribute details to help diagnose format mismatches
		if (vertexLayout && vertexLayout->attributeCount > 0) {
			LOG_L(L_ERROR, "[MTLPipeline]   vertexStride=%u instanceStride=%u attribCount=%u",
			      vertexLayout->stride, instanceStride, vertexLayout->attributeCount);
			for (uint32_t i = 0; i < vertexLayout->attributeCount; ++i) {
				const auto& a = vertexLayout->attributes[i];
				LOG_L(L_ERROR, "[MTLPipeline]   attr[%u]: loc=%u fmt=%d offset=%u divisor=%u → bufIdx=%u",
				      i, a.location, (int)a.format, a.offset, a.divisor,
				      (a.divisor > 0) ? (kVertexBufferIndex - 1) : kVertexBufferIndex);
			}
		}
		pipelineCache[key] = nil;
		return nil;
	}

	LOG("[MTLPipeline] PSO created OK for shader '%s' (key=0x%lx)",
	    shader->GetName().c_str(), (unsigned long)key);

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
