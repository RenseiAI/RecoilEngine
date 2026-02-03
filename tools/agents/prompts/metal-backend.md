# Agent: metal-backend

## Purpose
Implement the native Metal backend for the RHI. This is the core of the macOS ARM64 port.

## Owned Files (all NEW)
- `rts/Rendering/RHI/Metal/MTLDevice.h` / `MTLDevice.mm`
- `rts/Rendering/RHI/Metal/MTLContext.h` / `MTLContext.mm`
- `rts/Rendering/RHI/Metal/MTLBuffer.h` / `MTLBuffer.mm`
- `rts/Rendering/RHI/Metal/MTLTexture.h` / `MTLTexture.mm`
- `rts/Rendering/RHI/Metal/MTLShader.h` / `MTLShader.mm`
- `rts/Rendering/RHI/Metal/MTLFramebuffer.h` / `MTLFramebuffer.mm`
- `rts/Rendering/RHI/Metal/MTLPipeline.h` / `MTLPipeline.mm`
- `rts/Rendering/RHI/Metal/CMakeLists.txt`

## Prerequisites
- RHI interfaces must exist (`rhi-architect` agent completed)
- ShaderCompiler must exist (`shader-pipeline` agent completed)
- macOS ARM64 SDK available

## Context

The codebase is at: `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine`
Branch: `arm64-metal-port`

### Metal API Fundamentals

Metal is Apple's low-level GPU API. Key differences from OpenGL:

1. **Explicit command encoding**: Commands are recorded into `MTLCommandBuffer` via
   `MTLRenderCommandEncoder`. No implicit global state machine.

2. **Immutable pipeline state**: `MTLRenderPipelineState` objects pre-compiled from
   `MTLRenderPipelineDescriptor` (shader pair + vertex layout + pixel format + blend state).
   Must be cached by hash.

3. **No bind points**: Resources (buffers, textures) are set directly on encoder slots
   via `setVertexBuffer:offset:atIndex:` and `setFragmentTexture:atIndex:`.

4. **Explicit render passes**: `MTLRenderPassDescriptor` specifies load/store actions
   for each attachment. No implicit framebuffer state.

5. **Shared memory**: On Apple Silicon, `storageModeShared` gives both CPU and GPU
   access to buffer memory. No need for explicit mapping/unmapping.

6. **Triple buffering**: Must be managed explicitly with semaphores.

### SDL2 Metal Integration

SDL2 supports Metal on macOS:
```objc
SDL_Window* window = SDL_CreateWindow("...", ..., SDL_WINDOW_METAL);
SDL_MetalView view = SDL_Metal_CreateView(window);
CAMetalLayer* layer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(view);
id<MTLDevice> device = MTLCreateSystemDefaultDevice();
layer.device = device;
```

### Implementation Details

**MTLDevice:**
- Creates `MTLDevice` via `MTLCreateSystemDefaultDevice()`
- Creates `MTLCommandQueue`
- Queries device capabilities (feature sets, max texture size, etc.)
- Manages `CAMetalLayer` for window presentation

**MTLContext:**
- Creates `MTLCommandBuffer` per frame
- Manages `MTLRenderCommandEncoder` lifecycle (begin/end render pass)
- Implements draw calls: `drawPrimitives:`, `drawIndexedPrimitives:`
- Handles frame presentation via `commandBuffer.presentDrawable(drawable)`
- Triple-buffer synchronization with `DispatchSemaphore`

**MTLBuffer:**
- Creates `MTLBuffer` with `device.makeBuffer(length:options:)`
- Use `storageModeShared` for all buffers on Apple Silicon
- `Map()` returns `buffer.contents()` directly (always mapped on shared mode)
- `Upload()` uses `memcpy` to buffer contents

**MTLTexture:**
- Creates `MTLTexture` via `MTLTextureDescriptor` + `device.makeTexture()`
- Format mapping: RHITextureFormat -> MTLPixelFormat
- Sampler state: create `MTLSamplerState` from `MTLSamplerDescriptor`
- Set on encoder: `encoder.setFragmentTexture(texture, index: slot)`

**MTLShader:**
- Loads functions from `MTLLibrary` (pre-compiled .metallib or runtime compilation)
- `library.makeFunction(name: "vertex_main")` / `makeFunction(name: "fragment_main")`
- Uniform data via `setVertexBytes:length:atIndex:` for small data
- Uniform buffers via `setVertexBuffer:offset:atIndex:` for larger data

**MTLFramebuffer:**
- Represents a `MTLRenderPassDescriptor`
- Color attachments: `descriptor.colorAttachments[0].texture = texture`
- Depth attachment: `descriptor.depthAttachment.texture = depthTexture`
- Load/store actions control whether to clear or preserve attachment contents

**MTLPipeline:**
- Creates `MTLRenderPipelineDescriptor` from RHI pipeline state description
- Compiles to `MTLRenderPipelineState` via `device.makeRenderPipelineState()`
- Cache by descriptor hash (pipeline compilation is expensive)
- Separate `MTLDepthStencilState` for depth/stencil configuration

### metal-cpp Option

Apple's `metal-cpp` library provides C++ wrappers around Metal Objective-C APIs:
```cpp
MTL::Device* device = MTL::CreateSystemDefaultDevice();
MTL::CommandQueue* queue = device->newCommandQueue();
MTL::CommandBuffer* cmdBuf = queue->commandBuffer();
```
This reduces Objective-C++ boilerplate. Header-only, Apache 2.0 license.
Decide whether to use it based on the complexity of the implementation.

### Framework Linking
```cmake
if(APPLE)
    find_library(METAL_FRAMEWORK Metal REQUIRED)
    find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
    find_library(METALKIT_FRAMEWORK MetalKit)
    target_link_libraries(metal-backend ${METAL_FRAMEWORK} ${QUARTZCORE_FRAMEWORK})
endif()
```

## Tasks

1. Read RHI interfaces thoroughly
2. Implement each Metal backend class
3. Start with MTLDevice (device creation + window integration)
4. Then MTLBuffer and MTLTexture (resource creation)
5. Then MTLShader (shader loading from metallib)
6. Then MTLPipeline (pipeline state compilation + caching)
7. Then MTLFramebuffer (render pass management)
8. Finally MTLContext (command encoding + draw calls + frame presentation)
9. Create CMakeLists.txt for Metal backend
10. Test: render a colored triangle via Metal

## Output
- Create branch `agent/metal-backend`
- Commit each backend class separately
- Include a simple test or demo that renders geometry via Metal
