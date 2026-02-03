/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#ifndef RHI_FRAMEBUFFER_H
#define RHI_FRAMEBUFFER_H

/**
 * RHI Framebuffer Interface
 *
 * Maps FBO operations to backend-agnostic interface:
 *   FBO::Init()                           ->  IRHIDevice::CreateFramebuffer()
 *   FBO::Bind() / FBO::Unbind()           ->  IRHIContext::BeginRenderPass() / EndRenderPass()
 *   FBO::AttachTexture(id, tgt, attach)   ->  IRHIFramebuffer::AttachColor() / AttachDepth()
 *   FBO::Blit(from, to, ...)             ->  IRHIContext::BlitFramebuffer()
 *   FBO::IsValid() / FBO::GetStatus()    ->  IRHIFramebuffer::IsComplete()
 *   FBO::Detach() / FBO::DetachAll()     ->  IRHIFramebuffer::DetachAll()
 *   FBO::CreateRenderBuffer()            ->  IRHIFramebuffer::AttachRenderbuffer()
 *
 * Metal uses MTLRenderPassDescriptor instead of FBOs. The RHI framebuffer
 * stores attachment configuration; actual render pass begins via IRHIContext.
 */

#include <cstdint>
#include "RHITypes.h"

namespace RHI {

class IRHITexture;

class IRHIFramebuffer {
public:
	virtual ~IRHIFramebuffer() = default;

	// --- Attachment ---
	virtual void AttachColor(IRHITexture* texture, uint32_t index = 0, uint32_t mipLevel = 0, uint32_t layer = 0) = 0;
	virtual void AttachDepth(IRHITexture* texture, uint32_t mipLevel = 0) = 0;
	virtual void AttachDepthStencil(IRHITexture* texture, uint32_t mipLevel = 0) = 0;

	/// Create and attach an internal renderbuffer (for depth/stencil not needing texture reads)
	virtual void AttachRenderbuffer(TextureFormat format, uint32_t width, uint32_t height, uint32_t attachment) = 0;

	virtual void Detach(uint32_t attachment) = 0;
	virtual void DetachAll() = 0;

	/// Set which color attachments to draw into (MRT output selection)
	/// Maps to glDrawBuffers
	virtual void SetDrawBuffers(const uint32_t* attachments, uint32_t count) = 0;

	// --- Validation ---
	virtual bool IsComplete() const = 0;

	// --- Direct bind (legacy path, prefer BeginRenderPass) ---
	virtual void Bind() = 0;
	virtual void Unbind() = 0;

	// --- Queries ---
	virtual uint32_t GetNativeHandle() const = 0;
	virtual uint32_t GetColorAttachmentCount() const = 0;
};

} // namespace RHI

#endif // RHI_FRAMEBUFFER_H
