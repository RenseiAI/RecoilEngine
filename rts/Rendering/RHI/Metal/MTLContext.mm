/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#import "MTLContext.h"
#import "MTLDevice.h"
#import "MTLBuffer.h"
#import "MTLTexture.h"
#import "MTLShader.h"
#import "MTLFramebuffer.h"
#import "MTLPipeline.h"

#import <Metal/Metal.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "System/Log/ILog.h"

namespace RHI {

MTLPrimitiveType MTLContext::ToMTLPrimitiveType(PrimitiveType type) {
	switch (type) {
		case PrimitiveType::Points:        return MTLPrimitiveTypePoint;
		case PrimitiveType::Lines:         return MTLPrimitiveTypeLine;
		case PrimitiveType::LineStrip:     return MTLPrimitiveTypeLineStrip;
		case PrimitiveType::Triangles:     return MTLPrimitiveTypeTriangle;
		case PrimitiveType::TriangleStrip: return MTLPrimitiveTypeTriangleStrip;
		case PrimitiveType::TriangleFan:   return MTLPrimitiveTypeTriangle;  // Fan not supported, use triangles
	}
	return MTLPrimitiveTypeTriangle;
}

MTLIndexType MTLContext::ToMTLIndexType(IndexType type) {
	switch (type) {
		case IndexType::UInt16: return MTLIndexTypeUInt16;
		case IndexType::UInt32: return MTLIndexTypeUInt32;
	}
	return MTLIndexTypeUInt32;
}

MTLContext::MTLContext(MTLDevice* device)
	: device(device)
{
	// Create semaphore for triple buffering
	frameSemaphore = dispatch_semaphore_create(MaxFramesInFlight);
}

MTLContext::~MTLContext() {
	// Wait for all in-flight frames to complete
	for (int i = 0; i < MaxFramesInFlight; i++) {
		dispatch_semaphore_wait(frameSemaphore, DISPATCH_TIME_FOREVER);
	}

	renderEncoder = nil;
	commandBuffer = nil;

	// Release blit pipeline resources
	blitLibrary = nil;
	blitPSO = nil;
	blitSamplerLinear = nil;
	blitSamplerNearest = nil;
}

void MTLContext::EnsureCommandBuffer() {
	if (!commandBuffer) {
		commandBuffer = [device->GetCommandQueue() commandBuffer];
		commandBuffer.label = @"RHI Frame Command Buffer";
	}
}

void MTLContext::EnsureRenderEncoder() {
	if (!renderEncoder) {
		// Auto-begin default render pass when drawing without an explicit pass.
		// The game rendering path (CGame::Draw -> WorldDrawer) doesn't explicitly
		// call BeginDefaultRenderPass — it just sets up state and draws.
		// This also handles the case where a Lua widget's RenderToTexture ended
		// a custom FBO render pass, leaving no active encoder.
		RenderPassDesc passDesc;
		passDesc.colorAttachmentCount = 1;
		if (pendingColorClear) {
			passDesc.colorAttachments[0].loadAction = LoadAction::Clear;
			passDesc.colorAttachments[0].clearColor = clearColor;
		} else {
			passDesc.colorAttachments[0].loadAction = LoadAction::Load;
		}
		BeginDefaultRenderPass(passDesc);

		if (!renderEncoder) {
			LOG_L(L_ERROR, "[MTLContext] EnsureRenderEncoder: BeginDefaultRenderPass failed to create encoder");
		}
	}
}

void MTLContext::BeginFrame() {
	// Wait for a frame slot to become available
	dispatch_semaphore_wait(frameSemaphore, DISPATCH_TIME_FOREVER);

	// Create new command buffer for this frame
	EnsureCommandBuffer();
}

void MTLContext::EndFrame() {
	@autoreleasepool {
		// Log per-frame draw totals
		static int frameCount = 0;
		if (frameCount < 5 || frameCount % 60 == 0) {
			LOG("[MTL-Frame] frame=%d draws=%u drawIdx=%u drawInst=%u drawIdxInst=%u",
			    frameCount, frameDrawCount, frameDrawIdxCount,
			    frameDrawInstCount, frameDrawIdxInstCount);
		}
		frameCount++;
		frameDrawCount = 0;
		frameDrawIdxCount = 0;
		frameDrawInstCount = 0;
		frameDrawIdxInstCount = 0;

		// End any active render pass
		if (inRenderPass) {
			EndRenderPass();
		}

		if (!commandBuffer) {
			// Still release the drawable even if no command buffer
			device->ClearCurrentDrawable();
			return;
		}

		// Present the drawable
		id<CAMetalDrawable> drawable = device->GetCurrentDrawable();
		if (drawable) {
			[commandBuffer presentDrawable:drawable];
		}

		// Add completion handler to signal semaphore
		__block dispatch_semaphore_t blockSemaphore = frameSemaphore;
		[commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> buffer) {
			(void)buffer;
			dispatch_semaphore_signal(blockSemaphore);
		}];

		// Commit the command buffer
		[commandBuffer commit];

		// Clear state for next frame — drawable must be released so the layer
		// can recycle it; each CAMetalDrawable can only be presented once.
		commandBuffer = nil;
		renderEncoder = nil;
		device->ClearCurrentDrawable();
	}
}

void MTLContext::BeginRenderPass(IRHIFramebuffer* framebuffer, const RenderPassDesc& desc) {
	if (inRenderPass) {
		EndRenderPass();
	}

	EnsureCommandBuffer();

	currentFramebuffer = static_cast<MTLFramebuffer*>(framebuffer);
	currentPassDesc = desc;

	// Set hasDepth based on whether the FBO has a depth attachment
	if (currentFramebuffer && currentFramebuffer->GetDepthPixelFormat() != MTLPixelFormatInvalid) {
		currentPassDesc.hasDepth = true;
	}

	// Create render pass descriptor
	MTLRenderPassDescriptor* rpDesc = nil;
	if (currentFramebuffer) {
		rpDesc = currentFramebuffer->CreateRenderPassDescriptor(currentPassDesc);
	}

	if (!rpDesc) {
		LOG_L(L_ERROR, "[MTLContext] Failed to create render pass descriptor");
		return;
	}

	// Create render command encoder
	renderEncoder = [commandBuffer renderCommandEncoderWithDescriptor:rpDesc];
	renderEncoder.label = @"RHI Render Pass";

	inRenderPass = true;

	// Apply current viewport if set
	if (currentViewport.width > 0 && currentViewport.height > 0) {
		SetViewport(currentViewport);
	}
}

void MTLContext::BeginDefaultRenderPass(const RenderPassDesc& desc) {
	if (inRenderPass) {
		EndRenderPass();
	}

	EnsureCommandBuffer();

	currentFramebuffer = nullptr;
	currentPassDesc = desc;
	currentPassDesc.hasDepth = true;  // Default render pass always has depth

	// Create render pass descriptor for the screen
	MTLRenderPassDescriptor* rpDesc = [MTLRenderPassDescriptor renderPassDescriptor];

	// Get the drawable texture
	id<MTLTexture> drawableTexture = device->GetDrawableTexture();
	if (!drawableTexture) {
		LOG_L(L_ERROR, "[MTLContext] No drawable texture available");
		return;
	}

	// Configure color attachment
	rpDesc.colorAttachments[0].texture = drawableTexture;

	if (desc.colorAttachmentCount > 0) {
		switch (desc.colorAttachments[0].loadAction) {
			case LoadAction::Load:
				rpDesc.colorAttachments[0].loadAction = MTLLoadActionLoad;
				break;
			case LoadAction::Clear:
				rpDesc.colorAttachments[0].loadAction = MTLLoadActionClear;
				rpDesc.colorAttachments[0].clearColor = MTLClearColorMake(
					desc.colorAttachments[0].clearColor.r,
					desc.colorAttachments[0].clearColor.g,
					desc.colorAttachments[0].clearColor.b,
					desc.colorAttachments[0].clearColor.a
				);
				pendingColorClear = false;  // consumed — don't re-clear on next pass restart
				break;
			case LoadAction::DontCare:
				rpDesc.colorAttachments[0].loadAction = MTLLoadActionDontCare;
				break;
		}
	} else if (pendingColorClear) {
		rpDesc.colorAttachments[0].loadAction = MTLLoadActionClear;
		rpDesc.colorAttachments[0].clearColor = MTLClearColorMake(
			clearColor.r, clearColor.g, clearColor.b, clearColor.a
		);
		pendingColorClear = false;
	} else {
		rpDesc.colorAttachments[0].loadAction = MTLLoadActionLoad;
	}

	rpDesc.colorAttachments[0].storeAction = MTLStoreActionStore;

	// Create or resize default depth texture to match drawable
	NSUInteger drawW = drawableTexture.width;
	NSUInteger drawH = drawableTexture.height;
	if (!defaultDepthTexture || defaultDepthWidth != drawW || defaultDepthHeight != drawH) {
		MTLTextureDescriptor* depthDesc = [MTLTextureDescriptor
			texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
			width:drawW height:drawH mipmapped:NO];
		depthDesc.usage = MTLTextureUsageRenderTarget;
		depthDesc.storageMode = MTLStorageModePrivate;
		defaultDepthTexture = [device->GetMTLDevice() newTextureWithDescriptor:depthDesc];
		defaultDepthTexture.label = @"Default Depth";
		defaultDepthWidth = static_cast<uint32_t>(drawW);
		defaultDepthHeight = static_cast<uint32_t>(drawH);
	}

	// Attach depth
	rpDesc.depthAttachment.texture = defaultDepthTexture;
	if (pendingDepthClear) {
		rpDesc.depthAttachment.loadAction = MTLLoadActionClear;
		rpDesc.depthAttachment.clearDepth = clearDepthValue;
		pendingDepthClear = false;
	} else {
		rpDesc.depthAttachment.loadAction = MTLLoadActionClear;
		rpDesc.depthAttachment.clearDepth = 1.0;
	}
	rpDesc.depthAttachment.storeAction = MTLStoreActionStore;

	// Create render command encoder
	renderEncoder = [commandBuffer renderCommandEncoderWithDescriptor:rpDesc];
	renderEncoder.label = @"RHI Default Render Pass";

	inRenderPass = true;

	// Apply current viewport if set
	if (currentViewport.width > 0 && currentViewport.height > 0) {
		SetViewport(currentViewport);
	} else {
		LOG_L(L_WARNING, "[MTLContext] BeginDefaultRenderPass: viewport NOT set (w=%.0f h=%.0f) — Metal will use undefined viewport!",
		      currentViewport.width, currentViewport.height);
	}
}

void MTLContext::EndRenderPass() {
	if (!inRenderPass || !renderEncoder) {
		return;
	}

	[renderEncoder endEncoding];
	renderEncoder = nil;
	inRenderPass = false;
	// Clear framebuffer pointer so that ApplyPipelineState doesn't use
	// stale pixel format info from a custom FBO after ending its pass.
	currentFramebuffer = nullptr;
}

void MTLContext::BindPipeline(IRHIPipeline* pipeline) {
	if (pipeline) {
		// Keep a context-owned copy of the pipeline configuration.
		// Callers typically create local unique_ptr<IRHIPipeline> that gets
		// destroyed after this call (GL applies state immediately, but Metal
		// needs the pipeline object at draw time for PSO creation).
		if (!explicitPipeline) {
			explicitPipeline = std::make_unique<MTLPipeline>(device, pipeline->GetDesc());
		} else {
			explicitPipeline->UpdateDesc(pipeline->GetDesc());
		}
		currentPipeline = explicitPipeline.get();
	} else {
		currentPipeline = nullptr;
	}
}

void MTLContext::BindVertexBuffer(IRHIBuffer* buffer, uint32_t binding) {
	if (binding == 0) {
		currentVertexBuffer = static_cast<MTLBuffer*>(buffer);
	} else if (binding == 1) {
		currentInstanceBuffer = static_cast<MTLBuffer*>(buffer);
	}
	currentVertexBinding = binding;
}

void MTLContext::BindIndexBuffer(IRHIBuffer* buffer, IndexType indexType) {
	currentIndexBuffer = static_cast<MTLBuffer*>(buffer);
	currentIndexType = indexType;
}

void MTLContext::BindUniformBuffer(IRHIBuffer* buffer, uint32_t bindingPoint) {
	if (bindingPoint < MaxUniformBindings) {
		boundUniformBuffers[bindingPoint] = static_cast<MTLBuffer*>(buffer);
	}
}

void MTLContext::BindTexture(IRHITexture* texture, uint32_t unit) {
	if (unit < MaxTextureUnits) {
		boundTextures[unit] = static_cast<MTLTexture*>(texture);
	}
}

void MTLContext::BindShader(IRHIShader* shader) {
	currentShader = static_cast<MTLShader*>(shader);
}

MTLPipeline* MTLContext::GetOrCreateDefaultPipeline() {
	if (!defaultPipeline) {
		PipelineDesc desc;
		desc.depthStencil.depthTestEnabled  = false;
		desc.depthStencil.depthWriteEnabled = false;
		desc.blend.enabled = false;
		desc.blend.colorMask[0] = true;
		desc.blend.colorMask[1] = true;
		desc.blend.colorMask[2] = true;
		desc.blend.colorMask[3] = true;
		desc.rasterizer.cullMode = CullMode::None;
		defaultPipeline = std::make_unique<MTLPipeline>(device, desc);
	}
	return defaultPipeline.get();
}

bool MTLContext::ApplyPipelineState() {
	bool usedDefault = (currentPipeline == nullptr);
	MTLPipeline* pipeline = currentPipeline ? currentPipeline : GetOrCreateDefaultPipeline();
	if (!renderEncoder || !pipeline || !currentShader) {
		return false;
	}

	static int psoCallCount = 0;
	if (psoCallCount++ < 10 || psoCallCount % 5000 == 0) {
		LOG("[MTL-PSO] pipeline=%p (%s) shader='%s' vtxLayout=%d",
		    (void*)pipeline, usedDefault ? "default" : "explicit",
		    currentShader->GetName().c_str(), (int)hasVertexLayout);
	}

	// Get or create the render pipeline state
	MTLPixelFormat colorFormat = MTLPixelFormatBGRA8Unorm;
	MTLPixelFormat depthFormat = MTLPixelFormatInvalid;

	if (currentFramebuffer) {
		colorFormat = currentFramebuffer->GetColorPixelFormat();
		depthFormat = currentFramebuffer->GetDepthPixelFormat();
	} else if (defaultDepthTexture) {
		depthFormat = MTLPixelFormatDepth32Float;
	}

	// Pass dynamic blend state override if it has been set
	const BlendState* blendOverride = dynamicBlendDirty ? &dynamicBlend : nullptr;

	// Set per-vertex stride on the layout (the stride field may have been
	// overwritten by a subsequent SetVertexLayout call for instance attribs).
	// perInstanceStride is passed separately.
	if (hasVertexLayout && perVertexStride > 0) {
		currentVertexLayout.stride = perVertexStride;
	}

	id<MTLRenderPipelineState> pipelineState =
		pipeline->GetRenderPipelineState(currentShader, colorFormat, depthFormat,
		                                 hasVertexLayout ? &currentVertexLayout : nullptr,
		                                 blendOverride, perInstanceStride,
		                                 currentFramebuffer);

	if (!pipelineState) {
		LOG_L(L_ERROR, "[MTL-PSO] GetRenderPipelineState returned nil for shader '%s'",
		      currentShader->GetName().c_str());
		return false;
	}

	@try {
		[renderEncoder setRenderPipelineState:pipelineState];
	} @catch (NSException* exception) {
		LOG_L(L_ERROR, "[MTL-PSO] setRenderPipelineState exception for shader '%s': %s — %s",
		      currentShader->GetName().c_str(),
		      [[exception name] UTF8String], [[exception reason] UTF8String]);
		return false;
	}

	// Apply depth-stencil state with dynamic overrides
	{
		const auto& baseDS = pipeline->GetDesc().depthStencil;
		bool depthTest  = dynamicDepthTestDirty  ? dynamicDepthTest  : baseDS.depthTestEnabled;
		bool depthWrite = dynamicDepthWriteDirty ? dynamicDepthWrite : baseDS.depthWriteEnabled;

		// Metal requires depth test/write to be disabled when the render pass
		// has no depth attachment. Custom FBOs (e.g., Lua gl.RenderToTexture)
		// often have color-only attachments.
		if (depthFormat == MTLPixelFormatInvalid) {
			depthTest = false;
			depthWrite = false;
		}

		if (dynamicDepthTestDirty || dynamicDepthWriteDirty || dynamicDepthFuncDirty
		    || depthFormat == MTLPixelFormatInvalid) {
			// Dynamic depth state overrides or no-depth-attachment override
			CompareFunc depthFunc = dynamicDepthFuncDirty ? dynamicDepthFunc : baseDS.depthFunc;
			MTLDepthStencilDescriptor* dsDesc = [[MTLDepthStencilDescriptor alloc] init];
			dsDesc.depthWriteEnabled = depthWrite;
			dsDesc.depthCompareFunction = depthTest
				? MTLPipeline::ToMTLCompareFunc(depthFunc)
				: MTLCompareFunctionAlways;
			id<MTLDepthStencilState> dsState = [device->GetMTLDevice() newDepthStencilStateWithDescriptor:dsDesc];
			[renderEncoder setDepthStencilState:dsState];
		} else {
			// Use pipeline's cached depth-stencil state
			id<MTLDepthStencilState> dsState = pipeline->GetDepthStencilState();
			if (dsState) {
				[renderEncoder setDepthStencilState:dsState];
			}
		}

		// Periodic diagnostics (show EFFECTIVE state, not just pipeline base)
		static int psoLogCount = 0;
		if (psoLogCount++ % 2000 == 0) {
			const auto& blend = dynamicBlendDirty ? dynamicBlend : pipeline->GetDesc().blend;
			LOG("[MTL-PSO-Detail] shader='%s' depthTest=%d depthWrite=%d blend=%d colorMask=%d%d%d%d (dynDT=%d dynDW=%d dynB=%d)",
			    currentShader->GetName().c_str(),
			    (int)depthTest, (int)depthWrite,
			    (int)blend.enabled,
			    (int)blend.colorMask[0], (int)blend.colorMask[1],
			    (int)blend.colorMask[2], (int)blend.colorMask[3],
			    (int)dynamicDepthTestDirty, (int)dynamicDepthWriteDirty, (int)dynamicBlendDirty);
		}
	}

	// Apply rasterizer state
	@try {
		pipeline->ApplyRasterizerState(renderEncoder);
	} @catch (NSException* exception) {
		LOG_L(L_ERROR, "[MTL-PSO] ApplyRasterizerState exception: %s — %s",
		      [[exception name] UTF8String], [[exception reason] UTF8String]);
		return false;
	}

	return true;
}

// --- Dynamic blend/depth state implementations ---

void MTLContext::SetBlendEnabled(bool enabled) {
	dynamicBlend.enabled = enabled;
	dynamicBlendDirty = true;
}

void MTLContext::SetBlendFunc(BlendFactor src, BlendFactor dst) {
	dynamicBlend.srcColor = src;
	dynamicBlend.dstColor = dst;
	dynamicBlend.srcAlpha = src;
	dynamicBlend.dstAlpha = dst;
	dynamicBlendDirty = true;
}

void MTLContext::SetBlendFuncSeparate(BlendFactor srcColor, BlendFactor dstColor,
                                      BlendFactor srcAlpha, BlendFactor dstAlpha) {
	dynamicBlend.srcColor = srcColor;
	dynamicBlend.dstColor = dstColor;
	dynamicBlend.srcAlpha = srcAlpha;
	dynamicBlend.dstAlpha = dstAlpha;
	dynamicBlendDirty = true;
}

void MTLContext::SetBlendEquation(BlendOp op) {
	dynamicBlend.colorOp = op;
	dynamicBlend.alphaOp = op;
	dynamicBlendDirty = true;
}

void MTLContext::SetBlendEquationSeparate(BlendOp colorOp, BlendOp alphaOp) {
	dynamicBlend.colorOp = colorOp;
	dynamicBlend.alphaOp = alphaOp;
	dynamicBlendDirty = true;
}

void MTLContext::SetBlendColor(float r, float g, float b, float a) {
	dynamicBlend.blendColor[0] = r;
	dynamicBlend.blendColor[1] = g;
	dynamicBlend.blendColor[2] = b;
	dynamicBlend.blendColor[3] = a;
	dynamicBlendDirty = true;
}

void MTLContext::SetColorMask(bool r, bool g, bool b, bool a) {
	dynamicBlend.colorMask[0] = r;
	dynamicBlend.colorMask[1] = g;
	dynamicBlend.colorMask[2] = b;
	dynamicBlend.colorMask[3] = a;
	dynamicBlendDirty = true;
}

void MTLContext::SetDepthTestEnabled(bool enabled) {
	dynamicDepthTest = enabled;
	dynamicDepthTestDirty = true;
}

void MTLContext::SetDepthFunc(CompareFunc func) {
	dynamicDepthFunc = func;
	dynamicDepthFuncDirty = true;
}

void MTLContext::SetDepthWriteEnabled(bool enabled) {
	dynamicDepthWrite = enabled;
	dynamicDepthWriteDirty = true;
}

void MTLContext::BindCurrentResources() {
	if (!renderEncoder) {
		return;
	}

	// Vertex buffer at index 30 to avoid conflicts with SPIRV-Cross auto-assigned
	// buffer indices (uniform blocks start from 0). Matches kVertexBufferIndex in
	// MTLPipeline::GetRenderPipelineState.
	static constexpr uint32_t kVertexBufferIndex = 30;

	// Bind vertex buffer (per-vertex data at index 30)
	if (currentVertexBuffer) {
		id<MTLBuffer> vbo = currentVertexBuffer->GetMTLBuffer();
		if (vbo) {
			[renderEncoder setVertexBuffer:vbo offset:0 atIndex:kVertexBufferIndex];
		} else {
			LOG_L(L_ERROR, "[MTLContext] BindCurrentResources: nil backing MTLBuffer for vertex buffer");
		}
	}

	// Bind instance buffer (per-instance data at index 29)
	static constexpr uint32_t kInstanceBufferIndex = 29;
	if (currentInstanceBuffer) {
		id<MTLBuffer> ibo = currentInstanceBuffer->GetMTLBuffer();
		if (ibo) {
			[renderEncoder setVertexBuffer:ibo offset:0 atIndex:kInstanceBufferIndex];
		}
	}

	// Bind uniform data from shader
	if (currentShader) {
		currentShader->BindUniforms(renderEncoder);
	}

	// Bind uniform buffers
	for (uint32_t i = 0; i < MaxUniformBindings; i++) {
		if (boundUniformBuffers[i] && boundUniformBuffers[i]->GetMTLBuffer()) {
			[renderEncoder setVertexBuffer:boundUniformBuffers[i]->GetMTLBuffer()
			                        offset:0
			                       atIndex:i + 16];  // Higher indices for explicit UBOs
			[renderEncoder setFragmentBuffer:boundUniformBuffers[i]->GetMTLBuffer()
			                          offset:0
			                         atIndex:i + 16];
		}
	}

	// Ensure default sampler exists (lazy init, once per context lifetime)
	if (!defaultSampler) {
		MTLSamplerDescriptor* sampDesc = [[MTLSamplerDescriptor alloc] init];
		sampDesc.minFilter = MTLSamplerMinMagFilterLinear;
		sampDesc.magFilter = MTLSamplerMinMagFilterLinear;
		sampDesc.sAddressMode = MTLSamplerAddressModeRepeat;
		sampDesc.tAddressMode = MTLSamplerAddressModeRepeat;
		defaultSampler = [device->GetMTLDevice() newSamplerStateWithDescriptor:sampDesc];
	}

	// Bind textures with SPIRV-Cross texture index remapping.
	//
	// GLSL shaders without explicit layout(binding=N) get SPIRV-Cross-assigned
	// [[texture(N)]] indices that may differ from the GL texture unit the engine
	// uses. The shader maintains a remap table (GL unit → Metal index) built
	// from sampler reflection + SetUniform1i("samplerName", glUnit) calls.
	//
	// Metal supports up to 31 textures per stage but only 16 samplers.
	static constexpr uint32_t MaxMetalSamplers = 16;
	int texCount = 0;
	const bool doRemap = currentShader && currentShader->HasTextureRemapping();

	// Pass 1: Set default sampler for all 16 sampler slots (both stages).
	// Shaders may reference sampler indices that no texture is bound to.
	for (uint32_t i = 0; i < MaxMetalSamplers; i++) {
		[renderEncoder setFragmentSamplerState:defaultSampler atIndex:i];
		[renderEncoder setVertexSamplerState:defaultSampler atIndex:i];
	}

	// Pass 2: Bind textures (and their samplers) at the remapped Metal indices.
	for (uint32_t i = 0; i < MaxTextureUnits; i++) {
		if (!boundTextures[i]) continue;

		id<MTLTexture> tex = boundTextures[i]->GetMTLTexture();
		if (!tex) continue;

		// Determine Metal texture index per stage.
		int vsTexIdx = doRemap ? currentShader->GetVSTextureIndex(i) : -1;
		int fsTexIdx = doRemap ? currentShader->GetFSTextureIndex(i) : -1;

		// When remapping is active, only bind to stages that have an explicit
		// mapping for this GL unit. Identity fallback (-1) would place textures
		// at arbitrary Metal indices, potentially overwriting correctly-remapped
		// bindings from other GL units. When remapping is NOT active (no sampler
		// reflection), use identity (GL unit = Metal index) for compatibility.
		if (!doRemap) {
			vsTexIdx = static_cast<int>(i);
			fsTexIdx = static_cast<int>(i);
		}

		id<MTLSamplerState> sampler = boundTextures[i]->GetSamplerState();
		id<MTLSamplerState> effectiveSampler = sampler ? sampler : defaultSampler;

		if (vsTexIdx >= 0) {
			[renderEncoder setVertexTexture:tex atIndex:static_cast<uint32_t>(vsTexIdx)];
			if (static_cast<uint32_t>(vsTexIdx) < MaxMetalSamplers)
				[renderEncoder setVertexSamplerState:effectiveSampler atIndex:static_cast<uint32_t>(vsTexIdx)];
		}
		if (fsTexIdx >= 0) {
			[renderEncoder setFragmentTexture:tex atIndex:static_cast<uint32_t>(fsTexIdx)];
			if (static_cast<uint32_t>(fsTexIdx) < MaxMetalSamplers)
				[renderEncoder setFragmentSamplerState:effectiveSampler atIndex:static_cast<uint32_t>(fsTexIdx)];
		}
		if (vsTexIdx >= 0 || fsTexIdx >= 0)
			texCount++;
	}

	// Periodic texture binding diagnostics
	static int texLogCount = 0;
	if (texLogCount++ % 5000 == 0 && currentShader) {
		LOG("[MTL-Tex] shader='%s' boundTextures=%d remap=%d vbo=%p",
		    currentShader->GetName().c_str(), texCount, doRemap ? 1 : 0,
		    currentVertexBuffer ? (void*)currentVertexBuffer->GetMTLBuffer() : nullptr);
	}
}

void MTLContext::Draw(PrimitiveType primitive, uint32_t vertexCount, uint32_t firstVertex) {
	frameDrawCount++;
	static int drawLogCount = 0;
	if (drawLogCount < 10 || drawLogCount % 5000 == 0) {
		LOG("[MTL-Draw] verts=%u shader=%s vbuf=%p",
		    vertexCount,
		    currentShader ? currentShader->GetName().c_str() : "null",
		    (void*)(currentVertexBuffer ? currentVertexBuffer->GetMTLBuffer() : nil));
	}
	drawLogCount++;
	EnsureRenderEncoder();
	if (!renderEncoder) return;

	if (!ApplyPipelineState()) return;
	BindCurrentResources();

	@try {
		[renderEncoder drawPrimitives:ToMTLPrimitiveType(primitive)
		                  vertexStart:firstVertex
		                  vertexCount:vertexCount];
	} @catch (NSException* exception) {
		LOG_L(L_ERROR, "[MTLContext] Draw exception: %s — %s",
		      [[exception name] UTF8String], [[exception reason] UTF8String]);
	}

}

void MTLContext::DrawIndexed(PrimitiveType primitive, uint32_t indexCount, uint32_t firstIndex, int32_t vertexOffset) {
	frameDrawIdxCount++;
	static int drawIdxLogCount = 0;
	if (drawIdxLogCount < 10 || drawIdxLogCount % 5000 == 0) {
		LOG("[MTL-DrawIdx] indices=%u shader=%s vbuf=%p ibuf=%p vp=%.0fx%.0f",
		    indexCount,
		    currentShader ? currentShader->GetName().c_str() : "null",
		    (void*)(currentVertexBuffer ? currentVertexBuffer->GetMTLBuffer() : nil),
		    (void*)(currentIndexBuffer ? currentIndexBuffer->GetMTLBuffer() : nil),
		    currentViewport.width, currentViewport.height);
	}
	drawIdxLogCount++;

	EnsureRenderEncoder();
	if (!renderEncoder || !currentIndexBuffer) return;

	id<MTLBuffer> idxBuf = currentIndexBuffer->GetMTLBuffer();
	if (!idxBuf) {
		LOG_L(L_ERROR, "[MTLContext] DrawIndexed: nil backing MTLBuffer for index buffer");
		return;
	}

	if (!ApplyPipelineState()) return;
	BindCurrentResources();

	MTLIndexType indexType = ToMTLIndexType(currentIndexType);
	size_t indexSize = (currentIndexType == IndexType::UInt16) ? 2 : 4;

	@try {
		[renderEncoder drawIndexedPrimitives:ToMTLPrimitiveType(primitive)
		                          indexCount:indexCount
		                           indexType:indexType
		                         indexBuffer:idxBuf
		                   indexBufferOffset:firstIndex * indexSize
		                       instanceCount:1
		                          baseVertex:vertexOffset
		                         baseInstance:0];
	} @catch (NSException* exception) {
		LOG_L(L_ERROR, "[MTLContext] DrawIndexed exception: %s — %s",
		      [[exception name] UTF8String], [[exception reason] UTF8String]);
	}

}

void MTLContext::DrawInstanced(PrimitiveType primitive, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
	frameDrawInstCount++;
	static int drawInstLogCount = 0;
	if (drawInstLogCount < 10 || drawInstLogCount % 5000 == 0) {
		LOG("[MTL-DrawInst] verts=%u instances=%u shader=%s",
		    vertexCount, instanceCount,
		    currentShader ? currentShader->GetName().c_str() : "null");
	}
	drawInstLogCount++;
	EnsureRenderEncoder();
	if (!renderEncoder) return;

	if (!ApplyPipelineState()) return;
	BindCurrentResources();

	@try {
		[renderEncoder drawPrimitives:ToMTLPrimitiveType(primitive)
		                  vertexStart:firstVertex
		                  vertexCount:vertexCount
		                instanceCount:instanceCount
		                 baseInstance:firstInstance];
	} @catch (NSException* exception) {
		LOG_L(L_ERROR, "[MTLContext] DrawInstanced exception: %s — %s",
		      [[exception name] UTF8String], [[exception reason] UTF8String]);
	}
}

void MTLContext::DrawIndexedInstanced(PrimitiveType primitive, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
	frameDrawIdxInstCount++;
	static int drawIdxInstLogCount = 0;
	if (drawIdxInstLogCount++ < 10 || drawIdxInstLogCount % 5000 == 0) {
		LOG("[MTL-DrawIdxInst] indices=%u instances=%u shader=%s encoder=%p",
		    indexCount, instanceCount,
		    currentShader ? currentShader->GetName().c_str() : "null",
		    (void*)renderEncoder);
	}
	EnsureRenderEncoder();
	if (!renderEncoder || !currentIndexBuffer) return;

	id<MTLBuffer> idxBuf = currentIndexBuffer->GetMTLBuffer();
	if (!idxBuf) {
		LOG_L(L_ERROR, "[MTLContext] DrawIndexedInstanced: nil backing MTLBuffer");
		return;
	}

	if (!ApplyPipelineState()) return;
	BindCurrentResources();

	MTLIndexType indexType = ToMTLIndexType(currentIndexType);
	size_t indexSize = (currentIndexType == IndexType::UInt16) ? 2 : 4;

	@try {
		[renderEncoder drawIndexedPrimitives:ToMTLPrimitiveType(primitive)
		                          indexCount:indexCount
		                           indexType:indexType
		                         indexBuffer:idxBuf
		                   indexBufferOffset:firstIndex * indexSize
		                       instanceCount:instanceCount
		                          baseVertex:vertexOffset
		                         baseInstance:firstInstance];
	} @catch (NSException* exception) {
		LOG_L(L_ERROR, "[MTLContext] DrawIndexedInstanced exception: %s — %s",
		      [[exception name] UTF8String], [[exception reason] UTF8String]);
	}
}

void MTLContext::DrawIndirect(PrimitiveType primitive, IRHIBuffer* buffer, size_t offset, uint32_t drawCount, uint32_t stride) {
	EnsureRenderEncoder();
	if (!renderEncoder || !buffer) return;

	if (!ApplyPipelineState()) return;
	BindCurrentResources();

	MTLBuffer* indirectBuffer = static_cast<MTLBuffer*>(buffer);

	for (uint32_t i = 0; i < drawCount; i++) {
		[renderEncoder drawPrimitives:ToMTLPrimitiveType(primitive)
		               indirectBuffer:indirectBuffer->GetMTLBuffer()
		         indirectBufferOffset:offset + i * stride];
	}
}

void MTLContext::DrawIndexedIndirect(PrimitiveType primitive, IRHIBuffer* buffer, size_t offset, uint32_t drawCount, uint32_t stride, IndexType indexType) {
	EnsureRenderEncoder();
	if (!renderEncoder || !buffer || !currentIndexBuffer) return;

	if (!ApplyPipelineState()) return;
	BindCurrentResources();

	MTLBuffer* indirectBuffer = static_cast<MTLBuffer*>(buffer);

	for (uint32_t i = 0; i < drawCount; i++) {
		[renderEncoder drawIndexedPrimitives:ToMTLPrimitiveType(primitive)
		                           indexType:ToMTLIndexType(indexType)
		                         indexBuffer:currentIndexBuffer->GetMTLBuffer()
		                   indexBufferOffset:0
		                      indirectBuffer:indirectBuffer->GetMTLBuffer()
		                indirectBufferOffset:offset + i * stride];
	}
}

void MTLContext::SetViewport(const Viewport& viewport) {
	// Log when viewport dimensions change (to track distinct viewports, not every call)
	static float lastW = 0, lastH = 0;
	if (viewport.width != lastW || viewport.height != lastH) {
		LOG("[MTLContext] SetViewport: x=%.0f y=%.0f w=%.0f h=%.0f znear=%.3f zfar=%.3f",
		    viewport.x, viewport.y, viewport.width, viewport.height,
		    viewport.minDepth, viewport.maxDepth);
		lastW = viewport.width;
		lastH = viewport.height;
	}
	currentViewport = viewport;

	if (renderEncoder) {
		MTLViewport mtlViewport;
		mtlViewport.originX = viewport.x;
		mtlViewport.originY = viewport.y;
		mtlViewport.width = viewport.width;
		mtlViewport.height = viewport.height;
		mtlViewport.znear = viewport.minDepth;
		mtlViewport.zfar = viewport.maxDepth;
		[renderEncoder setViewport:mtlViewport];
	}
}

void MTLContext::SetScissor(const ScissorRect& rect) {
	currentScissor = rect;
	scissorEnabled = (rect.width > 0 && rect.height > 0);

	if (renderEncoder && scissorEnabled) {
		MTLScissorRect mtlScissor;
		mtlScissor.x = rect.x;
		mtlScissor.y = rect.y;
		mtlScissor.width = rect.width;
		mtlScissor.height = rect.height;
		[renderEncoder setScissorRect:mtlScissor];
	}
}

void MTLContext::SetClipDistanceEnabled(uint32_t index, bool enabled) {
	// Metal handles clip distances via [[clip_distance]] in shader output
	// This is a no-op on the CPU side
	(void)index;
	(void)enabled;
}

void MTLContext::SetVertexAttribDivisor(uint32_t index, uint32_t divisor) {
	// Metal handles vertex attribute divisor via MTLVertexStepFunction in the
	// pipeline descriptor, not as runtime state. Store for pipeline creation.
	(void)index;
	(void)divisor;
}

void MTLContext::SetVertexLayout(const VertexLayout& layout) {
	// Metal uses vertex descriptors at pipeline creation time, not runtime state.
	// Store the layout so it can be consumed when building the next pipeline descriptor.
	// Additive: append new attributes after any previously stored ones.
	const uint32_t base = currentVertexLayout.attributeCount;
	const uint32_t count = (layout.attributeCount + base <= MaxVertexAttribs)
		? layout.attributeCount : (MaxVertexAttribs - base);

	// Detect whether this batch contains per-instance attributes (divisor > 0)
	bool hasInstanceAttribs = false;
	for (uint32_t i = 0; i < count; ++i) {
		storedAttributes[base + i] = layout.attributes[i];
		if (layout.attributes[i].divisor > 0)
			hasInstanceAttribs = true;
	}


	currentVertexLayout.attributes = storedAttributes;
	currentVertexLayout.attributeCount = base + count;
	currentVertexLayout.stride = layout.stride;

	// Track per-buffer strides separately. S3DModelVAO::Bind() calls
	// SetVertexLayout twice: first with per-vertex attribs (divisor=0,
	// stride=sizeof(SVertexData)), then with per-instance attribs
	// (divisor>0, stride=sizeof(SInstanceData)). The single stride field
	// gets overwritten, but Metal needs different strides for buffer
	// layouts 30 (per-vertex) and 29 (per-instance).
	if (hasInstanceAttribs) {
		perInstanceStride = layout.stride;
	} else {
		perVertexStride = layout.stride;
	}

	hasVertexLayout = true;
}

void MTLContext::ClearVertexLayout() {
	currentVertexLayout = {};
	hasVertexLayout = false;
	perVertexStride = 0;
	perInstanceStride = 0;
}

void MTLContext::ClearColor(float r, float g, float b, float a) {
	clearColor.r = r;
	clearColor.g = g;
	clearColor.b = b;
	clearColor.a = a;
	// Don't set pendingColorClear — ClearColor only stores the color value.
	// The actual clear is triggered by Clear(color=true, ...).
	// This matches OpenGL's glClearColor (set value) vs glClear (perform clear).
}

void MTLContext::ClearDepth(float depth) {
	clearDepthValue = depth;
	// Don't set pendingDepthClear — ClearDepth only stores the depth value.
	// The actual clear is triggered by Clear(depth=true, ...).
}

void MTLContext::ClearStencil(uint32_t value) {
	clearStencilValue = value;
	// Don't set pendingStencilClear — ClearStencil only stores the stencil value.
	// The actual clear is triggered by Clear(stencil=true, ...).
}

void MTLContext::Clear(bool color, bool depth, bool stencil) {

	// In Metal, clears happen via render pass load actions (at pass start only).
	// If we're in a render pass, we can't restart it — that would wipe all
	// previously drawn content. Instead, draw a fullscreen quad with the clear
	// color, which respects the current scissor rect (matching GL behavior).
	if (inRenderPass) {
		// Mid-pass clear: draw a fullscreen quad instead of restarting pass.
		// Color-only mid-pass clears are common (Lua widgets, minimap, etc.)
		// and in GL they only clear within the current scissor rect.
		bool needDepthRestart = depth && !color;
		if (needDepthRestart) {
			// Depth-only clear without color: must restart pass (rare case).
			auto* savedFramebuffer = currentFramebuffer;
			EndRenderPass();

			RenderPassDesc clearDesc = currentPassDesc;
			if (depth && clearDesc.hasDepth) {
				clearDesc.depthAttachment.loadAction = LoadAction::Clear;
				clearDesc.depthAttachment.clearDepth = clearDepthValue;
			}
			// Use LoadAction::Load for color to preserve existing content
			if (clearDesc.colorAttachmentCount > 0) {
				clearDesc.colorAttachments[0].loadAction = LoadAction::Load;
			}

			if (savedFramebuffer) {
				BeginRenderPass(savedFramebuffer, clearDesc);
			} else {
				BeginDefaultRenderPass(clearDesc);
			}
		} else {
			// Color clear (possibly with depth): draw clear quad
			DrawClearQuad(color, depth);
		}
	} else {
		// Set pending clear flags for next BeginRenderPass
		pendingColorClear = color;
		pendingDepthClear = depth;
		pendingStencilClear = stencil;
	}
}

// MSL source for the clear quad shader — used for mid-pass clears.
// In OpenGL, glClear respects the scissor rect and can be called at any point.
// In Metal, load-action clears restart the render pass and wipe ALL content.
// This shader draws a fullscreen triangle with a constant clear color, respecting
// the current scissor rect, allowing mid-pass clears without restarting the pass.
static NSString* const kClearShaderSource = @R"msl(
#include <metal_stdlib>
using namespace metal;

struct ClearVaryings {
    float4 position [[position]];
};

vertex ClearVaryings rhi_clearVS(uint vid [[vertex_id]]) {
    float2 pos;
    pos.x = (vid == 1) ? 3.0 : -1.0;
    pos.y = (vid == 2) ? -3.0 : 1.0;
    ClearVaryings out;
    out.position = float4(pos, 0.0, 1.0);
    return out;
}

fragment float4 rhi_clearFS(ClearVaryings in [[stage_in]],
                             constant float4& clearColor [[buffer(0)]]) {
    return clearColor;
}
)msl";

// MSL source for the blit shader (compiled lazily on first use)
static NSString* const kBlitShaderSource = @R"msl(
#include <metal_stdlib>
using namespace metal;

struct BlitVaryings {
    float4 position [[position]];
};

// Fullscreen triangle covering entire clip space
vertex BlitVaryings rhi_blitVS(uint vid [[vertex_id]]) {
    float2 pos;
    pos.x = (vid == 1) ? 3.0 : -1.0;
    pos.y = (vid == 2) ? -3.0 : 1.0;
    BlitVaryings out;
    out.position = float4(pos, 0.0, 1.0);
    return out;
}

// params.xy = per-pixel UV scale
// params.zw = UV offset (UV at framebuffer pixel 0,0)
fragment float4 rhi_blitFS(BlitVaryings in [[stage_in]],
                            texture2d<float> srcTex [[texture(0)]],
                            sampler s [[sampler(0)]],
                            constant float4& params [[buffer(0)]]) {
    float2 uv = in.position.xy * params.xy + params.zw;
    return srcTex.sample(s, uv);
}
)msl";

void MTLContext::EnsureBlitPipeline(MTLPixelFormat destFormat) {
	// Compile blit shader library (once)
	if (!blitLibrary) {
		NSError* error = nil;
		blitLibrary = [device->GetMTLDevice() newLibraryWithSource:kBlitShaderSource
		                                                   options:nil
		                                                     error:&error];
		if (!blitLibrary) {
			LOG_L(L_ERROR, "[MTLContext] Failed to compile blit shader: %s",
			      error ? [[error description] UTF8String] : "unknown error");
			return;
		}
	}

	// Create pipeline state if format changed
	if (!blitPSO || blitPSOFormat != destFormat) {
		id<MTLFunction> vertexFunc = [blitLibrary newFunctionWithName:@"rhi_blitVS"];
		id<MTLFunction> fragmentFunc = [blitLibrary newFunctionWithName:@"rhi_blitFS"];

		if (!vertexFunc || !fragmentFunc) {
			LOG_L(L_ERROR, "[MTLContext] Failed to find blit shader functions");
			return;
		}

		MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
		desc.label = @"RHI Blit Pipeline";
		desc.vertexFunction = vertexFunc;
		desc.fragmentFunction = fragmentFunc;
		desc.colorAttachments[0].pixelFormat = destFormat;

		NSError* error = nil;
		blitPSO = [device->GetMTLDevice() newRenderPipelineStateWithDescriptor:desc error:&error];
		if (!blitPSO) {
			LOG_L(L_ERROR, "[MTLContext] Failed to create blit pipeline: %s",
			      error ? [[error description] UTF8String] : "unknown error");
			return;
		}
		blitPSOFormat = destFormat;
	}

	// Create samplers (once)
	if (!blitSamplerLinear) {
		MTLSamplerDescriptor* sampDesc = [[MTLSamplerDescriptor alloc] init];
		sampDesc.minFilter = MTLSamplerMinMagFilterLinear;
		sampDesc.magFilter = MTLSamplerMinMagFilterLinear;
		sampDesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
		sampDesc.tAddressMode = MTLSamplerAddressModeClampToEdge;
		blitSamplerLinear = [device->GetMTLDevice() newSamplerStateWithDescriptor:sampDesc];

		sampDesc.minFilter = MTLSamplerMinMagFilterNearest;
		sampDesc.magFilter = MTLSamplerMinMagFilterNearest;
		blitSamplerNearest = [device->GetMTLDevice() newSamplerStateWithDescriptor:sampDesc];
	}
}

void MTLContext::EnsureClearQuadPipeline(MTLPixelFormat colorFormat, MTLPixelFormat depthFormat) {
	// Compile clear shader library (reuse blitLibrary slot? no, use separate)
	static id<MTLLibrary> clearLibrary = nil;
	if (!clearLibrary) {
		NSError* error = nil;
		clearLibrary = [device->GetMTLDevice() newLibraryWithSource:kClearShaderSource
		                                                    options:nil
		                                                      error:&error];
		if (!clearLibrary) {
			LOG_L(L_ERROR, "[MTLContext] Failed to compile clear shader: %s",
			      error ? [[error description] UTF8String] : "unknown error");
			return;
		}
	}

	if (!clearQuadPSO || clearQuadPSOFormat != colorFormat) {
		id<MTLFunction> vertexFunc = [clearLibrary newFunctionWithName:@"rhi_clearVS"];
		id<MTLFunction> fragmentFunc = [clearLibrary newFunctionWithName:@"rhi_clearFS"];

		if (!vertexFunc || !fragmentFunc) {
			LOG_L(L_ERROR, "[MTLContext] Failed to find clear shader functions");
			return;
		}

		MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
		desc.label = @"RHI Clear Quad Pipeline";
		desc.vertexFunction = vertexFunc;
		desc.fragmentFunction = fragmentFunc;
		desc.colorAttachments[0].pixelFormat = colorFormat;
		// Disable blending — overwrite destination completely
		desc.colorAttachments[0].blendingEnabled = NO;
		if (depthFormat != MTLPixelFormatInvalid) {
			desc.depthAttachmentPixelFormat = depthFormat;
		}

		NSError* error = nil;
		clearQuadPSO = [device->GetMTLDevice() newRenderPipelineStateWithDescriptor:desc error:&error];
		if (!clearQuadPSO) {
			LOG_L(L_ERROR, "[MTLContext] Failed to create clear quad pipeline: %s",
			      error ? [[error description] UTF8String] : "unknown error");
			return;
		}
		clearQuadPSOFormat = colorFormat;
	}

	if (!clearQuadDepthStencil) {
		MTLDepthStencilDescriptor* dsDesc = [[MTLDepthStencilDescriptor alloc] init];
		dsDesc.depthCompareFunction = MTLCompareFunctionAlways;
		dsDesc.depthWriteEnabled = YES;
		clearQuadDepthStencil = [device->GetMTLDevice() newDepthStencilStateWithDescriptor:dsDesc];
	}
}

void MTLContext::DrawClearQuad(bool color, bool depth) {
	if (!renderEncoder) return;

	// Determine pixel formats from current render target
	MTLPixelFormat colorFmt = MTLPixelFormatBGRA8Unorm; // default screen format
	MTLPixelFormat depthFmt = MTLPixelFormatDepth32Float;
	if (currentFramebuffer) {
		colorFmt = currentFramebuffer->GetColorPixelFormat();
		depthFmt = currentFramebuffer->GetDepthPixelFormat();
	}

	EnsureClearQuadPipeline(colorFmt, depthFmt);
	if (!clearQuadPSO) return;

	// Save current encoder state — we'll override pipeline/depth for the clear
	[renderEncoder setRenderPipelineState:clearQuadPSO];

	if (depth && clearQuadDepthStencil) {
		[renderEncoder setDepthStencilState:clearQuadDepthStencil];
	} else {
		// Depth test off, depth write off — only clear color
		MTLDepthStencilDescriptor* dsDesc = [[MTLDepthStencilDescriptor alloc] init];
		dsDesc.depthCompareFunction = MTLCompareFunctionAlways;
		dsDesc.depthWriteEnabled = depth ? YES : NO;
		id<MTLDepthStencilState> noDepth = [device->GetMTLDevice() newDepthStencilStateWithDescriptor:dsDesc];
		[renderEncoder setDepthStencilState:noDepth];
	}

	// Pass clear color to fragment shader via buffer(0)
	float clearColorVec[4] = { (float)clearColor.r, (float)clearColor.g,
	                            (float)clearColor.b, (float)clearColor.a };
	[renderEncoder setFragmentBytes:clearColorVec length:sizeof(clearColorVec) atIndex:0];

	// Draw fullscreen triangle (3 vertices, no vertex buffer needed)
	[renderEncoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];

	// Note: next Draw/DrawIndexed call will call ApplyPipelineState() which
	// unconditionally sets the correct render pipeline. No dirty flag needed.
}

void MTLContext::BlitViaRenderPass(id<MTLTexture> srcTex, id<MTLTexture> dstTex,
                                    int srcX0, int srcY0, int srcX1, int srcY1,
                                    int dstX0, int dstY0, int dstX1, int dstY1,
                                    bool filterLinear) {
	EnsureBlitPipeline(dstTex.pixelFormat);
	if (!blitPSO) return;

	const float srcTexW = (float)srcTex.width;
	const float srcTexH = (float)srcTex.height;
	const uint32_t dstTexH = (uint32_t)dstTex.height;

	// Compute linear UV mapping: UV = position * scale + offset
	// Maps Metal framebuffer pixel coordinates to source texture UV coordinates,
	// handling GL-to-Metal coordinate conversion (GL origin = bottom-left,
	// Metal texture UV origin = top-left).
	const float dstDx = (float)(dstX1 - dstX0);
	const float dstDy = (float)(dstY1 - dstY0);

	const float scaleU = (dstDx != 0.0f)
		? (float)(srcX1 - srcX0) / (dstDx * srcTexW) : 0.0f;
	const float scaleV = (dstDy != 0.0f)
		? (float)(srcY1 - srcY0) / (dstDy * srcTexH) : 0.0f;

	const float offsetU = (float)srcX0 / srcTexW - (float)dstX0 * scaleU;
	const float offsetV = 1.0f - (float)srcY0 / srcTexH
		- (float)((int)dstTexH - dstY0) * scaleV;

	const float params[4] = { scaleU, scaleV, offsetU, offsetV };

	// Create render pass targeting destination texture
	MTLRenderPassDescriptor* rpDesc = [MTLRenderPassDescriptor renderPassDescriptor];
	rpDesc.colorAttachments[0].texture = dstTex;
	rpDesc.colorAttachments[0].loadAction = MTLLoadActionLoad;
	rpDesc.colorAttachments[0].storeAction = MTLStoreActionStore;

	id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:rpDesc];
	encoder.label = @"RHI Blit (scaled)";

	// Set viewport to destination rect (in Metal coordinates, top-left origin)
	const int minDstX = std::min(dstX0, dstX1);
	const int maxDstX = std::max(dstX0, dstX1);
	const int minDstY = std::min(dstY0, dstY1);
	const int maxDstY = std::max(dstY0, dstY1);

	MTLViewport vp;
	vp.originX = minDstX;
	vp.originY = (int)dstTexH - maxDstY;
	vp.width = maxDstX - minDstX;
	vp.height = maxDstY - minDstY;
	vp.znear = 0.0;
	vp.zfar = 1.0;
	[encoder setViewport:vp];

	// Bind pipeline, texture, sampler, uniforms
	[encoder setRenderPipelineState:blitPSO];
	[encoder setFragmentTexture:srcTex atIndex:0];
	[encoder setFragmentSamplerState:(filterLinear ? blitSamplerLinear : blitSamplerNearest) atIndex:0];
	[encoder setFragmentBytes:params length:sizeof(params) atIndex:0];

	// Draw fullscreen triangle
	[encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];

	[encoder endEncoding];
}

void MTLContext::BlitTexture(id<MTLTexture> srcTex, id<MTLTexture> dstTex,
                              int srcX0, int srcY0, int srcX1, int srcY1,
                              int dstX0, int dstY0, int dstX1, int dstY1,
                              bool filterLinear) {
	const int srcW = srcX1 - srcX0;
	const int srcH = srcY1 - srcY0;
	const int dstW = dstX1 - dstX0;
	const int dstH = dstY1 - dstY0;

	const bool sameSize = (srcW == dstW && srcH == dstH);
	const bool noFlip = (srcW > 0 && srcH > 0 && dstW > 0 && dstH > 0);
	const bool sameFormat = (srcTex.pixelFormat == dstTex.pixelFormat);
	const bool msaaResolve = (srcTex.sampleCount > 1 && dstTex.sampleCount == 1);

	if (msaaResolve && sameSize && sameFormat) {
		// MSAA resolve: source is multisampled, destination is not
		// Metal does not allow copyFromTexture with mismatched sample counts;
		// use a render pass with resolveTexture instead.
		MTLRenderPassDescriptor* rpDesc = [MTLRenderPassDescriptor renderPassDescriptor];
		rpDesc.colorAttachments[0].texture = srcTex;
		rpDesc.colorAttachments[0].resolveTexture = dstTex;
		rpDesc.colorAttachments[0].loadAction = MTLLoadActionLoad;
		rpDesc.colorAttachments[0].storeAction = MTLStoreActionMultisampleResolve;

		id<MTLRenderCommandEncoder> enc = [commandBuffer renderCommandEncoderWithDescriptor:rpDesc];
		enc.label = @"RHI Blit (MSAA resolve)";
		[enc endEncoding];
	} else if (sameSize && noFlip && sameFormat && !msaaResolve) {
		// Fast path: use blit command encoder for pixel-exact copy
		const uint32_t srcTexH = (uint32_t)srcTex.height;
		const uint32_t dstTexH = (uint32_t)dstTex.height;

		// Convert GL coordinates (bottom-left origin) to Metal (top-left origin)
		MTLOrigin srcOrigin = MTLOriginMake(srcX0, srcTexH - srcY1, 0);
		MTLSize   copySize  = MTLSizeMake(srcW, srcH, 1);
		MTLOrigin dstOrigin = MTLOriginMake(dstX0, dstTexH - dstY1, 0);

		id<MTLBlitCommandEncoder> blit = [commandBuffer blitCommandEncoder];
		blit.label = @"RHI Blit (copy)";
		[blit copyFromTexture:srcTex sourceSlice:0 sourceLevel:0
		         sourceOrigin:srcOrigin sourceSize:copySize
		            toTexture:dstTex destinationSlice:0 destinationLevel:0
		    destinationOrigin:dstOrigin];
		[blit endEncoding];
	} else {
		// Slow path: render pass with fullscreen triangle for scaling/flipping
		BlitViaRenderPass(srcTex, dstTex,
			srcX0, srcY0, srcX1, srcY1,
			dstX0, dstY0, dstX1, dstY1,
			filterLinear);
	}
}

void MTLContext::BlitFramebuffer(IRHIFramebuffer* src, IRHIFramebuffer* dst,
                                  int srcX0, int srcY0, int srcX1, int srcY1,
                                  int dstX0, int dstY0, int dstX1, int dstY1,
                                  bool colorBit, bool depthBit, bool filterLinear) {
	// End any active render pass before blit
	if (inRenderPass) {
		EndRenderPass();
	}

	EnsureCommandBuffer();

	MTLFramebuffer* srcFB = static_cast<MTLFramebuffer*>(src);
	MTLFramebuffer* dstFB = static_cast<MTLFramebuffer*>(dst);

	// Blit color attachment
	if (colorBit) {
		id<MTLTexture> srcTex = srcFB ? srcFB->GetColorTexture(0) : device->GetDrawableTexture();
		id<MTLTexture> dstTex = dstFB ? dstFB->GetColorTexture(0) : device->GetDrawableTexture();

		if (srcTex && dstTex) {
			BlitTexture(srcTex, dstTex,
				srcX0, srcY0, srcX1, srcY1,
				dstX0, dstY0, dstX1, dstY1,
				filterLinear);
		} else {
			LOG_L(L_WARNING, "[MTLContext] BlitFramebuffer: missing color texture (src=%p dst=%p)",
			      (void*)srcTex, (void*)dstTex);
		}
	}

	// Blit depth attachment
	if (depthBit) {
		// When src is nil (drawable), fall back to the default depth texture
		// used by the screen render pass — the drawable itself has no depth.
		id<MTLTexture> srcTex = srcFB ? srcFB->GetDepthTexture() : defaultDepthTexture;
		id<MTLTexture> dstTex = dstFB ? dstFB->GetDepthTexture() : defaultDepthTexture;

		if (srcTex && dstTex) {
			// Depth blits always use nearest filtering
			BlitTexture(srcTex, dstTex,
				srcX0, srcY0, srcX1, srcY1,
				dstX0, dstY0, dstX1, dstY1,
				false);
		} else {
			LOG_L(L_WARNING, "[MTLContext] BlitFramebuffer: missing depth texture (src=%p dst=%p)",
			      (void*)srcTex, (void*)dstTex);
		}
	}
}

void MTLContext::ReadPixels(int x, int y, int width, int height,
                            uint32_t format, uint32_t type, void* data) {
	if (!data || width <= 0 || height <= 0) return;

	// End any active render pass so texture contents are committed
	if (inRenderPass) {
		EndRenderPass();
	}

	// Get the texture to read from
	id<MTLTexture> tex = nil;
	if (currentFramebuffer) {
		tex = static_cast<MTLFramebuffer*>(currentFramebuffer)->GetColorTexture(0);
	} else {
		tex = device->GetDrawableTexture();
	}

	if (!tex) {
		LOG_L(L_WARNING, "[MTLContext] ReadPixels: no texture available");
		return;
	}

	// Commit and wait for GPU work to complete
	if (commandBuffer) {
		[commandBuffer commit];
		[commandBuffer waitUntilCompleted];
		commandBuffer = nil;
	}

	// Determine bytes per pixel from GL format/type
	// Common cases: GL_RGBA + GL_UNSIGNED_BYTE = 4 bpp
	// GL_BGR_EXT + GL_UNSIGNED_BYTE = 3 bpp (but we'll read 4 and swizzle)
	uint32_t bytesPerPixel = 4;  // Default RGBA8

	// Convert GL y (bottom-left origin) to Metal y (top-left origin)
	const uint32_t texH = (uint32_t)tex.height;
	const uint32_t metalY = texH - y - height;

	const MTLRegion region = MTLRegionMake2D(x, metalY, width, height);
	const uint32_t bytesPerRow = width * bytesPerPixel;

	// Check if we can read directly from this texture
	if (tex.storageMode == MTLStorageModePrivate) {
		// Private storage: need a staging texture
		MTLTextureDescriptor* stagingDesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:tex.pixelFormat
		                                                                                      width:width height:height mipmapped:NO];
		stagingDesc.storageMode = MTLStorageModeShared;
		stagingDesc.usage = MTLTextureUsageShaderRead;

		id<MTLTexture> staging = [device->GetMTLDevice() newTextureWithDescriptor:stagingDesc];
		if (!staging) {
			LOG_L(L_WARNING, "[MTLContext] ReadPixels: failed to create staging texture");
			return;
		}

		// Blit from source to staging
		EnsureCommandBuffer();
		id<MTLBlitCommandEncoder> blit = [commandBuffer blitCommandEncoder];
		blit.label = @"RHI ReadPixels staging blit";
		[blit copyFromTexture:tex sourceSlice:0 sourceLevel:0
		         sourceOrigin:MTLOriginMake(x, metalY, 0) sourceSize:MTLSizeMake(width, height, 1)
		            toTexture:staging destinationSlice:0 destinationLevel:0
		    destinationOrigin:MTLOriginMake(0, 0, 0)];
		[blit endEncoding];
		[commandBuffer commit];
		[commandBuffer waitUntilCompleted];
		commandBuffer = nil;

		// Read from staging texture
		[staging getBytes:data bytesPerRow:bytesPerRow fromRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0];
	} else {
		// Shared/managed storage: read directly
		[tex getBytes:data bytesPerRow:bytesPerRow fromRegion:region mipmapLevel:0];
	}

	// Metal reads top-to-bottom, but GL expects bottom-to-top.
	// Flip the rows in-place.
	std::vector<uint8_t> rowBuffer(bytesPerRow);
	uint8_t* pixels = static_cast<uint8_t*>(data);
	for (int row = 0; row < height / 2; ++row) {
		uint8_t* top = pixels + row * bytesPerRow;
		uint8_t* bot = pixels + (height - 1 - row) * bytesPerRow;
		memcpy(rowBuffer.data(), top, bytesPerRow);
		memcpy(top, bot, bytesPerRow);
		memcpy(bot, rowBuffer.data(), bytesPerRow);
	}

	// Metal drawable is BGRA8Unorm but callers (Screenshot.cpp) request GL_RGBA (0x1908).
	// Swizzle B↔R when the source texture is BGRA and the caller wants RGBA.
	constexpr uint32_t kGL_RGBA = 0x1908;
	if (tex.pixelFormat == MTLPixelFormatBGRA8Unorm && format == kGL_RGBA) {
		const int totalPixels = width * height;
		for (int i = 0; i < totalPixels; ++i) {
			std::swap(pixels[i * 4 + 0], pixels[i * 4 + 2]);
		}
	}
}

void MTLContext::Flush() {
	// Commit current command buffer synchronously and create a new one.
	// Used for resource synchronization (e.g., render-to-texture before mipmap generation).
	if (inRenderPass) {
		EndRenderPass();
	}
	if (commandBuffer) {
		[commandBuffer commit];
		[commandBuffer waitUntilCompleted];
		// Create new command buffer so EndFrame can still present the drawable
		commandBuffer = [device->GetCommandQueue() commandBuffer];
		commandBuffer.label = @"RHI Command Buffer (post-flush)";
	}
}

void MTLContext::Finish() {
	// End any active render pass
	if (inRenderPass) {
		EndRenderPass();
	}

	if (commandBuffer) {
		[commandBuffer commit];
		[commandBuffer waitUntilCompleted];
		commandBuffer = nil;
	}
}

} // namespace RHI
