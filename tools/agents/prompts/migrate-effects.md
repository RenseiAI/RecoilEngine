# Agent: migrate-effects

## Purpose
Migrate particle rendering, HUD, icons, and debug drawing from direct GL to RHI.

## Owned Files
- `rts/Rendering/Env/Particles/ProjectileDrawer.cpp` / `ProjectileDrawer.h`
- `rts/Rendering/Env/Particles/Classes/FlyingPiece.cpp`
- `rts/Rendering/HUDDrawer.cpp` / `HUDDrawer.h`
- `rts/Rendering/IconHandler.cpp` / `IconHandler.h`
- `rts/Rendering/CommandDrawer.cpp` / `CommandDrawer.h`
- `rts/Rendering/InMapDrawView.cpp` / `InMapDrawView.h`
- `rts/Rendering/LineDrawer.cpp` / `LineDrawer.h`
- `rts/Rendering/DebugDrawerAI.cpp` / `DebugDrawerAI.h`
- `rts/Rendering/DebugVisibilityDrawer.cpp` / `DebugVisibilityDrawer.h`
- `rts/Rendering/QTPFSPathDrawer.cpp` / `QTPFSPathDrawer.h`
- `rts/Rendering/HAPFSPathDrawer.cpp` / `HAPFSPathDrawer.h`

## Context

See `migrate-template.md` for general migration rules.
The codebase is at: `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine`

### Key Files
- `ProjectileDrawer.cpp` - 44 GL calls, particle rendering with blending
- `HUDDrawer.cpp` - 4 GL calls (simple, uses RenderBuffers)
- `IconHandler.cpp` - 3 GL calls
- `HAPFSPathDrawer.cpp` - 6 GL calls, pathfinding visualization

### Migration Order (simplest first)
1. `HUDDrawer.cpp` - 4 calls
2. `IconHandler.cpp` - 3 calls
3. `CommandDrawer.cpp` - 12 calls
4. `InMapDrawView.cpp` - 10 calls
5. `LineDrawer.cpp` - 11 calls
6. `DebugDrawerAI.cpp` - 13 calls
7. `DebugVisibilityDrawer.cpp` - 10 calls
8. `QTPFSPathDrawer.cpp` - 4 calls
9. `HAPFSPathDrawer.cpp` - 6 calls
10. `FlyingPiece.cpp` - 2 calls
11. `ProjectileDrawer.cpp` - 44 calls (most complex, do last)

## Output
- Create branch `agent/migrate-effects`
- One commit per file
