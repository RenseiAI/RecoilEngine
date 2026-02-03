/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

// RHI-GAP: RefractWater extends AdvWater with additional legacy GL features:
//
// 1. GL_TEXTURE_RECTANGLE_ARB:
//    - Not supported on Metal; Metal only has standard 2D textures
//    - Rectangle textures use pixel coordinates (0..width) instead of normalized (0..1)
//    - Migration: Use GL_TEXTURE_2D with manual coordinate scaling in shader
//    - The code already has a fallback path for GL_TEXTURE_2D when ARB not available
//
// 2. glCopyTexSubImage2D (screen capture):
//    - Copies framebuffer contents to texture for refraction effect
//    - RHI equivalent: IRHIContext::BlitFramebuffer() to copy between FBOs
//    - Or use a dedicated refraction FBO and render the scene to it first
//    - Metal requires explicit blit encoder between render passes
//
// 3. ARB Fragment Programs:
//    - Uses "ARB/waterRefractTR.fp" and "ARB/waterRefractT2D.fp"
//    - Inherits waterFP from CAdvWater and overrides with refraction shaders
//    - Must be converted to GLSL/MSL for Metal support
//    - glProgramEnvParameter4fvARB -> shader uniforms
//
// 4. Fixed-Function Texture Coordinate Generation (GL_OBJECT_LINEAR):
//    - SetupWaterDepthTex() uses GL_OBJECT_LINEAR texgen mode
//    - Different from CAdvWater which uses GL_EYE_LINEAR
//    - GL_OBJECT_LINEAR computes: texCoord = dot(objectPos, planeEq)
//    - Must be moved to vertex shader with plane equations as uniforms
//
// 5. Deprecated glEnable(target) for texture targets:
//    - glEnable(GL_TEXTURE_RECTANGLE_ARB) is deprecated FFP
//    - No-op in core profile; shaders don't need texture enable
//
// Migration priority: LOW - Inherits all AdvWater issues plus adds more.
// Recommend using BumpWater for refraction effects instead.

#include "WaterRendering.h"

#include "Rendering/GlobalRendering.h"
#include "Rendering/GL/myGL.h" // retained: ARB programs, texgen, GL_TEXTURE_RECTANGLE_ARB, glCopyTexSubImage2D (no RHI equivalent)
#include "RefractWater.h"
#include "Map/MapInfo.h"
#include "Map/ReadMap.h"

#include <bit>

#include "System/Misc/TracyDefs.h"

void CRefractWater::InitResources(bool loadShader)
{
	RECOIL_DETAILED_TRACY_ZONE;
	CAdvWater::InitResources(false);
	LoadGfx();
}

void CRefractWater::FreeResources()
{
	RECOIL_DETAILED_TRACY_ZONE;
	// RHI-GAP: glDeleteTextures -> IRHITexture destructor
	if (subSurfaceTex) {
		glDeleteTextures(1, &subSurfaceTex);
		subSurfaceTex = 0;
	}
}

void CRefractWater::LoadGfx()
{
	RECOIL_DETAILED_TRACY_ZONE;
	// RHI-GAP: GL_TEXTURE_RECTANGLE_ARB is not available on Metal.
	// The GL_TEXTURE_2D fallback path would be used, but the ARB programs
	// still need conversion to GLSL. Consider using normalized coordinates
	// with GL_TEXTURE_2D for cross-platform compatibility.
	// valid because GL_TEXTURE_RECTANGLE_ARB = GL_TEXTURE_RECTANGLE_EXT
	if (GLAD_GL_ARB_texture_rectangle) {
		target = GL_TEXTURE_RECTANGLE_ARB;
	} else {
		target = GL_TEXTURE_2D;
	}

	// RHI-GAP: Raw texture creation -> IRHIDevice::CreateTexture()
	glGenTextures(1, &subSurfaceTex);
	glBindTexture(target, subSurfaceTex);
	glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

	if (target == GL_TEXTURE_RECTANGLE_ARB) {
		glTexImage2D(target, 0, 3, globalRendering->viewSizeX, globalRendering->viewSizeY, 0, GL_RGB, GL_INT, 0);
		waterFP = LoadFragmentProgram("ARB/waterRefractTR.fp");
	} else {
		glTexImage2D(target, 0, 3, std::bit_ceil <uint32_t> (globalRendering->viewSizeX), std::bit_ceil <uint32_t> (globalRendering->viewSizeY), 0, GL_RGB, GL_INT, 0);
		waterFP = LoadFragmentProgram("ARB/waterRefractT2D.fp");
	}
}

void CRefractWater::Draw()
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!waterRendering->forceRendering && !readMap->HasVisibleWater())
		return;

	glActiveTextureARB(GL_TEXTURE2_ARB);
	glBindTexture(target, subSurfaceTex);
	// RHI-GAP: glEnable(texture_target) is deprecated FFP, no-op in core profile
	glEnable(target);
	// RHI-GAP: glCopyTexSubImage2D (screen capture) -> IRHIContext::BlitFramebuffer()
	// Metal requires explicit blit encoder. Alternative: render scene to dedicated
	// refraction FBO first, then sample that texture in water shader.
	glCopyTexSubImage2D(target, 0, 0, 0, globalRendering->viewPosX, globalRendering->viewPosY, globalRendering->viewSizeX, globalRendering->viewSizeY);

	SetupWaterDepthTex();

	glActiveTextureARB(GL_TEXTURE0_ARB);

	// GL_TEXTURE_RECTANGLE uses texcoord range 0 to width, whereas GL_TEXTURE_2D uses 0 to 1
	if (target == GL_TEXTURE_RECTANGLE_ARB) {
		float v[] = { 10.0f * globalRendering->viewSizeX, 10.0f * globalRendering->viewSizeY, 0.0f, 0.0f };
		glProgramEnvParameter4fvARB(GL_FRAGMENT_PROGRAM_ARB, 2, v);
	} else {
		float v[] = { 10.0f, 10.0f, 0.0f, 0.0f };
		glProgramEnvParameter4fvARB(GL_FRAGMENT_PROGRAM_ARB, 2, v);
		v[0] = 1.0f / std::bit_ceil <uint32_t> (globalRendering->viewSizeX);
		v[1] = 1.0f / std::bit_ceil <uint32_t> (globalRendering->viewSizeY);
		glProgramEnvParameter4fvARB(GL_FRAGMENT_PROGRAM_ARB, 3, v);
	}
	CAdvWater::Draw(false);

	glActiveTextureARB(GL_TEXTURE3_ARB);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_TEXTURE_GEN_S);
	glDisable(GL_TEXTURE_GEN_T);

	glActiveTextureARB(GL_TEXTURE2_ARB);
	glDisable(target);
	glActiveTextureARB(GL_TEXTURE0_ARB);
}

void CRefractWater::SetupWaterDepthTex()
{
	RECOIL_DETAILED_TRACY_ZONE;
	glActiveTextureARB(GL_TEXTURE3_ARB);
	// RHI-GAP: glEnable(GL_TEXTURE_2D) is deprecated FFP, no-op in core profile
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, readMap->GetShadingTexture()); // the shading texture has water depth encoded in alpha
	// RHI-GAP: GL_OBJECT_LINEAR texgen has no RHI equivalent.
	// Unlike GL_EYE_LINEAR (used in AdvWater), GL_OBJECT_LINEAR computes
	// texture coordinates in object space: texCoord = dot(objectPos, planeEq).
	// Must be moved to vertex shader with plane equations passed as uniforms:
	//   vec2 texCoord = vec2(dot(objectPos, splane), dot(objectPos, tplane));
	glEnable(GL_TEXTURE_GEN_S);
	float splane[] = { 1.0f / (mapDims.mapxp1 * SQUARE_SIZE), 0.0f, 0.0f, 0.0f };
	glTexGeni(GL_S,GL_TEXTURE_GEN_MODE,GL_OBJECT_LINEAR);
	glTexGenfv(GL_S,GL_OBJECT_PLANE,splane);

	glEnable(GL_TEXTURE_GEN_T);
	float tplane[] = { 0.0f, 0.0f, 1.0f / (mapDims.mapyp1 * SQUARE_SIZE), 0.0f};
	glTexGeni(GL_T,GL_TEXTURE_GEN_MODE,GL_OBJECT_LINEAR);
	glTexGenfv(GL_T,GL_OBJECT_PLANE,tplane);
}
