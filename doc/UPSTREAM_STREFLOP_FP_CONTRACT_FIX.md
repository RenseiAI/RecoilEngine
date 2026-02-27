# Upstream PR: STREFLOP `-ffp-contract=off` fix

## Summary

STREFLOP's bundled libm (software transcendentals) is missing `-ffp-contract=off`
in its per-file compile flags. On ARM64, the compiler emits `fmadd`/`fmsub`
instructions in the libm code, which have different rounding than separate
multiply+add and produce 252 transcendental mismatches (1-3 ULP) against x86_64 SSE.

This is a **desync bug** for any ARM64 Linux build using STREFLOP_NEON.

## The Bug

`rts/lib/streflop/CMakeLists.txt` line 73-85:

```cmake
SET(cxxflags "${cxxflags} -w -O3")           # <-- no -ffp-contract=off
...
SET_SOURCE_FILES_PROPERTIES(${libm_flt32_source}
    PROPERTIES COMPILE_FLAGS "-DLIBM_COMPILING_FLT32 ${cxxflags}")
```

The per-file `COMPILE_FLAGS` property is appended *after* the target's `CXX_FLAGS`.
On x86_64, `-mfpmath=sse -msse` constrains to scalar SSE ops (no FMA available),
so the bug is latent. On ARM64 with `-march=armv8-a+simd`, the compiler is free to
emit fused multiply-add instructions unless explicitly told not to.

## The Fix

One line change in `rts/lib/streflop/CMakeLists.txt`:

```diff
-	SET(cxxflags "${cxxflags} -w -O3")
+	SET(cxxflags "${cxxflags} -w -O3 -ffp-contract=off")
```

## Evidence

Tested with `tools/sync-test/` (47,852 operations across basic arithmetic,
transcendentals, double precision, and compound operations):

| Configuration | vs x86_64 SSE (GCC) | Notes |
|---------------|---------------------|-------|
| ARM64 NEON **without** fix | 99.5% match, 252 mismatches | All in transcendentals, 1-3 ULP |
| ARM64 NEON **with** fix | **100% bit-exact** | Zero mismatches |
| ARM64 SOFT (reference) | 100% bit-exact | Software float, always exact |

The 252 mismatches are in `cbrt`, `log`, `log10`, `pow`, `sin`, `atan2` — all
functions where the libm C code uses `a * b + c` patterns that the compiler fuses
into `fmadd` on ARM64.

Disassembly confirms: `objdump -d` of `s_cbrtf.cpp.o` shows 4 `fmadd` instructions
without the fix, 0 with it.

## Impact on Upstream

- **x86_64 Linux**: No impact (latent bug, masked by `-mfpmath=sse`)
- **ARM64 Linux** (e.g., Raspberry Pi, AWS Graviton): **Active desync bug** if
  building with `STREFLOP_NEON`. The engine's root `CMAKE_CXX_FLAGS` may include
  `-ffp-contract=off` (via `TestCXXFlags.cmake` / `FP_CONTRACT_FLAG`), which would
  mask it in practice — but this depends on CMake flag ordering and is fragile.
- **macOS ARM64**: Currently disabled (`NOT_USING_STREFLOP`), so not hit today.

## Validation Steps

1. Clone upstream `beyond-all-reason/RecoilEngine`
2. Build the sync test:
   ```bash
   cmake -B build -S tools/sync-test -DSTREFLOP_MODE=NEON
   cmake --build build --target streflop-float-test -j$(nproc)
   ```
3. Check for FMA in libm objects:
   ```bash
   objdump -d build/streflop/CMakeFiles/streflop.dir/libm/flt-32/s_cbrtf.cpp.o \
     | grep -c "fmadd\|fmsub\|fnmadd\|fnmsub"
   ```
   - Without fix: 4+ FMA instructions
   - With fix: 0
4. Run the test and compare against x86_64 reference:
   ```bash
   ./build/streflop-float-test
   python3 tools/sync-test/compare_results.py \
     tools/sync-test/reference/streflop_results_SSE_x86_64.bin \
     streflop_results_NEON_arm64.bin
   ```

## Additional Context

The upstream streflop CMakeLists also unconditionally sets `-mfpmath=sse -msse`
on all non-MSVC/non-icpc compilers (line 78), which would break ARM64 builds
entirely. A companion fix should gate this behind an architecture check:

```cmake
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|armv8")
    SET(cxxflags "${cxxflags} -march=armv8-a+simd")
else()
    SET(cxxflags "${cxxflags} -mfpmath=sse -msse")
endif()
```

## Origin

Discovered during ARM64 Metal port (`arm64-metal-port` branch on `supaku/RecoilEngine`).
Reference results and test harness at `tools/sync-test/`.
