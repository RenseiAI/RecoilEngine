/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#ifndef LUA_GL_CONST_MAPPINGS_H
#define LUA_GL_CONST_MAPPINGS_H

/**
 * GL Constant to RHI Enum Mappings for Lua Bindings
 *
 * Lua scripts continue to use GL_* constants (like GL.TRIANGLES, GL.SRC_ALPHA)
 * for backward compatibility. These helper functions convert those raw GL enum
 * values to RHI enum types used by the rendering abstraction layer.
 *
 * Note: Lua constants remain unchanged - this layer handles the translation
 * internally within the Lua binding implementations.
 */

#include "Rendering/GL/myGL.h"
#include "Rendering/RHI/RHITypes.h"

namespace LuaGLConstMappings {

// --- Primitive Type Mappings ---

inline RHI::PrimitiveType GLPrimitiveToRHI(GLenum glPrimitive) {
	switch (glPrimitive) {
		case GL_POINTS:         return RHI::PrimitiveType::Points;
		case GL_LINES:          return RHI::PrimitiveType::Lines;
		case GL_LINE_STRIP:     return RHI::PrimitiveType::LineStrip;
		case GL_TRIANGLES:      return RHI::PrimitiveType::Triangles;
		case GL_TRIANGLE_STRIP: return RHI::PrimitiveType::TriangleStrip;
		case GL_TRIANGLE_FAN:   return RHI::PrimitiveType::TriangleFan;
		// Legacy primitives map to triangles (no direct RHI equivalent)
		case GL_QUADS:          return RHI::PrimitiveType::Triangles;
		case GL_QUAD_STRIP:     return RHI::PrimitiveType::TriangleStrip;
		case GL_POLYGON:        return RHI::PrimitiveType::TriangleFan;
		case GL_LINE_LOOP:      return RHI::PrimitiveType::LineStrip;
		default:                return RHI::PrimitiveType::Triangles;
	}
}

inline bool IsLegacyPrimitive(GLenum glPrimitive) {
	return (glPrimitive == GL_QUADS ||
	        glPrimitive == GL_QUAD_STRIP ||
	        glPrimitive == GL_POLYGON ||
	        glPrimitive == GL_LINE_LOOP);
}

// --- Blend Factor Mappings ---

inline RHI::BlendFactor GLBlendFactorToRHI(GLenum glFactor) {
	switch (glFactor) {
		case GL_ZERO:                     return RHI::BlendFactor::Zero;
		case GL_ONE:                      return RHI::BlendFactor::One;
		case GL_SRC_COLOR:                return RHI::BlendFactor::SrcColor;
		case GL_ONE_MINUS_SRC_COLOR:      return RHI::BlendFactor::OneMinusSrcColor;
		case GL_DST_COLOR:                return RHI::BlendFactor::DstColor;
		case GL_ONE_MINUS_DST_COLOR:      return RHI::BlendFactor::OneMinusDstColor;
		case GL_SRC_ALPHA:                return RHI::BlendFactor::SrcAlpha;
		case GL_ONE_MINUS_SRC_ALPHA:      return RHI::BlendFactor::OneMinusSrcAlpha;
		case GL_DST_ALPHA:                return RHI::BlendFactor::DstAlpha;
		case GL_ONE_MINUS_DST_ALPHA:      return RHI::BlendFactor::OneMinusDstAlpha;
		case GL_CONSTANT_COLOR:           return RHI::BlendFactor::ConstantColor;
		case GL_ONE_MINUS_CONSTANT_COLOR: return RHI::BlendFactor::OneMinusConstantColor;
		case GL_CONSTANT_ALPHA:           return RHI::BlendFactor::ConstantAlpha;
		case GL_ONE_MINUS_CONSTANT_ALPHA: return RHI::BlendFactor::OneMinusConstantAlpha;
		case GL_SRC_ALPHA_SATURATE:       return RHI::BlendFactor::SrcAlphaSaturate;
		default:                          return RHI::BlendFactor::One;
	}
}

inline GLenum RHIBlendFactorToGL(RHI::BlendFactor factor) {
	switch (factor) {
		case RHI::BlendFactor::Zero:                  return GL_ZERO;
		case RHI::BlendFactor::One:                   return GL_ONE;
		case RHI::BlendFactor::SrcColor:              return GL_SRC_COLOR;
		case RHI::BlendFactor::OneMinusSrcColor:      return GL_ONE_MINUS_SRC_COLOR;
		case RHI::BlendFactor::DstColor:              return GL_DST_COLOR;
		case RHI::BlendFactor::OneMinusDstColor:      return GL_ONE_MINUS_DST_COLOR;
		case RHI::BlendFactor::SrcAlpha:              return GL_SRC_ALPHA;
		case RHI::BlendFactor::OneMinusSrcAlpha:      return GL_ONE_MINUS_SRC_ALPHA;
		case RHI::BlendFactor::DstAlpha:              return GL_DST_ALPHA;
		case RHI::BlendFactor::OneMinusDstAlpha:      return GL_ONE_MINUS_DST_ALPHA;
		case RHI::BlendFactor::ConstantColor:         return GL_CONSTANT_COLOR;
		case RHI::BlendFactor::OneMinusConstantColor: return GL_ONE_MINUS_CONSTANT_COLOR;
		case RHI::BlendFactor::ConstantAlpha:         return GL_CONSTANT_ALPHA;
		case RHI::BlendFactor::OneMinusConstantAlpha: return GL_ONE_MINUS_CONSTANT_ALPHA;
		case RHI::BlendFactor::SrcAlphaSaturate:      return GL_SRC_ALPHA_SATURATE;
		default:                                      return GL_ONE;
	}
}

// --- Blend Operation Mappings ---

inline RHI::BlendOp GLBlendOpToRHI(GLenum glOp) {
	switch (glOp) {
		case GL_FUNC_ADD:              return RHI::BlendOp::Add;
		case GL_FUNC_SUBTRACT:         return RHI::BlendOp::Subtract;
		case GL_FUNC_REVERSE_SUBTRACT: return RHI::BlendOp::ReverseSubtract;
		case GL_MIN:                   return RHI::BlendOp::Min;
		case GL_MAX:                   return RHI::BlendOp::Max;
		default:                       return RHI::BlendOp::Add;
	}
}

inline GLenum RHIBlendOpToGL(RHI::BlendOp op) {
	switch (op) {
		case RHI::BlendOp::Add:             return GL_FUNC_ADD;
		case RHI::BlendOp::Subtract:        return GL_FUNC_SUBTRACT;
		case RHI::BlendOp::ReverseSubtract: return GL_FUNC_REVERSE_SUBTRACT;
		case RHI::BlendOp::Min:             return GL_MIN;
		case RHI::BlendOp::Max:             return GL_MAX;
		default:                            return GL_FUNC_ADD;
	}
}

// --- Compare Function Mappings ---

inline RHI::CompareFunc GLCompareFuncToRHI(GLenum glFunc) {
	switch (glFunc) {
		case GL_NEVER:    return RHI::CompareFunc::Never;
		case GL_LESS:     return RHI::CompareFunc::Less;
		case GL_LEQUAL:   return RHI::CompareFunc::LessEqual;
		case GL_EQUAL:    return RHI::CompareFunc::Equal;
		case GL_NOTEQUAL: return RHI::CompareFunc::NotEqual;
		case GL_GEQUAL:   return RHI::CompareFunc::GreaterEqual;
		case GL_GREATER:  return RHI::CompareFunc::Greater;
		case GL_ALWAYS:   return RHI::CompareFunc::Always;
		default:          return RHI::CompareFunc::Less;
	}
}

inline GLenum RHICompareFuncToGL(RHI::CompareFunc func) {
	switch (func) {
		case RHI::CompareFunc::Never:        return GL_NEVER;
		case RHI::CompareFunc::Less:         return GL_LESS;
		case RHI::CompareFunc::LessEqual:    return GL_LEQUAL;
		case RHI::CompareFunc::Equal:        return GL_EQUAL;
		case RHI::CompareFunc::NotEqual:     return GL_NOTEQUAL;
		case RHI::CompareFunc::GreaterEqual: return GL_GEQUAL;
		case RHI::CompareFunc::Greater:      return GL_GREATER;
		case RHI::CompareFunc::Always:       return GL_ALWAYS;
		default:                             return GL_LESS;
	}
}

// --- Stencil Operation Mappings ---

inline RHI::StencilOp GLStencilOpToRHI(GLenum glOp) {
	switch (glOp) {
		case GL_KEEP:      return RHI::StencilOp::Keep;
		case GL_ZERO:      return RHI::StencilOp::Zero;
		case GL_REPLACE:   return RHI::StencilOp::Replace;
		case GL_INCR:      return RHI::StencilOp::IncrClamp;
		case GL_DECR:      return RHI::StencilOp::DecrClamp;
		case GL_INVERT:    return RHI::StencilOp::Invert;
		case GL_INCR_WRAP: return RHI::StencilOp::IncrWrap;
		case GL_DECR_WRAP: return RHI::StencilOp::DecrWrap;
		default:           return RHI::StencilOp::Keep;
	}
}

inline GLenum RHIStencilOpToGL(RHI::StencilOp op) {
	switch (op) {
		case RHI::StencilOp::Keep:      return GL_KEEP;
		case RHI::StencilOp::Zero:      return GL_ZERO;
		case RHI::StencilOp::Replace:   return GL_REPLACE;
		case RHI::StencilOp::IncrClamp: return GL_INCR;
		case RHI::StencilOp::DecrClamp: return GL_DECR;
		case RHI::StencilOp::Invert:    return GL_INVERT;
		case RHI::StencilOp::IncrWrap:  return GL_INCR_WRAP;
		case RHI::StencilOp::DecrWrap:  return GL_DECR_WRAP;
		default:                        return GL_KEEP;
	}
}

// --- Cull Mode Mappings ---

inline RHI::CullMode GLCullModeToRHI(GLenum glMode) {
	switch (glMode) {
		case GL_FRONT:          return RHI::CullMode::Front;
		case GL_BACK:           return RHI::CullMode::Back;
		case GL_FRONT_AND_BACK: return RHI::CullMode::None; // disable drawing entirely
		default:                return RHI::CullMode::Back;
	}
}

inline GLenum RHICullModeToGL(RHI::CullMode mode) {
	switch (mode) {
		case RHI::CullMode::None:  return GL_NONE; // special case - disable culling
		case RHI::CullMode::Front: return GL_FRONT;
		case RHI::CullMode::Back:  return GL_BACK;
		default:                   return GL_BACK;
	}
}

// --- Polygon Mode Mappings ---

inline RHI::PolygonMode GLPolygonModeToRHI(GLenum glMode) {
	switch (glMode) {
		case GL_FILL:  return RHI::PolygonMode::Fill;
		case GL_LINE:  return RHI::PolygonMode::Line;
		case GL_POINT: return RHI::PolygonMode::Point;
		default:       return RHI::PolygonMode::Fill;
	}
}

inline GLenum RHIPolygonModeToGL(RHI::PolygonMode mode) {
	switch (mode) {
		case RHI::PolygonMode::Fill:  return GL_FILL;
		case RHI::PolygonMode::Line:  return GL_LINE;
		case RHI::PolygonMode::Point: return GL_POINT;
		default:                      return GL_FILL;
	}
}

// --- Texture Type Mappings ---

inline RHI::TextureType GLTextureTargetToRHI(GLenum glTarget) {
	switch (glTarget) {
		case GL_TEXTURE_1D:             return RHI::TextureType::Texture1D;
		case GL_TEXTURE_2D:             return RHI::TextureType::Texture2D;
		case GL_TEXTURE_3D:             return RHI::TextureType::Texture3D;
		case GL_TEXTURE_1D_ARRAY:       return RHI::TextureType::Texture1DArray;
		case GL_TEXTURE_2D_ARRAY:       return RHI::TextureType::Texture2DArray;
		case GL_TEXTURE_CUBE_MAP:       return RHI::TextureType::TextureCube;
		case GL_TEXTURE_RECTANGLE:      return RHI::TextureType::TextureRect;
		case GL_TEXTURE_BUFFER:         return RHI::TextureType::TextureBuffer;
		case GL_TEXTURE_2D_MULTISAMPLE: return RHI::TextureType::Texture2DMS;
		default:                        return RHI::TextureType::Texture2D;
	}
}

inline GLenum RHITextureTypeToGL(RHI::TextureType type) {
	switch (type) {
		case RHI::TextureType::Texture1D:      return GL_TEXTURE_1D;
		case RHI::TextureType::Texture2D:      return GL_TEXTURE_2D;
		case RHI::TextureType::Texture3D:      return GL_TEXTURE_3D;
		case RHI::TextureType::Texture1DArray: return GL_TEXTURE_1D_ARRAY;
		case RHI::TextureType::Texture2DArray: return GL_TEXTURE_2D_ARRAY;
		case RHI::TextureType::TextureCube:    return GL_TEXTURE_CUBE_MAP;
		case RHI::TextureType::TextureRect:    return GL_TEXTURE_RECTANGLE;
		case RHI::TextureType::TextureBuffer:  return GL_TEXTURE_BUFFER;
		case RHI::TextureType::Texture2DMS:    return GL_TEXTURE_2D_MULTISAMPLE;
		default:                               return GL_TEXTURE_2D;
	}
}

// --- Texture Filter Mappings ---

inline RHI::TextureFilter GLFilterToRHI(GLenum glFilter) {
	switch (glFilter) {
		case GL_NEAREST:                return RHI::TextureFilter::Nearest;
		case GL_LINEAR:                 return RHI::TextureFilter::Linear;
		case GL_NEAREST_MIPMAP_NEAREST: return RHI::TextureFilter::NearestMipmapNearest;
		case GL_LINEAR_MIPMAP_NEAREST:  return RHI::TextureFilter::LinearMipmapNearest;
		case GL_NEAREST_MIPMAP_LINEAR:  return RHI::TextureFilter::NearestMipmapLinear;
		case GL_LINEAR_MIPMAP_LINEAR:   return RHI::TextureFilter::LinearMipmapLinear;
		default:                        return RHI::TextureFilter::Linear;
	}
}

inline GLenum RHIFilterToGL(RHI::TextureFilter filter) {
	switch (filter) {
		case RHI::TextureFilter::Nearest:              return GL_NEAREST;
		case RHI::TextureFilter::Linear:               return GL_LINEAR;
		case RHI::TextureFilter::NearestMipmapNearest: return GL_NEAREST_MIPMAP_NEAREST;
		case RHI::TextureFilter::LinearMipmapNearest:  return GL_LINEAR_MIPMAP_NEAREST;
		case RHI::TextureFilter::NearestMipmapLinear:  return GL_NEAREST_MIPMAP_LINEAR;
		case RHI::TextureFilter::LinearMipmapLinear:   return GL_LINEAR_MIPMAP_LINEAR;
		default:                                       return GL_LINEAR;
	}
}

// --- Texture Wrap Mappings ---

inline RHI::TextureWrap GLWrapToRHI(GLenum glWrap) {
	switch (glWrap) {
		case GL_REPEAT:          return RHI::TextureWrap::Repeat;
		case GL_CLAMP_TO_EDGE:   return RHI::TextureWrap::ClampToEdge;
		case GL_CLAMP_TO_BORDER: return RHI::TextureWrap::ClampToBorder;
		case GL_MIRRORED_REPEAT: return RHI::TextureWrap::MirroredRepeat;
		case GL_CLAMP:           return RHI::TextureWrap::ClampToEdge; // Legacy GL_CLAMP
		default:                 return RHI::TextureWrap::Repeat;
	}
}

inline GLenum RHIWrapToGL(RHI::TextureWrap wrap) {
	switch (wrap) {
		case RHI::TextureWrap::Repeat:         return GL_REPEAT;
		case RHI::TextureWrap::ClampToEdge:    return GL_CLAMP_TO_EDGE;
		case RHI::TextureWrap::ClampToBorder:  return GL_CLAMP_TO_BORDER;
		case RHI::TextureWrap::MirroredRepeat: return GL_MIRRORED_REPEAT;
		default:                               return GL_REPEAT;
	}
}

// --- Buffer Type Mappings ---

inline RHI::BufferType GLBufferTargetToRHI(GLenum glTarget) {
	switch (glTarget) {
		case GL_ARRAY_BUFFER:         return RHI::BufferType::Vertex;
		case GL_ELEMENT_ARRAY_BUFFER: return RHI::BufferType::Index;
		case GL_UNIFORM_BUFFER:       return RHI::BufferType::Uniform;
		case GL_SHADER_STORAGE_BUFFER:return RHI::BufferType::Storage;
		case GL_PIXEL_PACK_BUFFER:    return RHI::BufferType::PixelPack;
		case GL_PIXEL_UNPACK_BUFFER:  return RHI::BufferType::PixelUnpack;
		default:                      return RHI::BufferType::Vertex;
	}
}

inline GLenum RHIBufferTypeToGL(RHI::BufferType type) {
	switch (type) {
		case RHI::BufferType::Vertex:      return GL_ARRAY_BUFFER;
		case RHI::BufferType::Index:       return GL_ELEMENT_ARRAY_BUFFER;
		case RHI::BufferType::Uniform:     return GL_UNIFORM_BUFFER;
		case RHI::BufferType::Storage:     return GL_SHADER_STORAGE_BUFFER;
		case RHI::BufferType::PixelPack:   return GL_PIXEL_PACK_BUFFER;
		case RHI::BufferType::PixelUnpack: return GL_PIXEL_UNPACK_BUFFER;
		default:                           return GL_ARRAY_BUFFER;
	}
}

// --- Buffer Usage Mappings ---

inline RHI::BufferUsage GLUsageToRHI(GLenum glUsage) {
	switch (glUsage) {
		case GL_STATIC_DRAW:  return RHI::BufferUsage::Static;
		case GL_DYNAMIC_DRAW: return RHI::BufferUsage::Dynamic;
		case GL_STREAM_DRAW:  return RHI::BufferUsage::Stream;
		case GL_STATIC_READ:  return RHI::BufferUsage::Static;
		case GL_DYNAMIC_READ: return RHI::BufferUsage::Dynamic;
		case GL_STREAM_READ:  return RHI::BufferUsage::Stream;
		case GL_STATIC_COPY:  return RHI::BufferUsage::Static;
		case GL_DYNAMIC_COPY: return RHI::BufferUsage::Dynamic;
		case GL_STREAM_COPY:  return RHI::BufferUsage::Stream;
		default:              return RHI::BufferUsage::Static;
	}
}

inline GLenum RHIUsageToGL(RHI::BufferUsage usage) {
	switch (usage) {
		case RHI::BufferUsage::Static:  return GL_STATIC_DRAW;
		case RHI::BufferUsage::Dynamic: return GL_DYNAMIC_DRAW;
		case RHI::BufferUsage::Stream:  return GL_STREAM_DRAW;
		default:                        return GL_STATIC_DRAW;
	}
}

// --- Index Type Mappings ---

inline RHI::IndexType GLIndexTypeToRHI(GLenum glType) {
	switch (glType) {
		case GL_UNSIGNED_SHORT: return RHI::IndexType::UInt16;
		case GL_UNSIGNED_INT:   return RHI::IndexType::UInt32;
		default:                return RHI::IndexType::UInt32;
	}
}

inline GLenum RHIIndexTypeToGL(RHI::IndexType type) {
	switch (type) {
		case RHI::IndexType::UInt16: return GL_UNSIGNED_SHORT;
		case RHI::IndexType::UInt32: return GL_UNSIGNED_INT;
		default:                     return GL_UNSIGNED_INT;
	}
}

// --- Shader Stage Mappings ---

inline RHI::ShaderStage GLShaderTypeToRHI(GLenum glType) {
	switch (glType) {
		case GL_VERTEX_SHADER:   return RHI::ShaderStage::Vertex;
		case GL_FRAGMENT_SHADER: return RHI::ShaderStage::Fragment;
		case GL_GEOMETRY_SHADER: return RHI::ShaderStage::Geometry;
		case GL_COMPUTE_SHADER:  return RHI::ShaderStage::Compute;
		default:                 return RHI::ShaderStage::Vertex;
	}
}

inline GLenum RHIShaderStageToGL(RHI::ShaderStage stage) {
	switch (stage) {
		case RHI::ShaderStage::Vertex:   return GL_VERTEX_SHADER;
		case RHI::ShaderStage::Fragment: return GL_FRAGMENT_SHADER;
		case RHI::ShaderStage::Geometry: return GL_GEOMETRY_SHADER;
		case RHI::ShaderStage::Compute:  return GL_COMPUTE_SHADER;
		default:                         return GL_VERTEX_SHADER;
	}
}

// --- Texture Format Mappings (partial - commonly used formats) ---

inline RHI::TextureFormat GLInternalFormatToRHI(GLenum glFormat) {
	switch (glFormat) {
		case GL_RGBA8:                   return RHI::TextureFormat::RGBA8;
		case GL_RGB8:                    return RHI::TextureFormat::RGB8;
		case GL_RG8:                     return RHI::TextureFormat::RG8;
		case GL_R8:                      return RHI::TextureFormat::R8;
		case GL_RGBA16F:                 return RHI::TextureFormat::RGBA16F;
		case GL_RGB16F:                  return RHI::TextureFormat::RGB16F;
		case GL_RG16F:                   return RHI::TextureFormat::RG16F;
		case GL_R16F:                    return RHI::TextureFormat::R16F;
		case GL_RGBA32F:                 return RHI::TextureFormat::RGBA32F;
		case GL_RGB32F:                  return RHI::TextureFormat::RGB32F;
		case GL_RG32F:                   return RHI::TextureFormat::RG32F;
		case GL_R32F:                    return RHI::TextureFormat::R32F;
		case GL_R32I:                    return RHI::TextureFormat::R32I;
		case GL_DEPTH_COMPONENT16:       return RHI::TextureFormat::Depth16;
		case GL_DEPTH_COMPONENT24:       return RHI::TextureFormat::Depth24;
		case GL_DEPTH_COMPONENT32F:      return RHI::TextureFormat::Depth32F;
		case GL_DEPTH24_STENCIL8:        return RHI::TextureFormat::Depth24Stencil8;
		case GL_DEPTH32F_STENCIL8:       return RHI::TextureFormat::Depth32FStencil8;
		case GL_SRGB8_ALPHA8:            return RHI::TextureFormat::SRGB8Alpha8;
		case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:  return RHI::TextureFormat::CompressedDXT1;
		case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT: return RHI::TextureFormat::CompressedDXT5;
		default:                         return RHI::TextureFormat::RGBA8;
	}
}

inline GLenum RHITextureFormatToGL(RHI::TextureFormat format) {
	switch (format) {
		case RHI::TextureFormat::RGBA8:            return GL_RGBA8;
		case RHI::TextureFormat::RGB8:             return GL_RGB8;
		case RHI::TextureFormat::RG8:              return GL_RG8;
		case RHI::TextureFormat::R8:               return GL_R8;
		case RHI::TextureFormat::RGBA16F:          return GL_RGBA16F;
		case RHI::TextureFormat::RGB16F:           return GL_RGB16F;
		case RHI::TextureFormat::RG16F:            return GL_RG16F;
		case RHI::TextureFormat::R16F:             return GL_R16F;
		case RHI::TextureFormat::RGBA32F:          return GL_RGBA32F;
		case RHI::TextureFormat::RGB32F:           return GL_RGB32F;
		case RHI::TextureFormat::RG32F:            return GL_RG32F;
		case RHI::TextureFormat::R32F:             return GL_R32F;
		case RHI::TextureFormat::R32I:             return GL_R32I;
		case RHI::TextureFormat::Depth16:          return GL_DEPTH_COMPONENT16;
		case RHI::TextureFormat::Depth24:          return GL_DEPTH_COMPONENT24;
		case RHI::TextureFormat::Depth32F:         return GL_DEPTH_COMPONENT32F;
		case RHI::TextureFormat::Depth24Stencil8:  return GL_DEPTH24_STENCIL8;
		case RHI::TextureFormat::Depth32FStencil8: return GL_DEPTH32F_STENCIL8;
		case RHI::TextureFormat::SRGB8Alpha8:      return GL_SRGB8_ALPHA8;
		case RHI::TextureFormat::CompressedDXT1:   return GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
		case RHI::TextureFormat::CompressedDXT5:   return GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
		default:                                   return GL_RGBA8;
	}
}

} // namespace LuaGLConstMappings

#endif // LUA_GL_CONST_MAPPINGS_H
