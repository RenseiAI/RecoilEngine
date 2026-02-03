# ARM64 + Native Metal Port - Work Plan

Fine-grained, ordered list of changes for porting RecoilEngine to ARM64 with a native Metal rendering backend.

**Fork:** https://github.com/supaku/RecoilEngine
**Branch:** `arm64-metal-port`
**Base:** PR #2540 (Chainfire's Asahi Linux ARM64 work) cherry-picked onto master

---

## Phase 1: ARM64 Headless Build on macOS

Goal: Engine compiles and runs in headless mode on macOS ARM64.

### 1.01 - Build system: ARM64 architecture detection
- [ ] `CMakeLists.txt` - Add `aarch64`/`arm64` to 64-bit architecture check (lines 112-116)
- [ ] `CMakeLists.txt` - Guard SSE flags behind `__x86_64__` check (lines 429-476)
- [ ] `rts/build/cmake/TestCXXFlags.cmake` - Add ARM64 march detection (lines 77-93)
- [ ] `rts/build/cmake/TestCXXFlags.cmake` - Skip `-mieee-fp` on ARM64 (lines 48-62)
- [ ] `rts/build/cmake/TestCXXFlags.cmake` - Add `-ffp-contract=off` for ARM64

### 1.02 - streflop: ARM64 soft-float mode
- [ ] `rts/lib/streflop/CMakeLists.txt` - Skip `-mfpmath=sse -msse` on ARM64
- [ ] `rts/lib/streflop/CMakeLists.txt` - Define `STREFLOP_SOFT` for ARM64
- [ ] `rts/CMakeLists.txt` - Propagate `STREFLOP_SOFT` define for ARM64 builds
- [ ] Verify `STREFLOP_SOFT` produces consistent results across ARM64 runs

### 1.03 - simdjson: Enable ARM64 NEON
- [ ] `rts/lib/CMakeLists.txt` - Remove ARM64 exclusion for simdjson

### 1.04 - squish: NEON compatibility
- [ ] `rts/lib/squish/simd_sse.h` - Include sse2neon on ARM64 via simd_compat.h
- [ ] Verify DXT compression produces identical output on ARM64

### 1.05 - macOS platform: CPU topology
- [ ] Create `rts/System/Platform/Mac/CpuTopology.cpp`
- [ ] Implement Apple Silicon P-core/E-core detection via `sysctlbyname("hw.perflevel*")`
- [ ] Implement cache topology via sysctl

### 1.06 - macOS platform: Build dependencies
- [ ] Verify/install Homebrew dependencies: SDL2, DevIL, Freetype, OpenAL, zlib
- [ ] Test CMake configure on macOS ARM64
- [ ] Resolve any missing find_package modules for macOS

### 1.07 - Headless build and test
- [ ] Build headless target (`spring-headless`) on macOS ARM64
- [ ] Run unit tests
- [ ] Run replay determinism test (compare checksums across multiple ARM64 runs)
- [ ] Compare ARM64 STREFLOP_SOFT checksums with x86_64 STREFLOP_SSE (document differences)

---

## Phase 2: RHI Abstraction Layer

Goal: All rendering goes through a Rendering Hardware Interface. OpenGL backend wraps existing code. No visual changes.

### 2.01 - RHI core interfaces
- [ ] Create `rts/Rendering/RHI/RHITypes.h` - Enums, format descriptors, shared types
- [ ] Create `rts/Rendering/RHI/RHIDevice.h` - Device creation, capability queries
- [ ] Create `rts/Rendering/RHI/RHIContext.h` - Command submission, state management
- [ ] Create `rts/Rendering/RHI/RHIBuffer.h` - Vertex/Index/Uniform/Storage buffers
- [ ] Create `rts/Rendering/RHI/RHITexture.h` - Texture creation, sampling, render targets
- [ ] Create `rts/Rendering/RHI/RHIShader.h` - Shader program compilation, uniforms
- [ ] Create `rts/Rendering/RHI/RHIFramebuffer.h` - Render targets (FBO equivalent)
- [ ] Create `rts/Rendering/RHI/RHIPipeline.h` - Render state (blend, depth, stencil, cull)
- [ ] Create `rts/Rendering/RHI/RHIFactory.h` - Backend selection factory

### 2.02 - OpenGL backend implementation
- [ ] Create `rts/Rendering/RHI/OpenGL/GLDevice.h/cpp` - Wraps CGlobalRendering
- [ ] Create `rts/Rendering/RHI/OpenGL/GLContext.h/cpp` - GL state machine wrapper
- [ ] Create `rts/Rendering/RHI/OpenGL/GLBuffer.h/cpp` - Wraps VBO
- [ ] Create `rts/Rendering/RHI/OpenGL/GLTexture.h/cpp` - Wraps GL texture calls
- [ ] Create `rts/Rendering/RHI/OpenGL/GLShader.h/cpp` - Wraps GLSLProgramObject
- [ ] Create `rts/Rendering/RHI/OpenGL/GLFramebuffer.h/cpp` - Wraps FBO
- [ ] Create `rts/Rendering/RHI/OpenGL/GLPipeline.h/cpp` - Wraps GL::State
- [ ] Add RHI CMakeLists.txt and integrate with build

### 2.03 - Migrate: GeometryBuffer
- [ ] `rts/Rendering/GL/GeometryBuffer.cpp` - Replace direct GL calls with RHI
- [ ] Verify deferred rendering G-buffer still works

### 2.04 - Migrate: ShadowHandler
- [ ] `rts/Rendering/ShadowHandler.cpp` - Replace direct GL calls with RHI
- [ ] Verify shadow maps render correctly

### 2.05 - Migrate: DepthBufferCopy
- [ ] `rts/Rendering/DepthBufferCopy.cpp` - Replace direct GL calls with RHI

### 2.06 - Migrate: RenderBuffers (central draw dispatch)
- [ ] `rts/Rendering/GL/RenderBuffers.h` - Replace GL draw calls with RHI
- [ ] Migrate runtime GLSL generation to use RHI shader interface
- [ ] This is the most critical migration - most subsystems use RenderBuffers

### 2.07 - Migrate: VBO, VAO, StreamBuffer
- [ ] `rts/Rendering/GL/VBO.h/cpp` - Wrap behind RHIBuffer or deprecate in favor of RHI
- [ ] `rts/Rendering/GL/VAO.h/cpp` - Wrap behind RHI or deprecate
- [ ] `rts/Rendering/GL/StreamBuffer.h/cpp` - Add RHI-based stream buffer strategy

### 2.08 - Migrate: FBO
- [ ] `rts/Rendering/GL/FBO.h/cpp` - Wrap behind RHIFramebuffer or deprecate

### 2.09 - Migrate: State management
- [ ] `rts/Rendering/GL/State.h` - Map to RHIPipeline
- [ ] `rts/Rendering/GL/TexBind.h` - Map to RHIContext::BindTexture

### 2.10 - Migrate: Water renderers
- [ ] `rts/Rendering/Env/BasicWater.cpp`
- [ ] `rts/Rendering/Env/AdvWater.cpp`
- [ ] `rts/Rendering/Env/DynWater.cpp` (170 GL calls - largest single file)
- [ ] `rts/Rendering/Env/BumpWater.cpp`
- [ ] `rts/Rendering/Env/RefractWater.cpp`

### 2.11 - Migrate: Sky renderers
- [ ] `rts/Rendering/Env/ModernSky.cpp`
- [ ] `rts/Rendering/Env/SkyBox.cpp`
- [ ] `rts/Rendering/Env/CubeMapHandler.cpp`

### 2.12 - Migrate: Ground and decals
- [ ] `rts/Rendering/Env/GrassDrawer.cpp`
- [ ] `rts/Rendering/Env/Decals/GroundDecalHandler.cpp`

### 2.13 - Migrate: Model and unit rendering
- [ ] `rts/Rendering/Models/3DModelVAO.cpp`
- [ ] `rts/Rendering/Units/UnitDrawer.cpp`
- [ ] `rts/Rendering/Common/ModelDrawer.h`
- [ ] `rts/Rendering/Common/ModelDrawerState.cpp`
- [ ] `rts/Rendering/Common/ModelDrawerHelpers.cpp`
- [ ] `rts/Rendering/LuaObjectDrawer.cpp`

### 2.14 - Migrate: Particles and effects
- [ ] `rts/Rendering/Env/Particles/ProjectileDrawer.cpp`
- [ ] `rts/Rendering/Env/Particles/Classes/FlyingPiece.cpp`

### 2.15 - Migrate: Map rendering
- [ ] `rts/Rendering/Map/InfoTexture/Modern/*.cpp` (8 files)

### 2.16 - Migrate: UI and HUD
- [ ] `rts/Rendering/HUDDrawer.cpp`
- [ ] `rts/Rendering/IconHandler.cpp`
- [ ] `rts/Rendering/CommandDrawer.cpp`
- [ ] `rts/Rendering/InMapDrawView.cpp`
- [ ] `rts/Rendering/LineDrawer.cpp`
- [ ] `rts/Rendering/DebugDrawerAI.cpp`
- [ ] `rts/Rendering/DebugVisibilityDrawer.cpp`
- [ ] `rts/Rendering/QTPFSPathDrawer.cpp`
- [ ] `rts/Rendering/HAPFSPathDrawer.cpp`
- [ ] `rts/Rendering/SmoothHeightMeshDrawer.cpp`

### 2.17 - Migrate: Font rendering
- [ ] `rts/Rendering/Fonts/glFontRenderer.cpp`
- [ ] `rts/Rendering/Fonts/CFontTexture.cpp`

### 2.18 - Migrate: Texture management
- [ ] `rts/Rendering/Textures/Texture.cpp`
- [ ] `rts/Rendering/Textures/Bitmap.cpp`
- [ ] `rts/Rendering/Textures/TextureCollection.cpp`
- [ ] `rts/Rendering/Textures/TextureRenderAtlas.cpp`
- [ ] `rts/Rendering/Textures/NamedTextures.cpp`
- [ ] `rts/Rendering/Textures/S3OTextureHandler.cpp`
- [ ] `rts/Rendering/Textures/3DOTextureHandler.cpp`
- [ ] `rts/Rendering/Textures/nv_dds.cpp`
- [ ] `rts/Rendering/UnitDefImage.h`

### 2.19 - Migrate: Shader system
- [ ] `rts/Rendering/Shaders/Shader.h/cpp` - Extend IProgramObject for RHI
- [ ] `rts/Rendering/Shaders/ShaderHandler.h` - Use RHI shader factory
- [ ] `rts/Rendering/Shaders/GLSLCopyState.cpp`

### 2.20 - Migrate: Top-level rendering
- [ ] `rts/Rendering/WorldDrawer.cpp`
- [ ] `rts/Rendering/GlobalRendering.h/cpp` - Make backend-agnostic
- [ ] `rts/Rendering/GL/myGL.h/cpp` - Deprecate or wrap

### 2.21 - Migrate: GL utilities
- [ ] `rts/Rendering/GL/glExtra.h/cpp`
- [ ] `rts/Rendering/GL/glHelpers.h`
- [ ] `rts/Rendering/GL/glStateDebug.h/cpp`
- [ ] `rts/Rendering/GL/LightHandler.cpp`
- [ ] `rts/Rendering/GL/VertexArray.cpp` (legacy, consider removing)

### 2.22 - RHI migration verification
- [ ] Verify no direct `gl*` calls remain outside `rts/Rendering/RHI/OpenGL/`
- [ ] Run full visual test suite on OpenGL backend through RHI
- [ ] Performance comparison: before vs after RHI (should be negligible difference)

---

## Phase 3: Shader Translation Pipeline

Goal: Infrastructure to convert GLSL shaders to Metal Shading Language via SPIR-V.

### 3.01 - External library integration
- [ ] Add `glslang` as git submodule in `rts/lib/glslang`
- [ ] Add `SPIRV-Cross` as git submodule in `rts/lib/SPIRV-Cross`
- [ ] Add CMakeLists.txt entries to build both as static libraries

### 3.02 - Shader compiler infrastructure
- [ ] Create `rts/Rendering/RHI/ShaderCompiler.h/cpp`
  - GLSL -> SPIR-V (via glslang)
  - SPIR-V -> MSL (via SPIRV-Cross)
  - Caching compiled shaders by content hash
- [ ] Create `rts/Rendering/RHI/ShaderReflection.h/cpp`
  - Extract uniform/attribute/sampler metadata from SPIR-V
  - Map GLSL uniform locations to Metal buffer indices

### 3.03 - Pre-compiled shader build step
- [ ] CMake custom command: compile all 41 GLSL shaders to `.metallib` at build time
- [ ] Pipeline: glslang -> .spv -> SPIRV-Cross -> .metal -> xcrun metal -> .air -> metallib
- [ ] Store compiled .metallib in build output

### 3.04 - Translate core shaders
- [ ] `ModelVertProg.glsl` / `ModelFragProg.glsl`
- [ ] `ModelVertProgGL4.glsl` / `ModelFragProgGL4.glsl`
- [ ] `SMFVertProg.glsl` / `SMFFragProg.glsl`
- [ ] `ShadowGenVertProg.glsl` / `ShadowGenFragProg.glsl`
- [ ] `ShadowGenVertProgGL4.glsl` / `ShadowGenFragProgGL4.glsl`
- [ ] `ShadowGenVertMapProg.glsl`
- [ ] Verify SPIR-V reflection matches original GL uniform bindings

### 3.05 - Translate environment shaders
- [ ] `BumpWaterVS.glsl` / `BumpWaterFS.glsl`
- [ ] `BumpWaterCoastBlurVS.glsl` / `BumpWaterCoastBlurFS.glsl`
- [ ] `GrassVertProg.glsl` / `GrassFragProg.glsl`
- [ ] `ModernSkyVS.glsl` / `ModernSkyFS.glsl`
- [ ] `GroundDecalsVertProg.glsl` / `GroundDecalsFragProg.glsl`
- [ ] `CubeMapVS.glsl` / `CubeMapFS.glsl`
- [ ] `EquiRectConverterFS.glsl`

### 3.06 - Translate effect/particle shaders
- [ ] `ProjFXVertProg.glsl` / `ProjFXFragProg.glsl`
- [ ] `ProjFXVertShadowProg.glsl` / `ProjFXFragShadowProg.glsl`

### 3.07 - Translate UI/HUD shaders
- [ ] `Icons2DVS.glsl` / `Icons3DVS.glsl` / `IconsFS.glsl`
- [ ] `MiniMapVertProg.glsl` / `MiniMapFragProg.glsl`
- [ ] `ShapesVertProg.glsl` / `ShapesFragProg.glsl`

### 3.08 - Translate map/utility shaders
- [ ] `SMFBorderVertProg.glsl` / `SMFBorderFragProg.glsl`
- [ ] `SMFShadingTextureVertProg.glsl` / `SMFShadingTextureFragProg.glsl`
- [ ] `FullscreenTriangleVS.glsl` / `FullscreenTriangleTexFS.glsl`

### 3.09 - Runtime shader generation support
- [ ] `RenderBuffers.h` runtime GLSL generation -> route through SPIRV-Cross on Metal
- [ ] Lua `gl.CreateShader` GLSL -> route through SPIRV-Cross on Metal

---

## Phase 4: Metal Backend

Goal: Native Metal rendering via the RHI.

### 4.01 - Metal device and context
- [ ] Create `rts/Rendering/RHI/Metal/MTLDevice.h/mm` - MTLDevice, MTLCommandQueue
- [ ] Create `rts/Rendering/RHI/Metal/MTLContext.h/mm` - Command buffer/encoder management
- [ ] Optionally integrate `metal-cpp` for C++ wrappers

### 4.02 - Metal buffer management
- [ ] Create `rts/Rendering/RHI/Metal/MTLBuffer.h/mm`
- [ ] Implement triple-buffering (maps to IStreamBuffer patterns)
- [ ] Use `storageModeShared` for CPU+GPU visible buffers on Apple Silicon

### 4.03 - Metal texture management
- [ ] Create `rts/Rendering/RHI/Metal/MTLTexture.h/mm`
- [ ] Texture creation via MTLTextureDescriptor
- [ ] Sampler state management

### 4.04 - Metal shader management
- [ ] Create `rts/Rendering/RHI/Metal/MTLShader.h/mm`
- [ ] Load pre-compiled .metallib at startup
- [ ] Runtime shader compilation fallback via ShaderCompiler

### 4.05 - Metal framebuffer / render pass
- [ ] Create `rts/Rendering/RHI/Metal/MTLFramebuffer.h/mm`
- [ ] Map FBO bind/unbind to MTLRenderPassDescriptor begin/end
- [ ] Handle render target load/store actions

### 4.06 - Metal pipeline state
- [ ] Create `rts/Rendering/RHI/Metal/MTLPipeline.h/mm`
- [ ] Hash-based MTLRenderPipelineState caching
- [ ] Map GL blend/depth/stencil state to Metal pipeline descriptors

### 4.07 - SDL2 Metal integration
- [ ] Modify `rts/Rendering/GlobalRendering.cpp` - SDL_WINDOW_METAL + SDL_Metal_CreateView()
- [ ] Handle Metal swap chain (CAMetalLayer drawable presentation)
- [ ] Implement RHIFactory backend selection

### 4.08 - Framework linking
- [ ] `CMakeLists.txt` - find_library(Metal), find_library(QuartzCore)
- [ ] Conditional linking based on APPLE platform and Metal backend selection

### 4.09 - Metal backend testing
- [ ] Render simple triangle via Metal backend
- [ ] Render terrain (SMF shaders)
- [ ] Render units (Model shaders)
- [ ] Render water, sky, grass
- [ ] Render shadows
- [ ] Full scene rendering
- [ ] Visual comparison: Metal vs OpenGL (screenshot diff)

---

## Phase 5: Lua Rendering API

Goal: Lua `gl.*` API works through RHI on both backends.

### 5.01 - Lua GL constants
- [ ] `rts/Lua/LuaConstGL.h` - Map GL enum constants to RHI-agnostic constants

### 5.02 - Core Lua OpenGL
- [ ] `rts/Lua/LuaOpenGL.cpp` - Rewrite 70+ functions to use RHI (146 GL calls)
  - Texture operations
  - State functions (DepthTest, Blending, Culling)
  - Draw functions
  - Viewport/Scissor
  - Clear

### 5.03 - Lua resource management
- [ ] `rts/Lua/LuaTextures.cpp` - Texture create/bind/delete via RHI
- [ ] `rts/Lua/LuaFBOs.cpp` - FBO operations via RHI
- [ ] `rts/Lua/LuaRBOs.cpp` - Render buffer objects via RHI

### 5.04 - Lua geometry
- [ ] `rts/Lua/LuaVAOImpl.cpp` - VAO/VBO via RHI
- [ ] `rts/Lua/LuaVBOImpl.h/cpp` - VBO via RHI

### 5.05 - Lua shaders
- [ ] `rts/Lua/LuaShaders.cpp` - Shader create/use via RHI + SPIRV-Cross for Metal
- [ ] `rts/Lua/LuaOpenGLUtils.cpp` - GL utilities via RHI

### 5.06 - Lua other
- [ ] `rts/Lua/LuaMaterial.cpp`
- [ ] `rts/Lua/LuaFonts.cpp`
- [ ] `rts/Lua/LuaDisplayLists.h` - Consider deprecating display lists
- [ ] `rts/Lua/LuaParser.cpp`

### 5.07 - Lua compatibility testing
- [ ] Test top 10 BAR widgets on OpenGL backend through RHI
- [ ] Test top 10 BAR widgets on Metal backend
- [ ] Test custom Lua shaders on Metal backend

---

## Phase 6: Polish and CI

### 6.01 - macOS app bundle
- [ ] Enhance `MACOSX_BUNDLE` CMake support for Metal
- [ ] Create `Info.plist` with Metal and arm64 entries
- [ ] Code signing with hardened runtime

### 6.02 - CI pipeline
- [ ] Create `.github/workflows/macos-arm64.yml`
- [ ] Headless build + unit test on `macos-14` runner
- [ ] Metal rendering smoke test (if CI supports GPU)
- [ ] Replay determinism regression test

### 6.03 - Backend selection
- [ ] Add config option: `RenderingBackend = "auto" | "opengl" | "metal"`
- [ ] Auto-detection: Metal on macOS ARM64, OpenGL elsewhere

### 6.04 - RmlUi Metal support
- [ ] Investigate RmlUi GL3 renderer -> either wrap behind RHI or create Metal renderer
- [ ] Test UI rendering on Metal

### 6.05 - Performance optimization
- [ ] Profile Metal backend vs OpenGL
- [ ] Optimize pipeline state caching
- [ ] Optimize buffer upload strategies for Apple Silicon
- [ ] Ensure RHI overhead is minimal (inlining, thin wrappers)

### 6.06 - Documentation
- [ ] Update README with macOS ARM64 build instructions
- [ ] Document RHI architecture for future contributors
- [ ] Document shader translation pipeline

---

## External Library Contributions

### Needed
- [ ] streflop: ARM64 FPCR control patches (if not already upstreamed)
- [ ] squish: NEON support or sse2neon compatibility

### Potential (based on issues found)
- [ ] sse2neon: Any edge cases from engine-specific SSE usage
- [ ] SPIRV-Cross: Any GLSL -> MSL translation bugs with RecoilEngine shaders
- [ ] glslang: Any GLSL parsing issues with engine shader conventions

---

## Notes

- Cross-architecture multiplayer (ARM64 vs x86_64) is explicitly out of scope for this effort. Same-architecture multiplayer is the target.
- The RHI should be minimal and engine-specific, not a general-purpose graphics abstraction.
- Each Phase 2 migration task should be a separate commit for easy bisection.
- Visual regression testing (screenshot comparison) should be done after each major subsystem migration.
