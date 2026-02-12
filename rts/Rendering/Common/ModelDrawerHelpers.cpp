/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

/**
 * RHI Migration Status: PARTIAL
 *
 * This file is partially migrated to the RHI abstraction layer.
 *
 * Migrated patterns:
 *   - Pipeline state (cull mode) -> RHI::PipelineDesc + ctx->BindPipeline()
 *   - RHI device/context access  -> RHI::CreateDevice(), GetContext()
 *   - Model texture binding -> IRHITexture::Bind() for 3DO atlas and S3O textures
 *   - Shadow texture binding -> IRHITexture::Bind() via shadowHandler.GetColorTexture()
 *
 * Remaining GL calls (with RHI_TODO comments):
 *   - glActiveTexture/glBindTexture(0): Texture unbinding not supported by IRHITexture
 *   - glActiveTexture/glBindTexture(cube maps): cubeMapHandler doesn't expose RHI textures
 *   - glMatrixMode/glPushMatrix/glPopMatrix: Legacy FFP matrix stack, no RHI equivalent
 *   - glGetIntegerv(GL_MATRIX_MODE): Legacy FFP state query, no RHI equivalent
 *
 * Dependencies blocking full migration:
 *   - cubeMapHandler needs to expose IRHITexture* getters (currently only GetNativeHandle())
 *   - IRHITexture needs Unbind() method or context->UnbindTexture(unit)
 *   - FFP matrix stack used by legacy path; GL4 path uses uniform buffers
 */

#include "ModelDrawerHelpers.h"
#include "ModelDrawer.h"
#include "System/float3.h"
#include "Map/Ground.h"
#include "Game/Camera.h"
#include "Sim/Misc/TeamHandler.h"
#include "Sim/Misc/Team.h"
#include "Sim/Objects/SolidObject.h"
#include "Rendering/Models/3DModel.hpp"
#include "Rendering/ShadowHandler.h"
#include "Rendering/Textures/3DOTextureHandler.h"
#include "Rendering/Env/CubeMapHandler.h"
#include "Rendering/GL/myGL.h"
#include "Rendering/RHI/RHIDevice.h"
#include "Rendering/RHI/RHIContext.h"
#include "Rendering/RHI/RHIPipeline.h"
#include "Rendering/RHI/RHIFactory.h"

#include "System/Misc/TracyDefs.h"

bool CModelDrawerHelper::ObjectVisibleReflection(const float3& objPos, const float3& camPos, float maxRadius)
{
	RECOIL_DETAILED_TRACY_ZONE;
#if 1
	// If the object is underwater then,
	// draw the object if the water depth at the object is less than the units draw radius
	if (objPos.y < 0.0f)
		return (-1.0 * CGround::GetApproximateHeight(objPos.x, objPos.z, false) <= maxRadius);

	const float dif = objPos.y - camPos.y;
	// Otherwise draw a line between the objects position and the underwater camera, intersecting the waterplane
	float3 zeroPos;
	zeroPos += (camPos * ( objPos.y / dif));
	zeroPos += (objPos * (-camPos.y / dif));
	// If the height of the ground at zeropos is less than the maxradius, 
	// we are likely to get a reflection (e.g. high cliffs will prevent reflections
	return (CGround::GetApproximateHeight(zeroPos.x, zeroPos.z, false) <= maxRadius);
#else
	// This method does not cull reflections hidden by cliffs,
	// and prevents units over water depth > 2 * radius  to have any reflection at all
	const float gh = CGround::GetApproximateHeight(objPos.x, objPos.z, false);
	return gh + 2.0f * maxRadius > 0.0f;
#endif
}

void CModelDrawerHelper::EnableTexturesCommon()
{
	RECOIL_DETAILED_TRACY_ZONE;
	// RHI_TODO: cubeMapHandler doesn't yet expose RHI texture objects (only GetNativeHandle),
	// so cube map binding still uses raw GL calls.

	if (shadowHandler.ShadowsLoaded()) {
		shadowHandler.SetupShadowTexSampler(GL_TEXTURE2, true);
		// Shadow color texture - bind via RHI if available, else GL fallback
		if (auto* colorTex = shadowHandler.GetColorTexture()) {
			colorTex->Bind(3);
		} else {
			glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, shadowHandler.GetColorTextureID());
		}
	}

	// Cube map textures - use GL until cubeMapHandler exposes RHI texture objects
	glActiveTexture(GL_TEXTURE4);
	glBindTexture(GL_TEXTURE_CUBE_MAP, cubeMapHandler.GetEnvReflectionTextureID());

	glActiveTexture(GL_TEXTURE5);
	glBindTexture(GL_TEXTURE_CUBE_MAP, cubeMapHandler.GetSpecularTextureID());
}

void CModelDrawerHelper::DisableTexturesCommon()
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (shadowHandler.ShadowsLoaded())
		shadowHandler.ResetShadowTexSampler(GL_TEXTURE2, true);
}

void CModelDrawerHelper::PushTransform(const CCamera* cam)
{
	RECOIL_DETAILED_TRACY_ZONE;
	// RHI_TODO: FFP matrix stack (glMatrixMode/glPushMatrix/glPopMatrix) has no
	// RHI equivalent. GL4 path uses uniform buffers for transforms. This legacy
	// path should be removed once the GL4 path handles all model rendering.
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glMultMatrixf(cam->GetViewMatrix());
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
}

void CModelDrawerHelper::PopTransform()
{
	RECOIL_DETAILED_TRACY_ZONE;
	// RHI_TODO: FFP matrix stack - see PushTransform note
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
}

float4 CModelDrawerHelper::GetTeamColor(int team, float alpha)
{
	RECOIL_DETAILED_TRACY_ZONE;
	assert(teamHandler.IsValidTeam(team));

	const   CTeam* t = teamHandler.Team(team);
	const uint8_t* c = t->color;

	return float4(c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f, alpha);
}

void CModelDrawerHelper::DIDResetPrevProjection(bool toScreen)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!toScreen)
		return;

	// RHI_TODO: FFP matrix stack - see PushTransform note
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glPushMatrix();
}

void CModelDrawerHelper::DIDResetPrevModelView()
{
	RECOIL_DETAILED_TRACY_ZONE;
	// RHI_TODO: FFP matrix stack - see PushTransform note
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
	glPushMatrix();
}

bool CModelDrawerHelper::DIDCheckMatrixMode(int wantedMode)
{
	RECOIL_DETAILED_TRACY_ZONE;
	// RHI_TODO: glGetIntegerv(GL_MATRIX_MODE) is FFP state query with no RHI equivalent.
	// This debug check should be removed once FFP matrix stack is eliminated.
#if 1
	int matrixMode = 0;
	glGetIntegerv(GL_MATRIX_MODE, &matrixMode);
	return (matrixMode == wantedMode);
#else
	return true;
#endif
}


void CModelDrawerHelper::BindModelTypeTexture(int mdlType, int texType)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const auto texMat = textureHandlerS3O.GetTexture(texType);

	if (shadowHandler.InShadowPass())
		modelDrawerHelpers[mdlType]->BindShadowTex(texMat);
	else
		modelDrawerHelpers[mdlType]->BindOpaqueTex(texMat);
}

void CModelDrawerHelper::UnbindModelTypeTexture(int mdlType)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (shadowHandler.InShadowPass())
		modelDrawerHelpers[mdlType]->UnbindShadowTex();
	else
		modelDrawerHelpers[mdlType]->UnbindOpaqueTex();
}

void CModelDrawerHelper::PushModelRenderState(int mdlType)
{
	RECOIL_DETAILED_TRACY_ZONE;
	assert(CModelDrawerHelper::modelDrawerHelpers[mdlType]);
	modelDrawerHelpers[mdlType]->PushRenderState();
}

void CModelDrawerHelper::PushModelRenderState(const S3DModel* m)
{
	RECOIL_DETAILED_TRACY_ZONE;
	PushModelRenderState(m->type);
	BindModelTypeTexture(m->type, m->textureType);
}

void CModelDrawerHelper::PushModelRenderState(const CSolidObject* o) { PushModelRenderState(o->model); }

void CModelDrawerHelper::PopModelRenderState(int mdlType)
{
	RECOIL_DETAILED_TRACY_ZONE;
	assert(modelDrawerHelpers[mdlType]);
	modelDrawerHelpers[mdlType]->PopRenderState();
}

void CModelDrawerHelper::PopModelRenderState(const S3DModel* m) { PopModelRenderState(m->type); }
void CModelDrawerHelper::PopModelRenderState(const CSolidObject* o) { PopModelRenderState(o->model); }

///////////////////////////////////////////////////////////////////////////

const std::array<const CModelDrawerHelper*, MODELTYPE_CNT> CModelDrawerHelper::modelDrawerHelpers = {
	CModelDrawerHelper::GetInstance<CModelDrawerHelper3DO>(),
	CModelDrawerHelper::GetInstance<CModelDrawerHelperS3O>(),
	CModelDrawerHelper::GetInstance<CModelDrawerHelperASS>(),
};

///////////////////////////////////////////////////////////////////////////

void CModelDrawerHelper3DO::PushRenderState() const
{
	RECOIL_DETAILED_TRACY_ZONE;
	auto* device = RHI::GetDevice();
	auto* ctx = device->GetContext();
	RHI::PipelineDesc desc;
	desc.rasterizer.cullMode = RHI::CullMode::None;
	auto pipeline = device->CreatePipeline(desc);
	ctx->BindPipeline(pipeline.get());
}

void CModelDrawerHelper3DO::PopRenderState() const
{
	RECOIL_DETAILED_TRACY_ZONE;
	auto* device = RHI::GetDevice();
	auto* ctx = device->GetContext();
	RHI::PipelineDesc desc;
	desc.rasterizer.cullMode = RHI::CullMode::Back;
	auto pipeline = device->CreatePipeline(desc);
	ctx->BindPipeline(pipeline.get());
}

void CModelDrawerHelper3DO::BindOpaqueTex(const CS3OTextureHandler::S3OTexMat* textureMat) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	// Use RHI texture binding for 3DO atlas textures
	if (auto* tex2 = textureHandler3DO.GetAtlasTex2()) {
		tex2->Bind(1);
	}
	if (auto* tex1 = textureHandler3DO.GetAtlasTex1()) {
		tex1->Bind(0);
	}
}

void CModelDrawerHelper3DO::UnbindOpaqueTex() const
{
	RECOIL_DETAILED_TRACY_ZONE;
	// Texture unbinding unnecessary — next Bind() call overrides
}

void CModelDrawerHelper3DO::BindShadowTex(const CS3OTextureHandler::S3OTexMat* textureMat) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	// RHI Bind(unit) handles glActiveTexture internally
	if (auto* tex2 = textureHandler3DO.GetAtlasTex2()) {
		tex2->Bind(0);
	}
}

void CModelDrawerHelper3DO::UnbindShadowTex() const
{
	RECOIL_DETAILED_TRACY_ZONE;
	// Texture unbinding unnecessary — next Bind() call overrides
}

///////////////////////////////////////////////////////////////////////////

void CModelDrawerHelperS3O::BindOpaqueTex(const CS3OTextureHandler::S3OTexMat* textureMat) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	// Use RHI texture binding for S3O textures - S3OTexMat has RHI texture ownership
	if (textureMat->tex2RHI) {
		textureMat->tex2RHI->Bind(1);
	}
	if (textureMat->tex1RHI) {
		textureMat->tex1RHI->Bind(0);
	}
}

void CModelDrawerHelperS3O::UnbindOpaqueTex() const
{
	RECOIL_DETAILED_TRACY_ZONE;
	// Texture unbinding unnecessary — next Bind() call overrides
}

void CModelDrawerHelperS3O::BindShadowTex(const CS3OTextureHandler::S3OTexMat* textureMat) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	// RHI Bind(unit) handles glActiveTexture internally
	if (textureMat->tex2RHI) {
		textureMat->tex2RHI->Bind(0);
	}
}

void CModelDrawerHelperS3O::UnbindShadowTex() const
{
	RECOIL_DETAILED_TRACY_ZONE;
	// Texture unbinding unnecessary — next Bind() call overrides
}

///////////////////////////////////////////////////////////////////////////

void CModelDrawerHelperASS::BindOpaqueTex(const CS3OTextureHandler::S3OTexMat* textureMat) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	// Use RHI texture binding for ASS (Assimp) textures - S3OTexMat has RHI texture ownership
	if (textureMat->tex2RHI) {
		textureMat->tex2RHI->Bind(1);
	}
	if (textureMat->tex1RHI) {
		textureMat->tex1RHI->Bind(0);
	}
}

void CModelDrawerHelperASS::UnbindOpaqueTex() const
{
	RECOIL_DETAILED_TRACY_ZONE;
	// Texture unbinding unnecessary — next Bind() call overrides
}

void CModelDrawerHelperASS::BindShadowTex(const CS3OTextureHandler::S3OTexMat* textureMat) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	// RHI Bind(unit) handles glActiveTexture internally
	if (textureMat->tex2RHI) {
		textureMat->tex2RHI->Bind(0);
	}
}

void CModelDrawerHelperASS::UnbindShadowTex() const
{
	RECOIL_DETAILED_TRACY_ZONE;
	// Texture unbinding unnecessary — next Bind() call overrides
}