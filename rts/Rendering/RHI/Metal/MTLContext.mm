/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#import "MTLContext.h"
#import "MTLDevice.h"
#import "MTLBuffer.h"
#import "MTLTexture.h"
#import "MTLShader.h"
#import "MTLFramebuffer.h"
#import "MTLPipeline.h"

#import <Metal/Metal.h>

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

void MTLContext::BlitFramebuffer(IRHIFramebuffer* src, IRHIFramebuffer* dst,
                                  int srcX0, int srcY0, int srcX1, int srcY1,
                                  int dstX0, int dstY0, int dstX1, int dstY1,
                                  bool colorBit, bool depthBit, bool filterLinear) {
	// End any active render pass
	if (inRenderPass) {
		EndRenderPass();
	}

	EnsureCommandBuffer();

	// Create a blit command encoder
	id<MTLBlitCommandEncoder> blitEncoder = [commandBuffer blitCommandEncoder];
	blitEncoder.label = @"RHI Blit";

	MTLFramebuffer* srcFB = static_cast<MTLFramebuffer*>(src);
	MTLFramebuffer* dstFB = static_cast<MTLFramebuffer*>(dst);

	// Blit color
	if (colorBit && srcFB && dstFB) {
		// Get source and dest textures
		// Note: This is a simplified implementation. Full blit with scaling
		// would require a compute shader or render pass.
		LOG_L(L_WARNING, "[MTLContext] BlitFramebuffer with scaling not fully implemented");
	}

	[blitEncoder endEncoding];
}

void MTLContext::Flush() {
	// In Metal, commands are buffered in the command buffer
	// To flush immediately, we'd need to commit the buffer
	// For now, this is a no-op as we commit at frame end
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
