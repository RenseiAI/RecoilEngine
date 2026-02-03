/**
 * ShaderTranslator - Offline GLSL to Metal shader compiler
 *
 * Translates Recoil Engine GLSL shaders to Metal Shading Language using
 * the glslang -> SPIR-V -> SPIRV-Cross pipeline.
 *
 * Usage: ShaderTranslator <input.glsl> <output.metal> <vertex|fragment> [defines...]
 */

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <filesystem>

// glslang headers
#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/SPIRV/GlslangToSpv.h>

// SPIRV-Cross headers
#include <spirv_cross/spirv_msl.hpp>

namespace fs = std::filesystem;

static bool glslangInitialized = false;

void InitGlslang() {
	if (!glslangInitialized) {
		glslang::InitializeProcess();
		glslangInitialized = true;
	}
}

void FinalizeGlslang() {
	if (glslangInitialized) {
		glslang::FinalizeProcess();
		glslangInitialized = false;
	}
}

std::string ReadFile(const std::string& path) {
	std::ifstream file(path);
	if (!file.is_open()) {
		std::cerr << "Error: Cannot open file: " << path << std::endl;
		return "";
	}
	std::stringstream buffer;
	buffer << file.rdbuf();
	return buffer.str();
}

bool WriteFile(const std::string& path, const std::string& content) {
	std::ofstream file(path);
	if (!file.is_open()) {
		std::cerr << "Error: Cannot write file: " << path << std::endl;
		return false;
	}
	file << content;
	return true;
}

std::vector<uint32_t> CompileGLSLToSPIRV(
	const std::string& source,
	EShLanguage stage,
	std::string& errorLog)
{
	glslang::TShader shader(stage);

	const char* sources[] = { source.c_str() };
	const int lengths[] = { static_cast<int>(source.size()) };
	shader.setStringsWithLengths(sources, lengths, 1);
	shader.setEntryPoint("main");
	shader.setSourceEntryPoint("main");

	// ECompatibilityProfile for legacy GLSL built-ins
	const int defaultVersion = 110;
	const EProfile profile = ECompatibilityProfile;
	const bool forwardCompatible = false;
	const TBuiltInResource* resources = GetDefaultResources();

	if (!shader.parse(resources, defaultVersion, profile, false, forwardCompatible, EShMsgDefault)) {
		errorLog = std::string("glslang parse failed:\n") + shader.getInfoLog();
		return {};
	}

	glslang::TProgram program;
	program.addShader(&shader);

	if (!program.link(EShMsgDefault)) {
		errorLog = std::string("glslang link failed:\n") + program.getInfoLog();
		return {};
	}

	std::vector<uint32_t> spirv;
	spv::SpvBuildLogger logger;
	glslang::SpvOptions spvOptions;
	spvOptions.generateDebugInfo = false;
	spvOptions.disableOptimizer = false;
	spvOptions.optimizeSize = true;

	glslang::GlslangToSpv(*program.getIntermediate(stage), spirv, &logger, &spvOptions);

	if (spirv.empty()) {
		errorLog = std::string("SPIR-V generation failed:\n") + logger.getAllMessages();
		return {};
	}

	return spirv;
}

std::string TranslateSPIRVToMSL(
	const std::vector<uint32_t>& spirv,
	std::string& errorLog)
{
	try {
		spirv_cross::CompilerMSL mslCompiler(spirv);
		spirv_cross::CompilerMSL::Options mslOpts;

		mslOpts.platform = spirv_cross::CompilerMSL::Options::macOS;
		mslOpts.msl_version = 20100;  // MSL 2.1
		mslOpts.enable_clip_distance_user_varying = true;
		mslOpts.enable_point_size_builtin = false;

		mslCompiler.set_msl_options(mslOpts);

		return mslCompiler.compile();
	} catch (const spirv_cross::CompilerError& e) {
		errorLog = std::string("SPIRV-Cross MSL error: ") + e.what();
		return "";
	}
}

// Add preprocessor definitions to the shader source
std::string PrependDefines(const std::string& source, const std::vector<std::string>& defines) {
	std::string result;

	// Extract version pragma if present
	std::string versionLine;
	std::string restOfSource = source;

	size_t versionPos = source.find("#version");
	if (versionPos != std::string::npos) {
		size_t endOfLine = source.find('\n', versionPos);
		if (endOfLine != std::string::npos) {
			versionLine = source.substr(versionPos, endOfLine - versionPos + 1);
			restOfSource = source.substr(0, versionPos) + source.substr(endOfLine + 1);
		}
	}

	// If no version, add default
	if (versionLine.empty()) {
		versionLine = "#version 130\n";
	}

	result = versionLine;

	// Add defines
	for (const auto& def : defines) {
		result += "#define " + def + "\n";
	}

	result += restOfSource;
	return result;
}

void PrintUsage(const char* programName) {
	std::cout << "Usage: " << programName << " <input.glsl> <output.metal> <vertex|fragment> [defines...]\n";
	std::cout << "\nOptions:\n";
	std::cout << "  input.glsl     - Input GLSL shader file\n";
	std::cout << "  output.metal   - Output Metal shader file\n";
	std::cout << "  vertex|fragment - Shader stage\n";
	std::cout << "  defines...     - Optional preprocessor defines (e.g., USE_SHADOWS=1)\n";
	std::cout << "\nExample:\n";
	std::cout << "  " << programName << " ModelVertProg.glsl ModelVertProg.metal vertex USE_SHADOWS=1\n";
}

int main(int argc, char* argv[]) {
	if (argc < 4) {
		PrintUsage(argv[0]);
		return 1;
	}

	std::string inputPath = argv[1];
	std::string outputPath = argv[2];
	std::string stageStr = argv[3];

	EShLanguage stage;
	if (stageStr == "vertex" || stageStr == "vert" || stageStr == "vs") {
		stage = EShLangVertex;
	} else if (stageStr == "fragment" || stageStr == "frag" || stageStr == "fs") {
		stage = EShLangFragment;
	} else {
		std::cerr << "Error: Invalid shader stage: " << stageStr << std::endl;
		std::cerr << "Valid stages: vertex, fragment\n";
		return 1;
	}

	// Collect defines
	std::vector<std::string> defines;
	for (int i = 4; i < argc; ++i) {
		defines.push_back(argv[i]);
	}

	// Read input
	std::string source = ReadFile(inputPath);
	if (source.empty()) {
		return 1;
	}

	// Prepend defines
	source = PrependDefines(source, defines);

	// Initialize glslang
	InitGlslang();

	std::string errorLog;

	// Compile to SPIR-V
	std::cout << "Compiling GLSL to SPIR-V..." << std::endl;
	auto spirv = CompileGLSLToSPIRV(source, stage, errorLog);
	if (spirv.empty()) {
		std::cerr << "Error compiling GLSL:\n" << errorLog << std::endl;
		FinalizeGlslang();
		return 1;
	}

	// Translate to MSL
	std::cout << "Translating SPIR-V to MSL..." << std::endl;
	std::string msl = TranslateSPIRVToMSL(spirv, errorLog);
	if (msl.empty()) {
		std::cerr << "Error translating to MSL:\n" << errorLog << std::endl;
		FinalizeGlslang();
		return 1;
	}

	// Write output
	if (!WriteFile(outputPath, msl)) {
		FinalizeGlslang();
		return 1;
	}

	std::cout << "Successfully translated " << inputPath << " -> " << outputPath << std::endl;

	FinalizeGlslang();
	return 0;
}
