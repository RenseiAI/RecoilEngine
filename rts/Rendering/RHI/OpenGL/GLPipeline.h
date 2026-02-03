/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#ifndef GL_RHI_PIPELINE_H
#define GL_RHI_PIPELINE_H

#include "Rendering/RHI/RHIPipeline.h"

namespace RHI {

/// Maps an immutable PipelineDesc to GL state calls on Bind().
class GLPipeline : public IRHIPipeline {
public:
	explicit GLPipeline(const PipelineDesc& desc);
	~GLPipeline() override = default;

	void Bind() override;

	const PipelineDesc& GetDesc() const override { return desc; }

private:
	PipelineDesc desc;
};

} // namespace RHI

#endif // GL_RHI_PIPELINE_H
