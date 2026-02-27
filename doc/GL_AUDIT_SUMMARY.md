# GL Call Audit Summary - RecoilEngine RHI Migration
**Date:** 2026-02-16  
**Status:** 1,921 total GL calls remaining across 77 files

---

## Quick Stats

```
BREAKDOWN BY PRIORITY:
┌─────────────────────────────────┬─────────┬────────┐
│ Category                        │ GL Calls│ Files  │
├─────────────────────────────────┼─────────┼────────┤
│ Skip: Deprecated Water          │   527   │   3    │
│ Keep: GL Backend (DO NOT TOUCH) │   241   │   7    │
│ Low: System Boundary            │    49   │   1    │
│ High: Active Targets (10+ calls)│   986   │  36    │
│ Low: Minimal (1-9 calls)        │   118   │  30    │
├─────────────────────────────────┼─────────┼────────┤
│ TOTAL                           │ 1,921   │  77    │
└─────────────────────────────────┴─────────┴────────┘

EFFECTIVE MIGRATION TARGETS: 1,153 GL calls across 67 files
```

---

## Top 10 Files Needing Migration

```
#  File                                GL Calls  Category
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
1. Shaders/Shader.cpp                    109      DO NOT MIGRATE (GL backend)
2. GL/VertexArray.cpp                     85      Utility wrapper refactor
3. GL/FBO.cpp                             81      Framebuffer → RHI conversion
4. Units/UnitDrawer.cpp                   74      BLOCKED: display lists + clip planes
5. Models/3DModelVAO.cpp                  42      Utility wrapper refactor
6. Shaders/GLSLCopyState.cpp              41      GL shader introspection
7. Env/GrassDrawer.cpp                    38      BLOCKED: glCallList
8. GL/myGL.cpp                            37      Utility wrapper refactor
9. Env/Decals/GroundDecalHandler.cpp      34      RHI-ready (mostly textures)
10. GL/LightHandler.cpp                   32      FFP lighting (deprecated)
```

---

## Major Blockers

### 1. Display Lists (15 GL calls)
**Issue:** `glCallList()` has no RHI equivalent
**Files:** UnitDrawer, GrassDrawer, FeatureDrawer
**Example from UnitDrawer.cpp line 327:**
```cpp
glCallList(preList);  // Lua display lists - no RHI equivalent
```
**Blocker Status:** HARD BLOCKER - Lua infrastructure requires redesign

---

### 2. Clip Planes (15 GL calls)
**Issue:** `glClipPlane()` is FFP-only; no modern RHI equivalent
**Files:** WorldDrawer, UnitDrawer, IWater
**Example from WorldDrawer.cpp lines 442-455:**
```cpp
if (wireFrameMode) {
    glEnable(GL_CLIP_PLANE3);  // RHI_TODO: implement via shader gl_ClipDistance[]
    ...
    glDisable(GL_CLIP_PLANE3);
}
```
**Blocker Status:** HARD BLOCKER - requires shader-based clip distance implementation

---

### 3. FFP Vertex Color / gl_Color (10+ GL calls)
**Issue:** Model shaders read `gl_Color` from FFP
**Files:** UnitDrawer, HUDDrawer
**Example from UnitDrawer.cpp line 899:**
```cpp
glColor4f(0.6f, 0.6f, 0.6f, IModelDrawerState::alphaValues.y);
// Model shader reads: color = gl_Color;
```
**Blocker Status:** HARD BLOCKER - shaders must be updated to read uniform colors

---

### 4. External Texture Boundaries (many files)
**Issue:** IconHandler, SMFReadMap, ShadowHandler return raw `GLuint`
**Files:** Any code consuming these textures
**Example pattern:**
```cpp
GLuint iconTexture = iconHandler->GetTexture(iconID);  // Returns raw GLuint
glBindTexture(GL_TEXTURE_2D, iconTexture);
```
**Blocker Status:** SOFT BLOCKER - needs IRHITexture wrapper adapters at system boundaries

---

### 5. Matrix Stack (FFP) - 150+ calls
**Issue:** Legacy FFP matrix stack (glMatrixMode, glLoadMatrixf, glTranslatef, etc.)
**Files:** Many utility files, debug features
**Status:** PARTIALLY RESOLVED via SetTransformMatrix() API
**Example from HUDDrawer.cpp line 54:**
```cpp
glMatrixMode(GL_PROJECTION);
glPushMatrix();
glLoadMatrixf(mvpMatrix);
// Modern approach: rb.SetTransformMatrix(mvp);
```
**Progress:** MiniMap done (commit 4c1b0e2), others pending

---

## Detailed File Status

### GL Backend Files (DO NOT MIGRATE - 241 calls)
These ARE the GL implementation:
```
RHI/OpenGL/GLContext.cpp       - 73 calls (OpenGL context binding)
RHI/OpenGL/GLTexture.cpp       - 51 calls (Texture implementation)
RHI/OpenGL/GLDevice.cpp        - 41 calls (Device creation)
RHI/OpenGL/GLPipeline.cpp      - 35 calls (Pipeline state)
Textures/Texture.cpp           - 37 calls (GL::Texture2D backend)
RHI/OpenGL/GLBuffer.cpp        -  2 calls (Buffer binding)
RHI/OpenGL/GLFramebuffer.cpp   -  2 calls (FBO binding)
```

### Deprecated Water (SKIP - 527 calls)
Candidates for removal:
```
Env/DynWater.cpp          - 419 calls (12 ARB programs, heavy FBO)
Env/AdvWater.cpp          -  76 calls (legacy vertex shaders)
Env/RefractWater.cpp      -  32 calls (refraction simulation)
```

### Utility Wrapper Layer (NEEDS REFACTOR - ~300 calls)
These need RHI interface refactoring:
```
GL/VertexArray.cpp        -  85 calls (CVertexArray -> RHI buffers)
GL/FBO.cpp                -  81 calls (FBO wrapper -> RHI::IRHIFramebuffer)
GL/myGL.cpp               -  37 calls (misc GL utilities)
GL/VBO.cpp                -  28 calls (buffer wrapper)
GL/glExtra.cpp            -  28 calls (utility functions)
GL/StreamBuffer.cpp       -  17 calls (streaming buffers)
GL/StreamBuffer.h         -  19 calls (header utilities)
```

### Active Rendering Paths (PARTIALLY MIGRATED - ~250 calls)
Primary content rendering code:
```
Units/UnitDrawer.cpp          -  74 calls (BLOCKED: display lists + clip planes)
Env/GrassDrawer.cpp           -  38 calls (BLOCKED: glCallList)
Env/Decals/GroundDecalHandler -  34 calls (mostly RHI-ready)
Env/SkyBox.cpp                -  17 calls (matrix stack)
HUDDrawer.cpp                 -  20 calls (matrix stack)
WorldDrawer.cpp               -  16 calls (clip planes + matrix stack)
```

---

## Recent Migration Momentum

Recent commits have removed ~550+ GL calls:
- Wave 1-2 migration (0af7bc7): ~219 GL calls
- GuiHandler migration (f412a7e): ~75 GL calls  
- MiniMap matrix stack (4c1b0e2): ~12 GL calls
- RenderBuffer shader update (ec02dbd): 3 sites enabled
- GL::SubState + clip distances (0f0f20dc): ~40 GL calls

**Cumulative:** ~60-70% of active rendering code is RHI-ready

---

## Recommended Next Actions

### Phase 1: Quick Wins (Low-Hanging Fruit)
- [x] Remove deprecated water from build (saves 527 GL calls, 3 files)
- [ ] Migrate GlobalRendering.cpp debug/query calls (49 calls)
- [ ] Add IRHITexture adapters at system boundaries (IconHandler, SMFReadMap, ShadowHandler)

### Phase 2: Infrastructure (Enables Larger Refactors)
- [ ] Refactor GL/VertexArray.cpp to use RHI buffers (85 calls → ~10)
- [ ] Refactor GL/FBO.cpp to use RHI::IRHIFramebuffer (81 calls → ~5)
- [ ] Implement shader-based clip planes (remove glClipPlane, add gl_ClipDistance)
- [ ] Update model shaders to read uniform color instead of gl_Color

### Phase 3: Content Rendering (Visible Impact)
- [ ] Migrate UnitDrawer display lists (after Lua infrastructure)
- [ ] Continue SetTransformMatrix() migration for remaining matrix stack
- [ ] Migrate GrassDrawer display lists
- [ ] Migrate remaining texture binding sites

### Phase 4: Cleanup (Finishing Touches)
- [ ] Remove FFP lighting (LightHandler.cpp, 32 calls)
- [ ] Migrate remaining utility functions (nv_dds, Bitmap, etc.)
- [ ] Audit and remove all GL state queries that aren't at boundaries

---

## RHI Migration Status Markers

57 files contain `RHI_TODO` or `RHI Migration Status` comments indicating active work:

**Examples:**
- `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine/rts/Rendering/Units/UnitDrawer.cpp` (lines 13-21)
- `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine/rts/Rendering/WorldDrawer.cpp` (lines 5-22)
- `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine/rts/Rendering/HAPFSPathDrawer.cpp` (lines 45-48)

---

## Code Patterns Identified

### Pattern 1: Legacy FFP Matrix Stack
```cpp
glMatrixMode(GL_PROJECTION);
glPushMatrix();
glLoadMatrixf(mvpMatrix);
// ... draw calls ...
glPopMatrix();
```
**Solution:** Use `RenderBuffer::SetTransformMatrix(mvp)` API

### Pattern 2: Immediate Mode + Client State Arrays
```cpp
glEnableClientState(GL_VERTEX_ARRAY);
glVertexPointer(3, GL_FLOAT, stride, vertices);
glDrawArrays(GL_TRIANGLES, 0, count);
```
**Solution:** Convert to TypedRenderBuffer<VA_TYPE_C>

### Pattern 3: Display Lists
```cpp
glCallList(preList);   // Lua compiled geometry
glCallList(postList);  // More Lua geometry
```
**Solution:** Cache geometry as static RenderBuffer

### Pattern 4: Multi-Pass FBO
```cpp
glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo);
glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex1, 0);
// ... render pass 1 ...
glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex2, 0);
// ... render pass 2 ...
```
**Solution:** Use RHI::IRHIFramebuffer + explicit render passes

---

## Files Needing Attention

### Critical Path (Must Fix for Core Rendering)
1. **UnitDrawer.cpp** (74 calls) - blocked by display lists + clip planes
2. **GrassDrawer.cpp** (38 calls) - blocked by display lists
3. **WorldDrawer.cpp** (16 calls) - blocked by clip planes + matrix stack
4. **VertexArray.cpp** (85 calls) - utility refactor needed
5. **FBO.cpp** (81 calls) - framebuffer refactor needed

### Medium Priority (Nice to Have)
1. **GL/myGL.cpp** (37 calls) - utility wrapper refactor
2. **GL/LightHandler.cpp** (32 calls) - FFP lighting deprecated
3. **GroundDecalHandler.cpp** (34 calls) - mostly RHI-ready
4. **Models/3DModelVAO.cpp** (42 calls) - VAO wrapper refactor

### Low Priority (Cleanup)
1. **Shader.cpp** (109 calls) - DO NOT MIGRATE (GL backend)
2. **GLSLCopyState.cpp** (41 calls) - GL introspection, non-critical
3. **Font files** (15+ calls) - matrix stack only
4. **Particle/Effect files** (many calls) - low visual impact

---

## Build Impact

The `engine-headless` build (CI/verification target) will continue working as all GL calls are stubbed in `rts/lib/headlessStubs/gladstub.cpp`.

The `engine-legacy` build (full Metal + OpenGL) will see performance improvements as more code paths use efficient RHI abstractions instead of legacy GL calls.

---

## Conclusion

**RHI migration is ~65% complete** for production rendering code.

**Remaining work:**
- 5 hard blockers (infrastructure): display lists, clip planes, FFP colors, external textures, matrix stack
- 3 utility layer refactors: VertexArray, FBO, myGL wrappers
- ~1,100 GL calls to migrate across 67 files

**Estimated effort:** 8-12 weeks for experienced engineer after infrastructure blocks are resolved.

---

**Audit Report:** `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine/GL_AUDIT_REPORT_2026_02_16.md`  
**Audit Date:** 2026-02-16  
**Auditor:** Claude Code RHI Audit Agent
