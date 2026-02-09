/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "GeometryBuffer.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/RHI/RHIContext.h"
#include "Rendering/RHI/RHIDevice.h"
#include "Rendering/RHI/RHIFactory.h"
#include "System/Config/ConfigHandler.h"

#include <algorithm>
#include <cstring> //memset

#include "System/Misc/TracyDefs.h"

void GL::GeometryBuffer::Init(bool ctor) {
	RECOIL_DETAILED_TRACY_ZONE;
	// if dead, this must be a non-ctor reload
	assert(!dead || !ctor);

	// Clear texture pointers (reset unique_ptrs to nullptr)
	for (auto& tex : bufferTextures) {
		tex.reset();
	}
	memset(&bufferAttachments[0], 0, sizeof(bufferAttachments));

	// NOTE:
	//   initial buffer size must be 0 s.t. prevSize != currSize when !init
	//   (Lua can toggle drawDeferred and might be the first to cause a call
	//   to Create)
	prevBufferSize = GetWantedSize(false);
	currBufferSize = GetWantedSize(true);

	dead = false;
	bound = false;
	msaa |= configHandler->GetBool("AllowMultiSampledFrameBuffers");
	msaa &= globalRendering->supportMSAAFrameBuffer;
}

void GL::GeometryBuffer::Kill(bool dtor) {
	RECOIL_DETAILED_TRACY_ZONE;
	if (dead) {
		// if already dead, this must be final cleanup
		assert(dtor);
		return;
	}

	if (buffer.IsValid())
		DetachTextures(false);

	dead = true;
}

void GL::GeometryBuffer::Clear() const {
	RECOIL_DETAILED_TRACY_ZONE;
	assert(bound);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void GL::GeometryBuffer::SetDepthRange(float nearDepth, float farDepth) const {
	RECOIL_DETAILED_TRACY_ZONE;
	auto* ctx = RHI::GetDevice()->GetContext();

	#if 0
	if (RHI::GetDevice()->SupportClipSpaceControl()) {
		// TODO: need to inform shaders about this, modify PM instead
		glDepthRangef(nearDepth, farDepth);
		glClearDepth(farDepth);
		glDepthFunc((nearDepth <= farDepth)? GL_LEQUAL: GL_GREATER);
	}
	#else
	glClearDepth(std::max(nearDepth, farDepth));
	ctx->SetDepthFunc(RHI::CompareFunc::LessEqual);
	#endif
}

void GL::GeometryBuffer::DetachTextures(const bool init) {
	RECOIL_DETAILED_TRACY_ZONE;
	// nothing to detach yet during init
	if (init)
		return;

	buffer.Bind();

	// detach only actually attached textures, ATI drivers might crash
	for (unsigned int i = 0; i < (ATTACHMENT_COUNT - 1); ++i) {
		buffer.Detach(GL_COLOR_ATTACHMENT0_EXT + i);
	}

	buffer.Detach(GL_DEPTH_ATTACHMENT_EXT);
	buffer.Unbind();

	// Delete RHI textures (unique_ptr handles deallocation)
	for (auto& tex : bufferTextures) {
		tex.reset();
	}

	// return to incomplete state
	memset(&bufferAttachments[0], 0, sizeof(bufferAttachments));
}

void GL::GeometryBuffer::DrawDebug(const unsigned int texID, const float2 texMins, const float2 texMaxs) const {
	RECOIL_DETAILED_TRACY_ZONE;
	glPushMatrix();
	glLoadIdentity();
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();

	glActiveTexture(GL_TEXTURE0);
	glEnable(GetTextureTarget());
	glBindTexture(GetTextureTarget(), texID);
	glBegin(GL_QUADS);
	glTexCoord2f(texMins.x, texMins.y); glNormal3fv(&UpVector.x); glVertex2f(texMins.x, texMins.y);
	glTexCoord2f(texMaxs.x, texMins.y); glNormal3fv(&UpVector.x); glVertex2f(texMaxs.x, texMins.y);
	glTexCoord2f(texMaxs.x, texMaxs.y); glNormal3fv(&UpVector.x); glVertex2f(texMaxs.x, texMaxs.y);
	glTexCoord2f(texMins.x, texMaxs.y); glNormal3fv(&UpVector.x); glVertex2f(texMins.x, texMaxs.y);
	glEnd();
	glBindTexture(GetTextureTarget(), 0);
	glDisable(GetTextureTarget());

	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
}

bool GL::GeometryBuffer::Create(const int2 size) {
	RECOIL_DETAILED_TRACY_ZONE;
	auto* device = RHI::GetDevice();
	assert(device != nullptr);

	const RHI::TextureType texType = GetRHITextureType();
	const uint32_t sampleCount = msaa ? globalRendering->msaaLevel : 1;

	// Create RHI textures for all attachments
	for (unsigned int n = 0; n < ATTACHMENT_COUNT; n++) {
		const RHI::TextureFormat format = GetRHIAttachmentFormat(n);

		// Create texture via RHI
		bufferTextures[n] = device->CreateTexture(
			texType,
			format,
			size.x,
			size.y,
			1,           // depthOrLayers
			1,           // mipLevels
			sampleCount
		);

		if (!bufferTextures[n]) {
			// Creation failed - clean up and return
			for (auto& tex : bufferTextures) {
				tex.reset();
			}
			return false;
		}

		// Set texture parameters (only for non-MSAA textures; MSAA textures don't support these)
		if (!msaa) {
			bufferTextures[n]->SetWrapS(RHI::TextureWrap::ClampToBorder);
			bufferTextures[n]->SetWrapT(RHI::TextureWrap::ClampToBorder);
			bufferTextures[n]->SetMinFilter(RHI::TextureFilter::Linear);
			bufferTextures[n]->SetMagFilter(RHI::TextureFilter::Linear);
		}

		// GL_DEPTH_TEXTURE_MODE is GL-specific and has no RHI equivalent
		// It's legacy and typically defaults to GL_LUMINANCE (or GL_RED in modern GL)
		// Modern shaders use texture() which doesn't need this mode set
		// TODO: If needed for compatibility, add to IRHITexture interface

		if (n == ATTACHMENT_ZVALTEX) {
			bufferAttachments[n] = GL_DEPTH_ATTACHMENT_EXT;
		} else {
			bufferAttachments[n] = GL_COLOR_ATTACHMENT0_EXT + n;
		}
	}

	// Collect GLuint handles for FBO attachment (FBO class still uses raw GL)
	GLuint texIDs[ATTACHMENT_COUNT];
	for (unsigned int n = 0; n < ATTACHMENT_COUNT; n++) {
		texIDs[n] = bufferTextures[n]->GetNativeHandle();
	}

	const unsigned int texTarget = GetTextureTarget();

	// sic; Mesa complains about an incomplete FBO if calling Bind before TexImage (?)
	buffer.Bind();
	buffer.AttachTextures(texIDs, bufferAttachments, texTarget, ATTACHMENT_COUNT);

	// TODO: Could use IRHIFramebuffer::SetDrawBuffers here instead of raw GL
	// define the attachments we are going to draw into
	// note: the depth-texture attachment does not count
	// here and will be GL_NONE implicitly!
	glDrawBuffers(ATTACHMENT_COUNT - 1, &bufferAttachments[0]);

	// FBO must have been valid from point of construction
	// if we reached CreateGeometryBuffer, but CheckStatus
	// can still invalidate it
	assert(buffer.IsValid());

	const bool ret = buffer.CheckStatus(name);

	buffer.Unbind();
	return ret;
}

bool GL::GeometryBuffer::Update(const bool init) {
	RECOIL_DETAILED_TRACY_ZONE;
	currBufferSize = GetWantedSize(true);

	// FBO must be valid from point of construction
	if (!buffer.IsValid())
		return false;

	// buffer isn't bound by calling context, can not call
	// GetStatus to check for GL_FRAMEBUFFER_COMPLETE_EXT
	//
	if (HasAttachments()) {
		// technically a buffer can not be complete yet during
		// initialization, however the GL spec says that FBO's
		// with only empty attachments are complete by default
		// assert(!init);

		// FBO was already initialized (during init or from Lua)
		// so it will have attachments -> check if they need to
		// be regenerated, eg. if a window resize event happened
		if (prevBufferSize == currBufferSize)
			return true;

		DetachTextures(init);
	}

	return (Create(prevBufferSize = currBufferSize));
}

int2 GL::GeometryBuffer::GetWantedSize(bool allowed) const {
	RECOIL_DETAILED_TRACY_ZONE;
	return {globalRendering->viewSizeX * allowed, globalRendering->viewSizeY * allowed};
}

void GL::GeometryBuffer::LoadViewport()
{
	RECOIL_DETAILED_TRACY_ZONE;
	glViewport(0, 0, globalRendering->viewSizeX, globalRendering->viewSizeY);
}
