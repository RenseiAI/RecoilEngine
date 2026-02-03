# Tier 4.1: Remaining RHI Migration Work

This document captures the state of the ARM64+Metal port after Tier 3 completion.
Use this to resume migration work in a new session.

## Current State (Post Tier 3)

### Completed Infrastructure ✓

| Component | Status | Notes |
|-----------|--------|-------|
| RHI Interfaces | 7/7 Complete | IRHIDevice, IRHIContext, IRHIBuffer, IRHITexture, IRHIShader, IRHIFramebuffer, IRHIPipeline |
| OpenGL Backend | Complete | 7 implementation files in `rts/Rendering/RHI/OpenGL/` |
| Metal Backend | Complete | 9 implementation files in `rts/Rendering/RHI/Metal/` |
| Shader Pipeline | Complete | GLSL → SPIR-V → MSL via glslang + SPIRV-Cross |
| Shader Translations | 41/41 | All GLSL shaders have MSL equivalents in `cont/base/springcontent/shaders/Metal/` |
| RHI Factory | Complete | Selects Metal on macOS ARM64, OpenGL elsewhere |

### Remaining Work

**~2,487 direct GL calls** still need migration across ~100 files.

The Tier 3 agents added comprehensive migration documentation to files but did NOT perform the actual code migration (that would require significant refactoring).

## Priority Files

### HIGH Priority (Core Functionality)

| File | GL Calls | Migration Notes |
|------|----------|-----------------|
| `rts/Lua/LuaOpenGL.cpp` | 221 | Lua scripting API - must maintain compatibility. See `rts/Lua/LuaOpenGL_RHI_MIGRATION.md` |
| `rts/Rendering/Env/BumpWater.cpp` | 121 | Recommended water renderer for Metal. Uses GLSL shaders (compilable to MSL) |
| `rts/Rendering/WorldDrawer.cpp` | ~26 | Top-level render coordinator. Documents migration paths inline |
| `rts/Rendering/GlobalRendering.cpp` | Many | Should own IRHIDevice instead of SDL_GLContext |

### MEDIUM Priority

| File | GL Calls | Migration Notes |
|------|----------|-----------------|
| `rts/Rendering/RmlUi_Renderer_GL3_Recoil.cpp` | 176 | UI renderer - may need RHI-native RmlUi backend |
| `rts/Rendering/GL/VertexArray.cpp` | ~56 | Legacy FFP - preserved for Lua widget compatibility |
| `rts/Rendering/Env/Particles/ProjectileDrawer.cpp` | 44+ | Perlin textures, FBO operations |
| `rts/Map/SMF/SMFRenderState.cpp` | Many | Terrain texture binding |

### LOW Priority (Deprecation Candidates)

| File | GL Calls | Recommendation |
|------|----------|----------------|
| `rts/Rendering/Env/DynWater.cpp` | 301 | **DEPRECATE** - 12 ARB programs, ~170 GL calls, immediate mode. Estimate 5-7 weeks to port |
| `rts/Rendering/Env/AdvWater.cpp` | 66 | **DEPRECATE** - ARB fragment programs, GL_EYE_LINEAR texgen |
| `rts/Rendering/Env/RefractWater.cpp` | 55 | **DEPRECATE** - Inherits AdvWater issues, GL_TEXTURE_RECTANGLE_ARB |

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

### Pattern 1: Direct GL Call → RHI Equivalent

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

### 1. GlobalRendering Refactor
`GlobalRendering` should own `IRHIDevice*` instead of `SDL_GLContext`. This is the most significant architectural change.

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
Deprecate DynWater, AdvWater, RefractWater. Focus on BumpWater as the single Metal-compatible water renderer.

## Files with Migration Documentation

Every file touched by Tier 3 agents has inline documentation with one of these markers:
- `RHI Migration Notes` - Block comment with migration status
- `RHI_MIGRATION_DOCS` - Macro-style documentation
- `RHI_TODO` - Inline markers for specific calls
- `RHI GAP` - Calls with no RHI equivalent

Search for these markers to find documented migration points:
```bash
grep -r "RHI Migration" rts/Rendering/
grep -r "RHI_TODO" rts/Rendering/
grep -r "RHI GAP" rts/Rendering/
```

## Suggested Tier 4.1 Agent Structure

To complete the remaining migration, consider these focused agents:

### Agent: migrate-globalrendering
- Refactor GlobalRendering to own IRHIDevice
- Update initialization/shutdown paths
- Critical foundation for other migrations

### Agent: migrate-textures-impl
- Convert texture managers from GLuint to IRHITexture
- Update TextureCollection, S3OTextureHandler, 3DOTextureHandler
- Requires RHI texture creation throughout

### Agent: migrate-lua-opengl
- Add RHI path to LuaOpenGL
- Maintain backward compatibility with existing Lua scripts
- May need dual GL/RHI paths initially

### Agent: migrate-bumpwater
- Complete BumpWater RHI migration
- This becomes the reference water implementation for Metal

### Agent: deprecate-legacy-water
- Remove or disable DynWater, AdvWater, RefractWater
- Update water selection logic to prefer BumpWater

### Agent: rhi-gap-filler
- Add missing RHI interface methods
- Implement MSAA, border color, swizzle, clip distances
- Update both GL and Metal backends

## Estimated Effort

| Task | Estimate |
|------|----------|
| GlobalRendering refactor | 1-2 weeks |
| Texture manager refactor | 2-3 weeks |
| LuaOpenGL RHI path | 2-3 weeks |
| BumpWater completion | 1 week |
| Legacy water deprecation | 1 week |
| RHI gap filling | 1-2 weeks |
| **Total** | **8-12 weeks** |

## How to Resume

1. Review this document and `doc/AUDIT_REPORT.md`
2. Run auditor agent to get current GL call counts
3. Choose a migration agent to run based on priorities
4. After each agent completes, merge and re-audit

## Branch Status

Current branch: `arm64-metal-port`

Merged Tier 3 branches:
- agent/migrate-core ✓
- agent/migrate-water ✓
- agent/migrate-sky ✓
- agent/migrate-terrain ✓
- agent/migrate-models ✓
- agent/migrate-effects ✓
- agent/migrate-textures ✓
- agent/migrate-fonts ✓
- agent/migrate-shaders ✓
- agent/migrate-toplevel ✓

Tier 5 prompts created (not yet executed):
- tools/agents/prompts/ci-pipeline.md
- tools/agents/prompts/app-bundle.md
- tools/agents/prompts/auditor.md (executed twice)

## Quick Reference Commands

```bash
# Check current GL call count
grep -rE "gl[A-Z][a-zA-Z]+\(" rts/ --include="*.cpp" | wc -l

# Find files with most GL calls
grep -rEc "gl[A-Z][a-zA-Z]+\(" rts/Rendering/*.cpp | sort -t: -k2 -nr | head -20

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
