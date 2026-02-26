/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#ifndef SHADER_COMPILER_H
#define SHADER_COMPILER_H

/**
 * Shader Cross-Compiler: GLSL -> SPIR-V -> MSL
 *
 * Pipeline:
 *   1. glslang compiles legacy GLSL (ECompatibilityProfile) to SPIR-V
 *   2. SPIRV-Cross translates SPIR-V to Metal Shading Language
 *   3. SPIRV-Cross reflection extracts uniforms, attributes, samplers
 *
 * Handles engine shader features:
 *   - GLSL 120 varying/gl_TexCoord[]/gl_MultiTexCoord0/gl_FragColor/gl_FragData[]
 *   - GLSL 130 in/out, textureLod
 *   - Built-in matrices (gl_ModelViewMatrix, gl_ProjectionMatrix, etc.)
 *   - Built-in state (gl_Fog, gl_LightSource[], gl_NormalMatrix)
 *   - gl_ClipVertex for user clip planes (water shaders)
 *   - sampler2D, samplerCube, sampler2DShadow, shadow2DProj
 *
 * Results are cached by content hash.
 */

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "ShaderReflection.h"

namespace RHI {

enum class CompilerShaderStage : uint8_t {
	Vertex,
	Fragment
};

struct MSLCompilerOptions {
	uint32_t mslVersion        = 20100;  // MSL 2.1
	bool     isIOS             = false;  // macOS platform
	bool     enableClipDistance = true;   // engine uses gl_ClipVertex
	bool     enablePointSize   = false;
};

class ShaderCompiler {
public:
	ShaderCompiler();
	~ShaderCompiler();

	ShaderCompiler(const ShaderCompiler&) = delete;
	ShaderCompiler& operator=(const ShaderCompiler&) = delete;

	/// Compile GLSL source to SPIR-V bytecode.
	std::vector<uint32_t> CompileGLSLToSPIRV(
		const std::string& source,
		CompilerShaderStage stage,
		const std::string& entryPoint = "main");

	/// Translate SPIR-V bytecode to Metal Shading Language source.
	/// attribLocations: optional name->index map from BindAttribLocation.
	/// Stage inputs without SPIR-V Location decorations are assigned locations:
	///   1. From attribLocations if the input name matches
	///   2. Auto-assigned sequentially for unmatched inputs
	/// This is required because Metal's stage_in struct needs [[attribute(N)]].
	std::string TranslateSPIRVToMSL(
		const std::vector<uint32_t>& spirv,
		const MSLCompilerOptions& options = MSLCompilerOptions{},
		const std::unordered_map<std::string, uint32_t>& attribLocations = {});

	/// Extract reflection data from SPIR-V.
	ShaderReflection ReflectSPIRV(const std::vector<uint32_t>& spirv);

	/// Convenience: compile GLSL directly to MSL (cached by content hash).
	/// attribLocations: forwarded to TranslateSPIRVToMSL for vertex shaders.
	std::string CompileGLSLToMSL(
		const std::string& source,
		CompilerShaderStage stage,
		const std::unordered_map<std::string, uint32_t>& attribLocations = {});

	/// Retrieve cached reflection data for a previously compiled shader.
	/// Returns nullptr if not found in cache.
	const ShaderReflection* GetCachedReflection(
		const std::string& source, CompilerShaderStage stage) const;

	void ClearCache();

	const std::string& GetLastError() const { return lastError; }

private:
	uint64_t HashSource(const std::string& source, CompilerShaderStage stage) const;

	struct CachedShader {
		std::vector<uint32_t> spirv;
		std::string           msl;
		ShaderReflection      reflection;
	};

	std::unordered_map<uint64_t, CachedShader> cache;
	std::string lastError;
	bool glslangInitialized = false;
};

} // namespace RHI

#endif // SHADER_COMPILER_H
