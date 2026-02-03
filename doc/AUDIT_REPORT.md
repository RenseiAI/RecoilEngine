# RHI Migration Audit Report

**Date:** 2026-02-03
**Branch:** arm64-metal-port
**Auditor:** Claude Opus 4.5

## Executive Summary

The RHI (Render Hardware Interface) migration is **partially complete**. The abstraction layer is well-designed and functional, with both OpenGL and Metal backends implemented. However, significant direct GL calls remain throughout the codebase that need migration before Metal can fully replace OpenGL.

| Category | Status |
|----------|--------|
| RHI Interface Design | Complete |
| OpenGL Backend | Complete |
| Metal Backend | Complete |
| Shader Pipeline | Complete (41/41 MSL translations) |
| Code Migration | **In Progress** (~2,748 direct GL calls remain) |

---

## 1. Direct GL Calls Outside the GL Backend

### Summary by Directory

| Directory | Direct GL Calls | Files | Priority |
|-----------|-----------------|-------|----------|
| `rts/Rendering/` | 1,871 | 89 | High |
| `rts/Lua/` | 493 | 20 | High |
| `rts/Game/` | 207 | 15 | Medium |
| `rts/Map/` | 157 | 7 | Medium |
| `rts/aGui/` | 16 | 4 | Low |
| `rts/System/` | 4 | 1 | Low |
| **Total** | **2,748** | **136** | - |

### 1.1 Acceptable Locations (GL backend and library code)

The following contain GL calls but are **acceptable**:

- `rts/Rendering/RHI/OpenGL/` - GL backend implementation (95 calls in 6 files)
- `rts/Rendering/GL/` - Low-level GL utilities (342 calls in 22 files)
- `rts/lib/` - Third-party libraries (headless stubs, Tracy, RmlUi, etc.)

### 1.2 High-Priority Files Needing Migration

#### Lua Scripting API (493 GL calls in 20 files)
These expose GL functionality to Lua scripts and need RHI abstraction:

| File | GL Calls | Notes |
|------|----------|-------|
| `rts/Lua/LuaOpenGL.cpp:221` | 221 | Core Lua GL bindings |
| `rts/Lua/LuaShaders.cpp:65` | 65 | Shader compilation/uniforms |
| `rts/Lua/LuaTextures.cpp:49` | 49 | Texture creation/management |
| `rts/Lua/LuaFBOs.cpp:48` | 48 | Framebuffer objects |
| `rts/Lua/LuaVAOImpl.cpp:30` | 30 | Vertex array objects |
| `rts/Lua/LuaMaterial.cpp:20` | 20 | Material uniforms |
| `rts/Lua/LuaOpenGLUtils.cpp:19` | 19 | Texture binding utilities |
| `rts/Lua/LuaRBOs.cpp:11` | 11 | Renderbuffer objects |

#### Water Rendering (497 GL calls in 6 files)
Legacy water effects with extensive FFP and ARB program usage:

| File | GL Calls | Notes |
|------|----------|-------|
| `rts/Rendering/Env/DynWater.cpp:300` | 300 | ARB programs, immediate mode, raw FBOs |
| `rts/Rendering/Env/BumpWater.cpp:109` | 109 | Complex water simulation |
| `rts/Rendering/Env/AdvWater.cpp:59` | 59 | ARB programs, texgen |
| `rts/Rendering/Env/RefractWater.cpp:23` | 23 | ARB programs, glCopyTexSubImage2D |
| `rts/Rendering/Env/BasicWater.cpp:6` | 6 | Simple water |
| `rts/Rendering/Env/IWater.cpp:4` | 4 | GL_CLIP_PLANE2 |

#### UI Rendering (159 GL calls in 2 files)
Game UI with heavy GL usage:

| File | GL Calls | Notes |
|------|----------|-------|
| `rts/Game/UI/GuiHandler.cpp:97` | 97 | Command icons, selection |
| `rts/Game/UI/MiniMap.cpp:62` | 62 | Minimap rendering |

#### Map/Terrain (157 GL calls in 7 files)
SMF terrain system:

| File | GL Calls | Notes |
|------|----------|-------|
| `rts/Map/SMF/SMFReadMap.cpp:49` | 49 | Texture creation |
| `rts/Map/SMF/SMFRenderState.cpp:34` | 34 | Texture binding |
| `rts/Map/SMF/SMFGroundDrawer.cpp:31` | 31 | State changes |
| `rts/Map/SMF/SMFGroundTextures.cpp:27` | 27 | Texture management |
| `rts/Map/SMF/ROAM/Patch.cpp:14` | 14 | Vertex attributes |

#### Other High-Impact Files

| File | GL Calls | Notes |
|------|----------|-------|
| `rts/Rendering/Env/GrassDrawer.cpp:89` | 89 | Grass system |
| `rts/Rendering/Common/ModelDrawerHelpers.cpp:67` | 67 | Model rendering |
| `rts/Rendering/Shaders/Shader.cpp:65` | 65 | Shader program wrapper |
| `rts/Rendering/Env/Particles/ProjectileDrawer.cpp:60` | 60 | Particle effects |
| `rts/Rendering/Units/UnitDrawer.cpp:52` | 52 | Unit rendering |
| `rts/Rendering/GlobalRendering.cpp:46` | 46 | Capability queries |
| `rts/Rendering/GL/FBO.cpp:45` | 45 | FBO wrapper |
| `rts/Rendering/Env/CubeMapHandler.cpp:41` | 41 | Environment maps |
| `rts/Rendering/Env/Decals/GroundDecalHandler.cpp:39` | 39 | Ground decals |

---

## 2. Categorization of GL Usage

### 2.1 Direct Calls (Needs Migration)
Functions calling GL directly instead of through RHI:

- **Draw calls:** `glDrawArrays`, `glDrawElements`, `glDrawElementsInstanced`
- **State changes:** `glEnable`, `glDisable`, `glBlendFunc`, `glDepthFunc`
- **Texture ops:** `glGenTextures`, `glBindTexture`, `glTexImage2D`, `glTexParameter*`
- **Framebuffer ops:** `glGenFramebuffers*`, `glBindFramebuffer*`, `glFramebufferTexture*`
- **Shader ops:** `glCreateShader`, `glCompileShader`, `glUseProgram`, `glUniform*`
- **Buffer ops:** `glGenBuffers`, `glBindBuffer`, `glBufferData`, `glMapBuffer*`

### 2.2 GL Enum Usage (Needs Conversion)
Code using GL constants that should use RHI enums:

- `GL_TRIANGLES` -> `RHI::PrimitiveType::Triangles`
- `GL_TEXTURE_2D` -> `RHI::TextureType::Texture2D`
- `GL_DEPTH_TEST` -> Pipeline state
- `GL_BLEND` -> Pipeline state

### 2.3 GL Includes (Should Use RHI Headers)
Files including `myGL.h` or `glad.h` directly:

**Total: 119 files** including `Rendering/GL/myGL.h`

Many include comments like:
- `// transitional: GL types still needed`
- `// RHI-TODO: replace with RHI texture handles`
- `// retained: FFP matrix stack, no RHI equivalent`

### 2.4 Acceptable (OK to Keep)
- `rts/lib/headlessStubs/gladstub.cpp` - Headless mode stubs
- `rts/Rendering/RHI/OpenGL/*` - OpenGL backend implementation
- `rts/Rendering/GL/*` - Low-level GL utilities (will be deprecated)
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

### 3.2 OpenGL Backend Files
- `GLBuffer.cpp/h` - Buffer implementation
- `GLContext.cpp/h` - Command submission
- `GLDevice.cpp/h` - Device and resource creation
- `GLFramebuffer.cpp/h` - FBO implementation
- `GLPipeline.cpp/h` - Pipeline state
- `GLShader.cpp/h` - Shader programs
- `GLTexture.cpp/h` - Texture management

### 3.3 Metal Backend Files
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

## 5. Recommendations

### 5.1 Immediate Priority (Blocks Metal Testing)

1. **Create RHI-aware Lua GL API wrapper**
   - Abstract LuaOpenGL.cpp to use RHI
   - Maintain Lua API compatibility
   - ~500 GL calls to migrate

2. **Migrate Shader.cpp to use RHI**
   - Replace direct glCreateShader/glCompileShader
   - Use IRHIShader abstraction
   - ~65 GL calls

3. **Migrate GlobalRendering capability queries**
   - Delegate to IRHIDevice
   - ~46 GL calls

### 5.2 High Priority (Core Rendering)

4. **Water rendering migration**
   - DynWater.cpp needs complete rewrite (ARB programs -> GLSL/MSL)
   - BumpWater.cpp complex but doable
   - ~500 GL calls total

5. **UI rendering migration**
   - GuiHandler.cpp and MiniMap.cpp
   - ~160 GL calls

6. **Terrain/Map migration**
   - SMF* files
   - ~160 GL calls

### 5.3 Medium Priority

7. **Model and particle rendering**
   - ModelDrawerHelpers.cpp
   - ProjectileDrawer.cpp
   - UnitDrawer.cpp
   - ~180 GL calls

8. **Environment effects**
   - GrassDrawer.cpp
   - CubeMapHandler.cpp
   - GroundDecalHandler.cpp
   - ~170 GL calls

### 5.4 Low Priority (Can Defer)

9. **aGui components** - 16 GL calls
10. **SplashScreen.cpp** - 4 GL calls

### 5.5 Architecture Recommendations

1. **Phase out `rts/Rendering/GL/` gradually**
   - Replace FBO.cpp usage with IRHIFramebuffer
   - Replace VBO.cpp usage with IRHIBuffer
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

## 6. Conclusion

The RHI abstraction layer is well-designed and complete. Both OpenGL and Metal backends implement all required interfaces. All 41 GLSL shaders have MSL translations.

However, **approximately 2,748 direct GL calls** remain in application code outside the GL backend, preventing a full transition to Metal. The Lua scripting API and water rendering are the largest remaining work items.

**Estimated remaining effort:**
- Lua GL API: 2-3 weeks
- Water effects: 2-3 weeks
- UI/Terrain/Models: 2-3 weeks
- Total: 6-9 weeks for complete migration

The incremental approach allows testing with OpenGL while gradually migrating to RHI abstractions. The Metal backend can be validated with simpler rendering paths first before tackling complex features like dynamic water.
