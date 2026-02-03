# Agent: shader-pipeline

## Purpose
Integrate glslang and SPIRV-Cross libraries and build the shader compilation
infrastructure for translating GLSL to Metal Shading Language via SPIR-V.

## Owned Files (all NEW)
- `rts/lib/glslang/` (git submodule)
- `rts/lib/SPIRV-Cross/` (git submodule)
- `rts/Rendering/RHI/ShaderCompiler.h` / `ShaderCompiler.cpp`
- `rts/Rendering/RHI/ShaderReflection.h` / `ShaderReflection.cpp`

## Context

The codebase is at: `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine`
Branch: `arm64-metal-port`

### Shader Translation Pipeline

```
GLSL source -> glslang -> SPIR-V binary -> SPIRV-Cross -> MSL source
                                        -> SPIRV-Cross -> Reflection data
```

This is the same pipeline MoltenVK uses internally.

### External Libraries

**glslang** - Khronos reference GLSL compiler
- Repo: https://github.com/KhronosGroup/glslang
- License: Apache 2.0
- Compiles GLSL to SPIR-V binary
- Supports GLSL 110 through 460, plus extensions

**SPIRV-Cross** - Khronos SPIR-V shader reflector and cross-compiler
- Repo: https://github.com/KhronosGroup/SPIRV-Cross
- License: Apache 2.0
- Converts SPIR-V to MSL (Metal Shading Language)
- Also provides shader reflection (uniform names, types, bindings)

### Engine Shader System

Current shader loading in `rts/Rendering/Shaders/Shader.cpp`:
- Loads shader files from `shaders/` directory via `CFileHandler`
- Supports inline GLSL code (if contains "void main()")
- Extracts `#version` directive
- Prepends `#define` strings for shader variants
- Hash-based caching to avoid recompilation

The ShaderCompiler needs to fit into this pipeline:
1. Receive GLSL source (after preprocessing/defines)
2. Compile to SPIR-V via glslang
3. Either: translate to MSL via SPIRV-Cross (for Metal backend)
4. Or: extract reflection data via SPIRV-Cross (for uniform binding)
5. Cache results by content hash

### GLSL -> MSL Translation Challenges

The engine's 41 GLSL shaders use these features that need special handling:

1. **gl_ClipDistance** (water shaders) -> Metal `[[clip_distance]]` attribute
2. **gl_VertexID / gl_InstanceID** -> Metal `[[vertex_id]]` / `[[instance_id]]`
3. **sampler2D / sampler2DShadow** -> Metal `texture2d<float>` + `sampler`
4. **layout(location=N)** -> Metal `[[attribute(N)]]` / `[[color(N)]]`
5. **Uniform buffers** -> Metal argument buffers / constant buffers
6. **Image load/store** (if used) -> Metal `[[raster_order_group]]`

SPIRV-Cross handles most of these automatically. The ShaderCompiler just needs
to configure SPIRV-Cross's MSL options correctly.

### Shader Reflection

The reflection system needs to extract:
- Uniform names, types, and locations (to map GLSL uniform locations to Metal buffer indices)
- Vertex attribute locations and types
- Sampler/texture binding points
- Uniform buffer block layout

This data is used by the RHI shader implementation to set up Metal argument tables.

## Tasks

### 1. Add git submodules
```bash
git submodule add https://github.com/KhronosGroup/glslang rts/lib/glslang
git submodule add https://github.com/KhronosGroup/SPIRV-Cross rts/lib/SPIRV-Cross
```

### 2. Build integration
- Add CMakeLists.txt entries in `rts/lib/CMakeLists.txt` to build both libraries
- glslang: build as static library (libglslang, libSPIRV)
- SPIRV-Cross: build as static library (libspirv-cross-core, libspirv-cross-msl)

### 3. Create ShaderCompiler
```cpp
class ShaderCompiler {
public:
    // Compile GLSL to SPIR-V
    std::vector<uint32_t> CompileGLSLToSPIRV(
        const std::string& source,
        RHIShaderStage stage,
        const std::string& entryPoint = "main");

    // Translate SPIR-V to MSL
    std::string TranslateSPIRVToMSL(
        const std::vector<uint32_t>& spirv,
        const MSLCompilerOptions& options = {});

    // Get reflection data from SPIR-V
    ShaderReflection ReflectSPIRV(const std::vector<uint32_t>& spirv);

    // Combined: GLSL -> MSL (convenience)
    std::string CompileGLSLToMSL(
        const std::string& source,
        RHIShaderStage stage);

    // Cache management
    void ClearCache();
};
```

### 4. Create ShaderReflection
```cpp
struct ShaderReflection {
    struct Uniform {
        std::string name;
        uint32_t location;
        uint32_t metalBufferIndex; // mapped by SPIRV-Cross
        // type info
    };
    struct Attribute {
        std::string name;
        uint32_t location;
        // type info
    };
    struct Sampler {
        std::string name;
        uint32_t binding;
        uint32_t metalTextureIndex;
        uint32_t metalSamplerIndex;
    };

    std::vector<Uniform> uniforms;
    std::vector<Attribute> attributes;
    std::vector<Sampler> samplers;
};
```

### 5. Test with engine shaders
Pick 2-3 engine shaders and verify the full pipeline:
- `cont/base/springcontent/shaders/GLSL/ModelVertProg.glsl`
- `cont/base/springcontent/shaders/GLSL/ModelFragProg.glsl`
- `cont/base/springcontent/shaders/GLSL/SMFVertProg.glsl`

Read each shader, run it through ShaderCompiler, verify MSL output compiles
(if on macOS, use `xcrun -sdk macosx metal` to compile MSL).

## Output
- Create branch `agent/shader-pipeline` from current HEAD
- Commit submodule additions
- Commit ShaderCompiler and ShaderReflection implementations
- Include test results for the 2-3 shader translations
