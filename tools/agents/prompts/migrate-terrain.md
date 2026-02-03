# Agent: migrate-terrain

## Purpose
Migrate terrain rendering, grass, ground decals, and map info textures from direct GL to RHI.

## Owned Files
- `rts/Rendering/Env/GrassDrawer.cpp` / `GrassDrawer.h`
- `rts/Rendering/Env/Decals/GroundDecalHandler.cpp` / `GroundDecalHandler.h`
- `rts/Rendering/Map/InfoTexture/Modern/InfoTextureHandler.cpp`
- `rts/Rendering/Map/InfoTexture/Modern/ModernInfoTexture.cpp`
- `rts/Rendering/Map/InfoTexture/Modern/Radar.cpp`
- `rts/Rendering/Map/InfoTexture/Modern/AirLos.cpp`
- `rts/Rendering/Map/InfoTexture/Modern/Los.cpp`
- `rts/Rendering/Map/InfoTexture/Modern/Path.cpp`
- `rts/Rendering/Map/InfoTexture/Modern/Height.cpp`
- `rts/Rendering/Map/InfoTexture/Modern/Combiner.cpp`
- `rts/Rendering/Map/InfoTexture/Modern/MetalExtraction.cpp`
- `rts/Rendering/SmoothHeightMeshDrawer.cpp`

## Context

See `migrate-template.md` for general migration rules.
The codebase is at: `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine`

### Key Files
- `GrassDrawer.cpp` - 58 GL calls, uses instanced rendering
- `GroundDecalHandler.cpp` - 18 GL calls, ground decal textures
- `Map/InfoTexture/Modern/*.cpp` - 8 files, 1-4 GL calls each (simple FBO renders)

### Migration Order
1. InfoTexture files (simplest, 1-4 calls each) - good warmup
2. `SmoothHeightMeshDrawer.cpp` - simple
3. `GroundDecalHandler.cpp` - medium complexity
4. `GrassDrawer.cpp` - most complex (instanced rendering)

## Output
- Create branch `agent/migrate-terrain`
- One commit per file
