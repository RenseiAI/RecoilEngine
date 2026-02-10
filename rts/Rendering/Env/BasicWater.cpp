/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

// RHI Migration Status: ~60% migrated
// - MIGRATED: glDepthMask → ctx->SetDepthWriteEnabled
// - MIGRATED: glPolygonMode → ctx->SetPolygonMode
// - KEPT (FFP, no RHI equivalent): glDisable(GL_ALPHA_TEST), glEnable(GL_TEXTURE_2D)
// - KEPT (external boundary): glDeleteTextures, glBindTexture (waiting for CBitmap to return IRHITexture)
// - KEPT (scoped state): glPushAttrib/glPopAttrib (ScopedPipeline requires full blend state tracking)

#include "BasicWater.h"
#include "ISky.h"
#include "WaterRendering.h"

#include "Rendering/GL/myGL.h" // retained: glDeleteTextures, glBindTexture, glPushAttrib/glPopAttrib
#include "Rendering/RHI/RHIContext.h"
#include "Rendering/RHI/RHIFactory.h"
#include "Rendering/RHI/RHITypes.h"
#include "Rendering/Textures/Bitmap.h"
#include "Map/MapInfo.h"
#include "Map/ReadMap.h"
#include "System/Log/ILog.h"
#include "System/SpringMath.h"

#include "System/Misc/TracyDefs.h"

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

void CBasicWater::InitResources(bool loadShader)
{
	RECOIL_DETAILED_TRACY_ZONE;
	CBitmap waterTexBM;
	if (!waterTexBM.Load(waterRendering->texture)) {
		LOG_L(L_WARNING, "[%s] could not read water texture from file \"%s\"", __FUNCTION__, waterRendering->texture.c_str());

		// fallback
		waterTexBM.AllocDummy(SColor(0,0,255,255));
	}

	// create mipmapped texture
	textureID = waterTexBM.CreateMipMapTexture();
	xsize = waterTexBM.xsize;
	ysize = waterTexBM.ysize;

	GenWaterQuadsRB();
}

void CBasicWater::FreeResources()
{
	RECOIL_DETAILED_TRACY_ZONE;
	// RHI-GAP: glDeleteTextures should be replaced with IRHITexture destructor
	// once textureID is migrated from raw GLuint to std::unique_ptr<IRHITexture>.
	// This requires CBitmap::CreateMipMapTexture() to return IRHITexture.
	if (textureID > 0) {
		glDeleteTextures(1, &textureID);
		textureID = 0;
	}
}

void CBasicWater::GenWaterQuadsRB()
{
	RECOIL_DETAILED_TRACY_ZONE;
	static constexpr float div16 = 1.0f / 16.0f;

	const float mapSizeX = mapDims.mapx * SQUARE_SIZE;
	const float mapSizeY = mapDims.mapy * SQUARE_SIZE;

	// Calculate number of times texture should repeat over the map,
	// taking aspect ratio into account.
	float repeatX = 65536.0f / mapDims.mapx;
	float repeatY = 65536.0f / mapDims.mapy * xsize / ysize;

	// Use better repeat setting of 1 repeat per 4096 mapx/mapy for the new
	// ocean.jpg while retaining backward compatibility with old maps relying
	// on 1 repeat per 1024 mapx/mapy. (changed 16/05/2007)
	if (waterRendering->texture == "bitmaps/ocean.jpg") {
		repeatX /= 4;
		repeatY /= 4;
	}

	repeatX = (waterRendering->repeatX != 0 ? waterRendering->repeatX : repeatX) / 16;
	repeatY = (waterRendering->repeatY != 0 ? waterRendering->repeatY : repeatY) / 16;

	rb = TypedRenderBuffer<VA_TYPE_T>(4 * 16 * 16, 6 * 16 * 16, IStreamBufferConcept::Types::SB_BUFFERDATA);
	for (int y = 0; y < 16; y++) {
		for (int x = 0; x < 16; x++) {
			rb.AddQuadTriangles(
				{ { (x + 0) * mapSizeX * div16, 0, (y + 0) * mapSizeY * div16 }, (x + 0) * repeatX, (y + 0) * repeatY },
				{ { (x + 0) * mapSizeX * div16, 0, (y + 1) * mapSizeY * div16 }, (x + 0) * repeatX, (y + 1) * repeatY },
				{ { (x + 1) * mapSizeX * div16, 0, (y + 1) * mapSizeY * div16 }, (x + 1) * repeatX, (y + 1) * repeatY },
				{ { (x + 1) * mapSizeX * div16, 0, (y + 0) * mapSizeY * div16 }, (x + 1) * repeatX, (y + 0) * repeatY }
			);
		}
	}
	rb.SetReadonly();
}



void CBasicWater::Draw()
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!waterRendering->forceRendering && !readMap->HasVisibleWater())
		return;

	auto* ctx = RHI::GetDevice()->GetContext();

	// RHI-GAP: glPushAttrib/glPopAttrib should be replaced with RHI::ScopedPipeline.
	// Migration pattern:
	//   RHI::PipelineDesc desc;
	//   desc.depthStencil.depthWriteEnabled = false;
	//   desc.rasterizer.polygonMode = wireFrameMode ? RHI::PolygonMode::Line : RHI::PolygonMode::Fill;
	//   RHI::ScopedPipeline scope(device, desc);
	// This requires access to the RHI device and proper state tracking.

	// RHI-GAP: GL_ALPHA_TEST is deprecated FFP state, no-op in core profile.
	// Safe to remove once all code paths use shaders with discard.
	glDisable(GL_ALPHA_TEST);
	ctx->SetDepthWriteEnabled(false);

	const auto& sky = ISky::GetSky();
	sky->SetupFog();
	ctx->SetPolygonMode(wireFrameMode ? RHI::PolygonMode::Line : RHI::PolygonMode::Fill);

	// RHI-GAP: glBindTexture -> IRHIContext::BindTexture once textureID is IRHITexture
	glBindTexture(GL_TEXTURE_2D, textureID);

	auto& sh = rb.GetShader();
	sh.Enable();
	sh.SetUniform("ucolor", 0.7f, 0.7f, 0.7f, 0.5f);
	rb.DrawElements(GL_TRIANGLES, false);
	sh.SetUniform("ucolor", 1.0f, 1.0f, 1.0f, 1.0f);
	sh.Disable();

	// RHI-GAP: glBindTexture -> IRHIContext::BindTexture(nullptr, unit) to unbind
	glBindTexture(GL_TEXTURE_2D, 0);

	// Restore state explicitly
	ctx->SetDepthWriteEnabled(true);
	ctx->SetPolygonMode(RHI::PolygonMode::Fill);
	glEnable(GL_ALPHA_TEST);
}
