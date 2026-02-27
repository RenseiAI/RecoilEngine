# Metal Debug Iteration Log

## Current Status
- Last iteration: 0 (baseline)
- Commit: 986f15e165
- Shaders: 84/84, Draw calls: 37, FPS: 48.9
- Exit: SHUTDOWN_HANG (widget EXIT_SUCCESS, process timeout at Kill[3])
- Visual: Black screen + faint sky gradient upper-right

## Priority Queue
1. BLACK SCREEN — 37 draw calls happen but nothing visible on screen
2. SHUTDOWN_HANG — hangs at SpringApp::Kill[3] after widget exit
3. Font path error — doubled path in font loading
4. Texture swizzle warnings — incorrect texture format handling
5. ModernSky disabled — falls back to NullSky

## Iteration History

### Iteration 0 — 2026-02-27 (baseline)
- Run: 20260226_201955
- Commit: 986f15e165
- Metrics: shaders=84/84 draws=37 fps=48.9 exit=SHUTDOWN_HANG screenshots=12(7 game)
- Screenshots: 5 menu + 7 game (all black + sky gradient)
- Notes: Engine loads BAR, enters gameplay, runs stable 300 frames. All rendering
  infrastructure works (shaders compile, PSOs create, draw calls issue) but final
  framebuffer content is not visible. Likely a presentation/blit/viewport issue.
