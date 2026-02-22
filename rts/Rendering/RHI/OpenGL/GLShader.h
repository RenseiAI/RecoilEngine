/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#ifndef GL_RHI_SHADER_H
#define GL_RHI_SHADER_H

#include "Rendering/RHI/RHIShader.h"
#include "Rendering/Shaders/Shader.h"
#include <memory>

namespace RHI {

/// Thin wrapper around Shader::GLSLProgramObject.
class GLShader : public IRHIShader {
public:
	explicit GLShader(const std::string& name);
	~GLShader() override;

	void AttachStage(ShaderStage stage, const std::string& sourceFile, const std::string& defines) override;
	void AttachStageFromSource(ShaderStage stage, const std::string& source, const std::string& defines) override;
	void Link() override;
	bool Validate() override;
	void Release() override;

	void Bind() override;
	void Unbind() override;

	void BindAttribLocation(const std::string& name, uint32_t index) override;
	void BindOutputLocation(const std::string& name, uint32_t index) override;

	void SetUniform1i(const char* name, int v0) override;
	void SetUniform2i(const char* name, int v0, int v1) override;
	void SetUniform3i(const char* name, int v0, int v1, int v2) override;
	void SetUniform4i(const char* name, int v0, int v1, int v2, int v3) override;

	void SetUniform1f(const char* name, float v0) override;
	void SetUniform2f(const char* name, float v0, float v1) override;
	void SetUniform3f(const char* name, float v0, float v1, float v2) override;
	void SetUniform4f(const char* name, float v0, float v1, float v2, float v3) override;

	void SetUniform2iv(const char* name, const int* v) override;
	void SetUniform3iv(const char* name, const int* v) override;
	void SetUniform4iv(const char* name, const int* v) override;
	void SetUniform2fv(const char* name, const float* v) override;
	void SetUniform3fv(const char* name, const float* v) override;
	void SetUniform4fv(const char* name, const float* v) override;

	void SetUniformMatrix2fv(const char* name, bool transpose, const float* v) override;
	void SetUniformMatrix3fv(const char* name, bool transpose, const float* v) override;
	void SetUniformMatrix4fv(const char* name, bool transpose, const float* v) override;

	bool IsValid() const override;
	bool IsBound() const override;
	const std::string& GetName() const override;
	uint32_t GetNativeHandle() const override;

	/// Access the underlying GLSLProgramObject for legacy code paths
	Shader::GLSLProgramObject* GetGLSLProgram() { return glslProgram.get(); }

private:
	std::unique_ptr<Shader::GLSLProgramObject> glslProgram;
	std::string shaderName;
};

} // namespace RHI

#endif // GL_RHI_SHADER_H
