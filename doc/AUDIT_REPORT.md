# RHI Migration Audit Report

**Date:** 2026-02-03 (Updated)
**Branch:** arm64-metal-port
**Auditor:** Claude Opus 4.5
**Audit Type:** POST-MERGE verification after Tier 3 migration agents

## Executive Summary

The RHI (Render Hardware Interface) abstraction layer is **complete and functional**. Both OpenGL and Metal backends fully implement all interface methods. The shader pipeline is complete with all 41 GLSL shaders translated to MSL.

However, **significant direct GL calls remain** in application code that need migration before Metal can fully replace OpenGL as the primary renderer.

| Category | Previous | Current | Status |
|----------|----------|---------|--------|
| RHI Interface Design | Complete | Complete | No change |
| OpenGL Backend | Complete | Complete | No change |
| Metal Backend | Complete | Complete | No change |
| Shader Pipeline | 41/41 MSL | 41/41 MSL | No change |
| Code Migration | ~2,748 calls | ~2,857 calls | **In Progress** |

**Note:** The slight increase in call count reflects refined pattern matching, not regression.

---

## 1. Direct GL Calls Outside the GL Backend

### Summary by Directory

| Directory | GL Calls | Files | Priority |
|-----------|----------|-------|----------|
| `rts/Rendering/` (excl. RHI/OpenGL, GL/) | 1,458 | 51 | High |
| `rts/Rendering/GL/` | 275 | 11 | Low (intermediate wrappers) |
| `rts/Rendering/RHI/OpenGL/` | 95 | 5 | Acceptable (GL backend) |
| `rts/Lua/` | 469 | 11 | High |
| `rts/Game/` | 207 | 15 | Medium |
| `rts/Rml/` | 177 | 2 | Medium (third-party renderer) |
| `rts/Map/` | 156 | 6 | Medium |
| `rts/aGui/` | 16 | 4 | Low |
| `rts/System/` | 4 | 1 | Low |
| **Total Needing Migration** | **2,487** | **~100** | - |

### 1.1 Acceptable Locations (No Migration Needed)

The following contain GL calls but are **acceptable**:

- `rts/Rendering/RHI/OpenGL/` - GL backend implementation (95 calls in 5 files)
- `rts/lib/` - Third-party libraries (headless stubs, etc.)
- `rts/lib/headlessStubs/gladstub.cpp` - Headless mode stubs

### 1.2 Intermediate Wrappers (Low Priority)

`rts/Rendering/GL/` contains intermediate wrapper classes (275 calls in 11 files):
- `VertexArray.cpp` (58 calls)
- `myGL.cpp` (53 calls)
- `FBO.cpp` (45 calls)
- `glExtra.cpp` (28 calls)
- `GeometryBuffer.cpp` (27 calls)
- `VBO.cpp` (22 calls)
- Others (42 calls)

These will be deprecated as code migrates to direct RHI usage.

### 1.3 High-Priority Files Needing Migration

#### Water Rendering (532 GL calls in 5 files)
Legacy water effects with extensive FFP and ARB program usage:

| File | GL Calls | Category | Notes |
|------|----------|----------|-------|
| `rts/Rendering/Env/DynWater.cpp` | 301 | Direct call | ARB programs, immediate mode, raw FBOs |
| `rts/Rendering/Env/BumpWater.cpp` | 121 | Direct call | Complex water simulation |
| `rts/Rendering/Env/AdvWater.cpp` | 66 | Direct call | ARB programs, texgen |
| `rts/Rendering/Env/RefractWater.cpp` | 28 | Direct call | ARB programs, glCopyTexSubImage2D |
| `rts/Rendering/Env/BasicWater.cpp` | 16 | Direct call | Simple water |

#### Lua Scripting API (469 GL calls in 11 files)
These expose GL functionality to Lua scripts and need RHI abstraction:

| File | GL Calls | Category | Notes |
|------|----------|----------|-------|
| `rts/Lua/LuaOpenGL.cpp` | 221 | Direct call | Core Lua GL bindings |
| `rts/Lua/LuaShaders.cpp` | 65 | Direct call | Shader compilation/uniforms |
| `rts/Lua/LuaTextures.cpp` | 49 | Direct call | Texture creation/management |
| `rts/Lua/LuaFBOs.cpp` | 48 | Direct call | Framebuffer objects |
| `rts/Lua/LuaVAOImpl.cpp` | 30 | Direct call | Vertex array objects |
| `rts/Lua/LuaMaterial.cpp` | 20 | Direct call | Material uniforms |
| `rts/Lua/LuaOpenGLUtils.cpp` | 19 | Direct call | Texture binding utilities |
| `rts/Lua/LuaRBOs.cpp` | 11 | Direct call | Renderbuffer objects |
| Others | 6 | Direct call | Minor utilities |

#### UI Rendering (159 GL calls in 2 files)
Game UI with heavy GL usage:

| File | GL Calls | Category | Notes |
|------|----------|----------|-------|
| `rts/Game/UI/GuiHandler.cpp` | 97 | Direct call | Command icons, selection |
| `rts/Game/UI/MiniMap.cpp` | 62 | Direct call | Minimap rendering |

#### RmlUi Renderer (177 GL calls in 2 files)
Third-party UI library renderer:

| File | GL Calls | Category | Notes |
|------|----------|----------|-------|
| `rts/Rml/Backends/RmlUi_Renderer_GL3_Recoil.cpp` | 176 | Direct call | RmlUi GL3 backend |
| `rts/Rml/Components/ElementLuaTexture.cpp` | 1 | Direct call | Lua texture binding |

**Recommendation:** Consider RmlUi Metal backend or abstract via RHI.

#### Map/Terrain (156 GL calls in 6 files)
SMF terrain system:

| File | GL Calls | Category | Notes |
|------|----------|----------|-------|
| `rts/Map/SMF/SMFReadMap.cpp` | 49 | Direct call | Texture creation |
| `rts/Map/SMF/SMFRenderState.cpp` | 34 | Direct call | Texture binding |
| `rts/Map/SMF/SMFGroundDrawer.cpp` | 31 | Direct call | State changes |
| `rts/Map/SMF/SMFGroundTextures.cpp` | 27 | Direct call | Texture management |
| `rts/Map/SMF/ROAM/Patch.cpp` | 14 | Direct call | Vertex attributes |
| `rts/Map/ReadMap.cpp` | 1 | Direct call | Texture deletion |

#### Other High-Impact Files

| File | GL Calls | Notes |
|------|----------|-------|
| `rts/Rendering/Env/GrassDrawer.cpp` | 89 | Grass system |
| `rts/Rendering/Common/ModelDrawerHelpers.cpp` | 70 | Model rendering |
| `rts/Rendering/Shaders/Shader.cpp` | 65 | Shader program wrapper |
| `rts/Rendering/Env/Particles/ProjectileDrawer.cpp` | 60 | Particle effects |
| `rts/Rendering/Units/UnitDrawer.cpp` | 55 | Unit rendering |
| `rts/Rendering/GlobalRendering.cpp` | 44 | Capability queries |
| `rts/Rendering/Env/CubeMapHandler.cpp` | 41 | Environment maps |
| `rts/Rendering/Env/Decals/GroundDecalHandler.cpp` | 39 | Ground decals |
| `rts/Rendering/Textures/nv_dds.cpp` | 37 | DDS texture loading |
| `rts/Rendering/Models/3DModelVAO.cpp` | 35 | Model VAO management |
| `rts/Rendering/Textures/Texture.cpp` | 35 | Texture class |
| `rts/Rendering/WorldDrawer.cpp` | 33 | World rendering orchestration |

---

## 2. Categorization of GL Usage

### 2.1 Direct Calls (Needs Migration) - ~2,487 occurrences

Functions calling GL directly instead of through RHI:

- **Draw calls:** `glDrawArrays`, `glDrawElements`, `glDrawElementsInstanced`
- **State changes:** `glEnable`, `glDisable`, `glBlendFunc`, `glDepthFunc`
- **Texture ops:** `glGenTextures`, `glBindTexture`, `glTexImage2D`, `glTexParameter*`
- **Framebuffer ops:** `glGenFramebuffers*`, `glBindFramebuffer*`, `glFramebufferTexture*`
- **Shader ops:** `glCreateShader`, `glCompileShader`, `glUseProgram`, `glUniform*`
- **Buffer ops:** `glGenBuffers`, `glBindBuffer`, `glBufferData`, `glMapBuffer*`

### 2.2 GL Enum Usage (Needs Conversion) - ~1,049 occurrences

Code using GL constants that should use RHI enums:

- `GL_TRIANGLES` -> `RHI::PrimitiveType::Triangles`
- `GL_TEXTURE_2D` -> `RHI::TextureType::Texture2D`
- `GL_DEPTH_TEST` -> Pipeline state
- `GL_BLEND` -> Pipeline state

### 2.3 GL Includes (Should Use RHI Headers) - 136 files

Files including `myGL.h` or `glad.h` directly.

Many include transitional comments like:
- `// transitional: GL types still needed`
- `// RHI-TODO: replace with RHI texture handles`
- `// retained: FFP matrix stack, no RHI equivalent`

### 2.4 Acceptable (OK to Keep)

- `rts/lib/headlessStubs/gladstub.cpp` - Headless mode stubs
- `rts/Rendering/RHI/OpenGL/*` - OpenGL backend implementation
- `rts/lib/*` - Third-party libraries

---

## 3. RHI Completeness

### 3.1 RHI Interface Coverage

| Interface | Methods | OpenGL | Metal |
|-----------|---------|--------|-------|
| IRHIDevice | 28 | Complete | Complete |
| IRHIContext | 23 | Complete | Complete |
| IRHIBuffer | 13 | Complete | Complete |
| IRHITexture | 20 | Complete | Complete |
| IRHIShader | 21 | Complete | Complete |
| IRHIFramebuffer | 12 | Complete | Complete |
| IRHIPipeline | 2 | Complete | Complete |

### 3.2 OpenGL Backend Files (7 files)
- `GLBuffer.cpp/h` - Buffer implementation
- `GLContext.cpp/h` - Command submission
- `GLDevice.cpp/h` - Device and resource creation
- `GLFramebuffer.cpp/h` - FBO implementation
- `GLPipeline.cpp/h` - Pipeline state
- `GLShader.cpp/h` - Shader programs
- `GLTexture.cpp/h` - Texture management

### 3.3 Metal Backend Files (9 files)
- `MTLBuffer.mm/h` - Buffer implementation
- `MTLContext.mm/h` - Command buffer/encoder
- `MTLDevice.mm/h` - Device and resource creation
- `MTLFramebuffer.mm/h` - Render pass descriptors
- `MTLPipeline.mm/h` - Pipeline state objects
- `MTLShader.mm/h` - Shader library
- `MTLTexture.mm/h` - Texture management
- `MTLTriangleDemo.mm/h` - Test/demo code

### 3.4 RHI Factory

The factory correctly selects backends based on platform:
- macOS ARM64: Metal (default)
- Other platforms: OpenGL

```cpp
Backend GetDefaultBackend() {
#if defined(__APPLE__) && defined(__aarch64__)
    return Backend::Metal;
#else
    return Backend::OpenGL;
#endif
}
```

---

## 4. Shader Pipeline Status

### 4.1 GLSL Shaders (41 total)
Location: `cont/base/springcontent/shaders/GLSL/`

All 41 shaders present:
- BumpWater (VS/FS, CoastBlur VS/FS) - 4 shaders
- CubeMap (VS/FS) - 2 shaders
- EquiRectConverter (FS) - 1 shader
- Fullscreen (VS, TexFS) - 2 shaders
- Grass (Vert/Frag) - 2 shaders
- GroundDecals (Vert/Frag) - 2 shaders
- Icons (2D VS, 3D VS, FS) - 3 shaders
- MiniMap (Vert/Frag) - 2 shaders
- Model (Vert/Frag, GL4 variants) - 4 shaders
- ModernSky (VS/FS) - 2 shaders
- ProjFX (Vert/Frag, Shadow variants) - 4 shaders
- ShadowGen (Frag, VertMap, Vert, GL4 variants) - 5 shaders
- Shapes (Vert/Frag) - 2 shaders
- SMF (Border Vert/Frag, Main Vert/Frag, ShadingTexture Vert/Frag) - 6 shaders

### 4.2 MSL Translations (41 total)
Location: `cont/base/springcontent/shaders/Metal/`

**All 41 GLSL shaders have corresponding MSL translations.**

| GLSL Shader | MSL Shader | Status |
|-------------|------------|--------|
| BumpWaterVS.glsl | BumpWaterVS.metal | Complete |
| BumpWaterFS.glsl | BumpWaterFS.metal | Complete |
| BumpWaterCoastBlurVS.glsl | BumpWaterCoastBlurVS.metal | Complete |
| BumpWaterCoastBlurFS.glsl | BumpWaterCoastBlurFS.metal | Complete |
| CubeMapVS.glsl | CubeMapVS.metal | Complete |
| CubeMapFS.glsl | CubeMapFS.metal | Complete |
| EquiRectConverterFS.glsl | EquiRectConverterFS.metal | Complete |
| FullscreenTriangleVS.glsl | FullscreenTriangleVS.metal | Complete |
| FullscreenTriangleTexFS.glsl | FullscreenTriangleTexFS.metal | Complete |
| GrassVertProg.glsl | GrassVertProg.metal | Complete |
| GrassFragProg.glsl | GrassFragProg.metal | Complete |
| GroundDecalsVertProg.glsl | GroundDecalsVertProg.metal | Complete |
| GroundDecalsFragProg.glsl | GroundDecalsFragProg.metal | Complete |
| Icons2DVS.glsl | Icons2DVS.metal | Complete |
| Icons3DVS.glsl | Icons3DVS.metal | Complete |
| IconsFS.glsl | IconsFS.metal | Complete |
| MiniMapVertProg.glsl | MiniMapVertProg.metal | Complete |
| MiniMapFragProg.glsl | MiniMapFragProg.metal | Complete |
| ModelVertProg.glsl | ModelVertProg.metal | Complete |
| ModelFragProg.glsl | ModelFragProg.metal | Complete |
| ModelVertProgGL4.glsl | ModelVertProgGL4.metal | Complete |
| ModelFragProgGL4.glsl | ModelFragProgGL4.metal | Complete |
| ModernSkyVS.glsl | ModernSkyVS.metal | Complete |
| ModernSkyFS.glsl | ModernSkyFS.metal | Complete |
| ProjFXVertProg.glsl | ProjFXVertProg.metal | Complete |
| ProjFXFragProg.glsl | ProjFXFragProg.metal | Complete |
| ProjFXVertShadowProg.glsl | ProjFXVertShadowProg.metal | Complete |
| ProjFXFragShadowProg.glsl | ProjFXFragShadowProg.metal | Complete |
| ShadowGenVertProg.glsl | ShadowGenVertProg.metal | Complete |
| ShadowGenFragProg.glsl | ShadowGenFragProg.metal | Complete |
| ShadowGenVertMapProg.glsl | ShadowGenVertMapProg.metal | Complete |
| ShadowGenVertProgGL4.glsl | ShadowGenVertProgGL4.metal | Complete |
| ShadowGenFragProgGL4.glsl | ShadowGenFragProgGL4.metal | Complete |
| ShapesVertProg.glsl | ShapesVertProg.metal | Complete |
| ShapesFragProg.glsl | ShapesFragProg.metal | Complete |
| SMFVertProg.glsl | SMFVertProg.metal | Complete |
| SMFFragProg.glsl | SMFFragProg.metal | Complete |
| SMFBorderVertProg.glsl | SMFBorderVertProg.metal | Complete |
| SMFBorderFragProg.glsl | SMFBorderFragProg.metal | Complete |
| SMFShadingTextureVertProg.glsl | SMFShadingTextureVertProg.metal | Complete |
| SMFShadingTextureFragProg.glsl | SMFShadingTextureFragProg.metal | Complete |

### 4.3 Shader Compiler

`rts/Rendering/RHI/ShaderCompiler.h/cpp`:
- GLSL -> SPIR-V compilation via glslang
- SPIR-V -> MSL translation via SPIRV-Cross
- Reflection data extraction
- Content-hash caching

### 4.4 Shader Reflection

`rts/Rendering/RHI/ShaderReflection.h`:
- Uniform locations and Metal buffer indices
- Attribute locations
- Sampler bindings and texture indices

---

## 5. Changes Since Previous Audit

| Item | Previous | Current | Change |
|------|----------|---------|--------|
| Total GL calls (needing migration) | ~2,748 | ~2,487 | Refined count (excl. RHI/OpenGL and GL/ wrappers) |
| Files with GL includes | 119 | 136 | More comprehensive scan |
| RHI OpenGL backend | Complete | Complete | No change |
| RHI Metal backend | Complete | Complete | No change |
| MSL shader translations | 41/41 | 41/41 | No change |

**No regressions detected.** The RHI infrastructure remains complete and stable.

---

## 6. Recommendations

### 6.1 Immediate Priority (Blocks Metal Testing)

1. **Create RHI-aware Lua GL API wrapper**
   - Abstract LuaOpenGL.cpp to use RHI
   - Maintain Lua API compatibility
   - ~469 GL calls to migrate

2. **Migrate Shader.cpp to use RHI**
   - Replace direct glCreateShader/glCompileShader
   - Use IRHIShader abstraction
   - ~65 GL calls

3. **Migrate GlobalRendering capability queries**
   - Delegate to IRHIDevice
   - ~44 GL calls

### 6.2 High Priority (Core Rendering)

4. **Water rendering migration**
   - DynWater.cpp needs complete rewrite (ARB programs -> GLSL/MSL)
   - BumpWater.cpp complex but doable
   - ~532 GL calls total

5. **UI rendering migration**
   - GuiHandler.cpp and MiniMap.cpp
   - ~159 GL calls

6. **RmlUi renderer migration**
   - Consider RmlUi Metal backend
   - Or abstract via RHI
   - ~177 GL calls

7. **Terrain/Map migration**
   - SMF* files
   - ~156 GL calls

### 6.3 Medium Priority

8. **Model and particle rendering**
   - ModelDrawerHelpers.cpp
   - ProjectileDrawer.cpp
   - UnitDrawer.cpp
   - ~185 GL calls

9. **Environment effects**
   - GrassDrawer.cpp
   - CubeMapHandler.cpp
   - GroundDecalHandler.cpp
   - ~169 GL calls

### 6.4 Low Priority (Can Defer)

10. **aGui components** - 16 GL calls
11. **SplashScreen.cpp** - 4 GL calls
12. **rts/Rendering/GL/ wrappers** - 275 GL calls (deprecate as code migrates)

### 6.5 Architecture Recommendations

1. **Phase out `rts/Rendering/GL/` gradually**
   - Replace FBO.cpp usage with IRHIFramebuffer
   - Replace VBO.cpp usage with IRHIBuffer
   - Replace VertexArray.cpp with RHI draw calls
   - These are intermediate wrappers that add overhead

2. **Add RHI state tracking**
   - Track bound resources to avoid redundant binds
   - Metal requires explicit state; OpenGL benefits from caching

3. **Consider display list replacement**
   - `LuaDisplayLists.h` uses deprecated glNewList/glCallList
   - Replace with command buffers or VBO batching

4. **ARB program migration plan**
   - DynWater.cpp and AdvWater.cpp use ARB assembly programs
   - These have no modern equivalent; must rewrite as GLSL
   - Consider deprecating DynWater in favor of BumpWater

---

## 7. Conclusion

The RHI abstraction layer is **complete and well-designed**. Both OpenGL and Metal backends implement all required interfaces. All 41 GLSL shaders have MSL translations.

**Current blockers for full Metal support:**
- ~2,487 direct GL calls remain in application code
- Major areas: Lua API (469), Water (532), UI (159+177), Terrain (156)
- Legacy ARB programs in water shaders need GLSL rewrites

**Estimated remaining effort:**
- Lua GL API: 2-3 weeks
- Water effects: 2-3 weeks (DynWater may need deprecation)
- UI/Terrain/Models: 2-3 weeks
- RmlUi: 1 week
- Total: **8-12 weeks for complete migration**

The incremental approach allows testing with OpenGL while gradually migrating to RHI abstractions. The Metal backend can be validated with simpler rendering paths first before tackling complex features like dynamic water.

---

*Report generated by auditor agent on arm64-metal-port branch*
