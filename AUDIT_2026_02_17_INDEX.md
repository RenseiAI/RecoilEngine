# RecoilEngine GL Call Audit - 2026-02-17

**Audit Type:** Comprehensive GL API call count across RHI migration  
**Date:** 2026-02-17  
**Auditor:** Claude Code Audit Agent  
**Status:** Complete - Ready for Team Review

---

## Quick Facts

- **Total GL Calls:** 1,117 (down from 1,198 - reduction of 81 calls)
- **Compiled Source Files:** 227 (excluding rts/lib/, deprecated water)
- **Migrateable Calls:** 348 (31.2% of total)
- **RHI Backend (Intentional):** 229 (20.5% of total)
- **Lua/Scripting (External):** 340 (30.4% of total)
- **Infrastructure (Utilities):** 200 (17.9% of total)

---

## Report Files

### Primary Documents

1. **GL_AUDIT_REPORT_2026_02_17_FINAL.md** (370 lines, 15KB)
   - Complete audit report with full analysis
   - All 68 migrateable files listed
   - Critical blockers documented
   - Priority roadmap for Tier 4.1
   - Success metrics and targets
   - **START HERE** for complete information

2. **GL_AUDIT_QUICK_REFERENCE.txt** (203 lines, 8.3KB)
   - Condensed summary for quick lookup
   - Top 10 migrateable files
   - 7 critical blockers summary
   - Priority tier breakdown
   - Key insights
   - **USE THIS** for quick facts

### Supporting Documents (Previous Audits)

- GL_AUDIT_REPORT_2026_02_16_COMPREHENSIVE.md (590 lines)
- GL_AUDIT_REPORT_2026_02_16.md (390 lines)
- GL_AUDIT_SUMMARY.md (296 lines)
- GL_AUDIT_QUICK_SUMMARY.txt (127 lines)
- GL_AUDIT_INDEX.md (253 lines)

---

## Key Findings Summary

### Category Breakdown

| Category | Calls | % | Status | Action |
|----------|-------|---|--------|--------|
| **RHI GL Backend** | 229 | 20.5% | Complete | Do NOT migrate |
| **Lua/Scripting** | 340 | 30.4% | External | Defer to Tier 4.3 |
| **GL Utilities** | 200 | 17.9% | Infrastructure | Part of GL layer |
| **Migrateable** | 348 | 31.2% | **PRIMARY FOCUS** | Tier 4.1 work |

### Top 10 Migrateable Files

1. `/rts/Rendering/Units/UnitDrawer.cpp` (35) - BLOCKED: display lists, FFP color
2. `/rts/Game/UI/GuiHandler.cpp` (34) - BLOCKED: FFP matrix, immediate geom
3. `/rts/Rendering/GlobalRendering.cpp` (15) - Debug/capability queries
4. `/rts/Map/SMF/SMFGroundTextures.cpp` (14) - Texture binding
5. `/rts/Rml/Backends/RmlUi_Renderer_GL3_Recoil.cpp` (14) - External RmlUI
6. `/rts/Map/SMF/ROAM/RoamMeshDrawer.cpp` (12) - Shader/texture binding
7. `/rts/Rendering/CommandDrawer.cpp` (11) - FFP matrix state
8. `/rts/Rendering/HAPFSPathDrawer.cpp` (10) - FFP matrix + geometry
9. `/rts/Rendering/Fonts/glFont.cpp` (10) - Font texture binding
10. `/rts/Map/SMF/ROAM/Patch.cpp` (9) - Shader/texture binding

Remaining 58 files: 263 calls (mostly 1-7 calls each)

---

## Critical Blockers (Affects ~100+ calls)

### 1. FFP Matrix Stack (45+ calls) - MOSTLY RESOLVED
- **Status:** SetTransformMatrix pattern implemented
- **Remaining:** 8-10 calls (UnitDrawer display lists, MiniMap clip planes)
- **Solution:** Already have pattern; display list issue needs Lua recorder

### 2. FFP Color State (14+ calls) - BLOCKED
- **Files:** UnitDrawer, GuiHandler
- **Issue:** Model shaders need color uniform support
- **Solution:** Add color uniform, migrate model shaders

### 3. Display Lists (8+ calls) - BLOCKED
- **Files:** UnitDrawer, FeatureDrawer, GrassDrawer
- **Issue:** No direct RHI equivalent; Lua display lists are dynamic
- **Solution:** Implement Lua display list recorder or pre-compile

### 4. Immediate-Mode Geometry (15-20 calls) - PARTIALLY RESOLVED
- **Files:** GuiHandler, HAPFSPathDrawer, QTPFSPathDrawer
- **Issue:** RenderBuffer exists but need geometry helpers
- **Solution:** Implement glSurfaceCircle, glDrawVolume replacements

### 5. Instanced Vertex Attributes (27 calls) - BLOCKED
- **Files:** 3DModelVAO, LuaVAOImpl, GroundDecalHandler
- **Issue:** RHI lacks vertex layout/attribute API
- **Solution:** Add RHI::VertexLayout interface

### 6. Non-Identity Clip Planes (4-6 calls) - PARTIALLY RESOLVED
- **Status:** Identity-MV sites done; non-identity blocked
- **Solution:** Apply ModelView transform in clip plane setup

### 7. External Texture Lifecycle (4-5 calls) - MOSTLY RESOLVED
- **Status:** WrapExistingTexture infrastructure complete
- **Solution:** Use WrapExistingTexture for external GL textures

---

## Tier 4.1 Recommended Priority Order

### Priority 1: Existing RHI Infrastructure (90+ calls, 2-3 days)
Ready to migrate immediately - use existing RHI:
- SMFGroundTextures.cpp (14) - Texture binding
- RoamMeshDrawer.cpp (12) - Shader/texture binding
- Patch.cpp (9) - Texture binding
- GrassDrawer.cpp (9) - Texture + clip plane
- GroundDecalHandler.cpp (8) - Depth texture binding
- CommandDrawer.cpp (11) - SetTransformMatrix
- HAPFSPathDrawer.cpp (10) - SetTransformMatrix + geometry
- Various smaller files (15-20 combined)

**Expected reduction:** 30-40 calls

### Priority 2: Minor RHI Additions (60+ calls, 2-3 days after P1)
Requires small RHI enhancements:
- GuiHandler.cpp (34) - RenderBuffer geometry
- Path drawers (20+) - Specialized geometry
- Font rendering (10) - Texture binding

**Expected reduction:** 60+ calls

### Priority 3: Major RHI Extensions (100+ calls, 3-5 days after P2)
Requires new RHI APIs:
- RHI::VertexLayout interface (27 calls)
- Model shader color uniform (15 calls)
- Lua display list recorder (8 calls)
- Non-identity clip planes (4-6 calls)

**Expected reduction:** 50-60 calls

### Priority 4: Lua RHI Binding (340 calls - DEFERRED to Tier 4.3)
Requires comprehensive Lua binding layer:
- LuaOpenGL.cpp (177)
- LuaShaders.cpp (65)
- LuaFBOs.cpp (31)
- Other Lua files (67)

**Estimate:** 5-7 days, deferred

---

## Success Milestones

| Milestone | Calls | Effort | Timeline | Status |
|-----------|-------|--------|----------|--------|
| **Current (2026-02-17)** | 1,117 | - | Done | ✓ |
| **After Priority 1** | 1,050-1,080 | 2-3 days | Next |  |
| **Tier 4.1 Complete** | 900-950 | +2-3 days | 5-7 days total |  |
| **Tier 4.2 Complete** | 700-750 | +3-5 days | 8-12 days total |  |
| **Final Target** | 769 | - | - | Strategic |

**Final Target Strategy:** Keep RHI backend (229) + utilities (540) = 769 minimum. Engine core fully RHI-based; Lua as optional external interface.

---

## Progress Since Last Audit

**Previous:** 1,198 calls (2026-02-17)  
**Current:** 1,117 calls (2026-02-17)  
**Reduction:** -81 calls (-6.8%)

**Contributing commits:**
- 02f6be0: Shadow/cubemap/depth texture binding (~26 calls)
- abf34bc: Texture binding callers (~15 calls)
- 29d73b6: External texture wrappers (~10 calls)
- 778544f: Non-owning texture infrastructure (~5 calls)
- 19b2abc: SetClipPlaneEquation RHI (~5 calls)
- Various small migrations (~5+ calls)

**Key achievements:**
- External texture wrapper infrastructure mature
- Clip plane RHI support working (identity-MV cases)
- Texture binding infrastructure established
- SetTransformMatrix pattern widely applicable

---

## RHI Infrastructure Status

### Complete Components
- **RHI Interface Layer:** 8 headers, 180+ virtual methods
- **OpenGL Backend:** 14 files, 229 GL calls (100% complete)
- **Metal Backend:** 15 files, 138 methods (100% complete)
- **Shader Pipeline:** GLSL→SPIR-V→MSL (41/41 shaders)
- **Headless Stubs:** 421+ GL function stubs (CI/verification)

### Recent Additions
- SetClipPlaneEquation (clip plane RHI support)
- WrapExistingTexture (non-owning texture wrapper)
- GetRHITexture (texture reference helpers)
- GetSkyReflectionTexture (external texture access)

---

## Implementation Patterns

### Proven Patterns (Ready to Deploy)

**1. SetTransformMatrix Pattern**
```cpp
const CMatrix44f mvp = CMatrix44f(projStack.Top()) * mvStack.Top();
rb.SetTransformMatrix(mvp);  // Bypass FFP matrix stack
rb.DrawArrays(...);
```
Status: Used successfully, resolves ~45+ matrix stack calls

**2. RHI Texture Binding Pattern**
```cpp
auto* ctx = GetRHIDevice()->GetContext();
ctx->Bind(texture.get(), 0);
ctx->DrawArrays(...);
ctx->Unbind();
```
Status: Working, resolves texture binding calls

**3. Non-Owning Texture Wrapper Pattern**
```cpp
auto wrapped = device->WrapExistingTexture(glId, type, format, w, h);
// Use wrapped as IRHITexture, does NOT delete GL texture
```
Status: Established, resolves external texture lifecycle

### Partially Resolved Patterns

**4. RenderBuffer Geometry Generation**
Status: Infrastructure exists, need geometry helpers
- Need: glSurfaceCircle, glDrawVolume replacements
- Affects: ~15-20 calls in GuiHandler, path drawers

**5. Clip Plane RHI Support**
Status: Identity-MV done, non-identity blocked
- Resolved: `SetClipPlaneEquation()` with identity transform
- Remaining: Apply transform for non-identity ModelView

### Blocked Patterns (Need New RHI APIs)

**6. Vertex Layout/Attributes**
Status: BLOCKED - no RHI::VertexLayout API yet
- Blocks: 27 calls in VAO/instancing code
- Solution: Add vertex attribute setup interface

**7. Lua Display Lists**
Status: BLOCKED - no RHI recording mechanism yet
- Blocks: 8 calls across unit/feature/grass drawers
- Solution: Implement Lua display list recorder

**8. FFP Color in Shaders**
Status: BLOCKED - model shaders don't read color uniform
- Blocks: 14 calls in unit/model rendering
- Solution: Add color uniform to model shaders

---

## Next Steps for Migration Team

### Immediate (Today)
1. Review GL_AUDIT_REPORT_2026_02_17_FINAL.md
2. Understand the 7 critical blockers
3. Identify which Priority 1 files to start with

### Short-Term (Next 5-7 days)
1. Complete Priority 1 migrations (90+ calls)
   - Use existing RHI texture binding
   - Apply SetTransformMatrix pattern widely
   - Expected: 1,050-1,080 calls remaining

2. Create Priority 2 RenderBuffer geometry helpers
   - glSurfaceCircle implementation
   - glDrawVolume replacement
   - Circle/arc drawing utilities

### Medium-Term (Next 8-12 days)
1. Add RHI::VertexLayout interface
2. Implement Lua display list recorder
3. Update model shaders for color uniform
4. Complete Priority 3 migrations (100+ calls)
   - Expected: 700-750 calls remaining

### Long-Term (Deferred)
1. Create Lua RHI binding layer (Tier 4.3)
2. Wrap remaining Lua GL calls (340 calls)
3. Final engine audit pass

---

## Files to Review

### Essential Reading
1. `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine/GL_AUDIT_REPORT_2026_02_17_FINAL.md`
   - Complete audit with all details
   - All 68 migrateable files listed
   - Priority roadmap

2. `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine/GL_AUDIT_QUICK_REFERENCE.txt`
   - Quick lookup guide
   - Top 10 files summary
   - Key insights

### Code References (Top Migrateable Files)

**Priority 1 - Start Here:**
- `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine/rts/Map/SMF/SMFGroundTextures.cpp`
- `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine/rts/Map/SMF/ROAM/RoamMeshDrawer.cpp`
- `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine/rts/Rendering/CommandDrawer.cpp`

**Priority 2 - After Priority 1:**
- `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine/rts/Game/UI/GuiHandler.cpp`
- `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine/rts/Rendering/Units/UnitDrawer.cpp`

**Priority 3 - After Priorities 1-2:**
- `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine/rts/Rendering/Models/3DModelVAO.cpp`
- `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine/rts/Rendering/Env/Decals/GroundDecalHandler.cpp`

---

## Conclusion

The RecoilEngine RHI migration is **well-structured with clear remaining work:**

1. **Infrastructure is complete** - RHI interface, backends, shader pipeline all working
2. **81 calls eliminated** in recent commits - solid momentum
3. **348 calls remain to migrate** - well-defined, prioritized work
4. **Clear blockers identified** - 7 specific issues with known solutions
5. **Proven patterns established** - SetTransformMatrix, RHI texture binding working

**Realistic timelines:**
- Priority 1 (texture + matrix patterns): 2-3 days, 90+ calls
- Priority 2 (geometry + extensions): 2-3 days, 60+ calls
- Priority 3 (RHI APIs): 3-5 days, 100+ calls
- Tier 4.1 completion: 5-7 days total, 150-200 calls saved

**Strategic outcome:** Engine core fully RHI-based with Lua as optional external scripting interface.

---

**Audit prepared by:** Claude Code Audit Agent  
**Date:** 2026-02-17  
**Status:** Ready for Team Review and Migration Planning

