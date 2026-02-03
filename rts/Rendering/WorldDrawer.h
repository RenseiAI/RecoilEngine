/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */
// RHI Migration: CWorldDrawer is the top-level rendering coordinator with ~26 GL calls.
// Key mappings:
//   - glClearColor/glClear -> IRHIContext::ClearColor()/ClearDepth()/ClearStencil()
//   - glEnable/glDisable(GL_DEPTH_TEST/GL_BLEND/GL_FOG) -> RHI::PipelineDesc state
//   - glDepthMask/glDepthFunc/glBlendFunc -> RHI::DepthStencilState/BlendState
//   - glMatrixMode/glLoadIdentity/gluOrtho2D -> CMatrix44f projection (already available)
//   - glClipPlane/GL_CLIP_PLANE* -> shader-based clipping or RHI clip distance extension
//   - glEnableClientState/glVertexPointer/glDrawArrays -> IRHIBuffer + IRHIContext::Draw()
//   - glColor4f/glDisable(GL_TEXTURE_2D) -> per-vertex color in IRHIBuffer or push constants

#ifndef _WORLD_DRAWER_H
#define _WORLD_DRAWER_H

class CWorldDrawer
{
public:
	void InitPre() const;
	void InitPost() const;
	void Kill();

	void Update(bool newSimFrame);
	void Draw() const;

	void GenerateIBLTextures() const;
	void ResetMVPMatrices() const;

private:
	void DrawOpaqueObjects() const;
	void DrawAlphaObjects() const;
	void DrawMiscObjects() const;
	void DrawBelowWaterOverlay() const;

private:
	unsigned int numUpdates = 0;
};

#endif // _WORLD_DRAWER_H
