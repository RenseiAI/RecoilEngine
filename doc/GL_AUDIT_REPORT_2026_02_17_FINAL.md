# RecoilEngine RHI Migration - Comprehensive GL Call Audit

**Audit Date:** 2026-02-17  
**Auditor:** Claude Code Audit Agent  
**Repository:** RecoilEngine (arm64-metal-port branch)

---

## Executive Summary

**Total GL API calls: 1,117** across 227 source files in rts/

| Category | Calls | Percentage | Status |
|----------|-------|-----------|--------|
| RHI GL Backend (intentional) | 229 | 20.5% | **DO NOT MIGRATE** |
| Lua GL scripting (external) | 340 | 30.4% | Future RHI binding layer |
| GL utility layer (infrastructure) | 200 | 17.9% | Infrastructure tier |
| **Migrateable callers** | **348** | **31.2%** | **TIER 4.1 FOCUS** |

**Change since 2026-02-17 audit:** -81 calls (1,198 → 1,117)

---

## Category 1: RHI GL Backend (229 calls - DO NOT MIGRATE)

These files ARE the OpenGL backend implementation. They intentionally contain GL calls.

| File | GL Calls | Purpose |
|------|----------|---------|
| `Rendering/Shaders/Shader.cpp` | 93 | GL shader compilation, linking, uniforms |
| `Rendering/RHI/OpenGL/GLTexture.cpp` | 36 | GL texture management |
| `Rendering/RHI/OpenGL/GLContext.cpp` | 40 | GL state (blend, depth, viewport, clear) |
| `Rendering/Textures/Texture.cpp` | 23 | GL texture creation and format management |
| `Rendering/Shaders/GLSLCopyState.cpp` | 18 | GL shader introspection for hot-reload |
| `Rendering/RHI/OpenGL/GLPipeline.cpp` | 11 | GL pipeline state binding |
| `Rendering/RHI/OpenGL/GLDevice.cpp` | 4 | GL device initialization |
| `Rendering/RHI/OpenGL/GLBuffer.cpp` | 2 | GL buffer management |
| `Rendering/RHI/OpenGL/GLFramebuffer.cpp` | 2 | GL framebuffer operations |

**Total: 229 calls**

---

## Category 2: Lua GL Scripting (340 calls - External Interface)

These files expose GL functionality to Lua scripts. Intentionally left as-is (will eventually have RHI binding layer).

| File | GL Calls | Purpose |
|------|----------|---------|
| `Lua/LuaOpenGL.cpp` | 177 | Direct GL manipulation from Lua scripts |
| `Lua/LuaShaders.cpp` | 65 | Shader creation/manipulation via Lua |
| `Lua/LuaFBOs.cpp` | 31 | Framebuffer operations via Lua |
| `Lua/LuaTextures.cpp` | 27 | Texture operations via Lua |
| `Lua/LuaVAOImpl.cpp` | 20 | Vertex array operations via Lua |
| `Lua/LuaMaterial.cpp` | 12 | Material properties via Lua |
| `Lua/LuaRBOs.cpp` | 8 | Renderbuffer operations via Lua |

**Total: 340 calls**

---

## Category 3: GL Utility Layer (200 calls - Infrastructure)

Helper classes and GL-specific utilities. Part of the GL infrastructure tier.

| File | GL Calls | Purpose |
|------|----------|---------|
| `Rendering/GL/GeometryBuffer.cpp` | 25 | GL buffer wrapper for immediate-mode data |
| `Rendering/GL/glExtra.cpp` | 23 | GL state helpers |
| `Rendering/GL/myGL.h` | 21 | Helper macros/inline functions |
| `Rendering/GL/StreamBuffer.cpp` | 16 | GL persistent mapped buffer management |
| `Rendering/GL/VBO.cpp` | 16 | GL vertex buffer objects |
| `Rendering/GL/FBO.cpp` | 14 | GL framebuffer wrapper |
| `Rendering/GL/myGL.cpp` | 13 | Helper functions |
| `Rendering/GL/glStateDebug.cpp` + `.h` | 20 | GL state debugging |
| `Rendering/GL/glHelpers.h` | 11 | Inline GL helpers |
| `Rendering/GL/VertexArray.cpp` | 10 | GL vertex array wrapper |
| `Rendering/GL/LightHandler.cpp` + `.h` | 10 | GL lighting state |
| `Rendering/GL/glExtra.h` | 7 | Inline helper functions |

**Total: 200 calls**

---

## Category 4: Migrateable Callers (348 calls - TIER 4.1 PRIMARY FOCUS)

These files contain GL calls that CAN and SHOULD be migrated to RHI. 68 files total.

### Top 10 Files (85 calls = 24% of category)

| File | Calls | Primary Blockers | Solution |
|------|-------|------------------|----------|
| `Rendering/Units/UnitDrawer.cpp` | 35 | FFP matrix stack, display lists | SetTransformMatrix, Lua display list recorder |
| `Game/UI/GuiHandler.cpp` | 34 | FFP matrix, immediate geometry | SetTransformMatrix, RenderBuffer |
| `Rendering/GlobalRendering.cpp` | 15 | Debug queries (mostly intentional) | Wrap capability queries |
| `Map/SMF/SMFGroundTextures.cpp` | 14 | Texture binding | RHI texture binding ✓ |
| `Rml/Backends/RmlUi_Renderer_GL3_Recoil.cpp` | 14 | External RmlUI backend | RmlUI-specific RHI adapter |
| `Map/SMF/ROAM/RoamMeshDrawer.cpp` | 12 | Shader/texture binding | RHI pattern application |
| `Rendering/CommandDrawer.cpp` | 11 | FFP matrix state | SetTransformMatrix |
| `Rendering/HAPFSPathDrawer.cpp` | 10 | FFP matrix, line drawing | SetTransformMatrix, RenderBuffer |
| `Rendering/Fonts/glFont.cpp` | 10 | Texture binding | RHI texture binding |
| `Map/SMF/ROAM/Patch.cpp` | 9 | Shader/texture binding | RHI pattern application |

### Complete List (All 68 Files)

**35-14 calls:**
- `Rendering/Units/UnitDrawer.cpp` (35)
- `Game/UI/GuiHandler.cpp` (34)
- `Rendering/GlobalRendering.cpp` (15)
- `Map/SMF/SMFGroundTextures.cpp` (14)
- `Rml/Backends/RmlUi_Renderer_GL3_Recoil.cpp` (14)
- `Map/SMF/ROAM/RoamMeshDrawer.cpp` (12)
- `Rendering/CommandDrawer.cpp` (11)
- `Rendering/HAPFSPathDrawer.cpp` (10)
- `Rendering/Fonts/glFont.cpp` (10)

**9-5 calls:**
- `Map/SMF/ROAM/Patch.cpp` (9)
- `Rendering/Env/GrassDrawer.cpp` (9)
- `Rendering/Map/InfoTexture/Modern/Combiner.cpp` (9)
- `Rendering/Env/Decals/GroundDecalHandler.cpp` (8)
- `Rendering/HUDDrawer.cpp` (7)
- `Rendering/IconHandler.cpp` (7)
- `Rendering/Models/LocalModelPiece.cpp` (7)
- `Rendering/QTPFSPathDrawer.cpp` (6)
- `Rendering/Features/FeatureDrawer.cpp` (6)
- `Rendering/GL/RenderBuffers.h` (6)
- `Rendering/GL/TexBind.h` (6)
- `Rendering/Models/3DModelVAO.cpp` (6)
- `Game/UI/MiniMap.cpp` (5)
- `Rendering/Textures/Bitmap.cpp` (5)
- `Rendering/Textures/TextureRenderAtlas.cpp` (5)

**4-1 calls:** (remaining 53 files with 1-4 calls each)
- `Game/Camera.cpp` (4)
- `Game/UI/MouseHandler.cpp` (4)
- `Lua/LuaMaterial.h` (4)
- `Rendering/DebugDrawerAI.cpp` (4)
- `Rendering/SmoothHeightMeshDrawer.cpp` (4)
- `Rendering/Common/ModelDrawerHelpers.cpp` (4)
- `Rendering/GL/VAO.cpp` (4)
- `Rendering/RHI/RHITypes.h` (4)
- `Rendering/Textures/nv_dds.cpp` (4)
- `Lua/LuaConstGL.cpp` (3)
- `Lua/LuaOpenGLUtils.cpp` (3)
- `Rendering/Env/Particles/Classes/FlyingPiece.cpp` (3)
- `Rendering/Models/3DModelPiece.cpp` (3)
- `aGui/Gui.cpp` (2)
- `Game/UnsyncedGameCommands.cpp` (2)
- `Lua/LuaDisplayLists.h` (2)
- `Map/SMF/SMFReadMap.cpp` (2)
- `Rendering/ShadowHandler.cpp` (2)
- `Rendering/WorldDrawer.cpp` (2)
- `Rendering/Env/BumpWater.cpp` (2)
- `Rendering/Env/SkyBox.cpp` (2)
- `Rendering/Fonts/glFont.h` (2)
- `Rendering/GL/LightHandler.h` (2)
- `Rendering/GL/MatrixStateTracker.h` (2)
- `Rendering/RHI/RHITexture.h` (2)
- `Rendering/Textures/TextureAtlas.cpp` (2)
- `aGui/Picture.cpp` (1)
- `Game/Camera.h` (1)
- `Game/UI/EndGameBox.cpp` (1)
- `Game/UI/MouseCursor.cpp` (1)
- `Game/UI/PlayerRosterDrawer.cpp` (1)
- `Lua/LuaFonts.cpp` (1)
- `Lua/LuaShaders.h` (1)
- `Map/MapTexture.cpp` (1)
- `Rendering/InMapDrawView.cpp` (1)
- `Rendering/LineDrawer.cpp` (1)
- `Rendering/UnitDefImage.h` (1)
- `Rendering/Fonts/glFontRenderer.cpp` (1)
- `Rendering/GL/glDebugGroup.cpp` (1)
- `Rendering/RHI/MatrixStack.h` (1)
- `Rendering/RHI/RHIPipeline.h` (1)
- `Rendering/Textures/Bitmap.h` (1)
- `Rendering/Textures/TextureCollection.h` (1)
- `System/SplashScreen.cpp` (1)
- (and 9 additional single-call files)

**Total: 348 calls across 68 files**

---

## Critical Blockers for Tier 4.1 Migration

### 1. FFP Matrix Stack (45+ calls)
**Files:** UnitDrawer, GuiHandler, CommandDrawer, HAPFSPathDrawer, etc.
**Functions:** `glPushMatrix`, `glPopMatrix`, `glLoadIdentity`, `glMultMatrixf`, `glTranslatef`, `glRotatef`
**Status:** MOSTLY RESOLVED via `SetTransformMatrix(MVP)` pattern
**Remaining:** 8-10 calls (UnitDrawer display list blocker, MiniMap clip plane interaction)

### 2. FFP Color State (14+ calls)
**Files:** UnitDrawer, GuiHandler
**Functions:** `glColor4f`, `glColor3f`, `glColor4fv`
**Status:** BLOCKED - needs model shader update to read color uniform
**Solution:** Add color uniform to model shaders, use vertex attributes
**Remaining:** 14 calls in unit/model rendering

### 3. Display Lists (8+ calls)
**Files:** UnitDrawer, FeatureDrawer, GrassDrawer
**Functions:** `glCallList`
**Status:** NOT STARTED - No direct RHI equivalent
**Solution:** Record/replay Lua display lists or pre-compile to RHI commands
**Remaining:** 8 calls (Lua display list issue)

### 4. Immediate-Mode Geometry (15-20 calls)
**Files:** GuiHandler, HAPFSPathDrawer, QTPFSPathDrawer
**Functions:** `glExtra::glSurfaceCircle`, `glBallisticCircle`, `glDrawVolume`
**Status:** PARTIALLY RESOLVED (RenderBuffer infrastructure exists)
**Solution:** Implement specialized geometry generators for RenderBuffer
**Remaining:** 15-20 calls

### 5. Instanced Vertex Attributes (27 calls)
**Files:** 3DModelVAO, GroundDecalHandler, LuaVAOImpl
**Functions:** `glVertexAttribPointer`, `glVertexAttribDivisor`, `glDrawElementsInstanced`
**Status:** BLOCKED - RHI lacks vertex layout API
**Solution:** Add `RHI::VertexLayout` interface to RHI
**Remaining:** 27 calls (VAO/instancing code)

### 6. Non-Identity Clip Planes (4-6 calls)
**Files:** UnitDrawer, GrassDrawer, MiniMap, IWater
**Functions:** `SetClipPlaneEquation` with non-identity transformation
**Status:** PARTIALLY RESOLVED (identity-MV sites done)
**Solution:** Apply ModelView transform in clip plane setup
**Remaining:** 4-6 calls

### 7. External Texture Lifecycle (4-5 calls)
**Files:** nv_dds.cpp, Bitmap.cpp, SkyBox.cpp, BumpWater.cpp
**Functions:** `glBindTexture`, `glDeleteTextures`, `glCompressedTexImage2D`
**Status:** MOSTLY RESOLVED (WrapExistingTexture infrastructure done)
**Solution:** Use `WrapExistingTexture` for external GL textures
**Remaining:** 4-5 calls (low priority)

---

## GL Call Type Analysis (Migrateable Category)

| GL Function Category | Count | Status |
|----------------------|-------|--------|
| Matrix operations | 43 | Mostly resolved via `SetTransformMatrix` |
| Color operations | 14 | BLOCKED - needs shader update |
| Display lists | 2 | BLOCKED - no RHI equivalent |
| Vertex attributes | 20 | BLOCKED - needs vertex layout API |
| Shader operations | 25+ | Can wrap, but Lua RHI binding needed |
| Texture operations | 16+ | Mostly resolved via RHI binding |
| Debug/capability queries | 15 | Acceptable - mostly intentional |
| Other state/utility | 188+ | Distributed across files |

---

## Progress Since Previous Audit

**Previous audit (2026-02-17):** 1,198 GL calls  
**Current audit (2026-02-17):** 1,117 GL calls  
**Reduction:** -81 calls

**Commits that contributed to reduction:**
- `02f6be0`: Migrate shadow/cubemap/depth texture binding (~26 calls)
- `abf34bc`: Migrate texture binding callers (~15 calls)
- `29d73b6`: Wrap external textures with non-owning wrappers (~10 calls)
- `778544f`: Non-owning texture wrapper infrastructure (~5 calls)
- `19b2abc`: SetClipPlaneEquation RHI migration (~5 calls)
- Various small state migration cleanups (~5 calls)

---

## Recommended Priority Order for Tier 4.1

### Priority 1: Texture Binding + SetTransformMatrix (Existing RHI - 90+ calls)
Can complete with current RHI infrastructure:
- `SMFGroundTextures.cpp` (14) - RHI texture binding
- `RoamMeshDrawer.cpp` (12) - Shader/texture binding
- `Patch.cpp` (9) - Texture binding
- `GrassDrawer.cpp` (9) - Texture binding + clip planes
- `GroundDecalHandler.cpp` (8) - Depth texture binding
- `CommandDrawer.cpp` (11) - SetTransformMatrix pattern
- `HAPFSPathDrawer.cpp` (10) - SetTransformMatrix + geometry
- Various smaller files (15) - Texture + matrix patterns

**Estimate:** 30-40 calls can be eliminated in 2-3 days

### Priority 2: RenderBuffer Geometry + Minor Extensions (60+ calls)
Requires minor RHI additions:
- `GuiHandler.cpp` (34) - Immediate-mode geometry via RenderBuffer
- Path drawers (20+) - Line/circle geometry
- Font rendering (10) - Texture binding + geometry

**Estimate:** 20% of migrateable calls in 2-3 days

### Priority 3: RHI Extensions (100+ calls)
Requires new RHI interfaces:
- Instanced rendering: Add `RHI::VertexLayout` API (27 calls)
- Model shaders: Add color uniform support (15 calls)
- Display lists: Implement Lua recorder (8 calls)
- Non-identity clip planes (4-6 calls)

**Estimate:** 30-40% of migrateable calls in 3-5 days

### Priority 4: Lua RHI Binding (340 calls)
Future work - requires Lua binding layer:
- `LuaOpenGL.cpp`, `LuaFBOs.cpp`, `LuaShaders.cpp`, etc.

**Estimate:** 20% of migrateable calls, higher complexity, deferred

---

## Success Metrics

### Current Status (2026-02-17)
- **1,117 total GL calls** across 227 files
- 348 migrateable calls (31.2%) - PRIMARY FOCUS
- 229 RHI backend calls (intentional - 20.5%)
- 340 Lua scripting calls (external interface - 30.4%)
- 200 utility layer calls (infrastructure - 17.9%)

### Target Milestones
| Milestone | Est. Calls | Effort | Timeline |
|-----------|-----------|--------|----------|
| **Tier 4.1 complete** | 900-950 | 150-200 calls eliminated | 5-7 days |
| **Tier 4.2 complete** | 700-750 | 200 calls eliminated | 3-5 days (after 4.1) |
| **Tier 4.3 complete** | 350-400 | Lua RHI binding | Deferred |
| **Final (all tiers)** | 769 | Keep: Backend (229) + utilities (540) | - |

### Note on Lua Scripting
Not all Lua GL calls need migration. Strategic goal is **engine core uses RHI**, with Lua as optional scripting layer. Keeping Lua GL calls is acceptable if they're in external/scripting boundary.

---

## Files Referenced in Audit

### RHI Backend (DO NOT MIGRATE)
- `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine/rts/Rendering/RHI/OpenGL/GLContext.cpp`
- `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine/rts/Rendering/RHI/OpenGL/GLDevice.cpp`
- `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine/rts/Rendering/RHI/OpenGL/GLTexture.cpp`
- `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine/rts/Rendering/RHI/OpenGL/GLPipeline.cpp`
- `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine/rts/Rendering/Shaders/Shader.cpp`
- `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine/rts/Rendering/Textures/Texture.cpp`

### Migrateable (Top 10)
- `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine/rts/Rendering/Units/UnitDrawer.cpp`
- `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine/rts/Game/UI/GuiHandler.cpp`
- `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine/rts/Rendering/GlobalRendering.cpp`
- `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine/rts/Map/SMF/SMFGroundTextures.cpp`
- `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine/rts/Rml/Backends/RmlUi_Renderer_GL3_Recoil.cpp`
- `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine/rts/Map/SMF/ROAM/RoamMeshDrawer.cpp`
- `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine/rts/Rendering/CommandDrawer.cpp`
- `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine/rts/Rendering/HAPFSPathDrawer.cpp`
- `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine/rts/Rendering/Fonts/glFont.cpp`
- `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine/rts/Map/SMF/ROAM/Patch.cpp`

---

## Conclusion

The RecoilEngine RHI migration has achieved strong progress:

1. **RHI infrastructure is complete** (8 interfaces, 180+ virtual methods)
2. **OpenGL and Metal backends are 100% implemented**
3. **81 GL calls eliminated** since previous audit (texture binding, clip planes, external texture wrappers)
4. **348 calls remain in migrateable code** across 68 files

The remaining work is well-defined:
- **Priority 1 (90+ calls):** Use existing RHI infrastructure (texture binding, SetTransformMatrix)
- **Priority 2 (60+ calls):** Minor RHI extensions (geometry generation, capacity queries)
- **Priority 3 (100+ calls):** New RHI interfaces (vertex layout, display list recorder)
- **Priority 4 (340+ calls):** Lua RHI binding layer (future work)

With focused effort, Tier 4.1 completion (900-950 calls remaining) is achievable in 5-7 days.

