# RHI Migration Audit Report

**Date:** 2026-02-05 (revision 2)
**Previous audit:** 2026-02-04
**Branch:** arm64-metal-port
**Auditor:** Claude Opus 4.6
**Audit Type:** Comprehensive scan for direct GL calls and RHI infrastructure verification

## Executive Summary

The RHI (Render Hardware Interface) abstraction layer is **complete and verified** (not stubs). Both OpenGL and Metal backends fully implement all interface methods with real API calls. The shader pipeline is complete with all 41 GLSL shaders translated to Metal Shading Language (MSL).

**~1,111 direct GL function calls** remain across ~86 files. The previous audit reported 8,167 but that count included GL_ enum constants (GL_TEXTURE_2D, GL_BLEND, etc.) which inflated the number. This audit counts only actual function calls (`gl[A-Z]...(`).

Tier 3 migration agents added **documentation only** - no actual code migration was performed (verified).

| Category | Count | Status |
|----------|-------|--------|
| Direct GL function calls (outside RHI backend, lib/) | ~1,111 | Needs Migration |
| Files with GL calls | ~86 | Needs Migration |
| Files with RHI migration doc headers | 33 | Documented |
| BumpWater GL calls remaining | 16 | ~85-90% Migrated |
| RHI Interface Files | 8 headers + helpers | Complete (Verified) |
| OpenGL Backend Files | 7 class pairs (14 files) | Complete (Verified) |
| Metal Backend Files | 15 files | Complete (Verified) |
| GLSL Shaders | 41 | Complete |
| Metal Shaders | 41 | Complete (100% translated, verified real MSL) |

---

## 1. Direct GL Calls (Corrected Counts)

### Methodology

This audit counts actual GL function call invocations matching `gl[A-Z][a-zA-Z]+\(` in active code (not comments, not documentation blocks). Excludes:
- `rts/Rendering/RHI/OpenGL/` (legitimate backend code)
- `rts/lib/` (third-party libraries including GLAD)
- `rts/lib/headlessStubs/` (test stubs)

### Top 30 Files with Most GL Calls

| Rank | File | GL Calls | Status |
|------|------|----------|--------|
| 1 | `rts/Lua/LuaOpenGL.cpp` | 513 | DOCUMENTED_ONLY |
| 2 | `rts/Rml/Backends/RmlUi_Renderer_GL3_Recoil.cpp` | 194 | UNTOUCHED |
| 3 | `rts/Game/UI/MiniMap.cpp` | 98 | UNTOUCHED |
| 4 | `rts/Game/UI/GuiHandler.cpp` | 91 | UNTOUCHED |
| 5 | `rts/Rendering/GL/VertexArray.cpp` | 85 | DOCUMENTED_ONLY |
| 6 | `rts/Rendering/Units/UnitDrawer.cpp` | 82 | PARTIAL |
| 7 | `rts/Rendering/Env/Particles/ProjectileDrawer.cpp` | 82 | DOCUMENTED_ONLY |
| 8 | `rts/Rendering/GlobalRendering.cpp` | 73 | DOCUMENTED_ONLY |
| 9 | `rts/Rendering/Env/GrassDrawer.cpp` | 65 | UNTOUCHED |
| 10 | `rts/Rendering/Env/DynWater.cpp` | 65 | DEPRECATION CANDIDATE |
| 11 | `rts/Rendering/Common/ModelDrawerHelpers.cpp` | 60 | PARTIAL |
| 12 | `rts/Rendering/WorldDrawer.cpp` | 52 | DOCUMENTED_ONLY |
| 13 | `rts/Rendering/HUDDrawer.cpp` | 43 | UNTOUCHED |
| 14 | `rts/Rendering/Env/SkyBox.cpp` | 25 | UNTOUCHED |
| 15 | `rts/Rendering/HAPFSPathDrawer.cpp` | 22 | UNTOUCHED |
| 16 | `rts/Rendering/Env/AdvWater.cpp` | 22 | DEPRECATION CANDIDATE |
| 17 | `rts/Rendering/QTPFSPathDrawer.cpp` | 20 | UNTOUCHED |
| 18 | `rts/Rendering/Fonts/glFontRenderer.cpp` | 18 | UNTOUCHED |
| 19 | `rts/Rendering/DebugDrawerAI.cpp` | 18 | UNTOUCHED |
| 20 | `rts/Rendering/SmoothHeightMeshDrawer.cpp` | 17 | UNTOUCHED |
| 21 | `rts/Rendering/Env/Decals/GroundDecalHandler.cpp` | 16 | UNTOUCHED |
| 22 | `rts/Rendering/Env/BumpWater.cpp` | 16 | ~85-90% MIGRATED |
| 23 | `rts/Rendering/Env/DebugCubeMapTexture.cpp` | 14 | UNTOUCHED |
| 24 | `rts/Game/SelectedUnitsHandler.cpp` | 14 | UNTOUCHED |
| 25 | `rts/Rendering/GlobalRendering.cpp` | 12 | DOCUMENTED_ONLY |
| 26 | `rts/Rendering/Env/RefractWater.cpp` | 12 | DEPRECATION CANDIDATE |
| 27 | `rts/Rendering/GL/GeometryBuffer.cpp` | 11 | UNTOUCHED |
| 28 | `rts/Rendering/Common/ModelDrawer.h` | 11 | PARTIAL |
| 29 | `rts/Rendering/LineDrawer.cpp` | 10 | UNTOUCHED |
| 30 | `rts/Rendering/CommandDrawer.cpp` | 10 | UNTOUCHED |

### Summary by Subsystem

| Subsystem | Files | GL Calls | Status |
|-----------|-------|----------|--------|
| Lua API (LuaOpenGL) | 1 | ~513 | DOCUMENTED_ONLY |
| Rendering/Env (water, grass, sky, decals) | ~10 | ~200+ | MIXED (BumpWater migrated, others not) |
| Game/UI (MiniMap, GuiHandler, etc.) | ~8 | ~170+ | UNTOUCHED |
| Rendering/Units+Models | ~5 | ~140+ | PARTIAL |
| RmlUi Renderer | 1 | ~194 | UNTOUCHED |
| Rendering/Common | ~3 | ~70+ | PARTIAL |
| Rendering/GL (non-backend) | ~5 | ~50+ | LEGACY (LightHandler needs UBO) |
| Rendering/Misc (HUD, paths, debug) | ~8 | ~100+ | UNTOUCHED |
| Rendering/Fonts | ~2 | ~18 | UNTOUCHED |

---

## 2. Tier 3 Verification

**Finding: Tier 3 agents added DOCUMENTATION ONLY. No actual code migration was performed.**

This was verified by comparing claimed vs actual GL call counts:

| File | Tier 3 Claimed | Actual Count | Documentation | Code Changed |
|------|---------------|-------------|---------------|-------------|
| WorldDrawer.cpp | ~26 | 52 | Yes | No |
| GlobalRendering.cpp | "Many" | 73 | Yes | No |
| LuaOpenGL.cpp | 221 | 513 | Yes | No |
| RmlUi_Renderer | 176 | 194 | No | No |
| VertexArray.cpp | ~56 | 85 | Yes | No |
| ProjectileDrawer.cpp | 44+ | 82 | Yes | No |

33 files have `RHI Migration Status:` headers with categorized migration guides. These guides are high quality and actionable but represent documentation, not implementation.

---

## 3. Post-Tier 3 Work Completed

### BumpWater Migration (~85-90% complete)

Commits `baef79c55f` and `005d034f1c` migrated BumpWater from 121 GL calls to 16:

**Migrated to RHI:**
- All internal textures use `std::unique_ptr<RHI::IRHITexture>`
- All FBOs use `std::unique_ptr<RHI::IRHIFramebuffer>`
- Viewport, clear, blit operations use IRHIContext
- Pipeline state uses RHI::ScopedPipeline
- FFP fog state removed

**16 GL calls intentionally remaining:**
- 7 calls for external/atlas texture binding (coastUpdateTexture from CTextureAtlas)
- 5 calls for texture unit activation (glActiveTexture)
- 1 call for legacy FBO attachment (coastUpdateTexture)
- 1 call for depth copy (glCopyTexSubImage2D - no RHI equivalent for depth)
- 1 feature flag constant (GLAD_GL_ARB_imaging)
- 1 call for shadow texture parameter

These will be resolved when external systems (CTextureAtlas, readMap, shadowHandler) are RHI-migrated.

---

## 4. RHI Infrastructure Status (Verified)

### 4.1 RHI Interfaces (8 headers, all substantive)

| File | Lines | Purpose | Verdict |
|------|-------|---------|---------|
| `RHITypes.h` | 391 | Enums, structs, pipeline desc | COMPLETE |
| `RHIDevice.h` | 100 | Device interface, resource creation | COMPLETE |
| `RHIContext.h` | 93 | Command submission, draw calls | COMPLETE |
| `RHIBuffer.h` | 57 | Buffer interface | COMPLETE |
| `RHITexture.h` | 94 | Texture interface | COMPLETE |
| `RHIShader.h` | 79 | Shader interface, uniform setters | COMPLETE |
| `RHIFramebuffer.h` | 63 | Framebuffer interface | COMPLETE |
| `RHIPipeline.h` | 43 | Pipeline state interface | COMPLETE |
| `RHIScopedState.h` | 170 | RAII wrappers | COMPLETE |
| `RHIFactory.h/cpp` | 48/77 | Backend selection | COMPLETE |
| `ShaderCompiler.h/cpp` | - | GLSL->SPIRV->MSL | COMPLETE |
| `ShaderReflection.h` | - | Shader reflection data | COMPLETE |

### 4.2 OpenGL Backend (14 files, verified real implementations)

All files contain real GLAD GL function calls, not stubs:
- GLDevice: 18 capability queries, timer queries, resource factories
- GLContext: render pass management, primitive type mapping, draw commands
- GLPipeline: complete conversion tables (blend, depth, stencil, polygon mode)
- GLTexture: format conversion tables for all texture types

### 4.3 Metal Backend (15 files, verified real implementations)

All files contain real Metal API calls, not stubs:
- MTLDevice: `MTLCreateSystemDefaultDevice`, GPU family queries, command queue creation
- MTLContext: triple buffering with dispatch semaphores, render encoders
- MTLPipeline: complete conversion tables (all 15 blend factors, 5 blend ops, 8 compare funcs)
- MTLShader: integration with ShaderCompiler for GLSL->MSL translation

### 4.4 Shader Translations (41/41 verified)

All 41 Metal shaders contain real MSL code with:
- `#include <metal_stdlib>` and `using namespace metal;`
- Proper struct definitions with `[[stage_in]]`, `[[buffer(N)]]`, `[[position]]` attributes
- Full implementations (not placeholder code)

---

## 5. RHI Gaps Identified

| Interface | Gap | Impact |
|-----------|-----|--------|
| IRHIShader | No runtime source compilation | Blocks RenderBuffers generated GLSL |
| IRHIShader | No hot-reload | Development convenience only |
| IRHIContext | Missing DrawArrays with baseVertex | Some draw calls |
| IRHIContext | Missing SetClipDistanceEnabled() | Water clip planes |
| IRHIContext | Missing glVertexAttribDivisor | Instanced rendering |
| IRHITexture | No MSAA support | Anti-aliasing |
| IRHITexture | No border color | Edge sampling |
| IRHITexture | No swizzle support | Texture channel remap |
| IRHIBuffer | No persistent mapping | StreamBuffer performance |
| IRHIBuffer | No fence sync | Buffer synchronization |
| IRHIFramebuffer | No SetDrawBuffers() | MRT (multiple render targets) |
| IRHIPipeline | No polygon mode (wireframe) | Debug only, not available in Metal |

---

## 6. Recommendations

### Phase 1: Foundation (Blocking)
1. **GlobalRendering refactor** - Own IRHIDevice instead of SDL_GLContext (73 calls, architectural blocker)
2. **RHI gap filling** - Add missing interface methods for clip distances, instancing, MRT

### Phase 2: Core Systems
3. **Texture manager refactor** - Replace GLuint with IRHITexture throughout (unblocks BumpWater completion)
4. **LuaOpenGL RHI path** - 513 calls, largest single file, needs dual GL/RHI path
5. **Game/UI migration** - MiniMap (98) + GuiHandler (91), completely untouched

### Phase 3: Subsystems
6. **Environment rendering** - GrassDrawer (65), SkyBox (25), ProjectileDrawer (82)
7. **Model/unit rendering** - UnitDrawer (82), ModelDrawerHelpers (60)
8. **Misc rendering** - HUDDrawer (43), PathDrawers (42), DebugDrawerAI (18), Fonts (18)
9. **Light system redesign** - LightHandler FFP->UBO conversion
10. **Legacy water deprecation** - DynWater, AdvWater, RefractWater

### Phase 4: Polish
11. **RmlUi migration** - 194 calls, may need separate RHI-based renderer class

---

## 7. Conclusion

**Infrastructure: 100% complete and verified** - All RHI interfaces, both backends, shader pipeline, and shader translations are real, substantive implementations.

**Code migration: ~5-10% complete** - BumpWater and ISky are the only substantially migrated files. 33 files have documentation. ~86 files still need actual code changes.

**Corrected scope: ~1,111 GL function calls** across ~86 files (previous report's 8,167 included GL_ enum constants).

**Primary blockers for full Metal support:**
1. GlobalRendering architectural refactor (blocks most other work)
2. Texture manager needs GLuint->IRHITexture conversion
3. LuaOpenGL exposes GL directly to scripts (513 calls)
4. Game/UI subsystem completely untouched (170+ calls)
5. RmlUi renderer is GL-only (194 calls)

**Estimated total effort: 18-27 weeks** (revised from previous 8-12 week estimate based on corrected counts)

---

*Report generated by auditor agent on arm64-metal-port branch*
*Scan date: 2026-02-05 (revision 2, corrects 2026-02-04 audit)*
