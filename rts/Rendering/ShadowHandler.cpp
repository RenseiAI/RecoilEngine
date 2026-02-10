/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

/**
 * RHI Migration Status: PARTIAL
 *
 * This file is partially migrated to the RHI abstraction layer.
 *
 * Migrated patterns:
 *   - FBO creation/management    -> IRHIDevice::CreateFramebuffer()
 *   - Depth texture creation     -> IRHIDevice::CreateTexture(Depth format)
 *   - Texture parameters         -> IRHITexture::Set{Min,Mag}Filter(), SetWrap{S,T}()
 *   - Depth texture compare mode -> IRHITexture::SetCompareMode()
 *   - RHI device/context access  -> RHI::CreateDevice(), GetContext()
 *   - Color write mask           -> IRHIContext::SetColorMask()
 *
 * Remaining GL calls (with RHI_TODO comments):
 *   - GL_TEXTURE_SWIZZLE_*: Texture swizzle mask not in RHI interface
 *   - GL_DEPTH_TEXTURE_MODE: Legacy FFP, no RHI equivalent
 *   - Line width: RasterizerState::lineWidth not yet implemented (debug frustum)
 *   - glEnable/glDisable(GL_TEXTURE_2D): Legacy FFP texture enable, no RHI equivalent
 *
 * Dependencies blocking full migration:
 *   - RHI needs texture swizzle support in IRHITexture
 */

#include <cfloat>

#include "ShadowHandler.h"
#include "Game/Camera.h"
#include "Game/CameraHandler.h"
#include "Game/GameVersion.h"
#include "Map/BaseGroundDrawer.h"
#include "Map/Ground.h"
#include "Map/MapInfo.h"
#include "Map/ReadMap.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/Features/FeatureDrawer.h"
#include "Rendering/Env/Particles/ProjectileDrawer.h"
#include "Rendering/Units/UnitDrawer.h"
#include "Rendering/Env/GrassDrawer.h"
#include "Rendering/Env/ISky.h"
#include "Rendering/GL/myGL.h"
#include "Rendering/Shaders/ShaderHandler.h"
#include "Rendering/Shaders/Shader.h"
#include "Rendering/GL/RenderBuffers.h"
#include "Rendering/RHI/RHIDevice.h"
#include "Rendering/RHI/RHIContext.h"
#include "Rendering/RHI/RHIPipeline.h"
#include "Rendering/RHI/RHIFactory.h"
#include "System/Config/ConfigHandler.h"
#include "System/EventHandler.h"
#include "System/Matrix44f.h"
#include "System/SpringMath.h"
#include "System/StringUtil.h"
#include "System/Log/ILog.h"

#include "lib/fmt/format.h"

namespace {
	RHI::TextureFormat DepthBitsToRHIFormat(int bits) {
		switch (bits) {
			case 16: return RHI::TextureFormat::Depth16;
			case 24: return RHI::TextureFormat::Depth24;
			case 32: return RHI::TextureFormat::Depth32F;
			default: return RHI::TextureFormat::Depth24;
		}
	}
}

CONFIG(int, Shadows).defaultValue(2).headlessValue(-1).minimumValue(-1).safemodeValue(-1).description("Sets whether shadows are rendered.\n-1:=forceoff, 0:=off, 1:=full, 2:=fast (skip terrain)"); //FIXME document bitmask
CONFIG(int, ShadowMapSize).defaultValue(CShadowHandler::DEF_SHADOWMAP_SIZE).minimumValue(32).description("Sets the resolution of shadows. Higher numbers increase quality at the cost of performance.");
CONFIG(int, ShadowProjectionMode).defaultValue(CShadowHandler::SHADOWPROMODE_CAM_CENTER);
CONFIG(bool, ShadowColorMode).defaultValue(true).description("Whether the colorbuffer of shadowmap FBO is RGB vs greyscale(to conserve some VRAM)");

CShadowHandler shadowHandler;

void CShadowHandler::Reload(const char* argv)
{
	int nextShadowConfig = (shadowConfig + 1) & 0xF;
	int nextShadowMapSize = shadowMapSize;
	int nextShadowProMode = shadowProMode;
	int nextShadowColorMode = shadowColorMode;

	if (argv != nullptr)
		(void) sscanf(argv, "%i %i %i %i", &nextShadowConfig, &nextShadowMapSize, &nextShadowProMode, &nextShadowColorMode);

	// do nothing without a parameter change
	if (nextShadowConfig == shadowConfig && nextShadowMapSize == shadowMapSize && nextShadowProMode == shadowProMode && nextShadowColorMode == shadowColorMode)
		return;

	configHandler->Set("Shadows", nextShadowConfig & 0xF);
	configHandler->Set("ShadowMapSize", std::clamp(nextShadowMapSize, int(MIN_SHADOWMAP_SIZE), int(MAX_SHADOWMAP_SIZE)));
	configHandler->Set("ShadowProjectionMode", std::clamp(nextShadowProMode, int(SHADOWPROMODE_MAP_CENTER), int(SHADOWPROMODE_MIX_CAMMAP)));
	configHandler->Set("ShadowColorMode", static_cast<bool>(nextShadowColorMode));

	Kill();
	Init();
}

void CShadowHandler::Init()
{
	const bool tmpFirstInit = firstInit;
	firstInit = false;

	shadowConfig  = configHandler->GetInt("Shadows");
	shadowMapSize = configHandler->GetInt("ShadowMapSize");
	// disabled; other option usually produces worse resolution
	shadowProMode = configHandler->GetInt("ShadowProjectionMode");
	//shadowProMode = SHADOWPROMODE_CAM_CENTER;
	shadowColorMode = configHandler->GetInt("ShadowColorMode");
	shadowGenBits = SHADOWGEN_BIT_NONE;

	shadowsLoaded = false;
	inShadowPass = false;

	shadowDepthTexture = nullptr;
	shadowColorTexture = nullptr;

	if (!tmpFirstInit && !shadowsSupported)
		return;

	// possible values for the "Shadows" config-parameter:
	// < 0: disable and don't try to initialize
	//   0: disable, but create a fallback FBO
	// > 0: enabled (by default for all shadow-casting geometry if equal to 1)
	if (shadowConfig < 0) {
		LOG("[%s] shadow rendering is disabled (config-value %d)", __func__, shadowConfig);
		return;
	}

	if (shadowConfig > 0)
		shadowGenBits = SHADOWGEN_BIT_MODEL | SHADOWGEN_BIT_MAP | SHADOWGEN_BIT_PROJ | SHADOWGEN_BIT_TREE;

	if (shadowConfig > 1)
		shadowGenBits &= (~shadowConfig);

	// no warnings when running headless
	if (SpringVersion::IsHeadless())
		return;

	if (!InitFBOAndTextures()) {
		// free any resources allocated by InitFBOAndTextures()
		FreeFBOAndTextures();

		LOG_L(L_ERROR, "[%s] failed to initialize depth-texture FBO", __func__);
		return;
	}

	if (tmpFirstInit)
		shadowsSupported = true;

	LoadProjectionMatrix(CCameraHandler::GetCamera(CCamera::CAMTYPE_SHADOW));

	if (shadowConfig > 0)
		LoadShadowGenShaders();
}

void CShadowHandler::Kill()
{
	FreeFBOAndTextures();
	shaderHandler->ReleaseProgramObjects("[ShadowHandler]");
	shadowGenProgs.fill(nullptr);
}


void CShadowHandler::Update()
{
	CCamera* playCam = CCameraHandler::GetCamera(CCamera::CAMTYPE_PLAYER);
	CCamera* shadCam = CCameraHandler::GetCamera(CCamera::CAMTYPE_SHADOW);

	SetShadowMatrix(playCam, shadCam);
	SetShadowCamera(shadCam);
}

void CShadowHandler::SaveShadowMapTextures() const
{
	// glSaveTexture requires raw GL texture IDs
	const uint32_t depthID = shadowDepthTexture ? shadowDepthTexture->GetNativeHandle() : 0;
	const uint32_t colorID = shadowColorTexture ? shadowColorTexture->GetNativeHandle() : 0;
	if (depthID > 0)
		glSaveTexture(depthID, fmt::format("smDepth_{}.png", globalRendering->drawFrame).c_str());
	if (colorID > 0)
		glSaveTexture(colorID, fmt::format("smColor_{}.png", globalRendering->drawFrame).c_str());
}

void CShadowHandler::DrawFrustumDebug() const
{
	if (!debugFrustum || !shadowsLoaded)
		return;

	CCamera* shadCam = CCameraHandler::GetCamera(CCamera::CAMTYPE_SHADOW);

	auto& rb = RenderBuffer::GetTypedRenderBuffer<VA_TYPE_0>();
	rb.AssertSubmission();

	rb.AddVertices({ { shadCam->GetFrustumVert(0) }, { shadCam->GetFrustumVert(1) } }); // NBL - NBR
	rb.AddVertices({ { shadCam->GetFrustumVert(1) }, { shadCam->GetFrustumVert(2) } }); // NBR - NTR
	rb.AddVertices({ { shadCam->GetFrustumVert(2) }, { shadCam->GetFrustumVert(3) } }); // NTR - NTL
	rb.AddVertices({ { shadCam->GetFrustumVert(3) }, { shadCam->GetFrustumVert(0) } }); // NTL - NBL

	rb.AddVertices({ { shadCam->GetFrustumVert(3) }, { shadCam->GetFrustumVert(7) } }); // NTL - FTL
	rb.AddVertices({ { shadCam->GetFrustumVert(2) }, { shadCam->GetFrustumVert(6) } }); // NTR - FTR
	rb.AddVertices({ { shadCam->GetFrustumVert(0) }, { shadCam->GetFrustumVert(4) } }); // NBL - FBL
	rb.AddVertices({ { shadCam->GetFrustumVert(1) }, { shadCam->GetFrustumVert(5) } }); // NBR - FBR

	rb.AddVertices({ { shadCam->GetFrustumVert(4) }, { shadCam->GetFrustumVert(5) } }); // FBL - FBR
	rb.AddVertices({ { shadCam->GetFrustumVert(5) }, { shadCam->GetFrustumVert(6) } }); // FBR - FTR
	rb.AddVertices({ { shadCam->GetFrustumVert(6) }, { shadCam->GetFrustumVert(7) } }); // FTR - FTL
	rb.AddVertices({ { shadCam->GetFrustumVert(7) }, { shadCam->GetFrustumVert(4) } }); // FTL - FBL

	auto* ctx = RHI::GetDevice()->GetContext();
	auto& sh = rb.GetShader();
	ctx->SetLineWidth(2.0f);
	sh.Enable();
	sh.SetUniform("ucolor", 0.0f, 0.0f, 1.0f, 1.0f);
	rb.DrawArrays(GL_LINES);
	sh.SetUniform("ucolor", 1.0f, 1.0f, 1.0f, 1.0f);
	sh.Disable();
	ctx->SetLineWidth(1.0f);
}

void CShadowHandler::FreeFBOAndTextures() {
	if (smOpaqFBO && smOpaqFBO->IsComplete()) {
		smOpaqFBO->Bind();
		smOpaqFBO->DetachAll();
		smOpaqFBO->Unbind();
	}

	smOpaqFBO = nullptr;
	shadowDepthTexture = nullptr;
	shadowColorTexture = nullptr;
}



void CShadowHandler::LoadProjectionMatrix(const CCamera* shadowCam)
{
	const CMatrix44f& ccm = shadowCam->GetClipControlMatrix();
	      CMatrix44f& spm = projMatrix[SHADOWMAT_TYPE_DRAWING];

	// same as glOrtho(-1, 1,  -1, 1,  -1, 1); just inverts Z
	// spm.LoadIdentity();
	// spm.SetZ(-FwdVector);

	// same as glOrtho(0, 1,  0, 1,  0, -1); maps [0,1] to [-1,1]
	spm.LoadIdentity();
	spm.Translate(-OnesVector);
	spm.Scale(OnesVector * 2.0f);

	// if using ZTO clip-space, cancel out the above remap for Z
	spm = ccm * spm;
}

void CShadowHandler::LoadShadowGenShaders()
{
	// NOTE: Shader loading uses GL_VERTEX_SHADER/GL_FRAGMENT_SHADER constants
	// which are part of the shader system. Shader migration is handled by the
	// shader-pipeline agent, not this migration.
	#define sh shaderHandler
	static const std::string shadowGenProgHandles[SHADOWGEN_PROGRAM_COUNT] = {
		"ShadowGenShaderProgModel",
		"ShadowGenShaderProgModelGL4",
		"ShadowGenshaderProgMap",
		"ShadowGenshaderProgProjectileOpaque",
	};
	static const std::string shadowGenProgDefines[SHADOWGEN_PROGRAM_COUNT] = {
		"#define SHADOWGEN_PROGRAM_MODEL\n",
		"#define SHADOWGEN_PROGRAM_MODEL_GL4\n",
		"#define SHADOWGEN_PROGRAM_MAP\n",
		"#define SHADOWGEN_PROGRAM_PROJ_OPAQ\n",
	};

	// #version has to be added here because it is conditional
	static const std::string versionDefs[3] = {
		"#version 130\n",
		"#version " + IntToString(RHI::GetDevice()->SupportFragDepthLayout()? 420: 130) + "\n",
	};

	static const std::string extraDefs =
		("#define SUPPORT_CLIP_CONTROL " + IntToString(RHI::GetDevice()->SupportClipSpaceControl()) + "\n") +
		("#define SUPPORT_DEPTH_LAYOUT " + IntToString(RHI::GetDevice()->SupportFragDepthLayout()) + "\n");

	for (int i = 0; i < SHADOWGEN_PROGRAM_COUNT; i++) {
		if (i == SHADOWGEN_PROGRAM_MODEL_GL4)
			continue; //special path

		if (i == SHADOWGEN_PROGRAM_MAP)
			continue; //special path

		Shader::IProgramObject* po = sh->CreateProgramObject("[ShadowHandler]", shadowGenProgHandles[i] + "GLSL");

		po->AttachShaderObject(sh->CreateShaderObject("GLSL/ShadowGenVertProg.glsl", versionDefs[0] + shadowGenProgDefines[i] + extraDefs, GL_VERTEX_SHADER));
		po->AttachShaderObject(sh->CreateShaderObject("GLSL/ShadowGenFragProg.glsl", versionDefs[1] + shadowGenProgDefines[i] + extraDefs, GL_FRAGMENT_SHADER));

		po->Link();
		po->Enable();
		po->SetUniform("alphaMaskTex", 0);
		po->SetUniform("alphaParams", mapInfo->map.voidAlphaMin, 0.0f);
		po->Disable();
		po->Validate();

		if (!po->IsValid()) {
			po->RemoveShaderObject(GL_FRAGMENT_SHADER);
			po->AttachShaderObject(sh->CreateShaderObject("GLSL/ShadowGenFragProg.glsl", versionDefs[0] + shadowGenProgDefines[i] + extraDefs, GL_FRAGMENT_SHADER));
			po->Link();
			po->Enable();
			po->SetUniform("alphaMaskTex", 0);
			po->SetUniform("alphaParams", mapInfo->map.voidAlphaMin, 0.0f);
			po->Disable();
			po->Validate();
		}

		shadowGenProgs[i] = po;
	}
	{
		Shader::IProgramObject* po = sh->CreateProgramObject("[ShadowHandler]", shadowGenProgHandles[SHADOWGEN_PROGRAM_MAP] + "GLSL");

		po->AttachShaderObject(sh->CreateShaderObject("GLSL/ShadowGenVertMapProg.glsl", versionDefs[0] + shadowGenProgDefines[SHADOWGEN_PROGRAM_MAP] + extraDefs, GL_VERTEX_SHADER));
		po->AttachShaderObject(sh->CreateShaderObject("GLSL/ShadowGenFragProg.glsl"   , versionDefs[1] + shadowGenProgDefines[SHADOWGEN_PROGRAM_MAP] + extraDefs, GL_FRAGMENT_SHADER));
		po->BindAttribLocation("vertexPos", 0);
		po->Link();
		po->Enable();
		po->SetUniform("alphaMaskTex", 0);
		po->SetUniform("heightMapTex", 1);
		po->SetUniform("alphaParams", mapInfo->map.voidAlphaMin, 0.0f);
		po->SetUniform("mapSize",
			static_cast<float>(mapDims.mapx * SQUARE_SIZE), static_cast<float>(mapDims.mapy * SQUARE_SIZE),
					   1.0f / (mapDims.mapx * SQUARE_SIZE),            1.0f / (mapDims.mapy * SQUARE_SIZE)
		);
		po->SetUniform("texSquare", 0, 0);
		po->Disable();
		po->Validate();

		if (!po->IsValid()) {
			po->RemoveShaderObject(GL_FRAGMENT_SHADER);
			po->AttachShaderObject(sh->CreateShaderObject("GLSL/ShadowGenFragProg.glsl", versionDefs[0] + shadowGenProgDefines[SHADOWGEN_PROGRAM_MAP] + extraDefs, GL_FRAGMENT_SHADER));
			po->Link();
			po->Enable();
			po->SetUniform("alphaMaskTex", 0);
			po->SetUniform("heightMapTex", 1);
			po->SetUniform("alphaParams", mapInfo->map.voidAlphaMin, 0.0f);
			po->SetUniform("mapSize",
				static_cast<float>(mapDims.mapx * SQUARE_SIZE), static_cast<float>(mapDims.mapy * SQUARE_SIZE),
						   1.0f / (mapDims.mapx * SQUARE_SIZE),            1.0f / (mapDims.mapy * SQUARE_SIZE)
			);
			po->SetUniform("texSquare", 0, 0);
			po->Disable();
			po->Validate();
		}

		shadowGenProgs[SHADOWGEN_PROGRAM_MAP] = po;
	}
	if (RHI::GetDevice()->HaveGL4()) {
		Shader::IProgramObject* po = sh->CreateProgramObject("[ShadowHandler]", shadowGenProgHandles[SHADOWGEN_PROGRAM_MODEL_GL4] + "GLSL");

		po->AttachShaderObject(sh->CreateShaderObject("GLSL/ShadowGenVertProgGL4.glsl", shadowGenProgDefines[SHADOWGEN_PROGRAM_MODEL_GL4] + extraDefs, GL_VERTEX_SHADER));
		po->AttachShaderObject(sh->CreateShaderObject("GLSL/ShadowGenFragProgGL4.glsl", shadowGenProgDefines[SHADOWGEN_PROGRAM_MODEL_GL4] + extraDefs, GL_FRAGMENT_SHADER));
		po->Link();
		po->Enable();
		po->SetUniform("alphaCtrl", 0.5f, 1.0f, 0.0f, 0.0f); // test > 0.5
		po->Disable();
		po->Validate();

		shadowGenProgs[SHADOWGEN_PROGRAM_MODEL_GL4] = po;
	}

	shadowsLoaded = true;
	#undef sh
}



bool CShadowHandler::InitFBOAndTextures()
{
	auto* device = RHI::GetDevice();
	auto* ctx = device->GetContext();

	//create dummy textures / FBO in case shadowConfig is 0
	const int realShTexSize = shadowConfig > 0 ? shadowMapSize : 1;

	// Create the FBO
	smOpaqFBO = device->CreateFramebuffer();

	if (!smOpaqFBO) {
		LOG_L(L_ERROR, "[%s] framebuffer not valid", __func__);
		return false;
	}

	struct Preset {
		RHI::TextureWrap clampMode;
		RHI::TextureFilter filterMode;
		const char* name;
	};
	static constexpr Preset presets[] = {
		{RHI::TextureWrap::ClampToBorder, RHI::TextureFilter::Linear,  "SHADOW-BEST"  },
		{RHI::TextureWrap::ClampToEdge,   RHI::TextureFilter::Nearest, "SHADOW-COMPAT"},
	};

	bool status = false;
	for (const auto& preset : presets)
	{
		if (smOpaqFBO->IsComplete())
			smOpaqFBO->DetachAll();

		//depth
		const int depthBits = std::min(device->GetDepthBufferBitDepth(), 24);
		const auto depthFormat = DepthBitsToRHIFormat(depthBits);

		shadowDepthTexture = device->CreateTexture(
			RHI::TextureType::Texture2D,
			depthFormat,
			realShTexSize,
			realShTexSize);

		shadowDepthTexture->SetWrapS(preset.clampMode);
		shadowDepthTexture->SetWrapT(preset.clampMode);
		shadowDepthTexture->SetMinFilter(preset.filterMode);
		shadowDepthTexture->SetMagFilter(preset.filterMode);

		/// color
		const auto colorFormat = static_cast<bool>(shadowColorMode) ? RHI::TextureFormat::RGB8 : RHI::TextureFormat::R8;

		shadowColorTexture = device->CreateTexture(
			RHI::TextureType::Texture2D,
			colorFormat,
			realShTexSize,
			realShTexSize);

		shadowColorTexture->SetWrapS(preset.clampMode);
		shadowColorTexture->SetWrapT(preset.clampMode);
		shadowColorTexture->SetMinFilter(preset.filterMode);
		shadowColorTexture->SetMagFilter(preset.filterMode);

		// RHI_TODO: texture swizzle mask is not in RHI interface yet.
		// The original code sets swizzle for greyscale mode (R->RRR1)
		// and RGB mode (RGB->RGB1). This is a cross-cutting RHI gap
		// that needs IRHITexture::SetSwizzle() added.

		// Mesa complains about an incomplete FBO if calling Bind before TexImage (?)
		smOpaqFBO->Bind();
		smOpaqFBO->AttachDepth(shadowDepthTexture.get());
		smOpaqFBO->AttachColor(shadowColorTexture.get(), 0);

		// test the FBO
		status = smOpaqFBO->IsComplete();

		if (status) //exit on the first occasion
			break;
	}

	ctx->ClearDepth(1.0f);
	ctx->Clear(false, true, false);
	EnableColorOutput(true);
	ctx->ClearColor(1.0f, 1.0f, 1.0f, 0.0f);
	ctx->Clear(true, false, false);

	smOpaqFBO->Unbind();

	// revert to FBO = 0 default
	ctx->SetColorMask(true, true, true, true);

	return status;
}

void CShadowHandler::DrawShadowPasses()
{
	auto* device = RHI::GetDevice();
	auto* ctx = device->GetContext();

	inShadowPass = true;

	// Set up culling state via RHI pipeline
	RHI::PipelineDesc cullDesc;
	cullDesc.rasterizer.cullMode = RHI::CullMode::Back;
	auto cullPipeline = device->CreatePipeline(cullDesc);
	ctx->BindPipeline(cullPipeline.get());

	eventHandler.DrawWorldShadow();

	EnableColorOutput(true);
	ctx->ClearColor(1.0f, 1.0f, 1.0f, 0.0f);
	ctx->Clear(true, false, false);
	EnableColorOutput(false);

	if ((shadowGenBits & SHADOWGEN_BIT_TREE) != 0) {
		grassDrawer->DrawShadow();
	}

	if ((shadowGenBits & SHADOWGEN_BIT_PROJ) != 0){
		projectileDrawer->DrawShadowOpaque();
	}
	if ((shadowGenBits & SHADOWGEN_BIT_MODEL) != 0) {
		unitDrawer->DrawShadowPass();
		featureDrawer->DrawShadowPass();
	}

	// Restore GL_BACK culling, because Lua shadow materials might
	// have changed culling at their own discretion
	ctx->BindPipeline(cullPipeline.get());
	if ((shadowGenBits & SHADOWGEN_BIT_MAP) != 0){
		ZoneScopedN("Draw::World::CreateShadows::Terrain");
		readMap->GetGroundDrawer()->DrawShadowPass();
	}

	//transparent pass, comes last
	if ((shadowGenBits & SHADOWGEN_BIT_PROJ) != 0) {
		projectileDrawer->DrawShadowTransparent();
		eventHandler.DrawShadowPassTransparent();
	}

	// Restore default pipeline state
	RHI::PipelineDesc defaultDesc;
	defaultDesc.rasterizer.cullMode = RHI::CullMode::None;
	auto defaultPipeline = device->CreatePipeline(defaultDesc);
	ctx->BindPipeline(defaultPipeline.get());

	inShadowPass = false;
}

static CMatrix44f ComposeLightMatrix(const CCamera* playerCam, const ISkyLight* light)
{
	CMatrix44f lightMatrix;

	// sun direction is in world-space, invert it
	float3 zDir = -float3(light->GetLightDir());

	// Try to rotate LM's X and Y around Z direction to fit playerCam tightest

	// find the most orthogonal vector to zDir and call it xDir
	float minDot = 1.0f;
	float3 xDir;
	for (const auto* dir : { &playerCam->forward, &playerCam->right, &playerCam->up }) {
		const float dp = zDir.dot(*dir);
		if (math::fabs(dp) < minDot) {
			xDir = std::copysign(1.0f, dp) * (*dir);
			minDot = math::fabs(dp);
		}
	}

	// orthonormalize
	xDir = (xDir - xDir.dot(zDir) * zDir).ANormalize();
	float3 yDir = xDir.cross(zDir).ANormalize();

	lightMatrix.SetZ(zDir);
	lightMatrix.SetY(yDir);
	lightMatrix.SetX(xDir);

	return lightMatrix;
}

static CMatrix44f ComposeScaleMatrix(const float4 scales)
{
	// note: T is z-bias, scales.z is z-near
	return (CMatrix44f(FwdVector * 0.5f, RgtVector / scales.x, UpVector / scales.y, FwdVector / scales.w));
}

void CShadowHandler::SetShadowMatrix(CCamera* playerCam, CCamera* shadowCam)
{
	const CMatrix44f lightMatrix = ComposeLightMatrix(playerCam, ISky::GetSky()->GetLight());
	const CMatrix44f scaleMatrix = ComposeScaleMatrix(shadowProjScales = GetShadowProjectionScales(playerCam, lightMatrix));

	viewMatrix[SHADOWMAT_TYPE_CULLING].LoadIdentity();
	viewMatrix[SHADOWMAT_TYPE_CULLING].SetX(lightMatrix.GetX());
	viewMatrix[SHADOWMAT_TYPE_CULLING].SetY(lightMatrix.GetY());
	viewMatrix[SHADOWMAT_TYPE_CULLING].SetZ(lightMatrix.GetZ());
	viewMatrix[SHADOWMAT_TYPE_CULLING].SetPos(projMidPos[2]);

	viewMatrix[SHADOWMAT_TYPE_DRAWING].LoadIdentity();
	viewMatrix[SHADOWMAT_TYPE_DRAWING].SetX(lightMatrix.GetX());
	viewMatrix[SHADOWMAT_TYPE_DRAWING].SetY(lightMatrix.GetY());
	viewMatrix[SHADOWMAT_TYPE_DRAWING].SetZ(lightMatrix.GetZ());
	viewMatrix[SHADOWMAT_TYPE_DRAWING].Scale(float3(scaleMatrix[0], scaleMatrix[5], scaleMatrix[10]));
	viewMatrix[SHADOWMAT_TYPE_DRAWING].Transpose();
	viewMatrix[SHADOWMAT_TYPE_DRAWING].SetPos(viewMatrix[SHADOWMAT_TYPE_DRAWING] * -projMidPos[2]);
	viewMatrix[SHADOWMAT_TYPE_DRAWING].SetPos(viewMatrix[SHADOWMAT_TYPE_DRAWING].GetPos() + scaleMatrix.GetPos());
}

void CShadowHandler::SetShadowCamera(CCamera* shadowCam)
{
	const int realShTexSize = shadowConfig > 0 ? shadowMapSize : 1;

	shadowCam->SetProjMatrix(projMatrix[SHADOWMAT_TYPE_DRAWING]);
	shadowCam->SetViewMatrix(viewMatrix[SHADOWMAT_TYPE_DRAWING]);

	shadowCam->SetAspectRatio(shadowProjScales.x / shadowProjScales.y);
	shadowCam->SetFrustumScales(shadowProjScales * float4(0.5f, 0.5f, 1.0f, 1.0f));
	shadowCam->UpdateFrustum();
	shadowCam->UpdateLoadViewport(0, 0, realShTexSize, realShTexSize);
	shadowCam->Update({false, false, false, false, false});

	shadowCam->SetProjMatrix(projMatrix[SHADOWMAT_TYPE_CULLING]);
	shadowCam->SetViewMatrix(viewMatrix[SHADOWMAT_TYPE_CULLING]);
	shadowCam->UpdateFrustum();
}


void CShadowHandler::SetupShadowTexSampler(unsigned int texUnit, bool enable) const
{
	if (shadowDepthTexture)
		shadowDepthTexture->Bind(texUnit - GL_TEXTURE0);

	SetupShadowTexSamplerRaw();
}

void CShadowHandler::SetupShadowTexSamplerRaw() const
{
	if (shadowDepthTexture)
		shadowDepthTexture->SetCompareMode(true, RHI::CompareFunc::LessEqual);
	// RHI_TODO: GL_DEPTH_TEXTURE_MODE (GL_LUMINANCE) is legacy FFP, no RHI equivalent.
}

void CShadowHandler::ResetShadowTexSampler(unsigned int texUnit, bool disable) const
{
	if (shadowDepthTexture)
		shadowDepthTexture->Unbind(texUnit - GL_TEXTURE0);

	ResetShadowTexSamplerRaw();
}

void CShadowHandler::ResetShadowTexSamplerRaw() const
{
	if (shadowDepthTexture)
		shadowDepthTexture->SetCompareMode(false);
}


void CShadowHandler::CreateShadows()
{
	auto* device = RHI::GetDevice();
	auto* ctx = device->GetContext();

	// NOTE:
	//   we unbind later in WorldDrawer::GenerateIBLTextures() to save render
	//   context switches (which are one of the slowest OpenGL operations!)
	//   together with VP restoration
	if (smOpaqFBO)
		smOpaqFBO->Bind();

	// Set up shadow rendering pipeline state
	RHI::PipelineDesc shadowDesc;
	shadowDesc.blend.enabled = false;
	shadowDesc.depthStencil.depthTestEnabled = true;
	shadowDesc.depthStencil.depthWriteEnabled = true;
	shadowDesc.blend.colorMask[0] = true;
	shadowDesc.blend.colorMask[1] = true;
	shadowDesc.blend.colorMask[2] = true;
	shadowDesc.blend.colorMask[3] = true;
	auto shadowPipeline = device->CreatePipeline(shadowDesc);
	ctx->BindPipeline(shadowPipeline.get());

	ctx->Clear(false, true, false);


	//flickers without it. Why?
	SetShadowCamera(CCameraHandler::GetCamera(CCamera::CAMTYPE_SHADOW));

	CCamera* prvCam = CCameraHandler::GetSetActiveCamera(CCamera::CAMTYPE_SHADOW);

	if (ISky::GetSky()->GetLight()->GetLightIntensity() > 0.0f)
		DrawShadowPasses();

	CCameraHandler::SetActiveCamera(prvCam->GetCamType());
	prvCam->Update();

	//revert to default, EnableColorOutput(true) is not enough
	ctx->SetColorMask(true, true, true, true);
}

void CShadowHandler::EnableColorOutput(bool enable) const
{
	auto* ctx = RHI::GetDevice()->GetContext();
	ctx->SetColorMask(enable, enable, enable, false);
}



float4 CShadowHandler::GetShadowProjectionScales(CCamera* playerCam, const CMatrix44f& lightViewMat) {
	float4 projScales;
	float2 projRadius;

	switch (shadowProMode) {
		case SHADOWPROMODE_CAM_CENTER: {
			projScales.x = GetOrthoProjectedFrustumRadius(playerCam, lightViewMat, projMidPos[2]);
		} break;
		case SHADOWPROMODE_MAP_CENTER: {
			projScales.x = GetOrthoProjectedMapRadius(-lightViewMat.GetZ(), projMidPos[2]);
		} break;
		case SHADOWPROMODE_MIX_CAMMAP: {
			projRadius.x = GetOrthoProjectedFrustumRadius(playerCam, lightViewMat, projMidPos[0]);
			projRadius.y = GetOrthoProjectedMapRadius(-lightViewMat.GetZ(), projMidPos[1]);
			projScales.x = std::min(projRadius.x, projRadius.y);

			projMidPos[2] = projMidPos[projRadius.x >= projRadius.y];
		} break;
	}

	projScales.y = projScales.x;
	#if 0
	projScales.z = cam->GetNearPlaneDist();
	projScales.w = cam->GetFarPlaneDist();
	#else
	projScales.z = 0.0f;
	projScales.w = readMap->GetBoundingRadius() * 2.0f;
	#endif
	return projScales;
}

float CShadowHandler::GetOrthoProjectedMapRadius(const float3& sunDir, float3& projPos) {
	const float maxMapDiameter = readMap->GetBoundingRadius() * 2.0f;
	static float curMapDiameter = 0.0f;

	if (sunProjDir != sunDir) {
		sunProjDir = sunDir;

		float3 sunDirXZ = (sunDir * XZVector).ANormalize();
		float3 mapVerts[2];

		if (sunDirXZ.x >= 0.0f) {
			if (sunDirXZ.z >= 0.0f) {
				mapVerts[0] = float3(mapDims.mapx * SQUARE_SIZE, 0.0f,                       0.0f);
				mapVerts[1] = float3(                      0.0f, 0.0f, mapDims.mapy * SQUARE_SIZE);
			} else {
				mapVerts[0] = float3(                      0.0f, 0.0f,                       0.0f);
				mapVerts[1] = float3(mapDims.mapx * SQUARE_SIZE, 0.0f, mapDims.mapy * SQUARE_SIZE);
			}
		} else {
			if (sunDirXZ.z >= 0.0f) {
				mapVerts[0] = float3(mapDims.mapx * SQUARE_SIZE, 0.0f, mapDims.mapy * SQUARE_SIZE);
				mapVerts[1] = float3(                      0.0f, 0.0f,                       0.0f);
			} else {
				mapVerts[0] = float3(                      0.0f, 0.0f, mapDims.mapy * SQUARE_SIZE);
				mapVerts[1] = float3(mapDims.mapx * SQUARE_SIZE, 0.0f,                       0.0f);
			}
		}

		const float3 v1 = (mapVerts[1] - mapVerts[0]).ANormalize();
		const float3 v2 = float3(-sunDirXZ.z, 0.0f, sunDirXZ.x);

		curMapDiameter = maxMapDiameter * v2.dot(v1);

		projPos.x = (mapDims.mapx * SQUARE_SIZE) * 0.5f;
		projPos.z = (mapDims.mapy * SQUARE_SIZE) * 0.5f;
		projPos.y = CGround::GetHeightReal(projPos.x, projPos.z, false);
	}

	return curMapDiameter;
}

float CShadowHandler::GetOrthoProjectedFrustumRadius(CCamera* playerCam, const CMatrix44f& lightViewMat, float3& centerPos) {
	float3 frustumPoints[8];

	#if 0
	{
		float sqRadius = 0.0f;
		projPos = CalcShadowProjectionPos(playerCam, &frustumPoints[0]);

		for (unsigned int n = 0; n < 8; n++) {
			sqRadius = std::max(sqRadius, (frustumPoints[n] - projPos).SqLength());
		}

		const float maxMapDiameter = readMap->GetBoundingRadius() * 2.0f;
		const float frustumDiameter = std::sqrt(sqRadius) * 2.0f;

		return (std::min(maxMapDiameter, frustumDiameter));
	}
	#else
	{
		CMatrix44f lightViewCenterMat;
		lightViewCenterMat.SetX(lightViewMat.GetX());
		lightViewCenterMat.SetY(lightViewMat.GetY());
		lightViewCenterMat.SetZ(lightViewMat.GetZ());
		centerPos = CalcShadowProjectionPos(playerCam, &frustumPoints[0]);
		lightViewCenterMat.SetPos(centerPos);

		float2 xbounds = {std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()};
		float2 zbounds = {std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()};

		for (unsigned int n = 0; n < 8; n++) {
			frustumPoints[n] = lightViewCenterMat * frustumPoints[n];

			xbounds.x = std::min(xbounds.x, frustumPoints[n].x);
			xbounds.y = std::max(xbounds.y, frustumPoints[n].x);
			zbounds.x = std::min(zbounds.x, frustumPoints[n].z);
			zbounds.y = std::max(zbounds.y, frustumPoints[n].z);
		}

		return (std::min(readMap->GetBoundingRadius() * 2.0f, std::max(xbounds.y - xbounds.x, zbounds.y - zbounds.x)));
	}
	#endif
}

float3 CShadowHandler::CalcShadowProjectionPos(CCamera* playerCam, float3* frustumPoints)
{
	static constexpr float T1 = 100.0f;
	static constexpr float T2 = 200.0f;

	float3 projPos;
	for (int i = 0; i < 8; ++i)
		frustumPoints[i] = playerCam->GetFrustumVert(i);

	const std::initializer_list<float4> clipPlanes = {
		float4{-UpVector,  (readMap->GetCurrMaxHeight() + T1) },
		float4{ UpVector, -(readMap->GetCurrMinHeight() - T1) },
	};

	for (int i = 0; i < 4; ++i) {
		ClipRayByPlanes(frustumPoints[4 + i], frustumPoints[i], clipPlanes);
		ClipRayByPlanes(frustumPoints[i], frustumPoints[4 + i], clipPlanes);

		frustumPoints[    i].x = std::clamp(frustumPoints[    i].x, -T2, mapDims.mapx * SQUARE_SIZE + T2);
		frustumPoints[    i].z = std::clamp(frustumPoints[    i].z, -T2, mapDims.mapy * SQUARE_SIZE + T2);
		frustumPoints[4 + i].x = std::clamp(frustumPoints[4 + i].x, -T2, mapDims.mapx * SQUARE_SIZE + T2);
		frustumPoints[4 + i].z = std::clamp(frustumPoints[4 + i].z, -T2, mapDims.mapy * SQUARE_SIZE + T2);

		projPos += frustumPoints[i] + frustumPoints[4 + i];
	}

	projPos *= 0.125f;

	return projPos;
}
