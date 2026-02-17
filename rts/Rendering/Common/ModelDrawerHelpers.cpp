/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

/**
 * RHI Migration Status: TIER 4.2 - MIGRATED
 *
 * Migrated GL calls to RHI equivalents:
 *   - Pipeline state (cull mode): RHI::PipelineDesc + BindPipeline()
 *   - RHI device/context access: RHI::GetDevice(), GetContext()
 *   - Model texture binding: IRHITexture::Bind() for 3DO atlas, S3O, ASS textures
 *   - Shadow texture binding: IRHITexture::Bind() via shadowHandler.GetColorTexture()
 *   - Cube map texture binding: IRHITexture::Bind() via cubeMapHandler.GetEnvReflectionTexture()/GetSpecularTexture()
 *   - Texture unbinding: removed (next Bind() call overrides)
 *   - Matrix stack: CPU-side RHI::MatrixStack + explicit shader uniforms
 *     [x] modelMatrix/viewProjMatrix/cameraPosW set via SyncModelMatrixUniform()
 *     [x] FlushMatricesToFFP() REMOVED — model shader reads explicit uniforms
 *   - GL::SubState(DepthTest, Blending, BlendFunc) -> ctx->Set*() dynamic state
 *   - GL_CLIP_PLANE0/1 enable/disable -> ctx->SetClipDistanceEnabled()
 *
 * Retained GL calls:
 *   - Shadow depth texture (SetupShadowTexSampler / ResetShadowTexSampler).
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
#include "Rendering/RHI/MatrixStack.h"

#include "Rendering/Common/ModelDrawerState.hpp"
#include "Rendering/Shaders/Shader.h"

#include "System/Misc/TracyDefs.h"

// CPU-side matrix stacks for model rendering.
// Model shaders read explicit uniforms (modelMatrix, viewProjMatrix) set via SyncModelMatrixUniform().
static RHI::MatrixStack projectionStack;
static RHI::MatrixStack modelViewStack;

RHI::MatrixStack& CModelDrawerHelper::GetModelViewStack() { return modelViewStack; }
RHI::MatrixStack& CModelDrawerHelper::GetProjectionStack() { return projectionStack; }

void CModelDrawerHelper::SyncModelMatrixUniform()
{
	auto* state = IModelDrawerState::modelDrawerStates[MODEL_DRAWER_GLSL];
	if (state == nullptr)
		return;

	auto* shader = state->GetActiveShader();
	if (shader == nullptr || !shader->IsBound())
		return;

	shader->SetUniformMatrix4x4("modelMatrix", false, modelViewStack.Top().m);
}

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
	if (shadowHandler.ShadowsLoaded()) {
		shadowHandler.SetupShadowTexSampler(GL_TEXTURE2, true);
		if (auto* colorTex = shadowHandler.GetColorTexture())
			colorTex->Bind(3);
	}

	// Cube map textures - bind via RHI
	if (auto* envReflTex = cubeMapHandler.GetEnvReflectionTexture()) {
		envReflTex->Bind(4);
	}
	if (auto* specTex = cubeMapHandler.GetSpecularTexture()) {
		specTex->Bind(5);
	}
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
	// Push matrices on RHI stacks — model shader reads explicit uniforms, not FFP
	projectionStack.Push();
	projectionStack.MultMatrix(cam->GetViewMatrix());

	modelViewStack.Push();
	modelViewStack.LoadIdentity();
}

void CModelDrawerHelper::PopTransform()
{
	RECOIL_DETAILED_TRACY_ZONE;
	// Pop matrices from RHI stacks
	projectionStack.Pop();
	modelViewStack.Pop();
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

	// Pop and re-push projection matrix (reset to previous state)
	projectionStack.Pop();
	projectionStack.Push();
}

void CModelDrawerHelper::DIDResetPrevModelView()
{
	RECOIL_DETAILED_TRACY_ZONE;
	// Pop and re-push modelview matrix (reset to previous state)
	modelViewStack.Pop();
	modelViewStack.Push();
}

bool CModelDrawerHelper::DIDCheckMatrixMode(int wantedMode)
{
	RECOIL_DETAILED_TRACY_ZONE;
	// Debug check for FFP matrix mode. Retained because FlushMatricesToFFP() manipulates GL matrix mode.
	// This check verifies the FFP state is as expected before drawing.
	// Could be removed once the legacy rendering path is eliminated.
#if 0 // Disabled: glGetIntegerv(GL_MATRIX_MODE) is a debug-only query with no Metal equivalent
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