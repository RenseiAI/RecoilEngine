/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#ifndef MTL_RHI_FRAMEBUFFER_H
#define MTL_RHI_FRAMEBUFFER_H

/**
 * Metal Framebuffer Implementation
 *
 * Metal doesn't have framebuffer objects like OpenGL. Instead, render
 * passes are configured via MTLRenderPassDescriptor at the start of
 * each render pass.
 *
 * This class stores the attachment configuration and creates the
 * appropriate MTLRenderPassDescriptor when a render pass begins.
 *
 * Metal API mapping:
 *   FBO attachment   ->  Store texture reference + config
 *   FBO bind         ->  Create MTLRenderPassDescriptor at BeginRenderPass
 *   FBO unbind       ->  Handled by EndRenderPass (command encoder ends)
 *   glDrawBuffers    ->  Not needed (Metal handles MRT automatically)
 */

#include "Rendering/RHI/RHIFramebuffer.h"
#include <array>

#ifdef __OBJC__
#import <Metal/Metal.h>
#endif

namespace RHI {

class MTLDevice;
class MTLTexture;

class MTLFramebuffer : public IRHIFramebuffer {
public:
	explicit MTLFramebuffer(MTLDevice* device);
	~MTLFramebuffer() override;

	// Prevent copying
	MTLFramebuffer(const MTLFramebuffer&) = delete;
	MTLFramebuffer& operator=(const MTLFramebuffer&) = delete;

	// --- Attachment ---
	void AttachColor(IRHITexture* texture, uint32_t index, uint32_t mipLevel, uint32_t layer) override;
	void AttachDepth(IRHITexture* texture, uint32_t mipLevel) override;
	void AttachDepthStencil(IRHITexture* texture, uint32_t mipLevel) override;
	void AttachRenderbuffer(TextureFormat format, uint32_t width, uint32_t height, uint32_t attachment) override;

	void Detach(uint32_t attachment) override;
	void DetachAll() override;

	void SetDrawBuffers(const uint32_t* attachments, uint32_t count) override;

	// --- Validation ---
	bool IsComplete() const override;

	// --- Direct bind (legacy path) ---
	void Bind() override;
	void Unbind() override;

	// --- Queries ---
	uint32_t GetNativeHandle() const override { return 0; }  // No native handle in Metal
	uint32_t GetColorAttachmentCount() const override { return colorAttachmentCount; }

#ifdef __OBJC__
	// --- Metal-specific ---

	/// Create a render pass descriptor based on current attachments and load/store config
	MTLRenderPassDescriptor* CreateRenderPassDescriptor(const RenderPassDesc& passDesc);

	/// Get the pixel format of color attachment 0 (for pipeline creation)
	MTLPixelFormat GetColorPixelFormat(uint32_t index = 0) const;

	/// Get the depth pixel format (for pipeline creation)
	MTLPixelFormat GetDepthPixelFormat() const;
#endif

private:
	static constexpr uint32_t MaxColorAttachments = 8;

	struct ColorAttachmentInfo {
		MTLTexture* texture = nullptr;
		uint32_t    mipLevel = 0;
		uint32_t    layer = 0;
	};

	struct DepthAttachmentInfo {
		MTLTexture* texture = nullptr;
		uint32_t    mipLevel = 0;
		bool        hasStencil = false;
	};

	MTLDevice* device;

	std::array<ColorAttachmentInfo, MaxColorAttachments> colorAttachments;
	DepthAttachmentInfo depthAttachment;
	uint32_t colorAttachmentCount = 0;

	// Draw buffer configuration
	std::array<uint32_t, MaxColorAttachments> drawBuffers;
	uint32_t drawBufferCount = 0;

#ifdef __OBJC__
	// Internal renderbuffers (for depth/stencil without texture reads)
	id<MTLTexture> depthRenderbuffer = nil;
#else
	void* depthRenderbuffer = nullptr;
#endif
};

} // namespace RHI

#endif // MTL_RHI_FRAMEBUFFER_H
