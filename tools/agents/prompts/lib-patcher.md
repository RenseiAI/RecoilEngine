# Agent: lib-patcher

## Purpose
Patch third-party libraries bundled in `rts/lib/` to compile and work correctly on ARM64.

## Owned Files
- `rts/lib/streflop/CMakeLists.txt`
- `rts/lib/streflop/FPUSettings.h` (ARM64 additions only)
- `rts/lib/CMakeLists.txt` (simdjson ARM64 config only)
- `rts/lib/squish/simd_sse.h`
- `rts/lib/squish/CMakeLists.txt` (if it exists)

## Context

The codebase is at: `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine`
Branch: `arm64-metal-port`

Three bundled libraries have x86-specific issues:

### 1. streflop - Floating-point determinism

streflop enforces deterministic floating-point behavior for multiplayer sync. It has
three modes: `STREFLOP_X87` (x87 FPU), `STREFLOP_SSE` (SSE control), `STREFLOP_SOFT`
(pure software IEEE 754 via SoftFloat library).

**Current state:**
- `rts/lib/streflop/CMakeLists.txt` line 75 hardcodes `-mfpmath=sse -msse`
- `rts/lib/streflop/FPUSettings.h` lines 223-237 contain x86 inline assembly macros
- The build globally defines `-DSTREFLOP_SSE`

**Required changes:**
- In `rts/lib/streflop/CMakeLists.txt`:
  - Skip `-mfpmath=sse -msse` when building for ARM64
  - Add ARM64 detection and define `STREFLOP_SOFT` instead of `STREFLOP_SSE`
- In `rts/CMakeLists.txt` (note: you don't own this file, document the needed change):
  - The global `-DSTREFLOP_SSE` definition needs to become architecture-conditional
  - On ARM64: `-DSTREFLOP_SOFT`
  - On x86: `-DSTREFLOP_SSE` (unchanged)

**STREFLOP_SOFT mode** uses the SoftFloat library already bundled at
`rts/lib/streflop/softfloat/`. It implements IEEE 754 in pure integer arithmetic,
making it deterministic across all platforms.

### 2. simdjson - JSON parsing

**Current state:**
- `rts/lib/CMakeLists.txt` around line 80 forces simdjson to its fallback implementation
  with ARM64 NEON explicitly disabled.

**Required changes:**
- Read the simdjson configuration in `rts/lib/CMakeLists.txt`
- Remove or conditionally skip the ARM64 exclusion
- simdjson natively supports ARM64 NEON and it's well-tested, so enabling it is safe

### 3. squish - DXT texture compression

**Current state:**
- `rts/lib/squish/simd_sse.h` line 29 includes `<xmmintrin.h>` directly
- The file implements a `Vec4` class using `__m128` SSE types
- This will not compile on ARM64

**Required changes:**
- Include `simd_compat.h` (from PR #2540, at `rts/System/simd_compat.h`) instead of
  raw `<xmmintrin.h>`. simd_compat.h routes to sse2neon on ARM64.
- OR include sse2neon directly with proper `#ifdef __aarch64__` guards
- The sse2neon library translates `__m128` and `_mm_*` intrinsics to NEON equivalents,
  so the squish code should work without further changes after the include fix.

## Verification
After patching, try to compile individual libraries:
```bash
# Test streflop compiles with STREFLOP_SOFT
cd rts/lib/streflop && cmake -B build -DSTREFLOP_SOFT=ON 2>&1

# Check simdjson ARM64 detection
grep -n "arm\|aarch\|neon\|SIMDJSON" rts/lib/CMakeLists.txt
```

## Output
- Create branch `agent/lib-patcher` from current HEAD
- Make atomic commits: one for streflop, one for simdjson, one for squish
- Document any cross-cutting changes needed (especially the STREFLOP define in rts/CMakeLists.txt)
