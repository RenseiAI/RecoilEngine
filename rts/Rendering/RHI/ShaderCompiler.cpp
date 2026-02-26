/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#include "ShaderCompiler.h"

#ifdef __APPLE__

#include <CommonCrypto/CommonDigest.h>

// glslang headers
#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <SPIRV/GlslangToSpv.h>

// SPIRV-Cross headers
#include <spirv_msl.hpp>

#include <algorithm>
#include <set>

#include "System/Log/ILog.h"

namespace RHI {

// --- Helpers ---

// Forward declaration: defined later after CompileSPIRVToMSL
static ShaderReflection ExtractReflectionFromCompiler(spirv_cross::CompilerMSL& compiler);

static EShLanguage ToGlslangStage(CompilerShaderStage stage) {
	switch (stage) {
		case CompilerShaderStage::Vertex:   return EShLangVertex;
		case CompilerShaderStage::Fragment: return EShLangFragment;
	}
	return EShLangVertex;
}

static ShaderDataType SPIRTypeToDataType(const spirv_cross::SPIRType& type) {
	if (type.basetype == spirv_cross::SPIRType::Float) {
		if (type.columns == 1) {
			switch (type.vecsize) {
				case 1: return ShaderDataType::Float;
				case 2: return ShaderDataType::Vec2;
				case 3: return ShaderDataType::Vec3;
				case 4: return ShaderDataType::Vec4;
			}
		} else if (type.columns == 3 && type.vecsize == 3) {
			return ShaderDataType::Mat3;
		} else if (type.columns == 4 && type.vecsize == 4) {
			return ShaderDataType::Mat4;
		}
	} else if (type.basetype == spirv_cross::SPIRType::Int || type.basetype == spirv_cross::SPIRType::UInt) {
		switch (type.vecsize) {
			case 1: return ShaderDataType::Int;
			case 2: return ShaderDataType::IVec2;
			case 3: return ShaderDataType::IVec3;
			case 4: return ShaderDataType::IVec4;
		}
	} else if (type.basetype == spirv_cross::SPIRType::SampledImage || type.basetype == spirv_cross::SPIRType::Image) {
		switch (type.image.dim) {
			case spv::Dim2D:
				return type.image.depth ? ShaderDataType::Sampler2DShadow : ShaderDataType::Sampler2D;
			case spv::DimCube:
				return ShaderDataType::SamplerCube;
			default:
				return ShaderDataType::Sampler2D;
		}
	}
	return ShaderDataType::Unknown;
}

// --- ShaderCompiler ---

ShaderCompiler::ShaderCompiler() {
	if (!glslangInitialized) {
		glslang::InitializeProcess();
		glslangInitialized = true;
	}
}

ShaderCompiler::~ShaderCompiler() {
	if (glslangInitialized) {
		glslang::FinalizeProcess();
		glslangInitialized = false;
	}
}

uint64_t ShaderCompiler::HashSource(const std::string& source, CompilerShaderStage stage) const {
	unsigned char hash[CC_SHA256_DIGEST_LENGTH];
	CC_SHA256_CTX ctx;
	CC_SHA256_Init(&ctx);
	CC_SHA256_Update(&ctx, source.data(), static_cast<CC_LONG>(source.size()));
	uint8_t stageByte = static_cast<uint8_t>(stage);
	CC_SHA256_Update(&ctx, &stageByte, 1);
	CC_SHA256_Final(hash, &ctx);

	uint64_t key = 0;
	for (int i = 0; i < 8; ++i)
		key |= static_cast<uint64_t>(hash[i]) << (i * 8);
	return key;
}

std::vector<uint32_t> ShaderCompiler::CompileGLSLToSPIRV(
	const std::string& source,
	CompilerShaderStage stage,
	const std::string& entryPoint)
{
	lastError.clear();

	const EShLanguage glslangStage = ToGlslangStage(stage);
	glslang::TShader shader(glslangStage);

	const char* sources[] = { source.c_str() };
	const int   lengths[] = { static_cast<int>(source.size()) };
	shader.setStringsWithLengths(sources, lengths, 1);
	shader.setEntryPoint(entryPoint.c_str());
	shader.setSourceEntryPoint(entryPoint.c_str());

	// ECompatibilityProfile allows legacy GLSL built-ins (gl_ModelViewMatrix, etc.)
	// while still generating SPIR-V. We don't use setEnvTarget here because that
	// enforces strict SPIR-V rules (uniform locations, bindings) which our shaders
	// don't have. Instead, we fix up the SPIR-V version header after generation.
	const int defaultVersion = 110;
	const EProfile profile = ECompatibilityProfile;
	const bool forwardCompatible = false;
	const TBuiltInResource* resources = GetDefaultResources();

	if (!shader.parse(resources, defaultVersion, profile, false, forwardCompatible, EShMsgDefault)) {
		lastError = std::string("glslang parse failed:\n") + shader.getInfoLog();
		LOG_L(L_ERROR, "[ShaderCompiler] %s", lastError.c_str());
		return {};
	}

	glslang::TProgram program;
	program.addShader(&shader);

	if (!program.link(EShMsgDefault)) {
		lastError = std::string("glslang link failed:\n") + program.getInfoLog();
		LOG_L(L_ERROR, "[ShaderCompiler] %s", lastError.c_str());
		return {};
	}

	std::vector<uint32_t> spirv;
	spv::SpvBuildLogger logger;
	glslang::SpvOptions spvOptions;
	spvOptions.generateDebugInfo = false;
	spvOptions.disableOptimizer  = true;
	spvOptions.optimizeSize      = false;

	glslang::GlslangToSpv(*program.getIntermediate(glslangStage), spirv, &logger, &spvOptions);

	if (spirv.empty()) {
		lastError = std::string("SPIR-V generation failed:\n") + logger.getAllMessages();
		LOG_L(L_ERROR, "[ShaderCompiler] %s", lastError.c_str());
		return {};
	}

	// Without setEnvTarget, glslang emits SPIR-V version 0 in the header.
	// SPIRV-Cross requires a valid version (1.0+). Patch it to SPIR-V 1.0.
	if (spirv.size() >= 5) {
		if (spirv[0] != 0x07230203) {
			lastError = "SPIR-V magic mismatch: 0x" + std::to_string(spirv[0]);
			LOG_L(L_ERROR, "[ShaderCompiler] %s", lastError.c_str());
			return {};
		}
		if (spirv[1] == 0) {
			spirv[1] = 0x00010000; // SPIR-V 1.0
		}
	}

	return spirv;
}

std::string ShaderCompiler::TranslateSPIRVToMSL(
	const std::vector<uint32_t>& spirv,
	const MSLCompilerOptions& options,
	const std::unordered_map<std::string, uint32_t>& attribLocations)
{
	lastError.clear();

	if (spirv.size() < 5) {
		lastError = "SPIR-V data too small (" + std::to_string(spirv.size()) + " words)";
		LOG_L(L_ERROR, "[ShaderCompiler] %s", lastError.c_str());
		return {};
	}

	LOG("[ShaderCompiler] TranslateSPIRVToMSL: %zu words, magic=0x%08x version=0x%08x",
	    spirv.size(), spirv[0], spirv[1]);

	try {
		spirv_cross::CompilerMSL mslCompiler(spirv);
		spirv_cross::CompilerMSL::Options mslOpts;

		mslOpts.platform = options.isIOS
			? spirv_cross::CompilerMSL::Options::iOS
			: spirv_cross::CompilerMSL::Options::macOS;
		mslOpts.msl_version = options.mslVersion;
		mslOpts.enable_clip_distance_user_varying = options.enableClipDistance;
		mslOpts.enable_point_size_builtin = options.enablePointSize;

		mslCompiler.set_msl_options(mslOpts);

		// Rename entry points to avoid main0 collision when vertex+fragment
		// are combined into one MTLLibrary
		auto ep = mslCompiler.get_entry_points_and_stages();
		for (auto& e : ep) {
			if (e.execution_model == spv::ExecutionModelVertex)
				mslCompiler.rename_entry_point(e.name, "vertexMain", e.execution_model);
			else if (e.execution_model == spv::ExecutionModelFragment)
				mslCompiler.rename_entry_point(e.name, "fragmentMain", e.execution_model);
		}

		// Ensure all stage inputs/outputs have SPIR-V Location decorations.
		// Metal requires:
		//   - [[attribute(N)]] on every vertex stage_in struct member
		//   - user(locnN) on every vertex-to-fragment varying
		// GLSL shaders without explicit layout(location=N) may produce
		// SPIR-V without Location decorations. Fix up here.
		//
		// Strategy:
		//   - Vertex attributes (vertex stage_inputs): use attribLocations
		//     from BindAttribLocation, or auto-assign sequentially
		//   - Varyings (vertex stage_outputs / fragment stage_inputs):
		//     sort by NAME and assign sequentially, ensuring both stages
		//     get matching locations for the same variable names
		{
			auto resources = mslCompiler.get_shader_resources();

			// Detect shader stage from entry point
			spv::ExecutionModel execModel = spv::ExecutionModelMax;
			for (auto& e : ep) {
				execModel = e.execution_model;
				break;
			}
			const bool isVertexStage = (execModel == spv::ExecutionModelVertex);

			// Helper: assign locations to a set of variables, sorted by name
			// for deterministic cross-stage matching
			auto assignLocationsSorted = [&](
				const spirv_cross::SmallVector<spirv_cross::Resource>& vars,
				const char* label)
			{
				std::set<uint32_t> usedLocs;
				for (const auto& v : vars) {
					if (mslCompiler.has_decoration(v.id, spv::DecorationLocation))
						usedLocs.insert(mslCompiler.get_decoration(v.id, spv::DecorationLocation));
				}

				// Collect unlocated, non-builtin variables and sort by name
				std::vector<std::pair<std::string, uint32_t>> unlocated;
				for (const auto& v : vars) {
					if (mslCompiler.has_decoration(v.id, spv::DecorationLocation))
						continue;
					if (mslCompiler.has_decoration(v.id, spv::DecorationBuiltIn))
						continue;
					unlocated.emplace_back(mslCompiler.get_name(v.id), v.id);
				}
				std::sort(unlocated.begin(), unlocated.end());

				uint32_t nextLoc = 0;
				for (const auto& [name, id] : unlocated) {
					while (usedLocs.count(nextLoc)) nextLoc++;
					mslCompiler.set_decoration(id, spv::DecorationLocation, nextLoc);
					usedLocs.insert(nextLoc);
					LOG("[ShaderCompiler] Auto-assigned %s location %u to '%s'",
					    label, nextLoc, name.c_str());
					nextLoc++;
				}
			};

			// --- Vertex stage inputs (vertex attributes) ---
			// Use attribLocations for named bindings, auto-assign for others
			if (isVertexStage) {
				std::set<uint32_t> usedLocs;
				for (const auto& input : resources.stage_inputs) {
					if (mslCompiler.has_decoration(input.id, spv::DecorationLocation))
						usedLocs.insert(mslCompiler.get_decoration(input.id, spv::DecorationLocation));
				}

				uint32_t nextAutoLoc = 0;
				for (const auto& input : resources.stage_inputs) {
					if (mslCompiler.has_decoration(input.id, spv::DecorationLocation))
						continue;
					if (mslCompiler.has_decoration(input.id, spv::DecorationBuiltIn))
						continue;

					const std::string inputName = mslCompiler.get_name(input.id);
					uint32_t location;

					auto it = attribLocations.find(inputName);
					if (it != attribLocations.end()) {
						location = it->second;
					} else {
						while (usedLocs.count(nextAutoLoc)) nextAutoLoc++;
						location = nextAutoLoc++;
					}

					mslCompiler.set_decoration(input.id, spv::DecorationLocation, location);
					usedLocs.insert(location);
					LOG("[ShaderCompiler] Auto-assigned attrib location %u to '%s'",
					    location, inputName.c_str());
				}

				// --- Vertex stage outputs (varyings to fragment) ---
				// Sort by name for deterministic matching with fragment inputs
				assignLocationsSorted(resources.stage_outputs, "varying output");
			} else {
				// --- Fragment stage inputs (varyings from vertex) ---
				// Sort by name for deterministic matching with vertex outputs
				assignLocationsSorted(resources.stage_inputs, "varying input");
			}
		}

		std::string msl = mslCompiler.compile();

		// Post-process MSL: fix address-space mismatch in helper functions.
		//
		// SPIRV-Cross emits standalone uniforms (SPIR-V UniformConstant) as
		// `constant T& name [[buffer(N)]]` in the entry point. When these are
		// passed to helper functions, the helper's parameter is declared as
		// `thread const T& name` (Function storage class → thread). MSL's strict
		// address spaces reject passing `constant T&` where `thread const T&`
		// is expected.
		//
		// Fix: change `thread const T& name` to `T name` (pass by value) in
		// helper function signatures. This is safe because:
		//   1. The parameter is const (read-only)
		//   2. By-value passing copies from constant → thread implicitly
		//   3. The functions are always_inline so the copy is optimized away
		{
			const std::string needle = "thread const ";
			size_t pos = 0;
			while ((pos = msl.find(needle, pos)) != std::string::npos) {
				// Find the & that makes this a reference parameter
				// Pattern: "thread const TYPE& NAME" → "TYPE NAME"
				size_t ampPos = msl.find('&', pos + needle.size());
				size_t commaPos = msl.find(',', pos + needle.size());
				size_t parenPos = msl.find(')', pos + needle.size());
				size_t nextLine = msl.find('\n', pos);

				// The & must come before the next comma, closing paren, or newline
				size_t limit = std::min({commaPos, parenPos, nextLine});
				if (ampPos != std::string::npos && ampPos < limit) {
					// Remove "thread const " prefix
					msl.erase(pos, needle.size());
					// Recalculate ampPos after the erase
					ampPos -= needle.size();
					// Remove the "& " (reference + space)
					if (ampPos < msl.size() && msl[ampPos] == '&') {
						msl.erase(ampPos, 1);
						// Also remove a trailing space if present
						if (ampPos < msl.size() && msl[ampPos] == ' ')
							; // keep the space (it's between type and name)
					}
					// Don't advance pos — check same position again in case of
					// multiple "thread const" params on the same line
				} else {
					pos += needle.size();
				}
			}
		}

		return msl;
	} catch (const spirv_cross::CompilerError& e) {
		lastError = std::string("SPIRV-Cross MSL error: ") + e.what();
		LOG_L(L_ERROR, "[ShaderCompiler] %s", lastError.c_str());
		return {};
	}
}

ShaderReflection ShaderCompiler::ReflectSPIRV(const std::vector<uint32_t>& spirv) {
	lastError.clear();
	ShaderReflection reflection;

	try {
		spirv_cross::CompilerMSL compiler(spirv);
		spirv_cross::CompilerMSL::Options mslOpts;
		mslOpts.platform = spirv_cross::CompilerMSL::Options::macOS;
		mslOpts.msl_version = 20100;
		mslOpts.enable_clip_distance_user_varying = true;
		compiler.set_msl_options(mslOpts);

		// Force compilation so MSL resource assignments are computed
		compiler.compile();

		reflection = ExtractReflectionFromCompiler(compiler);
	} catch (const spirv_cross::CompilerError& e) {
		lastError = std::string("SPIRV-Cross reflection error: ") + e.what();
		LOG_L(L_ERROR, "[ShaderCompiler] %s", lastError.c_str());
	}

	return reflection;
}

// Extract reflection data from an already-compiled CompilerMSL instance.
// MUST be called after mslCompiler.compile() so auto-assigned resource
// bindings are available. Using the same compiler that generated the MSL
// guarantees buffer indices match the [[buffer(N)]] in the output.
static ShaderReflection ExtractReflectionFromCompiler(spirv_cross::CompilerMSL& compiler) {
	ShaderReflection reflection;
	auto resources = compiler.get_shader_resources();

	// Uniform buffers & push constants -> uniforms
	auto extractUniforms = [&](const spirv_cross::SmallVector<spirv_cross::Resource>& res) {
		for (const auto& r : res) {
			const auto& type = compiler.get_type(r.base_type_id);
			if (type.basetype == spirv_cross::SPIRType::Struct) {
				for (uint32_t i = 0; i < type.member_types.size(); ++i) {
					ReflectedUniform u;
					u.name = compiler.get_member_name(r.base_type_id, i);
					if (u.name.empty())
						u.name = compiler.get_name(r.id) + "." + std::to_string(i);
					u.location = static_cast<int>(compiler.get_member_decoration(r.base_type_id, i, spv::DecorationLocation));
					u.offset = static_cast<int>(compiler.get_member_decoration(r.base_type_id, i, spv::DecorationOffset));
					u.metalBufferIndex = static_cast<int>(compiler.get_automatic_msl_resource_binding(r.id));
					const auto& memberType = compiler.get_type(type.member_types[i]);
					u.type = SPIRTypeToDataType(memberType);
					if (!memberType.array.empty())
						u.arraySize = static_cast<int>(memberType.array[0]);
					reflection.uniforms.push_back(std::move(u));
				}
			} else {
				ReflectedUniform u;
				u.name = compiler.get_name(r.id);
				u.location = static_cast<int>(compiler.get_decoration(r.id, spv::DecorationLocation));
				u.metalBufferIndex = static_cast<int>(compiler.get_automatic_msl_resource_binding(r.id));
				u.type = SPIRTypeToDataType(type);
				reflection.uniforms.push_back(std::move(u));
			}
		}
	};

	extractUniforms(resources.uniform_buffers);
	extractUniforms(resources.push_constant_buffers);

	// Standalone GLSL uniforms (SPIR-V UniformConstant, non-opaque types)
	for (const auto& r : resources.gl_plain_uniforms) {
		ReflectedUniform u;
		u.name = compiler.get_name(r.id);
		u.location = static_cast<int>(compiler.get_decoration(r.id, spv::DecorationLocation));
		u.metalBufferIndex = static_cast<int>(compiler.get_automatic_msl_resource_binding(r.id));
		u.offset = -1;  // standalone: no struct offset, uses own buffer
		const auto& type = compiler.get_type(r.type_id);
		u.type = SPIRTypeToDataType(type);
		if (!type.array.empty())
			u.arraySize = static_cast<int>(type.array[0]);
		reflection.uniforms.push_back(std::move(u));
	}

	// Stage inputs -> attributes
	for (const auto& r : resources.stage_inputs) {
		ReflectedAttribute a;
		a.name = compiler.get_name(r.id);
		a.location = static_cast<int>(compiler.get_decoration(r.id, spv::DecorationLocation));
		a.type = SPIRTypeToDataType(compiler.get_type(r.type_id));
		reflection.attributes.push_back(std::move(a));
	}

	// Sampled images -> samplers
	for (const auto& r : resources.sampled_images) {
		ReflectedSampler s;
		s.name = compiler.get_name(r.id);
		s.binding = static_cast<int>(compiler.get_decoration(r.id, spv::DecorationBinding));
		s.metalTextureIndex = static_cast<int>(compiler.get_automatic_msl_resource_binding(r.id));
		s.metalSamplerIndex = static_cast<int>(compiler.get_automatic_msl_resource_binding_secondary(r.id));
		reflection.samplers.push_back(std::move(s));
	}

	// Separate images
	for (const auto& r : resources.separate_images) {
		ReflectedSampler s;
		s.name = compiler.get_name(r.id);
		s.binding = static_cast<int>(compiler.get_decoration(r.id, spv::DecorationBinding));
		s.metalTextureIndex = static_cast<int>(compiler.get_automatic_msl_resource_binding(r.id));
		s.metalSamplerIndex = -1;
		reflection.samplers.push_back(std::move(s));
	}

	return reflection;
}

std::string ShaderCompiler::CompileGLSLToMSL(
	const std::string& source,
	CompilerShaderStage stage,
	const std::unordered_map<std::string, uint32_t>& attribLocations)
{
	const uint64_t key = HashSource(source, stage);

	auto it = cache.find(key);
	if (it != cache.end())
		return it->second.msl;

	auto spirv = CompileGLSLToSPIRV(source, stage);
	if (spirv.empty())
		return {};

	// Use a SINGLE compiler instance for both MSL generation and reflection.
	// Previously, TranslateSPIRVToMSL and ReflectSPIRV created separate
	// CompilerMSL instances that could produce different auto-assigned
	// [[buffer(N)]] indices, causing uniform data to go to wrong slots.
	lastError.clear();

	if (spirv.size() < 5) {
		lastError = "SPIR-V data too small (" + std::to_string(spirv.size()) + " words)";
		LOG_L(L_ERROR, "[ShaderCompiler] %s", lastError.c_str());
		return {};
	}

	try {
		spirv_cross::CompilerMSL mslCompiler(spirv);
		spirv_cross::CompilerMSL::Options mslOpts;

		MSLCompilerOptions options; // defaults
		mslOpts.platform = options.isIOS
			? spirv_cross::CompilerMSL::Options::iOS
			: spirv_cross::CompilerMSL::Options::macOS;
		mslOpts.msl_version = options.mslVersion;
		mslOpts.enable_clip_distance_user_varying = options.enableClipDistance;
		mslOpts.enable_point_size_builtin = options.enablePointSize;

		mslCompiler.set_msl_options(mslOpts);

		// Rename entry points to avoid main0 collision when vertex+fragment
		// are compiled into separate MTLLibraries
		auto ep = mslCompiler.get_entry_points_and_stages();
		for (auto& e : ep) {
			if (e.execution_model == spv::ExecutionModelVertex)
				mslCompiler.rename_entry_point(e.name, "vertexMain", e.execution_model);
			else if (e.execution_model == spv::ExecutionModelFragment)
				mslCompiler.rename_entry_point(e.name, "fragmentMain", e.execution_model);
		}

		// Fix up SPIR-V Location decorations for Metal stage_in/varyings
		{
			auto resources = mslCompiler.get_shader_resources();

			spv::ExecutionModel execModel = spv::ExecutionModelMax;
			for (auto& e : ep) {
				execModel = e.execution_model;
				break;
			}
			const bool isVertexStage = (execModel == spv::ExecutionModelVertex);

			auto assignLocationsSorted = [&](
				const spirv_cross::SmallVector<spirv_cross::Resource>& vars,
				const char* label)
			{
				std::set<uint32_t> usedLocs;
				for (const auto& v : vars) {
					if (mslCompiler.has_decoration(v.id, spv::DecorationLocation))
						usedLocs.insert(mslCompiler.get_decoration(v.id, spv::DecorationLocation));
				}

				std::vector<std::pair<std::string, uint32_t>> unlocated;
				for (const auto& v : vars) {
					if (mslCompiler.has_decoration(v.id, spv::DecorationLocation))
						continue;
					if (mslCompiler.has_decoration(v.id, spv::DecorationBuiltIn))
						continue;
					unlocated.emplace_back(mslCompiler.get_name(v.id), v.id);
				}
				std::sort(unlocated.begin(), unlocated.end());

				uint32_t nextLoc = 0;
				for (const auto& [name, id] : unlocated) {
					while (usedLocs.count(nextLoc)) nextLoc++;
					mslCompiler.set_decoration(id, spv::DecorationLocation, nextLoc);
					usedLocs.insert(nextLoc);
					LOG("[ShaderCompiler] Auto-assigned %s location %u to '%s'",
					    label, nextLoc, name.c_str());
					nextLoc++;
				}
			};

			if (isVertexStage) {
				std::set<uint32_t> usedLocs;
				for (const auto& input : resources.stage_inputs) {
					if (mslCompiler.has_decoration(input.id, spv::DecorationLocation))
						usedLocs.insert(mslCompiler.get_decoration(input.id, spv::DecorationLocation));
				}

				uint32_t nextAutoLoc = 0;
				for (const auto& input : resources.stage_inputs) {
					if (mslCompiler.has_decoration(input.id, spv::DecorationLocation))
						continue;
					if (mslCompiler.has_decoration(input.id, spv::DecorationBuiltIn))
						continue;

					const std::string inputName = mslCompiler.get_name(input.id);
					uint32_t location;

					auto locIt = attribLocations.find(inputName);
					if (locIt != attribLocations.end()) {
						location = locIt->second;
					} else {
						while (usedLocs.count(nextAutoLoc)) nextAutoLoc++;
						location = nextAutoLoc++;
					}

					mslCompiler.set_decoration(input.id, spv::DecorationLocation, location);
					usedLocs.insert(location);
					LOG("[ShaderCompiler] Auto-assigned attrib location %u to '%s'",
					    location, inputName.c_str());
				}

				assignLocationsSorted(resources.stage_outputs, "varying output");
			} else {
				assignLocationsSorted(resources.stage_inputs, "varying input");
			}
		}

		// Generate MSL — this triggers auto-assignment of [[buffer(N)]] etc.
		std::string msl = mslCompiler.compile();

		// Post-process: fix address-space mismatch in helper functions
		{
			const std::string needle = "thread const ";
			size_t pos = 0;
			while ((pos = msl.find(needle, pos)) != std::string::npos) {
				size_t ampPos = msl.find('&', pos + needle.size());
				size_t commaPos = msl.find(',', pos + needle.size());
				size_t parenPos = msl.find(')', pos + needle.size());
				size_t nextLine = msl.find('\n', pos);

				size_t limit = std::min({commaPos, parenPos, nextLine});
				if (ampPos != std::string::npos && ampPos < limit) {
					msl.erase(pos, needle.size());
					ampPos -= needle.size();
					if (ampPos < msl.size() && msl[ampPos] == '&') {
						msl.erase(ampPos, 1);
						if (ampPos < msl.size() && msl[ampPos] == ' ')
							; // keep the space
					}
				} else {
					pos += needle.size();
				}
			}
		}

		// Extract reflection from the SAME compiler that generated the MSL.
		// This guarantees metalBufferIndex values match the [[buffer(N)]] in the
		// generated MSL, because auto-assignment happened during compile().
		ShaderReflection reflection = ExtractReflectionFromCompiler(mslCompiler);

		CachedShader cached;
		cached.spirv = std::move(spirv);
		cached.msl = msl;
		cached.reflection = std::move(reflection);
		cache[key] = std::move(cached);

		return msl;
	} catch (const spirv_cross::CompilerError& e) {
		lastError = std::string("SPIRV-Cross MSL error: ") + e.what();
		LOG_L(L_ERROR, "[ShaderCompiler] %s", lastError.c_str());
		return {};
	}
}

const ShaderReflection* ShaderCompiler::GetCachedReflection(
	const std::string& source, CompilerShaderStage stage) const
{
	const uint64_t key = HashSource(source, stage);
	auto it = cache.find(key);
	if (it != cache.end())
		return &it->second.reflection;
	return nullptr;
}

void ShaderCompiler::ClearCache() {
	cache.clear();
}

} // namespace RHI

#else // !__APPLE__

// Stub implementation for non-Apple platforms
namespace RHI {

ShaderCompiler::ShaderCompiler() {}
ShaderCompiler::~ShaderCompiler() {}

std::vector<uint32_t> ShaderCompiler::CompileGLSLToSPIRV(const std::string&, CompilerShaderStage, const std::string&) {
	lastError = "ShaderCompiler: Metal shader pipeline only available on Apple platforms";
	return {};
}

std::string ShaderCompiler::TranslateSPIRVToMSL(const std::vector<uint32_t>&, const MSLCompilerOptions&, const std::unordered_map<std::string, uint32_t>&) {
	lastError = "ShaderCompiler: Metal shader pipeline only available on Apple platforms";
	return {};
}

ShaderReflection ShaderCompiler::ReflectSPIRV(const std::vector<uint32_t>&) {
	lastError = "ShaderCompiler: Metal shader pipeline only available on Apple platforms";
	return {};
}

std::string ShaderCompiler::CompileGLSLToMSL(const std::string&, CompilerShaderStage, const std::unordered_map<std::string, uint32_t>&) {
	lastError = "ShaderCompiler: Metal shader pipeline only available on Apple platforms";
	return {};
}

uint64_t ShaderCompiler::HashSource(const std::string&, CompilerShaderStage) const { return 0; }
const ShaderReflection* ShaderCompiler::GetCachedReflection(const std::string&, CompilerShaderStage) const { return nullptr; }
void ShaderCompiler::ClearCache() { cache.clear(); }

} // namespace RHI

#endif // __APPLE__
