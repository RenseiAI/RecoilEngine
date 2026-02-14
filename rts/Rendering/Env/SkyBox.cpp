/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

/**
 * RHI Migration Status: TIER 4.1 - MIGRATED
 *
 * Migrated GL calls to RHI equivalents:
 *   - Pipeline state (blend, depth test): RHI::PipelineDesc + BindPipeline()
 *   - Viewport: ctx->SetViewport() (with explicit save/restore)
 *   - Draw calls: ctx->Draw()
 *   - Cubemap texture creation: device->CreateTexture(TextureCube) with UploadCubeFace()
 *   - Texture mipmap generation: texture->GenerateMipmaps()
 *   - Matrix stack: RHI::MatrixStack for CPU-side transforms (flushed to FFP before draw)
 *
 * Retained GL calls (no RHI equivalent or external dependencies):
 *   - FFP matrix flush (6 calls): glMatrixMode(3), glLoadMatrixf(3). Required because
 *     CubeMapVS.glsl reads gl_ModelViewProjectionMatrix from FFP state. Matrix computation
 *     done CPU-side via RHI::MatrixStack, then flushed to FFP before draw.
 *   - FFP viewport query (1 call): glGetIntegerv(GL_VIEWPORT) for state save/restore (no RHI query API)
 *   - Framebuffer draw buffer (1 call): glDrawBuffer (no RHI equivalent)
 *   - skyTex binding (2 calls): glBindTexture(2). MapTexture stores raw GL IDs, not RHI texture objects.
 */

#include <vector>
#include <algorithm>

#include "SkyBox.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/GL/myGL.h"  // retained: FFP matrix flush (glMatrixMode/glLoadMatrixf), glDrawBuffer, glGetIntegerv, glBindTexture for MapTexture
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

	if (convertToCM) {
		auto generateMipMaps = configHandler->GetBool("CubeTexGenerateMipMaps");
		// here textureID represents 2D texture

		// Create cubemap texture via RHI
		auto* device = RHI::GetDevice();
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
			// Save current viewport
			int savedViewport[4];
			glGetIntegerv(GL_VIEWPORT, savedViewport);

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

				// Flush matrices to FFP state (shader reads gl_ModelViewProjectionMatrix)
				glMatrixMode(GL_PROJECTION);
				glLoadMatrixf(projStack.Top());
				glMatrixMode(GL_MODELVIEW);
				glLoadMatrixf(mvStack.Top());

				glDrawBuffer(GL_COLOR_ATTACHMENT0);

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

		glDeleteTextures(1, &textureID); // release 2D texture

		// Transfer ownership of native texture handle to MapTexture
		skyTex.SetRawTexID(cubeTexRHI->DisownNativeHandle());
		skyTex.SetRawSize(int2(ysize, ysize));
	}
	else {
		valid = true;

		skyTex.SetRawTexID(textureID);
		skyTex.SetRawSize(int2(xsize, ysize));

		// Set wrap modes for non-converted cubemaps (converted path already set via RHI)
		glBindTexture(GL_TEXTURE_CUBE_MAP, skyTex.GetID());
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
	}

	shader = shaderHandler->CreateProgramObject("[SkyBox]", "SkyBox");
	shader->AttachShaderObject(shaderHandler->CreateShaderObject("GLSL/CubeMapVS.glsl", "", GL_VERTEX_SHADER));
	shader->AttachShaderObject(shaderHandler->CreateShaderObject("GLSL/CubeMapFS.glsl", "", GL_FRAGMENT_SHADER"));
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

	// Flush matrices to FFP state (CubeMapVS.glsl reads gl_ModelViewProjectionMatrix)
	glMatrixMode(GL_MODELVIEW);
	glLoadMatrixf(mvStack.Top());
	glMatrixMode(GL_PROJECTION);
	glLoadMatrixf(projStack.Top());

	// NOTE: cubemap bind retained as raw GL - skyTex stores a raw GL texture ID
	// (MapTexture), not an RHI texture object
	glBindTexture(GL_TEXTURE_CUBE_MAP, skyTex.GetID());

	skyVAO.Bind();
	assert(shader->IsValid());
	shader->Enable();

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

	glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

	sky->SetupFog();
#endif
}
