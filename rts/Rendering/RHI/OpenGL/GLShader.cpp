/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#include "GLShader.h"

namespace RHI {

static GLenum ToGLShaderType(ShaderStage stage) {
	switch (stage) {
		case ShaderStage::Vertex:   return GL_VERTEX_SHADER;
		case ShaderStage::Fragment: return GL_FRAGMENT_SHADER;
		case ShaderStage::Geometry: return GL_GEOMETRY_SHADER;
		case ShaderStage::Compute:  return GL_COMPUTE_SHADER;
	}
	return GL_VERTEX_SHADER;
}

GLShader::GLShader(const std::string& name)
	: glslProgram(std::make_unique<Shader::GLSLProgramObject>(name))
	, shaderName(name)
{
}

GLShader::~GLShader() {
	Release();
}

void GLShader::AttachStage(ShaderStage stage, const std::string& sourceFile, const std::string& defines) {
	auto* shaderObj = new Shader::GLSLShaderObject(ToGLShaderType(stage), sourceFile, defines);
	glslProgram->AttachShaderObject(shaderObj);
}

void GLShader::AttachStageFromSource(ShaderStage stage, const std::string& source, const std::string& defines) {
	// GLSLShaderObject accepts inline source text as the "name" parameter
	auto* shaderObj = new Shader::GLSLShaderObject(ToGLShaderType(stage), source, defines);
	glslProgram->AttachShaderObject(shaderObj);
}

void GLShader::Link()     { glslProgram->Link(); }
bool GLShader::Validate() { return glslProgram->Validate(); }
void GLShader::Release()  { if (glslProgram) glslProgram->Release(); }

void GLShader::Bind()   { glslProgram->Enable(); }
void GLShader::Unbind() { glslProgram->Disable(); }

void GLShader::BindAttribLocation(const std::string& name, uint32_t index) { glslProgram->BindAttribLocation(name, index); }
void GLShader::BindOutputLocation(const std::string& name, uint32_t index) { glslProgram->BindOutputLocation(name, index); }

// --- Uniform setters ---
// GLSLProgramObject's private SetUniform(UniformState*,...) overrides hide the
// public template SetUniform(const char*,...) inherited from IProgramObject.
// Route through the base class reference to access the name-based templates.

void GLShader::SetUniform1i(const char* name, int v0)                         { static_cast<Shader::IProgramObject*>(glslProgram.get())->SetUniform(name, v0); }
void GLShader::SetUniform2i(const char* name, int v0, int v1)                 { static_cast<Shader::IProgramObject*>(glslProgram.get())->SetUniform(name, v0, v1); }
void GLShader::SetUniform3i(const char* name, int v0, int v1, int v2)         { static_cast<Shader::IProgramObject*>(glslProgram.get())->SetUniform(name, v0, v1, v2); }
void GLShader::SetUniform4i(const char* name, int v0, int v1, int v2, int v3) { static_cast<Shader::IProgramObject*>(glslProgram.get())->SetUniform(name, v0, v1, v2, v3); }

void GLShader::SetUniform1f(const char* name, float v0)                                   { static_cast<Shader::IProgramObject*>(glslProgram.get())->SetUniform(name, v0); }
void GLShader::SetUniform2f(const char* name, float v0, float v1)                         { static_cast<Shader::IProgramObject*>(glslProgram.get())->SetUniform(name, v0, v1); }
void GLShader::SetUniform3f(const char* name, float v0, float v1, float v2)               { static_cast<Shader::IProgramObject*>(glslProgram.get())->SetUniform(name, v0, v1, v2); }
void GLShader::SetUniform4f(const char* name, float v0, float v1, float v2, float v3)     { static_cast<Shader::IProgramObject*>(glslProgram.get())->SetUniform(name, v0, v1, v2, v3); }

void GLShader::SetUniform2iv(const char* name, const int* v)   { static_cast<Shader::IProgramObject*>(glslProgram.get())->SetUniform2v(name, v); }
void GLShader::SetUniform3iv(const char* name, const int* v)   { static_cast<Shader::IProgramObject*>(glslProgram.get())->SetUniform3v(name, v); }
void GLShader::SetUniform4iv(const char* name, const int* v)   { static_cast<Shader::IProgramObject*>(glslProgram.get())->SetUniform4v(name, v); }
void GLShader::SetUniform2fv(const char* name, const float* v) { static_cast<Shader::IProgramObject*>(glslProgram.get())->SetUniform2v(name, v); }
void GLShader::SetUniform3fv(const char* name, const float* v) { static_cast<Shader::IProgramObject*>(glslProgram.get())->SetUniform3v(name, v); }
void GLShader::SetUniform4fv(const char* name, const float* v) { static_cast<Shader::IProgramObject*>(glslProgram.get())->SetUniform4v(name, v); }

void GLShader::SetUniformMatrix2fv(const char* name, bool transp, const float* v) { static_cast<Shader::IProgramObject*>(glslProgram.get())->SetUniformMatrix2x2(name, transp, v); }
void GLShader::SetUniformMatrix3fv(const char* name, bool transp, const float* v) { static_cast<Shader::IProgramObject*>(glslProgram.get())->SetUniformMatrix3x3(name, transp, v); }
void GLShader::SetUniformMatrix4fv(const char* name, bool transp, const float* v) { static_cast<Shader::IProgramObject*>(glslProgram.get())->SetUniformMatrix4x4(name, transp, v); }

bool GLShader::IsValid() const        { return glslProgram->IsValid(); }
bool GLShader::IsBound() const        { return glslProgram->IsBound(); }
const std::string& GLShader::GetName() const { return shaderName; }
uint32_t GLShader::GetNativeHandle() const   { return glslProgram->GetObjID(); }

} // namespace RHI
