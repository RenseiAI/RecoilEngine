# Agent: migrate-toplevel

## Purpose
Migrate top-level rendering coordination, global rendering context, and GL utility
files from direct GL to RHI.

## Owned Files
- `rts/Rendering/WorldDrawer.cpp` / `WorldDrawer.h`
- `rts/Rendering/GlobalRendering.h` / `GlobalRendering.cpp`
- `rts/Rendering/GL/myGL.h` / `myGL.cpp`
- `rts/Rendering/GL/glExtra.h` / `glExtra.cpp`
- `rts/Rendering/GL/glHelpers.h`
- `rts/Rendering/GL/glStateDebug.h` / `glStateDebug.cpp`
- `rts/Rendering/GL/LightHandler.cpp` / `LightHandler.h`
- `rts/Rendering/GL/VertexArray.cpp` / `VertexArray.h`

## Context

See `migrate-template.md` for general migration rules.
The codebase is at: `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine`

### GlobalRendering (most important)

`CGlobalRendering` is the central rendering singleton. It manages:
- SDL window and GL context creation
- Display/window geometry
- Capability flags (haveGL4, supportPersistentMapping, etc.)
- Multisampling, vsync settings

For RHI: `GlobalRendering` becomes the `RHIDevice` owner. It needs to:
1. Create either a GL context or Metal context based on config
2. Store an `RHIDevice*` instead of (or alongside) `SDL_GLContext`
3. Expose capabilities through `RHIDevice` instead of raw flags

### WorldDrawer

Coordinates initialization and per-frame rendering of all subsystems.
26 GL calls. After other subsystems are migrated, WorldDrawer's GL calls
should be minimal (mostly glClear, viewport setup).

### GL Utilities

- `myGL.h` - Core GL include and helper macros. This becomes a legacy include.
  After full migration, only the OpenGL backend should include it.
- `glExtra.h/cpp` - Drawing utilities (circles, rectangles, etc.) - 24 GL calls
- `glHelpers.h` - Small helper functions
- `glStateDebug.h/cpp` - GL debug state queries - 30 GL calls
- `LightHandler.cpp` - UBO-based lighting - 6 GL calls
- `VertexArray.cpp` - Legacy immediate-mode emulation - 56 GL calls
  Consider marking as deprecated; it uses glBegin/glEnd style

### Migration Order
1. `VertexArray.cpp` - legacy, consider wrapping minimally or marking deprecated
2. `glHelpers.h` - small helpers, convert to RHI equivalents
3. `glExtra.h/cpp` - drawing utilities
4. `LightHandler.cpp` - UBO management
5. `glStateDebug.h/cpp` - debug queries (Metal has its own debug tools)
6. `myGL.h/cpp` - make it only included by OpenGL backend
7. `WorldDrawer.cpp` - top-level coordinator
8. `GlobalRendering.h/cpp` - make backend-agnostic (most impactful change)

## Output
- Create branch `agent/migrate-toplevel`
- One commit per file
- GlobalRendering changes are the most architecturally significant - document design decisions
