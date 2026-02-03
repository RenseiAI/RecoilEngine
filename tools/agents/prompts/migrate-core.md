# Agent: migrate-core

## Purpose
Migrate the core GL infrastructure classes to work through the RHI. This is the
most critical migration because nearly every other rendering subsystem depends on
these classes.

## Owned Files
- `rts/Rendering/GL/RenderBuffers.h` (central draw dispatch)
- `rts/Rendering/GL/VBO.h` / `VBO.cpp`
- `rts/Rendering/GL/VAO.h` / `VAO.cpp`
- `rts/Rendering/GL/StreamBuffer.h` / `StreamBuffer.cpp`
- `rts/Rendering/GL/FBO.h` / `FBO.cpp`
- `rts/Rendering/GL/State.h`
- `rts/Rendering/GL/TexBind.h`
- `rts/Rendering/GL/GeometryBuffer.cpp` / `GeometryBuffer.h`

## Context

See `migrate-template.md` for general migration rules.

### Migration Order (within this agent)

These files have internal dependencies. Migrate bottom-up:

1. **VBO** -> wraps behind `RHIBuffer` (or make VBO implement RHIBuffer interface)
2. **FBO** -> wraps behind `RHIFramebuffer`
3. **VAO** -> wraps behind RHI vertex input description
4. **State** -> wraps behind `RHIPipeline`
5. **TexBind** -> wraps behind `RHIContext::BindTexture`
6. **StreamBuffer** -> add RHI-based strategy to the existing factory
7. **RenderBuffers** -> this depends on all of the above; migrate last
8. **GeometryBuffer** -> uses FBO and textures

### RenderBuffers is Special

`RenderBuffers.h` is the most complex file. It:
- Defines `TypedRenderBuffer<T>` which combines stream buffers + shaders + draw dispatch
- Generates GLSL shaders at runtime from vertex attribute metadata (lines 112-165)
- Handles both immediate and batched submission modes
- Is used by almost every rendering subsystem

Strategy: Make `RenderBuffers` use `RHIBuffer` for data and `RHIShader` for programs,
but keep the same external API so other subsystems don't need to change yet.

### StreamBuffer Factory Pattern

`StreamBuffer.h` already uses a factory pattern with `IStreamBufferImpl<T>` and
multiple strategies (BufferData, SubData, MapOrphan, MapSync, PersistentMap, PinnedAMD).

Add a new strategy: `StreamBufferRHI<T>` that delegates to `RHIBuffer`. Register it
in the factory alongside the existing ones.

## Output
- Create branch `agent/migrate-core` from current HEAD
- Commit each file migration separately
- Report any RHI interface gaps discovered
