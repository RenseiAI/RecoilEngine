/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#ifndef GL_RHI_TEXTURE_H
#define GL_RHI_TEXTURE_H

#include "Rendering/RHI/RHITexture.h"
#include "Rendering/GL/myGL.h"

namespace RHI {

/// Thin wrapper around raw GL texture calls.
class GLTexture : public IRHITexture {
public:
	GLTexture(TextureType type, TextureFormat format, uint32_t width, uint32_t height, uint32_t depthOrLayers, uint32_t mipLevels, uint32_t sampleCount = 1);
	~GLTexture() override;

	void Bind(uint32_t unit) override;
	void Unbind(uint32_t unit) override;

	void Upload(uint32_t level, uint32_t x, uint32_t y, uint32_t width, uint32_t height, const void* data) override;
	void Upload3D(uint32_t level, uint32_t x, uint32_t y, uint32_t z, uint32_t width, uint32_t height, uint32_t depth, const void* data) override;
	void UploadCompressed(uint32_t level, uint32_t x, uint32_t y, uint32_t width, uint32_t height, size_t dataSize, const void* data) override;
	void UploadCubeFace(CubeFace face, uint32_t level, uint32_t width, uint32_t height, const void* data) override;
	void UpdateCubeFace(CubeFace face, uint32_t level, uint32_t x, uint32_t y, uint32_t width, uint32_t height, const void* data) override;

	void SetMinFilter(TextureFilter filter) override;
	void SetMagFilter(TextureFilter filter) override;
	void SetWrapS(TextureWrap wrap) override;
	void SetWrapT(TextureWrap wrap) override;
	void SetWrapR(TextureWrap wrap) override;
	void SetAnisotropy(float level) override;
	void SetCompareMode(bool enabled, CompareFunc func) override;

	void SetSwizzle(uint8_t r, uint8_t g, uint8_t b, uint8_t a) override;
	void SetBorderColor(float r, float g, float b, float a) override;
	void SetLodBias(float bias) override;

	void GenerateMipmaps() override;

	uint32_t GetWidth() const override { return texWidth; }
	uint32_t GetHeight() const override { return texHeight; }
	uint32_t GetDepthOrLayers() const override { return texDepthOrLayers; }
	uint32_t GetMipLevels() const override { return texMipLevels; }
	TextureType GetType() const override { return texType; }
	TextureFormat GetFormat() const override { return texFormat; }
	uint32_t GetNativeHandle() const override { return texId; }
	uint32_t GetSampleCount() const override { return texSampleCount; }
	uint32_t DisownNativeHandle() override;

	static GLenum ToGLTarget(TextureType type);
	static GLenum ToGLInternalFormat(TextureFormat format);
	static GLenum ToGLFormat(TextureFormat format);
	static GLenum ToGLDataType(TextureFormat format);
	static GLenum ToGLFilter(TextureFilter filter);
	static GLenum ToGLWrap(TextureWrap wrap);

private:
	GLuint      texId;
	GLenum      glTarget;
	GLenum      glInternalFormat;
	TextureType   texType;
	TextureFormat texFormat;
	uint32_t    texWidth;
	uint32_t    texHeight;
	uint32_t    texDepthOrLayers;
	uint32_t    texMipLevels;
	uint32_t    texSampleCount;
};

} // namespace RHI

#endif // GL_RHI_TEXTURE_H
