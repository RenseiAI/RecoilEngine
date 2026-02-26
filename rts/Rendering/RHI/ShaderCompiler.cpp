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
#include <regex>
#include <set>

#include "System/Log/ILog.h"

namespace RHI {

// --- Helpers ---

// Post-process MSL to remap [[buffer(N)]] indices that exceed Metal's limit of 30.
// SPIRV-Cross auto-assigns buffer indices without knowledge of Metal's constraint;
// large uniform arrays (e.g., gl_TextureMatrix[32]) or many standalone uniforms can
// push indices past 30. This rewrites the MSL text to compact indices into 0-29,
// reserving index 30 for the vertex buffer.
static void RemapOutOfBoundsBufferIndices(std::string& msl) {
	// Find all [[buffer(N)]] occurrences and collect used indices
	std::set<int> usedIndices;
	std::regex bufferRe(R"(\[\[buffer\((\d+)\)\]\])");
	{
		auto begin = std::sregex_iterator(msl.begin(), msl.end(), bufferRe);
		auto end = std::sregex_iterator();
		for (auto it = begin; it != end; ++it) {
			usedIndices.insert(std::stoi((*it)[1].str()));
		}
	}

	// Check if any index exceeds 30 (Metal's max buffer argument table index)
	bool needsRemap = false;
	for (int idx : usedIndices) {
		if (idx > 29) { // reserve 30 for vertex buffer
			needsRemap = true;
			break;
		}
	}
	if (!needsRemap) return;

	// Build remap table: assign out-of-bounds indices to free slots in 0-29
	// Collect in-bounds indices that we must preserve
	std::set<int> takenSlots;
	for (int idx : usedIndices) {
		if (idx <= 29) {
			takenSlots.insert(idx);
		}
	}

	std::map<int, int> remap;
	int nextFree = 0;
	for (int idx : usedIndices) {
		if (idx > 29) {
			// Find next slot in 0-29 not taken by an existing in-bounds index or previous remap
			while (nextFree <= 29 && takenSlots.count(nextFree))
				nextFree++;
			if (nextFree > 29) {
				LOG_L(L_WARNING, "[ShaderCompiler] Cannot remap buffer(%d): no free slots in 0-29", idx);
				return; // give up if we can't fit
			}
			remap[idx] = nextFree;
			takenSlots.insert(nextFree);
			LOG("[ShaderCompiler] Remapped [[buffer(%d)]] -> [[buffer(%d)]] (Metal limit)", idx, nextFree);
			nextFree++;
		}
	}

	// Apply remapping — replace from highest index first to avoid cascading
	for (auto it = remap.rbegin(); it != remap.rend(); ++it) {
		if (it->first == it->second) continue;
		std::string oldStr = "[[buffer(" + std::to_string(it->first) + ")]]";
		std::string newStr = "[[buffer(" + std::to_string(it->second) + ")]]";
		size_t pos = 0;
		while ((pos = msl.find(oldStr, pos)) != std::string::npos) {
			msl.replace(pos, oldStr.size(), newStr);
			pos += newStr.size();
		}
	}
}

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

	// Pre-process: join backslash-continuation lines. GLSL 1.10 (which we use
	// as the default parse version for ECompatibilityProfile) does not support
	// line continuation in preprocessor directives. Some shaders (e.g., decals)
	// declare #version 130+ and use backslash continuations in #define macros.
	// Joining them before parsing avoids "line continuation not supported" errors.
	std::string preprocessed;
	preprocessed.reserve(source.size());
	for (size_t i = 0; i < source.size(); ++i) {
		if (source[i] == '\\' && i + 1 < source.size() && source[i + 1] == '\n') {
			++i; // skip the backslash and newline
			continue;
		}
		if (source[i] == '\\' && i + 2 < source.size() && source[i + 1] == '\r' && source[i + 2] == '\n') {
			i += 2; // skip backslash, CR, LF
			continue;
		}
		preprocessed += source[i];
	}

	const EShLanguage glslangStage = ToGlslangStage(stage);
	glslang::TShader shader(glslangStage);

	const char* sources[] = { preprocessed.c_str() };
	const int   lengths[] = { static_cast<int>(preprocessed.size()) };
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
			// Compute how many locations a variable consumes.
			// Arrays consume one location per element; matrices consume one per column.
			auto getLocationCount = [&](uint32_t typeId) -> uint32_t {
				const spirv_cross::SPIRType& type = mslCompiler.get_type(typeId);
				uint32_t count = 1;
				// Arrays: multiply by array size (check variable type, not base type)
				if (!type.array.empty())
					count *= type.array[0];
				// Matrices: each column is a separate location
				if (type.columns > 1)
					count *= type.columns;
				return count;
			};

			auto assignLocationsSorted = [&](
				const spirv_cross::SmallVector<spirv_cross::Resource>& vars,
				const char* label)
			{
				std::set<uint32_t> usedLocs;
				for (const auto& v : vars) {
					if (mslCompiler.has_decoration(v.id, spv::DecorationLocation)) {
						uint32_t baseLoc = mslCompiler.get_decoration(v.id, spv::DecorationLocation);
						uint32_t numLocs = getLocationCount(v.type_id);
						for (uint32_t i = 0; i < numLocs; ++i)
							usedLocs.insert(baseLoc + i);
					}
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
					// Use type_id from the resource for location counting
					uint32_t typeId = 0;
					for (const auto& v : vars) {
						if (v.id == id) { typeId = v.type_id; break; }
					}
					uint32_t numLocs = getLocationCount(typeId);
					while (usedLocs.count(nextLoc)) nextLoc++;
					mslCompiler.set_decoration(id, spv::DecorationLocation, nextLoc);
					for (uint32_t i = 0; i < numLocs; ++i)
						usedLocs.insert(nextLoc + i);
					LOG("[ShaderCompiler] Auto-assigned %s location %u (+%u) to '%s'",
					    label, nextLoc, numLocs, name.c_str());
					nextLoc += numLocs;
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

				// --- Fragment stage outputs (MRT color attachments) ---
				// Metal requires [[color(N)]] attributes on fragment output struct members.
				// If GLSL uses unsized arrays (e.g., `out vec4 fragColor[5]`) instead of
				// explicit `layout(location=N)`, the Location decoration may be missing.
				// Assign sequential locations so SPIRV-Cross emits [[color(N)]].
				assignLocationsSorted(resources.stage_outputs, "color attachment");
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
				// Must skip commas inside template angle brackets <...>
				size_t searchStart = pos + needle.size();
				size_t nextLine = msl.find('\n', pos);
				if (nextLine == std::string::npos) nextLine = msl.size();

				size_t ampPos = std::string::npos;
				size_t endPos = std::string::npos; // first , or ) outside templates
				int angleDepth = 0;
				for (size_t i = searchStart; i < nextLine; ++i) {
					char c = msl[i];
					if (c == '<') { angleDepth++; continue; }
					if (c == '>') { angleDepth--; continue; }
					if (angleDepth > 0) continue;
					if (c == '&' && ampPos == std::string::npos) { ampPos = i; continue; }
					if (c == ',' || c == ')') { endPos = i; break; }
				}

				if (ampPos != std::string::npos && (endPos == std::string::npos || ampPos < endPos)) {
					// Remove "thread const " prefix and "&" reference → pass by value.
					msl.erase(pos, needle.size());
					ampPos -= needle.size();
					if (ampPos < msl.size() && msl[ampPos] == '&') {
						msl.erase(ampPos, 1);
						if (ampPos < msl.size() && msl[ampPos] == ' ')
							; // keep the space (it's between type and name)
					}
					// Don't advance pos — check same position again
				} else {
					pos += needle.size();
				}
			}
		}

		// Remap any [[buffer(N)]] indices > 29 into free slots (Metal limit)
		RemapOutOfBoundsBufferIndices(msl);

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

			// Compute how many locations a variable consumes.
			auto getLocationCount = [&](uint32_t typeId) -> uint32_t {
				const spirv_cross::SPIRType& type = mslCompiler.get_type(typeId);
				uint32_t count = 1;
				if (!type.array.empty())
					count *= type.array[0];
				if (type.columns > 1)
					count *= type.columns;
				return count;
			};

			auto assignLocationsSorted = [&](
				const spirv_cross::SmallVector<spirv_cross::Resource>& vars,
				const char* label)
			{
				std::set<uint32_t> usedLocs;
				for (const auto& v : vars) {
					if (mslCompiler.has_decoration(v.id, spv::DecorationLocation)) {
						uint32_t baseLoc = mslCompiler.get_decoration(v.id, spv::DecorationLocation);
						uint32_t numLocs = getLocationCount(v.type_id);
						for (uint32_t i = 0; i < numLocs; ++i)
							usedLocs.insert(baseLoc + i);
					}
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
					uint32_t typeId = 0;
					for (const auto& v : vars) {
						if (v.id == id) { typeId = v.type_id; break; }
					}
					uint32_t numLocs = getLocationCount(typeId);
					while (usedLocs.count(nextLoc)) nextLoc++;
					mslCompiler.set_decoration(id, spv::DecorationLocation, nextLoc);
					for (uint32_t i = 0; i < numLocs; ++i)
						usedLocs.insert(nextLoc + i);
					LOG("[ShaderCompiler] Auto-assigned %s location %u (+%u) to '%s'",
					    label, nextLoc, numLocs, name.c_str());
					nextLoc += numLocs;
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

				// --- Fragment stage outputs (MRT color attachments) ---
				// Metal requires [[color(N)]] attributes on fragment output struct members.
				// If GLSL uses unsized arrays (e.g., `out vec4 fragColor[5]`) instead of
				// explicit `layout(location=N)`, the Location decoration may be missing.
				// Assign sequential locations so SPIRV-Cross emits [[color(N)]].
				assignLocationsSorted(resources.stage_outputs, "color attachment");
			}
		}

		// Fix descriptor aliasing: UBOs and SSBOs sharing the same
		// (set, binding) cause SPIRV-Cross to generate a `constant void*`
		// alias parameter with casts to both `constant T*` (for UBOs) and
		// `const device T*` (for SSBOs). Metal rejects the cross-address-space
		// cast. Fix: reassign conflicting SSBOs to unique binding numbers so
		// SPIRV-Cross emits separate parameters.
		{
			auto res = mslCompiler.get_shader_resources();

			std::set<std::pair<uint32_t, uint32_t>> uboBindings;
			for (const auto& r : res.uniform_buffers) {
				uint32_t dset = mslCompiler.has_decoration(r.id, spv::DecorationDescriptorSet)
					? mslCompiler.get_decoration(r.id, spv::DecorationDescriptorSet) : 0;
				uint32_t dbind = mslCompiler.get_decoration(r.id, spv::DecorationBinding);
				uboBindings.emplace(dset, dbind);
			}

			uint32_t nextFreeBinding = 20;
			for (const auto& r : res.storage_buffers) {
				uint32_t dset = mslCompiler.has_decoration(r.id, spv::DecorationDescriptorSet)
					? mslCompiler.get_decoration(r.id, spv::DecorationDescriptorSet) : 0;
				uint32_t dbind = mslCompiler.get_decoration(r.id, spv::DecorationBinding);
				if (uboBindings.count({dset, dbind})) {
					mslCompiler.set_decoration(r.id, spv::DecorationBinding, nextFreeBinding);
					LOG("[ShaderCompiler] Reassigned SSBO '%s' binding %u→%u (UBO alias conflict at set=%u)",
					    mslCompiler.get_name(r.id).c_str(), dbind, nextFreeBinding, dset);
					nextFreeBinding++;
				}
			}
		}

		// Generate MSL — this triggers auto-assignment of [[buffer(N)]] etc.
		std::string msl = mslCompiler.compile();

		// Post-process: fix address-space mismatch in helper functions
		{
			const std::string needle = "thread const ";
			size_t pos = 0;
			while ((pos = msl.find(needle, pos)) != std::string::npos) {
				size_t searchStart = pos + needle.size();
				size_t nextLine = msl.find('\n', pos);
				if (nextLine == std::string::npos) nextLine = msl.size();

				size_t ampPos = std::string::npos;
				size_t endPos = std::string::npos;
				int angleDepth = 0;
				for (size_t i = searchStart; i < nextLine; ++i) {
					char c = msl[i];
					if (c == '<') { angleDepth++; continue; }
					if (c == '>') { angleDepth--; continue; }
					if (angleDepth > 0) continue;
					if (c == '&' && ampPos == std::string::npos) { ampPos = i; continue; }
					if (c == ',' || c == ')') { endPos = i; break; }
				}

				if (ampPos != std::string::npos && (endPos == std::string::npos || ampPos < endPos)) {
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

		// Remap any [[buffer(N)]] indices > 29 into free slots (Metal limit)
		RemapOutOfBoundsBufferIndices(msl);

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
