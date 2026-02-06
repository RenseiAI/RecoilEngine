/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#include "GLDevice.h"
#include "GLContext.h"
#include "GLBuffer.h"
#include "GLTexture.h"
#include "GLShader.h"
#include "GLFramebuffer.h"
#include "GLPipeline.h"
#include "Rendering/GL/myGL.h"
#include "System/Config/ConfigHandler.h"

namespace RHI {

GLDevice::GLDevice()
	: context(std::make_unique<GLContext>())
{
	InitCapabilities();
}

void GLDevice::InitCapabilities()
{
	// Feature support flags - query GLAD extension booleans directly.
	// GLAD is already loaded before RHI::InitDevice() is called
	// (gladLoadGL happens in GlobalRendering::CreateWindowAndContext).
	// In headless builds, all GLAD extension flags are 0 (false).
	caps_haveGL4 = GLAD_GL_ARB_multi_draw_indirect
	            && GLAD_GL_ARB_uniform_buffer_object
	            && GLAD_GL_ARB_shader_storage_buffer_object;

	caps_supportPersistentMapping = GLAD_GL_ARB_buffer_storage;
	caps_supportExplicitAttribLoc = GLAD_GL_ARB_explicit_attrib_location;
	caps_supportRestartPrimitive  = GLAD_GL_NV_primitive_restart;
	caps_supportClipSpaceControl  = GLAD_GL_ARB_clip_control;
	caps_supportSeamlessCubeMaps  = GLAD_GL_ARB_seamless_cube_map;
	caps_supportFragDepthLayout   = GLAD_GL_ARB_conservative_depth;
	caps_supportMSAAFrameBuffer   = GLAD_GL_EXT_framebuffer_multisample;

	// Resource limits - query GL directly.
	// In headless builds, glGetIntegerv is a no-op stub that leaves values at 0.
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &caps_maxTextureSize);
	glGetIntegerv(GL_MAX_TEXTURE_COORDS, &caps_maxTexSlots);

	if (GLAD_GL_EXT_texture_filter_anisotropic)
		glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &caps_maxTexAnisoLvl);

	if (GLAD_GL_ARB_uniform_buffer_object) {
		glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS, &caps_glslMaxUniformBufferBindings);
		glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE,      &caps_glslMaxUniformBufferSize);
	}

	if (GLAD_GL_ARB_shader_storage_buffer_object) {
		glGetIntegerv(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS, &caps_glslMaxStorageBufferBindings);
		glGetIntegerv(GL_MAX_SHADER_STORAGE_BLOCK_SIZE,      &caps_glslMaxStorageBufferSize);
	}

	glGetIntegerv(GL_MAX_DRAW_BUFFERS, &caps_glslMaxDrawBuffers);

	glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS,          &caps_maxFragShSlots);
	glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &caps_maxCombShSlots);
	glGetIntegerv(GL_MAX_VARYING_FLOATS,               &caps_glslMaxVaryings);
	glGetIntegerv(GL_MAX_VERTEX_ATTRIBS,               &caps_glslMaxAttributes);
	glGetIntegerv(GL_MAX_ELEMENTS_INDICES,             &caps_glslMaxRecommendedIndices);
	glGetIntegerv(GL_MAX_ELEMENTS_VERTICES,            &caps_glslMaxRecommendedVertices);

	// GL_MAX_VARYING_FLOATS is the maximum number of floats; count float4's
	caps_glslMaxVaryings /= 4;

	caps_supportTextureQueryLOD = GLAD_GL_ARB_texture_query_lod;

	// Depth buffer bit depth is probed via FBO in GlobalRendering::SetGLSupportFlags().
	// We can't easily replicate that probe here without FBO infrastructure, so default
	// to 24 bits which is the most common and safe default.
	caps_supportDepthBufferBitDepth = 24;

	// Apply user config overrides (mirrors GlobalRendering::SetGLSupportFlags logic)
	if (configHandler->GetInt("ForceDisablePersistentMapping") != 0)
		caps_supportPersistentMapping = false;
	if (configHandler->GetInt("ForceDisableGL4") != 0)
		caps_haveGL4 = false;
	if (configHandler->GetInt("ForceDisableClipCtrl") != 0)
		caps_supportClipSpaceControl = false;
	if (configHandler->GetInt("ForceDisableExplicitAttribLocs") != 0)
		caps_supportExplicitAttribLoc = false;
}

// --- Capability queries: read from own fields ---

bool GLDevice::HaveGL4() const { return caps_haveGL4; }
bool GLDevice::SupportPersistentMapping() const { return caps_supportPersistentMapping; }
bool GLDevice::SupportClipSpaceControl() const { return caps_supportClipSpaceControl; }
bool GLDevice::SupportSeamlessCubeMaps() const { return caps_supportSeamlessCubeMaps; }
bool GLDevice::SupportMSAAFrameBuffer() const { return caps_supportMSAAFrameBuffer; }
bool GLDevice::SupportExplicitAttribLoc() const { return caps_supportExplicitAttribLoc; }
bool GLDevice::SupportFragDepthLayout() const { return caps_supportFragDepthLayout; }
bool GLDevice::SupportRestartPrimitive() const { return caps_supportRestartPrimitive; }

int GLDevice::GetMaxTextureSize() const { return caps_maxTextureSize; }
int GLDevice::GetMaxTextureSlots() const { return caps_maxTexSlots; }
float GLDevice::GetMaxTexAnisotropy() const { return caps_maxTexAnisoLvl; }
int GLDevice::GetMaxDrawBuffers() const { return caps_glslMaxDrawBuffers; }
int GLDevice::GetMaxUniformBufferBindings() const { return caps_glslMaxUniformBufferBindings; }
int GLDevice::GetMaxUniformBufferSize() const { return caps_glslMaxUniformBufferSize; }
int GLDevice::GetMaxStorageBufferBindings() const { return caps_glslMaxStorageBufferBindings; }
int GLDevice::GetMaxStorageBufferSize() const { return caps_glslMaxStorageBufferSize; }
int GLDevice::GetDepthBufferBitDepth() const { return caps_supportDepthBufferBitDepth; }

int GLDevice::GetMaxFragmentTextureSlots() const { return caps_maxFragShSlots; }
int GLDevice::GetMaxCombinedTextureSlots() const { return caps_maxCombShSlots; }
int GLDevice::GetMaxVaryings() const { return caps_glslMaxVaryings; }
int GLDevice::GetMaxVertexAttributes() const { return caps_glslMaxAttributes; }
int GLDevice::GetMaxRecommendedIndices() const { return caps_glslMaxRecommendedIndices; }
int GLDevice::GetMaxRecommendedVertices() const { return caps_glslMaxRecommendedVertices; }
bool GLDevice::SupportTextureQueryLOD() const { return caps_supportTextureQueryLOD; }

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

void GLDevice::TimestampQuery(uint32_t query) {
	glQueryCounter(query, GL_TIMESTAMP);
}

bool GLDevice::IsTimerQueryResultAvailable(uint32_t query) {
	GLint available = 0;
	glGetQueryObjectiv(query, GL_QUERY_RESULT_AVAILABLE, &available);
	return available != 0;
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

// --- Fence sync ---

FenceHandle GLDevice::CreateFence() {
	return reinterpret_cast<FenceHandle>(glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0));
}

bool GLDevice::WaitFence(FenceHandle fence, uint64_t timeoutNs) {
	if (!fence) return true;
	GLenum result = glClientWaitSync(reinterpret_cast<GLsync>(fence), GL_SYNC_FLUSH_COMMANDS_BIT, timeoutNs);
	return result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED;
}

void GLDevice::DeleteFence(FenceHandle fence) {
	if (fence)
		glDeleteSync(reinterpret_cast<GLsync>(fence));
}

// --- Version/debug info ---

VersionInfo GLDevice::GetVersionInfo() const {
	VersionInfo info;
	const char* s;
	s = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
	if (s) info.vendor = s;
	s = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
	if (s) info.renderer = s;
	s = reinterpret_cast<const char*>(glGetString(GL_VERSION));
	if (s) info.version = s;
	s = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));
	if (s) info.shadingLanguageVersion = s;
	return info;
}

int GLDevice::GetFramebufferSampleCount() const {
	GLint samples = 0;
	glGetIntegerv(GL_SAMPLES, &samples);
	return samples;
}

// --- Debug output ---

void GLDevice::SetDebugOutputEnabled(bool enabled, bool synchronous) {
	if (enabled) {
		glEnable(GL_DEBUG_OUTPUT);
		if (synchronous)
			glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
		else
			glDisable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
	} else {
		glDisable(GL_DEBUG_OUTPUT);
		glDisable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
	}
}

void GLDevice::SetDebugMessageCallback(DebugMessageCallback callback, const void* userParam) {
	glDebugMessageCallback(reinterpret_cast<GLDEBUGPROC>(callback), userParam);
}

void GLDevice::ClearErrors() {
	while (glGetError() != GL_NO_ERROR) {}
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
