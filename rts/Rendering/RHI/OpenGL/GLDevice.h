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

	// Capability queries - delegate to globalRendering
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
	uint64_t GetTimerQueryResult(uint32_t query, bool wait) override;

	// Resource creation
	std::unique_ptr<IRHIBuffer> CreateBuffer(BufferType type, BufferUsage usage, size_t size, const void* initialData) override;
	std::unique_ptr<IRHITexture> CreateTexture(TextureType type, TextureFormat format, uint32_t width, uint32_t height, uint32_t depthOrLayers, uint32_t mipLevels, uint32_t sampleCount) override;
	std::unique_ptr<IRHIShader> CreateShader(const std::string& name) override;
	std::unique_ptr<IRHIFramebuffer> CreateFramebuffer() override;
	std::unique_ptr<IRHIPipeline> CreatePipeline(const PipelineDesc& desc) override;

	IRHIContext* GetContext() override;

private:
	std::unique_ptr<IRHIContext> context;
};

} // namespace RHI

#endif // GL_RHI_DEVICE_H
