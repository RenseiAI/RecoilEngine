# Agent: migrate-sky

## Purpose
Migrate sky rendering and environment cubemap subsystems from direct GL to RHI.

## Owned Files
- `rts/Rendering/Env/ISky.cpp` / `ISky.h`
- `rts/Rendering/Env/ModernSky.cpp` / `ModernSky.h`
- `rts/Rendering/Env/SkyBox.cpp` / `SkyBox.h`
- `rts/Rendering/Env/CubeMapHandler.cpp` / `CubeMapHandler.h`
- `rts/Rendering/Env/DebugCubeMapTexture.cpp` / `DebugCubeMapTexture.h`

## Context

See `migrate-template.md` for general migration rules.
The codebase is at: `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine`

### Migration Order
1. `ISky.cpp` - base interface (minimal GL)
2. `ModernSky.cpp` - shader-based sky (6 GL calls)
3. `SkyBox.cpp` - cubemap skybox (27 GL calls)
4. `CubeMapHandler.cpp` - cubemap FBO rendering (20 GL calls)
5. `DebugCubeMapTexture.cpp` - debug visualization (17 GL calls)

## Output
- Create branch `agent/migrate-sky`
- One commit per file
