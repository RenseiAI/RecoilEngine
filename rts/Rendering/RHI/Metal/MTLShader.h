/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#ifndef MTL_RHI_SHADER_H
#define MTL_RHI_SHADER_H

/**
 * Metal Shader Implementation
 *
 * Shader compilation pipeline:
 *   1. GLSL source is loaded from file
 *   2. ShaderCompiler translates GLSL -> SPIR-V -> MSL
 *   3. MSL is compiled to MTLLibrary at runtime
 *   4. MTLFunction objects are extracted for vertex/fragment stages
 *
 * Uniform handling:
 *   Metal doesn't have uniforms like GLSL. Instead:
 *   - Small uniforms (<4KB): setVertexBytes / setFragmentBytes
 *   - Larger data: Use constant buffers via setVertexBuffer
 *
 * The shader maintains a uniform buffer that accumulates SetUniform* calls,
 * which is then uploaded before each draw call.
 */

#include "Rendering/RHI/RHIShader.h"
#include <unordered_map>
#include <vector>

#ifdef __OBJC__
#import <Metal/Metal.h>
#endif

namespace RHI {

class MTLDevice;
class ShaderCompiler;

class MTLShader : public IRHIShader {
public:
	MTLShader(MTLDevice* device, const std::string& name);
	~MTLShader() override;

	// Prevent copying
	MTLShader(const MTLShader&) = delete;
	MTLShader& operator=(const MTLShader&) = delete;

	// --- Program lifecycle ---
	void AttachStage(ShaderStage stage, const std::string& sourceFile, const std::string& defines) override;
	void AttachStageFromSource(ShaderStage stage, const std::string& source, const std::string& defines) override;
	void Link() override;
	bool Validate() override;
	void Release() override;

	// --- Binding ---
	void Bind() override;
	void Unbind() override;

	// --- Attribute / output locations ---
	void BindAttribLocation(const std::string& name, uint32_t index) override;
	void BindOutputLocation(const std::string& name, uint32_t index) override;

	// --- Uniform setters (int) ---
	void SetUniform1i(const char* name, int v0) override;
	void SetUniform2i(const char* name, int v0, int v1) override;
	void SetUniform3i(const char* name, int v0, int v1, int v2) override;
	void SetUniform4i(const char* name, int v0, int v1, int v2, int v3) override;

	// --- Uniform setters (float) ---
	void SetUniform1f(const char* name, float v0) override;
	void SetUniform2f(const char* name, float v0, float v1) override;
	void SetUniform3f(const char* name, float v0, float v1, float v2) override;
	void SetUniform4f(const char* name, float v0, float v1, float v2, float v3) override;

	// --- Uniform setters (vector) ---
	void SetUniform2iv(const char* name, const int* v) override;
	void SetUniform3iv(const char* name, const int* v) override;
	void SetUniform4iv(const char* name, const int* v) override;
	void SetUniform2fv(const char* name, const float* v) override;
	void SetUniform3fv(const char* name, const float* v) override;
	void SetUniform4fv(const char* name, const float* v) override;

	// --- Uniform setters (matrix) ---
	void SetUniformMatrix2fv(const char* name, bool transpose, const float* v) override;
	void SetUniformMatrix3fv(const char* name, bool transpose, const float* v) override;
	void SetUniformMatrix4fv(const char* name, bool transpose, const float* v) override;

	// --- Queries ---
	bool IsValid() const override { return valid; }
	bool IsBound() const override { return bound; }
	const std::string& GetName() const override { return shaderName; }
	uint32_t GetNativeHandle() const override { return 0; }
	std::vector<ShaderUniformDesc> GetActiveUniformDescs() const override;

#ifdef __OBJC__
	// --- Metal-specific accessors ---
	id<MTLFunction> GetVertexFunction() const { return vertexFunction; }
	id<MTLFunction> GetFragmentFunction() const { return fragmentFunction; }
	id<MTLLibrary> GetLibrary() const { return library; }

	/// Get the uniform data to be bound before draw calls
	const std::vector<uint8_t>& GetUniformData() const { return uniformData; }
	size_t GetUniformDataSize() const { return uniformData.size(); }

	/// Upload uniforms to command encoder
	void BindUniforms(id<MTLRenderCommandEncoder> encoder);
#endif

private:
	struct UniformInfo {
		size_t offset;
		size_t size;
		bool isMatrix;
	};

	void SetUniformData(const char* name, const void* data, size_t size);
	size_t GetOrCreateUniformSlot(const char* name, size_t size);

#ifdef __OBJC__
	id<MTLLibrary>  library          = nil;
	id<MTLLibrary>  fragmentLibrary  = nil; // separate library for fragment stage
	id<MTLFunction> vertexFunction   = nil;
	id<MTLFunction> fragmentFunction = nil;
#else
	void*           library          = nullptr;
	void*           fragmentLibrary  = nullptr;
	void*           vertexFunction   = nullptr;
	void*           fragmentFunction = nullptr;
#endif

	MTLDevice*   device;
	std::string  shaderName;
	bool         valid = false;
	bool         bound = false;

	// Shader source stages (before compilation)
	std::string vertexSource;
	std::string fragmentSource;
	std::string vertexDefines;
	std::string fragmentDefines;

	// Uniform buffer
	std::vector<uint8_t> uniformData;
	std::unordered_map<std::string, UniformInfo> uniformMap;
	size_t uniformBufferSize = 0;

	// Attribute bindings (requested before Link)
	std::unordered_map<std::string, uint32_t> attribLocations;
	std::unordered_map<std::string, uint32_t> outputLocations;

	// Shared shader compiler
	static std::unique_ptr<ShaderCompiler> shaderCompiler;
};

} // namespace RHI

#endif // MTL_RHI_SHADER_H
