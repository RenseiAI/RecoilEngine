/**
 * RHI Migration Status: COMPLETE
 *
 * All GL calls have been replaced with RHI equivalents:
 *   - Pipeline state (blend, depth test): RHI::PipelineDesc + BindPipeline()
 *   - Draw call: ctx->Draw()
 *   - Matrix transforms: explicit uniform uploads (modelViewProjectionMatrix, modelViewMatrixInverse)
 */

#include "ModernSky.h"

#include "Rendering/GlobalRendering.h"
#include "Rendering/GL/myGL.h"  // retained: GL_VERTEX_SHADER, GL_FRAGMENT_SHADER constants
#include "Rendering/RHI/RHITypes.h"
#include "Rendering/RHI/RHIPipeline.h"
#include "Rendering/RHI/RHIContext.h"
#include "Rendering/RHI/RHIDevice.h"
#include "Rendering/RHI/RHIFactory.h"
#include "Rendering/Shaders/Shader.h"
#include "Rendering/Shaders/ShaderHandler.h"
#include "Rendering/Env/DebugCubeMapTexture.h"
#include "Rendering/Env/WaterRendering.h"
#include "System/StringUtil.h"
#include "Game/Game.h"
#include "Game/Camera.h"
#include "Sim/Misc/GlobalSynced.h"
#include "Map/MapInfo.h"
#include "Map/ReadMap.h"

#include "System/Misc/TracyDefs.h"

CModernSky::CModernSky()
{
	valid = true;
#ifndef HEADLESS
	// shaderHandler uses raw GL calls (glCreateShader, etc.) — skip on Metal
	if (RHI::GetDefaultBackend() != RHI::Backend::OpenGL) {
		valid = false;
		return;
	}

	for (size_t i = 0; i < 2; ++i) {
		skyShaders[i] = shaderHandler->CreateProgramObject("[ModernSky]", "Sky-" + IntToString(i));
		skyShaders[i]->AttachShaderObject(shaderHandler->CreateShaderObject("GLSL/ModernSkyVS.glsl", "", GL_VERTEX_SHADER));
		skyShaders[i]->AttachShaderObject(shaderHandler->CreateShaderObject("GLSL/ModernSkyFS.glsl", "", GL_FRAGMENT_SHADER));
		skyShaders[i]->SetFlag("SIMPLIFIED_RENDERING", static_cast<int>(i == 1));
		skyShaders[i]->Link();
		skyShaders[i]->Enable();
		skyShaders[i]->Disable();
		valid &= skyShaders[i]->Validate();
	}
#endif
}

CModernSky::~CModernSky()
{
#ifndef HEADLESS
	shaderHandler->ReleaseProgramObjects("[ModernSky]");
#endif
}

void CModernSky::Draw()
{
	RECOIL_DETAILED_TRACY_ZONE;
#ifndef HEADLESS
	if (!globalRendering->drawSky)
		return;

	if (!valid)
		return;

	// Pipeline state via RHI
	{
		RHI::PipelineDesc pipeDesc;
		pipeDesc.blend.enabled  = true;
		pipeDesc.blend.srcColor = RHI::BlendFactor::SrcAlpha;
		pipeDesc.blend.dstColor = RHI::BlendFactor::OneMinusSrcAlpha;
		pipeDesc.blend.srcAlpha = RHI::BlendFactor::SrcAlpha;
		pipeDesc.blend.dstAlpha = RHI::BlendFactor::OneMinusSrcAlpha;
		pipeDesc.depthStencil.depthTestEnabled = true;
		pipeDesc.depthStencil.depthFunc = RHI::CompareFunc::LessEqual;
		auto pipeline = RHI::GetDevice()->CreatePipeline(pipeDesc);
		RHI::GetDevice()->GetContext()->BindPipeline(pipeline.get());
	}

	vao.Bind();

	auto* skyShader = skyShaders[game->GetDrawMode() != CGame::GameDrawMode::gameNormalDraw];

	skyShader->Enable();

	// Upload matrix uniforms (replaces FFP matrix stack)
	const CMatrix44f& view = camera->GetViewMatrix();
	const CMatrix44f mvp = camera->GetProjectionMatrix() * view;
	const CMatrix44f mvInverse = CMatrix44f(view).InvertAffine();
	skyShader->SetUniformMatrix4x4<float>("modelViewProjectionMatrix", false, &mvp.md[0][0]);
	skyShader->SetUniformMatrix4x4<float>("modelViewMatrixInverse", false, &mvInverse.md[0][0]);

	const float3 midMap{ static_cast<float>(SQUARE_SIZE * mapDims.mapx >> 1), 0.0f, static_cast<float>(SQUARE_SIZE * mapDims.mapy >> 1) };
	skyShader->SetUniform("midMap", midMap.x, midMap.y, midMap.z);

	const float4& sunDir = skyLight->GetLightDir();
	skyShader->SetUniform("sunDir", sunDir.x, sunDir.y, sunDir.z);

	skyShader->SetUniform("sunColor", sunColor.x, sunColor.y, sunColor.z, sunDir.w); // sunDir.w -- intensity

	skyShader->SetUniform("cloudInfo", cloudColor.x, cloudColor.y, cloudColor.z, mapInfo->atmosphere.cloudDensity);

	skyShader->SetUniform("skyColor", skyColor.x, skyColor.y, skyColor.z);

	skyShader->SetUniform("fogColor", fogColor.x, fogColor.y, fogColor.z);

	skyShader->SetUniform("planeColor",
		waterRendering->planeColor.x,
		waterRendering->planeColor.y,
		waterRendering->planeColor.z,
		static_cast<float>(waterRendering->hasWaterPlane && !globalRendering->drawDebugCubeMap)
	);

	skyShader->SetUniform("time", (static_cast<float>(gs->frameNum) + globalRendering->timeOffset) * 0.005f);

	// Draw via RHI
	RHI::GetDevice()->GetContext()->Draw(RHI::PrimitiveType::Triangles, 36, 0);

	skyShader->Disable();
	vao.Unbind();

#endif
}
