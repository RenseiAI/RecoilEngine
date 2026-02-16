/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

/**
 * RHI Migration Status (LineDrawer)
 *
 * MIGRATED:
 * - Dynamic state: glDisable(GL_DEPTH_TEST) -> ctx->SetDepthTestEnabled(false) (with explicit state restore)
 * - FFP client-state vertex arrays: Converted to TypedRenderBuffer<VA_TYPE_C>
 *
 * REMAINING (NOT MIGRATED):
 * - Line stipple: glLineStipple() removed (no RHI equivalent, deprecated in GL3+, renders as solid lines)
 */

// TODO: move this out of Sim, this is rendering code!

#include "LineDrawer.h"

#include <cmath>

#include "Rendering/GlobalRendering.h"
#include "Rendering/RHI/RHITypes.h"
#include "Rendering/RHI/RHIFactory.h"
#include "Rendering/RHI/RHIContext.h"
#include "Rendering/GL/RenderBuffers.h"
#include "Game/UI/CommandColors.h"
#include "System/Matrix44f.h"

CLineDrawer lineDrawer;


CLineDrawer::CLineDrawer()
	: lineStipple(false)
	, useColorRestarts(false)
	, useRestartColor(false)
	, restartAlpha(0.0f)
	, restartColor(NULL)
	, lastPos(ZeroVector)
	, lastColor(NULL)
	, stippleTimer(0.0f)
{
	lines.reserve(32);
	stippled.reserve(32);
}


void CLineDrawer::UpdateLineStipple()
{
	stippleTimer += (globalRendering->lastFrameTime * 0.001f * cmdColors.StippleSpeed());
	stippleTimer = std::fmod(stippleTimer, (16.0f / 20.0f));
}


void CLineDrawer::SetupLineStipple()
{
	const unsigned int stipPat = (0xffff & cmdColors.StipplePattern());
	if ((stipPat != 0x0000) && (stipPat != 0xffff)) {
		lineStipple = true;
	} else {
		lineStipple = false;
		return;
	}
	// Note: glLineStipple removed (no RHI equivalent, deprecated in GL3+)
	// Stippled lines now render as solid lines
}


void CLineDrawer::DrawAll(const CMatrix44f* transform)
{
	if (lines.empty() && stippled.empty())
		return;

	auto* ctx = RHI::GetDevice()->GetContext();

	auto& rb = RenderBuffer::GetTypedRenderBuffer<VA_TYPE_C>();
	auto& sh = rb.GetShader();

	auto drawLinePairs = [&](const std::vector<LinePair>& pairs) {
		for (const auto& lp : pairs) {
			const int vertCount = lp.colors.size() / 4;
			if (vertCount <= 0) continue;

			for (int v = 0; v < vertCount; ++v) {
				const SColor color(
					lp.colors[v * 4 + 0],
					lp.colors[v * 4 + 1],
					lp.colors[v * 4 + 2],
					lp.colors[v * 4 + 3]
				);
				rb.AddVertex({
					{ lp.verts[v * 3 + 0], lp.verts[v * 3 + 1], lp.verts[v * 3 + 2] },
					color
				});
			}
			if (transform) rb.SetTransformMatrix(*transform);
			rb.DrawArrays(lp.type);  // GL_LINES or GL_LINE_STRIP
		}
	};

	ctx->SetDepthTestEnabled(false);

	sh.Enable();
	drawLinePairs(lines);
	drawLinePairs(stippled);
	sh.Disable();

	// Restore state
	ctx->SetDepthTestEnabled(true);

	lines.clear();
	stippled.clear();
}
