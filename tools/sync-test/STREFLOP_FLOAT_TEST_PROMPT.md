# Standalone STREFLOP Float Comparison Test

## Goal

Create a standalone C++ test binary that links against the RecoilEngine's bundled streflop library and outputs bit-exact results for a deterministic sequence of floating-point operations. The output file can then be compared byte-for-byte between x86_64 (STREFLOP_SSE) and ARM64 (STREFLOP_SSE via sse2neon, STREFLOP_NEON, or STREFLOP_SOFT) to identify any cross-architecture desync sources.

## Context

The Recoil/Spring RTS engine uses a deterministic lockstep simulation. All clients must produce bit-identical simulation results. The engine uses `STREFLOP_SSE` on x86_64 to control FPU behavior (rounding, denormals, precision) and routes all sim-relevant math through streflop's bundled libm (pure C reimplementations of transcendental functions).

On ARM64, there are three possible STREFLOP modes to test:
1. **STREFLOP_SSE via sse2neon** — translates SSE intrinsics to NEON. The engine's streflop CMake already sets `-march=armv8-a+simd` for ARM64. sse2neon has precision flags that may need enabling for IEEE compliance.
2. **STREFLOP_NEON** — native NEON path, uses FPCR register to control rounding/denormals. Already defined in streflop headers.
3. **STREFLOP_SOFT** — pure software IEEE 754 via bundled SoftFloat. Architecture-independent by definition but may differ from SSE hardware results.

The key question: **which (if any) of these modes produces bit-identical results to STREFLOP_SSE on x86_64?**

## Repository Layout

```
RecoilEngine/
├── rts/lib/streflop/           # Bundled streflop library
│   ├── CMakeLists.txt          # Build config (sets STREFLOP_SOFT for arm64)
│   ├── streflop.h              # Main header, mode selection guards
│   ├── streflop_cond.h         # Conditional include (STREFLOP_ENABLED)
│   ├── SMath.h                 # All transcendental function wrappers
│   ├── SMath.cpp               # Constants (NaN, Inf)
│   ├── FPUSettings.h           # FPU init per mode (SSE/NEON/SOFT/X87)
│   ├── Random.h/cpp            # Reproducible RNG
│   ├── SoftFloatWrapper.h/cpp  # STREFLOP_SOFT wrapper types
│   └── libm/                   # Bundled libm (flt-32/ and flt-64/)
├── CMakeLists.txt              # Root CMake (line ~437: STREFLOP mode selection)
└── tools/sync-test/            # <-- Put the test here
```

## What to Build

### `tools/sync-test/CMakeLists.txt`

A standalone CMake target (`streflop-float-test`) that:
- Links against the existing `streflop` library target from `rts/lib/streflop/`
- Can be configured with `-DSTREFLOP_MODE=SSE|NEON|SOFT` to override the auto-detected mode
- Builds as a simple command-line tool (no engine dependencies)

### `tools/sync-test/streflop_float_test.cpp`

The test binary should:

#### 1. Initialize streflop
```cpp
#include "lib/streflop/streflop_cond.h"

// Initialize FPU state exactly as the engine does
streflop::streflop_init<streflop::Simple>();
```

#### 2. Generate deterministic input values
Use streflop's own `RandomState` (from `Random.h`) or a simple reproducible PRNG (e.g., xorshift32 with fixed seed) to generate N float values spanning:
- Normal range: small positive, large positive, small negative, large negative
- Edge cases: denormals (1e-40f), near-zero (1e-38f), near-max (1e+38f)
- Special values: 0.0f, -0.0f, INFINITY, -INFINITY, NaN
- Values known to stress transcendentals: near pi, near pi/2, near 0, near 1

Use N = 10000 for thoroughness.

#### 3. Test categories

**Category A: Basic arithmetic** (these use hardware FPU, NOT bundled libm)
- Addition: a + b
- Subtraction: a - b
- Multiplication: a * b
- Division: a / b
- Fused operations: a * b + c (if compiler emits FMA)

This is the category most likely to differ between SSE and NEON.

**Category B: Streflop transcendentals** (bundled libm, should be identical)
These all go through streflop's C reimplementations regardless of STREFLOP mode:
- `streflop::sqrt`, `streflop::cbrt`
- `streflop::sin`, `streflop::cos`, `streflop::tan`
- `streflop::asin`, `streflop::acos`, `streflop::atan`, `streflop::atan2`
- `streflop::exp`, `streflop::log`, `streflop::log2`, `streflop::log10`
- `streflop::pow`
- `streflop::floor`, `streflop::ceil`, `streflop::round`, `streflop::trunc`
- `streflop::fmod`, `streflop::fabs`
- `streflop::sinh`, `streflop::cosh`, `streflop::tanh`

**Category C: Double precision** (same ops as A and B but with `streflop::Double`)
The engine uses doubles in some sim paths. Test the same operations.

**Category D: Compound operations** (simulate real engine patterns)
- Normalize a float3: `x/sqrt(x*x + y*y + z*z)`
- Dot product: `a.x*b.x + a.y*b.y + a.z*b.z`
- Linear interpolation: `a + t*(b-a)`
- Distance: `sqrt((x1-x2)^2 + (y1-y2)^2 + (z1-z2)^2)`
- Accumulated sum of 1000 small values (tests rounding accumulation)

#### 4. Output format

Write a binary file (`streflop_results_<mode>_<arch>.bin`) containing:
- 4-byte magic: `SFLT`
- 4-byte version: `0x00000001`
- Null-terminated mode string: e.g., `"STREFLOP_SSE\0"`
- Null-terminated arch string: e.g., `"x86_64\0"` or `"arm64\0"`
- For each test: 4-byte test ID (uint32), 4-byte raw float bits (uint32 via memcpy)

Also write a human-readable text file (`streflop_results_<mode>_<arch>.txt`) with:
```
# STREFLOP Float Test Results
# Mode: STREFLOP_SSE
# Arch: x86_64
# Compiler: Apple clang 16.0.0
# Date: 2026-02-27
#
# TestID  Category  Operation        Input(hex)       Result(hex)      Result(float)
0001      arith     add              3F800000+40000000 40400000         3.000000
0002      arith     mul              3F800000*40000000 40000000         2.000000
...
0500      trans     sin              3FC90FDB          3F7FFFFF         0.999999
```

#### 5. Comparison tool

Also write a simple `compare_results.py` (or second C++ mode) that:
- Reads two binary result files
- Reports: total tests, matching, mismatching
- For mismatches: prints test ID, operation, input, expected bits, actual bits, ULP difference
- Summary: which categories have mismatches (arithmetic? transcendentals? both?)

## Build and Run Instructions

The test should be buildable standalone:

```bash
# On x86_64 Linux/macOS (reference):
cmake -B build-x86 -DSTREFLOP_MODE=SSE
cmake --build build-x86 --target streflop-float-test
./build-x86/streflop-float-test

# On ARM64 macOS (test each mode):
cmake -B build-arm64-sse -DSTREFLOP_MODE=SSE    # via sse2neon
cmake --build build-arm64-sse --target streflop-float-test
./build-arm64-sse/streflop-float-test

cmake -B build-arm64-neon -DSTREFLOP_MODE=NEON
cmake --build build-arm64-neon --target streflop-float-test
./build-arm64-neon/streflop-float-test

cmake -B build-arm64-soft -DSTREFLOP_MODE=SOFT
cmake --build build-arm64-soft --target streflop-float-test
./build-arm64-soft/streflop-float-test

# Compare:
python3 compare_results.py streflop_results_SSE_x86_64.bin streflop_results_SSE_arm64.bin
python3 compare_results.py streflop_results_SSE_x86_64.bin streflop_results_NEON_arm64.bin
python3 compare_results.py streflop_results_SSE_x86_64.bin streflop_results_SOFT_arm64.bin
```

## Key Things to Watch For

1. **FMA (fused multiply-add)**: ARM64 compilers love to emit FMA instructions which have different rounding behavior than separate MUL+ADD. May need `-ffp-contract=off` compiler flag.
2. **Denormal handling**: SSE can be configured to flush denormals to zero (DAZ/FTZ bits in MXCSR). NEON has similar controls in FPCR. These must match.
3. **Rounding mode**: STREFLOP_SSE sets MXCSR to specific values. The equivalent FPCR settings on ARM must match.
4. **Compiler optimizations**: `-O2` vs `-O0` can change float codegen. Test should report compiler and flags.
5. **The transcendentals should match**: They're pure C software implementations. If they don't match, something is wrong with basic arithmetic (which feeds into the libm code).

## Important Notes

- This test does NOT require building the full engine
- It only depends on the streflop library (rts/lib/streflop/)
- The streflop library's CMakeLists.txt already handles ARM64 detection and sets `-march=armv8-a+simd`
- On macOS, the root CMakeLists.txt currently disables streflop entirely (`NOT_USING_STREFLOP`). The test's CMake must override this to actually test streflop modes.
- The root CMakeLists.txt is at: `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine/CMakeLists.txt`, line 437-453
- The streflop CMakeLists.txt is at: `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine/rts/lib/streflop/CMakeLists.txt`
