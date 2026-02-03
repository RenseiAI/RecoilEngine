# Agent: migrate-shaders

## Purpose
Migrate the shader compilation and management system to work through the RHI.

## Owned Files
- `rts/Rendering/Shaders/Shader.h` / `Shader.cpp`
- `rts/Rendering/Shaders/ShaderHandler.h` / `ShaderHandler.cpp`
- `rts/Rendering/Shaders/GLSLCopyState.cpp` / `GLSLCopyState.h`

## Context

See `migrate-template.md` for general migration rules.
The codebase is at: `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine`

### Existing Architecture

`Shader.h` already has an interface-based design:
- `IShaderObject` - base shader compilation unit
- `IProgramObject` - base shader program (linked set of shader objects)
- `NullShaderObject` / `NullProgramObject` - no-op stubs
- `ARBShaderObject` - legacy ARB assembly shaders (can probably be removed)
- `GLSLShaderObject` / `GLSLProgramObject` - GLSL implementations

The `IProgramObject` interface already defines:
- `Enable()` / `Disable()` - bind/unbind
- `SetUniform*()` - uniform setting (int, float, matrix variants)
- `GetUniformLocation()` / `SetUniformLocation()`
- `Link()` / `Validate()` / `Release()`

### Migration Strategy

Since `IProgramObject` already exists as an interface, the migration is to:
1. Keep `IProgramObject` as the RHI shader interface (or make `RHIShader` extend it)
2. Keep `GLSLProgramObject` as the OpenGL backend implementation
3. Prepare for a `MetalProgramObject` that loads from pre-compiled metallib

The `ShaderHandler` singleton manages program creation and caching. It needs to
use `RHIFactory` to create the right type of program object.

### Shader loading flow
1. `ShaderHandler::CreateProgramObject()` - factory method
2. Loads GLSL from file or inline string
3. Compiles via `glCreateShader` / `glCompileShader`
4. Links via `glCreateProgram` / `glLinkProgram`
5. Caches by hash

On Metal, this becomes:
1. `ShaderHandler::CreateProgramObject()` -> creates `MetalProgramObject`
2. Loads pre-compiled metallib or compiles GLSL via ShaderCompiler
3. Creates `MTLRenderPipelineState` with the Metal functions
4. Caches by hash

## Output
- Create branch `agent/migrate-shaders`
- One commit per file
- Document how MetalProgramObject should integrate with ShaderCompiler
