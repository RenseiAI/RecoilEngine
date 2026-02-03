/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#include "GLDevice.h"
#include "GLContext.h"
#include "GLBuffer.h"
#include "GLTexture.h"
#include "GLShader.h"
#include "GLFramebuffer.h"
#include "GLPipeline.h"
#include "Rendering/GlobalRendering.h"

namespace RHI {

GLDevice::GLDevice()
	: context(std::make_unique<GLContext>())
{
}

// --- Capability queries: thin wrappers around globalRendering fields ---

bool GLDevice::HaveGL4() const { return globalRendering->haveGL4; }
bool GLDevice::SupportPersistentMapping() const { return globalRendering->supportPersistentMapping; }
bool GLDevice::SupportClipSpaceControl() const { return globalRendering->supportClipSpaceControl; }
bool GLDevice::SupportSeamlessCubeMaps() const { return globalRendering->supportSeamlessCubeMaps; }
bool GLDevice::SupportMSAAFrameBuffer() const { return globalRendering->supportMSAAFrameBuffer; }
bool GLDevice::SupportExplicitAttribLoc() const { return globalRendering->supportExplicitAttribLoc; }
bool GLDevice::SupportFragDepthLayout() const { return globalRendering->supportFragDepthLayout; }
bool GLDevice::SupportRestartPrimitive() const { return globalRendering->supportRestartPrimitive; }

int GLDevice::GetMaxTextureSize() const { return globalRendering->maxTextureSize; }
int GLDevice::GetMaxTextureSlots() const { return globalRendering->maxTexSlots; }
float GLDevice::GetMaxTexAnisotropy() const { return globalRendering->maxTexAnisoLvl; }
int GLDevice::GetMaxDrawBuffers() const { return globalRendering->glslMaxDrawBuffers; }
int GLDevice::GetMaxUniformBufferBindings() const { return globalRendering->glslMaxUniformBufferBindings; }
int GLDevice::GetMaxUniformBufferSize() const { return globalRendering->glslMaxUniformBufferSize; }
int GLDevice::GetMaxStorageBufferBindings() const { return globalRendering->glslMaxStorageBufferBindings; }
int GLDevice::GetMaxStorageBufferSize() const { return globalRendering->glslMaxStorageBufferSize; }
int GLDevice::GetDepthBufferBitDepth() const { return globalRendering->supportDepthBufferBitDepth; }

// --- Resource creation ---

std::unique_ptr<IRHIBuffer> GLDevice::CreateBuffer(BufferType type, BufferUsage usage, size_t size, const void* initialData) {
	return std::make_unique<GLBuffer>(type, usage, size, initialData);
}

std::unique_ptr<IRHITexture> GLDevice::CreateTexture(TextureType type, TextureFormat format, uint32_t width, uint32_t height, uint32_t depthOrLayers, uint32_t mipLevels) {
	return std::make_unique<GLTexture>(type, format, width, height, depthOrLayers, mipLevels);
}

std::unique_ptr<IRHIShader> GLDevice::CreateShader(const std::string& name) {
	return std::make_unique<GLShader>(name);
}

std::unique_ptr<IRHIFramebuffer> GLDevice::CreateFramebuffer() {
	return std::make_unique<GLFramebuffer>();
}

std::unique_ptr<IRHIPipeline> GLDevice::CreatePipeline(const PipelineDesc& desc) {
	return std::make_unique<GLPipeline>(desc);
}

IRHIContext* GLDevice::GetContext() {
	return context.get();
}

} // namespace RHI
