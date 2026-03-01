/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#import "MTLFramebuffer.h"
#import "MTLDevice.h"
#import "MTLTexture.h"

#import <Metal/Metal.h>

#include "System/Log/ILog.h"

namespace RHI {

MTLFramebuffer::MTLFramebuffer(MTLDevice* device)
	: device(device)
{
	// Initialize draw buffers to identity (attachment i -> output i)
	for (uint32_t i = 0; i < MaxColorAttachments; i++) {
		drawBuffers[i] = i;
	}
}

MTLFramebuffer::~MTLFramebuffer() {
	depthRenderbuffer = nil;
}

void MTLFramebuffer::AttachColor(IRHITexture* texture, uint32_t index, uint32_t mipLevel, uint32_t layer) {
	if (index >= MaxColorAttachments) {
		LOG_L(L_ERROR, "[MTLFramebuffer] Color attachment index %u exceeds maximum %u",
		      index, MaxColorAttachments - 1);
		return;
	}

	colorAttachments[index].texture = static_cast<MTLTexture*>(texture);
	colorAttachments[index].mipLevel = mipLevel;
	colorAttachments[index].layer = layer;

	// Update attachment count
	if (texture) {
		colorAttachmentCount = std::max(colorAttachmentCount, index + 1);
	} else {
		// Recalculate count if removing an attachment
		colorAttachmentCount = 0;
		for (uint32_t i = 0; i < MaxColorAttachments; i++) {
			if (colorAttachments[i].texture) {
				colorAttachmentCount = i + 1;
			}
		}
	}
}

void MTLFramebuffer::AttachDepth(IRHITexture* texture, uint32_t mipLevel) {
	depthAttachment.texture = static_cast<MTLTexture*>(texture);
	depthAttachment.mipLevel = mipLevel;
	depthAttachment.hasStencil = false;
}

void MTLFramebuffer::AttachDepthStencil(IRHITexture* texture, uint32_t mipLevel) {
	depthAttachment.texture = static_cast<MTLTexture*>(texture);
	depthAttachment.mipLevel = mipLevel;
	depthAttachment.hasStencil = true;
}

void MTLFramebuffer::AttachRenderbuffer(TextureFormat format, uint32_t width, uint32_t height, uint32_t attachment) {
	if (!device || !device->IsValid()) {
		LOG_L(L_ERROR, "[MTLFramebuffer] Cannot create renderbuffer: invalid device");
		return;
	}

	// Create internal texture for renderbuffer
	// attachment value follows GL convention: GL_DEPTH_ATTACHMENT = 0x8D00
	bool isDepth = (attachment >= 0x8D00);  // GL_DEPTH_ATTACHMENT range

	if (isDepth) {
		MTLTextureDescriptor* desc = [[MTLTextureDescriptor alloc] init];
		desc.textureType = MTLTextureType2D;
		desc.width = width;
		desc.height = height;
		desc.pixelFormat = MTLTexture::ToMTLPixelFormat(format);
		desc.storageMode = MTLStorageModePrivate;  // GPU only
		desc.usage = MTLTextureUsageRenderTarget;

		depthRenderbuffer = [device->GetMTLDevice() newTextureWithDescriptor:desc];
		depthRenderbuffer.label = @"MTLFramebuffer Depth Renderbuffer";

		depthAttachment.texture = nullptr;  // Using renderbuffer instead
		depthAttachment.hasStencil = (format == TextureFormat::Depth24Stencil8 ||
		                              format == TextureFormat::Depth32FStencil8);
	} else {
		// Color renderbuffer - create as color attachment
		LOG_L(L_WARNING, "[MTLFramebuffer] Color renderbuffers should use textures in Metal");
	}
}

void MTLFramebuffer::Detach(uint32_t attachment) {
	// attachment follows GL convention
	// GL_COLOR_ATTACHMENT0 = 0x8CE0
	// GL_DEPTH_ATTACHMENT = 0x8D00
	// GL_STENCIL_ATTACHMENT = 0x8D20

	if (attachment >= 0x8CE0 && attachment < 0x8CE0 + MaxColorAttachments) {
		uint32_t index = attachment - 0x8CE0;
		colorAttachments[index].texture = nullptr;
		colorAttachments[index].mipLevel = 0;
		colorAttachments[index].layer = 0;

		// Recalculate attachment count
		colorAttachmentCount = 0;
		for (uint32_t i = 0; i < MaxColorAttachments; i++) {
			if (colorAttachments[i].texture) {
				colorAttachmentCount = i + 1;
			}
		}
	} else if (attachment == 0x8D00 || attachment == 0x8D20 || attachment == 0x821A) {
		// Depth, stencil, or depth-stencil
		depthAttachment.texture = nullptr;
		depthAttachment.mipLevel = 0;
		depthAttachment.hasStencil = false;
		depthRenderbuffer = nil;
	}
}

void MTLFramebuffer::DetachAll() {
	for (uint32_t i = 0; i < MaxColorAttachments; i++) {
		colorAttachments[i].texture = nullptr;
		colorAttachments[i].mipLevel = 0;
		colorAttachments[i].layer = 0;
	}
	depthAttachment.texture = nullptr;
	depthAttachment.mipLevel = 0;
	depthAttachment.hasStencil = false;
	depthRenderbuffer = nil;
	colorAttachmentCount = 0;
}

void MTLFramebuffer::SetDrawBuffers(const uint32_t* attachments, uint32_t count) {
	drawBufferCount = std::min(count, MaxColorAttachments);
	for (uint32_t i = 0; i < drawBufferCount; i++) {
		drawBuffers[i] = attachments[i];
	}
}

bool MTLFramebuffer::IsComplete() const {
	// A framebuffer is complete if it has at least one attachment
	if (colorAttachmentCount == 0 && !depthAttachment.texture && !depthRenderbuffer) {
		return false;
	}

	// Verify all color attachments have valid textures
	for (uint32_t i = 0; i < colorAttachmentCount; i++) {
		if (colorAttachments[i].texture && !colorAttachments[i].texture->GetMTLTexture()) {
			return false;
		}
	}

	// Verify depth attachment
	if (depthAttachment.texture && !depthAttachment.texture->GetMTLTexture()) {
		return false;
	}

	return true;
}

void MTLFramebuffer::Bind() {
	// In Metal, framebuffers are not bound globally.
	// This is a no-op; actual binding happens via BeginRenderPass in the context.
	// Note: Making this start a render pass breaks RmlUi compositing because
	// the RmlUi layer system expects to draw directly to the screen when its
	// FBOs are "bound" — the compositing step then overwrites the screen
	// with an opaque dark layer from the FBO content (mostly transparent black).
}

void MTLFramebuffer::Unbind() {
	// No-op for Metal
}

MTLRenderPassDescriptor* MTLFramebuffer::CreateRenderPassDescriptor(const RenderPassDesc& passDesc) {
	MTLRenderPassDescriptor* rpDesc = [MTLRenderPassDescriptor renderPassDescriptor];

	// Configure color attachments
	for (uint32_t i = 0; i < colorAttachmentCount; i++) {
		MTLRenderPassColorAttachmentDescriptor* colorDesc = rpDesc.colorAttachments[i];

		if (colorAttachments[i].texture) {
			colorDesc.texture = colorAttachments[i].texture->GetMTLTexture();
			colorDesc.level = colorAttachments[i].mipLevel;
			colorDesc.slice = colorAttachments[i].layer;
		}

		// Load action
		if (i < passDesc.colorAttachmentCount) {
			switch (passDesc.colorAttachments[i].loadAction) {
				case LoadAction::Load:
					colorDesc.loadAction = MTLLoadActionLoad;
					break;
				case LoadAction::Clear:
					colorDesc.loadAction = MTLLoadActionClear;
					colorDesc.clearColor = MTLClearColorMake(
						passDesc.colorAttachments[i].clearColor.r,
						passDesc.colorAttachments[i].clearColor.g,
						passDesc.colorAttachments[i].clearColor.b,
						passDesc.colorAttachments[i].clearColor.a
					);
					break;
				case LoadAction::DontCare:
					colorDesc.loadAction = MTLLoadActionDontCare;
					break;
			}

			// Store action
			switch (passDesc.colorAttachments[i].storeAction) {
				case StoreAction::Store:
					colorDesc.storeAction = MTLStoreActionStore;
					break;
				case StoreAction::DontCare:
					colorDesc.storeAction = MTLStoreActionDontCare;
					break;
			}
		} else {
			// Default: load and store
			colorDesc.loadAction = MTLLoadActionLoad;
			colorDesc.storeAction = MTLStoreActionStore;
		}
	}

	// Configure depth attachment
	id<MTLTexture> depthTex = nil;
	if (depthAttachment.texture) {
		depthTex = depthAttachment.texture->GetMTLTexture();
	} else if (depthRenderbuffer) {
		depthTex = depthRenderbuffer;
	}

	if (depthTex) {
		rpDesc.depthAttachment.texture = depthTex;
		rpDesc.depthAttachment.level = depthAttachment.mipLevel;

		if (passDesc.hasDepth) {
			switch (passDesc.depthAttachment.loadAction) {
				case LoadAction::Load:
					rpDesc.depthAttachment.loadAction = MTLLoadActionLoad;
					break;
				case LoadAction::Clear:
					rpDesc.depthAttachment.loadAction = MTLLoadActionClear;
					rpDesc.depthAttachment.clearDepth = passDesc.depthAttachment.clearDepth;
					break;
				case LoadAction::DontCare:
					rpDesc.depthAttachment.loadAction = MTLLoadActionDontCare;
					break;
			}

			switch (passDesc.depthAttachment.storeAction) {
				case StoreAction::Store:
					rpDesc.depthAttachment.storeAction = MTLStoreActionStore;
					break;
				case StoreAction::DontCare:
					rpDesc.depthAttachment.storeAction = MTLStoreActionDontCare;
					break;
			}
		} else {
			rpDesc.depthAttachment.loadAction = MTLLoadActionLoad;
			rpDesc.depthAttachment.storeAction = MTLStoreActionStore;
		}

		// Configure stencil if present
		if (depthAttachment.hasStencil) {
			rpDesc.stencilAttachment.texture = depthTex;
			rpDesc.stencilAttachment.loadAction = rpDesc.depthAttachment.loadAction;
			rpDesc.stencilAttachment.storeAction = rpDesc.depthAttachment.storeAction;
			rpDesc.stencilAttachment.clearStencil = 0;
		}
	}

	return rpDesc;
}

MTLPixelFormat MTLFramebuffer::GetColorPixelFormat(uint32_t index) const {
	if (index < colorAttachmentCount && colorAttachments[index].texture) {
		return MTLTexture::ToMTLPixelFormat(colorAttachments[index].texture->GetFormat());
	}
	return MTLPixelFormatBGRA8Unorm;  // Default format
}

MTLPixelFormat MTLFramebuffer::GetDepthPixelFormat() const {
	if (depthAttachment.texture) {
		return MTLTexture::ToMTLPixelFormat(depthAttachment.texture->GetFormat());
	}
	if (depthRenderbuffer) {
		return depthRenderbuffer.pixelFormat;
	}
	return MTLPixelFormatInvalid;
}

id<MTLTexture> MTLFramebuffer::GetColorTexture(uint32_t index) const {
	if (index < colorAttachmentCount && colorAttachments[index].texture) {
		return colorAttachments[index].texture->GetMTLTexture();
	}
	return nil;
}

id<MTLTexture> MTLFramebuffer::GetDepthTexture() const {
	if (depthAttachment.texture) {
		return depthAttachment.texture->GetMTLTexture();
	}
	return depthRenderbuffer;
}

} // namespace RHI
