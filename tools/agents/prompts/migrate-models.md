# Agent: migrate-models

## Purpose
Migrate unit/model rendering and shadow mapping from direct GL to RHI.

## Owned Files
- `rts/Rendering/Models/3DModelVAO.cpp` / `3DModelVAO.hpp`
- `rts/Rendering/Units/UnitDrawer.cpp` / `UnitDrawer.h`
- `rts/Rendering/Common/ModelDrawer.h`
- `rts/Rendering/Common/ModelDrawerState.cpp` / `ModelDrawerState.h`
- `rts/Rendering/Common/ModelDrawerHelpers.cpp` / `ModelDrawerHelpers.h`
- `rts/Rendering/LuaObjectDrawer.cpp` / `LuaObjectDrawer.h`
- `rts/Rendering/ShadowHandler.cpp` / `ShadowHandler.h`
- `rts/Rendering/DepthBufferCopy.cpp` / `DepthBufferCopy.h`

## Context

See `migrate-template.md` for general migration rules.
The codebase is at: `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine`

### Key Architecture

**3DModelVAO** is the central model rendering system:
- Singleton managing all model vertex/index data
- `ProcessVertices()` / `ProcessIndicies()` for model preparation
- 2MB+ instance buffers for batched rendering
- `DrawElements()` for GPU-to-GPU rendering
- 18 GL calls

**UnitDrawer** coordinates rendering of all units:
- 54 GL calls
- Uses ModelDrawer base class
- Manages draw passes (opaque, alpha, shadow)

**ShadowHandler** manages shadow mapping:
- 26 GL calls
- Single FBO for shadow depth map
- Shadow matrix computation

### Migration Order
1. `DepthBufferCopy.cpp` - simplest (7 GL calls, simple blit)
2. `ShadowHandler.cpp` - single FBO, well-contained
3. `ModelDrawerState.cpp` - GL state for model rendering
4. `ModelDrawerHelpers.cpp` - helper draw functions
5. `3DModelVAO.cpp` - central model system
6. `UnitDrawer.cpp` - unit rendering coordination
7. `LuaObjectDrawer.cpp` - Lua-driven model rendering
8. `ModelDrawer.h` - base class (may be header-only changes)

## Output
- Create branch `agent/migrate-models`
- One commit per file
