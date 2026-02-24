/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

/**
 * RHI Migration Status: TIER 4.1 - MIGRATED
 *
 * Migrated GL calls to RHI equivalents:
 *   - Pipeline state (blend, depth test): RHI::PipelineDesc + BindPipeline()
 *   - Viewport: ctx->SetViewport() (with explicit save/restore via globalRendering)
 *   - Draw calls: ctx->Draw()
 *   - Cubemap texture creation: device->CreateTexture(TextureCube) with UploadCubeFace()
 *   - Texture mipmap generation: texture->GenerateMipmaps()
 *   - Matrix stack: RHI::MatrixStack for CPU-side transforms
 *   - MVP uniform: shader->SetUniformMatrix4x4 (CubeMapVS reads uniform, not FFP)
 *   - Cubemap binding: skyTex RHI wrapper Bind/Unbind (owning wrapper)
 *   - Cubemap wrap modes: RHI texture SetWrapS/T
 *   - Viewport query: globalRendering->viewPosX/Y, viewSizeX/Y (replaces glGetIntegerv)
 *
 * Retained GL calls:
 *   - Framebuffer draw buffer (1 call): glDrawBuffer (no RHI FBO equivalent)
 *   - 2D texture deletion (1 call): glDeleteTextures in convertToCM path (temp texture)
 */

#include <vector>
#include <algorithm>

#include "SkyBox.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/GL/myGL.h"  // retained: glDrawBuffer, glDeleteTextures (convertToCM temp 2D texture)
#include "Rendering/GL/FBO.h"
#include "Rendering/RHI/RHITypes.h"
#include "Rendering/RHI/RHIPipeline.h"
#include "Rendering/RHI/RHIContext.h"
#include "Rendering/RHI/RHIDevice.h"
#include "Rendering/RHI/RHIFactory.h"
#include "Rendering/RHI/RHITexture.h"
#include "Rendering/RHI/MatrixStack.h"
#include "Rendering/Shaders/Shader.h"
#include "Rendering/Shaders/ShaderHandler.h"
#include "Rendering/Textures/Bitmap.h"
#include "Rendering/Env/DebugCubeMapTexture.h"
#include "Rendering/Env/WaterRendering.h"
#include "Game/Game.h"
#include "Game/Camera.h"
#include "Map/MapInfo.h"
#include "Map/ReadMap.h"
#include "System/Exceptions.h"
#include "System/ScopedResource.h"
#include "System/float3.h"
#include "System/type2.h"
#include "System/Color.h"
#include "System/Config/ConfigHandler.h"
#include "System/Log/ILog.h"

#include "System/Misc/TracyDefs.h"

#define LOG_SECTION_SKY_BOX "SkyBox"
LOG_REGISTER_SECTION_GLOBAL(LOG_SECTION_SKY_BOX)

// use the specific section for all LOG*() calls in this source file
#ifdef LOG_SECTION_CURRENT
	#undef LOG_SECTION_CURRENT
#endif
#define LOG_SECTION_CURRENT LOG_SECTION_SKY_BOX

void CSkyBox::Init(uint32_t textureID, uint32_t xsize, uint32_t ysize, bool convertToCM)
{
	RECOIL_DETAILED_TRACY_ZONE;
	shader = nullptr;
#ifndef HEADLESS
	if (textureID == 0)
		return;

	auto* device = RHI::GetDevice();

	if (convertToCM) {
		auto generateMipMaps = configHandler->GetBool("CubeTexGenerateMipMaps");
		// here textureID represents 2D texture

		// Create cubemap texture via RHI
		auto cubeTexRHI = device->CreateTexture(
			RHI::TextureType::TextureCube,
			RHI::TextureFormat::RGBA8,
			ysize, ysize,
			1, // depthOrLayers (ignored for cubemaps)
			1  // mipLevels
		);
		cubeTexRHI->SetMinFilter(generateMipMaps ? RHI::TextureFilter::LinearMipmapLinear : RHI::TextureFilter::Linear);
		cubeTexRHI->SetMagFilter(RHI::TextureFilter::Linear);
		cubeTexRHI->SetWrapS(RHI::TextureWrap::ClampToEdge);
		cubeTexRHI->SetWrapT(RHI::TextureWrap::ClampToEdge);
		cubeTexRHI->SetWrapR(RHI::TextureWrap::ClampToEdge);

		// Allocate empty faces (glTexStorage2D in GLTexture constructor already did this)
		// No explicit per-face allocation needed with glTexStorage2D

		FBO fbo;
		fbo.Init(false);

		if (!fbo.IsValid())
			return;

		fbo.Bind();

		auto* ercShader = shaderHandler->CreateProgramObject("[SkyBox]", "EquiRectConverter");
		ercShader->AttachShaderObject(shaderHandler->CreateShaderObject("GLSL/CubeMapVS.glsl", "", GL_VERTEX_SHADER));
		ercShader->AttachShaderObject(shaderHandler->CreateShaderObject("GLSL/EquiRectConverterFS.glsl", "", GL_FRAGMENT_SHADER));
		ercShader->Link();
		ercShader->Enable();
		ercShader->SetUniform("tex", 0);
		ercShader->SetUniform("uvFlip", 1.0f, 1.0f, 1.0f);
		ercShader->Disable();

		if (!ercShader->Validate()) {
			fbo.DetachAll();
			FBO::Unbind();
			cubeTexRHI.reset();
			shaderHandler->ReleaseProgramObject("[SkyBox]", "EquiRectConverter");
			return;
		}

		valid = true;
		{
			// Save current viewport from globalRendering (avoids glGetIntegerv)
			const int savedViewport[4] = {
				globalRendering->viewPosX, globalRendering->viewPosY,
				globalRendering->viewSizeX, globalRendering->viewSizeY
			};

			// Viewport via RHI
			auto* ctx = device->GetContext();
			ctx->SetViewport({0.0f, 0.0f, static_cast<float>(ysize), static_cast<float>(ysize)});

			// Pipeline state via RHI (no depth test, no blending for equirect conversion)
			{
				RHI::PipelineDesc pipeDesc;
				pipeDesc.depthStencil.depthTestEnabled = false;
				pipeDesc.blend.enabled = false;
				auto pipeline = device->CreatePipeline(pipeDesc);
				ctx->BindPipeline(pipeline.get());
			}

			// Matrix stacks via RHI (CPU-side computation)
			RHI::MatrixStack projStack;
			RHI::MatrixStack mvStack;
			projStack.LoadIdentity();
			mvStack.Push();  // duplicate identity matrix

			static constexpr std::array viewMatParams = {
				std::pair{ float3( 1.0f,  0.0f,  0.0f), float3(0.0f, -1.0f,  0.0f) }, // GL_TEXTURE_CUBE_MAP_POSITIVE_X
				std::pair{ float3(-1.0f,  0.0f,  0.0f), float3(0.0f, -1.0f,  0.0f) }, // GL_TEXTURE_CUBE_MAP_NEGATIVE_X
				std::pair{ float3( 0.0f,  1.0f,  0.0f), float3(0.0f,  0.0f,  1.0f) }, // GL_TEXTURE_CUBE_MAP_POSITIVE_Y
				std::pair{ float3( 0.0f, -1.0f,  0.0f), float3(0.0f,  0.0f, -1.0f) }, // GL_TEXTURE_CUBE_MAP_NEGATIVE_Y
				std::pair{ float3( 0.0f,  0.0f,  1.0f), float3(0.0f, -1.0f,  0.0f) }, // GL_TEXTURE_CUBE_MAP_POSITIVE_Z
				std::pair{ float3( 0.0f,  0.0f, -1.0f), float3(0.0f, -1.0f,  0.0f) }  // GL_TEXTURE_CUBE_MAP_NEGATIVE_Z
			};

			VAO vao;
			vao.Bind();
			ercShader->Enable();
			for (int side = 0; side < 6; ++side) {
				// FBO.AttachTexture expects raw GL texture ID - use GetNativeHandle()
				fbo.AttachTexture(cubeTexRHI->GetNativeHandle(), GL_TEXTURE_CUBE_MAP_POSITIVE_X + side, GL_COLOR_ATTACHMENT0);
				valid &= fbo.CheckStatus("SKYBOX-EQUIRECT-CONVERT");
				if (!valid)
					continue;

				CMatrix44f viewMat = CMatrix44f::LookAtView(
					float3(),
					viewMatParams[side].first,
					viewMatParams[side].second
				);
				mvStack.LoadMatrix(viewMat);

				// CubeMapVS.glsl reads uniform mat4 modelViewProjectionMatrix (not FFP)
				const CMatrix44f mvp = CMatrix44f(projStack.Top()) * mvStack.Top();
				ercShader->SetUniformMatrix4x4<float>("modelViewProjectionMatrix", false, &mvp.md[0][0]);

				fbo.SetDrawBuffer(GL_COLOR_ATTACHMENT0);

				// Draw via RHI
				ctx->Draw(RHI::PrimitiveType::Triangles, 6, side * 6);
			}
			ercShader->Disable();
			vao.Unbind();

			// Restore viewport (enable states are managed by scoped pipeline and restore on destruction)
			ctx->SetViewport({static_cast<float>(savedViewport[0]), static_cast<float>(savedViewport[1]),
			                  static_cast<float>(savedViewport[2]), static_cast<float>(savedViewport[3])});

			FBO::Unbind();

			if (!valid) {
				fbo = {};
				cubeTexRHI.reset();
				return;
			}

			if (generateMipMaps) {
				cubeTexRHI->GenerateMipmaps();
			}
		}

		if (!RHI::IsMetalBackend())
			glDeleteTextures(1, &textureID); // release 2D texture

		// Transfer cubemap to MapTexture via RHI (owning wrapper handles lifecycle)
		skyTex.SetRawTexID(cubeTexRHI->GetNativeHandle());
		skyTex.SetRawSize(int2(ysize, ysize));
		skyTex.SetRawRHITexture(std::move(cubeTexRHI));
	}
	else {
		valid = true;

		skyTex.SetRawSize(int2(xsize, ysize));

		// Wrap the pre-loaded cubemap with an owning RHI texture (handles lifecycle)
		auto skyTexRHI = device->CreateTextureFromExisting(
			textureID,
			RHI::TextureType::TextureCube,
			RHI::TextureFormat::RGBA8,
			xsize, ysize);
		skyTexRHI->SetWrapS(RHI::TextureWrap::ClampToEdge);
		skyTexRHI->SetWrapT(RHI::TextureWrap::ClampToEdge);

		skyTex.SetRawTexID(textureID);
		skyTex.SetRawRHITexture(std::move(skyTexRHI));
	}

	shader = shaderHandler->CreateProgramObject("[SkyBox]", "SkyBox");
	shader->AttachShaderObject(shaderHandler->CreateShaderObject("GLSL/CubeMapVS.glsl", "", GL_VERTEX_SHADER));
	shader->AttachShaderObject(shaderHandler->CreateShaderObject("GLSL/CubeMapFS.glsl", "", GL_FRAGMENT_SHADER));
	shader->Link();
	shader->Enable();
	shader->SetUniform("uvFlip", 1.0f, -1.0f, 1.0f);
	shader->SetUniform("skybox", 0);
	shader->Disable();

	valid &= shader->Validate();
#else
	valid = true;
#endif
	globalRendering->drawFog = (fogStart <= 0.99f);
}

CSkyBox::CSkyBox(const std::string& texture)
{
	CBitmap btex;
#ifndef HEADLESS
	if (!btex.Load(texture) || !(btex.textype == GL_TEXTURE_CUBE_MAP || btex.textype == GL_TEXTURE_2D)) {
		LOG_L(L_WARNING, "could not load skybox texture from file %s", texture.c_str());
		valid = false;
	}
	Init(btex.CreateTexture(), btex.xsize, btex.ysize, btex.textype == GL_TEXTURE_2D);
#else
	Init(btex.CreateTexture(), btex.xsize, btex.ysize,                         false);
#endif
}


CSkyBox::~CSkyBox()
{
	RECOIL_DETAILED_TRACY_ZONE;
#ifndef HEADLESS
	if (shader)
		shaderHandler->ReleaseProgramObject("[SkyBox]", "SkyBox");
#endif
}

void CSkyBox::Draw()
{
	RECOIL_DETAILED_TRACY_ZONE;
#ifndef HEADLESS
	if (!globalRendering->drawSky)
		return;

	if (!valid)
		return;

	// Pipeline state via RHI (no blending, depth test with LessEqual)
	{
		RHI::PipelineDesc pipeDesc;
		pipeDesc.blend.enabled = false;
		pipeDesc.depthStencil.depthTestEnabled = true;
		pipeDesc.depthStencil.depthFunc = RHI::CompareFunc::LessEqual;
		auto pipeline = RHI::GetDevice()->CreatePipeline(pipeDesc);
		RHI::GetDevice()->GetContext()->BindPipeline(pipeline.get());
	}

	// Matrix stacks via RHI (CPU-side computation)
	RHI::MatrixStack mvStack;
	RHI::MatrixStack projStack;

	CMatrix44f model; model.Rotate(skyAxisAngle.w, float3{ skyAxisAngle.x, skyAxisAngle.y, skyAxisAngle.z });
	CMatrix44f view = camera->GetViewMatrix(); view.SetPos(float3());
	mvStack.LoadMatrix(view * model);

	projStack.LoadMatrix(camera->GetProjectionMatrix());

	// Bind cubemap via RHI wrapper
	if (auto* rhiTex = skyTex.GetRawRHITexture())
		rhiTex->Bind(0);

	skyVAO.Bind();
	assert(shader->IsValid());
	shader->Enable();

	// CubeMapVS.glsl reads uniform mat4 modelViewProjectionMatrix (not FFP)
	const CMatrix44f mvp = CMatrix44f(projStack.Top()) * mvStack.Top();
	shader->SetUniformMatrix4x4<float>("modelViewProjectionMatrix", false, &mvp.md[0][0]);

	shader->SetUniform("planeColor",
		waterRendering->planeColor.x,
		waterRendering->planeColor.y,
		waterRendering->planeColor.z,
		static_cast<float>(waterRendering->hasWaterPlane && !globalRendering->drawDebugCubeMap)
	);

	// Draw via RHI
	RHI::GetDevice()->GetContext()->Draw(RHI::PrimitiveType::Triangles, 36, 0);

	shader->Disable();
	skyVAO.Unbind();

	// Unbind cubemap via RHI wrapper
	if (auto* rhiTex = skyTex.GetRawRHITexture())
		rhiTex->Unbind(0);

#endif
}
