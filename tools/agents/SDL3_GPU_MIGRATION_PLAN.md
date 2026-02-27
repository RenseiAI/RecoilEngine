# SDL3 GPU Migration Plan: Replacing Custom RHI with SDL3 GPU API

**Date:** 2026-02-27
**Status:** Proposal / Research Complete
**Branch:** `arm64-metal-port` (current) -> `sdl3-gpu-migration` (proposed)

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Why SDL3 GPU](#2-why-sdl3-gpu)
3. [Current State Assessment](#3-current-state-assessment)
4. [SDL3 GPU API Overview](#4-sdl3-gpu-api-overview)
5. [Interface Mapping: RHI -> SDL3 GPU](#5-interface-mapping-rhi---sdl3-gpu)
6. [Breaking Changes & Paradigm Shifts](#6-breaking-changes--paradigm-shifts)
7. [Migration Strategy](#7-migration-strategy)
8. [Parallel Agent Architecture](#8-parallel-agent-architecture)
9. [Tier Breakdown & Work Packages](#9-tier-breakdown--work-packages)
10. [Risk Analysis](#10-risk-analysis)
11. [Timeline & Resource Model](#11-timeline--resource-model)
12. [Open Questions](#12-open-questions)

---

## 1. Executive Summary

Replace RecoilEngine's custom RHI (Render Hardware Interface) with SDL3's built-in GPU API. This eliminates maintaining two backend implementations (OpenGL + Metal, ~29 files, ~300 methods) and gains automatic Vulkan/D3D12 support. The engine already depends on SDL2 for windowing/input, so SDL2->SDL3 migration is a natural prerequisite.

**Key insight from community feedback (vblanco, Supaku):**
- SDL3 GPU is "fairly similar to OGL" — eases paradigm transition
- The implementation is "pretty simple so forking things is not much of a big deal"
- The auto-sync model (resource cycling) is "a lot cleaner than reconstructing pipeline state from scattered state calls"

**What we gain:**
- Multi-backend support (Metal + Vulkan + D3D12) with zero custom backend code
- Cleaner state model (immutable PSOs, explicit render passes, auto-sync)
- Smaller maintenance surface (~190 virtual methods -> thin SDL3 GPU wrapper)
- Linux Vulkan support (currently absent)
- Windows D3D12 support (future-proof)
- Console pathways (FNA ecosystem proven on Switch)

**What we lose:**
- Direct OpenGL support (SDL3 GPU has no GL backend)
- Fine-grained GL state control (some advanced GL features)
- Legacy hardware support (requires Vulkan 1.0, Metal 10.14+, D3D12 FL11_0)

---

## 2. Why SDL3 GPU

### 2.1 Engine Already Depends on SDL

RecoilEngine uses SDL2 for:
- Window creation (`SDL_CreateWindow`, `SDL_WINDOW_OPENGL`/`SDL_WINDOW_METAL`)
- Input handling (keyboard, mouse, joystick/gamepad)
- Event loop (`SDL_PollEvent`)
- Timer/clock (`SDL_GetTicks`)
- Audio device management
- Thread primitives (some usage)

SDL2->SDL3 migration is required regardless for long-term support (SDL2 is in maintenance mode). Adopting SDL3 GPU at the same time amortizes the migration cost.

### 2.2 Eliminates Two Backend Implementations

Current maintenance burden:
```
OpenGL backend:  14 files, 165+ methods (GLDevice, GLContext, GLBuffer, GLTexture, GLFramebuffer, GLPipeline, GLShader...)
Metal backend:   15 files, 140+ methods (MTLDevice, MTLContext, MTLBuffer, MTLTexture, MTLFramebuffer, MTLPipeline, MTLShader...)
RHI interfaces:   8 files, 190 virtual methods
Shader compiler:   2 files (GLSL->SPIR-V->MSL pipeline)
Headless stubs:    1 file, 421+ GL function stubs
```

With SDL3 GPU, this collapses to:
```
SDL3 GPU wrapper: ~5-8 files implementing thin adapter over SDL3 GPU API
Shader pipeline:   Reuse existing GLSL->SPIR-V, add SDL_shadercross for runtime MSL/DXIL
```

### 2.3 Resource Cycling Solves Our Hardest Metal Bugs

Our Metal backend has extensive workarounds for state reconstruction:
- `dynamicDepthTest/Write/Func` + `dynamicBlend` dirty-flag tracking (Phase 23.1)
- Pipeline lifetime issues where ScopedPipeline destructors bind nullptr (Phase 22)
- Mid-pass Clear() wipe fix (Phase 32)
- Triple-buffered in-flight frame management

SDL3 GPU handles all of this automatically:
- Immutable PSOs (no dynamic state reconstruction)
- Render pass load/store ops (no mid-pass clear ambiguity)
- Automatic resource cycling (no manual triple-buffering)
- Internal reference counting (no lifetime management)

### 2.4 Proven in Production

SDL3 GPU is built on FNA3D (Ethan Lee / MojoShader fame). FNA games ship on:
- PC (Windows/Linux/macOS) via Vulkan/D3D12/Metal
- Nintendo Switch
- Xbox (GDK)

---

## 3. Current State Assessment

### 3.1 RHI Interface Surface (What Must Be Replaced)

**190 virtual methods across 7 interfaces:**

| Interface | Methods | Domain |
|-----------|---------|--------|
| IRHIDevice | ~55 | Creation, capabilities, limits, timers, fences, debug |
| IRHIContext | ~53 | Commands: draw, state, viewport, clear, blit, readback |
| IRHIBuffer | ~14 | VBO/IBO/UBO: bind, map, upload, resize |
| IRHITexture | ~22 | Bind, upload, sampling, mipmaps, swizzle |
| IRHIFramebuffer | ~10 | Attach, detach, validate, bind |
| IRHIShader | ~20 | Compile, link, bind, uniform setters |
| IRHIPipeline | ~2 | Bind, get descriptor |

**Plus utilities:**
- ShaderCompiler (GLSL->SPIR-V->MSL, 7 methods)
- MatrixStack (FFP replacement, 16 methods)
- 4 RAII scoped helpers (ScopedPipeline, ScopedBlendState, etc.)
- RHIFactory (9 methods: init, kill, get device, backend selection)

### 3.2 RHI Usage Across Codebase

**89+ files** reference RHI interfaces. Key hotspots:

| File/Subsystem | Usage Pattern | Complexity |
|----------------|---------------|------------|
| `RenderBuffers.h` | TypedRenderBuffer dual-path (GL vs RHI), 12 template instantiations | HIGH |
| `BumpWater.cpp` | 6+ GetContext() calls, 3 FBOs, 5 textures, 3+ pipeline states | HIGH |
| `LuaVAOImpl.cpp` | All draw variants, full resource binding | HIGH |
| `LuaOpenGL.cpp` | 464 GL calls (guarded), extensive state management | HIGH |
| `LuaTextures/Shaders/FBOs/VBOs.cpp` | Full Lua resource API | MEDIUM |
| `Patch.cpp` (ROAM terrain) | DrawIndexed, vertex layout, buffer binding | MEDIUM |
| `GrassDrawer.cpp` | DrawIndexed, texture creation, FBO | MEDIUM |
| `GroundDecalHandler.cpp` | DrawInstanced, instance buffers | MEDIUM |
| `SMFReadMap.cpp` | BeginRenderPass, shader compilation, texture wrapping | MEDIUM |
| `SkyBox.cpp` | Draw, cubemap textures | LOW |
| `MiniMap.cpp` | SetViewport, state management | LOW |
| Other drawers | Individual draw/state calls | LOW |

### 3.3 Shader Pipeline

Current: GLSL 120/130 -> glslang -> SPIR-V -> SPIRV-Cross -> MSL 2.1

**41 GLSL shaders** pre-translated to MSL equivalents. Shader compilation happens at runtime via `ShaderCompiler.cpp`.

SDL3 GPU compatible: GLSL->SPIR-V (existing) feeds directly into SDL3 GPU's Vulkan backend. SDL_shadercross handles SPIR-V->MSL for Metal and SPIR-V->DXIL for D3D12.

### 3.4 GL Calls Still Direct (Not Through RHI)

**~2,857 effective GL calls remain** across 117 compiled files:
- ~718 in GL backend files (DO NOT MIGRATE — they get deleted)
- ~673 in Lua API (guarded, many are no-ops on non-GL)
- ~539 in deprecated water (already excluded from build)
- ~297 in game code (need migration to SDL3 GPU wrapper or removal)

---

## 4. SDL3 GPU API Overview

### 4.1 Architecture

Pure C API. Driver-based architecture with backends:

| Backend | Platform | Requirements |
|---------|----------|-------------|
| Vulkan | Windows, Linux, Android | Vulkan 1.0 + extensions |
| D3D12 | Windows 10+, Xbox | Feature Level 11_0 |
| Metal | macOS 10.14+, iOS 13+ | Native Metal |

**No OpenGL backend.** This is deliberate — the API targets modern explicit GPU paradigms.

### 4.2 Core Concepts

**Command Buffers:** Explicit. Acquire -> encode passes -> submit. No global state machine.

**Render Passes:** Explicit begin/end with load/store ops per attachment. Up to 4 color targets + 1 depth-stencil. State resets between passes.

**Immutable PSOs:** All render state (blend, depth, stencil, rasterizer, vertex layout, shaders, primitive type) baked at creation. Only viewport/scissor/blend constants/stencil ref are dynamic.

**Resource Cycling:** Automatic ring-buffering. When `cycle=true`, resources that are GPU-in-flight automatically rotate to a fresh copy. Eliminates manual synchronization.

**Transfer Buffers:** CPU-visible staging memory. All CPU->GPU data goes through: map transfer buffer -> memcpy -> upload in copy pass.

**Shaders:** Pre-compiled binaries (SPIR-V for Vulkan, MSL for Metal, DXBC/DXIL for D3D12). No runtime GLSL compilation. SDL_shadercross provides cross-compilation.

### 4.3 Binding Limits

Per shader stage:
- 16 texture samplers
- 8 storage textures
- 8 storage buffers
- 4 uniform buffers (push data, std140 layout)
- 16 vertex buffer slots (global)

### 4.4 Primitive Types

`TRIANGLELIST`, `TRIANGLESTRIP`, `LINELIST`, `LINESTRIP`, `POINTLIST` only.

No `TRIANGLE_FAN`, `LINE_LOOP`, `QUADS` — must convert to indexed triangle lists (we already do this in `RBPrimConvert`).

---

## 5. Interface Mapping: RHI -> SDL3 GPU

### 5.1 Device & Factory

| Current RHI | SDL3 GPU Equivalent |
|-------------|---------------------|
| `RHI::InitDevice()` | `SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV \| SDL_GPU_SHADERFORMAT_MSL, true, NULL)` |
| `RHI::KillDevice()` | `SDL_DestroyGPUDevice(device)` |
| `RHI::GetDevice()` | Store `SDL_GPUDevice*` globally |
| `SetupWindowIntegration()` | `SDL_ClaimWindowForGPUDevice(device, window)` |
| `GetBackend()` | `SDL_GetGPUDeviceDriver(device)` (returns "vulkan", "metal", "direct3d12") |
| `GetMaxTextureSize()` etc. | Not directly queryable — use SDL GPU documented limits |

### 5.2 Command Submission (Context -> Command Buffer)

| Current RHI | SDL3 GPU Equivalent |
|-------------|---------------------|
| `ctx->BeginFrame()` | `SDL_AcquireGPUCommandBuffer(device)` + `SDL_WaitAndAcquireGPUSwapchainTexture()` |
| `ctx->EndFrame()` | `SDL_SubmitGPUCommandBuffer(cmd)` (auto-presents swapchain) |
| `ctx->BeginRenderPass(fbo, desc)` | `SDL_BeginGPURenderPass(cmd, colorTargets, numColors, depthTarget)` |
| `ctx->EndRenderPass()` | `SDL_EndGPURenderPass(pass)` |
| `ctx->BeginDefaultRenderPass()` | `SDL_BeginGPURenderPass(cmd, &swapchainTarget, 1, &depthTarget)` |
| `ctx->Flush()` | Implicit on submit |
| `ctx->Finish()` | `SDL_WaitForGPUIdle(device)` |

### 5.3 Draw Calls

| Current RHI | SDL3 GPU Equivalent |
|-------------|---------------------|
| `ctx->Draw(prim, count, first)` | `SDL_DrawGPUPrimitives(pass, count, 1, first, 0)` |
| `ctx->DrawIndexed(prim, count, first, offset)` | `SDL_DrawGPUIndexedPrimitives(pass, count, 1, first, offset, 0)` |
| `ctx->DrawInstanced(prim, count, instances, first, firstInst)` | `SDL_DrawGPUPrimitives(pass, count, instances, first, firstInst)` |
| `ctx->DrawIndexedInstanced(...)` | `SDL_DrawGPUIndexedPrimitives(pass, count, instances, first, offset, firstInst)` |
| `ctx->DrawIndirect(prim, buf, offset, drawCount, stride)` | `SDL_DrawGPUPrimitivesIndirect(pass, buf, offset, drawCount)` |
| `ctx->DrawIndexedIndirect(...)` | `SDL_DrawGPUIndexedPrimitivesIndirect(pass, buf, offset, drawCount)` |

### 5.4 Pipeline State

| Current RHI | SDL3 GPU Equivalent |
|-------------|---------------------|
| `RHI::PipelineDesc` | `SDL_GPUGraphicsPipelineCreateInfo` |
| `BlendState` | `SDL_GPUColorTargetBlendState` (per-target) |
| `DepthStencilState` | `SDL_GPUDepthStencilState` |
| `RasterizerState` | `SDL_GPURasterizerState` |
| `device->CreatePipeline(desc)` | `SDL_CreateGPUGraphicsPipeline(device, &createInfo)` |
| `ctx->BindPipeline(pipeline)` | `SDL_BindGPUGraphicsPipeline(pass, pipeline)` |
| `ScopedPipeline` | RAII wrapper around SDL_GPUGraphicsPipeline bind/unbind |

### 5.5 Resources

| Current RHI | SDL3 GPU Equivalent |
|-------------|---------------------|
| `device->CreateBuffer(type, usage, size, data)` | `SDL_CreateGPUBuffer(device, &createInfo)` + copy pass for initial data |
| `buffer->Upload(data, offset, size)` | Map transfer buffer -> memcpy -> `SDL_UploadToGPUBuffer()` in copy pass |
| `buffer->Map(offset, size)` | `SDL_MapGPUTransferBuffer()` (transfer buffers only; GPU buffers not directly mappable) |
| `device->CreateTexture(...)` | `SDL_CreateGPUTexture(device, &createInfo)` |
| `texture->Upload(level, x, y, w, h, data)` | Transfer buffer -> `SDL_UploadToGPUTexture()` in copy pass |
| `texture->GenerateMipmaps()` | `SDL_GenerateMipmapsForGPUTexture(cmd, texture)` (outside passes) |
| `device->CreateFramebuffer()` | No explicit FBO — render targets specified per `SDL_BeginGPURenderPass()` |
| `ctx->BlitFramebuffer(...)` | `SDL_BlitGPUTexture(cmd, &src, &dst)` (outside passes) |
| `ctx->ReadPixels(...)` | Transfer buffer + `SDL_DownloadFromGPUTexture()` in copy pass |

### 5.6 Shaders

| Current RHI | SDL3 GPU Equivalent |
|-------------|---------------------|
| `device->CreateShader(name)` | `SDL_CreateGPUShader(device, &createInfo)` per stage |
| `shader->AttachStageFromSource(stage, glsl, defines)` | GLSL -> SPIR-V (existing) -> `SDL_CreateGPUShader(SPIRV)` or use shadercross |
| `shader->Link()` | No linking — shaders are per-stage in pipeline creation |
| `shader->SetUniform*(name, value)` | `SDL_PushGPUVertexUniformData(cmd, slot, &data, size)` (std140 blocks) |
| `shader->Bind()` | Implicit — shaders are part of bound pipeline |

### 5.7 State Management

| Current RHI | SDL3 GPU Equivalent |
|-------------|---------------------|
| `ctx->SetViewport(viewport)` | `SDL_SetGPUViewport(pass, &viewport)` |
| `ctx->SetScissor(rect)` | `SDL_SetGPUScissor(pass, &rect)` |
| `ctx->SetDepthTestEnabled(bool)` | Baked into pipeline `DepthStencilState` |
| `ctx->SetBlendEnabled(bool)` | Baked into pipeline `ColorTargetBlendState` |
| `ctx->SetCullFace(mode)` | Baked into pipeline `RasterizerState` |
| `ctx->Clear(color, depth, stencil)` | Load ops on `SDL_BeginGPURenderPass()` |
| `ctx->SetBlendColor(r,g,b,a)` | `SDL_SetGPUBlendConstants(pass, blendConstants)` |
| `ctx->SetStencilFunc(func,ref,mask)` | `SDL_SetGPUStencilReference(pass, ref)` (func in PSO) |
| All other dynamic state | **Must be baked into PSO variants** |

### 5.8 Features That Don't Map Cleanly

| Current RHI Feature | SDL3 GPU Status | Resolution |
|---------------------|-----------------|------------|
| `WrapExistingTexture(glId)` | No GL interop | Create new SDL GPU textures; no wrapping |
| `CreateTextureFromExisting(glId)` | No GL interop | Create new SDL GPU textures |
| `buffer->MapWithFlags(Persistent, Coherent)` | Transfer buffers only | Restructure upload patterns |
| `SetClipPlaneEquation()` | No clip planes | Shader-based clip distances (already done for Metal) |
| `SetClipDistanceEnabled()` | No API | Shader-based (already done for Metal) |
| `SetLogicOp()` | Not in SDL3 GPU | Remove or emulate in shader |
| `SetPolygonMode(Line/Point)` | Wireframe mode | SDL3 GPU supports fill_mode in rasterizer |
| `SetLineWidth(width)` | Not in SDL3 GPU (Vulkan deprecated it) | Use geometry shader or screen-space quads |
| `SetPointSize(size)` | Not in SDL3 GPU | Use `gl_PointSize` in shader (POINTLIST) |
| Timer queries | Not in SDL3 GPU API | Use SDL_GetPerformanceCounter or backend-specific |
| Fence sync | `SDL_WaitForGPUIdle()` + `SDL_QueryGPUFence()` | Simpler model |
| Debug callbacks | Not in SDL3 GPU | Use backend-specific validation layers |
| 8 MRT color targets | SDL3 GPU max = 4 | May need workaround for deferred rendering |
| `SetFramebufferSRGBEnabled()` | Texture format + swapchain config | Handled at creation |
| `SetSeamlessCubeMapsEnabled()` | Always on in modern APIs | No action needed |
| `TexSwizzle()` | Not in SDL3 GPU | Shader-based swizzle |
| `SetLodBias()` | Sampler creation time | Bake into `SDL_GPUSampler` |

---

## 6. Breaking Changes & Paradigm Shifts

### 6.1 No Global Mutable State

**Current:** `ctx->SetDepthTestEnabled(true)` persists until changed. Our Metal backend emulates this with dirty flags.

**SDL3 GPU:** All state baked into PSO. Changing depth test = binding different pipeline.

**Impact:** Need a **pipeline cache** keyed on full state descriptor. Every combination of (shader, blend, depth, rasterizer, vertex layout, primitive type, render target format) produces a unique PSO. In practice, game engines create hundreds of PSOs at load time.

**Estimated work:** Create `SDL3PipelineCache` that hashes `PipelineDesc` + shader + target format and caches `SDL_GPUGraphicsPipeline*` objects. All dynamic state calls (`SetDepthTestEnabled`, `SetBlendFunc`, etc.) become deferred state that triggers PSO lookup/creation at draw time.

### 6.2 Explicit Data Upload (No Direct Buffer Mapping)

**Current:** `buffer->Map()` returns CPU pointer to GPU memory. `buffer->Upload()` does `glBufferSubData`.

**SDL3 GPU:** GPU buffers are not CPU-mappable. Must use transfer buffers:
1. `SDL_MapGPUTransferBuffer()` -> get CPU pointer
2. Write data
3. `SDL_UnmapGPUTransferBuffer()`
4. Begin copy pass -> `SDL_UploadToGPUBuffer()` -> end copy pass

**Impact:** `TypedRenderBuffer` (the engine's primary dynamic geometry system) currently maps VBO directly. Need to restructure to use transfer buffers + copy passes.

### 6.3 No Separate Framebuffer Objects

**Current:** `device->CreateFramebuffer()` returns an FBO with attach/detach methods.

**SDL3 GPU:** Render targets are specified inline at `SDL_BeginGPURenderPass()`. No persistent FBO objects.

**Impact:** Replace `IRHIFramebuffer` with a descriptor struct. `BeginRenderPass` takes texture references directly.

### 6.4 Per-Stage Shaders (No Linked Programs)

**Current:** `IRHIShader` represents a linked program (vertex + fragment stages linked together). Uniforms set by name via `SetUniform*()`.

**SDL3 GPU:** Each stage (`SDL_GPUShader`) is independent. They're combined in the pipeline creation info. Uniforms pushed per-draw via `SDL_PushGPU*UniformData()` to slot indices, not by name.

**Impact:** Need uniform block layout management. All shader uniforms must be in std140 uniform blocks. Build a mapping from uniform names to block offsets.

### 6.5 Clear Operations Are Load Ops

**Current:** `ctx->ClearColor(r,g,b,a); ctx->Clear(true, true, false);` can happen mid-pass.

**SDL3 GPU:** Clear is a load op specified at `SDL_BeginGPURenderPass()`. Mid-pass clearing requires ending and restarting the render pass.

**Impact:** Audit all `Clear()` calls. Most are at pass boundaries already. The Phase 32 fix (mid-pass Clear wipe) becomes structurally impossible — which is a good thing.

### 6.6 Uniform Blocks Required (No Individual Uniforms)

**Current GLSL:** `uniform float time; uniform mat4 mvp;`

**SDL3 GPU SPIR-V:** All uniforms must be in uniform blocks:
```glsl
layout(std140, binding = 0) uniform PerDraw {
    mat4 mvp;
    float time;
    // ... pad to 16-byte alignment
};
```

**Impact:** All 41 GLSL shaders need uniform block wrapping. The existing GLSL->SPIR-V pipeline already handles this partially (glslang requires it for Vulkan target).

---

## 7. Migration Strategy

### 7.1 Two-Phase Approach

**Phase A: SDL2 -> SDL3 Migration (Windowing/Input/Audio)**
- Update all SDL includes (`SDL2/SDL.h` -> `SDL3/SDL.h`)
- Update event handling (promoted events, nanosecond timestamps)
- Update input (float mouse coords, text input changes, gamepad renames)
- Update window management (`SDL_GL_GetDrawableSize` -> `SDL_GetWindowSizeInPixels`)
- Update audio (new stream-based model)
- Run SDL rename scripts: `rename_symbols.py`, `rename_headers.py`, `rename_macros.py`
- **Gate: Engine builds and runs on SDL3 with existing GL rendering**

**Phase B: RHI -> SDL3 GPU Backend**
- Implement SDL3 GPU backend behind existing RHI interfaces (new backend alongside GL/Metal)
- Migrate RHI consumers one subsystem at a time
- Validate each subsystem renders correctly
- Remove OpenGL and Metal backends once SDL3 GPU is complete
- **Gate: Engine renders identically on SDL3 GPU (Metal) as on current Metal backend**

### 7.2 Adapter Pattern

Rather than rewrite all 89+ RHI consumer files, implement SDL3 GPU as a **new RHI backend**:

```
rts/Rendering/RHI/
├── RHITypes.h          # Keep (minor enum adjustments)
├── RHIDevice.h         # Keep interface
├── RHIContext.h        # Keep interface
├── RHIFactory.h/cpp    # Add SDL3GPU backend option
├── OpenGL/             # Keep initially, remove later
├── Metal/              # Keep initially, remove later
└── SDL3GPU/            # NEW: SDL3 GPU backend
    ├── SDL3Device.cpp
    ├── SDL3Context.cpp
    ├── SDL3Buffer.cpp
    ├── SDL3Texture.cpp
    ├── SDL3Framebuffer.cpp  # Thin wrapper (descriptors only)
    ├── SDL3Pipeline.cpp     # Pipeline cache
    ├── SDL3Shader.cpp
    └── SDL3PipelineCache.cpp
```

This means:
1. All existing RHI consumers work unchanged
2. Test SDL3 GPU backend against GL/Metal backends (visual parity)
3. Remove GL/Metal backends when SDL3 GPU is proven
4. Then optionally simplify RHI interfaces to match SDL3 GPU more directly

### 7.3 Shader Migration Path

```
Current:  GLSL 120/130 -> glslang -> SPIR-V -> SPIRV-Cross -> MSL
SDL3 GPU: GLSL 120/130 -> glslang -> SPIR-V -> SDL_shadercross -> MSL/DXIL (or ship SPIR-V for Vulkan)
```

- Keep GLSL source shaders
- Restructure uniforms into std140 blocks (if not already)
- Use SDL_shadercross at runtime for Metal/D3D12, or pre-compile offline
- SPIR-V ships directly for Vulkan backend

---

## 8. Parallel Agent Architecture

### 8.1 Design Principles (Proven from Metal Port)

From 32 phases of Metal port experience:

1. **Tier-based dependency DAG** — agents within a tier run in parallel; tiers run sequentially
2. **File ownership per agent** — no two agents modify the same file within a tier
3. **Build verification gates** — each tier must pass build before next tier starts
4. **Cross-cutting change protocol** — changes outside ownership documented in `changes-needed.md`
5. **Atomic commits** — one logical change per commit for bisectability
6. **Agent verification** — always verify agent output with `git diff --stat` / `git log`

### 8.2 Agent Types for SDL3 Migration

#### Infrastructure Agents (Tier 0-1)
| Agent | Role | Files Owned |
|-------|------|-------------|
| `sdl3-build-system` | CMake: find SDL3, link SDL3::SDL3, remove SDL2 | `CMakeLists.txt`, `rts/builds/*/CMakeLists.txt` |
| `sdl3-platform` | SDL2->SDL3 API migration (windowing, input, events) | `SpringApp.cpp`, `KeyInput.cpp`, `MouseInput.cpp`, event handlers |
| `sdl3-audio` | SDL2->SDL3 audio migration | `Sound/`, `OpenAL/` files |
| `sdl3-gpu-deps` | Build SDL_shadercross, integrate into build system | `cmake/`, shader build scripts |

#### Backend Implementation Agents (Tier 2)
| Agent | Role | Files Owned |
|-------|------|-------------|
| `sdl3-device` | Implement `IRHIDevice` over `SDL_GPUDevice` | `RHI/SDL3GPU/SDL3Device.cpp` |
| `sdl3-context` | Implement `IRHIContext` over command buffers + passes | `RHI/SDL3GPU/SDL3Context.cpp` |
| `sdl3-buffer` | Implement `IRHIBuffer` with transfer buffer pattern | `RHI/SDL3GPU/SDL3Buffer.cpp` |
| `sdl3-texture` | Implement `IRHITexture` over `SDL_GPUTexture` | `RHI/SDL3GPU/SDL3Texture.cpp` |
| `sdl3-pipeline` | Implement `IRHIPipeline` + pipeline cache | `RHI/SDL3GPU/SDL3Pipeline.cpp`, `SDL3PipelineCache.cpp` |
| `sdl3-shader` | Implement `IRHIShader` with SDL_shadercross | `RHI/SDL3GPU/SDL3Shader.cpp` |
| `sdl3-framebuffer` | Implement `IRHIFramebuffer` as descriptor wrapper | `RHI/SDL3GPU/SDL3Framebuffer.cpp` |
| `sdl3-factory` | Wire SDL3GPU backend into RHIFactory | `RHI/RHIFactory.cpp` |

#### Shader Migration Agents (Tier 3, parallel)
| Agent | Role | Files Owned |
|-------|------|-------------|
| `shader-uniforms-core` | Convert core shader uniforms to std140 blocks | First 10 shader files |
| `shader-uniforms-terrain` | Convert terrain shader uniforms | Terrain shader files |
| `shader-uniforms-effects` | Convert effects shader uniforms | Effects shader files |
| `shader-uniforms-lua` | Convert Lua shader uniforms | Lua shader files |
| `shader-uniforms-ui` | Convert UI/font shader uniforms | UI shader files |
| `shader-descriptor-sets` | Adjust SPIR-V descriptor set/binding layout for SDL3 GPU conventions | ShaderCompiler.cpp, reflection |

#### Integration & Validation Agents (Tier 4, parallel per subsystem)
| Agent | Role | Files Owned |
|-------|------|-------------|
| `validate-terrain` | Test terrain rendering on SDL3 GPU backend | Test scripts, screenshots |
| `validate-units` | Test unit/model rendering | Test scripts |
| `validate-effects` | Test particles, decals, grass | Test scripts |
| `validate-ui` | Test UI, minimap, fonts | Test scripts |
| `validate-lua` | Test Lua rendering API | Test scripts |
| `validate-water` | Test water rendering | Test scripts |

#### Cleanup Agents (Tier 5)
| Agent | Role | Files Owned |
|-------|------|-------------|
| `remove-gl-backend` | Delete OpenGL backend files | `RHI/OpenGL/*` |
| `remove-metal-backend` | Delete Metal backend files | `RHI/Metal/*` |
| `remove-glad` | Remove GLAD loader dependency | `rts/lib/glad/`, headless stubs |
| `simplify-rhi` | Simplify RHI interfaces to match SDL3 GPU more directly | `RHI/*.h` |
| `ci-pipeline` | GitHub Actions for SDL3 GPU CI | `.github/workflows/` |
| `final-auditor` | Verify no remaining GL/Metal calls outside backends | Audit scripts |

### 8.3 Estimated Agent Count

| Tier | Agents | Parallelism | Est. Effort per Agent |
|------|--------|-------------|----------------------|
| T0: SDL3 Build | 2 | 2 parallel | 2-4 hours |
| T1: SDL3 Platform | 3 | 3 parallel | 4-8 hours |
| T2: SDL3 GPU Backend | 8 | 4-8 parallel | 8-16 hours |
| T3: Shader Migration | 6 | 6 parallel | 4-8 hours |
| T4: Validation | 6 | 6 parallel | 2-4 hours |
| T5: Cleanup | 6 | 6 parallel | 2-4 hours |
| **TOTAL** | **31** | | |

---

## 9. Tier Breakdown & Work Packages

### Tier 0: SDL3 Build System (Gate: SDL3 links, existing code compiles)

**T0.1: SDL3 Dependency**
- Clone/build SDL3 for macOS ARM64
- Update CMakeLists.txt: `find_package(SDL3 REQUIRED)`, `target_link_libraries(... SDL3::SDL3)`
- Ensure both engine-headless and engine-legacy link SDL3
- Handle SDL2/SDL3 coexistence during transition (if needed)

**T0.2: SDL_shadercross Dependency**
- Build SDL_shadercross with SPIRV-Cross backend
- Add to CMake build system
- Verify: can compile SPIR-V -> MSL at runtime

### Tier 1: SDL2 -> SDL3 Platform Migration (Gate: Engine runs on SDL3 with GL rendering)

**T1.1: Windowing & Events**
- Run SDL rename scripts
- Update `SpringApp.cpp` window creation
- Update event handling (promoted events, timestamp format)
- Update `SDL_GL_GetDrawableSize` -> `SDL_GetWindowSizeInPixels`
- Fix platform macros (`__MACOSX__` -> `SDL_PLATFORM_MACOS`)

**T1.2: Input**
- Update keyboard handling (text input changes)
- Update mouse handling (float coordinates)
- Update gamepad handling (renamed APIs)

**T1.3: Audio**
- Migrate SDL_audio usage to stream-based model
- Update OpenAL integration if affected

### Tier 2: SDL3 GPU Backend Implementation (Gate: RHI consumers work on SDL3 GPU)

**T2.1: Device + Factory**
- `SDL3Device` implementing `IRHIDevice`
- Capability queries (return SDL3 GPU documented limits)
- Timer queries (SDL perf counter fallback)
- Factory registration (`Backend::SDL3GPU`)
- Window integration via `SDL_ClaimWindowForGPUDevice()`

**T2.2: Context (Command Buffer + Render Pass)**
- `SDL3Context` implementing `IRHIContext`
- Frame lifecycle: acquire cmd buffer -> record -> submit
- Render pass management: begin/end with load/store ops
- Clear operations via load ops (restructure mid-pass clears)
- Viewport/scissor (only dynamic state)
- Deferred state tracking for PSO selection

**T2.3: Buffer**
- `SDL3Buffer` implementing `IRHIBuffer`
- Vertex/Index/Uniform buffer creation
- Transfer buffer pool for uploads
- Map/Unmap via transfer buffers
- Upload via copy passes

**T2.4: Texture**
- `SDL3Texture` implementing `IRHITexture`
- Create with format mapping (RHI format -> SDL GPU format)
- Upload via transfer buffers + copy passes
- Sampling state -> `SDL_GPUSampler` objects (immutable, cached)
- Mipmap generation
- Cubemap support

**T2.5: Pipeline + Cache**
- `SDL3Pipeline` implementing `IRHIPipeline`
- `SDL3PipelineCache` keyed on (shader, blend, depth, rasterizer, vertex layout, primitive type, target format)
- Convert all deferred dynamic state calls into PSO cache lookups
- Handle pipeline creation failures gracefully

**T2.6: Shader**
- `SDL3Shader` implementing `IRHIShader`
- GLSL source -> SPIR-V (existing glslang path)
- SPIR-V -> backend format via SDL_shadercross (if needed)
- Uniform management: name->slot mapping via SPIR-V reflection
- Per-draw uniform push (`SDL_PushGPU*UniformData`)

**T2.7: Framebuffer (Descriptor Wrapper)**
- Thin wrapper storing attachment references
- `BeginRenderPass` reads attachments and constructs `SDL_GPUColorTargetInfo[]`
- `BlitFramebuffer` via `SDL_BlitGPUTexture()`

**T2.8: Factory Wiring**
- Add `Backend::SDL3GPU` to `RHIFactory`
- `--sdl3gpu-backend` command-line flag
- Backend auto-detection (Metal on macOS, Vulkan on Linux, D3D12 on Windows)

### Tier 3: Shader Uniform Block Migration (Gate: All shaders compile with std140 blocks)

**T3.1-T3.5: Uniform Block Conversion (5 parallel agents)**
Each agent converts a subset of the 41 GLSL shaders:
- Wrap individual uniforms in named uniform blocks
- Ensure std140 layout (proper alignment/padding)
- Update shader reflection to map uniform names -> block offsets
- Verify SPIR-V compilation

**T3.6: Descriptor Set Layout**
- Adjust SPIR-V descriptor set/binding assignments to match SDL3 GPU conventions
- Vertex: set=0 (textures), set=1 (uniform blocks)
- Fragment: set=2 (textures), set=3 (uniform blocks)
- Update `ShaderCompiler.cpp` / reflection pipeline

### Tier 4: Subsystem Validation (Gate: Visual parity with Metal backend)

Each agent runs the engine on SDL3 GPU backend and validates one subsystem:
- Screenshot comparison against Metal backend output
- Verify no crashes, no GPU validation errors
- Document any visual discrepancies
- Fix issues found

### Tier 5: Cleanup & Polish (Gate: Old backends removed, CI passing)

- Remove OpenGL backend (14 files)
- Remove Metal backend (15 files)
- Remove GLAD dependency
- Remove headless stubs (can use SDL3 GPU null device or minimal stub)
- Simplify RHI interfaces (remove GL-isms like `GetNativeHandle()`, `WrapExistingTexture()`)
- Set up CI with SDL3 GPU
- Final audit for remaining direct GL/Metal calls

---

## 10. Risk Analysis

### High Risk

| Risk | Impact | Mitigation |
|------|--------|------------|
| **4 MRT limit** in SDL3 GPU (current RHI supports 8) | Blocks deferred rendering, geometry buffer | Audit actual MRT usage; engine may only use 4. If >4 needed, track SDL3 issue #12818 or use multi-pass |
| **No direct buffer mapping** | TypedRenderBuffer (most-used draw path) relies on map/upload | Restructure to transfer buffer pattern; may need double-buffered transfer buffers for streaming geometry |
| **Uniform block migration** for 41 shaders | Every shader needs modification; alignment bugs are subtle | Automated conversion tool; validate each shader's SPIR-V independently |
| **SDL2->SDL3 event model changes** | Input/event code touches many files, behavioral changes | Systematic testing of all input paths; use SDL rename scripts |
| **Performance regression** from pipeline cache | PSO creation at draw time if cache misses | Pre-warm cache during loading; profile cache hit rates |

### Medium Risk

| Risk | Impact | Mitigation |
|------|--------|------------|
| **No GL backend** in SDL3 GPU | Drops support for GL-only systems (older Linux, some VMs) | Accept limitation; Vulkan has broad Linux support now |
| **SDL_shadercross runtime dependency** | Additional library to build/ship | Can pre-compile all shader formats offline instead |
| **SDL3 GPU API maturity** | API is new (shipped 2024), may have bugs | SDL3 is actively maintained; FNA ecosystem battle-tested; can fork if needed |
| **Lua rendering API** compatibility | Lua scripts use GL-flavored API | Already partially abstracted; SDL3 GPU adapter handles rest |
| **Transfer buffer overhead** | Extra copy for every buffer update | Pool transfer buffers; reuse across frames; SDL3 GPU resource cycling helps |

### Low Risk

| Risk | Impact | Mitigation |
|------|--------|------------|
| **Primitive type conversion** | No QUADS/TRIANGLE_FAN/LINE_LOOP | Already handled by `RBPrimConvert` in current RHI |
| **Clip plane removal** | SetClipPlaneEquation not in SDL3 GPU | Already shader-based for Metal backend |
| **Logic op removal** | SetLogicOp not in SDL3 GPU | Rarely used; can emulate in shader or remove |
| **Line width removal** | SetLineWidth deprecated in Vulkan | Minimal use in engine; use alternatives |

---

## 11. Timeline & Resource Model

### 24/7 Agent Operation Model

With custom agents running continuously:

| Phase | Duration (Wall Clock) | Agent-Hours | Parallelism |
|-------|----------------------|-------------|-------------|
| Tier 0: Build System | 1 day | 8-16 | 2 agents |
| Tier 1: SDL3 Platform | 2-3 days | 24-48 | 3 agents |
| Tier 2: SDL3 GPU Backend | 3-5 days | 80-160 | 8 agents |
| Tier 3: Shader Migration | 1-2 days | 24-48 | 6 agents |
| Tier 4: Validation | 2-3 days | 24-48 | 6 agents |
| Tier 5: Cleanup | 1-2 days | 16-32 | 6 agents |
| **Total** | **10-16 days** | **176-352** | **Up to 8 concurrent** |

### Agent Scaling Considerations

**Max useful parallelism:** ~8 agents per tier (limited by file ownership boundaries and dependency chains).

**Bottlenecks:**
1. Tier 2 context/pipeline agents depend on device agent
2. Shader migration can't start until backend compiles SPIR-V
3. Validation requires fully functional backend

**24/7 operation benefits:**
- No context-switching overhead between human work sessions
- Agents can run validation suites overnight
- Build-verification cycles (10-15 min each) don't block human time
- Multiple subsystems validated in parallel during Tier 4

### Resource Requirements

- macOS ARM64 build machine (for Metal backend testing)
- Linux machine (for Vulkan backend testing, optional for initial phase)
- SDL3 + SDL_shadercross built from source
- Agent infrastructure: Claude Code CLI with Task tool, git worktree support

---

## 12. Open Questions

### Architecture Decisions Needed

1. **Keep RHI abstraction or collapse to direct SDL3 GPU calls?**
   - Option A: Keep RHI interfaces, implement SDL3 GPU as third backend (safest, most incremental)
   - Option B: Replace RHI with thin SDL3 GPU wrapper (simpler long-term, bigger initial change)
   - **Recommendation:** Option A first, then simplify in Tier 5

2. **Runtime or offline shader cross-compilation?**
   - Runtime: Ship SPIR-V + SDL_shadercross (simpler build, larger runtime dependency)
   - Offline: Pre-compile SPIR-V + MSL + DXIL (no runtime dependency, more complex build)
   - **Recommendation:** Offline for shipping, runtime for development/Lua shaders

3. **SDL3 GPU version pinning?**
   - Pin to SDL 3.2.x (current stable) or track HEAD?
   - FNA typically pins; engine projects often pin
   - **Recommendation:** Pin to latest stable release, update periodically

4. **Headless mode on SDL3?**
   - SDL3 GPU requires a GPU device; no "null" backend
   - Options: Keep GLAD stubs for headless, or create SDL3 GPU null device
   - **Recommendation:** Keep minimal headless path (existing stubs) for CI

5. **What about the ~297 direct GL calls in game code?**
   - These bypass the RHI layer entirely
   - Must be migrated to RHI before GL backend removal
   - Can be done as part of Tier 2 (wrap in RHI) or Tier 5 (direct SDL3 GPU)

6. **MRT limit (4 vs 8)?**
   - Does the engine actually use >4 color targets simultaneously?
   - Need audit of `SetDrawBuffers()` calls and `GeometryBuffer` configuration
   - If yes, may need multi-pass workaround or wait for SDL3 GPU expansion

### Community Engagement

7. **Upstream contribution?**
   - Should fixes/improvements to SDL3 GPU be contributed back?
   - vblanco noted the implementation is "pretty simple so forking things is not much of a big deal"
   - Could maintain a fork with engine-specific extensions (e.g., >4 MRT)

8. **Coordination with Spring/Recoil community?**
   - Other forks may benefit from SDL3 GPU migration
   - Share migration tooling, shader conversion scripts, agent prompts

---

## Appendix A: SDL3 GPU API Quick Reference

```c
// Device
SDL_GPUDevice* device = SDL_CreateGPUDevice(formatFlags, debugMode, NULL);
SDL_ClaimWindowForGPUDevice(device, window);

// Command buffer
SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);

// Swapchain
SDL_GPUTexture* swapchain;
SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window, &swapchain, NULL, NULL);

// Render pass
SDL_GPUColorTargetInfo colorTarget = {.texture=swapchain, .load_op=CLEAR, .store_op=STORE};
SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &colorTarget, 1, NULL);

// Draw
SDL_BindGPUGraphicsPipeline(pass, pipeline);
SDL_BindGPUVertexBuffers(pass, 0, &vbBinding, 1);
SDL_PushGPUVertexUniformData(cmd, 0, &mvp, sizeof(mvp));
SDL_DrawGPUPrimitives(pass, vertexCount, 1, 0, 0);

// End
SDL_EndGPURenderPass(pass);
SDL_SubmitGPUCommandBuffer(cmd);  // auto-presents swapchain

// Cleanup
SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
SDL_DestroyGPUDevice(device);
```

## Appendix B: File Ownership Map (Draft)

```
TIER 0: Build System
  sdl3-build-system:
    MODIFY: CMakeLists.txt, rts/builds/*/CMakeLists.txt, cmake/FindSDL3.cmake
  sdl3-gpu-deps:
    CREATE: cmake/FindSDL_shadercross.cmake
    MODIFY: CMakeLists.txt (add shadercross)

TIER 1: Platform Migration
  sdl3-platform:
    MODIFY: rts/System/SpringApp.cpp, rts/System/Input/*.cpp,
            rts/Game/UI/MouseHandler.cpp, rts/Game/UI/KeyInput.cpp
  sdl3-audio:
    MODIFY: rts/System/Sound/*.cpp

TIER 2: SDL3 GPU Backend
  sdl3-device:    CREATE: rts/Rendering/RHI/SDL3GPU/SDL3Device.h/.cpp
  sdl3-context:   CREATE: rts/Rendering/RHI/SDL3GPU/SDL3Context.h/.cpp
  sdl3-buffer:    CREATE: rts/Rendering/RHI/SDL3GPU/SDL3Buffer.h/.cpp
  sdl3-texture:   CREATE: rts/Rendering/RHI/SDL3GPU/SDL3Texture.h/.cpp
  sdl3-pipeline:  CREATE: rts/Rendering/RHI/SDL3GPU/SDL3Pipeline.h/.cpp, SDL3PipelineCache.h/.cpp
  sdl3-shader:    CREATE: rts/Rendering/RHI/SDL3GPU/SDL3Shader.h/.cpp
  sdl3-framebuffer: CREATE: rts/Rendering/RHI/SDL3GPU/SDL3Framebuffer.h/.cpp
  sdl3-factory:   MODIFY: rts/Rendering/RHI/RHIFactory.h/.cpp

TIER 3: Shader Migration
  shader-uniforms-*: MODIFY: cont/shaders/*.glsl (respective subsets)
  shader-descriptor-sets: MODIFY: rts/Rendering/RHI/ShaderCompiler.cpp

TIER 4: Validation (read-only, test scripts only)
  validate-*: CREATE: tools/tests/sdl3gpu-validate-*.sh

TIER 5: Cleanup
  remove-gl-backend:    DELETE: rts/Rendering/RHI/OpenGL/*
  remove-metal-backend: DELETE: rts/Rendering/RHI/Metal/*
  remove-glad:          DELETE: rts/lib/glad/*, MODIFY: gladstub.cpp
  simplify-rhi:         MODIFY: rts/Rendering/RHI/*.h
  ci-pipeline:          CREATE: .github/workflows/sdl3gpu.yml
  final-auditor:        CREATE: tools/audit-sdl3gpu.sh
```

## Appendix C: SDL3 GPU Format Mapping

| RHI TextureFormat | SDL_GPUTextureFormat |
|-------------------|---------------------|
| RGBA8 | SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM |
| RGB8 | (No direct; use RGBA8 + swizzle) |
| RGBA16F | SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT |
| RGBA32F | SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT |
| Depth16 | SDL_GPU_TEXTUREFORMAT_D16_UNORM |
| Depth24 | (No direct; use D32_FLOAT) |
| Depth32F | SDL_GPU_TEXTUREFORMAT_D32_FLOAT |
| Depth24Stencil8 | SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT |
| Depth32FStencil8 | SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT |
| CompressedDXT1 | SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM |
| CompressedDXT5 | SDL_GPU_TEXTUREFORMAT_BC3_UNORM |
| SRGB8Alpha8 | SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB |
| R8 | SDL_GPU_TEXTUREFORMAT_R8_UNORM |
| RG8 | SDL_GPU_TEXTUREFORMAT_R8G8_UNORM |
| R16F | SDL_GPU_TEXTUREFORMAT_R16_FLOAT |
| RG16F | SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT |
| R32F | SDL_GPU_TEXTUREFORMAT_R32_FLOAT |
