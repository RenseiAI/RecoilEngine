/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#ifndef RHI_PIPELINE_H
#define RHI_PIPELINE_H

/**
 * RHI Pipeline Interface
 *
 * Maps GL mutable state to Metal-style immutable pipeline state objects:
 *   glEnable(GL_DEPTH_TEST) + glDepthFunc()    -> part of PipelineDesc
 *   glEnable(GL_BLEND) + glBlendFunc()         -> part of PipelineDesc
 *   glEnable(GL_CULL_FACE) + glCullFace()      -> part of PipelineDesc
 *   glPolygonMode()                            -> part of PipelineDesc
 *   GL::SubState scoped changes                -> bind different pipeline
 *
 * Metal compiles MTLRenderPipelineState from an immutable descriptor.
 * The OpenGL backend applies the descriptor's state via glEnable/glDisable
 * calls when Bind() is called, effectively emulating the immutable model.
 *
 * For OpenGL, the backend caches compiled pipeline descriptors by hash
 * to avoid redundant state changes.
 */

#include "RHITypes.h"

namespace RHI {

class IRHIPipeline {
public:
	virtual ~IRHIPipeline() = default;

	/// Apply this pipeline's state. On OpenGL, issues glEnable/glDisable/glBlendFunc etc.
	/// On Metal, binds the pre-compiled MTLRenderPipelineState.
	virtual void Bind() = 0;

	/// Get the descriptor used to create this pipeline
	virtual const PipelineDesc& GetDesc() const = 0;
};

} // namespace RHI

#endif // RHI_PIPELINE_H
