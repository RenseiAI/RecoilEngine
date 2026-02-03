# Agent: rhi-architect

## Purpose
Design and create the Rendering Hardware Interface (RHI) abstraction layer header files.
These define the API contract that both the OpenGL backend and Metal backend will implement.

## Owned Files (all NEW)
- `rts/Rendering/RHI/RHITypes.h`
- `rts/Rendering/RHI/RHIDevice.h`
- `rts/Rendering/RHI/RHIContext.h`
- `rts/Rendering/RHI/RHIBuffer.h`
- `rts/Rendering/RHI/RHITexture.h`
- `rts/Rendering/RHI/RHIShader.h`
- `rts/Rendering/RHI/RHIFramebuffer.h`
- `rts/Rendering/RHI/RHIPipeline.h`
- `rts/Rendering/RHI/RHIFactory.h`
- `rts/Rendering/RHI/CMakeLists.txt`

## Context

The codebase is at: `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine`
Branch: `arm64-metal-port`

### Design Principles

1. **Engine-specific, not general-purpose.** Only abstract what RecoilEngine actually uses.
   Do NOT create a bgfx/DiligentEngine-style universal abstraction.

2. **Follow existing patterns.** The engine already has interface-based designs:
   - `Shader::IProgramObject` -> `GLSLProgramObject` / `ARBShaderObject` / `NullShaderObject`
     (see `rts/Rendering/Shaders/Shader.h`)
   - `IStreamBuffer<T>` factory with 6 implementations
     (see `rts/Rendering/GL/StreamBuffer.h`)
   - `IWater`, `ISky`, `IGroundDecalDrawer` virtual interfaces

3. **Thin wrappers with inlining.** Performance overhead must be negligible.
   Use virtual dispatch only at the backend level, not per-draw-call.

4. **Two backends:** OpenGL (existing code wrapped) and Metal (new).

### What to Abstract

Study these existing GL classes to understand what the RHI must cover:

**Buffers:**
- `rts/Rendering/GL/VBO.h` - GL buffer objects (glGenBuffers, glBindBuffer, glBufferData, glMapBufferRange, etc.)
- `rts/Rendering/GL/StreamBuffer.h` - Streaming vertex data with 6 strategies. Lines 317-392 show triple-buffering patterns that map well to Metal.
- `rts/Rendering/GL/RenderBuffers.h` - Combines stream buffers with shaders and draw dispatch. Lines 112-165 generate GLSL at runtime.

**Textures:**
- Direct GL texture calls spread across ~33 files (glGenTextures, glBindTexture, glTexImage2D, glTexParameter, etc.)
- `rts/Rendering/GL/TexBind.h` - RAII texture binding

**Framebuffers:**
- `rts/Rendering/GL/FBO.h` - Full FBO abstraction (create, attach textures, blit, bind/unbind)
- `rts/Rendering/GL/GeometryBuffer.cpp` - G-buffer with multiple render targets

**Shaders:**
- `rts/Rendering/Shaders/Shader.h` - Already has `IProgramObject` interface with:
  - `Enable()` / `Disable()`
  - `SetUniform*()` for int/float/matrix uniforms
  - `SetUniformLocation()` / `GetUniformLocation()`
  - `Link()` / `Validate()` / `Release()`
  - Shader object management (vertex, fragment, geometry, compute)

**Pipeline state:**
- `rts/Rendering/GL/State.h` - Template-based GL state tracking
  - `StateAttribute<GLenum>` for capabilities (depth test, blending, etc.)
  - `StateParamf<>`, `StateParami<>` for GL parameters
  - `SubState` for scoped state changes with auto-restore
- Blend, depth, stencil, cull, viewport, scissor settings

**Device / Context:**
- `rts/Rendering/GlobalRendering.h` - Window/context management, capability queries
  - `haveGL4`, `supportPersistentMapping`, `supportClipSpaceControl`, etc.
  - `CreateWindowAndContext()`, `CreateSDLWindow()`, `CreateGLContext()`

### RHI Types Design

```cpp
// RHITypes.h should define:
enum class RHIBackend { OpenGL, Metal };
enum class RHIBufferType { Vertex, Index, Uniform, Storage };
enum class RHIBufferUsage { Static, Dynamic, Stream };
enum class RHITextureFormat { RGBA8, RGBA16F, RGBA32F, Depth24, Depth32F, ... };
enum class RHITextureType { Texture2D, TextureCube, Texture2DArray };
enum class RHIBlendFactor { Zero, One, SrcAlpha, OneMinusSrcAlpha, ... };
enum class RHICompareFunc { Never, Less, LessEqual, Equal, ... };
enum class RHICullMode { None, Front, Back };
enum class RHIPrimitiveType { Triangles, TriangleStrip, Lines, Points };
enum class RHIShaderStage { Vertex, Fragment, Geometry, Compute };

struct RHIVertexAttribute {
    uint32_t location;
    uint32_t offset;
    // format, size, etc.
};

struct RHIViewport { float x, y, width, height, minDepth, maxDepth; };
struct RHIScissorRect { int32_t x, y; uint32_t width, height; };
```

### Key Design Decisions

**Buffer mapping:** Metal uses `storageModeShared` with direct CPU pointers.
OpenGL uses `glMapBufferRange`. The RHI should expose:
```cpp
class IRHIBuffer {
    virtual void* Map(size_t offset, size_t size) = 0;
    virtual void Unmap() = 0;
    virtual void Upload(const void* data, size_t offset, size_t size) = 0;
};
```

**Pipeline state:** Metal uses pre-compiled immutable pipeline state objects.
OpenGL uses mutable state. The RHI should lean toward Metal's model:
```cpp
struct RHIPipelineDesc {
    RHIBlendState blend;
    RHIDepthStencilState depthStencil;
    RHICullMode cullMode;
    // vertex layout, shader program reference
};
// Backend caches compiled pipeline states by descriptor hash
```

**Render passes:** Metal requires explicit render passes with load/store actions.
OpenGL has implicit FBO bind. Abstract as:
```cpp
struct RHIRenderPassDesc {
    std::vector<RHIColorAttachment> colorAttachments;
    RHIDepthAttachment depthAttachment;
};
class IRHIContext {
    virtual void BeginRenderPass(const RHIRenderPassDesc&) = 0;
    virtual void EndRenderPass() = 0;
};
```

**Shader interface:** Extend the existing `IProgramObject` pattern:
```cpp
class IRHIShader {
    virtual void Bind() = 0;
    virtual void Unbind() = 0;
    virtual void SetUniform(const char* name, float value) = 0;
    virtual void SetUniform(const char* name, const float4x4& matrix) = 0;
    // etc.
};
```

## Tasks

1. Read the existing GL classes listed above to understand the full API surface
2. Design the RHI interfaces as pure virtual base classes
3. Create all header files with detailed documentation
4. Create a `CMakeLists.txt` for the RHI directory
5. The interfaces should compile standalone (no backend implementation yet)

## Output
- Create branch `agent/rhi-architect` from current HEAD
- Commit all RHI header files
- Include a brief design doc comment at the top of each header explaining the mapping
  from GL concepts to RHI concepts
