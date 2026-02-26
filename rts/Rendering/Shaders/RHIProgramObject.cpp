/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#include "Rendering/Shaders/RHIProgramObject.h"
#include "Rendering/Shaders/ShaderHandler.h"
#include "Rendering/RHI/RHIFactory.h"
#include "Rendering/RHI/RHIDevice.h"
#include "Rendering/GL/myGL.h"
#include "System/Log/ILog.h"
#include "System/StringUtil.h"
#include "System/StringHash.h"
#include "System/SpringHash.h"

#include "System/Misc/TracyDefs.h"


#define LOG_SECTION_SHADER "Shader"

namespace {
	/// Extract "#version ..." line from source, return it and erase from source
	bool ExtractGlslVersion(std::string* src, std::string* version)
	{
		const auto pos = src->find("#version ");
		if (pos != std::string::npos) {
			const auto eol = src->find('\n', pos) + 1;
			*version = src->substr(pos, eol - pos);
			src->erase(pos, eol - pos);
			return true;
		}
		return false;
	}
}


namespace Shader {

RHIProgramObject::RHIProgramObject(const std::string& poName)
	: IProgramObject(poName)
{
}

RHIProgramObject::~RHIProgramObject()
{
	Release();
}

void RHIProgramObject::BindAttribLocation(const std::string& name, uint32_t index)
{
	attribLocations[name] = index;
}

void RHIProgramObject::BindOutputLocation(const std::string& name, uint32_t index)
{
	outputLocations[name] = index;
}

void RHIProgramObject::Enable()
{
	RECOIL_DETAILED_TRACY_ZONE;
	RecompileIfNeeded(true);
	EnableRaw();
}

void RHIProgramObject::EnableRaw()
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (rhiShader && rhiShader->IsValid())
		rhiShader->Bind();
	IProgramObject::Enable();
}

void RHIProgramObject::DisableRaw()
{
	RECOIL_DETAILED_TRACY_ZONE;
	IProgramObject::Disable();
	if (rhiShader && rhiShader->IsBound())
		rhiShader->Unbind();
}

void RHIProgramObject::Link()
{
	RECOIL_DETAILED_TRACY_ZONE;
	RecompileIfNeeded(false);
}

bool RHIProgramObject::Validate()
{
	RECOIL_DETAILED_TRACY_ZONE;
	valid = (rhiShader && rhiShader->IsValid());
	if (!valid && logReporting)
		LOG_L(L_ERROR, "[RHI-PO::%s] program-object \"%s\" is not valid", __func__, name.c_str());
	return valid;
}

void RHIProgramObject::Release()
{
	RECOIL_DETAILED_TRACY_ZONE;
	IProgramObject::Release();
	rhiShader.reset();
	curSrcHash = 0;
}

RHI::ShaderStage RHIProgramObject::GLTypeToRHIStage(unsigned int glType)
{
	switch (glType) {
		case GL_VERTEX_SHADER:   return RHI::ShaderStage::Vertex;
		case GL_FRAGMENT_SHADER: return RHI::ShaderStage::Fragment;
		case GL_GEOMETRY_SHADER: return RHI::ShaderStage::Geometry;
		default:
			LOG_L(L_ERROR, "[RHI-PO] Unknown GL shader type: 0x%X", glType);
			return RHI::ShaderStage::Vertex;
	}
}

std::string RHIProgramObject::ComposeGLSLSource(IShaderObject* so)
{
	// Read source from disk if not loaded yet
	if (so->GetSrcText().empty()) {
		so->ReloadFromDisk();
	}

	std::string sourceStr = so->GetSrcText();
	std::string defFlags = so->GetRawDefStrs() + "\n" + so->GetModDefStrs();
	std::string versionStr;

	// Extract #version pragma and put it first (only allowed on first line)
	// Version pragma in definitions overrides version pragma in source
	ExtractGlslVersion(&sourceStr, &versionStr);
	ExtractGlslVersion(&defFlags, &versionStr);

	if (!versionStr.empty()) EnsureEndsWith(&versionStr, "\n");
	if (!defFlags.empty())   EnsureEndsWith(&defFlags, "\n");

	// Compose in same order as GLSLShaderObject::CompileShaderObject()
	return versionStr +
		"// SHADER FLAGS\n" +
		defFlags +
		"// SHADER SOURCE\n" +
		"#line 1\n" +
		sourceStr;
}

void RHIProgramObject::Reload(bool reloadFromDisk, bool validate)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const unsigned int oldSrcHash = curSrcHash;
	valid = false;

	// Update definitions from shader flags
	for (IShaderObject*& so : shaderObjs) {
		so->SetDefinitions(shaderFlags.GetString());
	}

	log.clear();

	// Reload shader source from disk if requested
	if (reloadFromDisk) {
		for (IShaderObject*& so : shaderObjs) {
			so->ReloadFromDisk();
		}
	}

	// Compute source hash
	curSrcHash = shaderFlags.UpdateHash();
	for (const IShaderObject* so : shaderObjs) {
		curSrcHash ^= so->GetHash();
	}

	if (shaderObjs.empty())
		return;

	// Skip recompile if hash hasn't changed and we have a valid shader
	if (curSrcHash == oldSrcHash && rhiShader && rhiShader->IsValid()) {
		valid = true;
		return;
	}

	// Get or create the RHI device
	auto* device = RHI::GetDevice();
	if (!device) {
		LOG_L(L_ERROR, "[RHI-PO::%s] no RHI device available for \"%s\"", __func__, name.c_str());
		return;
	}

	// Create a new RHI shader
	auto newShader = device->CreateShader(std::string("[RHI-PO:") + name + "]");
	if (!newShader) {
		LOG_L(L_ERROR, "[RHI-PO::%s] failed to create RHI shader for \"%s\"", __func__, name.c_str());
		return;
	}

	// Bind attribute and output locations before compilation
	for (const auto& [attrName, index] : attribLocations) {
		newShader->BindAttribLocation(attrName, index);
	}
	for (const auto& [outName, index] : outputLocations) {
		newShader->BindOutputLocation(outName, index);
	}

	// Attach each shader stage
	for (IShaderObject*& so : shaderObjs) {
		const RHI::ShaderStage stage = GLTypeToRHIStage(so->GetType());
		const std::string composedSource = ComposeGLSLSource(so);

		if (composedSource.empty()) {
			LOG_L(L_ERROR, "[RHI-PO::%s] empty source for stage %d in \"%s\" (file: %s)",
				__func__, (int)stage, name.c_str(), so->GetSrcFile().c_str());
			return;
		}

		newShader->AttachStageFromSource(stage, composedSource, "");
	}

	// Link (triggers GLSL -> SPIR-V -> MSL compilation on Metal)
	newShader->Link();
	valid = newShader->IsValid();

	if (!valid) {
		LOG_L(L_WARNING, "[RHI-PO::%s] shader \"%s\" failed to link", __func__, name.c_str());
		for (IShaderObject*& so : shaderObjs) {
			LOG_L(L_WARNING, "[RHI-PO::%s]   stage %s: %s", __func__,
				(so->GetType() == GL_VERTEX_SHADER ? "VS" : "FS"),
				so->GetSrcFile().c_str());
		}
		return;
	}

	// Replace old shader
	rhiShader = std::move(newShader);

	// Populate uniform states from shader reflection
	auto uniformDescs = rhiShader->GetActiveUniformDescs();
	for (const auto& desc : uniformDescs) {
		if (desc.name.empty() || desc.name.compare(0, 3, "gl_") == 0)
			continue;
		// Create or update the uniform state entry
		// Location is set to 0 (not used for RHI path — name-based lookup)
		GetUniformState(desc.name)->SetLocation(0);
	}

	LOG_L(L_DEBUG, "[RHI-PO::%s] shader \"%s\" compiled successfully (%zu uniforms)",
		__func__, name.c_str(), uniformDescs.size());
}


// ---------------------------------------------------------------
// Uniform location management
// ---------------------------------------------------------------

int RHIProgramObject::GetUniformLoc(const char* name)
{
	// RHI uses name-based uniform access; return 0 as a valid sentinel
	return 0;
}

int RHIProgramObject::GetUniformType(const int idx)
{
	return -1;
}

void RHIProgramObject::SetUniformLocation(const std::string& name)
{
	uniformLocs.push_back(hashString(name.c_str()));
	GetUniformLocation(name);
}


// ---------------------------------------------------------------
// UniformState-based interface (name-based, primary path)
// These override the base class to forward to IRHIShader by name.
// ---------------------------------------------------------------

void RHIProgramObject::SetUniform(UniformState* uState, int   v0)                               { if (rhiShader && uState->Set(v0            )) rhiShader->SetUniform1i(uState->GetName(), v0            ); }
void RHIProgramObject::SetUniform(UniformState* uState, float v0)                               { if (rhiShader && uState->Set(v0            )) rhiShader->SetUniform1f(uState->GetName(), v0            ); }
void RHIProgramObject::SetUniform(UniformState* uState, int   v0, int   v1)                     { if (rhiShader && uState->Set(v0, v1        )) rhiShader->SetUniform2i(uState->GetName(), v0, v1        ); }
void RHIProgramObject::SetUniform(UniformState* uState, float v0, float v1)                     { if (rhiShader && uState->Set(v0, v1        )) rhiShader->SetUniform2f(uState->GetName(), v0, v1        ); }
void RHIProgramObject::SetUniform(UniformState* uState, int   v0, int   v1, int   v2)           { if (rhiShader && uState->Set(v0, v1, v2    )) rhiShader->SetUniform3i(uState->GetName(), v0, v1, v2    ); }
void RHIProgramObject::SetUniform(UniformState* uState, float v0, float v1, float v2)           { if (rhiShader && uState->Set(v0, v1, v2    )) rhiShader->SetUniform3f(uState->GetName(), v0, v1, v2    ); }
void RHIProgramObject::SetUniform(UniformState* uState, int   v0, int   v1, int   v2, int   v3) { if (rhiShader && uState->Set(v0, v1, v2, v3)) rhiShader->SetUniform4i(uState->GetName(), v0, v1, v2, v3); }
void RHIProgramObject::SetUniform(UniformState* uState, float v0, float v1, float v2, float v3) { if (rhiShader && uState->Set(v0, v1, v2, v3)) rhiShader->SetUniform4f(uState->GetName(), v0, v1, v2, v3); }

void RHIProgramObject::SetUniform1v(UniformState* uState, int count, const int*   v) { if (rhiShader) { int   iv[4] = {v[0]};           rhiShader->SetUniform1i(uState->GetName(), v[0]); } }
void RHIProgramObject::SetUniform1v(UniformState* uState, int count, const float* v) { if (rhiShader) {                                  rhiShader->SetUniform1f(uState->GetName(), v[0]); } }
void RHIProgramObject::SetUniform2v(UniformState* uState, int count, const int*   v) { if (rhiShader && uState->Set2v(v)) rhiShader->SetUniform2iv(uState->GetName(), v); }
void RHIProgramObject::SetUniform2v(UniformState* uState, int count, const float* v) { if (rhiShader && uState->Set2v(v)) rhiShader->SetUniform2fv(uState->GetName(), v); }
void RHIProgramObject::SetUniform3v(UniformState* uState, int count, const int*   v) { if (rhiShader && uState->Set3v(v)) rhiShader->SetUniform3iv(uState->GetName(), v); }
void RHIProgramObject::SetUniform3v(UniformState* uState, int count, const float* v) { if (rhiShader && uState->Set3v(v)) rhiShader->SetUniform3fv(uState->GetName(), v); }
void RHIProgramObject::SetUniform4v(UniformState* uState, int count, const int*   v) { if (rhiShader && uState->Set4v(v)) rhiShader->SetUniform4iv(uState->GetName(), v); }
void RHIProgramObject::SetUniform4v(UniformState* uState, int count, const float* v) { if (rhiShader && uState->Set4v(v)) rhiShader->SetUniform4fv(uState->GetName(), v); }

void RHIProgramObject::SetUniformMatrix2x2(UniformState* uState, bool transp, const float* v) { if (rhiShader && uState->Set2x2(v, transp)) rhiShader->SetUniformMatrix2fv(uState->GetName(), transp, v); }
void RHIProgramObject::SetUniformMatrix3x3(UniformState* uState, bool transp, const float* v) { if (rhiShader && uState->Set3x3(v, transp)) rhiShader->SetUniformMatrix3fv(uState->GetName(), transp, v); }
void RHIProgramObject::SetUniformMatrix4x4(UniformState* uState, bool transp, const float* v) { if (rhiShader && uState->Set4x4(v, transp)) rhiShader->SetUniformMatrix4fv(uState->GetName(), transp, v); }


// ---------------------------------------------------------------
// Index-based uniform interface (legacy, for SetUniformLocation users)
// Maps index -> uniform name hash -> UniformState -> IRHIShader by name
// ---------------------------------------------------------------

void RHIProgramObject::SetUniform1i(int idx, int   v0)                               { if (!rhiShader || idx < 0 || idx >= (int)uniformLocs.size()) return; auto it = uniformStates.find(uniformLocs[idx]); if (it != uniformStates.end()) SetUniform(&it->second, v0); }
void RHIProgramObject::SetUniform2i(int idx, int   v0, int   v1)                     { if (!rhiShader || idx < 0 || idx >= (int)uniformLocs.size()) return; auto it = uniformStates.find(uniformLocs[idx]); if (it != uniformStates.end()) SetUniform(&it->second, v0, v1); }
void RHIProgramObject::SetUniform3i(int idx, int   v0, int   v1, int   v2)           { if (!rhiShader || idx < 0 || idx >= (int)uniformLocs.size()) return; auto it = uniformStates.find(uniformLocs[idx]); if (it != uniformStates.end()) SetUniform(&it->second, v0, v1, v2); }
void RHIProgramObject::SetUniform4i(int idx, int   v0, int   v1, int   v2, int   v3) { if (!rhiShader || idx < 0 || idx >= (int)uniformLocs.size()) return; auto it = uniformStates.find(uniformLocs[idx]); if (it != uniformStates.end()) SetUniform(&it->second, v0, v1, v2, v3); }
void RHIProgramObject::SetUniform1f(int idx, float v0)                               { if (!rhiShader || idx < 0 || idx >= (int)uniformLocs.size()) return; auto it = uniformStates.find(uniformLocs[idx]); if (it != uniformStates.end()) SetUniform(&it->second, v0); }
void RHIProgramObject::SetUniform2f(int idx, float v0, float v1)                     { if (!rhiShader || idx < 0 || idx >= (int)uniformLocs.size()) return; auto it = uniformStates.find(uniformLocs[idx]); if (it != uniformStates.end()) SetUniform(&it->second, v0, v1); }
void RHIProgramObject::SetUniform3f(int idx, float v0, float v1, float v2)           { if (!rhiShader || idx < 0 || idx >= (int)uniformLocs.size()) return; auto it = uniformStates.find(uniformLocs[idx]); if (it != uniformStates.end()) SetUniform(&it->second, v0, v1, v2); }
void RHIProgramObject::SetUniform4f(int idx, float v0, float v1, float v2, float v3) { if (!rhiShader || idx < 0 || idx >= (int)uniformLocs.size()) return; auto it = uniformStates.find(uniformLocs[idx]); if (it != uniformStates.end()) SetUniform(&it->second, v0, v1, v2, v3); }

void RHIProgramObject::SetUniform2iv(int idx, const int*   v) { if (!rhiShader || idx < 0 || idx >= (int)uniformLocs.size()) return; auto it = uniformStates.find(uniformLocs[idx]); if (it != uniformStates.end() && it->second.Set2v(v)) rhiShader->SetUniform2iv(it->second.GetName(), v); }
void RHIProgramObject::SetUniform3iv(int idx, const int*   v) { if (!rhiShader || idx < 0 || idx >= (int)uniformLocs.size()) return; auto it = uniformStates.find(uniformLocs[idx]); if (it != uniformStates.end() && it->second.Set3v(v)) rhiShader->SetUniform3iv(it->second.GetName(), v); }
void RHIProgramObject::SetUniform4iv(int idx, const int*   v) { if (!rhiShader || idx < 0 || idx >= (int)uniformLocs.size()) return; auto it = uniformStates.find(uniformLocs[idx]); if (it != uniformStates.end() && it->second.Set4v(v)) rhiShader->SetUniform4iv(it->second.GetName(), v); }
void RHIProgramObject::SetUniform2fv(int idx, const float* v) { if (!rhiShader || idx < 0 || idx >= (int)uniformLocs.size()) return; auto it = uniformStates.find(uniformLocs[idx]); if (it != uniformStates.end() && it->second.Set2v(v)) rhiShader->SetUniform2fv(it->second.GetName(), v); }
void RHIProgramObject::SetUniform3fv(int idx, const float* v) { if (!rhiShader || idx < 0 || idx >= (int)uniformLocs.size()) return; auto it = uniformStates.find(uniformLocs[idx]); if (it != uniformStates.end() && it->second.Set3v(v)) rhiShader->SetUniform3fv(it->second.GetName(), v); }
void RHIProgramObject::SetUniform4fv(int idx, const float* v) { if (!rhiShader || idx < 0 || idx >= (int)uniformLocs.size()) return; auto it = uniformStates.find(uniformLocs[idx]); if (it != uniformStates.end() && it->second.Set4v(v)) rhiShader->SetUniform4fv(it->second.GetName(), v); }

void RHIProgramObject::SetUniform1iv(int idx, int count, const int*   v) { SetUniform1i(idx, v[0]); }
void RHIProgramObject::SetUniform2iv(int idx, int count, const int*   v) { SetUniform2iv(idx, v); }
void RHIProgramObject::SetUniform3iv(int idx, int count, const int*   v) { SetUniform3iv(idx, v); }
void RHIProgramObject::SetUniform4iv(int idx, int count, const int*   v) { SetUniform4iv(idx, v); }
void RHIProgramObject::SetUniform1fv(int idx, int count, const float* v) { SetUniform1f(idx, v[0]); }
void RHIProgramObject::SetUniform2fv(int idx, int count, const float* v) { SetUniform2fv(idx, v); }
void RHIProgramObject::SetUniform3fv(int idx, int count, const float* v) { SetUniform3fv(idx, v); }
void RHIProgramObject::SetUniform4fv(int idx, int count, const float* v) { SetUniform4fv(idx, v); }

void RHIProgramObject::SetUniformMatrix2fv(int idx, bool transp, const float* v) { if (!rhiShader || idx < 0 || idx >= (int)uniformLocs.size()) return; auto it = uniformStates.find(uniformLocs[idx]); if (it != uniformStates.end() && it->second.Set2x2(v, transp)) rhiShader->SetUniformMatrix2fv(it->second.GetName(), transp, v); }
void RHIProgramObject::SetUniformMatrix3fv(int idx, bool transp, const float* v) { if (!rhiShader || idx < 0 || idx >= (int)uniformLocs.size()) return; auto it = uniformStates.find(uniformLocs[idx]); if (it != uniformStates.end() && it->second.Set3x3(v, transp)) rhiShader->SetUniformMatrix3fv(it->second.GetName(), transp, v); }
void RHIProgramObject::SetUniformMatrix4fv(int idx, bool transp, const float* v) { if (!rhiShader || idx < 0 || idx >= (int)uniformLocs.size()) return; auto it = uniformStates.find(uniformLocs[idx]); if (it != uniformStates.end() && it->second.Set4x4(v, transp)) rhiShader->SetUniformMatrix4fv(it->second.GetName(), transp, v); }


} // namespace Shader
