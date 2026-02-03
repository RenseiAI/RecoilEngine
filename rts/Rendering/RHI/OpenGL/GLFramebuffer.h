/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#ifndef GL_RHI_FRAMEBUFFER_H
#define GL_RHI_FRAMEBUFFER_H

#include "Rendering/RHI/RHIFramebuffer.h"
#include "Rendering/GL/FBO.h"

namespace RHI {

/// Thin wrapper around FBO.
class GLFramebuffer : public IRHIFramebuffer {
public:
	GLFramebuffer();
	~GLFramebuffer() override = default;

	void AttachColor(IRHITexture* texture, uint32_t index, uint32_t mipLevel, uint32_t layer) override;
	void AttachDepth(IRHITexture* texture, uint32_t mipLevel) override;
	void AttachDepthStencil(IRHITexture* texture, uint32_t mipLevel) override;
	void AttachRenderbuffer(TextureFormat format, uint32_t width, uint32_t height, uint32_t attachment) override;
	void Detach(uint32_t attachment) override;
	void DetachAll() override;

	void SetDrawBuffers(const uint32_t* attachments, uint32_t count) override;

	bool IsComplete() const override;

	void Bind() override;
	void Unbind() override;

	uint32_t GetNativeHandle() const override { return fbo.GetId(); }
	uint32_t GetColorAttachmentCount() const override { return colorCount; }

	/// Access the underlying FBO for legacy code paths
	FBO& GetFBO() { return fbo; }

private:
	FBO fbo;
	uint32_t colorCount = 0;
};

} // namespace RHI

#endif // GL_RHI_FRAMEBUFFER_H
