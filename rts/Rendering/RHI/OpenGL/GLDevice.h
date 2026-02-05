/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#ifndef GL_RHI_DEVICE_H
#define GL_RHI_DEVICE_H

#include "Rendering/RHI/RHIDevice.h"
#include "Rendering/RHI/RHIContext.h"

namespace RHI {

class GLDevice : public IRHIDevice {
public:
	GLDevice();
	~GLDevice() override = default;

	Backend GetBackend() const override { return Backend::OpenGL; }
	const char* GetBackendName() const override { return "OpenGL"; }

	// Capability queries - read from own fields populated by InitCapabilities()
	bool HaveGL4() const override;
	bool SupportPersistentMapping() const override;
	bool SupportClipSpaceControl() const override;
	bool SupportSeamlessCubeMaps() const override;
	bool SupportMSAAFrameBuffer() const override;
	bool SupportExplicitAttribLoc() const override;
	bool SupportFragDepthLayout() const override;
	bool SupportRestartPrimitive() const override;

	int GetMaxTextureSize() const override;
	int GetMaxTextureSlots() const override;
	float GetMaxTexAnisotropy() const override;
	int GetMaxDrawBuffers() const override;
	int GetMaxUniformBufferBindings() const override;
	int GetMaxUniformBufferSize() const override;
	int GetMaxStorageBufferBindings() const override;
	int GetMaxStorageBufferSize() const override;
	int GetDepthBufferBitDepth() const override;

	bool SupportTimerQueries() const override;
	size_t GetAvailableVideoMemory() const override;

	uint32_t CreateTimerQuery() override;
	void DeleteTimerQuery(uint32_t query) override;
	void BeginTimerQuery(uint32_t query) override;
	void EndTimerQuery(uint32_t query) override;
	void TimestampQuery(uint32_t query) override;
	bool IsTimerQueryResultAvailable(uint32_t query) override;
	uint64_t GetTimerQueryResult(uint32_t query, bool wait) override;

	// Resource creation
	std::unique_ptr<IRHIBuffer> CreateBuffer(BufferType type, BufferUsage usage, size_t size, const void* initialData) override;
	std::unique_ptr<IRHITexture> CreateTexture(TextureType type, TextureFormat format, uint32_t width, uint32_t height, uint32_t depthOrLayers, uint32_t mipLevels, uint32_t sampleCount) override;
	std::unique_ptr<IRHIShader> CreateShader(const std::string& name) override;
	std::unique_ptr<IRHIFramebuffer> CreateFramebuffer() override;
	std::unique_ptr<IRHIPipeline> CreatePipeline(const PipelineDesc& desc) override;

	IRHIContext* GetContext() override;

private:
	/// Query GL capabilities directly via GLAD. Called once at construction.
	void InitCapabilities();

	std::unique_ptr<IRHIContext> context;

	// Capability flags (populated by InitCapabilities)
	bool caps_haveGL4 = false;
	bool caps_supportPersistentMapping = false;
	bool caps_supportClipSpaceControl = false;
	bool caps_supportSeamlessCubeMaps = false;
	bool caps_supportMSAAFrameBuffer = false;
	bool caps_supportExplicitAttribLoc = false;
	bool caps_supportFragDepthLayout = false;
	bool caps_supportRestartPrimitive = false;

	// Resource limits (populated by InitCapabilities)
	int caps_maxTextureSize = 0;
	int caps_maxTexSlots = 0;
	float caps_maxTexAnisoLvl = 0.0f;
	int caps_glslMaxDrawBuffers = 0;
	int caps_glslMaxUniformBufferBindings = 0;
	int caps_glslMaxUniformBufferSize = 0;
	int caps_glslMaxStorageBufferBindings = 0;
	int caps_glslMaxStorageBufferSize = 0;
	int caps_supportDepthBufferBitDepth = 0;
};

} // namespace RHI

#endif // GL_RHI_DEVICE_H
