# Agent: app-bundle

## Purpose
Create proper macOS application bundle (.app) packaging for the ARM64 Metal build,
including code signing preparation, DMG creation, and CMake integration.

## Owned Files (all NEW)
- `tools/packaging/macos/create-bundle.sh`
- `tools/packaging/macos/Info.plist.in`
- `tools/packaging/macos/create-dmg.sh`
- `tools/packaging/macos/entitlements.plist`
- `tools/packaging/macos/bundle-dylibs.sh`
- `tools/packaging/macos/CMakeLists.txt`
- `rts/build/cmake/MacOSBundle.cmake` (CMake module)

## Prerequisites
- Metal backend functional (`metal-backend` agent completed)
- Successful ARM64 build (`build-verifier` confirms compilation)
- All shaders translated to MSL (`shader-translator` completed)
- ShaderTranslator tool built (for runtime Lua shader compilation)

## Context

The codebase is at: `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine`
Branch: `arm64-metal-port`

### Existing macOS Assets

The `installer/Mac/` directory already contains:
- `Info.plist` - Basic plist template (needs updating for ARM64/Metal)
- `spring.icns` - Application icon (131KB, ready to use)

The existing `Info.plist` uses:
- Bundle identifier: `com.springrts.Spring`
- Executable name: `spring`
- Version placeholder: `###VERSION###`
- `LSRequiresCarbon: true` (needs updating - Carbon is deprecated)

### macOS Application Bundle Structure

A proper `.app` bundle must follow this structure:
```
RecoilEngine.app/
  Contents/
    Info.plist              # Bundle metadata
    PkgInfo                 # Package type (APPL????)
    MacOS/
      spring                # Main executable
      spring-headless       # Headless server (optional)
      spring-dedicated      # Dedicated server (optional)
    Frameworks/             # Bundled dylibs
      libSDL2.dylib
      libfreetype.dylib
      libfontconfig.dylib
      libpng.dylib
      libogg.dylib
      libvorbis.dylib
      libvorbisfile.dylib
      libdevil.dylib
      ... (other dependencies)
    Resources/
      spring.icns           # Application icon
      base/                 # Game content (springcontent.sdz, etc.)
      fonts/
      shaders/Metal/        # Pre-compiled .metallib files
      ... (other data from cont/)
```

### Dependencies to Bundle

From the build system analysis, these libraries need bundling on macOS:
- **SDL2** - Window/input management
- **Freetype** - Font rendering
- **Fontconfig** - Font discovery
- **DevIL** - Image loading
- **libogg/libvorbis** - Audio decoding (if sound enabled)
- **libpng** - PNG support
- **zlib** - Compression
- **OpenAL** - Audio output (system framework, no bundling needed)

System frameworks (linked, not bundled):
- Metal.framework
- QuartzCore.framework
- Foundation.framework
- CoreFoundation.framework
- AudioToolbox.framework (for sound)

### Code Signing Requirements

For Metal GPU access and Gatekeeper, the app needs:
1. **Entitlements** for Metal compute/rendering
2. **Hardened runtime** for notarization
3. **Code signature** (ad-hoc for development, proper for distribution)

Entitlements needed:
```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "...">
<plist version="1.0">
<dict>
    <key>com.apple.security.cs.allow-unsigned-executable-memory</key>
    <true/>
    <key>com.apple.security.cs.disable-library-validation</key>
    <true/>
</dict>
</plist>
```

The `allow-unsigned-executable-memory` is needed because the engine uses JIT
compilation for Lua scripts and runtime shader compilation.

### dylib Bundling with install_name_tool

When bundling dylibs, their install names must be updated:
```bash
# Copy dylib to Frameworks/
cp /usr/local/lib/libSDL2.dylib RecoilEngine.app/Contents/Frameworks/

# Update the dylib's own install name
install_name_tool -id @executable_path/../Frameworks/libSDL2.dylib \
    RecoilEngine.app/Contents/Frameworks/libSDL2.dylib

# Update references in the main executable
install_name_tool -change /usr/local/lib/libSDL2.dylib \
    @executable_path/../Frameworks/libSDL2.dylib \
    RecoilEngine.app/Contents/MacOS/spring
```

Use `otool -L` to list all dylib dependencies of the executable.

### DMG Creation

For distribution, create a disk image:
```bash
# Create a temporary directory with the app and symlink to /Applications
mkdir -p dmg_contents
cp -R RecoilEngine.app dmg_contents/
ln -s /Applications dmg_contents/Applications

# Create DMG
hdiutil create -volname "RecoilEngine" -srcfolder dmg_contents \
    -ov -format UDZO RecoilEngine-VERSION-arm64.dmg
```

### CMake Integration

The main `CMakeLists.txt` already has:
```cmake
option(MACOSX_BUNDLE "Compile spring to work as a Bundle.app" TRUE)
if (MACOSX_BUNDLE)
    add_definitions(-DMACOSX_BUNDLE)
endif()
```

Need to add CPack configuration for macOS bundle generation.

## Tasks

### 1. Create Info.plist.in template

Create `tools/packaging/macos/Info.plist.in`:
- Update bundle identifier to `com.beyondallreason.RecoilEngine`
- Set `CFBundleExecutable` to `spring`
- Set `LSMinimumSystemVersion` to `11.0` (minimum for Apple Silicon)
- Add `NSHighResolutionCapable` for Retina support
- Add GPU requirement hints
- Remove deprecated `LSRequiresCarbon`
- Use CMake variables: `@PROJECT_VERSION@`, `@MACOSX_BUNDLE_SHORT_VERSION@`

### 2. Create entitlements.plist

Create `tools/packaging/macos/entitlements.plist`:
- `com.apple.security.cs.allow-unsigned-executable-memory` - for Lua JIT
- `com.apple.security.cs.disable-library-validation` - for plugin loading

### 3. Create bundle-dylibs.sh script

Create `tools/packaging/macos/bundle-dylibs.sh`:
- Take executable path as input
- Use `otool -L` to find all dylib dependencies
- Recursively resolve transitive dependencies
- Copy dylibs to `Contents/Frameworks/`
- Fix install names with `install_name_tool`
- Skip system libraries (those in `/System/`, `/usr/lib/`)
- Handle `@rpath` and `@loader_path` references

### 4. Create create-bundle.sh script

Create `tools/packaging/macos/create-bundle.sh`:
- Create `.app` directory structure
- Copy executable(s) to `Contents/MacOS/`
- Call `bundle-dylibs.sh` to handle libraries
- Copy icon from `installer/Mac/spring.icns` to `Contents/Resources/`
- Copy game content from `cont/` to `Contents/Resources/`
- Copy Metal shaders (.metallib) to `Contents/Resources/shaders/Metal/`
- Generate `Info.plist` from template
- Create `PkgInfo` file with `APPL????`
- Ad-hoc code sign: `codesign --force --deep -s - RecoilEngine.app`

### 5. Create create-dmg.sh script

Create `tools/packaging/macos/create-dmg.sh`:
- Create temporary staging directory
- Copy the .app bundle
- Create symlink to /Applications
- Use `hdiutil` to create compressed DMG
- Clean up temporary files
- Output naming: `RecoilEngine-{VERSION}-arm64.dmg`

### 6. Create CMake module

Create `rts/build/cmake/MacOSBundle.cmake`:
- Define `configure_macos_bundle()` function
- Configure Info.plist from template
- Set up CPack for macOS DragNDrop generator
- Add custom target `package-macos` that calls the scripts
- Handle version string extraction from git

### 7. Create packaging CMakeLists.txt

Create `tools/packaging/macos/CMakeLists.txt`:
- Include the MacOSBundle.cmake module
- Set CPack variables for macOS
- Configure bundle identifier, version, icon
- Add install rules for the bundle structure

### 8. Update main CMakeLists.txt (document changes)

Document the additions needed to the main `CMakeLists.txt`:
```cmake
if(APPLE AND MACOSX_BUNDLE)
    include(MacOSBundle)
    configure_macos_bundle()
endif()
```

### 9. Test the bundle

Verification steps:
1. Build: `cmake --build build-arm64 --target package-macos`
2. Verify structure: `ls -la build-arm64/RecoilEngine.app/Contents/`
3. Check code signing: `codesign -dv --verbose=4 RecoilEngine.app`
4. Verify dylibs: `otool -L RecoilEngine.app/Contents/MacOS/spring`
5. Test launch: `open RecoilEngine.app` (should open and render)
6. Verify Metal: Check Console.app for Metal errors

## Verification

The bundle is considered working when:
1. `open RecoilEngine.app` launches without crash
2. Metal backend initializes (check log output)
3. Main menu renders correctly
4. A simple game/demo can be loaded and renders
5. No dylib loading errors in Console.app
6. `codesign --verify RecoilEngine.app` passes
7. DMG mounts and app runs from mounted volume

## Output
- Create branch `agent/app-bundle`
- Commit each script/file separately with descriptive messages
- Include a README in `tools/packaging/macos/` explaining usage
- Document any issues encountered with specific dylibs
