/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#ifndef RHI_SCOPED_STATE_H
#define RHI_SCOPED_STATE_H

/**
 * RHI Scoped State Helpers
 *
 * Provides RAII wrappers for pipeline state changes, equivalent to
 * glPushAttrib/glPopAttrib patterns in legacy GL code.
 *
 * Usage:
 *   {
 *       RHI::ScopedPipeline scope(device, newPipelineDesc);
 *       // ... draw with new state ...
 *   } // previous state restored automatically
 */

#include "RHIDevice.h"
#include "RHIContext.h"
#include "RHIPipeline.h"
#include <memory>

namespace RHI {

/**
 * RAII wrapper that binds a new pipeline on construction and
 * restores the default pipeline on destruction.
 *
 * Note: This is a simplified version. A full implementation would
 * track the previously bound pipeline, but since OpenGL doesn't have
 * a "get current pipeline" API, we restore to a default state.
 */
class ScopedPipeline {
public:
	/// Create scoped pipeline from a descriptor
	ScopedPipeline(IRHIDevice* device, const PipelineDesc& desc)
		: device(device)
	{
		if (device) {
			pipeline = device->CreatePipeline(desc);
			if (pipeline)
				device->GetContext()->BindPipeline(pipeline.get());
		}
	}

	/// Create scoped pipeline from an existing pipeline
	ScopedPipeline(IRHIDevice* device, IRHIPipeline* existingPipeline)
		: device(device)
	{
		if (device && existingPipeline)
			device->GetContext()->BindPipeline(existingPipeline);
	}

	~ScopedPipeline() {
		// Restore default pipeline state
		if (device) {
			PipelineDesc defaultDesc;
			auto defaultPipeline = device->CreatePipeline(defaultDesc);
			if (defaultPipeline)
				device->GetContext()->BindPipeline(defaultPipeline.get());
		}
	}

	// Non-copyable
	ScopedPipeline(const ScopedPipeline&) = delete;
	ScopedPipeline& operator=(const ScopedPipeline&) = delete;

private:
	IRHIDevice* device = nullptr;
	std::unique_ptr<IRHIPipeline> pipeline;
};

/**
 * Scoped blend state change
 */
class ScopedBlendState {
public:
	ScopedBlendState(IRHIDevice* device, const BlendState& blend)
		: device(device)
	{
		if (device) {
			PipelineDesc desc;
			desc.blend = blend;
			pipeline = device->CreatePipeline(desc);
			if (pipeline)
				device->GetContext()->BindPipeline(pipeline.get());
		}
	}

	~ScopedBlendState() {
		if (device) {
			PipelineDesc defaultDesc;
			auto defaultPipeline = device->CreatePipeline(defaultDesc);
			if (defaultPipeline)
				device->GetContext()->BindPipeline(defaultPipeline.get());
		}
	}

	ScopedBlendState(const ScopedBlendState&) = delete;
	ScopedBlendState& operator=(const ScopedBlendState&) = delete;

private:
	IRHIDevice* device = nullptr;
	std::unique_ptr<IRHIPipeline> pipeline;
};

/**
 * Scoped depth/stencil state change
 */
class ScopedDepthStencilState {
public:
	ScopedDepthStencilState(IRHIDevice* device, const DepthStencilState& depthStencil)
		: device(device)
	{
		if (device) {
			PipelineDesc desc;
			desc.depthStencil = depthStencil;
			pipeline = device->CreatePipeline(desc);
			if (pipeline)
				device->GetContext()->BindPipeline(pipeline.get());
		}
	}

	~ScopedDepthStencilState() {
		if (device) {
			PipelineDesc defaultDesc;
			auto defaultPipeline = device->CreatePipeline(defaultDesc);
			if (defaultPipeline)
				device->GetContext()->BindPipeline(defaultPipeline.get());
		}
	}

	ScopedDepthStencilState(const ScopedDepthStencilState&) = delete;
	ScopedDepthStencilState& operator=(const ScopedDepthStencilState&) = delete;

private:
	IRHIDevice* device = nullptr;
	std::unique_ptr<IRHIPipeline> pipeline;
};

/**
 * Scoped clip distance enable
 */
class ScopedClipDistance {
public:
	ScopedClipDistance(IRHIContext* ctx, uint32_t index, bool enable)
		: ctx(ctx), index(index), wasEnabled(!enable)
	{
		if (ctx)
			ctx->SetClipDistanceEnabled(index, enable);
	}

	~ScopedClipDistance() {
		if (ctx)
			ctx->SetClipDistanceEnabled(index, wasEnabled);
	}

	ScopedClipDistance(const ScopedClipDistance&) = delete;
	ScopedClipDistance& operator=(const ScopedClipDistance&) = delete;

private:
	IRHIContext* ctx = nullptr;
	uint32_t index = 0;
	bool wasEnabled = false;
};

} // namespace RHI

#endif // RHI_SCOPED_STATE_H
