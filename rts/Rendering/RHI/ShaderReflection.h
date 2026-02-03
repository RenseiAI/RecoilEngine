/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#ifndef SHADER_REFLECTION_H
#define SHADER_REFLECTION_H

/**
 * Shader Reflection Data
 *
 * Extracted from SPIR-V bytecode via SPIRV-Cross. Maps GLSL uniform/attribute/
 * sampler bindings to Metal argument table indices for the Metal backend.
 *
 * Used by the RHI shader implementation to translate GL-style uniform locations
 * (glGetUniformLocation) to Metal buffer/texture/sampler indices.
 */

#include <cstdint>
#include <string>
#include <vector>

namespace RHI {

enum class ShaderDataType : uint8_t {
	Float,
	Vec2,
	Vec3,
	Vec4,
	Int,
	IVec2,
	IVec3,
	IVec4,
	Mat3,
	Mat4,
	Sampler2D,
	SamplerCube,
	Sampler2DShadow,
	Unknown
};

struct ReflectedUniform {
	std::string    name;
	int            location         = -1;
	int            metalBufferIndex = -1;
	int            offset           = 0;
	int            arraySize        = 1;
	ShaderDataType type             = ShaderDataType::Unknown;
};

struct ReflectedAttribute {
	std::string    name;
	int            location = -1;
	ShaderDataType type     = ShaderDataType::Unknown;
};

struct ReflectedSampler {
	std::string name;
	int         binding           = -1;
	int         metalTextureIndex = -1;
	int         metalSamplerIndex = -1;
};

struct ShaderReflection {
	std::vector<ReflectedUniform>   uniforms;
	std::vector<ReflectedAttribute> attributes;
	std::vector<ReflectedSampler>   samplers;

	const ReflectedUniform*   FindUniform(const std::string& name) const;
	const ReflectedAttribute* FindAttribute(const std::string& name) const;
	const ReflectedSampler*   FindSampler(const std::string& name) const;
	void Clear();
};

} // namespace RHI

#endif // SHADER_REFLECTION_H
