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

bool GLDevice::SupportTimerQueries() const { return GLAD_GL_ARB_timer_query; }

size_t GLDevice::GetAvailableVideoMemory() const {
	// Try NVIDIA extension first
	if (GLAD_GL_NVX_gpu_memory_info) {
		GLint totalMemKB = 0;
		glGetIntegerv(GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX, &totalMemKB);
		return static_cast<size_t>(totalMemKB) * 1024;
	}
	// Try AMD extension
	if (GLAD_GL_ATI_meminfo) {
		GLint vboFreeMemKB[4] = {0};
		glGetIntegerv(GL_VBO_FREE_MEMORY_ATI, vboFreeMemKB);
		return static_cast<size_t>(vboFreeMemKB[0]) * 1024;
	}
	return 0;
}

uint32_t GLDevice::CreateTimerQuery() {
	GLuint query = 0;
	glGenQueries(1, &query);
	return query;
}

void GLDevice::DeleteTimerQuery(uint32_t query) {
	GLuint q = query;
	glDeleteQueries(1, &q);
}

void GLDevice::BeginTimerQuery(uint32_t query) {
	glBeginQuery(GL_TIME_ELAPSED, query);
}

void GLDevice::EndTimerQuery(uint32_t query) {
	(void)query;
	glEndQuery(GL_TIME_ELAPSED);
}

uint64_t GLDevice::GetTimerQueryResult(uint32_t query, bool wait) {
	if (wait) {
		GLuint64 result = 0;
		glGetQueryObjectui64v(query, GL_QUERY_RESULT, &result);
		return result;
	} else {
		GLint available = 0;
		glGetQueryObjectiv(query, GL_QUERY_RESULT_AVAILABLE, &available);
		if (available) {
			GLuint64 result = 0;
			glGetQueryObjectui64v(query, GL_QUERY_RESULT, &result);
			return result;
		}
		return 0;
	}
}

// --- Resource creation ---

std::unique_ptr<IRHIBuffer> GLDevice::CreateBuffer(BufferType type, BufferUsage usage, size_t size, const void* initialData) {
	return std::make_unique<GLBuffer>(type, usage, size, initialData);
}

std::unique_ptr<IRHITexture> GLDevice::CreateTexture(TextureType type, TextureFormat format, uint32_t width, uint32_t height, uint32_t depthOrLayers, uint32_t mipLevels, uint32_t sampleCount) {
	return std::make_unique<GLTexture>(type, format, width, height, depthOrLayers, mipLevels, sampleCount);
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
