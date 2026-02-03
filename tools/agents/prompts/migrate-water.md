# Agent: migrate-water

## Purpose
Migrate all water rendering subsystems from direct GL calls to the RHI.

## Owned Files
- `rts/Rendering/Env/BasicWater.cpp` / `BasicWater.h`
- `rts/Rendering/Env/AdvWater.cpp` / `AdvWater.h`
- `rts/Rendering/Env/DynWater.cpp` / `DynWater.h` (170 GL calls - largest single file)
- `rts/Rendering/Env/BumpWater.cpp` / `BumpWater.h`
- `rts/Rendering/Env/RefractWater.cpp` / `RefractWater.h`
- `rts/Rendering/Env/IWater.cpp` / `IWater.h`

## Context

See `migrate-template.md` for general migration rules.

The codebase is at: `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine`

### Water Architecture

`IWater` is the base interface with a static factory `SetWater(int mode)`.
5 implementations exist, from simplest to most complex:
- `BasicWater` - simple textured plane
- `AdvWater` - reflective water
- `RefractWater` - reflection + refraction
- `DynWater` - dynamic simulation (170 GL calls!)
- `BumpWater` - bump-mapped with coast blurring

### DynWater (most complex)

`DynWater.cpp` has 170 direct GL calls including:
- Multiple FBOs for simulation steps
- Texture ping-pong for wave simulation
- Heavy use of glBlendFunc, glEnable/glDisable per-pass
- Direct glBegin/glEnd style vertex submission (via VertexArray)

This will be the hardest file. Migrate it last within this agent.

### Migration Order
1. `IWater.cpp` - base class, minimal GL
2. `BasicWater.cpp` - simplest implementation
3. `AdvWater.cpp` - adds reflection
4. `RefractWater.cpp` - adds refraction FBO
5. `BumpWater.cpp` - complex but well-structured
6. `DynWater.cpp` - most complex, do last

## Output
- Create branch `agent/migrate-water` from current HEAD
- One commit per file
- Document any shader changes needed (water shaders use gl_ClipDistance)
