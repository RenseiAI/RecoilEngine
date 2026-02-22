/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#ifndef RHI_SHADER_H
#define RHI_SHADER_H

/**
 * RHI Shader Interface
 *
 * Extends the existing Shader::IProgramObject pattern:
 *   IProgramObject::Enable() / Disable()    ->  IRHIShader::Bind() / Unbind()
 *   IProgramObject::Link()                  ->  IRHIShader::Link()
 *   IProgramObject::SetUniform*()           ->  IRHIShader::SetUniform*()
 *   IProgramObject::BindAttribLocation()    ->  IRHIShader::BindAttribLocation()
 *   GLSLShaderObject::Compile()             ->  IRHIShader::AttachStage() (compiles internally)
 *
 * The GL backend delegates to GLSLProgramObject. The Metal backend
 * will use ShaderCompiler to translate GLSL->MSL and create MTLLibrary objects.
 */

#include <cstdint>
#include <string>
#include "RHITypes.h"

namespace RHI {

class IRHIShader {
public:
	virtual ~IRHIShader() = default;

	// --- Program lifecycle ---
	virtual void AttachStage(ShaderStage stage, const std::string& sourceFile, const std::string& defines = "") = 0;
	/// Attach a shader stage from inline GLSL source (not a file path).
	/// On Metal, the source is cross-compiled to MSL via ShaderCompiler.
	virtual void AttachStageFromSource(ShaderStage stage, const std::string& source, const std::string& defines = "") = 0;
	virtual void Link() = 0;
	virtual bool Validate() = 0;
	virtual void Release() = 0;

	// --- Binding ---
	virtual void Bind() = 0;
	virtual void Unbind() = 0;

	// --- Attribute / output locations ---
	virtual void BindAttribLocation(const std::string& name, uint32_t index) = 0;
	virtual void BindOutputLocation(const std::string& name, uint32_t index) = 0;

	// --- Uniform setters (int) ---
	virtual void SetUniform1i(const char* name, int v0) = 0;
	virtual void SetUniform2i(const char* name, int v0, int v1) = 0;
	virtual void SetUniform3i(const char* name, int v0, int v1, int v2) = 0;
	virtual void SetUniform4i(const char* name, int v0, int v1, int v2, int v3) = 0;

	// --- Uniform setters (float) ---
	virtual void SetUniform1f(const char* name, float v0) = 0;
	virtual void SetUniform2f(const char* name, float v0, float v1) = 0;
	virtual void SetUniform3f(const char* name, float v0, float v1, float v2) = 0;
	virtual void SetUniform4f(const char* name, float v0, float v1, float v2, float v3) = 0;

	// --- Uniform setters (vector) ---
	virtual void SetUniform2iv(const char* name, const int* v) = 0;
	virtual void SetUniform3iv(const char* name, const int* v) = 0;
	virtual void SetUniform4iv(const char* name, const int* v) = 0;
	virtual void SetUniform2fv(const char* name, const float* v) = 0;
	virtual void SetUniform3fv(const char* name, const float* v) = 0;
	virtual void SetUniform4fv(const char* name, const float* v) = 0;

	// --- Uniform setters (matrix) ---
	virtual void SetUniformMatrix2fv(const char* name, bool transpose, const float* v) = 0;
	virtual void SetUniformMatrix3fv(const char* name, bool transpose, const float* v) = 0;
	virtual void SetUniformMatrix4fv(const char* name, bool transpose, const float* v) = 0;

	// --- Queries ---
	virtual bool IsValid() const = 0;
	virtual bool IsBound() const = 0;
	virtual const std::string& GetName() const = 0;
	virtual uint32_t GetNativeHandle() const = 0;
};

} // namespace RHI

#endif // RHI_SHADER_H
