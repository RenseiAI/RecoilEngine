# ARM64 + Metal Compatibility Audit

This document catalogs every subsystem in RecoilEngine that is incompatible with ARM64 and/or Apple Metal, organized by severity and effort required.

## Summary

| Category | Files Affected | Severity |
|----------|---------------|----------|
| Direct OpenGL calls in rendering | 74 files, ~1105 calls | Critical |
| Direct OpenGL calls in Lua bindings | 13 files, ~250 calls | Critical |
| GLSL shaders (no MSL equivalent) | 41 shader files | Critical |
| x86 SSE intrinsics in engine code | 3 files | High |
| x86 SSE intrinsics in libraries | 3 files | Medium (lib-level) |
| streflop FPU control (x86 asm) | 1 file | High |
| x86 inline assembly | 2 files | Medium |
| Build system x86 assumptions | 4 files | High |
| simdjson ARM64 disabled | 1 file | Low |

---

## 1. SIMD / SSE Intrinsics

### 1.1 Engine Code (requires sse2neon or rewrite)

**`rts/System/FastMath.h`** - SSE math functions
- Lines 53-56: `isqrt_sse()` uses `_mm_set_ss`, `_mm_rsqrt_ss`, `_mm_cvtss_f32`
- Lines 69-71: `sqrt_sse()` uses `_mm_set_ss`, `_mm_sqrt_ss`, `_mm_cvtss_f32`
- Line 7: `#include <xmmintrin.h>` (now routed through `simd_compat.h` via PR #2540)
- **Status: Addressed by PR #2540** (sse2neon integration)

**`rts/System/Matrix44f.cpp`** - Matrix operations
- Uses `__m128` types for SIMD matrix math
- **Status: Addressed by PR #2540** (sse2neon)

**`rts/System/Threading/SpringThreading.h`** - Thread yield hint
- Uses `_mm_pause()` for spin-wait loops
- **Status: Addressed by PR #2540** (maps to ARM `yield` instruction via sse2neon)

**`rts/Sim/Misc/SmoothHeightMesh.cpp`** - Heightmap smoothing
- Uses `_mm_` intrinsics for vectorized heightmap processing
- **Status: Addressed by PR #2540** (sse2neon)

**`rts/Sim/Path/QTPFS/Path.h`** - Pathfinding
- Uses `__m128` for SIMD path calculations
- **Status: Addressed by PR #2540** (sse2neon)

### 1.2 Third-Party Libraries (need separate attention)

**`rts/lib/squish/simd_sse.h`** - DXT texture compression
- Line 29: `#include <xmmintrin.h>`
- Line 31: `#include <emmintrin.h>`
- Full SSE implementation of DXT compression/decompression
- **Status: NOT addressed.** Squish has its own SIMD abstraction but no NEON backend in this version. Needs update or replacement.

**`rts/lib/dr_mp3/dr_mp3.h`** - MP3 decoder
- Line 607: `#include <emmintrin.h>`
- Lines 627-712: x86 and ARM inline assembly for SIMD operations
- **Status: Partially addressed.** dr_mp3 has its own ARM NEON path (line 712 shows ARM `ssat` instruction). May work out of the box on ARM64.

**`rts/lib/xxhash/xxhash.h`** - Hash library
- Line 3125: `#include <immintrin.h>` (conditional on x86)
- Lines 3423, 4961: Has ARM64-specific assembly (`umlal`, `umaddl`)
- **Status: OK.** xxhash already supports ARM64 natively.

**`rts/lib/xsimd/`** - SIMD abstraction library
- Already includes NEON type support (`types/xsimd_neon_*.hpp`)
- **Status: OK.** Cross-platform by design.

### 1.3 Centralized SIMD Header (from PR #2540)

**`rts/System/simd_compat.h`** - New file from PR #2540
- Routes `#include` to either x86 intrinsics or sse2neon based on `__aarch64__`
- All engine .cpp files should include this instead of raw `<xmmintrin.h>`

**Remaining gap:** `rts/System/Misc/Win/FpuExceptions.h:20` still includes `<xmmintrin.h>` directly (Windows-only, low priority).

---

## 2. streflop / Floating-Point Determinism

### 2.1 x86 Inline Assembly

**`rts/lib/streflop/FPUSettings.h`** - FPU control word manipulation
- Lines 85-88: Function declarations for `__streflop_fstcw`, `__streflop_fldcw`, `__streflop_stmxcsr`, `__streflop_ldmxcsr`
- Lines 223-237: x86 FPU inline assembly macros (`fstcw`, `fldcw`, `stmxcsr`, `ldmxcsr`)
- Three modes: `STREFLOP_X87` (x87 FPU), `STREFLOP_SSE` (SSE), `STREFLOP_SOFT` (SoftFloat)
- **Resolution:** Use `STREFLOP_SOFT` on ARM64. SoftFloat is pure-software IEEE 754 using integer arithmetic. Already bundled in `rts/lib/streflop/softfloat/`.

### 2.2 Build System Flags

**`rts/lib/streflop/CMakeLists.txt`**
- Line 75: Hardcodes `-mfpmath=sse -msse` compiler flags
- **Resolution:** Skip these flags on ARM64, define `STREFLOP_SOFT` instead.

### 2.3 FPU State Validation

**`rts/System/Sync/FPUCheck.cpp`** - Runtime FPU state verification
- Validates x86 FPU control word and MXCSR register states
- **Resolution:** On ARM64, validate FPCR (Floating-Point Control Register) instead.

**`rts/System/ScopedFPUSettings.h`** - Scoped FPU state save/restore
- **Resolution:** ARM64 equivalent using FPCR read/write.

### 2.4 FMA (Fused Multiply-Add) Concern

ARM64 processors aggressively use FMA instructions. FMA produces different results from separate multiply+add (more precise, but different bits). This can cause multiplayer desyncs.

- **Resolution:** Compile with `-ffp-contract=off` on ARM64 to prevent automatic FMA generation.

---

## 3. x86 Inline Assembly

**`rts/System/Platform/CpuID.cpp`**
- Lines 38-56: Three `__asm__ __volatile__` blocks with x86 `cpuid` instruction
- **Status: Partially addressed.** The file is conditionally compiled (`#if !defined(__aarch64__)`). PR #2540's CpuTopology rework handles ARM64 CPU detection via sysfs.

**`rts/System/Platform/Win/CrashHandler.cpp`**
- Lines 244-246: x86 `call`, `pop eax`, `mov ebp/esp` for stack frame capture
- **Status: Windows-only.** Not relevant for macOS ARM64 port.

---

## 4. Build System x86 Assumptions

### 4.1 Architecture Detection

**`CMakeLists.txt`** (root)
- Lines 112-116: 64-bit check only recognizes x86_64 (`CMAKE_SIZEOF_VOID_P EQUAL 8`)
- Lines 429-476: SSE compiler flags enforced globally (`-msse`, `-mno-sse3`, `-mno-avx`, etc.)
- Line 505: `-fsingle-precision-constant -frounding-math` (may need adjustment for ARM64)
- **Resolution:** Add `aarch64`/`arm64` as valid 64-bit architecture. Skip SSE flags on ARM64.

### 4.2 Compiler Flag Detection

**`rts/build/cmake/TestCXXFlags.cmake`**
- Lines 77-93: March detection tries `-march=x86-64` only
- Lines 48-62: IEEE FP flag `-mieee-fp` is x86-only (GCC)
- **Resolution:** On ARM64, use `-march=armv8-a` or omit for Apple Silicon native. Replace `-mieee-fp` with `-ffp-model=strict` or omit (ARM64 is IEEE by default).

### 4.3 Main Definitions

**`rts/System/MainDefines.h`**
- Line 26: Already recognizes `__aarch64__` for `__is_x86_arch__ = 0` (from PR #2540)
- Line 36: 64-bit detection may need `__aarch64__` added
- **Status: Partially addressed by PR #2540.**

### 4.4 simdjson ARM64 Disabled

**`rts/lib/CMakeLists.txt`**
- Line ~80: simdjson forced to fallback implementation with ARM64 disabled
- **Resolution:** Remove the ARM64 exclusion. simdjson natively supports ARM64 NEON.

---

## 5. OpenGL Rendering (No Metal Backend)

This is the largest category. The engine has **no rendering abstraction layer** -- OpenGL calls are made directly throughout the codebase.

### 5.1 GL Wrapper Classes (core rendering infrastructure)

These files encapsulate OpenGL objects but expose GL-specific APIs:

| File | Purpose | GL Calls |
|------|---------|----------|
| `rts/Rendering/GL/VBO.h/cpp` | Vertex/Index buffers | glGenBuffers, glBindBuffer, glBufferData, glMapBufferRange |
| `rts/Rendering/GL/FBO.h/cpp` | Framebuffer objects | glGenFramebuffers, glBindFramebuffer, glFramebufferTexture2D, glBlitFramebuffer |
| `rts/Rendering/GL/VAO.h/cpp` | Vertex array objects | glGenVertexArrays, glBindVertexArray |
| `rts/Rendering/GL/State.h` | GL state tracking | glEnable, glDisable, glBlendFunc, glDepthFunc, etc. |
| `rts/Rendering/GL/StreamBuffer.h/cpp` | Streaming vertex data | 6 strategies wrapping GL buffer operations |
| `rts/Rendering/GL/RenderBuffers.h` | Draw dispatch + shader gen | glDrawElements, glDrawArrays, runtime GLSL generation |
| `rts/Rendering/GL/TexBind.h` | Texture binding RAII | glBindTexture, glActiveTexture |
| `rts/Rendering/GL/GeometryBuffer.cpp` | G-buffer (deferred) | FBO with multiple render targets |
| `rts/Rendering/GL/VertexArray.cpp` | Legacy vertex arrays | 56 direct GL calls, immediate-mode emulation |
| `rts/Rendering/GL/glHelpers.h` | GL utility functions | Various GL helpers |
| `rts/Rendering/GL/glExtra.h/cpp` | GL drawing utilities | 24 direct GL calls |
| `rts/Rendering/GL/glStateDebug.h/cpp` | GL debug state | 30 GL state query calls |
| `rts/Rendering/GL/LightHandler.cpp` | Lighting UBOs | 6 GL calls |
| `rts/Rendering/GL/myGL.h/cpp` | GL includes and helpers | Core GL include, legacy wrappers |

### 5.2 Rendering Subsystems (heavy direct GL usage)

| File | Purpose | GL Call Count |
|------|---------|---------------|
| `rts/Rendering/Env/DynWater.cpp` | Dynamic water rendering | 170 |
| `rts/Rendering/Env/BumpWater.cpp` | Bump-mapped water | 63 |
| `rts/Rendering/Env/GrassDrawer.cpp` | Grass rendering | 58 |
| `rts/Rendering/Units/UnitDrawer.cpp` | Unit rendering | 54 |
| `rts/Rendering/Env/Particles/ProjectileDrawer.cpp` | Projectile/particle rendering | 44 |
| `rts/Rendering/Env/AdvWater.cpp` | Advanced water | 35 |
| `rts/Rendering/Common/ModelDrawerHelpers.cpp` | Model draw helpers | 37 |
| `rts/Rendering/Env/SkyBox.cpp` | Skybox rendering | 27 |
| `rts/Rendering/ShadowHandler.cpp` | Shadow mapping | 26 |
| `rts/Rendering/WorldDrawer.cpp` | Top-level render coord | 26 |
| `rts/Rendering/Fonts/glFontRenderer.cpp` | Font rendering | 26 |
| `rts/Rendering/GL/glExtra.cpp` | GL drawing utilities | 23 |
| `rts/Rendering/GL/myGL.cpp` | GL initialization | 20 |
| `rts/Rendering/GL/FBO.cpp` | Framebuffer operations | 24 |
| `rts/Rendering/Env/Decals/GroundDecalHandler.cpp` | Ground decals | 18 |
| `rts/Rendering/Models/3DModelVAO.cpp` | Model VAO rendering | 18 |
| `rts/Rendering/GlobalRendering.cpp` | Window/context management | 16 |
| `rts/Rendering/Textures/Texture.cpp` | Texture management | 15 |
| `rts/Rendering/Textures/Bitmap.cpp` | Bitmap/texture loading | 13 |
| `rts/Rendering/Env/RefractWater.cpp` | Refraction water | 13 |
| `rts/Rendering/Shaders/Shader.cpp` | Shader compilation | 9 |
| `rts/Rendering/Env/CubeMapHandler.cpp` | Cubemap management | 20 |
| `rts/Rendering/Env/ModernSky.cpp` | Modern sky renderer | 6 |

### 5.3 Lua OpenGL Bindings

| File | Purpose | GL Call Count |
|------|---------|---------------|
| `rts/Lua/LuaOpenGL.cpp` | Core Lua GL API (~70+ functions) | 146 |
| `rts/Lua/LuaTextures.cpp` | Lua texture management | 26 |
| `rts/Lua/LuaFBOs.cpp` | Lua FBO management | 23 |
| `rts/Lua/LuaVAOImpl.cpp` | Lua VAO/VBO operations | 15 |
| `rts/Lua/LuaShaders.cpp` | Lua shader creation | 13 |
| `rts/Lua/LuaOpenGLUtils.cpp` | Lua GL utilities | 13 |
| `rts/Lua/LuaRBOs.cpp` | Lua render buffer objects | 5 |
| `rts/Lua/LuaFonts.cpp` | Lua font rendering | 2 |
| `rts/Lua/LuaMaterial.cpp` | Lua material system | 2 |
| `rts/Lua/LuaVBOImpl.h/cpp` | Lua VBO implementation | 3 |
| `rts/Lua/LuaDisplayLists.h` | Lua display lists | 1 |
| `rts/Lua/LuaParser.cpp` | Lua parser GL context | 1 |

### 5.4 Shader System

**`rts/Rendering/Shaders/Shader.h/cpp`** - GLSL-only shader pipeline
- `ARBShaderObject` - Legacy ARB assembly shaders
- `GLSLShaderObject` - GLSL shader objects
- `GLSLProgramObject` - GLSL program linking/binding
- Runtime shader compilation via `glCreateShader`, `glCompileShader`, `glLinkProgram`
- Hash-based shader cache

**`rts/Rendering/Shaders/ShaderHandler.h`** - Shader program manager
- Singleton managing all shader programs
- No abstraction for alternative shader languages

### 5.5 OpenGL Function Loader

**`rts/lib/glad/`** - GLAD OpenGL loader
- `glad.c/glad.h` - Core OpenGL function pointers
- `glad_glx.c/glad_glx.h` - X11 GLX extension (Linux)
- `rts/lib/headlessStubs/gladstub.cpp` - Headless mode stubs
- **Not applicable to Metal.** Metal uses its own framework APIs.

---

## 6. GLSL Shader Files

41 GLSL shader files in `cont/base/springcontent/shaders/GLSL/`:

### Core Rendering (highest priority for Metal port)
- `ModelVertProg.glsl` / `ModelFragProg.glsl` - Unit/feature rendering
- `ModelVertProgGL4.glsl` / `ModelFragProgGL4.glsl` - GL4 model path
- `SMFVertProg.glsl` / `SMFFragProg.glsl` - Map terrain
- `SMFBorderVertProg.glsl` / `SMFBorderFragProg.glsl` - Map border
- `ShadowGenVertProg.glsl` / `ShadowGenFragProg.glsl` - Shadow maps
- `ShadowGenVertProgGL4.glsl` / `ShadowGenFragProgGL4.glsl` - GL4 shadows
- `ShadowGenVertMapProg.glsl` - Map shadow generation

### Environment
- `BumpWaterVS.glsl` / `BumpWaterFS.glsl` - Water rendering
- `BumpWaterCoastBlurVS.glsl` / `BumpWaterCoastBlurFS.glsl` - Coast blur
- `GrassVertProg.glsl` / `GrassFragProg.glsl` - Grass
- `ModernSkyVS.glsl` / `ModernSkyFS.glsl` - Sky rendering
- `GroundDecalsVertProg.glsl` / `GroundDecalsFragProg.glsl` - Ground decals
- `CubeMapVS.glsl` / `CubeMapFS.glsl` - Environment cubemaps
- `EquiRectConverterFS.glsl` - Equirectangular conversion

### Particles/Effects
- `ProjFXVertProg.glsl` / `ProjFXFragProg.glsl` - Projectile effects
- `ProjFXVertShadowProg.glsl` / `ProjFXFragShadowProg.glsl` - Projectile shadows

### UI/HUD
- `Icons2DVS.glsl` / `Icons3DVS.glsl` / `IconsFS.glsl` - Unit icons
- `MiniMapVertProg.glsl` / `MiniMapFragProg.glsl` - Minimap
- `ShapesVertProg.glsl` / `ShapesFragProg.glsl` - Debug shapes

### Map Info Textures
- `SMFShadingTextureVertProg.glsl` / `SMFShadingTextureFragProg.glsl` - Shading textures

### Utility
- `FullscreenTriangleVS.glsl` / `FullscreenTriangleTexFS.glsl` - Fullscreen pass

### Translation Approach
All shaders will be translated via: **GLSL -> glslang -> SPIR-V -> SPIRV-Cross -> MSL**

---

## 7. macOS-Specific Gaps

### 7.1 Window/Context Management
**`rts/Rendering/GlobalRendering.cpp`** - Creates SDL GL context
- `SDL_GL_CreateContext()` - GL-specific, needs `SDL_Metal_CreateView()` for Metal
- `SDL_GL_SetSwapInterval()` - Metal uses `CAMetalLayer.displaySyncEnabled`
- `SDL_GL_SwapWindow()` - Metal uses `MTLCommandBuffer.present()`

### 7.2 Missing macOS Platform Code
**`rts/System/Platform/Mac/`** - Existing but minimal:
- `CrashHandler.cpp` - Crash handling (OK)
- `MessageBox.cpp` - Native dialogs (OK)
- `Signal.cpp/Signal.h` - Signal handling (OK)
- `SDLMain.h` - Cocoa integration (OK)
- `WindowManagerHelper.cpp` - Window management (OK)
- **Missing:** CPU topology detection for Apple Silicon (sysctl-based)
- **Missing:** Metal framework integration

### 7.3 Framework Dependencies
Currently links: `CoreFoundation`, `SDL2`, `OpenGL`
Needs to add: `Metal`, `QuartzCore` (for `CAMetalLayer`), optionally `MetalKit`

---

## 8. Third-Party Library Compatibility

| Library | ARM64 Status | Action Needed |
|---------|-------------|---------------|
| SDL2 | OK (supports ARM64 + Metal) | None |
| GLAD | N/A (OpenGL-only) | Replace with Metal framework on macOS |
| assimp | OK (cross-platform) | None |
| fastgltf | OK (cross-platform) | None |
| Lua | OK (pure C) | None |
| DevIL | Needs verification | Test on ARM64 macOS |
| Freetype | OK (cross-platform) | None |
| OpenAL | OK on macOS | None |
| Tracy | OK (cross-platform) | None |
| streflop | NOT OK | Use STREFLOP_SOFT mode on ARM64 |
| squish | NOT OK (SSE-only SIMD) | Update to version with NEON or use sse2neon |
| simdjson | Disabled on ARM64 | Re-enable native ARM64 path |
| xsimd | OK (has NEON support) | None |
| xxhash | OK (has ARM64 support) | None |
| dr_mp3 | OK (has ARM NEON path) | None |
| RmlUi | Uses GL3 renderer | Needs Metal renderer or RHI integration |

---

## 9. Existing ARM64 Support (from PR #2540)

PR #2540 (Chainfire/Asahi Linux) already addresses:
- [x] sse2neon integration via `simd_compat.h`
- [x] ARM64 CPU topology detection (Linux sysfs)
- [x] streflop ARM64 awareness (build system)
- [x] CpuID conditional compilation
- [x] `__aarch64__` guards on x86 inline assembly
- [x] Threading `_mm_pause()` -> ARM yield mapping

Still needed for macOS Metal:
- [ ] Metal rendering backend (RHI)
- [ ] GLSL -> MSL shader translation pipeline
- [ ] Lua GL API -> RHI migration
- [ ] macOS ARM64 build system support
- [ ] macOS CPU topology (sysctl, not sysfs)
- [ ] streflop STREFLOP_SOFT mode for ARM64
- [ ] FMA prevention (`-ffp-contract=off`)
- [ ] squish library NEON support
- [ ] RmlUi Metal renderer
- [ ] DevIL ARM64 macOS verification
