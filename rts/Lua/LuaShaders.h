/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#ifndef LUA_SHADERS_H
#define LUA_SHADERS_H

/**
 * Lua Shader Management
 *
 * Handles shader creation from GLSL source provided by Lua scripts.
 * This is the critical file for Metal backend support.
 *
 * RHI Migration - Critical Path for Metal:
 * =========================================
 * Lua scripts provide GLSL shader source at runtime:
 *   local shader = gl.CreateShader({ vertex = "...", fragment = "..." })
 *
 * For Metal backend, CreateShader must:
 * 1. Compile GLSL -> SPIR-V via glslang (ShaderCompiler::CompileGLSLToSPIRV)
 * 2. Translate SPIR-V -> MSL via SPIRV-Cross (ShaderCompiler::TranslateSPIRVToMSL)
 * 3. Create MTLLibrary from MSL source
 * 4. Cache compiled shaders by content hash
 *
 * The ShaderCompiler class (Rendering/RHI/ShaderCompiler.h) provides this
 * pipeline. CreateShader should detect backend and use appropriate path:
 * - OpenGL: glCreateShader/glShaderSource/glCompileShader (current code)
 * - Metal: ShaderCompiler GLSL->MSL pipeline
 *
 * Uniform handling:
 * - glUniform* calls -> IRHIShader::SetUniform*()
 * - glGetUniformLocation -> IRHIShader uniform name lookup
 * - ActiveUniforms reflect into shader for both backends
 */

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

#include "Rendering/GL/myGL.h"
#include "Rendering/RHI/RHIShader.h"


struct lua_State;

class LuaShaders {
	public:
		struct Program;

		static bool PushEntries(lua_State* L);

		LuaShaders();
		~LuaShaders();

		void Clear() {
			programs.clear();
			unused.clear();
		}

		std::string errorLog;

		GLuint GetProgramName(uint32_t progIdx) const;
		const Program* GetProgram(uint32_t progIdx) const;
		      Program* GetProgram(uint32_t progIdx);
	private:
		struct Object {
			Object(GLuint _id, GLenum _type) : id(_id), type(_type) {}
			GLuint id;
			GLenum type;
		};
	public:
		struct ActiveUniform {
			GLint size = 0;
			GLenum type = 0;
		};
		struct ActiveUniformLocation {
			GLint location = -1;
		};
		struct Program {
			Program(GLuint _id) : id(_id) {}
			Program(Program&&) = default;
			Program& operator=(Program&&) = default;
			Program(const Program&) = delete;
			Program& operator=(const Program&) = delete;

			GLuint id;
			std::vector<Object> objects;
			std::unordered_map<std::string, ActiveUniform> activeUniforms;
			std::unordered_map<std::string, ActiveUniformLocation> activeUniformLocations;

			// Metal backend: RHI shader object (null on GL path)
			std::unique_ptr<RHI::IRHIShader> rhiShader;
			// Metal backend: maps synthetic location integer -> uniform name
			std::unordered_map<int, std::string> locationToName;
		};
	private:
		std::vector<Program> programs;
		std::vector<uint32_t> unused; // references slots in programs
	private:
		uint32_t AddProgram(Program&& p);
		bool RemoveProgram(uint32_t progIdx);
		GLuint GetProgramName(lua_State* L, int index) const;
		const Program* GetProgram(lua_State* L, int index) const;
		      Program* GetProgram(lua_State* L, int index);
	private:
		// helper
		static bool DeleteProgram(Program& p);
		static GLint GetUniformLocation(Program* p, const char* name);
	private:

		// the call-outs
		static int CreateShader(lua_State* L);
		static int DeleteShader(lua_State* L);
		static int UseShader(lua_State* L);
		static int ActiveShader(lua_State* L);

		static int GetActiveUniforms(lua_State* L);
		static int GetUniformLocation(lua_State* L);
		static int GetSubroutineIndex(lua_State* L);
		static int Uniform(lua_State* L);
		static int UniformInt(lua_State* L);
		static int UniformArray(lua_State* L);
		static int UniformMatrix(lua_State* L);
		static int UniformSubroutine(lua_State* L);

		static int GetEngineUniformBufferDef(lua_State* L);
		static int GetEngineModelUniformDataDef(lua_State* L);
		static int GetEngineModelUniformDataSize(lua_State* L);

		static int SetUnitBufferUniforms(lua_State* L);
		static int SetFeatureBufferUniforms(lua_State* L);

		static int SetGeometryShaderParameter(lua_State* L);
		static int SetTesselationShaderParameter(lua_State* L);

		static int GetShaderLog(lua_State* L);

	private:
		inline static Program* activeProgram = nullptr;
		inline static int activeShaderDepth = 0;
};


#endif /* LUA_SHADERS_H */
