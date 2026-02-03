/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#include "ShaderCompiler.h"

#ifdef __APPLE__

#include <CommonCrypto/CommonDigest.h>

// glslang headers
#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/SPIRV/GlslangToSpv.h>

// SPIRV-Cross headers
#include <spirv_cross/spirv_msl.hpp>

#include "System/Log/ILog.h"

namespace RHI {

// --- Helpers ---

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

	// ECompatibilityProfile for legacy GLSL 120/130 built-ins:
	// gl_ModelViewMatrix, gl_Fog, gl_LightSource, gl_TexCoord,
	// gl_MultiTexCoord0, gl_FragColor, gl_FragData, gl_ClipVertex, etc.
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
	spvOptions.disableOptimizer  = false;
	spvOptions.optimizeSize      = true;

	glslang::GlslangToSpv(*program.getIntermediate(glslangStage), spirv, &logger, &spvOptions);

	if (spirv.empty()) {
		lastError = std::string("SPIR-V generation failed:\n") + logger.getAllMessages();
		LOG_L(L_ERROR, "[ShaderCompiler] %s", lastError.c_str());
		return {};
	}

	return spirv;
}

std::string ShaderCompiler::TranslateSPIRVToMSL(
	const std::vector<uint32_t>& spirv,
	const MSLCompilerOptions& options)
{
	lastError.clear();

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

		return mslCompiler.compile();
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

	} catch (const spirv_cross::CompilerError& e) {
		lastError = std::string("SPIRV-Cross reflection error: ") + e.what();
		LOG_L(L_ERROR, "[ShaderCompiler] %s", lastError.c_str());
	}

	return reflection;
}

std::string ShaderCompiler::CompileGLSLToMSL(
	const std::string& source,
	CompilerShaderStage stage)
{
	const uint64_t key = HashSource(source, stage);

	auto it = cache.find(key);
	if (it != cache.end())
		return it->second.msl;

	auto spirv = CompileGLSLToSPIRV(source, stage);
	if (spirv.empty())
		return {};

	auto msl = TranslateSPIRVToMSL(spirv);
	if (msl.empty())
		return {};

	auto reflection = ReflectSPIRV(spirv);

	CachedShader cached;
	cached.spirv = std::move(spirv);
	cached.msl = msl;
	cached.reflection = std::move(reflection);
	cache[key] = std::move(cached);

	return msl;
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

std::string ShaderCompiler::TranslateSPIRVToMSL(const std::vector<uint32_t>&, const MSLCompilerOptions&) {
	lastError = "ShaderCompiler: Metal shader pipeline only available on Apple platforms";
	return {};
}

ShaderReflection ShaderCompiler::ReflectSPIRV(const std::vector<uint32_t>&) {
	lastError = "ShaderCompiler: Metal shader pipeline only available on Apple platforms";
	return {};
}

std::string ShaderCompiler::CompileGLSLToMSL(const std::string&, CompilerShaderStage) {
	lastError = "ShaderCompiler: Metal shader pipeline only available on Apple platforms";
	return {};
}

uint64_t ShaderCompiler::HashSource(const std::string&, CompilerShaderStage) const { return 0; }
void ShaderCompiler::ClearCache() { cache.clear(); }

} // namespace RHI

#endif // __APPLE__
