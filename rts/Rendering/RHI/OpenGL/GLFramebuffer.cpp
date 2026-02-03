/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#include "GLFramebuffer.h"
#include "GLTexture.h"
#include "Rendering/GL/myGL.h"
#include <vector>

namespace RHI {

static GLenum ToGLAttachment(uint32_t index) {
	return GL_COLOR_ATTACHMENT0_EXT + index;
}

static GLenum ToGLRenderbufferFormat(TextureFormat format) {
	return GLTexture::ToGLInternalFormat(format);
}

GLFramebuffer::GLFramebuffer()
	: fbo()
{
}

void GLFramebuffer::AttachColor(IRHITexture* texture, uint32_t index, uint32_t mipLevel, uint32_t layer) {
	fbo.Bind();
	if (layer > 0) {
		fbo.AttachTextureLayer(texture->GetNativeHandle(), ToGLAttachment(index), mipLevel, layer);
	} else {
		fbo.AttachTexture(texture->GetNativeHandle(), GLTexture::ToGLTarget(texture->GetType()), ToGLAttachment(index), mipLevel);
	}
	if (index >= colorCount)
		colorCount = index + 1;
	fbo.Unbind();
}

void GLFramebuffer::AttachDepth(IRHITexture* texture, uint32_t mipLevel) {
	fbo.Bind();
	fbo.AttachTexture(texture->GetNativeHandle(), GLTexture::ToGLTarget(texture->GetType()), GL_DEPTH_ATTACHMENT_EXT, mipLevel);
	fbo.Unbind();
}

void GLFramebuffer::AttachDepthStencil(IRHITexture* texture, uint32_t mipLevel) {
	fbo.Bind();
	fbo.AttachTexture(texture->GetNativeHandle(), GLTexture::ToGLTarget(texture->GetType()), GL_DEPTH_STENCIL_ATTACHMENT, mipLevel);
	fbo.Unbind();
}

void GLFramebuffer::AttachRenderbuffer(TextureFormat format, uint32_t width, uint32_t height, uint32_t attachment) {
	fbo.Bind();
	GLenum glAttach = (attachment == 0) ? GL_DEPTH_ATTACHMENT_EXT : ToGLAttachment(attachment);
	fbo.CreateRenderBuffer(glAttach, ToGLRenderbufferFormat(format), width, height);
	fbo.Unbind();
}

void GLFramebuffer::Detach(uint32_t attachment) {
	fbo.Bind();
	fbo.Detach(ToGLAttachment(attachment));
	fbo.Unbind();
}

void GLFramebuffer::DetachAll() {
	fbo.Bind();
	fbo.DetachAll();
	fbo.Unbind();
	colorCount = 0;
}

void GLFramebuffer::SetDrawBuffers(const uint32_t* attachments, uint32_t count) {
	fbo.Bind();
	std::vector<GLenum> glAttachments(count);
	for (uint32_t i = 0; i < count; ++i) {
		glAttachments[i] = ToGLAttachment(attachments[i]);
	}
	glDrawBuffers(count, glAttachments.data());
	fbo.Unbind();
}

bool GLFramebuffer::IsComplete() const {
	return fbo.IsValid();
}

void GLFramebuffer::Bind()   { fbo.Bind(); }
void GLFramebuffer::Unbind() { FBO::Unbind(); }

} // namespace RHI
