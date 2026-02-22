# Phase 6.2: GL::Shapes Buffer/Draw Migration + MiniMap/GuiHandler Cleanup

## Context

Phase 6.1 completed all game-world rendering buffer migration (models, terrain, decals, grass). The remaining non-Lua, non-RmlUi GL calls in game rendering code are scattered small items:

- **GL::Shapes**: 5 `glDrawElements` calls + VBO buffers (debug shapes used by DebugColVolDrawer, GuiHandler)
- **MiniMap**: 7 FFP matrix stack calls in `FlushMatrices()` + `ApplyConstraintsMatrix()` (kept for downstream auto-sync)
- **GuiHandler**: 3 `glBindTexture` + 1 `glGetIntegerv` (external texture IDs, stencil query)
- **WorldDrawer**: 5 FFP matrix calls in `ResetMVPMatrices()` (intentionally kept — RHI_TODO)

After this phase, all non-Lua/non-RmlUi game rendering will be Metal-ready.

---

## 6.2a: GL::Shapes VBO → IRHIBuffer + Draw Call Migration (~5 GL calls)

### File: `rts/Rendering/GL/glExtra.h`

Add IRHIBuffer to the `allObjects` tuple storage:

```cpp
#include "Rendering/RHI/RHIBuffer.h"

// Change tuple from:
std::vector<std::tuple<VAO, VBO, VBO>> allObjects;
// To:
std::vector<std::tuple<VAO, VBO, VBO, std::unique_ptr<RHI::IRHIBuffer>, std::unique_ptr<RHI::IRHIBuffer>>> allObjects;
```

**Note:** This changes the tuple from `(VAO, VBO, VBO)` to `(VAO, VBO, VBO, rhiVertBuf, rhiIndxBuf)`. The `unique_ptr` members suppress implicit copy but Shapes is a singleton (`GL::shapes` global) so this is fine.

### File: `rts/Rendering/GL/glExtra.cpp`

#### `CreateGLObjects()` — Add RHI buffer creation after VBO upload:

```cpp
size_t GL::Shapes::CreateGLObjects(
    const std::vector<float3>& verts,
    const std::vector<uint32_t>& indcs
) {
    auto& [vao, vertVBO, indxVBO, rhiVertBuf, rhiIndxBuf] = allObjects.emplace_back(
        VAO{ },
        VBO{ GL_ARRAY_BUFFER, false },
        VBO{ GL_ELEMENT_ARRAY_BUFFER, false },
        std::unique_ptr<RHI::IRHIBuffer>{},
        std::unique_ptr<RHI::IRHIBuffer>{}
    );

    vao.Bind();
    vertVBO.Bind();
    vertVBO.New(verts, GL_STATIC_DRAW);
    indxVBO.Bind();
    indxVBO.New(indcs, GL_STATIC_DRAW);
    EnableAttribs();
    vao.Unbind();
    vertVBO.Unbind();
    indxVBO.Unbind();
    DisableAttribs();

    // RHI path: create buffers for Metal
    if (auto* device = RHI::GetDevice()) {
        const size_t vertSz = verts.size() * sizeof(float3);
        const size_t indxSz = indcs.size() * sizeof(uint32_t);
        rhiVertBuf = device->CreateBuffer(RHI::BufferType::Vertex, RHI::BufferUsage::Static, vertSz);
        rhiVertBuf->Upload(verts.data(), 0, vertSz);
        rhiIndxBuf = device->CreateBuffer(RHI::BufferType::Index, RHI::BufferUsage::Static, indxSz);
        rhiIndxBuf->Upload(indcs.data(), 0, indxSz);
    }

    return allObjects.size() - 1;
}
```

#### Draw functions — Add RHI dual-path (5 call sites):

Each of `DrawSolidSphere`, `DrawWireSphere`, `DrawWireCylinder`, `DrawWireBox` follows the same pattern. Example for `DrawSolidSphere`:

```cpp
void GL::Shapes::DrawSolidSphere(uint32_t numRows, uint32_t numCols)
{
#ifndef HEADLESS
    auto it = solidSpheresMap.find(std::make_tuple(numRows, numCols));
    if (it == solidSpheresMap.end())
        it = CreateSolidSphere(numRows, numCols);

    const auto& [vao, vertVBO, indxVBO, rhiVertBuf, rhiIndxBuf] = allObjects[it->second];
    const uint32_t indexCount = static_cast<uint32_t>(indxVBO.GetSize() / sizeof(uint32_t));

    vao.Bind();
    auto* device = RHI::GetDevice();
    if (device) {
        auto* ctx = device->GetContext();
        if (rhiVertBuf) ctx->BindVertexBuffer(rhiVertBuf.get(), 0);
        if (rhiIndxBuf) ctx->BindIndexBuffer(rhiIndxBuf.get(), RHI::IndexType::UInt32);
        ctx->DrawIndexed(RHI::PrimitiveType::Triangles, indexCount, 0, 0);
    } else {
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount), GL_UNSIGNED_INT, nullptr);
    }
    vao.Unbind();
#endif
}
```

Wire shapes use `RHI::PrimitiveType::Lines` instead of `Triangles`.

Also add `#include "Rendering/RHI/RHIDevice.h"` to glExtra.cpp.

---

## 6.2b: MiniMap FlushMatrices FFP Matrix Removal (~4 GL calls)

### File: `rts/Game/UI/MiniMap.cpp`

The `FlushMatrices()` function writes FFP matrix state for downstream draws that auto-sync from `glGetFloatv`. Phase 5.9 added `RenderBuffer::globalProjection/globalModelView` cache as the Metal-compatible replacement. The cache is already updated here — the FFP writes are redundant for Metal but kept for Lua backward compat.

**No change needed.** The 4 FFP matrix calls in `FlushMatrices()` (lines 1081-1084) must be kept because:
1. They're already accompanied by the global cache update (lines 1087-1088)
2. Lua `gl.GetProjectionMatrix()`/`gl.GetModelViewMatrix()` still reads FFP state
3. Removing them would break Lua scripts

The 3 calls in `ApplyConstraintsMatrix()` (lines 1096-1100) are only called from LuaOpenGL.cpp — Lua API, deferred.

**Status:** MiniMap is **already Metal-ready** via the global cache. FFP calls are Lua backward-compat only.

---

## 6.2c: GuiHandler Texture Binding + Stencil Query (~4 GL calls)

### File: `rts/Game/UI/GuiHandler.cpp`

#### Stencil query (line 102):
```cpp
// BEFORE:
GLint stencilBits;
glGetIntegerv(GL_STENCIL_BITS, &stencilBits);

// AFTER:
auto* device = RHI::GetDevice();
int stencilBits = device ? device->GetDepthBufferBitDepth() : 0;
// Note: GetDepthBufferBitDepth() exists but we actually need stencil bits.
// Check if IRHIDevice has a stencil query. If not, keep the GL call
// with a headless guard.
```

**Investigation needed:** Does `IRHIDevice` expose stencil buffer bit depth? If not, this is a one-line addition to the interface, or we keep the GL call (it's init-time only, not a hot path).

#### Texture binding (lines 2715, 2787, 2851):
These bind external texture IDs from `CUnitDrawer::GetUnitDefImage()` and `texInfo->id`. These are raw `GLuint` IDs managed by external systems.

**Options:**
1. Wrap with `RHI::IRHITexture` via `WrapExistingTexture()` — but unit def images are managed by icon handler, would need wrapping at source
2. Keep raw `glBindTexture` with headless guard — acceptable since the icon textures are GL-managed

**Recommendation:** Keep the 3 `glBindTexture` calls as-is for now. They're in the GUI drawing path which uses GL::SubState for blend/depth. Full migration requires the icon/texture subsystem to provide RHI wrappers, which is a cross-cutting concern beyond this phase.

---

## Files Modified Summary

| File | Change |
|------|--------|
| `rts/Rendering/GL/glExtra.h` | Expand allObjects tuple with 2 `unique_ptr<IRHIBuffer>`, add include |
| `rts/Rendering/GL/glExtra.cpp` | Dual-path buffer create in CreateGLObjects(), RHI draw in 5 draw functions |

**Total: 2 files, ~5 GL calls migrated to headless-only fallback.**

---

## What This Does NOT Change

- **MiniMap FlushMatrices FFP calls** — kept for Lua backward compat (cache already syncs for Metal)
- **MiniMap ApplyConstraintsMatrix** — Lua API, deferred
- **GuiHandler glBindTexture** — external texture IDs, needs cross-cutting icon wrapper migration
- **GuiHandler glGetIntegerv** — init-time stencil query, low priority
- **WorldDrawer ResetMVPMatrices** — intentionally kept (documented RHI_TODO)
- **LuaOpenGL** — 421 calls, separate major phase
- **RmlUi** — 549 calls, separate major phase

---

## Verification

```bash
# Build both targets
cmake --build build-arm64/ --target engine-headless -j$(sysctl -n hw.ncpu)
cmake --build build-arm64/ --target engine-legacy -j$(sysctl -n hw.ncpu)

# Verify GL::Shapes RHI buffer usage
grep -n "rhiVertBuf\|rhiIndxBuf\|BindVertexBuffer\|BindIndexBuffer\|DrawIndexed" \
    rts/Rendering/GL/glExtra.cpp

# Confirm glDrawElements is only in headless fallback
grep -n "glDrawElements" rts/Rendering/GL/glExtra.cpp
# Should show calls inside `else` branches only
```

---

## What Comes Next (Post-6.2 Roadmap)

After Phase 6.2, the **Phase 6.x buffer/draw migration series is complete**. All game-world and debug rendering has RHI paths.

The remaining GL calls fall into three large categories requiring fundamentally different approaches:

| Phase | Domain | Calls | Approach |
|-------|--------|-------|----------|
| 7.0 | Lua GL bindings | ~666 | Design Lua RHI query/mutation API |
| 7.1 | RmlUi renderer | ~549 | Write new RHI-based RmlUi backend |
| 7.2 | GL utilities (VBO/VAO/FBO classes) | ~395 | Deprecate as callers migrate |
| 7.3 | GlobalRendering + misc | ~114 | Platform boundary; mostly SDL (must stay) |
