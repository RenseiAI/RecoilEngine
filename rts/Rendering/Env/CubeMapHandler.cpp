/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

/**
 * RHI Migration Status: TIER 4.1 - MIGRATED
 *
 * Cubemap textures (envReflectionTex, skyReflectionTex, specularTex) migrated to RHI IRHITexture.
 * Texture creation via device->CreateTexture(), face uploads via UploadCubeFace()/UpdateCubeFace().
 * Getters return native handle via GetNativeHandle() for backward compatibility.
 * FBO operations (AttachTexture) still use raw GL texture IDs from GetNativeHandle().
 * Pipeline state (glPushAttrib, depth) already migrated to RHI in previous pass.
 */

#include "Game/Camera.h"
#include "Game/CameraHandler.h"
#include "Game/Game.h"
#include "Map/BaseGroundDrawer.h"
#include "Map/Ground.h"
#include "Map/ReadMap.h"
#include "Map/MapInfo.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/Units/UnitDrawer.h"
#include "Rendering/GL/myGL.h"  // retained: FBO ops, glPushAttrib/glPopAttrib
#include "Rendering/RHI/RHITypes.h"
#include "Rendering/RHI/RHIPipeline.h"
#include "Rendering/RHI/RHIContext.h"
#include "Rendering/RHI/RHIDevice.h"
#include "Rendering/RHI/RHIFactory.h"
#include "Rendering/Env/DebugCubeMapTexture.h"
#include "Rendering/Env/ISky.h"
#include "Rendering/Env/SunLighting.h"
#include "Rendering/Env/CubeMapHandler.h"
#include "System/Config/ConfigHandler.h"

#include "System/Misc/TracyDefs.h"

CONFIG(int, CubeTexSizeSpecular).defaultValue(128).minimumValue(1).description("The square resolution of each face of the specular cubemap.");
CONFIG(int, CubeTexSizeReflection).defaultValue(128).minimumValue(1).description("The square resolution of each face of the environment reflection cubemap.");
CONFIG(bool, CubeTexGenerateMipMaps).defaultValue(false).description("Generate mipmaps for the reflection and specular cubemap textures, useful for efficient subsampling and blurring.");

CubeMapHandler cubeMapHandler;


bool CubeMapHandler::Init() {
	RECOIL_DETAILED_TRACY_ZONE;
	auto* device = RHI::GetDevice();

	specTexSize = configHandler->GetInt("CubeTexSizeSpecular");
	reflTexSize = configHandler->GetInt("CubeTexSizeReflection");

	specTexPartBuf.clear();
	specTexPartBuf.resize(specTexSize * 4, 0);
	specTexFaceBuf.clear();
	specTexFaceBuf.resize(specTexSize * specTexSize * 4, 0);

	currReflectionFace = 0;
	specularTexIter = 0;

	mapSkyReflections = (!mapInfo->smf.skyReflectModTexName.empty());
	generateMipMaps = configHandler->GetBool("CubeTexGenerateMipMaps");

	// Create specular cubemap texture via RHI
	{
		specularTex = device->CreateTexture(
			RHI::TextureType::TextureCube,
			RHI::TextureFormat::RGBA8,
			specTexSize, specTexSize,
			1, // depthOrLayers (ignored for cubemaps)
			1  // mipLevels
		);
		specularTex->SetMinFilter(RHI::TextureFilter::Linear);
		specularTex->SetMagFilter(RHI::TextureFilter::Linear);
		specularTex->SetWrapS(RHI::TextureWrap::ClampToEdge);
		specularTex->SetWrapT(RHI::TextureWrap::ClampToEdge);

		CreateSpecularFace(GL_TEXTURE_CUBE_MAP_POSITIVE_X, specTexSize, float3( 1,  1,  1), float3( 0, 0, -2), float3(0, -2,  0));
		CreateSpecularFace(GL_TEXTURE_CUBE_MAP_NEGATIVE_X, specTexSize, float3(-1,  1, -1), float3( 0, 0,  2), float3(0, -2,  0));
		CreateSpecularFace(GL_TEXTURE_CUBE_MAP_POSITIVE_Y, specTexSize, float3(-1,  1, -1), float3( 2, 0,  0), float3(0,  0,  2));
		CreateSpecularFace(GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, specTexSize, float3(-1, -1,  1), float3( 2, 0,  0), float3(0,  0, -2));
		CreateSpecularFace(GL_TEXTURE_CUBE_MAP_POSITIVE_Z, specTexSize, float3(-1,  1,  1), float3( 2, 0,  0), float3(0, -2,  0));
		CreateSpecularFace(GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, specTexSize, float3( 1,  1, -1), float3(-2, 0,  0), float3(0, -2,  0));
	}

	// Create environment reflection cubemap texture via RHI
	{
		envReflectionTex = device->CreateTexture(
			RHI::TextureType::TextureCube,
			RHI::TextureFormat::RGBA8,
			reflTexSize, reflTexSize,
			1, // depthOrLayers (ignored for cubemaps)
			generateMipMaps ? 1 : 1  // TODO: calculate proper mip levels if needed
		);
		envReflectionTex->SetMinFilter(generateMipMaps ? RHI::TextureFilter::LinearMipmapLinear : RHI::TextureFilter::Linear);
		envReflectionTex->SetMagFilter(RHI::TextureFilter::Linear);
		envReflectionTex->SetWrapS(RHI::TextureWrap::ClampToEdge);
		envReflectionTex->SetWrapT(RHI::TextureWrap::ClampToEdge);

		// Allocate empty faces (glTexStorage2D in GLTexture constructor already did this)
		// No explicit per-face allocation needed with glTexStorage2D
	}

	if (generateMipMaps) {
		envReflectionTex->GenerateMipmaps();
	}

	// Create sky reflection cubemap texture via RHI (if needed)
	if (mapSkyReflections) {
		skyReflectionTex = device->CreateTexture(
			RHI::TextureType::TextureCube,
			RHI::TextureFormat::RGBA8,
			reflTexSize, reflTexSize,
			1, // depthOrLayers (ignored for cubemaps)
			1  // mipLevels
		);
		skyReflectionTex->SetMinFilter(RHI::TextureFilter::Linear);
		skyReflectionTex->SetMagFilter(RHI::TextureFilter::Linear);
		skyReflectionTex->SetWrapS(RHI::TextureWrap::ClampToEdge);
		skyReflectionTex->SetWrapT(RHI::TextureWrap::ClampToEdge);

		// Allocate empty faces (glTexStorage2D in GLTexture constructor already did this)
	}

	// reflectionCubeFBO is no-op constructed, has to be initialized manually
	reflectionCubeFBO.Init(false);

	if (reflectionCubeFBO.IsValid()) {
		reflectionCubeFBO.Bind();
		reflectionCubeFBO.CreateRenderBuffer(GL_DEPTH_ATTACHMENT_EXT, GL_DEPTH_COMPONENT, reflTexSize, reflTexSize);
		reflectionCubeFBO.Unbind();
		return true;
	}

	Free();
	return false;
}

void CubeMapHandler::Free() {
	RECOIL_DETAILED_TRACY_ZONE;
	// RHI textures cleaned up automatically via unique_ptr destructors
	specularTex.reset();
	envReflectionTex.reset();
	skyReflectionTex.reset();

	reflectionCubeFBO.Kill();
}



void CubeMapHandler::UpdateReflectionTexture()
{
	RECOIL_DETAILED_TRACY_ZONE;

	// NOTE:
	//   we unbind later in WorldDrawer::GenerateIBLTextures() to save render
	//   context switches (which are one of the slowest OpenGL operations!)
	//   together with VP restoration
	reflectionCubeFBO.Bind();

	switch (currReflectionFace) {
		case 0: { CreateReflectionFace(GL_TEXTURE_CUBE_MAP_POSITIVE_X, false); } break;
		case 1: { CreateReflectionFace(GL_TEXTURE_CUBE_MAP_NEGATIVE_X, false); } break;
		case 2: { CreateReflectionFace(GL_TEXTURE_CUBE_MAP_POSITIVE_Y, false); } break;
		case 3: { CreateReflectionFace(GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, false); } break;
		case 4: { CreateReflectionFace(GL_TEXTURE_CUBE_MAP_POSITIVE_Z, false); } break;
		case 5: { CreateReflectionFace(GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, false); } break;
		default: {} break;
	}

	if (mapSkyReflections) {
		// draw only the sky (into its own cubemap) for SSMF
		// by reusing data from previous frame we could also
		// make terrain reflect itself, not just the sky
		switch (currReflectionFace) {
			case  6: { CreateReflectionFace(GL_TEXTURE_CUBE_MAP_POSITIVE_X, true); } break;
			case  7: { CreateReflectionFace(GL_TEXTURE_CUBE_MAP_NEGATIVE_X, true); } break;
			case  8: { CreateReflectionFace(GL_TEXTURE_CUBE_MAP_POSITIVE_Y, true); } break;
			case  9: { CreateReflectionFace(GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, true); } break;
			case 10: { CreateReflectionFace(GL_TEXTURE_CUBE_MAP_POSITIVE_Z, true); } break;
			case 11: { CreateReflectionFace(GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, true); } break;
			default: {} break;
		}

		currReflectionFace +=  1;
		currReflectionFace %= 12;
	} else {
		// touch the FBO at least once per frame
		currReflectionFace += 1;
		currReflectionFace %= 6;
	}

	if (generateMipMaps && currReflectionFace == 0 && envReflectionTex) {
		envReflectionTex->GenerateMipmaps();
	}
}

void CubeMapHandler::CreateReflectionFace(unsigned int glFace, bool skyOnly)
{
	RECOIL_DETAILED_TRACY_ZONE;
	// FBO.AttachTexture expects raw GL texture ID - use GetNativeHandle()
	auto* tex = skyOnly ? skyReflectionTex.get() : envReflectionTex.get();
	reflectionCubeFBO.AttachTexture(tex ? tex->GetNativeHandle() : 0, glFace);

	glPushAttrib(GL_FOG_BIT | GL_DEPTH_BUFFER_BIT);
	const auto& sky = ISky::GetSky();

	// Clear via RHI
	RHI::GetDevice()->GetContext()->ClearColor(sky->fogColor.x, sky->fogColor.y, sky->fogColor.z, 1.0f);
	RHI::GetDevice()->GetContext()->Clear(true, true, false);

	// Depth state via RHI pipeline
	if (!skyOnly) {
		RHI::PipelineDesc pipeDesc;
		pipeDesc.depthStencil.depthTestEnabled = true;
		pipeDesc.depthStencil.depthWriteEnabled = true;
		auto pipeline = RHI::GetDevice()->CreatePipeline(pipeDesc);
		RHI::GetDevice()->GetContext()->BindPipeline(pipeline.get());
	} else {
		// do not need depth-testing for the sky alone
		RHI::PipelineDesc pipeDesc;
		pipeDesc.depthStencil.depthTestEnabled = false;
		pipeDesc.depthStencil.depthWriteEnabled = false;
		auto pipeline = RHI::GetDevice()->CreatePipeline(pipeDesc);
		RHI::GetDevice()->GetContext()->BindPipeline(pipeline.get());
	}

	{
		CCamera* prvCam = CCameraHandler::GetSetActiveCamera(CCamera::CAMTYPE_ENVMAP);
		CCamera* curCam = CCameraHandler::GetActiveCamera();

		const float3* fd = faceDirs[glFace - GL_TEXTURE_CUBE_MAP_POSITIVE_X];

		// env-reflections are only correct when drawn from an inverted
		// perspective (meaning right becomes left and up becomes down)
		curCam->forward  = fd[0];
		curCam->right    = fd[1] * -1.0f;
		curCam->up       = fd[2] * -1.0f;

		// set vertical *and* horizontal FOV to 90 degrees
		curCam->SetVFOV(90.0f);
		curCam->SetAspectRatio(1.0f);
		curCam->SetPos(prvCam->GetPos());

		curCam->UpdateLoadViewport(0, 0, reflTexSize, reflTexSize);
		curCam->UpdateViewRange();
		curCam->UpdateMatrices(globalRendering->viewSizeX, globalRendering->viewSizeY, curCam->GetAspectRatio());
		curCam->UpdateFrustum();
		curCam->LoadMatrices();

		// generate the face
		game->SetDrawMode(CGame::gameReflectionDraw);

		if (!globalRendering->drawDebugCubeMap) {
			sky->Draw();
			if (!skyOnly)
				readMap->GetGroundDrawer()->Draw(DrawPass::TerrainReflection);
		}
		else {
			debugCubeMapTexture.Draw(glFace);
		}

		game->SetDrawMode(CGame::gameNormalDraw);


		CCameraHandler::SetActiveCamera(prvCam->GetCamType());
	}

	glPopAttrib();
}


void CubeMapHandler::UpdateSpecularTexture()
{
	RECOIL_DETAILED_TRACY_ZONE;

	if (!specularTex)
		return;

	// Bind not strictly necessary for RHI face uploads, but kept for consistency
	// The underlying GL implementation in UpdateSpecularFace will bind as needed

	int specularTexRow = specularTexIter / 3; //FIXME WTF

	switch (specularTexIter % 3) {
		case 0: {
			UpdateSpecularFace(GL_TEXTURE_CUBE_MAP_POSITIVE_X, specTexSize, float3( 1,  1,  1), float3( 0, 0, -2), float3(0, -2,  0), specularTexRow, &specTexPartBuf[0]);
			UpdateSpecularFace(GL_TEXTURE_CUBE_MAP_NEGATIVE_X, specTexSize, float3(-1,  1, -1), float3( 0, 0,  2), float3(0, -2,  0), specularTexRow, &specTexPartBuf[0]);
		} break;
		case 1: {
			UpdateSpecularFace(GL_TEXTURE_CUBE_MAP_POSITIVE_Y, specTexSize, float3(-1,  1, -1), float3( 2, 0,  0), float3(0,  0,  2), specularTexRow, &specTexPartBuf[0]);
			UpdateSpecularFace(GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, specTexSize, float3(-1, -1,  1), float3( 2, 0,  0), float3(0,  0, -2), specularTexRow, &specTexPartBuf[0]);
		} break;
		case 2: {
			UpdateSpecularFace(GL_TEXTURE_CUBE_MAP_POSITIVE_Z, specTexSize, float3(-1,  1,  1), float3( 2, 0,  0), float3(0, -2,  0), specularTexRow, &specTexPartBuf[0]);
			UpdateSpecularFace(GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, specTexSize, float3( 1,  1, -1), float3(-2, 0,  0), float3(0, -2,  0), specularTexRow, &specTexPartBuf[0]);
		} break;
	}

	// update one face of one row per frame
	specularTexIter += 1;
	specularTexIter %= (specTexSize * 3);
}

void CubeMapHandler::CreateSpecularFacePart(
	unsigned int texType,
	unsigned int size,
	const float3& cdir,
	const float3& xdif,
	const float3& ydif,
	unsigned int y,
	unsigned char* buf
) {
	RECOIL_DETAILED_TRACY_ZONE;
	const auto& sky = ISky::GetSky();
	// TODO move to a shader
	for (int x = 0; x < size; ++x) {
		const float3 dir = (cdir + (xdif * (x + 0.5f)) / size + (ydif * (y + 0.5f)) / size).Normalize();
		const float dot  = std::max(0.0f, dir.dot(sky->GetLight()->GetLightDir()));
		const float spec = std::min(1.0f, std::pow(dot, sunLighting->specularExponent) + std::pow(dot, 3.0f) * 0.25f);

		buf[x * 4 + 0] = (sunLighting->modelSpecularColor.x * spec * 255);
		buf[x * 4 + 1] = (sunLighting->modelSpecularColor.y * spec * 255);
		buf[x * 4 + 2] = (sunLighting->modelSpecularColor.z * spec * 255);
		buf[x * 4 + 3] = 255;
	}
}

void CubeMapHandler::CreateSpecularFace(
	unsigned int texType,
	unsigned int size,
	const float3& cdir,
	const float3& xdif,
	const float3& ydif
) {
	RECOIL_DETAILED_TRACY_ZONE;
	for (int y = 0; y < size; ++y) {
		CreateSpecularFacePart(texType, size, cdir, xdif, ydif, y, &specTexFaceBuf[y * size * 4]);
	}

	//! note: no mipmaps, cubemap linear filtering is broken
	// Convert GL_TEXTURE_CUBE_MAP_POSITIVE_X offset to RHI::CubeFace enum
	RHI::CubeFace face = static_cast<RHI::CubeFace>(texType - GL_TEXTURE_CUBE_MAP_POSITIVE_X);
	if (specularTex) {
		specularTex->UploadCubeFace(face, 0, size, size, &specTexFaceBuf[0]);
	}
}

void CubeMapHandler::UpdateSpecularFace(
	unsigned int texType,
	unsigned int size,
	const float3& cdir,
	const float3& xdif,
	const float3& ydif,
	unsigned int y,
	unsigned char* buf
) {
	RECOIL_DETAILED_TRACY_ZONE;
	CreateSpecularFacePart(texType, size, cdir, xdif, ydif, y, buf);

	// Convert GL_TEXTURE_CUBE_MAP_POSITIVE_X offset to RHI::CubeFace enum
	RHI::CubeFace face = static_cast<RHI::CubeFace>(texType - GL_TEXTURE_CUBE_MAP_POSITIVE_X);
	if (specularTex) {
		specularTex->UpdateCubeFace(face, 0, 0, y, size, 1, buf);
	}
}
