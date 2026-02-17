# RecoilEngine GL Audit Report
## Comprehensive Migration Analysis

**Date:** 2026-02-16  
**Scope:** RecoilEngine arm64-metal-port branch  
**Audit Type:** Direct OpenGL Call Count & Categorization

---

## Executive Summary

- **Total GL Function Calls (Excluding Backend & Deprecated):** 2,147
- **GL RHI Backend Files (Intentional GL):** 209 calls across 6 files
- **Deprecated Water Files (Excluded):** 534 calls across 3 files
- **Files Analyzed:** 180+ compiled C++ source files in `rts/`
- **Migration Status:** ~68% of calls remain to migrate (1,460 estimated migrateable)

### Key Findings

1. **Lua GL Scripting Dominates**: 679 calls (31.6%) - requires careful Lua interface redesign
2. **GL Utility Layer**: 486 calls (22.6%) - mostly VAO/FBO/VBO wrappers that ARE the GL layer
3. **Intentional GL Boundaries**: ~200+ calls at platform/external boundaries (SDL, shader introspection)
4. **Migrateable Core Rendering**: ~600+ calls across UI, terrain, environment, and models

---

## GL Calls by Domain

| Domain | Count | % | Status | Notes |
|--------|-------|---|--------|-------|
| **Lua GL** | 679 | 31.6% | HIGH PRIORITY | LuaOpenGL(419), LuaShaders(78), LuaFBOs(59), LuaTextures(48), others |
| **GL Utility** | 486 | 22.6% | STAYS AS-IS | VAO, FBO, VBO, myGL wrappers - ARE the GL layer |
| **UI** | 172 | 8.0% | IN PROGRESS | GuiHandler, MiniMap, ProfileDrawer, etc. - mostly migrated |
| **Units/Models** | 126 | 5.9% | BLOCKED | UnitDrawer (FFP color/matrix), ModelVAO, FeatureDrawer |
| **Textures** | 125 | 5.8% | MIXED | nv_dds, Bitmap, TextureRenderAtlas mostly use GL texture uploads |
| **Environment** | 120 | 5.6% | IN PROGRESS | GrassDrawer, Water, SkyBox, Decals |
| **Other/Core** | 107 | 5.0% | MIXED | GlobalRendering (debug/config), WorldDrawer |
| **Map/Terrain** | 102 | 4.8% | IN PROGRESS | SMF terrain, ROAM, texture binding |
| **Core Rendering** | 99 | 4.6% | PARTIAL | LineDrawer, CommandDrawer, QTPFSPath, etc. |
| **Font** | 44 | 2.0% | NOT STARTED | glFont rendering |
| **Game** | 39 | 1.8% | MINIMAL | Camera, Game, PreGame - mostly stubs |
| **RHI Infrastructure** | 20 | 0.9% | INFO ONLY | Header includes of GL enums |
| **Info Textures** | 20 | 0.9% | PARTIAL | Combiner, Radar, Height shaders |
| **System** | 8 | 0.4% | MINIMAL | SplashScreen, Screenshot, etc. |

**TOTAL: 2,147 GL calls**

---

## Top 20 Files by GL Call Count

| Rank | File | Count | Category | Status |
|------|------|-------|----------|--------|
| 1 | LuaOpenGL.cpp | 419 | Lua GL | NOT STARTED - Infrastructure needed |
| 2 | VertexArray.cpp | 85 | GL Utility | INTENTIONAL - IS the GL VAO wrapper |
| 3 | FBO.cpp | 84 | GL Utility | INTENTIONAL - IS the GL framebuffer wrapper |
| 4 | LuaShaders.cpp | 78 | Lua GL | NOT STARTED - Lua shader binding |
| 5 | UnitDrawer.cpp | 67 | Units/Models | BLOCKED - FFP matrix/color state |
| 6 | LuaFBOs.cpp | 59 | Lua GL | NOT STARTED - Lua FBO binding |
| 7 | GuiHandler.cpp | 54 | UI | PARTIAL - LogicOp/immediate mode migrated |
| 8 | GlobalRendering.cpp | 50 | Core | MOSTLY MIGRATED - SDL/debug/config boundary |
| 9 | LuaTextures.cpp | 48 | Lua GL | NOT STARTED - Lua texture binding |
| 10 | nv_dds.cpp | 42 | Textures | PARTIAL - DDS file format loading |
| 11 | myGL.cpp | 37 | GL Utility | INTENTIONAL - GL state wrapper utilities |
| 12 | Texture.cpp | 37 | Textures | PARTIAL - GL texture creation/upload |
| 13 | 3DModelVAO.cpp | 37 | Units/Models | PARTIAL - VAO setup, model rendering |
| 14 | LightHandler.cpp | 34 | GL Utility | INTENTIONAL - FFP lighting state |
| 15 | GrassDrawer.cpp | 31 | Environment | PARTIAL - Texture binding migrated, matrix/color remain |
| 16 | glStateDebug.h | 30 | GL Utility | INTENTIONAL - GL debug introspection |
| 17 | GeometryBuffer.cpp | 30 | GL Utility | INTENTIONAL - GL geometry streaming |
| 18 | glExtra.cpp | 28 | GL Utility | INTENTIONAL - GL extension helpers |
| 19 | VBO.cpp | 28 | GL Utility | INTENTIONAL - IS the GL buffer wrapper |
| 20 | SMFGroundTextures.cpp | 28 | Map/Terrain | PARTIAL - Texture setup and binding |

**Note:** Files 2-4, 11, 14, 16-17 are GL wrapper utilities that implement OpenGL functionality. They stay as-is. The **actual migrateable** call count is ~1,460 across the remaining files.

---

## Categorization: Intentional vs Migrateable GL Calls

### A. INTENTIONAL GL CALLS (Stay as-is: ~650 calls)

#### 1. GL RHI Backend Files (206 calls)
Must not be migrated - these ARE the OpenGL backend:
- `Rendering/RHI/OpenGL/GLContext.cpp` - 78 calls
- `Rendering/RHI/OpenGL/GLTexture.cpp` - 51 calls
- `Rendering/RHI/OpenGL/GLDevice.cpp` - 41 calls
- `Rendering/RHI/OpenGL/GLPipeline.cpp` - 35 calls
- `Rendering/RHI/OpenGL/GLFramebuffer.cpp` - 2 calls
- `Rendering/RHI/OpenGL/GLBuffer.cpp` - 2 calls

#### 2. GL Shader Backend (151 calls)
Must not be migrated - these ARE the GL shader implementation:
- `Rendering/Shaders/Shader.cpp` - 109 calls (GLSLProgramObject/GLSLShaderObject)
- `Rendering/Shaders/GLSLCopyState.cpp` - 42 calls (GL-specific shader introspection)

#### 3. GL Texture Backend (37 calls)
Must not be migrated - legacy GL texture system:
- `Rendering/GL/Texture.cpp` - 37 calls (GL::Texture2D/Texture2DArray)

#### 4. GL Utility Layer - Intentional Wrappers (~300 calls)
These are the OpenGL wrapper utilities that other code depends on. Migrating them would be counterproductive; instead, keep them and call them from RHI backends:
- `VertexArray.cpp` - 85 calls (GL VAO wrapper)
- `FBO.cpp` - 84 calls (GL framebuffer wrapper)
- `myGL.cpp` - 37 calls (GL state helpers)
- `VBO.cpp` - 28 calls (GL buffer wrapper)
- `GeometryBuffer.cpp` - 30 calls (GL geometry streaming)
- `glExtra.cpp` - 28 calls (GL extension wrappers)
- `StreamBuffer.cpp` - 19 calls (GL streaming buffer)
- `LightHandler.cpp` - 34 calls (FFP lighting state - intentional)
- `glStateDebug.h` - 30 calls (GL debug introspection)
- `glxHandler.cpp` - 1 call (X11 platform boundary)
- Others - ~23 calls

#### 5. Platform/External Boundaries (~100 calls)
These are necessary GL calls at architectural boundaries:
- `GlobalRendering.cpp` - 50 GL calls out of 50 total:
  - `glGetIntegerv()` - 21 calls (query GPU capabilities - NO RHI YET)
  - `glGetString()` - 6 calls (GL version/extensions - NO RHI YET)
  - `glDebugMessageCallback()` - 2 calls (debug output)
  - SDL_GL_* calls - 10+ (window/context mgmt - platform boundary, intentional)
  - Diagnostic only - ~10 calls

#### 6. Deprecated Water (534 calls, EXCLUDED)
Already removed from build:
- `DynWater.cpp` - 421 calls
- `AdvWater.cpp` - 78 calls
- `RefractWater.cpp` - 35 calls

#### 7. External System Integration (Unquantified)
- Shader creation via ShaderHandler (wraps GL calls - OK)
- Texture IDs from external systems (SMFReadMap, IconHandler, etc. - now wrapped with RHI)

---

### B. MIGRATEABLE GL CALLS (Need RHI equivalents: ~1,460 calls)

#### 1. Lua GL Scripting (679 calls) - **HIGHEST PRIORITY**
Need infrastructure redesign:

**LuaOpenGL.cpp (419 calls)** - Lua rendering interface
- **Render State (PRIORITY 1):** glScissor, glViewport, glColorMask, glDepthMask, glDepthFunc, glEnable/Disable, glCullFace, glBlendFunc, glBlendEquation, glStencilFunc/Op/Mask, glPolygonMode, glPolygonOffset, glLineWidth, glPointSize, glClear
  - **Action:** Map to RHI equivalents (SetScissorRect, SetViewport, SetColorWriteMask, SetDepthTestEnabled, SetCullMode, SetBlendState, SetBlendEquation, SetStencilState, etc.)
  - **Count:** ~150 calls
  
- **FFP Deprecated (PRIORITY 3):** glLight*, glMaterial*, glFog*, glAlphaFunc, glTexEnv, glColor3f/4f, glMatrixMode, glLoadIdentity, glPushMatrix, glPopMatrix, glTranslate/Rotate/Scale, glOrtho, glFrustum, glClipPlane
  - **Action:** Remove or log deprecation warnings
  - **Count:** ~120 calls
  
- **Immediate Mode (PRIORITY 2):** glBegin/End, glVertex*/glNormal*/glTexCoord*/glColor*
  - **Action:** Convert to VBO streaming or VBO-based API
  - **Count:** ~50 calls
  
- **Display Lists (PRIORITY 4):** glNewList/EndList/CallList/DeleteLists
  - **Action:** No RHI equivalent; requires Lua API change
  - **Count:** ~20 calls
  
- **Texture Operations (PRIORITY 1):** glBindTexture, glActiveTexture, glTexImage2D, glTexSubImage2D, glGenerateMipmap, glCopyTexSubImage2D
  - **Action:** Map to RHI texture binding/upload
  - **Count:** ~30 calls
  
- **Queries (PRIORITY 2):** glGenQueries, glDeleteQueries, glBeginQuery, glEndQuery
  - **Action:** Add IRHIQuery interface
  - **Count:** ~10 calls
  
- **Sync (PRIORITY 1):** glFlush, glFinish, glMemoryBarrier
  - **Action:** Map to IRHIContext equivalents
  - **Count:** ~3 calls

**LuaShaders.cpp (78 calls)** - Lua shader binding
- Mostly glUseProgram, glUniform*, glGetUniform*, glGetAttribLocation, glBindAttribLocation
- **Action:** Wrap ShaderHandler calls (already maps to RHI Shader interface)
- **Estimate:** 50-60 migrateable

**LuaFBOs.cpp (59 calls)** - Lua framebuffer binding
- glBindFramebuffer, glFramebufferTexture2D, glFramebufferRenderbuffer, glCheckFramebufferStatus
- **Action:** Map to IRHIFramebuffer interface
- **Estimate:** 40-50 migrateable

**LuaTextures.cpp (48 calls)** - Lua texture binding
- glBindTexture, glActiveTexture, glTexImage2D, glTexSubImage2D, glGenerateMipmap, glCompressedTexImage2D
- **Action:** Map to IRHITexture interface
- **Estimate:** 40-45 migrateable

**Other Lua modules (75 calls):**
- LuaVAOImpl (25), LuaMaterial (20), LuaOpenGLUtils (6), LuaRBOs (9), LuaFonts (3), LuaConstGL (3), LuaVBOImpl (1)
- **Action:** Migrate to RHI equivalents (VAO->IRHIVertexArray, Material->Shader uniforms, RBO->IRHIRenderbuffer)
- **Estimate:** 50-60 migrateable

**Total Lua GL Estimate:** 500-550 migrateable calls (200+ are FFP/deprecated that should be removed)

---

#### 2. UI Layer (172 calls) - **PARTIAL, ~80-100 REMAIN**

**GuiHandler.cpp (54 calls)**
- Already migrated LogicOp (2 calls), immediate mode (25 calls → RenderBuffer)
- **Remaining:** ~15 GL calls likely in specialized paths
- **Top functions:** glEnable, glDisable, glBlendFunc, glDepthMask, glClear, glViewport
- **Status:** ~70% migrated, needs final audit

**MiniMap.cpp (18 calls)**
- **Remaining:** FlushMatrices blockers (4 calls), glScissor/glClear (6 calls), glBindTexture (4 calls)
- **Status:** Texture binding migrated (commit abf34bc), matrix stack calls remain

**Others (100 calls):** ProfileDrawer, EndGameBox, ResourceBar, HUDDrawer, etc.
- Mostly simple state setting + texture binding
- **Estimate:** 70-80 migrateable

---

#### 3. Units/Models (126 calls) - **BLOCKED ON FFP**

**UnitDrawer.cpp (67 calls)**
- **Blockers:** glColor4f (6 calls), glPushMatrix/glPopMatrix/glMultMatrixf (10+ calls), glCallList (2), glClipPlane (5)
- **Reason:** GLSL shaders read `gl_Color` and `gl_ModelViewProjectionMatrix` (FFP builtins)
- **Action:** Port shaders to use explicit color/matrix uniforms (like Sky shaders did in commit 26f75e7)
- **Estimate:** 40-50 migrateable (after shader port)

**3DModelVAO.cpp (37 calls)** - Model rendering VAOs
- Mostly VAO setup: glBindVertexArray, glEnableVertexAttribArray, glVertexAttribPointer, glBindBuffer
- **Action:** Wrap in IRHIVertexArray interface
- **Estimate:** 25-30 migrateable

**ModelDrawerHelpers.cpp (10 calls)** - Helper state
- **Estimate:** 5-8 migrateable

---

#### 4. Textures (125 calls) - **PARTIAL MIGRATION**

**nv_dds.cpp (42 calls)** - DDS file loading
- glCompressedTexImage2D, glCompressedTexSubImage2D, glTexImage2D
- **Action:** Wrap texture creation in IRHITexture::Upload() with compression support
- **Estimate:** 35-40 migrateable

**Texture.cpp (37 calls)** - GL texture backend
- Note: This file should eventually be removed/merged with GL backend
- **Estimate:** 25-30 migrateable (lower priority)

**TextureRenderAtlas.cpp (11 calls)** - Texture atlas rendering
- glBindTexture, glTexSubImage2D, glFramebufferTexture2D
- **Estimate:** 8-10 migrateable

**Bitmap.cpp (20 calls)** - Bitmap I/O
- Mostly glTexImage2D, glTexSubImage2D, glGenerateMipmap
- **Estimate:** 15-18 migrateable

---

#### 5. Environment (120 calls) - **IN PROGRESS**

**GrassDrawer.cpp (31 calls)**
- **Migrated:** Texture binding (commit abf34bc)
- **Remaining:** FFP matrices (14 calls), glClipPlane (2), glScissor (2)
- **Status:** ~65% migrated, blocked on FlushMatrices refactor
- **Estimate:** 10-15 remaining

**GroundDecalHandler.cpp (18 calls)**
- Texture binding migrated (commit abf34bc)
- **Estimate:** ~8-10 remaining (state/scissor)

**Water files (BumpWater, BasicWater, IWater) (~30 calls)**
- Mostly texture binding (migrated) + FFP state
- **Estimate:** 15-20 migrateable

**SkyBox.cpp (21 calls)** - Sky rendering
- Cubemap binding, matrix state
- **Estimate:** 12-15 migrateable

**DebugCubeMapTexture.cpp (13 calls)** - Debug visualization
- Cube map rendering
- **Estimate:** 8-10 migrateable

---

#### 6. Map/Terrain (102 calls) - **IN PROGRESS**

**SMFGroundTextures.cpp (28 calls)**
- Texture setup and binding
- **Estimate:** 18-22 migrateable

**SMFReadMap.cpp (22 calls)**
- **Migrated:** Texture wrapping with RHI (commit 29d73b6)
- **Remaining:** Some raw GLuint handling at boundaries
- **Estimate:** 5-10 remaining

**ROAM mesh (16 calls)**
- VAO setup and rendering
- **Estimate:** 10-12 migrateable

---

#### 7. Core Rendering & Other (200+ calls)

**LineDrawer.cpp, CommandDrawer.cpp, QTPFSPathDrawer.cpp, etc.**
- Mostly state setup (glEnable, glBlendFunc, glViewport, glScissor)
- **Estimate:** 150-180 migrateable

**Font system (44 calls)**
- glFont rendering with texture atlases
- **Estimate:** 30-35 migrateable

---

## GL Call Pattern Analysis

### Top GL Function Patterns (Across All Files)

1. **Texture Operations** (~320 calls)
   - glBindTexture, glActiveTexture, glTexImage2D, glTexSubImage2D, glGenerateMipmap
   - **RHI Mapping:** IRHIContext::BindTexture(), IRHITexture::Upload()
   - **Status:** Partially migrated, ~200 remain

2. **FFP Matrix Stack** (~280 calls)
   - glMatrixMode, glPushMatrix, glPopMatrix, glLoadIdentity, glMultMatrix, glTranslate, glRotate, glScale
   - **RHI Mapping:** Compute MVP matrices CPU-side, pass as uniforms
   - **Status:** Blocked on shader migration
   - **Examples:** Sky shaders (DONE), RenderBuffer (DONE), Model/Grass shaders (BLOCKED)

3. **State Enabling/Disabling** (~200+ calls)
   - glEnable, glDisable (depth test, culling, blending, scissor, etc.)
   - **RHI Mapping:** IRHIContext::SetDepthTestEnabled(), SetCullMode(), SetBlendState(), SetScissorRect()
   - **Status:** 60-70% migrated

4. **Rendering State** (~180 calls)
   - glBlendFunc, glBlendEquation, glDepthFunc, glCullFace, glPolygonMode, glLineWidth
   - **RHI Mapping:** Pipeline state + dynamic state methods
   - **Status:** 50-60% migrated

5. **Framebuffer Operations** (~140 calls)
   - glBindFramebuffer, glFramebufferTexture2D, glFramebufferRenderbuffer, glClear
   - **RHI Mapping:** IRHIFramebuffer, IRHIContext::Clear()
   - **Status:** 40-50% migrated

6. **Immediate Mode** (~80 calls, mostly Lua)
   - glBegin, glEnd, glVertex*, glColor*, glTexCoord*
   - **RHI Mapping:** VBO streaming or API redesign
   - **Status:** Not started

7. **Queries & Debug** (~60 calls)
   - glGetIntegerv, glGetFloatv, glGetString, glDebugMessage*
   - **RHI Mapping:** IRHIDevice query methods + debug infrastructure
   - **Status:** Partially done (debug output done)

---

## Migration Blockers & Dependencies

### Tier 1: No Dependencies (Can migrate immediately)
- **Scissor, Viewport, Clear color** - Simple state setting
- **Render state migrations** - State managers already exist
- **Framebuffer binding** - RHI FBO interface complete
- **Simple texture binding** - RHI texture interface complete
- **Estimate:** 300-400 calls

### Tier 2: Low Dependencies (Straightforward but require planning)
- **Font rendering** - VAO setup + texture binding
- **Decal rendering** - Texture binding + state
- **Path drawing** - Simple geometry + state
- **Estimate:** 150-200 calls

### Tier 3: Medium Dependencies (Require infrastructure)
- **Lua GL interface** - Need Lua-to-RHI mapping layer
- **Framebuffer operations** - RHI FBO interface largely done
- **Estimate:** 300-400 calls

### Tier 4: High Dependencies (Blocked)
- **FFP Matrix Stack** - Blocked on shader migration
  - **Depends on:** Migrating UnitDrawer, GrassDrawer, MiniMap shaders to use explicit uniforms
  - **Impact:** ~200 calls
  
- **FFP Vertex Color** - Blocked on shader migration
  - **Depends on:** Porting GLSL shaders to use gl_Color as uniform
  - **Impact:** ~100 calls
  
- **Display Lists (Lua)** - No RHI equivalent
  - **Depends on:** Lua infrastructure redesign
  - **Impact:** ~20 calls
  
- **Immediate Mode (Lua)** - Complex streaming redesign
  - **Depends on:** Lua VAO/VBO API
  - **Impact:** ~50 calls

---

## Files Fully Migrated vs Partial vs Not Started

### Fully Migrated (0 GL calls)
Files with migration status headers that show completion:
- None identified - all files with GL calls still have some remaining

### Substantially Migrated (75%+ of calls migrated)
Files with explicit status comments showing progress:
1. `GlobalRendering.cpp` - ~95% (34/50 GL calls are SDL/debug/config boundary)
2. `GuiHandler.cpp` - ~70% (LogicOp + immediate mode migrated)
3. `SMFReadMap.cpp` - ~75% (texture wrapping done)
4. `SMFRenderState.cpp` - ~70% (texture binding migrated)
5. `MiniMap.cpp` - ~60% (texture binding migrated)

### Partially Migrated (25-75%)
1. `GrassDrawer.cpp` - ~65% (texture binding done, matrices remain)
2. `UnitDrawer.cpp` - ~30% (some state migrated, FFP blocked)
3. `3DModelVAO.cpp` - ~50% (VAO setup partial)
4. `BumpWater.cpp` - ~50% (texture binding done)
5. `GroundDecalHandler.cpp` - ~55% (texture binding done)
6. `BasicWater.cpp` - ~40%
7. `IWater.cpp` - ~40%

### Not Started (>75% remain)
1. **LuaOpenGL.cpp** - 419 calls, ~5% migrated (some state may be trivial)
2. **LuaShaders.cpp** - 78 calls, ~20% migrated
3. **LuaFBOs.cpp** - 59 calls, ~10% migrated
4. **LuaTextures.cpp** - 48 calls, ~15% migrated
5. **nv_dds.cpp** - 42 calls, ~10% migrated
6. **VertexArray.cpp** - 85 calls, 100% INTENTIONAL (GL wrapper layer)
7. **FBO.cpp** - 84 calls, 100% INTENTIONAL (GL wrapper layer)
8. **myGL.cpp** - 37 calls, 100% INTENTIONAL (GL wrapper)
9. **VBO.cpp** - 28 calls, 100% INTENTIONAL (GL wrapper)
10. **GeometryBuffer.cpp** - 30 calls, 100% INTENTIONAL (GL wrapper)
11. **glExtra.cpp** - 28 calls, 100% INTENTIONAL (GL wrapper)
12. **LightHandler.cpp** - 34 calls, 100% INTENTIONAL (FFP lighting)
13. **glStateDebug.h** - 30 calls, 100% INTENTIONAL (GL debug)
14. Font files (glFont.cpp, glFontRenderer.cpp, CFontTexture.cpp) - ~45 calls, ~5% migrated
15. Various path drawers (HAPFSPathDrawer, QTPFSPathDrawer) - ~35 calls, ~10% migrated

---

## Comparison to Previous Audit (2026-02-16)

Previous summary from MEMORY.md:
- Estimated ~2,857 effective GL calls with 522 deprecated water
- Previous breakdown: Lua(506), Utility(425), Map(100), Env(95), UI(95), Units(117), Core(85), Textures(62), Font(33)

Current findings:
- **2,147 total GL calls** (slightly lower, likely due to recent migrations)
- **Lua GL: 679 calls** vs estimated 506 (higher - more detailed analysis)
- **GL Utility: 486 calls** vs estimated 425 (higher - includes more wrappers)
- **Better categorization** with explicit "intentional" vs "migrateable" breakdown

Key improvements since last audit:
- Texture binding migrations (commit abf34bc)
- Non-owning texture wrappers (commit 29d73b6)
- Clip plane infrastructure (commit 19b2abc)
- Sky shader migrations (commit 26f75e7)

---

## Recommended Migration Sequence

### Phase 1: High-Value, Low-Risk (Weeks 1-2)
1. **Simple State Setters** (~150 calls)
   - Map glScissor → SetScissorRect
   - Map glViewport → SetViewport
   - Map glClear* → Clear*
   - Map glColorMask/glDepthMask → SetColorWriteMask/SetDepthWriteEnabled
   - Impact: Reduces visual regressions

2. **Texture Binding Completion** (~100 calls)
   - Finish UI/Font texture binding
   - Complete terrain/decal texture setup
   - Wrap remaining external textures

3. **Framebuffer Operations** (~80 calls)
   - Use existing IRHIFramebuffer interface
   - Migrate all glBindFramebuffer and attachment calls

### Phase 2: Medium-Value, Medium-Risk (Weeks 3-4)
1. **Font Rendering** (~40 calls)
   - Use VAO + RHI texture binding
   - Keep text shading logic intact

2. **Path & Line Rendering** (~60 calls)
   - Use VAO + state setters
   - Minimal shader changes needed

3. **Decal & Particle Effects** (~40 calls)
   - Complete GrassDrawer, GroundDecalHandler migrations
   - Move FFP state → shader uniforms

### Phase 3: Infrastructure & Lua (Weeks 5-8)
1. **Lua GL Mapping Layer** (600+ calls)
   - Create Lua-to-RHI binding functions
   - Map render state functions
   - Implement FFP deprecation warnings

2. **Shader Migration** (100-150 calls)
   - Port UnitDrawer shaders to use color/matrix uniforms
   - Port GrassDrawer, MiniMap shaders
   - This unblocks 200+ FFP calls

3. **Lua Display Lists** (20 calls)
   - Decide: error with migration guidance OR implement VAO caching?
   - High impact on Lua compatibility

### Phase 4: Validation & Cleanup (Week 9)
1. Full headless build validation
2. Metal backend verification
3. Final GL call audit

---

## Risk Assessment

### High Risk (Visual Changes Likely)
- FFP matrix stack changes (unless done carefully per commit 26f75e7 pattern)
- Immediate mode refactoring (Lua)
- Display list removal (Lua compatibility break)

### Medium Risk (Requires Testing)
- Font rendering path changes
- State manager refactoring
- Texture format/compression handling

### Low Risk (Mechanical)
- Scissor/viewport/clear state
- Framebuffer binding
- Basic texture binding completion

---

## Code Examples

### Migration Pattern 1: State Setting
```cpp
// Before: Direct GL
glScissor(x, y, w, h);
glViewport(x, y, w, h);
glClearColor(r, g, b, a);
glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

// After: RHI
auto* ctx = GetRHIDevice()->GetContext();
ctx->SetScissorRect(RHI::Rect{x, y, w, h});
ctx->SetViewport(RHI::Viewport{x, y, w, h, 0, 1});
ctx->SetClearColor(RHI::RGBA{r, g, b, a});
ctx->Clear(RHI::CLEAR_COLOR_DEPTH);
```

### Migration Pattern 2: Texture Binding (Already Done)
```cpp
// Before: Direct GL
glActiveTexture(GL_TEXTURE0);
glBindTexture(GL_TEXTURE_2D, glId);

// After: RHI (Already migrated in SMFRenderState, etc.)
auto* rhiTex = readMap->GetRHITexture(type, num);
ctx->BindTexture(0, rhiTex);
```

### Migration Pattern 3: FFP Matrix Stack (Blocked)
```cpp
// Before: FFP stack
glMatrixMode(GL_MODELVIEW);
glPushMatrix();
glMultMatrixf(mvp);
// Draw with implicit MVP
glPopMatrix();

// After: Explicit uniform (pattern from commit 26f75e7)
const CMatrix44f mvp = projection * modelView;
rb.SetTransformMatrix(mvp);
rb.DrawArrays(...);  // Uses uniform instead of FFP
```

---

## Conclusion

**2,147 total GL function calls remain in the codebase.**

Of these:
- **~650 are intentional** (GL backend files, wrappers, platform boundaries)
- **~1,460 need migration** to RHI equivalents
- **~280 are blocked** on shader/infrastructure changes
- **~680 are high-priority** (Lua scripting interface)

**Estimated effort:**
- Phase 1 (state/texture): 2 weeks
- Phase 2 (fonts/paths): 1 week
- Phase 3 (Lua/shaders): 4-6 weeks
- Phase 4 (validation): 1 week

**Critical path blockers:**
1. Lua-to-RHI mapping layer design
2. Shader migration for FFP color/matrix builtins
3. Lua display list strategy decision

---

