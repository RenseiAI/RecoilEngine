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

	/// Texture unit remapping: maps GL texture unit → Metal [[texture(N)]] index.
	/// Returns -1 if no remapping exists (use identity mapping).
	/// Called by MTLContext::BindCurrentResources() to place textures at the
	/// correct Metal indices that SPIRV-Cross assigned.
	static constexpr int kMaxTextureUnits = 32;
	int GetVSTextureIndex(int glUnit) const {
		return (glUnit >= 0 && glUnit < kMaxTextureUnits) ? vsTextureRemap[glUnit] : -1;
	}
	int GetFSTextureIndex(int glUnit) const {
		return (glUnit >= 0 && glUnit < kMaxTextureUnits) ? fsTextureRemap[glUnit] : -1;
	}
	bool HasTextureRemapping() const { return hasTextureRemap; }

private:
	struct UniformInfo {
		size_t offset;       // position in uniformData
		size_t size;         // byte size of this uniform's data
		bool isMatrix;
		int vsBufferIndex = -1;  // vertex stage [[buffer(N)]], -1 if not in VS
		int fsBufferIndex = -1;  // fragment stage [[buffer(N)]], -1 if not in FS
	};

	// Pre-computed byte range per Metal buffer index per stage.
	// Uniforms sharing the same buffer index within one stage are part of
	// a single UBO and are uploaded in one setVertexBytes/setFragmentBytes call.
	struct BufferUploadRange {
		int metalBufferIndex;
		size_t byteOffset;  // start in uniformData
		size_t byteSize;    // upload length (aligned to 16)
	};

	void SetUniformData(const char* name, const void* data, size_t size);
	size_t GetOrCreateUniformSlot(const char* name, size_t size);
	void RebuildBufferRanges();

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
	std::vector<BufferUploadRange> vertexBufferRanges;    // uploaded via setVertexBytes
	std::vector<BufferUploadRange> fragmentBufferRanges;  // uploaded via setFragmentBytes
	size_t uniformBufferSize = 0;

	// Attribute bindings (requested before Link)
	std::unordered_map<std::string, uint32_t> attribLocations;
	std::unordered_map<std::string, uint32_t> outputLocations;

	// Sampler texture remapping: SPIRV-Cross assigns [[texture(N)]] indices
	// that may differ from the GL texture unit the engine uses. When the
	// engine calls SetUniform1i("samplerName", glUnit), we look up the
	// sampler's Metal texture index from reflection and record the mapping.
	struct SamplerInfo {
		int vsMetalTextureIndex = -1;
		int fsMetalTextureIndex = -1;
		int vsMetalSamplerIndex = -1;
		int fsMetalSamplerIndex = -1;
	};
	std::unordered_map<std::string, SamplerInfo> samplerInfoMap;

	// GL texture unit → Metal texture index per stage (-1 = identity/unmapped)
	int vsTextureRemap[kMaxTextureUnits];
	int fsTextureRemap[kMaxTextureUnits];
	// Also remap sampler indices (they track texture indices in SPIRV-Cross)
	int vsSamplerRemap[kMaxTextureUnits];
	int fsSamplerRemap[kMaxTextureUnits];
	bool hasTextureRemap = false;

	// Shared shader compiler
	static std::unique_ptr<ShaderCompiler> shaderCompiler;
};

} // namespace RHI

#endif // MTL_RHI_SHADER_H
