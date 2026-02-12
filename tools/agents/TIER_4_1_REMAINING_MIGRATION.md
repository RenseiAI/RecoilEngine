# Tier 4.1: Remaining RHI Migration Work

This document captures the state of the ARM64+Metal port after Tier 3 completion and
ongoing batch migration work.

**Last audited:** 2026-02-12

## Current State (Post Tier 3 + Batches 3-9)

### Completed Infrastructure (Verified 2026-02-12)

| Component | Status | Files | Notes |
|-----------|--------|-------|-------|
| RHI Interfaces | 8/8 Complete | 8 headers, 169 virtual methods | IRHIDevice, IRHIContext, IRHIBuffer, IRHITexture, IRHIShader, IRHIFramebuffer, IRHIPipeline, RHIScopedState |
| OpenGL Backend | Complete | 14 files, 160 methods | Real GL implementations |
| Metal Backend | Complete | 15 files, 138 methods | Real Metal API calls |
| Shader Pipeline | Complete | ShaderCompiler.h/.cpp + ShaderReflection.h | GLSL -> SPIR-V -> MSL via glslang + SPIRV-Cross |
| Shader Translations | 41/41 | 41 .metal files | Verified real MSL code |
| RHI Factory | Complete | RHIFactory.h/.cpp | Selects Metal on macOS ARM64, OpenGL elsewhere |
| Headless Stubs | Complete | gladstub.cpp | 421 GL function stubs |

### Completed Migration Work (Batches 3-9, ~219 GL calls removed)

| Batch | Description | GL calls removed |
|-------|-------------|-----------------|
| 3 | glRectf -> TypedRenderBuffer in 4 files | ~12 |
| 4 | WorldDrawer, CommandDrawer -> TypedRenderBuffer | ~23 |
| 5 | Remove FFP GL_TEXTURE_2D, GL_LIGHTING enables | ~17 |
| 6 | LineDrawer -> TypedRenderBuffer, remove line stipple | ~16 |
| 7 | ModelDrawerHelpers FFP texture enables | ~14 |
| 8 | DebugDrawerAI/MiniMap/InMapDrawView textures -> IRHITexture | ~47 |
| 9 | Remove FFP GL_TEXTURE_CUBE_MAP, GL_FOG, GL_LIGHTING, GL_ALPHA_TEST | ~90 |

Post-Batch 9 commits:
- `ee97f76` Remove redundant glActiveTexture resets and migrate shadow compare mode to RHI
- `cf4661f` Remove redundant glBindTexture(0) unbinds and fix GrassDrawer RHI device creation bug
- `c96e3ae` Remove no-op FFP calls, stale glGetError, and commented-out GL code from 8 files
- `79b1949` Remove glPushAttrib/glPopAttrib, FFP texture enables, and dead DrawShadow code
- `a590a55` Remove no-op glColor calls from HUDDrawer/LuaObjectDrawer/GrassDrawer

### Phase 1 Gap-Filling — COMPLETE
- Fence sync support added to IRHIDevice/IRHIContext
- Buffer mapping (Map/Unmap) added to IRHIBuffer
- Debug output callback support added
- GlobalRendering redirect to RHI device
- Clip distance enable/disable added to IRHIContext

### Legacy Water — DEPRECATED
- DynWater, AdvWater, RefractWater commented out of CMakeLists
- ~522 GL calls excluded from active build
- BumpWater is the sole Metal-compatible water renderer (~11 GL calls remain at external boundaries)

### Remaining Work

**~2,857 effective GL calls** remain across 117 compiled files (3,379 raw - 522 deprecated water).
**~2,671 calls need migration** (excluding 186 in GL backend files that stay as-is).

## Priority Files (2026-02-12 Audit)

### Top 25 Files by GL Call Count

| # | File | Calls | Status | Notes |
|---|------|-------|--------|-------|
| 1 | `rts/Lua/LuaOpenGL.cpp` | 464 | DOCUMENTED_ONLY | Lua GL scripting API — needs careful RHI wrapper |
| 2 | `rts/Rml/Backends/RmlUi_Renderer_GL3_Recoil.cpp` | 194 | UNTOUCHED | External UI lib — needs RHI backend |
| 3 | `rts/Game/UI/GuiHandler.cpp` | 130 | PARTIAL | Game UI — heavy FFP matrix stack |
| 4 | `rts/Rendering/Shaders/Shader.cpp` | 107 | **GL BACKEND** | IS the GL shader backend — keep as-is |
| 5 | `rts/Rendering/GL/myGL.cpp` | 92 | PARTIAL | GL utility layer |
| 6 | `rts/Rendering/Units/UnitDrawer.cpp` | 86 | PARTIAL | Unit rendering — partial migration |
| 7 | `rts/Rendering/GL/VertexArray.cpp` | 85 | DEPRECATED | Legacy vertex array (use RenderBuffers) |
| 8 | `rts/Rendering/GL/FBO.cpp` | 84 | WRAPPER | FBO wrapper — has RHI equivalent |
| 9 | `rts/Rendering/Env/GrassDrawer.cpp` | 80 | PARTIAL | Grass rendering — textures migrated |
| 10 | `rts/Game/UI/MiniMap.cpp` | 80 | PARTIAL | Minimap — textures migrated, matrix stack remains |
| 11 | `rts/Lua/LuaShaders.cpp` | 78 | DOCUMENTED_ONLY | Lua shader interface |
| 12 | `rts/Rendering/HUDDrawer.cpp` | 56 | UNTOUCHED | HUD rendering — FFP matrix stack |
| 13 | `rts/Lua/LuaFBOs.cpp` | 53 | DOCUMENTED_ONLY | Lua FBO interface |
| 14 | `rts/Map/SMF/SMFReadMap.cpp` | 51 | UNTOUCHED | Terrain map loading |
| 15 | `rts/Rendering/GlobalRendering.cpp` | 50 | ~95% DONE | SDL boundary + extension queries remain |
| 16 | `rts/Rendering/Env/Particles/ProjectileDrawer.cpp` | 49 | DOCUMENTED_ONLY | Particle effects |
| 17 | `rts/Lua/LuaTextures.cpp` | 48 | DOCUMENTED_ONLY | Lua texture interface |
| 18 | `rts/Rendering/Env/SkyBox.cpp` | 43 | UNTOUCHED | Sky rendering |
| 19 | `rts/Rendering/Textures/nv_dds.cpp` | 42 | UNTOUCHED | DDS texture loading |
| 20 | `rts/Rendering/Shaders/GLSLCopyState.cpp` | 42 | **GL BACKEND** | GL-specific shader introspection — keep as-is |
| 21 | `rts/Rendering/Env/Decals/GroundDecalHandler.cpp` | 38 | UNTOUCHED | Ground decals |
| 22 | `rts/Rendering/Textures/Texture.cpp` | 37 | **GL BACKEND** | IS the GL texture backend — keep as-is |
| 23 | `rts/Rendering/Models/3DModelVAO.cpp` | 37 | UNTOUCHED | Model vertex arrays |
| 24 | `rts/Map/SMF/SMFRenderState.cpp` | 35 | UNTOUCHED | Terrain render state |
| 25 | `rts/Rendering/Common/ModelDrawerHelpers.cpp` | 26 | PARTIAL | Model rendering helpers |

### By Domain

| Domain | Calls | % | Key Files |
|--------|-------|---|-----------|
| Lua GL scripting | ~643 | 22% | LuaOpenGL, LuaShaders, LuaFBOs, LuaTextures |
| Rendering core | ~534 | 19% | Shader.cpp, myGL.cpp, VertexArray.cpp, FBO.cpp, GlobalRendering |
| Environment | ~290 | 10% | GrassDrawer, SkyBox, ProjectileDrawer, Decals |
| Game UI | ~266 | 9% | GuiHandler, MiniMap, HUDDrawer |
| Unit/Model rendering | ~149 | 5% | UnitDrawer, 3DModelVAO, ModelDrawerHelpers |
| Map/Terrain | ~86 | 3% | SMFReadMap, SMFRenderState |
| RmlUi | 194 | 7% | RmlUi_Renderer_GL3_Recoil.cpp |
| Texture management | ~127 | 4% | nv_dds, Texture.cpp, GLSLCopyState |
| Other (~70 files) | ~568 | 20% | Scattered across remaining files |

### GL Backend Files (Do NOT Migrate)

These files are GL backend implementations and should remain as-is:

| File | Calls | Reason |
|------|-------|--------|
| `rts/Rendering/Shaders/Shader.cpp` | 107 | IS the GL shader backend (GLSLProgramObject/GLSLShaderObject) |
| `rts/Rendering/Shaders/GLSLCopyState.cpp` | 42 | GL-specific shader introspection for hot-reload |
| `rts/Rendering/Textures/Texture.cpp` | 37 | IS the GL texture backend (GL::Texture2D/Texture2DArray) |
| **Total** | **186** | Stays in codebase permanently |

## Prerequisites — Create Before Migration Phases

### P1. MatrixStack utility class (NEW)
- **Location:** `rts/Rendering/RHI/MatrixStack.h`
- **Purpose:** Replace all FFP `glPushMatrix/glPopMatrix/glTranslatef/glRotatef/glScalef` patterns
- **Design:** `class MatrixStack { std::stack<CMatrix44f>; Push(); Pop(); Top(); Translate(); Rotate(); Scale(); LoadIdentity(); Ortho(); }`
- **Unblocks:** UnitDrawer (30), GrassDrawer (24), MiniMap (48), GuiHandler (14), HUDDrawer (30), ProjectileDrawer (12), SkyBox (16) = **~174 calls total**
- **Effort:** Small

### P2. MapTexture refactor (ARCHITECTURAL)
- **What:** `MapTexture` currently stores raw `GLuint` ID. Needs `unique_ptr<IRHITexture>` alongside or instead.
- **Location:** `rts/Map/MapTexture.h`
- **Unblocks:** SMFReadMap (51), SMFRenderState (35), SkyBox (skyTex), GroundDecalHandler
- **Effort:** Medium — ripples to many consumers

### P3. glExtra.cpp migration
- **What:** `glSurfaceCircle()` and `glBallisticCircle()` use `CVertexArray` internally
- **Location:** `rts/Rendering/GL/glExtra.cpp`
- **Replace:** `CVertexArray` -> `TypedRenderBuffer`
- **Unblocks:** GuiHandler, MiniMap
- **Effort:** Small

### P4. RHI interface extensions (for later phases)
- `IRHIContext::SetStencilFunc()`, `SetStencilOp()`, `SetStencilMask()` — needed for RmlUi
- `IRHIContext::SetBlendEquation()` — needed for RmlUi
- `IRHITexture::SetMinLOD()`/`SetMaxLOD()` — needed for GrassDrawer
- **Effort:** Medium

## Migration Roadmap

### Phase A: Infrastructure & Immediate Wins

| Step | Task | Calls Removed | Dependency |
|------|------|--------------|------------|
| A1 | Create MatrixStack utility (P1) | 0 (enabler) | None |
| A2 | Migrate VertexArray.cpp callers -> TypedRenderBuffer | ~85 | None |
| A3 | myGL.cpp ARB removal + ClearScreen FFP cleanup | ~40 | None |
| A4 | FBO.cpp parallel wrapper (background) | ~84 | None |

### Phase B: Texture Pipeline (~300 calls)

| Step | Task | Calls Removed | Dependency |
|------|------|--------------|------------|
| B1 | TextureAtlas -> IRHITexture | ~15 | None |
| B2 | MapTexture refactor (P2) | 0 (enabler) | None |
| B3 | SMFReadMap textures | ~51 | B2 |
| B4 | SMFRenderState texture binding | ~35 | B3 |
| B5 | Bitmap callers -> CreateTextureRHI() | ~25 | None |

### Phase C: Core Subsystem Migration

| Step | Task | Calls Removed | Dependency |
|------|------|--------------|------------|
| C1 | GlobalRendering finish | ~10 | None |
| C2 | UnitDrawer phased | ~60 | P1, B1 |
| C3 | GrassDrawer phased | ~60 | P1, B1, B2 |
| C4 | SkyBox + ProjectileDrawer + Decals | ~100 | B1, B2, P1 |

### Phase D: UI Layer

| Step | Task | Calls Removed | Dependency |
|------|------|--------------|------------|
| D1 | HUDDrawer matrix stack | ~45 | P1 |
| D2 | MiniMap matrix + clip | ~60 | P1 |
| D3 | GuiHandler (includes glLogicOp redesign) | ~100 | P1, P3 |
| D4 | RmlUi Renderer (LAST) | ~194 | P4 |

### Phase E: Lua GL API

| Step | Task | Calls Removed | Dependency |
|------|------|--------------|------------|
| E1 | LuaOpenGL render state (easiest Lua win) | ~30 | None |
| E2 | LuaOpenGL FFP deprecation | ~40 | P1 |
| E3 | LuaOpenGL immediate mode | ~20 | None |
| E4 | LuaOpenGL display lists (deprecate) | ~5 | None |
| E5 | LuaOpenGL textures + support files | ~179 | B1 |
| E6 | LuaOpenGL queries + sync | ~15 | None |

### Phase F: Polish & Verification
1. Build and test `engine-legacy` — Full Metal rendering path
2. CI pipeline (GitHub Actions) — Automated headless build
3. macOS app bundle — Packaging for distribution
4. Final audit pass — Verify no GL calls remain outside backends

## Cross-Cutting Patterns

### Shared across many files:
1. **FFP Matrix Stack -> MatrixStack + uniforms** — ~174 calls across 7+ files
2. **Display Lists -> VBOs** — GrassDrawer, GuiHandler, UnitDrawer, LuaOpenGL
3. **Texture System GLuint -> IRHITexture*** — IconHandler, ReadMap, ShadowHandler, InfoTextureHandler, TextureAtlas
4. **glClipPlane -> shader gl_ClipDistance** — UnitDrawer, GrassDrawer, MiniMap, LuaOpenGL
5. **CVertexArray -> TypedRenderBuffer** — GrassDrawer, glExtra.cpp, ROAM/Patch
6. **glActiveTexture+glBindTexture -> ctx->BindTexture** — SMFRenderState, GroundDecalHandler, ProjectileDrawer

### RHI APIs still needed:
- `IRHITexture::SetMinLOD(float)` / `SetMaxLOD(float)` (GrassDrawer)
- `IRHIContext::SetStencilFunc/Op/Mask()` (RmlUi)
- `IRHIContext::SetBlendEquation()` (RmlUi)
- MSAA renderbuffer support in `IRHIFramebuffer` (RmlUi)
- `IRHITexture::Readback()` (debug utilities)

## Known Blockers

| Blocker | Impact | Status |
|---------|--------|--------|
| MatrixStack utility not yet created | Blocks ~174 FFP matrix calls | P1 prerequisite |
| MapTexture stores raw GLuint | Blocks SMF/Sky/Decal texture binding | P2 prerequisite |
| `glLogicOp(GL_INVERT)` has no Metal equivalent | Blocks GuiHandler selection XOR | Needs redesign (stencil or shader) |
| Lua API compatibility | 643 calls in Lua interface | Must maintain mod compatibility |
| RmlUi needs RHI stencil/blend extensions | 194 calls | P4 prerequisite |
| Texture managers return raw GLuint | Blocks many downstream bindings | TextureAtlas + others need IRHITexture |

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

| FFP Feature | Migration Path |
|-------------|----------------|
| `glMatrixMode`/`glPushMatrix`/`glPopMatrix` | MatrixStack utility + shader uniforms |
| `glBegin`/`glEnd` immediate mode | `TypedRenderBuffer` with vertex data |
| `glColor3f`/`glColor4f` | Vertex attribute or shader uniform |
| `glFog*` | Shader-based fog calculation |
| `glLight*`/`glMaterial*` | UBO with light data |
| `glClipPlane` | Shader `gl_ClipDistance[]` output |
| `glTexGen*` | Shader-based texture coordinate generation |
| Display lists | VBO/IBO with recorded geometry |

## Files with Migration Documentation

33 files have migration status headers from Tier 3 agents. Markers used:
- `RHI Migration Notes` - Block comment with migration status
- `RHI_MIGRATION_DOCS` - Macro-style documentation
- `RHI_TODO` - Inline markers for specific calls
- `RHI GAP` - Calls with no RHI equivalent

## Branch Status

Current branch: `arm64-metal-port`

Merged Tier 3 branches:
- agent/migrate-core, agent/migrate-water, agent/migrate-sky
- agent/migrate-terrain, agent/migrate-models, agent/migrate-effects
- agent/migrate-textures, agent/migrate-fonts, agent/migrate-shaders
- agent/migrate-toplevel

Post-Tier 3 work completed:
- BumpWater RHI migration (~85-90% complete)
- Batches 3-9: ~219 GL call sites removed
- Post-Batch 9: redundant GL call cleanup, shadow compare RHI migration
- Phase 1 gap-filling (fence sync, buffer mapping, debug output, clip distances)
- Legacy water deprecation (DynWater, AdvWater, RefractWater)

## How to Resume

1. Review this document and `HANDOFF_NEXT_BATCH.md`
2. Start with Prerequisites (P1-P4) — these unblock everything
3. Follow the phased roadmap (A -> B -> C/D/E in parallel -> F)
4. After each batch: rebuild headless, verify GL call count decreased
5. See `tools/agents/prompts/` for individual agent prompts (24 agents)

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

# Build headless (verification)
cmake --build build-arm64/ --target engine-headless -j$(sysctl -n hw.ncpu)
```
