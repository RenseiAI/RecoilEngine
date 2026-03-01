/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#import "MTLShader.h"
#import "MTLDevice.h"

#import <Metal/Metal.h>
#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <cstring>

#include "Rendering/RHI/ShaderCompiler.h"
#include "Rendering/RHI/ShaderReflection.h"
#include "System/Log/ILog.h"
#include "System/FileSystem/FileHandler.h"

namespace RHI {

/// Fix gl_PerVertex output location qualifiers in vertex MSL.
///
/// SPIRV-Cross assigns [[user(locnN)]] to gl_PerVertex compatibility builtins
/// (gl_Color, gl_TexCoord, gl_FogFragCoord, gl_SecondaryColor) in fragment
/// inputs but NOT in vertex outputs. Metal requires matching location qualifiers
/// on both sides for PSO creation. This function parses the fragment's input
/// struct locations and applies them to matching vertex output struct members.
///
/// Name mapping (vertex → fragment):
///   gl_FrontColor         → gl_Color
///   gl_FrontSecondaryColor → gl_SecondaryColor
///   gl_TexCoord_N         → gl_TexCoord_N  (identical)
///   gl_FogFragCoord       → gl_FogFragCoord (identical)
///   gl_ClipVertex         → (no fragment counterpart, skip)
///   gl_BackColor          → (no fragment counterpart, skip)
///   gl_BackSecondaryColor → (no fragment counterpart, skip)
static void FixGlPerVertexOutputLocations(std::string& vertexMSL, const std::string& fragmentMSL) {
	const std::string FIXUP_GL = "_RESERVED_IDENTIFIER_FIXUP_gl_";

	// Step 1: Parse fragmentMain_in struct for gl_PerVertex member locations.
	// Pattern: "TYPE m_NN_RESERVED_IDENTIFIER_FIXUP_gl_MEMBER [[user(locnN)]];"
	size_t fragStructPos = fragmentMSL.find("struct fragmentMain_in");
	if (fragStructPos == std::string::npos) return;
	size_t fragStructEnd = fragmentMSL.find("};", fragStructPos);
	if (fragStructEnd == std::string::npos) return;

	// Map: fragment gl_ member name (e.g. "gl_Color") → location number
	std::map<std::string, uint32_t> fragNameToLocn;

	size_t searchPos = fragStructPos;
	while ((searchPos = fragmentMSL.find(FIXUP_GL, searchPos)) != std::string::npos
	       && searchPos < fragStructEnd)
	{
		// Extract the gl_MEMBER suffix
		size_t nameStart = searchPos + FIXUP_GL.size();
		size_t nameEnd = nameStart;
		while (nameEnd < fragmentMSL.size()
		       && (std::isalnum(fragmentMSL[nameEnd]) || fragmentMSL[nameEnd] == '_'))
			nameEnd++;
		std::string memberSuffix = fragmentMSL.substr(nameStart, nameEnd - nameStart);
		std::string glName = "gl_" + memberSuffix;

		// Find [[user(locnN)]] on the same line
		size_t lineEnd = fragmentMSL.find('\n', searchPos);
		if (lineEnd == std::string::npos) lineEnd = fragmentMSL.size();
		std::string line = fragmentMSL.substr(searchPos, lineEnd - searchPos);
		size_t locnPos = line.find("[[user(locn");
		if (locnPos != std::string::npos) {
			size_t numStart = locnPos + 11; // strlen("[[user(locn")
			size_t numEnd = line.find(")", numStart);
			if (numEnd != std::string::npos) {
				try {
					uint32_t locn = static_cast<uint32_t>(std::stoul(line.substr(numStart, numEnd - numStart)));
					fragNameToLocn[glName] = locn;
				} catch (...) {}
			}
		}

		searchPos = nameEnd;
	}

	if (fragNameToLocn.empty()) return;

	// Step 2: Build vertex member name → location mapping.
	// Map fragment names to their vertex-side equivalents.
	std::map<std::string, uint32_t> vertNameToLocn;
	for (const auto& [fragName, locn] : fragNameToLocn) {
		// gl_Color → gl_FrontColor
		if (fragName == "gl_Color") {
			vertNameToLocn["gl_FrontColor"] = locn;
		}
		// gl_SecondaryColor → gl_FrontSecondaryColor
		else if (fragName == "gl_SecondaryColor") {
			vertNameToLocn["gl_FrontSecondaryColor"] = locn;
		}
		// All others keep their name (gl_TexCoord_N, gl_FogFragCoord)
		else {
			vertNameToLocn[fragName] = locn;
		}
	}

	// Step 3: In vertexMain_out struct, add [[user(locnN)]] to matching members.
	size_t vtxStructPos = vertexMSL.find("struct vertexMain_out");
	if (vtxStructPos == std::string::npos) return;
	size_t vtxStructEnd = vertexMSL.find("};", vtxStructPos);
	if (vtxStructEnd == std::string::npos) return;

	// Collect insertion points (position before ';' → qualifier text)
	// Process in reverse order so earlier insertions don't shift later positions.
	std::vector<std::pair<size_t, std::string>> insertions;

	searchPos = vtxStructPos;
	while ((searchPos = vertexMSL.find(FIXUP_GL, searchPos)) != std::string::npos
	       && searchPos < vtxStructEnd)
	{
		// Extract the gl_MEMBER suffix
		size_t nameStart = searchPos + FIXUP_GL.size();
		size_t nameEnd = nameStart;
		while (nameEnd < vertexMSL.size()
		       && (std::isalnum(vertexMSL[nameEnd]) || vertexMSL[nameEnd] == '_'))
			nameEnd++;
		std::string memberSuffix = vertexMSL.substr(nameStart, nameEnd - nameStart);
		std::string glName = "gl_" + memberSuffix;

		// Check: already has [[user(locn...]]) on this line?
		size_t lineEnd = vertexMSL.find('\n', searchPos);
		if (lineEnd == std::string::npos) lineEnd = vertexMSL.size();
		std::string line = vertexMSL.substr(searchPos, lineEnd - searchPos);
		if (line.find("[[user(locn") != std::string::npos
		    || line.find("[[position]]") != std::string::npos
		    || line.find("[[point_size]]") != std::string::npos) {
			searchPos = nameEnd;
			continue;
		}

		// Look up in our mapping
		auto it = vertNameToLocn.find(glName);
		if (it != vertNameToLocn.end()) {
			// Find the ';' that ends this struct member declaration
			size_t semiPos = vertexMSL.find(';', nameEnd);
			if (semiPos != std::string::npos && semiPos < vtxStructEnd) {
				std::string qualifier = " [[user(locn" + std::to_string(it->second) + ")]]";
				insertions.push_back({semiPos, qualifier});
				LOG("[MTLShader] Added [[user(locn%u)]] to vertex output %s (matches fragment %s)",
				    it->second, glName.c_str(),
				    (glName.find("Front") != std::string::npos)
				        ? glName.substr(glName.find("Front") + 5).c_str()
				        : glName.c_str());
			}
		}

		searchPos = nameEnd;
	}

	if (insertions.empty()) return;

	// Apply insertions in reverse position order
	std::sort(insertions.begin(), insertions.end(),
	          [](const auto& a, const auto& b) { return a.first > b.first; });
	for (const auto& [pos, text] : insertions) {
		vertexMSL.insert(pos, text);
	}
}

/// Ensure the vertex shader outputs all varyings the fragment expects.
/// In OpenGL, unwritten varyings default to zero. Metal requires explicit
/// vertex-to-fragment interface matching. This function adds zero-initialized
/// dummy outputs for any fragment input [[user(locnN)]] not present in the
/// vertex output struct. Runs AFTER FixGlPerVertexOutputLocations.
static void PadMissingVertexOutputs(std::string& vertexMSL, const std::string& fragmentMSL) {
	// Step 1: Parse fragmentMain_in for all [[user(locnN)]] entries with types
	size_t fragStructPos = fragmentMSL.find("struct fragmentMain_in");
	if (fragStructPos == std::string::npos) return;
	size_t fragStructEnd = fragmentMSL.find("};", fragStructPos);
	if (fragStructEnd == std::string::npos) return;

	struct VaryingInfo {
		std::string type;
		std::string name;
		uint32_t locn;
	};
	std::vector<VaryingInfo> fragmentInputs;

	// Parse each line in fragmentMain_in for "TYPE NAME [[user(locnN)]];"
	size_t linePos = fragmentMSL.find('\n', fragStructPos);
	while (linePos != std::string::npos && linePos < fragStructEnd) {
		linePos++; // skip the newline
		size_t lineEnd = fragmentMSL.find('\n', linePos);
		if (lineEnd == std::string::npos || lineEnd > fragStructEnd) lineEnd = fragStructEnd;

		std::string line = fragmentMSL.substr(linePos, lineEnd - linePos);
		linePos = lineEnd;

		// Look for [[user(locnN)]]
		size_t locnPos = line.find("[[user(locn");
		if (locnPos == std::string::npos) continue;

		size_t numStart = locnPos + 11;
		size_t numEnd = line.find(")", numStart);
		if (numEnd == std::string::npos) continue;

		uint32_t locn;
		try { locn = static_cast<uint32_t>(std::stoul(line.substr(numStart, numEnd - numStart))); }
		catch (...) { continue; }

		// Extract type and name: "    TYPE NAME [[user(locnN)]];"
		size_t typeStart = line.find_first_not_of(" \t");
		if (typeStart == std::string::npos) continue;

		// Find the [[user portion to get everything before it
		std::string before = line.substr(typeStart, locnPos - typeStart);
		// Trim trailing whitespace
		while (!before.empty() && (before.back() == ' ' || before.back() == '\t'))
			before.pop_back();

		// Split into type and name at last space
		size_t lastSpace = before.rfind(' ');
		if (lastSpace == std::string::npos) continue;

		std::string type = before.substr(0, lastSpace);
		std::string name = before.substr(lastSpace + 1);
		// Trim type trailing space
		while (!type.empty() && type.back() == ' ') type.pop_back();

		fragmentInputs.push_back({type, name, locn});
	}

	if (fragmentInputs.empty()) return;

	// Step 2: Collect existing [[user(locnN)]] locations in vertexMain_out
	size_t vtxStructPos = vertexMSL.find("struct vertexMain_out");
	if (vtxStructPos == std::string::npos) return;
	size_t vtxStructEnd = vertexMSL.find("};", vtxStructPos);
	if (vtxStructEnd == std::string::npos) return;

	std::set<uint32_t> existingLocations;
	size_t searchPos = vtxStructPos;
	while (searchPos < vtxStructEnd) {
		size_t lp = vertexMSL.find("[[user(locn", searchPos);
		if (lp == std::string::npos || lp >= vtxStructEnd) break;
		size_t ns = lp + 11;
		size_t ne = vertexMSL.find(")", ns);
		if (ne != std::string::npos) {
			try { existingLocations.insert(static_cast<uint32_t>(std::stoul(vertexMSL.substr(ns, ne - ns)))); }
			catch (...) {}
		}
		searchPos = lp + 1;
	}

	// Step 3: Find missing locations
	std::vector<VaryingInfo> missing;
	for (const auto& fi : fragmentInputs) {
		if (existingLocations.find(fi.locn) == existingLocations.end()) {
			missing.push_back(fi);
		}
	}

	if (missing.empty()) return;

	// Step 4: Add dummy members to vertexMain_out before closing "};"
	std::string structAdditions;
	for (const auto& m : missing) {
		structAdditions += "    " + m.type + " _pad_locn" + std::to_string(m.locn)
		                 + " [[user(locn" + std::to_string(m.locn) + ")]];\n";
		LOG("[MTLShader] Padded vertex output: '%s _pad_locn%u [[user(locn%u)]]' "
		    "(fragment expects '%s')", m.type.c_str(), m.locn, m.locn, m.name.c_str());
	}
	// vtxStructEnd may have shifted if FixGlPerVertexOutputLocations ran, re-find it
	vtxStructEnd = vertexMSL.find("};", vtxStructPos);
	if (vtxStructEnd == std::string::npos) return;
	vertexMSL.insert(vtxStructEnd, structAdditions);

	// Step 5: Zero-initialize the padding members before "return out;"
	size_t returnPos = vertexMSL.rfind("return out;");
	if (returnPos != std::string::npos) {
		std::string inits;
		for (const auto& m : missing) {
			std::string padName = "out._pad_locn" + std::to_string(m.locn);
			if (m.type == "float") {
				inits += "    " + padName + " = 0.0;\n";
			} else {
				inits += "    " + padName + " = " + m.type + "(0);\n";
			}
		}
		vertexMSL.insert(returnPos, inits);
	}
}

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

	// Initialize texture remap arrays to -1 (identity mapping)
	std::fill(std::begin(vsTextureRemap), std::end(vsTextureRemap), -1);
	std::fill(std::begin(fsTextureRemap), std::end(fsTextureRemap), -1);
	std::fill(std::begin(vsSamplerRemap), std::end(vsSamplerRemap), -1);
	std::fill(std::begin(fsSamplerRemap), std::end(fsSamplerRemap), -1);
}

MTLShader::~MTLShader() {
	Release();
}

void MTLShader::AttachStage(ShaderStage stage, const std::string& sourceFile, const std::string& defines) {
	// GL shader loading prepends "shaders/" to relative paths (see Shader.cpp).
	// Mirror that convention so paths like "GLSL/SMFShadingTextureVertProg.glsl"
	// resolve correctly in the VFS.
	const std::string fullPath = "shaders/" + sourceFile;
	CFileHandler file(fullPath);
	if (!file.FileExists()) {
		LOG_L(L_ERROR, "[MTLShader] Shader file not found: %s (tried: %s)", sourceFile.c_str(), fullPath.c_str());
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
		// Pass attribLocations so SPIRV-Cross can assign [[attribute(N)]] decorations
		// to vertex inputs that lack explicit layout(location=N) in GLSL
		vertexMSL = shaderCompiler->CompileGLSLToMSL(vertexSourceWithDefines, CompilerShaderStage::Vertex, attribLocations);
		if (vertexMSL.empty()) {
			LOG_L(L_ERROR, "[MTLShader] %s: Failed to compile vertex shader: %s",
			      shaderName.c_str(), shaderCompiler->GetLastError().c_str());
			return;
		}
	}

	if (!fragmentSource.empty()) {
		// Pass outputLocations so SPIRV-Cross can assign correct [[color(N)]]
		// decorations to fragment outputs matching the FBO attachment layout
		fragmentMSL = shaderCompiler->CompileGLSLToMSL(fragmentSourceWithDefines, CompilerShaderStage::Fragment, {}, outputLocations);
		if (fragmentMSL.empty()) {
			LOG_L(L_ERROR, "[MTLShader] %s: Failed to compile fragment shader: %s",
			      shaderName.c_str(), shaderCompiler->GetLastError().c_str());
			return;
		}
	}

	// Fix gl_PerVertex output locations: SPIRV-Cross generates [[user(locnN)]]
	// for fragment input builtins but not vertex output builtins. Parse the
	// fragment's locations and apply them to the matching vertex output members.
	if (!vertexMSL.empty() && !fragmentMSL.empty()) {
		FixGlPerVertexOutputLocations(vertexMSL, fragmentMSL);
	}

	// Pad missing vertex outputs: OpenGL defaults unwritten varyings to zero,
	// but Metal requires explicit matching. Add zero-initialized dummy outputs
	// for any fragment input location the vertex shader doesn't write.
	if (!vertexMSL.empty() && !fragmentMSL.empty()) {
		PadMissingVertexOutputs(vertexMSL, fragmentMSL);
	}

	// Log the generated MSL for debugging pipeline creation issues
	if (!vertexMSL.empty()) {
		LOG("[MTLShader] %s: Vertex MSL (%zu bytes):\n%s",
		    shaderName.c_str(), vertexMSL.size(), vertexMSL.c_str());
	}
	if (!fragmentMSL.empty()) {
		LOG("[MTLShader] %s: Fragment MSL (%zu bytes):\n%s",
		    shaderName.c_str(), fragmentMSL.size(), fragmentMSL.c_str());
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

	// Pre-populate uniform map from SPIR-V reflection data.
	//
	// Metal assigns buffer indices INDEPENDENTLY per stage:
	//   Vertex:   constant float4x4& modelMatrix [[buffer(0)]]
	//   Fragment: constant float4&   alphaCtrl   [[buffer(0)]]
	// Buffer(0) can mean different data in each stage!
	//
	// Strategy:
	//   1. Allocate each uniform a unique slot in uniformData
	//   2. Track per-stage buffer indices (vsBufferIndex / fsBufferIndex)
	//   3. Build separate upload ranges for vertex and fragment stages
	//   4. Upload vertex ranges with setVertexBytes, fragment with setFragmentBytes
	uniformMap.clear();
	uniformData.clear();
	uniformBufferSize = 0;

	const auto* vsRefl = !vertexSource.empty()
		? shaderCompiler->GetCachedReflection(vertexDefines + "\n" + vertexSource, CompilerShaderStage::Vertex)
		: nullptr;
	const auto* fsRefl = !fragmentSource.empty()
		? shaderCompiler->GetCachedReflection(fragmentDefines + "\n" + fragmentSource, CompilerShaderStage::Fragment)
		: nullptr;

	size_t nextOffset = 0;

	// Helper: add uniforms from one stage's reflection
	auto populateFromStage = [&](const ShaderReflection* refl, bool isVertex) {
		if (!refl)
			return;
		for (const auto& u : refl->uniforms) {
			size_t dataSize = ShaderDataTypeSize(u.type) * static_cast<size_t>(u.arraySize);
			bool isMat = (u.type == ShaderDataType::Mat3 || u.type == ShaderDataType::Mat4);

			auto it = uniformMap.find(u.name);
			if (it != uniformMap.end()) {
				// Uniform already exists (from other stage) — add this stage's buffer index
				if (isVertex)
					it->second.vsBufferIndex = u.metalBufferIndex;
				else
					it->second.fsBufferIndex = u.metalBufferIndex;
				continue;
			}

			// New uniform — allocate a slot in uniformData
			size_t offset;
			if (u.offset >= 0) {
				// UBO member: use SPIR-V struct offset so members of the same
				// stage's UBO are contiguous (needed for non-decomposed UBOs)
				offset = static_cast<size_t>(u.offset);
			} else {
				// Standalone (gl_plain_uniforms): append after current end
				offset = (nextOffset + 15) & ~size_t(15);
			}

			UniformInfo info;
			info.offset = offset;
			info.size = dataSize;
			info.isMatrix = isMat;
			info.vsBufferIndex = isVertex ? u.metalBufferIndex : -1;
			info.fsBufferIndex = isVertex ? -1 : u.metalBufferIndex;

			uniformMap[u.name] = info;

			size_t end = offset + dataSize;
			if (end > nextOffset)
				nextOffset = end;
		}
	};

	// Process vertex first, then fragment
	populateFromStage(vsRefl, true);
	populateFromStage(fsRefl, false);

	// Total buffer
	uniformBufferSize = (nextOffset + 15) & ~size_t(15);
	if (uniformBufferSize < 16) uniformBufferSize = 16;
	uniformData.resize(uniformBufferSize, 0);

	LOG("[MTLShader] %s: Linked successfully (%zu uniforms, %zu bytes)",
	    shaderName.c_str(), uniformMap.size(), uniformBufferSize);
	for (const auto& [uName, uInfo] : uniformMap) {
		LOG("[MTLShader] %s:   uniform '%s' vsBuf=%d fsBuf=%d offset=%zu size=%zu",
		    shaderName.c_str(), uName.c_str(), uInfo.vsBufferIndex, uInfo.fsBufferIndex,
		    uInfo.offset, uInfo.size);
	}

	// Extract sampler reflection data for texture unit remapping.
	// SPIRV-Cross assigns [[texture(N)]] indices independently of GL texture units.
	// When the engine later calls SetUniform1i("samplerName", glUnit), we use
	// this mapping to translate GL unit → Metal texture index.
	samplerInfoMap.clear();
	std::fill(std::begin(vsTextureRemap), std::end(vsTextureRemap), -1);
	std::fill(std::begin(fsTextureRemap), std::end(fsTextureRemap), -1);
	std::fill(std::begin(vsSamplerRemap), std::end(vsSamplerRemap), -1);
	std::fill(std::begin(fsSamplerRemap), std::end(fsSamplerRemap), -1);
	hasTextureRemap = false;

	auto populateSamplers = [&](const ShaderReflection* refl, bool isVertex) {
		if (!refl)
			return;
		for (const auto& s : refl->samplers) {
			auto& info = samplerInfoMap[s.name];
			if (isVertex) {
				info.vsMetalTextureIndex = s.metalTextureIndex;
				info.vsMetalSamplerIndex = s.metalSamplerIndex;
			} else {
				info.fsMetalTextureIndex = s.metalTextureIndex;
				info.fsMetalSamplerIndex = s.metalSamplerIndex;
			}
		}
	};
	populateSamplers(vsRefl, true);
	populateSamplers(fsRefl, false);

	for (const auto& [sName, sInfo] : samplerInfoMap) {
		LOG("[MTLShader] %s:   sampler '%s' vsTexIdx=%d fsTexIdx=%d vsSmpIdx=%d fsSmpIdx=%d",
		    shaderName.c_str(), sName.c_str(),
		    sInfo.vsMetalTextureIndex, sInfo.fsMetalTextureIndex,
		    sInfo.vsMetalSamplerIndex, sInfo.fsMetalSamplerIndex);
	}

	// Pre-compute per-stage buffer upload ranges
	RebuildBufferRanges();
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
	// (e.g., engine-injected uniforms like sampler bindings).
	// These get metalBufferIndex = -1 and won't be uploaded to Metal
	// (samplers are bound separately via BindTexture, not as buffer data).
	LOG_L(L_WARNING, "[MTLShader] %s: uniform '%s' NOT in reflection (size=%zu) — dynamic slot (not uploaded to Metal)",
	      shaderName.c_str(), name, size);
	size_t alignedSize = (size + 15) & ~size_t(15);
	size_t offset = uniformBufferSize;

	uniformBufferSize += alignedSize;
	if (uniformData.size() < uniformBufferSize) {
		uniformData.resize(uniformBufferSize, 0);
	}

	uniformMap[name] = {offset, size, false, -1};
	return offset;
}

void MTLShader::SetUniformData(const char* name, const void* data, size_t size) {
	size_t offset = GetOrCreateUniformSlot(name, size);
	std::memcpy(uniformData.data() + offset, data, size);
}

// --- Uniform setters (int) ---

void MTLShader::SetUniform1i(const char* name, int v0) {
	// Check if this is a sampler uniform (e.g., "diffuseTex", value = GL texture unit).
	// On OpenGL, glUniform1i(loc, N) tells the shader to sample from texture unit N.
	// On Metal, we need to map this GL unit to the [[texture(N)]] index that
	// SPIRV-Cross assigned during compilation.
	auto samplerIt = samplerInfoMap.find(name);
	if (samplerIt != samplerInfoMap.end() && v0 >= 0 && v0 < kMaxTextureUnits) {
		const auto& info = samplerIt->second;
		if (info.vsMetalTextureIndex >= 0) {
			vsTextureRemap[v0] = info.vsMetalTextureIndex;
			vsSamplerRemap[v0] = (info.vsMetalSamplerIndex >= 0)
				? info.vsMetalSamplerIndex : info.vsMetalTextureIndex;
			hasTextureRemap = true;
		}
		if (info.fsMetalTextureIndex >= 0) {
			fsTextureRemap[v0] = info.fsMetalTextureIndex;
			fsSamplerRemap[v0] = (info.fsMetalSamplerIndex >= 0)
				? info.fsMetalSamplerIndex : info.fsMetalTextureIndex;
			hasTextureRemap = true;
		}
		static int remapLogCount = 0;
		if (remapLogCount++ < 50) {
			LOG("[MTLShader] %s: sampler '%s' glUnit=%d → vsTexIdx=%d fsTexIdx=%d",
			    shaderName.c_str(), name, v0,
			    info.vsMetalTextureIndex, info.fsMetalTextureIndex);
		}
		// Don't store sampler uniforms in the uniform buffer — Metal
		// doesn't use sampler uniforms, textures are bound directly.
		return;
	}

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

static uint32_t ShaderDataTypeToGLType(ShaderDataType type) {
	// GL constants for Lua API compatibility
	// These values are the same regardless of whether GLAD is loaded
	switch (type) {
		case ShaderDataType::Float:           return 0x1406; // GL_FLOAT
		case ShaderDataType::Vec2:            return 0x8B50; // GL_FLOAT_VEC2
		case ShaderDataType::Vec3:            return 0x8B51; // GL_FLOAT_VEC3
		case ShaderDataType::Vec4:            return 0x8B52; // GL_FLOAT_VEC4
		case ShaderDataType::Int:             return 0x1404; // GL_INT
		case ShaderDataType::IVec2:           return 0x8B53; // GL_INT_VEC2
		case ShaderDataType::IVec3:           return 0x8B54; // GL_INT_VEC3
		case ShaderDataType::IVec4:           return 0x8B55; // GL_INT_VEC4
		case ShaderDataType::Mat3:            return 0x8B5B; // GL_FLOAT_MAT3
		case ShaderDataType::Mat4:            return 0x8B5C; // GL_FLOAT_MAT4
		case ShaderDataType::Sampler2D:       return 0x8B5E; // GL_SAMPLER_2D
		case ShaderDataType::SamplerCube:     return 0x8B60; // GL_SAMPLER_CUBE
		case ShaderDataType::Sampler2DShadow: return 0x8B62; // GL_SAMPLER_2D_SHADOW
		default:                              return 0x1406; // GL_FLOAT
	}
}

std::vector<IRHIShader::ShaderUniformDesc> MTLShader::GetActiveUniformDescs() const {
	std::vector<ShaderUniformDesc> result;

	// Use the cached reflection data from the shader compiler.
	// This provides type information which is not stored in uniformMap.
	if (!shaderCompiler)
		return result;

	auto processReflection = [&](const ShaderReflection* refl) {
		if (!refl)
			return;
		for (const auto& u : refl->uniforms) {
			// Avoid duplicates (vertex stage has priority, already in result)
			bool found = false;
			for (const auto& existing : result) {
				if (existing.name == u.name) { found = true; break; }
			}
			if (found)
				continue;

			ShaderUniformDesc desc;
			desc.name      = u.name;
			desc.glType    = ShaderDataTypeToGLType(u.type);
			desc.arraySize = u.arraySize;
			result.push_back(std::move(desc));
		}
	};

	if (!vertexSource.empty()) {
		const auto* vsRefl = shaderCompiler->GetCachedReflection(
			vertexDefines + "\n" + vertexSource, CompilerShaderStage::Vertex);
		processReflection(vsRefl);
	}
	if (!fragmentSource.empty()) {
		const auto* fsRefl = shaderCompiler->GetCachedReflection(
			fragmentDefines + "\n" + fragmentSource, CompilerShaderStage::Fragment);
		processReflection(fsRefl);
	}

	return result;
}

void MTLShader::RebuildBufferRanges() {
	vertexBufferRanges.clear();
	fragmentBufferRanges.clear();

	// Build per-stage buffer ranges. Metal assigns buffer indices independently
	// per stage, so vertex buffer(0) and fragment buffer(0) may contain
	// completely different data. We must upload them separately.
	auto buildRanges = [&](std::vector<BufferUploadRange>& ranges, bool isVertex) {
		std::unordered_map<int, std::pair<size_t, size_t>> rangeMap; // bufIdx → (minOffset, maxEnd)

		for (const auto& [name, info] : uniformMap) {
			int bufIdx = isVertex ? info.vsBufferIndex : info.fsBufferIndex;
			if (bufIdx < 0)
				continue;

			size_t end = info.offset + info.size;
			auto it = rangeMap.find(bufIdx);
			if (it == rangeMap.end()) {
				rangeMap[bufIdx] = {info.offset, end};
			} else {
				it->second.first = std::min(it->second.first, info.offset);
				it->second.second = std::max(it->second.second, end);
			}
		}

		for (const auto& [bufIdx, range] : rangeMap) {
			size_t byteSize = (range.second - range.first + 15) & ~size_t(15);
			if (byteSize == 0) byteSize = 16;
			ranges.push_back({bufIdx, range.first, byteSize});
		}
	};

	buildRanges(vertexBufferRanges, true);
	buildRanges(fragmentBufferRanges, false);

	LOG("[MTLShader] %s: VS=%zu FS=%zu buffer upload ranges",
	    shaderName.c_str(), vertexBufferRanges.size(), fragmentBufferRanges.size());
}

void MTLShader::BindUniforms(id<MTLRenderCommandEncoder> encoder) {
	if (uniformData.empty() || !encoder)
		return;

	// Metal's setVertexBytes/setFragmentBytes limit is 4096 bytes.
	// For larger uniform blocks, allocate a temporary buffer from the device.
	static constexpr size_t kMaxInlineBytes = 4096;

	// Upload vertex-stage uniform buffers
	for (const auto& range : vertexBufferRanges) {
		if (range.byteOffset + range.byteSize > uniformData.size())
			continue;
		const void* src = uniformData.data() + range.byteOffset;
		if (range.byteSize <= kMaxInlineBytes) {
			[encoder setVertexBytes:src
			                 length:range.byteSize
			                atIndex:range.metalBufferIndex];
		} else {
			id<MTLBuffer> buf = [encoder.device newBufferWithBytes:src
			                                               length:range.byteSize
			                                              options:MTLResourceStorageModeShared];
			[encoder setVertexBuffer:buf offset:0 atIndex:range.metalBufferIndex];
		}
	}

	// Upload fragment-stage uniform buffers
	for (const auto& range : fragmentBufferRanges) {
		if (range.byteOffset + range.byteSize > uniformData.size())
			continue;
		const void* src = uniformData.data() + range.byteOffset;
		if (range.byteSize <= kMaxInlineBytes) {
			[encoder setFragmentBytes:src
			                   length:range.byteSize
			                  atIndex:range.metalBufferIndex];
		} else {
			id<MTLBuffer> buf = [encoder.device newBufferWithBytes:src
			                                               length:range.byteSize
			                                              options:MTLResourceStorageModeShared];
			[encoder setFragmentBuffer:buf offset:0 atIndex:range.metalBufferIndex];
		}
	}
}

} // namespace RHI
