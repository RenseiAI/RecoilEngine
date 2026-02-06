/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#ifndef MTL_RHI_TEXTURE_H
#define MTL_RHI_TEXTURE_H

/**
 * Metal Texture Implementation
 *
 * Metal API mapping:
 *   MTLTextureDescriptor + makeTexture     ->  MTLTexture constructor
 *   texture.replaceRegion                  ->  Upload() / Upload3D()
 *   MTLSamplerDescriptor + makeSamplerState ->  SetFilter() / SetWrap()
 *
 * Texture format mapping:
 *   RGBA8           ->  MTLPixelFormatRGBA8Unorm
 *   RGB8            ->  MTLPixelFormatRGBA8Unorm (RGB not directly supported)
 *   Depth32F        ->  MTLPixelFormatDepth32Float
 *   Depth24Stencil8 ->  MTLPixelFormatDepth24Unorm_Stencil8
 *
 * Sampler state:
 *   Metal separates sampler state from textures.
 *   Each texture tracks its desired sampler configuration and
 *   creates/caches a MTLSamplerState object.
 */

#include "Rendering/RHI/RHITexture.h"

#ifdef __OBJC__
#import <Metal/Metal.h>
#endif

namespace RHI {

class MTLDevice;

class MTLTexture : public IRHITexture {
public:
	MTLTexture(MTLDevice* device, TextureType type, TextureFormat format,
	           uint32_t width, uint32_t height, uint32_t depthOrLayers,
	           uint32_t mipLevels, uint32_t sampleCount);
	~MTLTexture() override;

	// Prevent copying
	MTLTexture(const MTLTexture&) = delete;
	MTLTexture& operator=(const MTLTexture&) = delete;

	// --- Binding ---
	// Note: Metal doesn't have global texture binding state.
	// Textures are set directly on command encoder. These track state
	// for compatibility with the RHI interface.
	void Bind(uint32_t unit) override;
	void Unbind(uint32_t unit) override;

	// --- Data upload ---
	void Upload(uint32_t level, uint32_t x, uint32_t y,
	            uint32_t width, uint32_t height, const void* data) override;

	void Upload3D(uint32_t level, uint32_t x, uint32_t y, uint32_t z,
	              uint32_t width, uint32_t height, uint32_t depth,
	              const void* data) override;

	void UploadCompressed(uint32_t level, uint32_t x, uint32_t y,
	                      uint32_t width, uint32_t height,
	                      size_t dataSize, const void* data) override;

	void UploadCubeFace(CubeFace face, uint32_t level,
	                    uint32_t width, uint32_t height,
	                    const void* data) override;

	void UpdateCubeFace(CubeFace face, uint32_t level,
	                    uint32_t x, uint32_t y,
	                    uint32_t width, uint32_t height,
	                    const void* data) override;

	// --- Sampling state ---
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

	// --- Mipmap ---
	void GenerateMipmaps() override;

	// --- Queries ---
	uint32_t GetWidth() const override { return texWidth; }
	uint32_t GetHeight() const override { return texHeight; }
	uint32_t GetDepthOrLayers() const override { return texDepthOrLayers; }
	uint32_t GetMipLevels() const override { return texMipLevels; }
	TextureType GetType() const override { return texType; }
	TextureFormat GetFormat() const override { return texFormat; }
	uint32_t GetNativeHandle() const override { return 0; }  // Metal uses object pointers
	uint32_t GetSampleCount() const override { return texSampleCount; }

#ifdef __OBJC__
	// --- Metal-specific accessors ---
	id<MTLTexture> GetMTLTexture() const { return mtlTexture; }
	id<MTLSamplerState> GetSamplerState();

	// --- Format conversion helpers ---
	static MTLPixelFormat ToMTLPixelFormat(TextureFormat format);
	static MTLTextureType ToMTLTextureType(TextureType type);
	static MTLSamplerMinMagFilter ToMTLMinMagFilter(TextureFilter filter);
	static MTLSamplerMipFilter ToMTLMipFilter(TextureFilter filter);
	static MTLSamplerAddressMode ToMTLAddressMode(TextureWrap wrap);
	static MTLCompareFunction ToMTLCompareFunction(CompareFunc func);
#endif

private:
	void UpdateSamplerState();

#ifdef __OBJC__
	id<MTLTexture>      mtlTexture    = nil;
	id<MTLSamplerState> samplerState  = nil;
#else
	void*               mtlTexture    = nullptr;
	void*               samplerState  = nullptr;
#endif

	MTLDevice*    device;
	TextureType   texType;
	TextureFormat texFormat;
	uint32_t      texWidth;
	uint32_t      texHeight;
	uint32_t      texDepthOrLayers;
	uint32_t      texMipLevels;
	uint32_t      texSampleCount;

	// Sampler configuration
	TextureFilter minFilter    = TextureFilter::Linear;
	TextureFilter magFilter    = TextureFilter::Linear;
	TextureWrap   wrapS        = TextureWrap::Repeat;
	TextureWrap   wrapT        = TextureWrap::Repeat;
	TextureWrap   wrapR        = TextureWrap::Repeat;
	float         anisotropy   = 1.0f;
	bool          compareEnabled = false;
	CompareFunc   compareFunc  = CompareFunc::LessEqual;
	float         lodBias      = 0.0f;
	bool          samplerDirty = true;
};

} // namespace RHI

#endif // MTL_RHI_TEXTURE_H
