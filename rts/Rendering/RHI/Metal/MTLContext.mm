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
	// Render encoder must be created via BeginRenderPass
	if (!renderEncoder) {
		LOG_L(L_WARNING, "[MTLContext] No active render encoder - call BeginRenderPass first");
	}
}

void MTLContext::BeginFrame() {
	// Wait for a frame slot to become available
	dispatch_semaphore_wait(frameSemaphore, DISPATCH_TIME_FOREVER);

	// Create new command buffer for this frame
	EnsureCommandBuffer();
}

void MTLContext::EndFrame() {
	// End any active render pass
	if (inRenderPass) {
		EndRenderPass();
	}

	if (!commandBuffer) {
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

	// Clear state for next frame
	commandBuffer = nil;
	renderEncoder = nil;
}

void MTLContext::BeginRenderPass(IRHIFramebuffer* framebuffer, const RenderPassDesc& desc) {
	if (inRenderPass) {
		EndRenderPass();
	}

	EnsureCommandBuffer();

	currentFramebuffer = static_cast<MTLFramebuffer*>(framebuffer);
	currentPassDesc = desc;

	// Create render pass descriptor
	MTLRenderPassDescriptor* rpDesc = nil;
	if (currentFramebuffer) {
		rpDesc = currentFramebuffer->CreateRenderPassDescriptor(desc);
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

	// Create render command encoder
	renderEncoder = [commandBuffer renderCommandEncoderWithDescriptor:rpDesc];
	renderEncoder.label = @"RHI Default Render Pass";

	inRenderPass = true;

	// Apply current viewport if set
	if (currentViewport.width > 0 && currentViewport.height > 0) {
		SetViewport(currentViewport);
	}
}

void MTLContext::EndRenderPass() {
	if (!inRenderPass || !renderEncoder) {
		return;
	}

	[renderEncoder endEncoding];
	renderEncoder = nil;
	inRenderPass = false;
}

void MTLContext::BindPipeline(IRHIPipeline* pipeline) {
	currentPipeline = static_cast<MTLPipeline*>(pipeline);
}

void MTLContext::BindVertexBuffer(IRHIBuffer* buffer, uint32_t binding) {
	currentVertexBuffer = static_cast<MTLBuffer*>(buffer);
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
	MTLPipeline* pipeline = currentPipeline ? currentPipeline : GetOrCreateDefaultPipeline();
	if (!renderEncoder || !pipeline || !currentShader) {
		return false;
	}

	// Get or create the render pipeline state
	MTLPixelFormat colorFormat = MTLPixelFormatBGRA8Unorm;
	MTLPixelFormat depthFormat = MTLPixelFormatInvalid;

	if (currentFramebuffer) {
		colorFormat = currentFramebuffer->GetColorPixelFormat();
		depthFormat = currentFramebuffer->GetDepthPixelFormat();
	}

	id<MTLRenderPipelineState> pipelineState =
		pipeline->GetRenderPipelineState(currentShader, colorFormat, depthFormat,
		                                 hasVertexLayout ? &currentVertexLayout : nullptr);

	if (!pipelineState) {
		return false;
	}

	[renderEncoder setRenderPipelineState:pipelineState];

	// Apply depth-stencil state
	id<MTLDepthStencilState> dsState = pipeline->GetDepthStencilState();
	if (dsState) {
		[renderEncoder setDepthStencilState:dsState];
	}

	// Apply rasterizer state
	pipeline->ApplyRasterizerState(renderEncoder);
	return true;
}

void MTLContext::BindCurrentResources() {
	if (!renderEncoder) {
		return;
	}

	// Bind vertex buffer
	if (currentVertexBuffer && currentVertexBuffer->GetMTLBuffer()) {
		[renderEncoder setVertexBuffer:currentVertexBuffer->GetMTLBuffer()
		                        offset:0
		                       atIndex:currentVertexBinding + 1];  // Index 0 reserved for uniforms
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

	// Bind textures
	for (uint32_t i = 0; i < MaxTextureUnits; i++) {
		if (boundTextures[i]) {
			id<MTLTexture> tex = boundTextures[i]->GetMTLTexture();
			id<MTLSamplerState> sampler = boundTextures[i]->GetSamplerState();

			if (tex) {
				[renderEncoder setFragmentTexture:tex atIndex:i];
				[renderEncoder setVertexTexture:tex atIndex:i];
			}
			if (sampler) {
				[renderEncoder setFragmentSamplerState:sampler atIndex:i];
				[renderEncoder setVertexSamplerState:sampler atIndex:i];
			}
		}
	}
}

void MTLContext::Draw(PrimitiveType primitive, uint32_t vertexCount, uint32_t firstVertex) {
	EnsureRenderEncoder();
	if (!renderEncoder) return;

	if (!ApplyPipelineState()) return;
	BindCurrentResources();

	[renderEncoder drawPrimitives:ToMTLPrimitiveType(primitive)
	                  vertexStart:firstVertex
	                  vertexCount:vertexCount];
}

void MTLContext::DrawIndexed(PrimitiveType primitive, uint32_t indexCount, uint32_t firstIndex, int32_t vertexOffset) {
	EnsureRenderEncoder();
	if (!renderEncoder || !currentIndexBuffer) return;

	if (!ApplyPipelineState()) return;
	BindCurrentResources();

	MTLIndexType indexType = ToMTLIndexType(currentIndexType);
	size_t indexSize = (currentIndexType == IndexType::UInt16) ? 2 : 4;

	[renderEncoder drawIndexedPrimitives:ToMTLPrimitiveType(primitive)
	                          indexCount:indexCount
	                           indexType:indexType
	                         indexBuffer:currentIndexBuffer->GetMTLBuffer()
	                   indexBufferOffset:firstIndex * indexSize
	                       instanceCount:1
	                          baseVertex:vertexOffset
	                         baseInstance:0];
}

void MTLContext::DrawInstanced(PrimitiveType primitive, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
	EnsureRenderEncoder();
	if (!renderEncoder) return;

	if (!ApplyPipelineState()) return;
	BindCurrentResources();

	[renderEncoder drawPrimitives:ToMTLPrimitiveType(primitive)
	                  vertexStart:firstVertex
	                  vertexCount:vertexCount
	                instanceCount:instanceCount
	                 baseInstance:firstInstance];
}

void MTLContext::DrawIndexedInstanced(PrimitiveType primitive, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
	EnsureRenderEncoder();
	if (!renderEncoder || !currentIndexBuffer) return;

	if (!ApplyPipelineState()) return;
	BindCurrentResources();

	MTLIndexType indexType = ToMTLIndexType(currentIndexType);
	size_t indexSize = (currentIndexType == IndexType::UInt16) ? 2 : 4;

	[renderEncoder drawIndexedPrimitives:ToMTLPrimitiveType(primitive)
	                          indexCount:indexCount
	                           indexType:indexType
	                         indexBuffer:currentIndexBuffer->GetMTLBuffer()
	                   indexBufferOffset:firstIndex * indexSize
	                       instanceCount:instanceCount
	                          baseVertex:vertexOffset
	                         baseInstance:firstInstance];
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

	for (uint32_t i = 0; i < count; ++i) {
		storedAttributes[base + i] = layout.attributes[i];
	}

	currentVertexLayout.attributes = storedAttributes;
	currentVertexLayout.attributeCount = base + count;
	// Use the latest stride (caller sets stride per-call; the last call's stride
	// is typically the instance stride, but Metal uses per-buffer strides anyway)
	currentVertexLayout.stride = layout.stride;
	hasVertexLayout = true;
}

void MTLContext::ClearVertexLayout() {
	currentVertexLayout = {};
	hasVertexLayout = false;
}

void MTLContext::ClearColor(float r, float g, float b, float a) {
	clearColor.r = r;
	clearColor.g = g;
	clearColor.b = b;
	clearColor.a = a;
	pendingColorClear = true;
}

void MTLContext::ClearDepth(float depth) {
	clearDepthValue = depth;
	pendingDepthClear = true;
}

void MTLContext::ClearStencil(uint32_t value) {
	clearStencilValue = value;
	pendingStencilClear = true;
}

void MTLContext::Clear(bool color, bool depth, bool stencil) {
	// In Metal, clears happen via render pass load actions.
	// If we're in a render pass, we need to end it and start a new one with clear.
	if (inRenderPass) {
		EndRenderPass();

		// Modify pass descriptor to include clears
		RenderPassDesc clearDesc = currentPassDesc;

		if (color && clearDesc.colorAttachmentCount > 0) {
			clearDesc.colorAttachments[0].loadAction = LoadAction::Clear;
			clearDesc.colorAttachments[0].clearColor = clearColor;
		}

		if (depth && clearDesc.hasDepth) {
			clearDesc.depthAttachment.loadAction = LoadAction::Clear;
			clearDesc.depthAttachment.clearDepth = clearDepthValue;
		}

		// Re-begin the render pass with clear actions
		if (currentFramebuffer) {
			BeginRenderPass(currentFramebuffer, clearDesc);
		} else {
			BeginDefaultRenderPass(clearDesc);
		}
	} else {
		// Set pending clear flags for next BeginRenderPass
		pendingColorClear = color;
		pendingDepthClear = depth;
		pendingStencilClear = stencil;
	}
}

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

	if (sameSize && noFlip && sameFormat) {
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
		id<MTLTexture> srcTex = srcFB ? srcFB->GetDepthTexture() : nil;
		id<MTLTexture> dstTex = dstFB ? dstFB->GetDepthTexture() : nil;

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
