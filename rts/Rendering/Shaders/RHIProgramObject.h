/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#ifndef RHI_PROGRAM_OBJECT_H
#define RHI_PROGRAM_OBJECT_H

/**
 * RHIProgramObject: Bridges the legacy ShaderHandler system to the RHI backend.
 *
 * On Metal, ShaderHandler previously returned NullProgramObject for all shaders,
 * causing terrain, model, and other world rendering to silently skip drawing
 * (because shader->IsValid() returned false everywhere).
 *
 * RHIProgramObject wraps an IRHIShader and implements the full IProgramObject
 * interface. GLSL sources from attached IShaderObjects are cross-compiled
 * (GLSL -> SPIR-V -> MSL) at Link() time via the IRHIShader pipeline.
 *
 * The uniform interface maps IProgramObject's name-based SetUniform calls
 * to IRHIShader's name-based SetUniform calls, maintaining the dirty-check
 * optimization via UniformState.
 */

#include "Rendering/Shaders/Shader.h"
#include "Rendering/RHI/RHIShader.h"
#include <memory>

namespace Shader {

struct RHIProgramObject : public IProgramObject {
public:
	RHIProgramObject(const std::string& poName);
	~RHIProgramObject() override;

	void BindAttribLocation(const std::string& name, uint32_t index) override;
	void BindOutputLocation(const std::string& name, uint32_t index) override;

	void Enable() override;
	void Disable() override { DisableRaw(); }
	void EnableRaw() override;
	void DisableRaw() override;
	void Link() override;
	bool Validate() override;
	void Release() override;
	void Reload(bool reloadFromDisk, bool validate) override;

	// --- Index-based uniform interface (pure virtual requirement) ---
	// These use uniformLocs[] to map index -> name, similar to GLSLProgramObject.
	// However, most callers use the name-based template interface instead.
	void SetUniform1i(int idx, int   v0) override;
	void SetUniform2i(int idx, int   v0, int   v1) override;
	void SetUniform3i(int idx, int   v0, int   v1, int   v2) override;
	void SetUniform4i(int idx, int   v0, int   v1, int   v2, int   v3) override;
	void SetUniform1f(int idx, float v0) override;
	void SetUniform2f(int idx, float v0, float v1) override;
	void SetUniform3f(int idx, float v0, float v1, float v2) override;
	void SetUniform4f(int idx, float v0, float v1, float v2, float v3) override;

	void SetUniform2iv(int idx, const int*   v) override;
	void SetUniform3iv(int idx, const int*   v) override;
	void SetUniform4iv(int idx, const int*   v) override;
	void SetUniform2fv(int idx, const float* v) override;
	void SetUniform3fv(int idx, const float* v) override;
	void SetUniform4fv(int idx, const float* v) override;

	void SetUniform1iv(int idx, int count, const int*   v) override;
	void SetUniform2iv(int idx, int count, const int*   v) override;
	void SetUniform3iv(int idx, int count, const int*   v) override;
	void SetUniform4iv(int idx, int count, const int*   v) override;
	void SetUniform1fv(int idx, int count, const float* v) override;
	void SetUniform2fv(int idx, int count, const float* v) override;
	void SetUniform3fv(int idx, int count, const float* v) override;
	void SetUniform4fv(int idx, int count, const float* v) override;

	void SetUniformMatrix2fv(int idx, bool transp, const float* v) override;
	void SetUniformMatrix3fv(int idx, bool transp, const float* v) override;
	void SetUniformMatrix4fv(int idx, bool transp, const float* v) override;

	void SetUniformLocation(const std::string&) override;

	RHI::IRHIShader* GetRHIShader() const { return rhiShader.get(); }
	RHI::IRHIShader* GetBoundRHIShader() const override { return rhiShader.get(); }

private:
	int GetUniformLoc(const char* name) override;
	int GetUniformType(const int idx) override;

	// Override UniformState-based path to forward to IRHIShader by name
	void SetUniform(UniformState* uState, int   v0) override;
	void SetUniform(UniformState* uState, float v0) override;
	void SetUniform(UniformState* uState, int   v0, int   v1) override;
	void SetUniform(UniformState* uState, float v0, float v1) override;
	void SetUniform(UniformState* uState, int   v0, int   v1, int   v2) override;
	void SetUniform(UniformState* uState, float v0, float v1, float v2) override;
	void SetUniform(UniformState* uState, int   v0, int   v1, int   v2, int   v3) override;
	void SetUniform(UniformState* uState, float v0, float v1, float v2, float v3) override;

	void SetUniform1v(UniformState* uState, int count, const int*   v) override;
	void SetUniform1v(UniformState* uState, int count, const float* v) override;
	void SetUniform2v(UniformState* uState, int count, const int*   v) override;
	void SetUniform2v(UniformState* uState, int count, const float* v) override;
	void SetUniform3v(UniformState* uState, int count, const int*   v) override;
	void SetUniform3v(UniformState* uState, int count, const float* v) override;
	void SetUniform4v(UniformState* uState, int count, const int*   v) override;
	void SetUniform4v(UniformState* uState, int count, const float* v) override;

	void SetUniformMatrix2x2(UniformState* uState, bool transp, const float* v) override;
	void SetUniformMatrix3x3(UniformState* uState, bool transp, const float* v) override;
	void SetUniformMatrix4x4(UniformState* uState, bool transp, const float* v) override;

	/// Compose final GLSL source from an IShaderObject (version + flags + source)
	std::string ComposeGLSLSource(IShaderObject* so);

	/// Map GL shader type constant to RHI::ShaderStage
	static RHI::ShaderStage GLTypeToRHIStage(unsigned int glType);

private:
	std::unique_ptr<RHI::IRHIShader> rhiShader;
	std::vector<size_t> uniformLocs;
	unsigned int curSrcHash = 0;
};

} // namespace Shader

#endif // RHI_PROGRAM_OBJECT_H
