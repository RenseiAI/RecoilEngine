# Agent: ci-pipeline

## Purpose
Set up GitHub Actions CI/CD for ARM64 macOS builds, including Metal backend testing.
This enables automated building, testing, and artifact generation for the ARM64+Metal port.

## Owned Files
- `.github/workflows/macos-arm64.yml` (NEW)
- `.github/workflows/build-matrix.yml` (MODIFY if exists, or NEW)
- `tools/ci/build-macos.sh` (NEW)
- `tools/ci/test-metal.sh` (NEW)
- `tools/ci/test-opengl.sh` (NEW)

## Prerequisites
- All previous tiers completed and merged (Tiers 1-4)
- Metal backend functional (`rts/Rendering/RHI/Metal/` implemented)
- OpenGL backend functional (`rts/Rendering/RHI/OpenGL/` implemented)
- Shader translations complete (`cont/base/springcontent/shaders/Metal/`)
- Lua RHI bindings migrated

## Context

The codebase is at: `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine`
Branch: `arm64-metal-port`

### Existing CI Infrastructure

The project has existing (currently disabled) CI workflows in `.github/workflows/`:
- `engine-build.yml.disabled` - Main build workflow using Docker and namespace.so runners
- `release-build.yml.disabled` - Release packaging workflow
- `docker-build-v2/` - Docker-based build scripts for Linux/Windows

Key observations from the existing setup:
1. Uses namespace.so GitHub Actions Runners with caching
2. Docker containers for cross-compilation (amd64-linux, amd64-windows)
3. ccache with bazel-remote S3 storage for build caching
4. CMake + Ninja build system
5. Build matrices for multiple platforms

### Build System

The engine uses CMake (3.27+) with C++23:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -G Ninja
cmake --build build
```

Build targets:
- `spring` - Full engine (requires graphics)
- `spring-headless` - Headless mode (no graphics, good for CI unit tests)
- `spring-dedicated` - Dedicated server
- `unitsync` - Content sync library
- `test` - Unit tests (CTest)

ARM64/Metal-specific CMake options:
- `CMAKE_OSX_ARCHITECTURES=arm64` - Target ARM64
- The build system auto-detects ARM64 and configures appropriately
- Metal backend is selected automatically on macOS ARM64

### Testing Infrastructure

Existing tests in `test/CMakeLists.txt`:
- Unit tests using Catch2 framework
- Tests for: Float3, Matrix44f, SpringTime, ThreadPool, FileSystem, etc.
- Headless engine tests via `spring-headless --test-creg`
- Tests can be run via `ctest` or `make check`

For Metal/rendering tests:
- Full rendering tests require a display (cannot run in standard CI)
- Headless tests work without GPU
- Consider using macOS's virtual framebuffer or screenshot comparison

### macOS ARM64 Runners

GitHub provides `macos-14` (Sonoma) runners with M1 chips:
```yaml
runs-on: macos-14  # Apple Silicon (M1)
```

These runners have:
- Xcode 15.x with Metal SDK
- CMake, Ninja available via Homebrew
- Limited build minutes (more expensive than Linux)

## Tasks

### 1. Create macOS ARM64 Build Workflow

Create `.github/workflows/macos-arm64.yml`:

```yaml
name: macOS ARM64 Build

on:
  push:
    branches: [arm64-metal-port, master]
    paths-ignore: ['doc/**', '*.md']
  pull_request:
    branches: [arm64-metal-port, master]
    paths-ignore: ['doc/**', '*.md']
  workflow_dispatch:
    inputs:
      build_type:
        description: 'Build type (Debug, Release, RelWithDebInfo)'
        default: 'RelWithDebInfo'

jobs:
  build-macos-arm64:
    runs-on: macos-14  # Apple Silicon M1
    # ... configuration
```

The workflow should:
- Install dependencies via Homebrew (SDL2, OpenAL, etc.)
- Configure with CMake for ARM64
- Build all targets (spring, spring-headless, spring-dedicated, unitsync)
- Run unit tests
- Upload build artifacts
- Cache Homebrew packages and ccache

### 2. Create Build Matrix Workflow

Create `.github/workflows/build-matrix.yml` that builds for:
- amd64-linux (existing Docker approach)
- amd64-windows (existing Docker approach)
- arm64-macos (new native approach)

This provides a unified view of all platform build status.

### 3. Create macOS Build Script

Create `tools/ci/build-macos.sh`:

```bash
#!/bin/bash
set -euo pipefail

# Build script for macOS ARM64
# Can be used locally and in CI

BUILD_TYPE="${BUILD_TYPE:-RelWithDebInfo}"
BUILD_DIR="${BUILD_DIR:-build-arm64}"
JOBS="${JOBS:-$(sysctl -n hw.ncpu)}"

# Install dependencies if running in CI
if [[ "${CI:-}" == "true" ]]; then
    brew install sdl2 openal-soft libvorbis freetype p7zip ninja ccache
fi

# Configure
cmake -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -G Ninja

# Build
cmake --build "$BUILD_DIR" -j"$JOBS"
```

### 4. Create Metal Test Script

Create `tools/ci/test-metal.sh`:

```bash
#!/bin/bash
set -euo pipefail

# Test Metal backend
# Note: Requires display for full rendering tests

BUILD_DIR="${BUILD_DIR:-build-arm64}"

# Run unit tests first (no display required)
cd "$BUILD_DIR"
ctest --output-on-failure

# Check Metal device availability
if system_profiler SPDisplaysDataType | grep -q "Metal Support"; then
    echo "Metal supported on this machine"

    # Run headless tests
    ./spring-headless --test-creg

    # If display available, run quick render test
    # (This would require a test game/map and screenshot comparison)
else
    echo "Warning: Metal not available, skipping GPU tests"
fi
```

### 5. Create OpenGL Test Script

Create `tools/ci/test-opengl.sh`:

```bash
#!/bin/bash
set -euo pipefail

# Test OpenGL backend on macOS
# OpenGL is deprecated on macOS but still available

BUILD_DIR="${BUILD_DIR:-build-arm64}"

cd "$BUILD_DIR"
ctest --output-on-failure

# Run headless tests
./spring-headless --test-creg
```

### 6. Configure Caching

The workflow should cache:
- Homebrew packages: `~/Library/Caches/Homebrew`
- ccache: `~/.ccache`
- CMake build directory (partial, for incremental builds)

Example cache configuration:
```yaml
- name: Cache Homebrew
  uses: actions/cache@v4
  with:
    path: |
      ~/Library/Caches/Homebrew
      /opt/homebrew
    key: homebrew-${{ runner.os }}-${{ hashFiles('.github/workflows/macos-arm64.yml') }}

- name: Cache ccache
  uses: actions/cache@v4
  with:
    path: ~/.ccache
    key: ccache-macos-arm64-${{ github.ref }}-${{ github.sha }}
    restore-keys: |
      ccache-macos-arm64-${{ github.ref }}-
      ccache-macos-arm64-
```

### 7. Configure Artifacts

Upload build artifacts for each successful build:
```yaml
- name: Upload artifacts
  uses: actions/upload-artifact@v4
  with:
    name: recoil-macos-arm64-${{ github.sha }}
    path: |
      ${{ github.workspace }}/build-arm64/spring
      ${{ github.workspace }}/build-arm64/spring-headless
      ${{ github.workspace }}/build-arm64/spring-dedicated
      ${{ github.workspace }}/build-arm64/libunitsync.dylib
    retention-days: 14
```

### 8. Handle Dependencies

macOS ARM64 build dependencies (Homebrew):
```bash
brew install \
    cmake ninja ccache \
    sdl2 \
    openal-soft \
    libvorbis libogg \
    freetype \
    p7zip \
    minizip \
    curl \
    jsoncpp
```

Note: Some dependencies may need architecture-specific handling:
- Ensure all libraries are ARM64 native (not Rosetta)
- Use `arch -arm64 brew install` if needed

### 9. Add Status Badges

Document how to add status badges to README:
```markdown
[![macOS ARM64](https://github.com/beyond-all-reason/RecoilEngine/actions/workflows/macos-arm64.yml/badge.svg)](https://github.com/beyond-all-reason/RecoilEngine/actions/workflows/macos-arm64.yml)
```

### 10. Handle Rendering Tests (Advanced)

For CI rendering validation without a display:
1. Use virtual framebuffer (limited on macOS)
2. Generate reference screenshots locally
3. Compare against reference in CI using `spring-headless` output
4. Consider macOS's `screencapture` in headless mode

Minimal approach: verify Metal backend initializes without crash:
```bash
# Quick smoke test - run for 1 frame then exit
timeout 10 ./spring --test-metal-init || echo "Metal init test"
```

## Workflow File Structure

Final `.github/workflows/macos-arm64.yml` should include:

```yaml
name: macOS ARM64 Build

on:
  push:
    branches: [arm64-metal-port, master]
  pull_request:
    branches: [arm64-metal-port, master]
  workflow_dispatch:

env:
  BUILD_TYPE: RelWithDebInfo
  CCACHE_DIR: ~/.ccache
  CCACHE_MAXSIZE: 1G

jobs:
  build:
    runs-on: macos-14
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive
          fetch-depth: 0

      - name: Cache Homebrew
        uses: actions/cache@v4
        # ...

      - name: Cache ccache
        uses: actions/cache@v4
        # ...

      - name: Install dependencies
        run: |
          brew install sdl2 openal-soft libvorbis freetype p7zip ninja ccache
          echo "$(brew --prefix ccache)/libexec" >> $GITHUB_PATH

      - name: Configure
        run: |
          cmake -B build-arm64 \
            -DCMAKE_BUILD_TYPE=${{ env.BUILD_TYPE }} \
            -DCMAKE_OSX_ARCHITECTURES=arm64 \
            -DCMAKE_C_COMPILER_LAUNCHER=ccache \
            -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
            -G Ninja

      - name: Build
        run: cmake --build build-arm64 -j$(sysctl -n hw.ncpu)

      - name: Test
        run: |
          cd build-arm64
          ctest --output-on-failure

      - name: Upload artifacts
        uses: actions/upload-artifact@v4
        # ...
```

## Verification

After creating the workflows:

1. **Local verification:**
   ```bash
   # Test build script locally
   chmod +x tools/ci/build-macos.sh
   ./tools/ci/build-macos.sh

   # Test the test scripts
   chmod +x tools/ci/test-metal.sh tools/ci/test-opengl.sh
   ./tools/ci/test-metal.sh
   ```

2. **CI verification:**
   - Push to a test branch
   - Verify workflow triggers
   - Check build succeeds
   - Check tests pass
   - Verify artifacts are uploaded

3. **Workflow syntax validation:**
   ```bash
   # Use actionlint if available
   actionlint .github/workflows/macos-arm64.yml
   ```

## Output
- Create branch `agent/ci-pipeline`
- Commit workflow files atomically:
  1. Add `tools/ci/build-macos.sh`
  2. Add `tools/ci/test-metal.sh` and `tools/ci/test-opengl.sh`
  3. Add `.github/workflows/macos-arm64.yml`
  4. Add `.github/workflows/build-matrix.yml` (if creating unified matrix)
- Document any issues or limitations discovered
- Report workflow file locations and how to trigger them
