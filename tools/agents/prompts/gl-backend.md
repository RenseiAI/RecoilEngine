# Agent: gl-backend

## Purpose
Create the OpenGL backend implementation of the RHI, wrapping existing GL code.
This is the "identity transform" -- existing rendering behavior preserved exactly,
just routed through the RHI interface.

## Owned Files (all NEW)
- `rts/Rendering/RHI/OpenGL/GLDevice.h` / `GLDevice.cpp`
- `rts/Rendering/RHI/OpenGL/GLContext.h` / `GLContext.cpp`
- `rts/Rendering/RHI/OpenGL/GLBuffer.h` / `GLBuffer.cpp`
- `rts/Rendering/RHI/OpenGL/GLTexture.h` / `GLTexture.cpp`
- `rts/Rendering/RHI/OpenGL/GLShader.h` / `GLShader.cpp`
- `rts/Rendering/RHI/OpenGL/GLFramebuffer.h` / `GLFramebuffer.cpp`
- `rts/Rendering/RHI/OpenGL/GLPipeline.h` / `GLPipeline.cpp`
- `rts/Rendering/RHI/OpenGL/CMakeLists.txt`

## Prerequisites
- The `rhi-architect` agent must have completed first (RHI interfaces must exist)
- Read the RHI headers in `rts/Rendering/RHI/` to understand the interface contracts

## Context

The codebase is at: `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine`
Branch: `arm64-metal-port`

### Existing GL Code to Wrap

Each GL backend class wraps existing engine GL code:

**GLBuffer** wraps `rts/Rendering/GL/VBO.h`:
- `VBO::Bind()` / `VBO::Unbind()` -> `GLBuffer::Bind()` / `GLBuffer::Unbind()`
- `VBO::New()` (glGenBuffers) -> `GLBuffer` constructor
- `VBO::MapBuffer()` / `VBO::UnmapBuffer()` -> `GLBuffer::Map()` / `GLBuffer::Unmap()`
- `VBO::Upload()` (glBufferData/glBufferSubData) -> `GLBuffer::Upload()`

**GLTexture** wraps raw GL texture calls:
- `glGenTextures` -> `GLTexture` constructor
- `glBindTexture` / `glActiveTexture` -> `GLTexture::Bind(unit)`
- `glTexImage2D` / `glTexStorage2D` -> `GLTexture::Allocate()`
- `glTexParameter*` -> `GLTexture::SetFilter()` / `GLTexture::SetWrap()`
- `glDeleteTextures` -> `GLTexture` destructor

**GLFramebuffer** wraps `rts/Rendering/GL/FBO.h`:
- `FBO::Init()` -> `GLFramebuffer` constructor
- `FBO::Bind()` / `FBO::Unbind()` -> `GLFramebuffer::Bind()` / `GLFramebuffer::Unbind()`
- `FBO::AttachTexture()` -> `GLFramebuffer::AttachColor()` / `GLFramebuffer::AttachDepth()`
- `FBO::Blit()` -> `GLFramebuffer::BlitTo()`
- `FBO::IsValid()` / `FBO::GetStatus()` -> `GLFramebuffer::IsComplete()`

**GLShader** wraps `rts/Rendering/Shaders/Shader.h`:
- Already has `IProgramObject` interface with `GLSLProgramObject` implementation
- `GLShader` can delegate to `GLSLProgramObject` or wrap it
- Key methods: `Enable()`, `Disable()`, `SetUniform*()`, `Link()`

**GLPipeline** wraps `rts/Rendering/GL/State.h`:
- Maps RHI pipeline state descriptors to GL state calls
- `glEnable(GL_DEPTH_TEST)` / `glDepthFunc()` -> part of pipeline binding
- `glEnable(GL_BLEND)` / `glBlendFunc()` -> part of pipeline binding
- `glEnable(GL_CULL_FACE)` / `glCullFace()` -> part of pipeline binding
- On OpenGL, "binding a pipeline" means making the corresponding glEnable/glDisable calls

**GLDevice** wraps `rts/Rendering/GlobalRendering.h`:
- Device capabilities (haveGL4, supportPersistentMapping, etc.)
- Window/context creation delegates to existing `CGlobalRendering`

**GLContext** wraps GL command submission:
- Draw calls: `glDrawElements`, `glDrawArrays`, `glDrawElementsInstanced`
- State queries: `glGetIntegerv`, `glGetError`
- Clear: `glClear`, `glClearColor`, `glClearDepth`
- Viewport/Scissor: `glViewport`, `glScissor`

### Important: Thin Wrappers

The GL backend should be as thin as possible. In many cases, methods can be one-liners
that just call the underlying GL function. Use `inline` where appropriate.

The goal is NOT to rewrite the GL code, but to put it behind the RHI interface so
that the Metal backend can provide an alternative implementation.

## Tasks

1. Read the RHI interface headers (from `rhi-architect` agent)
2. Read the existing GL classes listed above
3. Implement each GL backend class as a thin wrapper
4. Ensure the GL backend can be instantiated via `RHIFactory`
5. Create CMakeLists.txt for the OpenGL backend

## Output
- Create branch `agent/gl-backend` from current HEAD (must include rhi-architect's work)
- Commit each backend class separately
- Document any RHI interface changes needed (feed back to rhi-architect)
