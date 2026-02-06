/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#ifndef RHI_TEXTURE_H
#define RHI_TEXTURE_H

/**
 * RHI Texture Interface
 *
 * Maps raw GL texture calls to backend-agnostic interface:
 *   glGenTextures              ->  IRHIDevice::CreateTexture()
 *   glDeleteTextures           ->  ~IRHITexture()
 *   glBindTexture              ->  IRHITexture::Bind(unit)
 *   glActiveTexture + glBind   ->  IRHITexture::Bind(unit) (combined)
 *   glTexImage2D / glTexStorage2D     ->  Allocate() done at creation
 *   glTexSubImage2D            ->  IRHITexture::Upload()
 *   glTexParameter (filter)    ->  IRHITexture::SetFilter()
 *   glTexParameter (wrap)      ->  IRHITexture::SetWrap()
 *   glGenerateMipmap           ->  IRHITexture::GenerateMipmaps()
 *
 * The engine uses TexBind.h for RAII binding, which maps naturally
 * to Bind()/Unbind() pairs at the RHI level.
 */

#include <cstdint>
#include "RHITypes.h"

namespace RHI {

class IRHITexture {
public:
	virtual ~IRHITexture() = default;

	// --- Binding ---
	virtual void Bind(uint32_t unit = 0) = 0;
	virtual void Unbind(uint32_t unit = 0) = 0;

	// --- Data upload ---
	virtual void Upload(
		uint32_t level,
		uint32_t x, uint32_t y,
		uint32_t width, uint32_t height,
		const void* data) = 0;

	virtual void Upload3D(
		uint32_t level,
		uint32_t x, uint32_t y, uint32_t z,
		uint32_t width, uint32_t height, uint32_t depth,
		const void* data) = 0;

	/// Upload compressed texture data (maps to glCompressedTexSubImage2D)
	virtual void UploadCompressed(
		uint32_t level,
		uint32_t x, uint32_t y,
		uint32_t width, uint32_t height,
		size_t dataSize,
		const void* data) = 0;

	// --- Sampling state ---
	virtual void SetMinFilter(TextureFilter filter) = 0;
	virtual void SetMagFilter(TextureFilter filter) = 0;
	virtual void SetWrapS(TextureWrap wrap) = 0;
	virtual void SetWrapT(TextureWrap wrap) = 0;
	virtual void SetWrapR(TextureWrap wrap) = 0;
	virtual void SetAnisotropy(float level) = 0;
	virtual void SetCompareMode(bool enabled, CompareFunc func = CompareFunc::LessEqual) = 0;

	// --- Extended sampling state ---
	/// Set texture swizzle mask (maps GL_TEXTURE_SWIZZLE_RGBA)
	/// Components: 0=Red, 1=Green, 2=Blue, 3=Alpha, 4=Zero, 5=One
	virtual void SetSwizzle(uint8_t r, uint8_t g, uint8_t b, uint8_t a) = 0;

	/// Set border color for ClampToBorder wrap mode (maps GL_TEXTURE_BORDER_COLOR)
	virtual void SetBorderColor(float r, float g, float b, float a) = 0;

	/// Set LOD bias for mipmap selection (maps GL_TEXTURE_LOD_BIAS)
	virtual void SetLodBias(float bias) = 0;

	// --- Mipmap ---
	virtual void GenerateMipmaps() = 0;

	// --- Queries ---
	virtual uint32_t GetWidth() const = 0;
	virtual uint32_t GetHeight() const = 0;
	virtual uint32_t GetDepthOrLayers() const = 0;
	virtual uint32_t GetMipLevels() const = 0;
	virtual TextureType GetType() const = 0;
	virtual TextureFormat GetFormat() const = 0;
	virtual uint32_t GetNativeHandle() const = 0;
	virtual uint32_t GetSampleCount() const = 0;
};

} // namespace RHI

#endif // RHI_TEXTURE_H
