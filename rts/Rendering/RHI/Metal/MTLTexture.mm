/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#import "MTLTexture.h"
#import "MTLDevice.h"

#import <Metal/Metal.h>
#include <cstring>

#include "System/Log/ILog.h"

namespace RHI {

MTLPixelFormat MTLTexture::ToMTLPixelFormat(TextureFormat format) {
	switch (format) {
		case TextureFormat::RGBA8:           return MTLPixelFormatRGBA8Unorm;
		case TextureFormat::RGB8:            return MTLPixelFormatRGBA8Unorm;  // RGB not directly supported
		case TextureFormat::RG8:             return MTLPixelFormatRG8Unorm;
		case TextureFormat::R8:              return MTLPixelFormatR8Unorm;
		case TextureFormat::RGBA16F:         return MTLPixelFormatRGBA16Float;
		case TextureFormat::RGB16F:          return MTLPixelFormatRGBA16Float;  // RGB not directly supported
		case TextureFormat::RG16F:           return MTLPixelFormatRG16Float;
		case TextureFormat::R16F:            return MTLPixelFormatR16Float;
		case TextureFormat::RGBA32F:         return MTLPixelFormatRGBA32Float;
		case TextureFormat::RGB32F:          return MTLPixelFormatRGBA32Float;  // RGB not directly supported
		case TextureFormat::RG32F:           return MTLPixelFormatRG32Float;
		case TextureFormat::R32F:            return MTLPixelFormatR32Float;
		case TextureFormat::R32I:            return MTLPixelFormatR32Sint;
		case TextureFormat::Depth16:         return MTLPixelFormatDepth16Unorm;
		case TextureFormat::Depth24:         return MTLPixelFormatDepth32Float;  // No 24-bit depth on Metal
		case TextureFormat::Depth32F:        return MTLPixelFormatDepth32Float;
		case TextureFormat::Depth24Stencil8: return MTLPixelFormatDepth24Unorm_Stencil8;
		case TextureFormat::Depth32FStencil8: return MTLPixelFormatDepth32Float_Stencil8;
		case TextureFormat::SRGB8Alpha8:     return MTLPixelFormatRGBA8Unorm_sRGB;
		case TextureFormat::CompressedDXT1:  return MTLPixelFormatBC1_RGBA;
		case TextureFormat::CompressedDXT5:  return MTLPixelFormatBC3_RGBA;
		case TextureFormat::CompressedETC2:  return MTLPixelFormatETC2_RGB8;
	}
	return MTLPixelFormatRGBA8Unorm;
}

MTLTextureType MTLTexture::ToMTLTextureType(TextureType type) {
	switch (type) {
		case TextureType::Texture1D:       return MTLTextureType1D;
		case TextureType::Texture2D:       return MTLTextureType2D;
		case TextureType::Texture3D:       return MTLTextureType3D;
		case TextureType::Texture1DArray:  return MTLTextureType1DArray;
		case TextureType::Texture2DArray:  return MTLTextureType2DArray;
		case TextureType::TextureCube:     return MTLTextureTypeCube;
		case TextureType::TextureRect:     return MTLTextureType2D;  // No rect texture in Metal
		case TextureType::TextureBuffer:   return MTLTextureTypeTextureBuffer;
		case TextureType::Texture2DMS:     return MTLTextureType2DMultisample;
	}
	return MTLTextureType2D;
}

MTLSamplerMinMagFilter MTLTexture::ToMTLMinMagFilter(TextureFilter filter) {
	switch (filter) {
		case TextureFilter::Nearest:
		case TextureFilter::NearestMipmapNearest:
		case TextureFilter::NearestMipmapLinear:
			return MTLSamplerMinMagFilterNearest;
		case TextureFilter::Linear:
		case TextureFilter::LinearMipmapNearest:
		case TextureFilter::LinearMipmapLinear:
			return MTLSamplerMinMagFilterLinear;
	}
	return MTLSamplerMinMagFilterLinear;
}

MTLSamplerMipFilter MTLTexture::ToMTLMipFilter(TextureFilter filter) {
	switch (filter) {
		case TextureFilter::Nearest:
		case TextureFilter::Linear:
			return MTLSamplerMipFilterNotMipmapped;
		case TextureFilter::NearestMipmapNearest:
		case TextureFilter::LinearMipmapNearest:
			return MTLSamplerMipFilterNearest;
		case TextureFilter::NearestMipmapLinear:
		case TextureFilter::LinearMipmapLinear:
			return MTLSamplerMipFilterLinear;
	}
	return MTLSamplerMipFilterNotMipmapped;
}

MTLSamplerAddressMode MTLTexture::ToMTLAddressMode(TextureWrap wrap) {
	switch (wrap) {
		case TextureWrap::Repeat:         return MTLSamplerAddressModeRepeat;
		case TextureWrap::ClampToEdge:    return MTLSamplerAddressModeClampToEdge;
		case TextureWrap::ClampToBorder:  return MTLSamplerAddressModeClampToBorderColor;
		case TextureWrap::MirroredRepeat: return MTLSamplerAddressModeMirrorRepeat;
	}
	return MTLSamplerAddressModeRepeat;
}

MTLCompareFunction MTLTexture::ToMTLCompareFunction(CompareFunc func) {
	switch (func) {
		case CompareFunc::Never:        return MTLCompareFunctionNever;
		case CompareFunc::Less:         return MTLCompareFunctionLess;
		case CompareFunc::LessEqual:    return MTLCompareFunctionLessEqual;
		case CompareFunc::Equal:        return MTLCompareFunctionEqual;
		case CompareFunc::NotEqual:     return MTLCompareFunctionNotEqual;
		case CompareFunc::GreaterEqual: return MTLCompareFunctionGreaterEqual;
		case CompareFunc::Greater:      return MTLCompareFunctionGreater;
		case CompareFunc::Always:       return MTLCompareFunctionAlways;
	}
	return MTLCompareFunctionLessEqual;
}

// Helper to calculate bytes per pixel
static size_t GetBytesPerPixel(TextureFormat format) {
	switch (format) {
		case TextureFormat::R8:         return 1;
		case TextureFormat::RG8:        return 2;
		case TextureFormat::RGB8:       return 4;  // Padded to RGBA
		case TextureFormat::RGBA8:      return 4;
		case TextureFormat::R16F:       return 2;
		case TextureFormat::RG16F:      return 4;
		case TextureFormat::RGB16F:     return 8;  // Padded to RGBA
		case TextureFormat::RGBA16F:    return 8;
		case TextureFormat::R32F:       return 4;
		case TextureFormat::RG32F:      return 8;
		case TextureFormat::RGB32F:     return 16; // Padded to RGBA
		case TextureFormat::RGBA32F:    return 16;
		case TextureFormat::R32I:       return 4;
		case TextureFormat::Depth16:    return 2;
		case TextureFormat::Depth24:    return 4;
		case TextureFormat::Depth32F:   return 4;
		case TextureFormat::Depth24Stencil8:  return 4;
		case TextureFormat::Depth32FStencil8: return 8;
		case TextureFormat::SRGB8Alpha8:      return 4;
		case TextureFormat::CompressedDXT1:   return 0;  // Block compressed
		case TextureFormat::CompressedDXT5:   return 0;  // Block compressed
		case TextureFormat::CompressedETC2:   return 0;  // Block compressed
	}
	return 4;
}

MTLTexture::MTLTexture(MTLDevice* device, TextureType type, TextureFormat format,
                       uint32_t width, uint32_t height, uint32_t depthOrLayers,
                       uint32_t mipLevels, uint32_t sampleCount)
	: device(device)
	, texType(type)
	, texFormat(format)
	, texWidth(width)
	, texHeight(height)
	, texDepthOrLayers(depthOrLayers)
	, texMipLevels(mipLevels)
	, texSampleCount(sampleCount)
{
	if (!device || !device->IsValid()) {
		LOG_L(L_ERROR, "[MTLTexture] Cannot create texture: invalid device");
		return;
	}

	// Create texture descriptor
	MTLTextureDescriptor* desc = [[MTLTextureDescriptor alloc] init];
	desc.textureType = ToMTLTextureType(type);
	desc.pixelFormat = ToMTLPixelFormat(format);
	desc.width = width;
	desc.height = height;

	// Set depth/array layers based on texture type
	switch (type) {
		case TextureType::Texture3D:
			desc.depth = depthOrLayers;
			desc.arrayLength = 1;
			break;
		case TextureType::TextureCube:
			desc.depth = 1;
			desc.arrayLength = 1;  // Cube maps have 6 faces internally
			break;
		case TextureType::Texture1DArray:
		case TextureType::Texture2DArray:
			desc.depth = 1;
			desc.arrayLength = depthOrLayers;
			break;
		default:
			desc.depth = 1;
			desc.arrayLength = 1;
			break;
	}

	desc.mipmapLevelCount = mipLevels;
	desc.sampleCount = (type == TextureType::Texture2DMS) ? sampleCount : 1;

	// Storage mode and usage
	// Use shared for CPU-accessible textures, private for GPU-only
	bool isDepthStencil = (format == TextureFormat::Depth16 ||
	                       format == TextureFormat::Depth24 ||
	                       format == TextureFormat::Depth32F ||
	                       format == TextureFormat::Depth24Stencil8 ||
	                       format == TextureFormat::Depth32FStencil8);

	desc.storageMode = MTLStorageModeShared;
	desc.usage = MTLTextureUsageShaderRead;

	if (isDepthStencil) {
		desc.storageMode = MTLStorageModePrivate;
		desc.usage |= MTLTextureUsageRenderTarget;
	}

	// Allow as render target for color formats too
	if (!isDepthStencil) {
		desc.usage |= MTLTextureUsageRenderTarget;
	}

	// Create the texture
	mtlTexture = [device->GetMTLDevice() newTextureWithDescriptor:desc];

	if (!mtlTexture) {
		LOG_L(L_ERROR, "[MTLTexture] Failed to create Metal texture %ux%u", width, height);
		return;
	}

	// Set a label for debugging
	mtlTexture.label = [NSString stringWithFormat:@"RHI Texture %ux%u", width, height];
}

MTLTexture::~MTLTexture() {
	samplerState = nil;
	mtlTexture = nil;
}

void MTLTexture::Bind(uint32_t unit) {
	// Metal doesn't have global texture binding state.
	// Textures are set directly on command encoder.
	(void)unit;
}

void MTLTexture::Unbind(uint32_t unit) {
	(void)unit;
}

void MTLTexture::Upload(uint32_t level, uint32_t x, uint32_t y,
                        uint32_t width, uint32_t height, const void* data) {
	if (!mtlTexture || !data) {
		return;
	}

	size_t bytesPerPixel = GetBytesPerPixel(texFormat);
	size_t bytesPerRow = width * bytesPerPixel;

	MTLRegion region = MTLRegionMake2D(x, y, width, height);

	[mtlTexture replaceRegion:region
	              mipmapLevel:level
	                    slice:0
	                withBytes:data
	              bytesPerRow:bytesPerRow
	            bytesPerImage:0];
}

void MTLTexture::Upload3D(uint32_t level, uint32_t x, uint32_t y, uint32_t z,
                          uint32_t width, uint32_t height, uint32_t depth,
                          const void* data) {
	if (!mtlTexture || !data) {
		return;
	}

	size_t bytesPerPixel = GetBytesPerPixel(texFormat);
	size_t bytesPerRow = width * bytesPerPixel;
	size_t bytesPerImage = bytesPerRow * height;

	MTLRegion region = MTLRegionMake3D(x, y, z, width, height, depth);

	[mtlTexture replaceRegion:region
	              mipmapLevel:level
	                    slice:0
	                withBytes:data
	              bytesPerRow:bytesPerRow
	            bytesPerImage:bytesPerImage];
}

void MTLTexture::UploadCompressed(uint32_t level, uint32_t x, uint32_t y,
                                  uint32_t width, uint32_t height,
                                  size_t dataSize, const void* data) {
	if (!mtlTexture || !data) {
		return;
	}

	// Block compressed formats (BC1/DXT1, BC3/DXT5)
	// Block size is 4x4 pixels
	size_t blockSize = 4;
	size_t blocksWide = (width + blockSize - 1) / blockSize;
	// DXT1 and ETC2 use 8 bytes per 4x4 block, DXT5 uses 16
	size_t bytesPerBlock = (texFormat == TextureFormat::CompressedDXT5) ? 16 : 8;
	size_t bytesPerRow = blocksWide * bytesPerBlock;

	MTLRegion region = MTLRegionMake2D(x, y, width, height);

	[mtlTexture replaceRegion:region
	              mipmapLevel:level
	                    slice:0
	                withBytes:data
	              bytesPerRow:bytesPerRow
	            bytesPerImage:0];
}

void MTLTexture::UploadCubeFace(CubeFace face, uint32_t level,
                                 uint32_t width, uint32_t height,
                                 const void* data) {
	if (!mtlTexture || !data) {
		return;
	}

	size_t bytesPerPixel = GetBytesPerPixel(texFormat);
	size_t bytesPerRow = width * bytesPerPixel;

	MTLRegion region = MTLRegionMake2D(0, 0, width, height);

	// Metal cubemap faces are accessed via slice index
	[mtlTexture replaceRegion:region
	              mipmapLevel:level
	                    slice:static_cast<uint32_t>(face)
	                withBytes:data
	              bytesPerRow:bytesPerRow
	            bytesPerImage:0];
}

void MTLTexture::UpdateCubeFace(CubeFace face, uint32_t level,
                                 uint32_t x, uint32_t y,
                                 uint32_t width, uint32_t height,
                                 const void* data) {
	if (!mtlTexture || !data) {
		return;
	}

	size_t bytesPerPixel = GetBytesPerPixel(texFormat);
	size_t bytesPerRow = width * bytesPerPixel;

	MTLRegion region = MTLRegionMake2D(x, y, width, height);

	// Metal cubemap faces are accessed via slice index
	[mtlTexture replaceRegion:region
	              mipmapLevel:level
	                    slice:static_cast<uint32_t>(face)
	                withBytes:data
	              bytesPerRow:bytesPerRow
	            bytesPerImage:0];
}

void MTLTexture::SetMinFilter(TextureFilter filter) {
	if (minFilter != filter) {
		minFilter = filter;
		samplerDirty = true;
	}
}

void MTLTexture::SetMagFilter(TextureFilter filter) {
	if (magFilter != filter) {
		magFilter = filter;
		samplerDirty = true;
	}
}

void MTLTexture::SetWrapS(TextureWrap wrap) {
	if (wrapS != wrap) {
		wrapS = wrap;
		samplerDirty = true;
	}
}

void MTLTexture::SetWrapT(TextureWrap wrap) {
	if (wrapT != wrap) {
		wrapT = wrap;
		samplerDirty = true;
	}
}

void MTLTexture::SetWrapR(TextureWrap wrap) {
	if (wrapR != wrap) {
		wrapR = wrap;
		samplerDirty = true;
	}
}

void MTLTexture::SetAnisotropy(float level) {
	if (anisotropy != level) {
		anisotropy = level;
		samplerDirty = true;
	}
}

void MTLTexture::SetCompareMode(bool enabled, CompareFunc func) {
	if (compareEnabled != enabled || compareFunc != func) {
		compareEnabled = enabled;
		compareFunc = func;
		samplerDirty = true;
	}
}

void MTLTexture::SetSwizzle(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
	// Metal doesn't support runtime swizzle like OpenGL.
	// Swizzle would need to be done in the shader or by creating a texture view.
	// For now, log a warning if non-identity swizzle is requested.
	if (r != 0 || g != 1 || b != 2 || a != 3) {
		LOG_L(L_WARNING, "[MTLTexture] Texture swizzle not supported in Metal backend");
	}
}

void MTLTexture::SetBorderColor(float r, float g, float b, float a) {
	// Metal supports limited border colors (transparent black, opaque black, opaque white)
	// Custom colors are not directly supported
	(void)r; (void)g; (void)b; (void)a;
}

void MTLTexture::SetLodBias(float bias) {
	if (lodBias != bias) {
		lodBias = bias;
		samplerDirty = true;
	}
}

void MTLTexture::SetMinLOD(float minLod) {
	if (lodMinClamp != minLod) {
		lodMinClamp = minLod;
		samplerDirty = true;
	}
}

void MTLTexture::SetMaxLOD(float maxLod) {
	if (lodMaxClamp != maxLod) {
		lodMaxClamp = maxLod;
		samplerDirty = true;
	}
}

void MTLTexture::GenerateMipmaps() {
	if (!mtlTexture || !device || !device->IsValid()) {
		return;
	}

	// Create a blit command encoder to generate mipmaps
	id<MTLCommandBuffer> cmdBuf = [device->GetCommandQueue() commandBuffer];
	id<MTLBlitCommandEncoder> blitEncoder = [cmdBuf blitCommandEncoder];

	[blitEncoder generateMipmapsForTexture:mtlTexture];
	[blitEncoder endEncoding];

	[cmdBuf commit];
	[cmdBuf waitUntilCompleted];
}

void MTLTexture::UpdateSamplerState() {
	if (!device || !device->IsValid()) {
		return;
	}

	MTLSamplerDescriptor* desc = [[MTLSamplerDescriptor alloc] init];
	desc.minFilter = ToMTLMinMagFilter(minFilter);
	desc.magFilter = ToMTLMinMagFilter(magFilter);
	desc.mipFilter = ToMTLMipFilter(minFilter);
	desc.sAddressMode = ToMTLAddressMode(wrapS);
	desc.tAddressMode = ToMTLAddressMode(wrapT);
	desc.rAddressMode = ToMTLAddressMode(wrapR);
	desc.maxAnisotropy = static_cast<NSUInteger>(std::max(1.0f, anisotropy));
	desc.lodMinClamp = lodMinClamp;
	desc.lodMaxClamp = lodMaxClamp;

	if (compareEnabled) {
		desc.compareFunction = ToMTLCompareFunction(compareFunc);
	}

	// LOD bias support
	if (@available(macOS 10.13, *)) {
		desc.lodAverage = NO;
	}

	samplerState = [device->GetMTLDevice() newSamplerStateWithDescriptor:desc];
	samplerDirty = false;
}

id<MTLSamplerState> MTLTexture::GetSamplerState() {
	if (samplerDirty || !samplerState) {
		UpdateSamplerState();
	}
	return samplerState;
}

uint32_t MTLTexture::DisownNativeHandle() {
	mtlTexture = nil;  // Release the Metal texture reference
	return 0;          // Metal doesn't use integer handles
}

} // namespace RHI
