/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#ifndef MTL_RHI_PIPELINE_H
#define MTL_RHI_PIPELINE_H

/**
 * Metal Pipeline Implementation
 *
 * Metal API mapping:
 *   MTLRenderPipelineDescriptor  ->  Created from PipelineDesc + shader
 *   makeRenderPipelineState      ->  Compiled pipeline (expensive, cached)
 *   MTLDepthStencilDescriptor    ->  Depth/stencil state (separate object)
 *
 * Pipeline state is immutable in Metal. The PipelineDesc contains:
 *   - Blend state (per-attachment)
 *   - Depth/stencil state (separate MTLDepthStencilState)
 *   - Rasterizer state (cull mode, front face, etc.)
 *
 * Note: The actual MTLRenderPipelineState requires shader functions,
 * which are set when the pipeline is bound with a shader. The pipeline
 * object caches compiled states by shader+desc hash.
 */

#include "Rendering/RHI/RHIPipeline.h"
#include <unordered_map>

#ifdef __OBJC__
#import <Metal/Metal.h>
#endif

namespace RHI {

class MTLDevice;
class MTLShader;

class MTLPipeline : public IRHIPipeline {
public:
	MTLPipeline(MTLDevice* device, const PipelineDesc& desc);
	~MTLPipeline() override;

	// Prevent copying
	MTLPipeline(const MTLPipeline&) = delete;
	MTLPipeline& operator=(const MTLPipeline&) = delete;

	void Bind() override;

	const PipelineDesc& GetDesc() const override { return desc; }

	/// Update the pipeline descriptor and rebuild depth-stencil state.
	/// Clears the PSO cache since blend/depth config may have changed.
	void UpdateDesc(const PipelineDesc& newDesc);

#ifdef __OBJC__
	// --- Metal-specific accessors ---

	/// Get or create a render pipeline state for the given shader.
	/// Caches compiled states by shader pointer + vertex layout hash + blend override hash.
	/// instanceStride: stride for per-instance buffer (Metal buffer index 29).
	/// Needed because SetVertexLayout is additive and the single VertexLayout::stride
	/// gets overwritten by the last call (instance attribs), losing the per-vertex stride.
	id<MTLRenderPipelineState> GetRenderPipelineState(MTLShader* shader,
	                                                   MTLPixelFormat colorFormat,
	                                                   MTLPixelFormat depthFormat,
	                                                   const VertexLayout* vertexLayout = nullptr,
	                                                   const BlendState* blendOverride = nullptr,
	                                                   uint32_t instanceStride = 0);

	/// Get the depth-stencil state
	id<MTLDepthStencilState> GetDepthStencilState() const { return depthStencilState; }

	/// Apply rasterizer state to a render command encoder
	void ApplyRasterizerState(id<MTLRenderCommandEncoder> encoder);

	// --- Format conversion helpers ---
	static MTLBlendFactor ToMTLBlendFactor(BlendFactor factor);
	static MTLBlendOperation ToMTLBlendOp(BlendOp op);
	static MTLCompareFunction ToMTLCompareFunc(CompareFunc func);
	static MTLStencilOperation ToMTLStencilOp(StencilOp op);
	static MTLCullMode ToMTLCullMode(CullMode mode);
	static MTLWinding ToMTLWinding(FrontFace face);
	static MTLTriangleFillMode ToMTLFillMode(PolygonMode mode);
	static MTLVertexFormat ToMTLVertexFormat(VertexFormat format);
	static size_t HashVertexLayout(const VertexLayout& layout);
#endif

private:
#ifdef __OBJC__
	void CreateDepthStencilState();

	id<MTLDepthStencilState> depthStencilState = nil;

	// Cache of render pipeline states by shader
	// Key is shader pointer (assumes shaders have stable addresses)
	std::unordered_map<uintptr_t, id<MTLRenderPipelineState>> pipelineCache;
#else
	void* depthStencilState = nullptr;
#endif

	MTLDevice*   device;
	PipelineDesc desc;
};

} // namespace RHI

#endif // MTL_RHI_PIPELINE_H
