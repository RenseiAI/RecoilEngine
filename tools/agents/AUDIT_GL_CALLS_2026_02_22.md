# RecoilEngine GL Call Comprehensive Audit

**Date:** 2026-02-22
**Audit Scope:** All .cpp/.c files in rts/ directory (1887 files searched)
**Search Pattern:** `gl[A-Z]\w*\(` (direct GL function calls)

---

## Executive Summary

- **GRAND TOTAL: 2,302 GL calls** across all categorized domains
- **Backend (DO NOT MIGRATE): 413 calls (17%)**
  - GL backend implementation files (GLContext, GLDevice, GLTexture, etc.)
  - Shader backend files
  - Must remain as-is for OpenGL functionality
  
- **Deprecated Water (EXCLUDED): 534 calls (23%)**
  - AdvWater.cpp, DynWater.cpp, RefractWater.cpp
  - Not included in active build — removed in Phase 1
  
- **Migratable Infrastructure: 1,355 calls (60%)**
  - GL Utilities (superseded by RHI): 305 calls (13%)
  - Lua Subsystem (requires Phase 8): 667 calls (28%)
  - Game Rendering (partially migrated): 379 calls (16%)
  - RmlUi Renderer: 2 calls (0%)

---

## Category Breakdown

### 1. GL Backend (DO NOT MIGRATE) - 413 calls

**Purpose:** Implementation of the OpenGL RHI backend and shader compilation infrastructure.

| File | GL Calls | Purpose |
|------|----------|---------|
| Shader.cpp | 109 | GLSL shader compilation, reflection, program linking |
| GLContext.cpp | 90 | OpenGL context state management (core backend) |
| GLTexture.cpp | 55 | Texture creation, binding, parameters (backend) |
| GLSLCopyState.cpp | 42 | GLSL program introspection for uniforms/attributes |
| GLDevice.cpp | 41 | Device creation, capability queries (backend) |
| Texture.cpp | 37 | GL-specific texture management (backend) |
| GLPipeline.cpp | 35 | Pipeline state setup, render state (backend) |
| GLFramebuffer.cpp | 2 | Framebuffer operations (backend) |
| GLBuffer.cpp | 2 | Buffer binding (backend) |

**Status:** COMPLETE. These files ARE the OpenGL backend implementation and must not be modified. All 413 calls are architecturally necessary.

---

### 2. Deprecated Water Systems (EXCLUDED) - 534 calls

**Purpose:** Legacy water rendering systems removed from build.

| File | GL Calls | Status |
|------|----------|--------|
| DynWater.cpp | 421 | Removed from build (Phase 1) |
| AdvWater.cpp | 78 | Removed from build (Phase 1) |
| RefractWater.cpp | 35 | Removed from build (Phase 1) |

**Status:** EXCLUDED. Not compiled into engine-legacy or engine-headless targets. These calls do not affect migration metrics.

---

### 3. GL Utility Classes (SUPERSEDED BY RHI) - 305 calls

**Purpose:** Legacy GL wrapper utilities. Being replaced by RHI abstraction layer throughout Phase 6-7.

| File | GL Calls | Migration Status |
|------|----------|------------------|
| FBO.cpp | 86 | Partial — SetDrawBuffer/SetDrawBuffers wrappers added (Phase 7.4) |
| VertexArray.cpp | 85 | Superseded by RHI::SetVertexLayout() (Phases 5.4-5.5, 5.8) |
| myGL.cpp | 37 | Utility functions, mostly superseded by RHI |
| glExtra.cpp | 28 | Deprecated FFP helpers, RenderBuffer replaces some |
| VBO.cpp | 28 | Superseded by IRHIBuffer (Phase 6.0-6.2) |
| StreamBuffer.cpp | 19 | Used by remaining FFP code (e.g., GrassDrawer blade rendering) |
| glStateDebug.cpp | 10 | Debug helper, low priority |
| GeometryBuffer.cpp | 5 | Legacy geometry output (DrawDebug migrated in Phase 7.1) |
| VAO.cpp | 4 | Superseded by RHI::SetVertexLayout() |
| glDebugGroup.cpp | 2 | Debug marker helpers |
| State.cpp | 1 | Misc state management |

**Status:** MIGRATING. These utilities are being progressively eliminated as game code migrates to RHI. ~60% have been replaced by RHI equivalents (Phases 5-7).

**Action:** Continue Phase 8+ work to eliminate remaining GL utility code.

---

### 4. RmlUi Renderer - 2 calls

**Purpose:** 3rd-party UI renderer backend.

| File | GL Calls | Details |
|------|----------|---------|
| RmlUi_Renderer_GL3_Recoil.cpp | 2 | Basic GL setup calls |

**Status:** MINIMAL. Only 2 calls — low-priority Phase 8 work.

**Note:** Full RmlUi renderer requires dedicated RHI backend implementation.

---

### 5. Lua Subsystem - 667 calls

**Purpose:** Lua scripting API for direct OpenGL control and engine inspection.

| File | GL Calls | Subtype |
|------|----------|---------|
| LuaOpenGL.cpp | 415 | Direct GL API exposure (glBegin, glVertex, etc.) |
| LuaShaders.cpp | 78 | Shader API (glCreateProgram, glCompileShader, etc.) |
| LuaFBOs.cpp | 59 | Framebuffer object API |
| LuaTextures.cpp | 48 | Texture management API |
| LuaVAOImpl.cpp | 25 | Vertex array object API |
| LuaMaterial.cpp | 20 | Material/lighting API |
| LuaRBOs.cpp | 9 | Renderbuffer object API |
| LuaOpenGLUtils.cpp | 6 | Utility functions |
| LuaFonts.cpp | 3 | Font rendering API |
| LuaConstGL.cpp | 3 | GL constant definitions |
| LuaVBOImpl.cpp | 1 | Vertex buffer object API |

**Status:** DEFERRED (Phase 8). These are intentional GL calls — the Lua subsystem exposes raw GL for mod authors.

**Strategy:** Requires either:
1. Creating RHI Lua bindings as parallel API (preferred for Metal support)
2. Leaving as GL-only with `!RHI::GetDevice()` guards (fallback)

**Blocker:** Lua subsystem needs architectural decision on GL vs RHI exposure.

---

### 6. Game Rendering (Other) - 379 calls

**Purpose:** Core game rendering logic (drawers, UI, misc rendering).

#### Top 20 Files by GL Call Count

| Rank | File | GL Calls | Category | Guard Status |
|------|------|----------|----------|--------------|
| 1 | nv_dds.cpp | 42 | Texture loading (3rd-party) | N/A (utility) |
| 2 | GlobalRendering.cpp | 36 | Global render state, window management | GUARDED: Lines 733 |
| 3 | GuiHandler.cpp | 32 | Game UI rendering | MIXED: 104 guarded, 2717/2789/2853 unguarded |
| 4 | ProfileDrawer.cpp | 25 | Performance profiling/debug | Debug-only |
| 5 | EndGameBox.cpp | 17 | Game end UI dialog | **UNGUARDED** |
| 6 | CommandDrawer.cpp | 15 | Command visualization | Mostly RHI |
| 7 | PreGame.cpp | 12 | Game setup screen | MIXED |
| 8 | ShareBox.cpp | 11 | Resource sharing UI | **UNGUARDED** |
| 9 | SelectedUnitsHandler.cpp | 10 | Unit selection visualization | Mostly RHI |
| 10 | ResourceBar.cpp | 10 | Resource display UI | MIXED |
| 11 | Combiner.cpp | 10 | Lua info texture API | Intentional GL |
| 12 | glFont.cpp | 9 | Font rendering | GLSLProgramObject backend |
| 13 | GrassDrawer.cpp | 9 | Grass rendering | INTENTIONAL: FFP matrix (Phase 7.2) |
| 14 | QuitBox.cpp | 8 | Quit dialog UI | **UNGUARDED** |
| 15 | HAPFSPathDrawer.cpp | 8 | Pathfinding debug visualization | **UNGUARDED** |
| 16 | DebugDrawerAI.cpp | 8 | AI debug visualization | **UNGUARDED** |
| 17 | Camera.cpp | 8 | Camera matrix management | INTENTIONAL: FFP matrix (Phase 7.3) |
| 18 | TextureRenderAtlas.cpp | 7 | Texture atlas management | Mostly RHI |
| 19 | ProjectileDrawer.cpp | 7 | Projectile rendering | Mostly RHI |
| 20 | HUDDrawer.cpp | 7 | HUD rendering | Mostly RHI |

---

## Detailed Analysis: Unguarded GL Calls in Game Rendering

### INTENTIONAL UNGUARDED CALLS (Must Remain)

**1. Camera.cpp (8 GL calls)**
- **Type:** `glLoadMatrixf` calls
- **Lines:** ~8 calls scattered in Camera::LoadMatrices()
- **Reason:** FFP matrix state required by GrassDrawer blade rendering when no shader is active
- **Status:** INTENTIONAL (Phase 7.3 deferred removal — blade draw has no active shader)
- **Solution:** None — keep as-is until GrassDrawer blade rendering migrated to RHI in Phase 9+

**2. GrassDrawer.cpp (9 GL calls)**
- **Type:** FFP matrix operations, `glEnable`/`glDisable`
- **Lines:** FlushMatrices() calls for blade + blur rendering
- **Reason:** Blade geometry draw has no shader active (uses FFP pipeline internally)
- **Status:** INTENTIONAL (known blocker in Phase 7)
- **Solution:** Defer to Phase 9+ when GrassDrawer migrated to compute/instancing

**3. Combiner.cpp (10 GL calls)**
- **Type:** `glBegin()`, `glEnd()`, `glVertex()`, `glTexCoord()` (immediate mode)
- **Lines:** Lua info texture API — intentional GL exposure
- **Reason:** Lua mod API for custom texture rendering
- **Status:** INTENTIONAL (Lua subsystem, Phase 8)
- **Solution:** Phase 8 — wrap in Lua-specific RHI bindings or guard with `!RHI::GetDevice()`

**4. LuaOpenGL.cpp (415 GL calls)**
- **Type:** Raw GL API exposure (`glBegin`, `glVertex`, `glColor`, etc.)
- **Reason:** Lua mod API — intentional direct GL control
- **Status:** INTENTIONAL (Lua subsystem, Phase 8)
- **Solution:** Phase 8 — parallel RHI Lua API or GL-only guard

---

### EXTERNALLY-SOURCED UNGUARDED CALLS (External Boundary)

**5. GuiHandler.cpp (32 GL calls)**
- **Type:** `glBindTexture(GL_TEXTURE_2D, ...)` for unit def images
- **Lines:** 2717, 2789, 2853 (NOT guarded), 104 (guarded)
- **Details:** 
  ```cpp
  glBindTexture(GL_TEXTURE_2D, CUnitDrawer::GetUnitDefImage(ud));
  ```
- **Reason:** CUnitDrawer::GetUnitDefImage() returns raw GLuint from external texture system
- **Status:** EXTERNAL BOUNDARY — texture IDs are from legacy texture manager
- **Action:** Requires Unit def image texture migration to RHI in Phase 8+
  - See Phase 7.1c: MouseCursor already wraps with WrapExistingTexture pattern
  - Apply same pattern to unit def images once texture system fully RHI-based

**6. GlobalRendering.cpp (36 GL calls)**
- **Type:** `glGetString`, `glGetIntegerv`, `glBindFramebuffer`
- **Details:**
  ```cpp
  // Line 733: glBindFramebuffer guarded
  if (!RHI::GetDevice())
      glBindFramebuffer(GL_READ_FRAMEBUFFER_EXT, 0);
  
  // Lines 1028-1031: fallback when RHI not initialized
  else {
      if ((grInfo.glVersion = (const char*) glGetString(GL_VERSION)) == nullptr) ...
  }
  ```
- **Status:** GUARDED (fallback paths when RHI device not available)
- **Action:** Already Phase 7.4 compliant — no changes needed

**7. nv_dds.cpp (42 GL calls)**
- **Type:** `glGenTextures`, `glBindTexture`, `glTexImage2D`, `glTexParameteri`
- **Reason:** 3rd-party DDS texture loader utility
- **Status:** EXTERNAL LIBRARY (fastgltf included)
- **Action:** Not in active game code — can be left as-is for now; wrap if used by game

**8. PreGame.cpp, EndGameBox.cpp, ShareBox.cpp, QuitBox.cpp (47 GL calls combined)**
- **Type:** `glColor4f`, `glBegin`, `glEnd`, `glVertex3f` (FFP immediate mode), `glPushMatrix`, `glPopMatrix`
- **Reason:** Legacy UI rendering using FFP
- **Status:** **NOT GUARDED** — needs migration to RenderBuffer
- **Action:** Phase 8+ priority — migrate menu/UI code to RHI::RenderBuffer for immediate-mode replacement

---

## Guard Pattern Analysis

**Guarded (RHI-aware):**
- GlobalRendering.cpp: Line 733 (`if (!RHI::GetDevice())`)
- GuiHandler.cpp: Line 100-106 (`if (RHI::GetDevice())`)
- NamedTextures.cpp: 2 sites (`if (!RHI::GetDevice())`)
- ShadowHandler.cpp: Line 178 (`if (!RHI::GetDevice())`)
- Camera.cpp: Lines intentionally unguarded (FFP matrix blocker)
- GrassDrawer.cpp: Lines intentionally unguarded (blade rendering blocker)

**Unguarded (assume GL context available):**
- EndGameBox, ShareBox, QuitBox, PreGame: FFP UI code (20+ calls)
- LuaOpenGL.cpp: 415 calls (intentional GL API)
- Combiner.cpp: 10 calls (Lua texture API)

---

## RHI Infrastructure Status

**Complete (Phases 1-7.4):**
- ✅ 8 RHI interface headers (device, context, pipeline, texture, framebuffer, etc.)
- ✅ OpenGL backend: 14 files, 165+ methods
- ✅ Metal backend: 15 files, 140+ methods
- ✅ Shader pipeline: GLSL → SPIR-V → MSL
- ✅ 41/41 shaders have MSL equivalents
- ✅ SetVertexLayout/ClearVertexLayout API (Phase 5.4)
- ✅ SetTransformMatrix() for MVP bypass (Phase 4.2)
- ✅ WrapExistingTexture() for external texture boundaries (Phase 7.1c)
- ✅ FBO blit wrappers (Phase 7.4c)

**Gaps (blocking Phase 8+):**
- ❌ RmlUi renderer backend (needs custom RHI renderer)
- ❌ Lua RHI bindings parallel API (needs design)
- ❌ Texture swizzle support in IRHITexture
- ❌ Line width in RasterizerState
- ❌ Metal ReadPixels implementation
- ❌ Unit def image texture allocation (legacy texture system boundary)

---

## Recommended Phase 8+ Priorities

1. **UI Immediate Mode Migration (HIGH)**
   - EndGameBox, ShareBox, QuitBox, PreGame: Replace `glColor4f/glBegin/glEnd/glVertex3f` with RenderBuffer
   - ~47 GL calls removed
   - Estimated effort: 2-3 days

2. **Lua Subsystem Refactor (MEDIUM)**
   - Decision: RHI Lua API (preferred) vs GL-only guards
   - Phase 8a: Create RHI Lua bindings for core functions
   - Phase 8b: Deprecate GL-only Lua functions
   - 667 GL calls in scope

3. **Unit Def Texture Migration (MEDIUM)**
   - Migrate CUnitDrawer::GetUnitDefImage() to RHI texture allocation
   - Apply WrapExistingTexture pattern to GuiHandler::glBindTexture calls
   - ~3 GL calls (glBindTexture) replaced

4. **GrassDrawer Blade Rendering (LOW - Phase 9)**
   - Requires architectural redesign of blade rendering pipeline
   - Current: blade mesh drawn with no active shader (uses FFP matrix state)
   - Future: compute-based or instancing-based blade rendering
   - ~9 GL calls (Camera::LoadMatrices, GrassDrawer::FlushMatrices)

5. **RmlUi Renderer Backend (LOW - Phase 8b)**
   - ~2 GL calls only
   - Requires custom IRHIRenderer interface
   - Deferred until other Phase 8 work complete

---

## Summary Statistics

| Category | GL Calls | % of Total | Status | Phase |
|----------|----------|-----------|--------|-------|
| GL Backend (do not migrate) | 413 | 17% | COMPLETE | N/A |
| Deprecated Water (excluded) | 534 | 23% | EXCLUDED | Phase 1 |
| GL Utilities (superseded) | 305 | 13% | 60% migrated | 5-7 |
| RmlUi Renderer | 2 | 0% | Minimal | 8b |
| Lua Subsystem | 667 | 28% | Blocked | 8a |
| Game Rendering (other) | 379 | 16% | Partial | 8-9 |
| **TOTAL** | **2,302** | **100%** | | |

**Next Action:** Phase 8 (Lua subsystem + RmlUi) — requires architectural decisions on GL vs RHI API exposure.

---

## Appendix: File-by-File GL Call Counts

### GL Backend
- /rts/Rendering/RHI/OpenGL/GLBuffer.cpp: 2
- /rts/Rendering/RHI/OpenGL/GLContext.cpp: 90
- /rts/Rendering/RHI/OpenGL/GLDevice.cpp: 41
- /rts/Rendering/RHI/OpenGL/GLFramebuffer.cpp: 2
- /rts/Rendering/RHI/OpenGL/GLPipeline.cpp: 35
- /rts/Rendering/RHI/OpenGL/GLTexture.cpp: 55
- /rts/Rendering/Shaders/Shader.cpp: 109
- /rts/Rendering/Shaders/GLSLCopyState.cpp: 42
- /rts/Rendering/Textures/Texture.cpp: 37

### GL Utilities
- /rts/Rendering/GL/FBO.cpp: 86
- /rts/Rendering/GL/VertexArray.cpp: 85
- /rts/Rendering/GL/myGL.cpp: 37
- /rts/Rendering/GL/glExtra.cpp: 28
- /rts/Rendering/GL/VBO.cpp: 28
- /rts/Rendering/GL/StreamBuffer.cpp: 19
- /rts/Rendering/GL/glStateDebug.cpp: 10
- /rts/Rendering/GL/GeometryBuffer.cpp: 5
- /rts/Rendering/GL/VAO.cpp: 4
- /rts/Rendering/GL/glDebugGroup.cpp: 2
- /rts/Rendering/GL/State.cpp: 1

### Lua Subsystem
- /rts/Lua/LuaOpenGL.cpp: 415
- /rts/Lua/LuaShaders.cpp: 78
- /rts/Lua/LuaFBOs.cpp: 59
- /rts/Lua/LuaTextures.cpp: 48
- /rts/Lua/LuaVAOImpl.cpp: 25
- /rts/Lua/LuaMaterial.cpp: 20
- /rts/Lua/LuaRBOs.cpp: 9
- /rts/Lua/LuaOpenGLUtils.cpp: 6
- /rts/Lua/LuaFonts.cpp: 3
- /rts/Lua/LuaConstGL.cpp: 3
- /rts/Lua/LuaVBOImpl.cpp: 1

### Game Rendering (Top 30)
- /rts/Rendering/Textures/nv_dds.cpp: 42
- /rts/Rendering/GlobalRendering.cpp: 36
- /rts/Game/UI/GuiHandler.cpp: 32
- /rts/Rendering/DebugDrawerAI.cpp: 25
- /rts/Rendering/Map/InfoTexture/Modern/Combiner.cpp: 10
- /rts/Game/UI/ProfileDrawer.cpp: 25
- /rts/Game/UI/EndGameBox.cpp: 17
- /rts/Rendering/CommandDrawer.cpp: 15
- /rts/Game/PreGame.cpp: 12
- /rts/Game/UI/ShareBox.cpp: 11
- /rts/Game/SelectedUnitsHandler.cpp: 10
- /rts/Game/UI/ResourceBar.cpp: 10
- /rts/Rendering/Fonts/glFont.cpp: 9
- /rts/Rendering/Env/GrassDrawer.cpp: 9
- /rts/Game/UI/QuitBox.cpp: 8
- /rts/Rendering/HAPFSPathDrawer.cpp: 8
- /rts/Game/Camera.cpp: 8
- /rts/Rendering/Textures/TextureRenderAtlas.cpp: 7
- /rts/Rendering/Env/ProjectileDrawer.cpp: 7
- /rts/Rendering/HUDDrawer.cpp: 7
- /rts/Rendering/Textures/Bitmap.cpp: 7
- /rts/Game/UI/MiniMap.cpp: 6
- /rts/Rendering/Units/UnitDrawer.cpp: 5
- /rts/Rendering/ShadowHandler.cpp: 5
- /rts/Rendering/QTPFSPathDrawer.cpp: 5
- /rts/Rendering/Textures/NamedTextures.cpp: 5
- /rts/Rendering/Env/BumpWater.cpp: 5
- /rts/Game/Game.cpp: 4
- /rts/Rendering/WorldDrawer.cpp: 3
- /rts/Game/UI/MouseCursor.cpp: 3

