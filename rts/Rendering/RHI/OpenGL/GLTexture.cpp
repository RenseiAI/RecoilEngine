/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#include "GLTexture.h"

namespace RHI {

// --- Format conversion tables ---

GLenum GLTexture::ToGLTarget(TextureType type) {
	switch (type) {
		case TextureType::Texture1D:      return GL_TEXTURE_1D;
		case TextureType::Texture2D:      return GL_TEXTURE_2D;
		case TextureType::Texture3D:      return GL_TEXTURE_3D;
		case TextureType::Texture1DArray: return GL_TEXTURE_1D_ARRAY;
		case TextureType::Texture2DArray: return GL_TEXTURE_2D_ARRAY;
		case TextureType::TextureCube:    return GL_TEXTURE_CUBE_MAP;
		case TextureType::TextureRect:    return GL_TEXTURE_RECTANGLE;
		case TextureType::TextureBuffer:  return GL_TEXTURE_BUFFER;
		case TextureType::Texture2DMS:    return GL_TEXTURE_2D_MULTISAMPLE;
	}
	return GL_TEXTURE_2D;
}

GLenum GLTexture::ToGLInternalFormat(TextureFormat format) {
	switch (format) {
		case TextureFormat::RGBA8:             return GL_RGBA8;
		case TextureFormat::RGB8:              return GL_RGB8;
		case TextureFormat::RG8:               return GL_RG8;
		case TextureFormat::R8:                return GL_R8;
		case TextureFormat::RGBA16F:           return GL_RGBA16F;
		case TextureFormat::RGB16F:            return GL_RGB16F;
		case TextureFormat::RG16F:             return GL_RG16F;
		case TextureFormat::R16F:              return GL_R16F;
		case TextureFormat::RGBA32F:           return GL_RGBA32F;
		case TextureFormat::RGB32F:            return GL_RGB32F;
		case TextureFormat::RG32F:             return GL_RG32F;
		case TextureFormat::R32F:              return GL_R32F;
		case TextureFormat::R32I:              return GL_R32I;
		case TextureFormat::Depth16:           return GL_DEPTH_COMPONENT16;
		case TextureFormat::Depth24:           return GL_DEPTH_COMPONENT24;
		case TextureFormat::Depth32F:          return GL_DEPTH_COMPONENT32F;
		case TextureFormat::Depth24Stencil8:   return GL_DEPTH24_STENCIL8;
		case TextureFormat::Depth32FStencil8:  return GL_DEPTH32F_STENCIL8;
		case TextureFormat::SRGB8Alpha8:       return GL_SRGB8_ALPHA8;
		case TextureFormat::CompressedDXT1:    return GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
		case TextureFormat::CompressedDXT5:    return GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
	}
	return GL_RGBA8;
}

GLenum GLTexture::ToGLFormat(TextureFormat format) {
	switch (format) {
		case TextureFormat::RGBA8:
		case TextureFormat::RGBA16F:
		case TextureFormat::RGBA32F:
		case TextureFormat::SRGB8Alpha8:
		case TextureFormat::CompressedDXT1:
		case TextureFormat::CompressedDXT5:    return GL_RGBA;
		case TextureFormat::RGB8:
		case TextureFormat::RGB16F:
		case TextureFormat::RGB32F:            return GL_RGB;
		case TextureFormat::RG8:
		case TextureFormat::RG16F:
		case TextureFormat::RG32F:             return GL_RG;
		case TextureFormat::R8:
		case TextureFormat::R16F:
		case TextureFormat::R32F:              return GL_RED;
		case TextureFormat::R32I:              return GL_RED_INTEGER;
		case TextureFormat::Depth16:
		case TextureFormat::Depth24:
		case TextureFormat::Depth32F:          return GL_DEPTH_COMPONENT;
		case TextureFormat::Depth24Stencil8:
		case TextureFormat::Depth32FStencil8:  return GL_DEPTH_STENCIL;
	}
	return GL_RGBA;
}

GLenum GLTexture::ToGLDataType(TextureFormat format) {
	switch (format) {
		case TextureFormat::RGBA8:
		case TextureFormat::RGB8:
		case TextureFormat::RG8:
		case TextureFormat::R8:
		case TextureFormat::SRGB8Alpha8:       return GL_UNSIGNED_BYTE;
		case TextureFormat::RGBA16F:
		case TextureFormat::RGB16F:
		case TextureFormat::RG16F:
		case TextureFormat::R16F:              return GL_HALF_FLOAT;
		case TextureFormat::RGBA32F:
		case TextureFormat::RGB32F:
		case TextureFormat::RG32F:
		case TextureFormat::R32F:
		case TextureFormat::Depth32F:          return GL_FLOAT;
		case TextureFormat::R32I:              return GL_INT;
		case TextureFormat::Depth16:           return GL_UNSIGNED_SHORT;
		case TextureFormat::Depth24:           return GL_UNSIGNED_INT;
		case TextureFormat::Depth24Stencil8:   return GL_UNSIGNED_INT_24_8;
		case TextureFormat::Depth32FStencil8:  return GL_FLOAT_32_UNSIGNED_INT_24_8_REV;
		default:                               return GL_UNSIGNED_BYTE;
	}
}

GLenum GLTexture::ToGLFilter(TextureFilter filter) {
	switch (filter) {
		case TextureFilter::Nearest:              return GL_NEAREST;
		case TextureFilter::Linear:               return GL_LINEAR;
		case TextureFilter::NearestMipmapNearest: return GL_NEAREST_MIPMAP_NEAREST;
		case TextureFilter::LinearMipmapNearest:  return GL_LINEAR_MIPMAP_NEAREST;
		case TextureFilter::NearestMipmapLinear:  return GL_NEAREST_MIPMAP_LINEAR;
		case TextureFilter::LinearMipmapLinear:   return GL_LINEAR_MIPMAP_LINEAR;
	}
	return GL_LINEAR;
}

GLenum GLTexture::ToGLWrap(TextureWrap wrap) {
	switch (wrap) {
		case TextureWrap::Repeat:         return GL_REPEAT;
		case TextureWrap::ClampToEdge:    return GL_CLAMP_TO_EDGE;
		case TextureWrap::ClampToBorder:  return GL_CLAMP_TO_BORDER;
		case TextureWrap::MirroredRepeat: return GL_MIRRORED_REPEAT;
	}
	return GL_REPEAT;
}

// --- Constructor / Destructor ---

GLTexture::GLTexture(TextureType type, TextureFormat format, uint32_t width, uint32_t height, uint32_t depthOrLayers, uint32_t mipLevels, uint32_t sampleCount)
	: texId(0)
	, glTarget(ToGLTarget(type))
	, glInternalFormat(ToGLInternalFormat(format))
	, texType(type)
	, texFormat(format)
	, texWidth(width)
	, texHeight(height)
	, texDepthOrLayers(depthOrLayers)
	, texMipLevels(mipLevels)
	, texSampleCount(sampleCount)
{
	glGenTextures(1, &texId);
	glBindTexture(glTarget, texId);

	// Allocate storage
	switch (type) {
		case TextureType::Texture2D:
		case TextureType::TextureRect:
			glTexStorage2D(glTarget, mipLevels, glInternalFormat, width, height);
			break;
		case TextureType::Texture3D:
		case TextureType::Texture2DArray:
			glTexStorage3D(glTarget, mipLevels, glInternalFormat, width, height, depthOrLayers);
			break;
		case TextureType::Texture1D:
			glTexStorage1D(glTarget, mipLevels, glInternalFormat, width);
			break;
		case TextureType::TextureCube:
			glTexStorage2D(glTarget, mipLevels, glInternalFormat, width, height);
			break;
		case TextureType::Texture2DMS:
			// MSAA texture - use glTexImage2DMultisample
			glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, sampleCount, glInternalFormat, width, height, GL_TRUE);
			break;
		default:
			// TextureBuffer, etc. handled separately
			break;
	}

	glBindTexture(glTarget, 0);
}

GLTexture::~GLTexture() {
	if (texId != 0) {
		glDeleteTextures(1, &texId);
		texId = 0;
	}
}

// --- Binding ---

void GLTexture::Bind(uint32_t unit) {
	glActiveTexture(GL_TEXTURE0 + unit);
	glBindTexture(glTarget, texId);
}

void GLTexture::Unbind(uint32_t unit) {
	glActiveTexture(GL_TEXTURE0 + unit);
	glBindTexture(glTarget, 0);
}

// --- Data upload ---

void GLTexture::Upload(uint32_t level, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const void* data) {
	glBindTexture(glTarget, texId);
	glTexSubImage2D(glTarget, level, x, y, w, h, ToGLFormat(texFormat), ToGLDataType(texFormat), data);
}

void GLTexture::Upload3D(uint32_t level, uint32_t x, uint32_t y, uint32_t z, uint32_t w, uint32_t h, uint32_t d, const void* data) {
	glBindTexture(glTarget, texId);
	glTexSubImage3D(glTarget, level, x, y, z, w, h, d, ToGLFormat(texFormat), ToGLDataType(texFormat), data);
}

void GLTexture::UploadCompressed(uint32_t level, uint32_t x, uint32_t y, uint32_t w, uint32_t h, size_t dataSize, const void* data) {
	glBindTexture(glTarget, texId);
	glCompressedTexSubImage2D(glTarget, level, x, y, w, h, glInternalFormat, static_cast<GLsizei>(dataSize), data);
}

void GLTexture::UploadCubeFace(CubeFace face, uint32_t level, uint32_t w, uint32_t h, const void* data) {
	glBindTexture(glTarget, texId);
	GLenum glFace = GL_TEXTURE_CUBE_MAP_POSITIVE_X + static_cast<uint32_t>(face);
	glTexImage2D(glFace, level, glInternalFormat, w, h, 0, ToGLFormat(texFormat), ToGLDataType(texFormat), data);
}

void GLTexture::UpdateCubeFace(CubeFace face, uint32_t level, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const void* data) {
	glBindTexture(glTarget, texId);
	GLenum glFace = GL_TEXTURE_CUBE_MAP_POSITIVE_X + static_cast<uint32_t>(face);
	glTexSubImage2D(glFace, level, x, y, w, h, ToGLFormat(texFormat), ToGLDataType(texFormat), data);
}

// --- Sampling state ---

void GLTexture::SetMinFilter(TextureFilter filter) {
	glBindTexture(glTarget, texId);
	glTexParameteri(glTarget, GL_TEXTURE_MIN_FILTER, ToGLFilter(filter));
}

void GLTexture::SetMagFilter(TextureFilter filter) {
	glBindTexture(glTarget, texId);
	glTexParameteri(glTarget, GL_TEXTURE_MAG_FILTER, ToGLFilter(filter));
}

void GLTexture::SetWrapS(TextureWrap wrap) {
	glBindTexture(glTarget, texId);
	glTexParameteri(glTarget, GL_TEXTURE_WRAP_S, ToGLWrap(wrap));
}

void GLTexture::SetWrapT(TextureWrap wrap) {
	glBindTexture(glTarget, texId);
	glTexParameteri(glTarget, GL_TEXTURE_WRAP_T, ToGLWrap(wrap));
}

void GLTexture::SetWrapR(TextureWrap wrap) {
	glBindTexture(glTarget, texId);
	glTexParameteri(glTarget, GL_TEXTURE_WRAP_R, ToGLWrap(wrap));
}

void GLTexture::SetAnisotropy(float level) {
	glBindTexture(glTarget, texId);
	glTexParameterf(glTarget, GL_TEXTURE_MAX_ANISOTROPY_EXT, level);
}

void GLTexture::SetCompareMode(bool enabled, CompareFunc func) {
	glBindTexture(glTarget, texId);
	if (enabled) {
		glTexParameteri(glTarget, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
		GLenum glFunc = GL_LEQUAL;
		switch (func) {
			case CompareFunc::Never:        glFunc = GL_NEVER; break;
			case CompareFunc::Less:         glFunc = GL_LESS; break;
			case CompareFunc::LessEqual:    glFunc = GL_LEQUAL; break;
			case CompareFunc::Equal:        glFunc = GL_EQUAL; break;
			case CompareFunc::NotEqual:     glFunc = GL_NOTEQUAL; break;
			case CompareFunc::GreaterEqual: glFunc = GL_GEQUAL; break;
			case CompareFunc::Greater:      glFunc = GL_GREATER; break;
			case CompareFunc::Always:       glFunc = GL_ALWAYS; break;
		}
		glTexParameteri(glTarget, GL_TEXTURE_COMPARE_FUNC, glFunc);
	} else {
		glTexParameteri(glTarget, GL_TEXTURE_COMPARE_MODE, GL_NONE);
	}
}

void GLTexture::SetSwizzle(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
	glBindTexture(glTarget, texId);
	auto toGLSwizzle = [](uint8_t c) -> GLint {
		switch (c) {
			case 0: return GL_RED;
			case 1: return GL_GREEN;
			case 2: return GL_BLUE;
			case 3: return GL_ALPHA;
			case 4: return GL_ZERO;
			case 5: return GL_ONE;
			default: return GL_RED;
		}
	};
	GLint swizzle[4] = { toGLSwizzle(r), toGLSwizzle(g), toGLSwizzle(b), toGLSwizzle(a) };
	glTexParameteriv(glTarget, GL_TEXTURE_SWIZZLE_RGBA, swizzle);
}

void GLTexture::SetBorderColor(float r, float g, float b, float a) {
	glBindTexture(glTarget, texId);
	GLfloat color[4] = { r, g, b, a };
	glTexParameterfv(glTarget, GL_TEXTURE_BORDER_COLOR, color);
}

void GLTexture::SetLodBias(float bias) {
	glBindTexture(glTarget, texId);
	glTexParameterf(glTarget, GL_TEXTURE_LOD_BIAS, bias);
}

void GLTexture::GenerateMipmaps() {
	glBindTexture(glTarget, texId);
	glGenerateMipmap(glTarget);
}

uint32_t GLTexture::DisownNativeHandle() {
	uint32_t handle = texId;
	texId = 0;  // Prevent destructor from calling glDeleteTextures
	return handle;
}

} // namespace RHI
