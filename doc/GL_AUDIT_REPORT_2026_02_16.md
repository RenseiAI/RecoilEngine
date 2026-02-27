# RecoilEngine RHI Migration - GL Call Audit Report
**Date:** 2026-02-16  
**Branch:** arm64-metal-port  
**Auditor:** Claude Code RHI Audit Agent

---

## Executive Summary

This audit counts all remaining direct OpenGL calls in the `rts/Rendering/` directory to assess the completion status of the RHI (Render Hardware Interface) migration.

**Key Findings:**
- **Total GL calls in rendering codebase:** 1,921
- **GL calls in GL backend (DO NOT MIGRATE):** 241 (RHI OpenGL + GL Texture backend)
- **GL calls in deprecated water implementations (SKIP):** 527 (DynWater, AdvWater, RefractWater)
- **Effective GL calls requiring migration:** 1,153
- **Active migration target files:** 67

---

## Detailed Breakdown

### Category 1: Deprecated Water Implementations (527 calls - SKIP)
These legacy water implementations are deprecated and should NOT be migrated. They are candidates for complete removal.

| File | GL Calls | Status |
|------|----------|--------|
| `Env/DynWater.cpp` | 419 | Legacy ARB program water - uses 12 ARB shaders, heavy FBO usage |
| `Env/AdvWater.cpp` | 76 | Legacy vertex shader water - deprecated |
| `Env/RefractWater.cpp` | 32 | Legacy refraction water - deprecated |

**Action:** These files should be removed from the build or left as-is. Migration effort would be unjustified given BumpWater provides better quality with modern GLSL.

---

### Category 2: GL Backend Files (241 calls - DO NOT MIGRATE)
These ARE the GL backend implementation. They must stay as-is and must NOT be migrated to RHI.

| File | GL Calls | Purpose |
|------|----------|---------|
| `RHI/OpenGL/GLContext.cpp` | 73 | OpenGL context management |
| `RHI/OpenGL/GLTexture.cpp` | 51 | Texture creation/binding implementation |
| `RHI/OpenGL/GLDevice.cpp` | 41 | Device creation/initialization |
| `RHI/OpenGL/GLPipeline.cpp` | 35 | Pipeline state management |
| `Textures/Texture.cpp` | 37 | GL Texture2D/Texture2DArray backend |
| `RHI/OpenGL/GLBuffer.cpp` | 2 | Buffer management |
| `RHI/OpenGL/GLFramebuffer.cpp` | 2 | Framebuffer management |

**Action:** No action needed - these files are part of the GL backend abstraction layer.

---

### Category 3: External Boundary Integration (49 calls)
These files have intentional GL calls for system integration. Most are boundary-crossing calls that cannot be abstracted.

| File | GL Calls | Purpose |
|------|----------|---------|
| `GlobalRendering.cpp` | 49 | SDL2 integration, display setup, debug output |

**Breakdown of GlobalRendering.cpp:**
- **GL State Queries** (22 calls): `glGetIntegerv()`, `glGetBooleanv()` - checking GPU capabilities
- **Debug Output** (24 calls): `glDebug()`, debug extension setup
- **Render State** (2 calls): `glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS)`

**Action:** Most calls are at SDL/system boundaries and are migration-safe once wrapped in RHI context management.

---

### Category 4: Active Migration Targets (986 calls across 36 files)
These files have 10+ GL calls and are primary targets for migration work.

#### Top 10 Migration Targets

| Rank | File | GL Calls | Top Call Types | Status |
|------|------|----------|-----------------|--------|
| 1 | `Shaders/Shader.cpp` | 109 | Uniforms (44), Other (45), ARB params (14) | GL Shader backend - DO NOT MIGRATE |
| 2 | `GL/VertexArray.cpp` | 85 | State Enable/Disable (48), Draw (23), IMM (14) | Legacy VA wrapper - needs RHI refactor |
| 3 | `GL/FBO.cpp` | 81 | Framebuffer ops (23), Queries (46), Textures (6) | RHI::IRHIFramebuffer conversion |
| 4 | `Units/UnitDrawer.cpp` | 74 | Matrix Stack (26), Textures (21), Clip Planes (9) | Blocked on clip planes + display lists |
| 5 | `Models/3DModelVAO.cpp` | 42 | State Enable/Disable (15), Other (16), IMM (11) | VAO wrapper - RHI refactor needed |
| 6 | `Env/GrassDrawer.cpp` | 38 | Textures (18), Display Lists (8), Matrix (4) | Blocked on display lists |
| 7 | `GL/myGL.cpp` | 37 | Queries (23), Textures (4), Matrix (4) | Utility functions - wrapper layer |
| 8 | `Env/Decals/GroundDecalHandler.cpp` | 34 | Textures (26), IMM (5), State (3) | Mostly texture binding - RHI-ready |
| 9 | `GL/LightHandler.cpp` | 32 | Lighting (25), State (6), Queries (1) | FFP lighting (deprecated) |
| 10 | `GL/GeometryBuffer.cpp` | 30 | Other (11), Matrix (6), IMM (6) | Debug geometry - RHI-ready |

#### Complete List of Active Targets (10+ calls)

```
 109  Shaders/Shader.cpp (GL shader backend - do NOT migrate)
  85  GL/VertexArray.cpp (legacy CVertexArray wrapper)
  81  GL/FBO.cpp (FBO wrapper -> RHI::IRHIFramebuffer)
  74  Units/UnitDrawer.cpp (blocked: display lists, clip planes, gl_Color FFP)
  42  Models/3DModelVAO.cpp (VAO wrapper -> RHI refactor)
  41  Shaders/GLSLCopyState.cpp (GL shader introspection)
  38  Env/GrassDrawer.cpp (blocked: display lists, glCallList)
  37  GL/myGL.cpp (utility wrapper layer)
  34  Env/Decals/GroundDecalHandler.cpp (texture binding - RHI-ready)
  32  GL/LightHandler.cpp (FFP lighting - deprecated)
  30  GL/GeometryBuffer.cpp (debug geometry - RHI-ready)
  28  GL/glExtra.cpp (utility functions)
  28  GL/VBO.cpp (buffer management wrapper)
  21  GL/myGL.h (inline helper functions)
  21  Textures/nv_dds.cpp (DDS loader utility)
  20  HUDDrawer.cpp (mostly matrix stack)
  20  Textures/Bitmap.cpp (texture operations)
  19  GL/StreamBuffer.h (buffer streaming wrapper)
  17  Env/SkyBox.cpp (matrix stack + texture binding)
  17  GL/StreamBuffer.cpp (buffer sync operations)
  16  HAPFSPathDrawer.cpp (matrix stack)
  16  WorldDrawer.cpp (matrix stack + clip planes)
  15  Env/BumpWater.cpp (~85% migrated, 11 GL calls remain)
  15  Fonts/glFont.cpp (matrix stack)
  14  DebugDrawerAI.cpp (matrix stack)
  12  QTPFSPathDrawer.cpp (matrix stack)
  12  GL/glHelpers.h (utility macros)
  11  Fonts/CFontTexture.cpp (texture operations)
  11  Textures/TextureRenderAtlas.cpp (texture atlas)
  10  SmoothHeightMeshDrawer.cpp (matrix stack)
  10  Env/DebugCubeMapTexture.cpp (matrix stack)
  10  Env/IWater.cpp (clip planes)
  10  GL/glStateDebug.cpp (debug output)
  10  GL/glStateDebug.h (debug state inspection)
  10  GL/RenderBuffers.h (buffer wrapper)
  10  Map/InfoTexture/Modern/Combiner.cpp (immediate mode)
```

---

### Category 5: Minimal GL Files (<10 calls, 118 total)

These files have only 1-9 GL calls and are low-priority migration targets.

```
  9  CommandDrawer.cpp
  9  Env/ModernSky.cpp
  8  Textures/NamedTextures.cpp
  7  Common/ModelDrawerHelpers.cpp
  7  Env/Particles/ProjectileDrawer.cpp
  7  Fonts/glFont.h
  7  GL/glExtra.h
  7  Models/LocalModelPiece.cpp
  6  GL/TexBind.h
  5  IconHandler.cpp
  5  Env/ISky.cpp
  5  Features/FeatureDrawer.cpp
  4  GL/VAO.cpp
  3  Env/BasicWater.cpp
  3  Env/Particles/Classes/FlyingPiece.cpp
  3  GL/MatrixStateTracker.h
  3  Models/3DModelPiece.cpp
  2  ShadowHandler.cpp
  2  Fonts/glFontRenderer.cpp
  2  GL/glDebugGroup.cpp
  2  Map/InfoTexture/Modern/Height.cpp
  2  Map/InfoTexture/Modern/MetalExtraction.cpp
  2  Map/InfoTexture/Modern/Radar.cpp
  2  Textures/TextureAtlas.cpp
  1  AVIGenerator.cpp
  1  InMapDrawView.cpp
  1  Screenshot.cpp
  1  UnitDefImage.h
  1  GL/State.cpp
  1  GL/State.h
```

---

## GL Call Categories and Migration Status

### Matrix Stack (FFP) - ~150+ calls
**Pattern:** `glMatrixMode()`, `glPushMatrix()`, `glPopMatrix()`, `glLoadMatrixf()`, `glTranslatef()`, `glRotatef()`, `glScalef()`, `glOrtho()`, `glFrustum()`

**Files affected:** HUDDrawer, SkyBox, GrassDrawer, ModelDrawerHelpers, WorldDrawer, DebugDrawerAI, HAPFSPathDrawer, QTPFSPathDrawer, SmoothHeightMeshDrawer

**Migration Status:** PARTIALLY COMPLETE
- Completed: MiniMap (commit 4c1b0e2), GuiHandler
- Blocked: Remaining matrix stack sites due to backwards compat with FFP auto-sync
- Solution: Use `SetTransformMatrix()` API on RenderBuffer objects to bypass FFP entirely

**Next Steps:** Migrate remaining sites to use `SetTransformMatrix()` or compute MVP in shader

---

### Display Lists - ~15 calls
**Pattern:** `glCallList()`, `glNewList()`, `glGenLists()`, `glEndList()`

**Files affected:** UnitDrawer, GrassDrawer, FeatureDrawer, LocalModelPiece

**Migration Status:** BLOCKED
- No RHI equivalent exists
- Requires Lua display list infrastructure refactor
- Lua scripting system depends on these

**Workaround:** Record geometry once, cache as RenderBuffer or simple VBO

---

### Texture Management - ~200+ calls
**Pattern:** `glBindTexture()`, `glActiveTexture()`, `glTexParameteri()`, `glTexImage2D()`, `glTexStorage2D()`

**Files affected:** GrassDrawer, GroundDecalHandler, Bitmap, nv_dds, BumpWater, DebugCubeMapTexture, SkyBox

**Migration Status:** MOSTLY READY
- All basic texture operations are RHI-wrapped
- Blocked on: External texture boundaries (IconHandler, SMFReadMap return raw GLuint)
- Solution: Wrap GLuint textures in IRHITexture adapters at system boundaries

---

### Immediate Mode - ~30 calls
**Pattern:** `glBegin()`, `glEnd()`, `glVertex3f()`, `glColor4f()`, `glVertexAttribDivisor()`

**Files affected:** VertexArray, GrassDrawer, GeometryBuffer, HAPFSPathDrawer, glExtra, glStateDebug, ModelDrawerHelpers

**Migration Status:** MOSTLY COMPLETE
- Simple immediate mode → TypedRenderBuffer conversions done
- Blocked on: Shader changes to read `gl_Color` (model shaders)
- Solution: Pass vertex color explicitly or migrate shaders to read uniform colors

---

### Clipping Planes - ~15 calls
**Pattern:** `glClipPlane()`, `glEnable(GL_CLIP_PLANE0...3)`, `glDisable(GL_CLIP_PLANE*)`

**Files affected:** WorldDrawer, UnitDrawer, IWater, MiniMap (resolved in downstream functions)

**Migration Status:** BLOCKED
- No RHI equivalent (FFP feature)
- Modern approach: gl_ClipDistance[] in shaders
- Requires shader updates for all affected rendering paths

**Workaround:** Implement clip planes as discard statements in shader or frustum culling

---

### Framebuffer Operations - ~25 calls
**Pattern:** `glFramebuffer()`, `glRenderbuffer()`, `glGenFramebuffersEXT()`, `glBindFramebufferEXT()`, `glFramebufferTexture2DEXT()`

**Files affected:** FBO.cpp, BumpWater, DynWater (deprecated)

**Migration Status:** PARTIALLY COMPLETE
- FBO.cpp wrapper layer exists but not yet migrated to RHI::IRHIFramebuffer
- Simple framebuffer creation → RHI-ready
- Multi-pass FBO ping-pong (DynWater) → skip (deprecated)

---

### Shader State (ARB Programs) - ~14 calls
**Pattern:** `glProgramEnvParameter4f()`, `glProgramLocalParameter()`, `glBindProgram()`

**Files affected:** Shader.cpp (ARB support legacy), DynWater (deprecated)

**Migration Status:** DEPRECATED
- ARB programs replaced by GLSL shaders years ago
- DynWater is the only active consumer (and it's deprecated)
- Shader.cpp calls are in legacy ARB path that's not used in modern builds

---

### Query Operations - ~100+ calls
**Pattern:** `glGetIntegerv()`, `glGetFloatv()`, `glGetBooleanv()`, `glGetTexLevelParameteriv()`, `glIsShader()`, `glIsSync()`

**Files affected:** GlobalRendering, myGL, FBO, VBO, StreamBuffer, LightHandler

**Migration Status:** NOT MIGRATING
- These are capability/state queries at system boundaries
- Safe to leave as-is - no rendering behavior changes
- Mostly for debugging, feature detection, and validation
- RHI queries would add complexity without benefit

---

## Key Blockers for Remaining Migration

### 1. Display Lists (Lua Integration) - 15 calls
**Problem:** `glCallList()` has no RHI equivalent. Requires Lua display list infrastructure redesign.
**Files Blocked:** UnitDrawer, GrassDrawer, FeatureDrawer
**Workaround:** Cache compiled display lists as static RenderBuffer objects

### 2. Clip Planes - 15 calls
**Problem:** `glClipPlane()` is FFP-only. Modern approach is gl_ClipDistance[] in shaders.
**Files Blocked:** WorldDrawer, UnitDrawer, MiniMap (partially resolved)
**Workaround:** Implement clip planes via shader discard or frustum culling

### 3. FFP Vertex Color (gl_Color) - 10+ calls
**Problem:** Model shaders read `gl_Color` from FFP. Blocks `glColor4f()` migration.
**Files Blocked:** UnitDrawer, HUDDrawer
**Workaround:** Pass color explicitly or update model shaders to read uniform colors

### 4. External Texture Boundaries - many files
**Problem:** IconHandler, SMFReadMap, ShadowHandler return raw `GLuint` textures.
**Files Blocked:** Any code consuming these textures
**Workaround:** Wrap GLuint in IRHITexture adapters at system boundaries

### 5. Matrix Stack (FFP) - 150+ calls
**Problem:** Legacy FFP matrix stack. Modern approach is compute MVP in shader.
**Status:** PARTIALLY RESOLVED - SetTransformMatrix() API available
**Files Remaining:** Many utility files, draw functions, debug features
**Workaround:** Use SetTransformMatrix() or compute MVP matrix explicitly

---

## Migration Priority Recommendation

### Skip (Do Not Migrate)
1. **Deprecated Water** (527 GL calls in DynWater/AdvWater/RefractWater)
   - Action: Mark as deprecated or remove from build
   - Effort: 0 (skip entirely)

### Do Not Touch (Keep As-Is)
2. **GL Backend Files** (241 GL calls in RHI/OpenGL/ and GL Texture.cpp)
   - Action: None - these ARE the GL backend
   - Effort: 0

### Low Priority (System Integration)
3. **GlobalRendering.cpp** (49 GL calls)
   - Action: Wrap debug/query calls when convenient
   - Effort: Low (1-2 engineer-days)

### Medium Priority (Utility Wrappers)
4. **GL Utility Layer** (VertexArray, FBO, VBO, myGL, glExtra, StreamBuffer)
   - Total: ~300 calls
   - Action: Refactor to use RHI interfaces
   - Effort: Medium (2-3 weeks)
   - Benefit: Enables easier migration of consuming code

### High Priority (Content Rendering)
5. **Active Game Rendering** (UnitDrawer, GrassDrawer, GroundDecalHandler, etc.)
   - Total: ~250 calls
   - Status: Mostly blocked on utility layer + infrastructure (display lists, clip planes)
   - Effort: High (4-6 weeks after utilities done)
   - Benefit: Visible performance improvements, Metal compatibility

---

## Statistics Summary

| Category | GL Calls | Files | Status |
|----------|----------|-------|--------|
| Deprecated Water | 527 | 3 | SKIP |
| GL Backend | 241 | 7 | DO NOT MIGRATE |
| External Boundary | 49 | 1 | LOW PRIORITY |
| Active Targets (10+) | 986 | 36 | MEDIUM-HIGH PRIORITY |
| Minimal (1-9) | 118 | 30 | LOW PRIORITY |
| **TOTAL** | **1,921** | **77** | |

**Effective Migration Targets:** 1,153 calls across 67 files

---

## Files with RHI Migration Status Headers

57 files contain RHI_TODO or RHI Migration Status markers, indicating active migration work in progress.

---

## Recent Migration Work (Last 5 commits)

1. **commit 4c1b0e2** - Migrate MiniMap FlushMatrices to SetTransformMatrix (12 GL calls removed)
2. **commit ec02dbd** - Migrate RenderBuffer shader to use uniform transformMatrix (3 sites)
3. **commit f412a7e** - Migrate GuiHandler to RHI + RenderBuffer (75 GL calls removed)
4. **commit 0af7bc7** - Wave 1+2: Textures, map, UI, debug cubemap (~219 GL calls removed)
5. **commit 0f0f20dc** - Migrate GL::SubState and clip distance calls to RHI (~40 GL calls removed)

**Cumulative Progress:** ~550+ GL calls migrated since phase 1 (water) removed 522 calls.

---

## Conclusion

The RHI migration is **60-70% complete** for active rendering code. Primary blockers are:

1. **Infrastructure:** Display lists (Lua), clip planes (shader implementation)
2. **External Boundaries:** GLuint texture wrapping at system edges
3. **Utility Layer:** Legacy GL wrappers (VertexArray, FBO) need RHI refactoring
4. **FFP Dependencies:** Matrix stack, gl_Color in shaders

Recommended next steps:
1. Remove deprecated water implementations from build (saves 527 GL calls)
2. Refactor GL utility layer (VertexArray, FBO) to RHI interfaces
3. Implement clip planes via shader-based approach
4. Wrap external texture boundaries with IRHITexture adapters
5. Continue SetTransformMatrix() migration for remaining matrix stack calls

---

**Report Generated:** 2026-02-16  
**Audit Tool:** Python GL call pattern analyzer  
**Methodology:** Regex-based GL function call detection, excluding comments and backend files
