# Tier 4.1: Remaining RHI Migration Work

This document captures the state of the ARM64+Metal port after Tier 3 completion.
Use this to resume migration work in a new session.

**Last audited:** 2026-02-05

## Current State (Post Tier 3 + BumpWater Migration)

### Completed Infrastructure (Verified 2026-02-05)

| Component | Status | Files | Notes |
|-----------|--------|-------|-------|
| RHI Interfaces | 8/8 Complete | 8 headers in `rts/Rendering/RHI/` | IRHIDevice, IRHIContext, IRHIBuffer, IRHITexture, IRHIShader, IRHIFramebuffer, IRHIPipeline, RHIScopedState |
| OpenGL Backend | Complete | 14 files (7 .h/.cpp pairs) in `rts/Rendering/RHI/OpenGL/` | Real GL implementations, not stubs |
| Metal Backend | Complete | 15 files in `rts/Rendering/RHI/Metal/` | Real Metal API calls, not stubs |
| Shader Pipeline | Complete | ShaderCompiler.h/.cpp + ShaderReflection.h | GLSL -> SPIR-V -> MSL via glslang + SPIRV-Cross |
| Shader Translations | 41/41 | 41 .metal files in `cont/base/springcontent/shaders/Metal/` | Verified real MSL code |
| RHI Factory | Complete | RHIFactory.h/.cpp | Selects Metal on macOS ARM64, OpenGL elsewhere |

### Remaining Work

**~1,111 direct GL calls** remain across ~86 files (audited 2026-02-05).

Previous estimate of ~2,487 was inflated. Actual count excludes:
- RHI OpenGL backend (`rts/Rendering/RHI/OpenGL/`) - legitimate GL code
- GLAD library files and headless stubs
- Comments and documentation blocks

The Tier 3 agents added comprehensive migration documentation to files but did NOT perform the actual code migration (confirmed by audit).

## Priority Files (Corrected Counts)

### HIGH Priority (Core Functionality)

| File | Actual GL Calls | Previous Estimate | Status | Migration Notes |
|------|----------------|-------------------|--------|-----------------|
| `rts/Lua/LuaOpenGL.cpp` | **513** | 221 | DOCUMENTED_ONLY | Lua scripting API - must maintain compatibility |
| `rts/Game/UI/MiniMap.cpp` | **98** | (not listed) | UNTOUCHED | Heavy FFP matrix stack, texture state |
| `rts/Game/UI/GuiHandler.cpp` | **91** | (not listed) | UNTOUCHED | FFP matrix stack, blend, texture state |
| `rts/Rendering/Units/UnitDrawer.cpp` | **82** | (not listed) | PARTIAL | Has RHI device/context setup, still uses GL calls |
| `rts/Rendering/GlobalRendering.cpp` | **73** | "Many" | DOCUMENTED_ONLY | Should own IRHIDevice instead of SDL_GLContext |
| `rts/Rendering/Env/GrassDrawer.cpp` | **65** | (not listed) | UNTOUCHED | Grass rendering |
| `rts/Rendering/WorldDrawer.cpp` | **52** | ~26 | DOCUMENTED_ONLY | Top-level render coordinator |

### MEDIUM Priority

| File | Actual GL Calls | Previous Estimate | Status | Migration Notes |
|------|----------------|-------------------|--------|-----------------|
| `rts/Rendering/RmlUi_Renderer_GL3_Recoil.cpp` | **194** | 176 | UNTOUCHED | UI renderer - may need RHI-native RmlUi backend |
| `rts/Rendering/GL/VertexArray.cpp` | **85** | ~56 | DOCUMENTED_ONLY | Legacy FFP - preserved for Lua widget compatibility |
| `rts/Rendering/Env/Particles/ProjectileDrawer.cpp` | **82** | 44+ | DOCUMENTED_ONLY | Perlin textures, FBO operations |
| `rts/Rendering/Common/ModelDrawerHelpers.cpp` | **60** | (not listed) | PARTIAL | Model rendering helpers |
| `rts/Rendering/HUDDrawer.cpp` | **43** | (not listed) | UNTOUCHED | HUD rendering, FFP matrix stack |
| `rts/Rendering/Env/SkyBox.cpp` | **25** | (not listed) | UNTOUCHED | Sky rendering |
| `rts/Rendering/HAPFSPathDrawer.cpp` | **22** | (not listed) | UNTOUCHED | Path visualization |
| `rts/Rendering/QTPFSPathDrawer.cpp` | **20** | (not listed) | UNTOUCHED | Path visualization |
| `rts/Rendering/Fonts/glFontRenderer.cpp` | **18** | (not listed) | UNTOUCHED | Font rendering |
| `rts/Rendering/DebugDrawerAI.cpp` | **18** | (not listed) | UNTOUCHED | Debug visualization |
| `rts/Rendering/SmoothHeightMeshDrawer.cpp` | **17** | (not listed) | UNTOUCHED | Terrain debug |
| `rts/Rendering/Env/Decals/GroundDecalHandler.cpp` | **16** | (not listed) | UNTOUCHED | Ground decals |

### SUBSTANTIALLY MIGRATED

| File | Remaining GL Calls | Status | Notes |
|------|-------------------|--------|-------|
| `rts/Rendering/Env/BumpWater.cpp` | **16** (from 121) | ~85-90% MIGRATED | Core pipeline uses RHI. Remaining calls are intentional: external texture boundaries (CTextureAtlas, readMap, shadowHandler) |
| `rts/Rendering/Env/ISky.cpp` | **~2** | MOSTLY COMPLETE | |

### LOW Priority (Deprecation Candidates)

| File | Actual GL Calls | Recommendation |
|------|----------------|----------------|
| `rts/Rendering/Env/DynWater.cpp` | **65** | **DEPRECATE** - 12 ARB programs, immediate mode. BumpWater is the Metal-compatible replacement |
| `rts/Rendering/Env/AdvWater.cpp` | **22** | **DEPRECATE** - ARB fragment programs, GL_EYE_LINEAR texgen |
| `rts/Rendering/Env/RefractWater.cpp` | **12** | **DEPRECATE** - Inherits AdvWater issues |

## GL Calls by Subsystem

| Subsystem | Est. GL Calls | Status |
|-----------|--------------|--------|
| Lua/LuaOpenGL | ~513 | DOCUMENTED_ONLY - largest single file |
| Game/UI | ~170+ | UNTOUCHED - MiniMap (98), GuiHandler (91) |
| Rendering/Env | ~200+ | MIXED - BumpWater migrated, others not |
| Rendering/Units+Models | ~140+ | PARTIAL - some RHI setup exists |
| Rendering/Common | ~70+ | PARTIAL |
| Rendering/GL (non-backend) | ~50+ | LEGACY - LightHandler needs UBO redesign |
| Rendering/Fonts | ~18 | UNTOUCHED |
| Rendering/Map | ~20+ | UNTOUCHED |
| Rendering/RmlUi | ~194 | UNTOUCHED |

## Remaining GL Call Categories

### 1. Fixed-Function Pipeline (FFP) - ~300+ calls
No direct RHI equivalent. Require shader-based alternatives:
- `glMatrixMode`/`glPushMatrix`/`glPopMatrix` (MiniMap, GuiHandler, HUDDrawer, WorldDrawer)
- `glEnable(GL_TEXTURE_2D)`/`glDisable(GL_TEXTURE_2D)` (~80 calls, FFP texture unit activation)
- `glColor4f`/`glDisable(GL_LIGHTING)` (per-vertex FFP lighting)
- `glPushAttrib`/`glPopAttrib` (legacy state save/restore)
- `glLight*`/`glLightf`/`glLightfv` (LightHandler.cpp - needs UBO redesign)

### 2. Texture Operations - ~80+ calls
Can be migrated to RHI:
- `glBindTexture`/`glActiveTexture`
- `glTexImage2D`/`glTexParameter*`
- `glGenTextures`/`glDeleteTextures`

### 3. State Management - ~300+ calls
Direct RHI equivalents exist:
- `glEnable/glDisable(GL_DEPTH_TEST)` -> `RHI::DepthStencilState`
- `glEnable/glDisable(GL_BLEND)` -> `RHI::BlendState`
- `glBlendFunc` -> pipeline state
- `glViewport` -> `IRHIContext::SetViewport()`
- `glClear`/`glClearColor` -> `IRHIContext::Clear()`

### 4. Framebuffer Operations - ~50+ calls
- `glGenFramebuffers`/`glBindFramebuffer`
- `glFramebufferTexture2D`/`glFramebufferRenderbuffer`

### 5. Immediate Mode / Vertex Array - ~40+ calls
- `glBegin`/`glEnd` (DynWater)
- Legacy client state: `glEnableClientState`, `glVertexPointer`, `glDrawArrays`

## RHI Gaps Discovered

These gaps were identified during Tier 3 and need RHI interface additions:

### IRHIShader Gaps
- No runtime source compilation (needed for generated GLSL in RenderBuffers)
- No hot-reload support

### IRHIContext Gaps
- Missing `DrawArrays` with baseVertex offset
- Missing `SetClipDistanceEnabled()` for clip planes
- Missing `glVertexAttribDivisor` equivalent (instancing)

### IRHITexture Gaps
- No MSAA texture support (`glTexImage2DMultisample`)
- No border color support (`GL_TEXTURE_BORDER_COLOR`)
- No swizzle support (`GL_TEXTURE_SWIZZLE_RGBA`)
- No `GL_DEPTH_TEXTURE_MODE` equivalent

### IRHIBuffer Gaps
- No persistent mapping support
- No fence sync support (for StreamBuffer)

### IRHIFramebuffer Gaps
- Needs `SetDrawBuffers()` for MRT (multiple render targets)

### IRHIPipeline Gaps
- No polygon mode (wireframe) in Metal (debug only)

## Migration Patterns

### Pattern 1: Direct GL Call -> RHI Equivalent

```cpp
// Before
glViewport(x, y, w, h);
glClearColor(r, g, b, a);
glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

// After
auto* ctx = RHI::GetDevice()->GetContext();
ctx->SetViewport(x, y, w, h);
ctx->ClearColor(r, g, b, a);
ctx->Clear(RHI::ClearFlags::Color | RHI::ClearFlags::Depth);
```

### Pattern 2: Texture Lifecycle

```cpp
// Before
GLuint texId;
glGenTextures(1, &texId);
glBindTexture(GL_TEXTURE_2D, texId);
glTexImage2D(...);
// ... later
glDeleteTextures(1, &texId);

// After
std::unique_ptr<RHI::IRHITexture> texture;
texture = RHI::GetDevice()->CreateTexture(desc);
ctx->BindTexture(0, texture.get());
// Cleanup automatic via destructor
```

### Pattern 3: Pipeline State

```cpp
// Before
glEnable(GL_BLEND);
glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
glEnable(GL_DEPTH_TEST);
glDepthFunc(GL_LEQUAL);

// After
RHI::PipelineDesc desc;
desc.blend.enabled = true;
desc.blend.srcFactor = RHI::BlendFactor::SrcAlpha;
desc.blend.dstFactor = RHI::BlendFactor::OneMinusSrcAlpha;
desc.depthStencil.depthTestEnabled = true;
desc.depthStencil.depthFunc = RHI::CompareFunc::LessEqual;
auto* pipeline = RHI::GetDevice()->CreatePipeline(desc);
ctx->BindPipeline(pipeline);
```

### Pattern 4: Fixed-Function Pipeline (No Direct Equivalent)

These require shader-based alternatives:

| FFP Feature | Migration Path |
|-------------|----------------|
| `glMatrixMode`/`glPushMatrix`/`glPopMatrix` | Use `CMatrix44f` and upload via uniform buffer |
| `glBegin`/`glEnd` immediate mode | Use `IRHIBuffer` with vertex data |
| `glColor3f`/`glColor4f` | Vertex attribute or uniform |
| `glFog*` | Shader-based fog calculation |
| `glLight*`/`glMaterial*` | UBO with light data |
| `glClipPlane` | Shader `gl_ClipDistance[]` output |
| `glTexGen*` | Shader-based texture coordinate generation |
| Display lists | VBO/IBO with recorded geometry |

## Architectural Changes Needed

### 1. GlobalRendering Refactor (BLOCKING)
`GlobalRendering` should own `IRHIDevice*` instead of `SDL_GLContext`. This is the most significant architectural change and blocks most other migrations.

```cpp
// Current
class CGlobalRendering {
    SDL_GLContext glContext;
};

// Target
class CGlobalRendering {
    std::unique_ptr<RHI::IRHIDevice> rhiDevice;
};
```

### 2. Texture Manager Refactor
Replace raw `GLuint`/`uint32_t` texture IDs with `std::unique_ptr<IRHITexture>` throughout:
- `TextureCollection`
- `S3OTextureHandler`
- `3DOTextureHandler`
- `NamedTextures`
- `CTextureAtlas` (blocks BumpWater's remaining 16 GL calls)

### 3. Light System Redesign
Replace FFP lighting with UBO-based system:
```cpp
struct LightData {
    float4 position[MAX_LIGHTS];
    float4 color[MAX_LIGHTS];
    float4 attenuation[MAX_LIGHTS];
    int numLights;
};
// Upload via IRHIBuffer as uniform buffer
```

### 4. Water Renderer Consolidation
Deprecate DynWater, AdvWater, RefractWater. Focus on BumpWater as the single Metal-compatible water renderer. BumpWater is already ~85-90% migrated.

### 5. Game/UI Subsystem
MiniMap (98 calls) and GuiHandler (91 calls) are heavy FFP users. Migration requires:
- Replace `glMatrixMode`/`glPushMatrix`/`glPopMatrix` with `CMatrix44f` stack
- Replace `glEnable(GL_TEXTURE_2D)` with shader-based texture activation
- Replace `glPushAttrib`/`glPopAttrib` with `RHI::ScopedPipeline`

## Files with Migration Documentation

Every file touched by Tier 3 agents has inline documentation with one of these markers:
- `RHI Migration Notes` - Block comment with migration status
- `RHI_MIGRATION_DOCS` - Macro-style documentation
- `RHI_TODO` - Inline markers for specific calls
- `RHI GAP` - Calls with no RHI equivalent

33 files have migration status headers. The following files were NOT documented by Tier 3:
- `rts/Game/UI/MiniMap.cpp` (98 GL calls)
- `rts/Game/UI/GuiHandler.cpp` (91 GL calls)
- `rts/Rendering/RmlUi_Renderer_GL3_Recoil.cpp` (194 GL calls)
- Most `rts/Game/` files

## Suggested Tier 4.1 Agent Structure

### Phase 1: Foundation (Blocking)

#### Agent: migrate-globalrendering
- Refactor GlobalRendering to own IRHIDevice
- Update initialization/shutdown paths
- Critical foundation for other migrations

#### Agent: rhi-gap-filler
- Add missing RHI interface methods
- Implement MSAA, border color, swizzle, clip distances
- Update both GL and Metal backends

### Phase 2: Core Systems (After Phase 1)

#### Agent: migrate-textures-impl
- Convert texture managers from GLuint to IRHITexture
- Update TextureCollection, S3OTextureHandler, 3DOTextureHandler, CTextureAtlas
- Requires RHI texture creation throughout
- Unblocks BumpWater's remaining 16 GL calls

#### Agent: migrate-lua-opengl
- Add RHI path to LuaOpenGL (513 GL calls - largest single file)
- Maintain backward compatibility with existing Lua scripts
- May need dual GL/RHI paths initially

#### Agent: migrate-game-ui
- Migrate MiniMap.cpp (98 calls) and GuiHandler.cpp (91 calls)
- Replace FFP matrix stack with CMatrix44f
- Replace glPushAttrib/glPopAttrib with RHI pipeline state

### Phase 3: Subsystems

#### Agent: migrate-rendering-env
- GrassDrawer (65), SkyBox (25), ProjectileDrawer (82)
- GroundDecalHandler (16), DebugCubeMapTexture (14)

#### Agent: migrate-rendering-models
- UnitDrawer (82), ModelDrawerHelpers (60), ModelDrawer (11)

#### Agent: migrate-rendering-misc
- HUDDrawer (43), PathDrawers (42), DebugDrawerAI (18)
- FontRenderer (18), SmoothHeightMeshDrawer (17)
- CommandDrawer (10), LineDrawer (10)

#### Agent: deprecate-legacy-water
- Remove or disable DynWater, AdvWater, RefractWater
- Update water selection logic to prefer BumpWater

#### Agent: migrate-light-system
- Redesign LightHandler from FFP to UBO
- Critical for removing GL_LIGHT* calls

### Phase 4: Polish

#### Agent: migrate-rmlui
- RmlUi_Renderer_GL3_Recoil.cpp (194 calls)
- May need RHI-native RmlUi backend
- Consider separate RHI-based renderer class

## Estimated Effort (Revised)

| Task | Estimate |
|------|----------|
| GlobalRendering refactor | 1-2 weeks |
| RHI gap filling | 1-2 weeks |
| Texture manager refactor | 2-3 weeks |
| LuaOpenGL RHI path | 3-4 weeks |
| Game/UI migration | 2-3 weeks |
| Rendering subsystems | 3-4 weeks |
| Model/unit rendering | 2-3 weeks |
| Light system redesign | 1-2 weeks |
| RmlUi migration | 2-3 weeks |
| Legacy water deprecation | 1 week |
| BumpWater completion | <1 week (16 calls, pending texture manager) |
| **Total** | **18-27 weeks** |

Note: Previous estimate of 8-12 weeks was based on understated GL call counts. Actual scope is significantly larger.

## How to Resume

1. Review this document and `doc/AUDIT_REPORT.md`
2. Run auditor agent to get current GL call counts
3. Start with Phase 1 (GlobalRendering + RHI gaps) - these block everything
4. After each agent completes, merge and re-audit

## Branch Status

Current branch: `arm64-metal-port`

Merged Tier 3 branches:
- agent/migrate-core
- agent/migrate-water
- agent/migrate-sky
- agent/migrate-terrain
- agent/migrate-models
- agent/migrate-effects
- agent/migrate-textures
- agent/migrate-fonts
- agent/migrate-shaders
- agent/migrate-toplevel

Post-Tier 3 work completed:
- BumpWater RHI migration (~85-90% complete, 16 GL calls remain)
- RHI infrastructure fixes (GLDevice, GLShader, Metal backend compilation)
- GLAD stubs for RHI-introduced GL symbols

Tier 5 prompts created (not yet executed):
- tools/agents/prompts/ci-pipeline.md
- tools/agents/prompts/app-bundle.md
- tools/agents/prompts/auditor.md (executed twice)

## Quick Reference Commands

```bash
# Check current GL call count (excluding backend and stubs)
grep -rE "gl[A-Z][a-zA-Z]+\(" rts/ --include="*.cpp" \
  --exclude-dir="RHI/OpenGL" --exclude-dir="headlessStubs" | \
  grep -v "^.*//.*gl[A-Z]" | wc -l

# Find files with most GL calls
grep -rEc "gl[A-Z][a-zA-Z]+\(" rts/ --include="*.cpp" \
  --exclude-dir="RHI/OpenGL" --exclude-dir="headlessStubs" | \
  sort -t: -k2 -nr | head -30

# Find RHI migration markers
grep -r "RHI_TODO\|RHI GAP\|RHI Migration" rts/Rendering/

# Check RHI interface completeness
ls -la rts/Rendering/RHI/*.h
ls -la rts/Rendering/RHI/OpenGL/
ls -la rts/Rendering/RHI/Metal/

# Verify shader translations
ls cont/base/springcontent/shaders/GLSL/*.glsl | wc -l
ls cont/base/springcontent/shaders/Metal/*.metal | wc -l
```
