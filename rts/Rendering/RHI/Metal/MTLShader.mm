/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#import "MTLShader.h"
#import "MTLDevice.h"

#import <Metal/Metal.h>
#include <fstream>
#include <sstream>
#include <cstring>

#include "Rendering/RHI/ShaderCompiler.h"
#include "Rendering/RHI/ShaderReflection.h"
#include "System/Log/ILog.h"
#include "System/FileSystem/FileHandler.h"

namespace RHI {

static size_t ShaderDataTypeSize(ShaderDataType type) {
	switch (type) {
		case ShaderDataType::Float:           return  4;
		case ShaderDataType::Vec2:            return  8;
		case ShaderDataType::Vec3:            return 12;
		case ShaderDataType::Vec4:            return 16;
		case ShaderDataType::Int:             return  4;
		case ShaderDataType::IVec2:           return  8;
		case ShaderDataType::IVec3:           return 12;
		case ShaderDataType::IVec4:           return 16;
		case ShaderDataType::Mat3:            return 36;
		case ShaderDataType::Mat4:            return 64;
		case ShaderDataType::Sampler2D:       return  4;
		case ShaderDataType::SamplerCube:     return  4;
		case ShaderDataType::Sampler2DShadow: return  4;
		default:                              return  4;
	}
}

// Static shader compiler instance
std::unique_ptr<ShaderCompiler> MTLShader::shaderCompiler;

MTLShader::MTLShader(MTLDevice* device, const std::string& name)
	: device(device)
	, shaderName(name)
{
	// Initialize the shader compiler on first use
	if (!shaderCompiler) {
		shaderCompiler = std::make_unique<ShaderCompiler>();
	}

	// Reserve some space for uniforms (will grow as needed)
	uniformData.reserve(1024);
}

MTLShader::~MTLShader() {
	Release();
}

void MTLShader::AttachStage(ShaderStage stage, const std::string& sourceFile, const std::string& defines) {
	// Read shader source from file
	CFileHandler file(sourceFile);
	if (!file.FileExists()) {
		LOG_L(L_ERROR, "[MTLShader] Shader file not found: %s", sourceFile.c_str());
		return;
	}

	std::string source;
	file.LoadStringData(source);

	if (source.empty()) {
		LOG_L(L_ERROR, "[MTLShader] Failed to read shader file: %s", sourceFile.c_str());
		return;
	}

	AttachStageFromSource(stage, source, defines);
}

void MTLShader::AttachStageFromSource(ShaderStage stage, const std::string& source, const std::string& defines) {
	switch (stage) {
		case ShaderStage::Vertex:
			vertexSource = source;
			vertexDefines = defines;
			break;
		case ShaderStage::Fragment:
			fragmentSource = source;
			fragmentDefines = defines;
			break;
		case ShaderStage::Geometry:
			LOG_L(L_WARNING, "[MTLShader] Geometry shaders not supported in Metal");
			break;
		case ShaderStage::Compute:
			LOG_L(L_WARNING, "[MTLShader] Compute shaders not yet implemented");
			break;
	}
}

void MTLShader::Link() {
	if (!device || !device->IsValid()) {
		LOG_L(L_ERROR, "[MTLShader] Cannot link: invalid device");
		return;
	}

	if (vertexSource.empty() && fragmentSource.empty()) {
		LOG_L(L_ERROR, "[MTLShader] Cannot link: no shader sources attached");
		return;
	}

	// Prepend defines to shader sources
	std::string vertexSourceWithDefines = vertexDefines + "\n" + vertexSource;
	std::string fragmentSourceWithDefines = fragmentDefines + "\n" + fragmentSource;

	// Compile GLSL to MSL
	std::string vertexMSL;
	std::string fragmentMSL;

	if (!vertexSource.empty()) {
		vertexMSL = shaderCompiler->CompileGLSLToMSL(vertexSourceWithDefines, CompilerShaderStage::Vertex);
		if (vertexMSL.empty()) {
			LOG_L(L_ERROR, "[MTLShader] %s: Failed to compile vertex shader: %s",
			      shaderName.c_str(), shaderCompiler->GetLastError().c_str());
			return;
		}
	}

	if (!fragmentSource.empty()) {
		fragmentMSL = shaderCompiler->CompileGLSLToMSL(fragmentSourceWithDefines, CompilerShaderStage::Fragment);
		if (fragmentMSL.empty()) {
			LOG_L(L_ERROR, "[MTLShader] %s: Failed to compile fragment shader: %s",
			      shaderName.c_str(), shaderCompiler->GetLastError().c_str());
			return;
		}
	}

	// Compile vertex and fragment MSL into separate Metal libraries.
	// They must be separate because SPIRV-Cross generates identical struct
	// definitions for interface blocks (e.g. "struct Data") in both stages,
	// which causes "redefinition" errors when combined.
	NSError* error = nil;
	MTLCompileOptions* options = [[MTLCompileOptions alloc] init];
	options.languageVersion = MTLLanguageVersion2_1;

	if (!vertexMSL.empty()) {
		NSString* vsSrc = [NSString stringWithUTF8String:vertexMSL.c_str()];
		id<MTLLibrary> vsLib = [device->GetMTLDevice() newLibraryWithSource:vsSrc
		                                                            options:options
		                                                              error:&error];
		if (!vsLib) {
			LOG_L(L_ERROR, "[MTLShader] %s: Failed to compile vertex Metal library: %s",
			      shaderName.c_str(), [[error localizedDescription] UTF8String]);
			return;
		}
		vsLib.label = [NSString stringWithUTF8String:(shaderName + "_VS").c_str()];
		vertexFunction = [vsLib newFunctionWithName:@"vertexMain"];
		if (!vertexFunction)
			vertexFunction = [vsLib newFunctionWithName:@"main0"];
		if (!vertexFunction) {
			LOG_L(L_ERROR, "[MTLShader] %s: Vertex function not found in library", shaderName.c_str());
		}
		library = vsLib; // keep a reference to prevent deallocation
	}

	if (!fragmentMSL.empty()) {
		error = nil;
		NSString* fsSrc = [NSString stringWithUTF8String:fragmentMSL.c_str()];
		id<MTLLibrary> fsLib = [device->GetMTLDevice() newLibraryWithSource:fsSrc
		                                                            options:options
		                                                              error:&error];
		if (!fsLib) {
			LOG_L(L_ERROR, "[MTLShader] %s: Failed to compile fragment Metal library: %s",
			      shaderName.c_str(), [[error localizedDescription] UTF8String]);
			LOG_L(L_ERROR, "[MTLShader] %s: Fragment MSL source:\n%s",
			      shaderName.c_str(), fragmentMSL.c_str());
			return;
		}
		fsLib.label = [NSString stringWithUTF8String:(shaderName + "_FS").c_str()];
		fragmentFunction = [fsLib newFunctionWithName:@"fragmentMain"];
		if (!fragmentFunction)
			fragmentFunction = [fsLib newFunctionWithName:@"main0"];
		if (!fragmentFunction) {
			LOG_L(L_ERROR, "[MTLShader] %s: Fragment function not found in library", shaderName.c_str());
		}
		if (!library) library = fsLib;
		fragmentLibrary = fsLib;
	}

	valid = (vertexFunction != nil || fragmentFunction != nil);

	if (!valid)
		return;

	// Pre-populate uniform map from SPIR-V reflection data so offsets match
	// the std140 layout that SPIRV-Cross generates in the MSL struct.
	uniformMap.clear();
	uniformData.clear();
	uniformBufferSize = 0;

	size_t maxEnd = 0;
	auto populateFromReflection = [&](const ShaderReflection* refl) {
		if (!refl)
			return;
		for (const auto& u : refl->uniforms) {
			// Skip if already present (vertex stage has priority)
			if (uniformMap.count(u.name))
				continue;
			size_t dataSize = ShaderDataTypeSize(u.type) * static_cast<size_t>(u.arraySize);
			size_t offset = static_cast<size_t>(u.offset);
			uniformMap[u.name] = {offset, dataSize, (u.type == ShaderDataType::Mat3 || u.type == ShaderDataType::Mat4)};
			size_t end = offset + dataSize;
			if (end > maxEnd)
				maxEnd = end;
		}
	};

	if (!vertexSource.empty()) {
		const auto* vsRefl = shaderCompiler->GetCachedReflection(
			vertexDefines + "\n" + vertexSource, CompilerShaderStage::Vertex);
		populateFromReflection(vsRefl);
	}
	if (!fragmentSource.empty()) {
		const auto* fsRefl = shaderCompiler->GetCachedReflection(
			fragmentDefines + "\n" + fragmentSource, CompilerShaderStage::Fragment);
		populateFromReflection(fsRefl);
	}

	// Align total buffer size to 16 bytes
	uniformBufferSize = (maxEnd + 15) & ~size_t(15);
	uniformData.resize(uniformBufferSize, 0);

	LOG("[MTLShader] %s: Linked successfully (%zu uniforms, %zu bytes)",
	    shaderName.c_str(), uniformMap.size(), uniformBufferSize);
}

bool MTLShader::Validate() {
	return valid && library != nil;
}

void MTLShader::Release() {
	fragmentFunction = nil;
	vertexFunction = nil;
	fragmentLibrary = nil;
	library = nil;
	valid = false;
	bound = false;

	vertexSource.clear();
	fragmentSource.clear();
	uniformData.clear();
	uniformMap.clear();
}

void MTLShader::Bind() {
	bound = true;
}

void MTLShader::Unbind() {
	bound = false;
}

void MTLShader::BindAttribLocation(const std::string& name, uint32_t index) {
	attribLocations[name] = index;
}

void MTLShader::BindOutputLocation(const std::string& name, uint32_t index) {
	outputLocations[name] = index;
}

// --- Uniform helpers ---

size_t MTLShader::GetOrCreateUniformSlot(const char* name, size_t size) {
	auto it = uniformMap.find(name);
	if (it != uniformMap.end()) {
		return it->second.offset;
	}

	// Uniform not found in reflection — allocate sequentially at the end.
	// This handles uniforms set at runtime that weren't in the GLSL source
	// (e.g., engine-injected uniforms).
	size_t alignedSize = (size + 15) & ~size_t(15);
	size_t offset = uniformBufferSize;

	uniformBufferSize += alignedSize;
	if (uniformData.size() < uniformBufferSize) {
		uniformData.resize(uniformBufferSize, 0);
	}

	uniformMap[name] = {offset, size, false};
	return offset;
}

void MTLShader::SetUniformData(const char* name, const void* data, size_t size) {
	size_t offset = GetOrCreateUniformSlot(name, size);
	std::memcpy(uniformData.data() + offset, data, size);
}

// --- Uniform setters (int) ---

void MTLShader::SetUniform1i(const char* name, int v0) {
	SetUniformData(name, &v0, sizeof(int));
}

void MTLShader::SetUniform2i(const char* name, int v0, int v1) {
	int data[2] = {v0, v1};
	SetUniformData(name, data, sizeof(data));
}

void MTLShader::SetUniform3i(const char* name, int v0, int v1, int v2) {
	int data[3] = {v0, v1, v2};
	SetUniformData(name, data, sizeof(data));
}

void MTLShader::SetUniform4i(const char* name, int v0, int v1, int v2, int v3) {
	int data[4] = {v0, v1, v2, v3};
	SetUniformData(name, data, sizeof(data));
}

// --- Uniform setters (float) ---

void MTLShader::SetUniform1f(const char* name, float v0) {
	SetUniformData(name, &v0, sizeof(float));
}

void MTLShader::SetUniform2f(const char* name, float v0, float v1) {
	float data[2] = {v0, v1};
	SetUniformData(name, data, sizeof(data));
}

void MTLShader::SetUniform3f(const char* name, float v0, float v1, float v2) {
	float data[3] = {v0, v1, v2};
	SetUniformData(name, data, sizeof(data));
}

void MTLShader::SetUniform4f(const char* name, float v0, float v1, float v2, float v3) {
	float data[4] = {v0, v1, v2, v3};
	SetUniformData(name, data, sizeof(data));
}

// --- Uniform setters (vector) ---

void MTLShader::SetUniform2iv(const char* name, const int* v) {
	SetUniformData(name, v, 2 * sizeof(int));
}

void MTLShader::SetUniform3iv(const char* name, const int* v) {
	SetUniformData(name, v, 3 * sizeof(int));
}

void MTLShader::SetUniform4iv(const char* name, const int* v) {
	SetUniformData(name, v, 4 * sizeof(int));
}

void MTLShader::SetUniform2fv(const char* name, const float* v) {
	SetUniformData(name, v, 2 * sizeof(float));
}

void MTLShader::SetUniform3fv(const char* name, const float* v) {
	SetUniformData(name, v, 3 * sizeof(float));
}

void MTLShader::SetUniform4fv(const char* name, const float* v) {
	SetUniformData(name, v, 4 * sizeof(float));
}

// --- Uniform setters (matrix) ---

void MTLShader::SetUniformMatrix2fv(const char* name, bool transpose, const float* v) {
	// Metal uses column-major matrices (same as OpenGL default)
	// If transpose is requested, we need to transpose the matrix
	if (transpose) {
		float transposed[4];
		transposed[0] = v[0]; transposed[1] = v[2];
		transposed[2] = v[1]; transposed[3] = v[3];
		SetUniformData(name, transposed, sizeof(transposed));
	} else {
		SetUniformData(name, v, 4 * sizeof(float));
	}
}

void MTLShader::SetUniformMatrix3fv(const char* name, bool transpose, const float* v) {
	if (transpose) {
		float transposed[9];
		for (int i = 0; i < 3; i++) {
			for (int j = 0; j < 3; j++) {
				transposed[i * 3 + j] = v[j * 3 + i];
			}
		}
		SetUniformData(name, transposed, sizeof(transposed));
	} else {
		SetUniformData(name, v, 9 * sizeof(float));
	}
}

void MTLShader::SetUniformMatrix4fv(const char* name, bool transpose, const float* v) {
	if (transpose) {
		float transposed[16];
		for (int i = 0; i < 4; i++) {
			for (int j = 0; j < 4; j++) {
				transposed[i * 4 + j] = v[j * 4 + i];
			}
		}
		SetUniformData(name, transposed, sizeof(transposed));
	} else {
		SetUniformData(name, v, 16 * sizeof(float));
	}
}

void MTLShader::BindUniforms(id<MTLRenderCommandEncoder> encoder) {
	if (uniformData.empty() || !encoder) {
		return;
	}

	// Upload uniform data using setVertexBytes / setFragmentBytes
	// This is efficient for data < 4KB
	if (uniformBufferSize > 0) {
		[encoder setVertexBytes:uniformData.data()
		                 length:uniformBufferSize
		                atIndex:0];  // Uniform buffer at index 0

		[encoder setFragmentBytes:uniformData.data()
		                   length:uniformBufferSize
		                  atIndex:0];
	}
}

} // namespace RHI
