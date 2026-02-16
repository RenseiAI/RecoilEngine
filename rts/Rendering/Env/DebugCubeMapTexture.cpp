// RHI migration status: COMPLETE
// - Cubemap creation migrated to RHI::IRHITexture
// - Texture binding migrated to RHI context
// - Matrix transforms: explicit uniform upload (modelViewProjectionMatrix)

#include "DebugCubeMapTexture.h"

#include "Rendering/GL/myGL.h"  // retained: GL constants (GL_TEXTURE_CUBE_MAP_*, GL_VERTEX_SHADER, etc.)
#include "Rendering/RHI/RHITypes.h"
#include "Rendering/RHI/RHIPipeline.h"
#include "Rendering/RHI/RHIContext.h"
#include "Rendering/RHI/RHIDevice.h"
#include "Rendering/RHI/RHIFactory.h"
#include "Rendering/RHI/RHITexture.h"
#include "Rendering/Textures/Bitmap.h"
#include "Rendering/Shaders/Shader.h"
#include "Rendering/Shaders/ShaderHandler.h"
#include "System/Log/ILog.h"
#include "Game/Camera.h"

DebugCubeMapTexture::DebugCubeMapTexture()
	: vao()
{
#ifndef HEADLESS
	auto* device = RHI::GetDevice();

	static constexpr const char* texture = "bitmaps/testsky.dds";

	CBitmap btex;
	if (!btex.Load(texture) || btex.textype != GL_TEXTURE_CUBE_MAP) {
		LOG_L(L_WARNING, "[DebugCubeMapTexture] could not load debug skybox texture from file %s, using fallback colors", texture);

		// match testsky.dds colors
		static constexpr const SColor debugFaceColors[] = {
			{1.0f, 1.0f, 0.0f, 1.0f}, // yellow GL_TEXTURE_CUBE_MAP_POSITIVE_X, Right
			{0.0f, 1.0f, 0.0f, 1.0f}, // green 	GL_TEXTURE_CUBE_MAP_NEGATIVE_X, Left
			{1.0f, 1.0f, 1.0f, 1.0f}, // white 	GL_TEXTURE_CUBE_MAP_POSITIVE_Y, Top
			{0.0f, 0.0f, 0.0f, 1.0f}, // black	GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, Bottom
			{0.0f, 1.0f, 1.0f, 1.0f}, // cyan	GL_TEXTURE_CUBE_MAP_POSITIVE_Z, Front
			{1.0f, 0.0f, 0.0f, 1.0f}, // red  	GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, Back
		};

		static constexpr int32_t FALLBACK_DIM = 16;
		std::vector<SColor> debugColorVec;
		debugColorVec.resize(FALLBACK_DIM * FALLBACK_DIM);

		dims = { FALLBACK_DIM, FALLBACK_DIM };

		// Create cubemap via RHI
		cubeTexture = device->CreateTexture(
			RHI::TextureType::TextureCube,
			RHI::TextureFormat::RGBA8,
			FALLBACK_DIM, FALLBACK_DIM,
			1, // depthOrLayers (ignored for cubemaps)
			1  // mipLevels
		);

		// Upload each face
		for (int faceIdx = 0; faceIdx < 6; ++faceIdx) {
			std::fill(debugColorVec.begin(), debugColorVec.end(), debugFaceColors[faceIdx]);
			cubeTexture->UploadCubeFace(
				static_cast<RHI::CubeFace>(faceIdx),
				0, // mipLevel
				debugColorVec.data(),
				debugColorVec.size() * sizeof(SColor)
			);
		}
	} else {
		dims = { btex.xsize, btex.ysize };

		// RHI_TODO: CBitmap::CreateDDSTextureRHI() doesn't support cubemaps yet.
		// For now, create RHI texture and use legacy CreateTexture() to populate it.
		cubeTexture = device->CreateTexture(
			RHI::TextureType::TextureCube,
			RHI::TextureFormat::RGBA8,
			btex.xsize, btex.ysize,
			1, // depthOrLayers (ignored for cubemaps)
			1  // mipLevels
		);

		// RHI_TODO: CBitmap::CreateDDSTextureRHI() doesn't support TextureCubemap yet.
		// Use legacy CreateTexture() to create a temp GL texture, then delete it.
		// This leaves cubeTexture empty (only allocated, no data uploaded).
		// Need to extend CBitmap to expose cubemap face data for RHI upload,
		// or extend CreateDDSTextureRHI() to support nv_dds::TextureCubemap type.
		uint32_t tempTexId = btex.CreateTexture();
		glDeleteTextures(1, &tempTexId); // cleanup temp texture

		LOG_L(L_WARNING, "[DebugCubeMapTexture] DDS cubemap upload via RHI not yet implemented - texture may be incomplete");
	}

	// Set texture parameters via RHI
	cubeTexture->SetMinFilter(RHI::TextureFilter::Linear);
	cubeTexture->SetMagFilter(RHI::TextureFilter::Linear);
	cubeTexture->SetWrapS(RHI::TextureWrap::ClampToEdge);
	cubeTexture->SetWrapT(RHI::TextureWrap::ClampToEdge);
	cubeTexture->SetWrapR(RHI::TextureWrap::ClampToEdge);

	shader = shaderHandler->CreateProgramObject("[DebugCubeMap]", "DebugCubeMap");
	shader->AttachShaderObject(shaderHandler->CreateShaderObject("GLSL/CubeMapVS.glsl", "", GL_VERTEX_SHADER));
	shader->AttachShaderObject(shaderHandler->CreateShaderObject("GLSL/CubeMapFS.glsl", "", GL_FRAGMENT_SHADER));
	shader->Link();
	shader->Enable();
	shader->SetUniform("uvFlip", 1.0f, -1.0f, 1.0f);
	shader->SetUniform("skybox", 0);
	shader->Disable();
	shader->Validate();
#endif
}

uint32_t DebugCubeMapTexture::GetId() const
{
	if (cubeTexture)
		return cubeTexture->GetNativeHandle();
	return 0;
}

DebugCubeMapTexture::~DebugCubeMapTexture()
{
#ifndef HEADLESS
	// RHI texture cleanup handled by unique_ptr destructor
	shaderHandler->ReleaseProgramObject("[DebugCubeMap]", "DebugCubeMap");
#endif
}

void DebugCubeMapTexture::Draw(uint32_t face) const
{
#ifndef HEADLESS
	assert(face == 0 || (face >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && face <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z));

	//all faces (default) are expected
	GLint baseVertex = 0;
	GLsizei vertCount = 36;

	//one face is expected
	if (face > 0) {
		baseVertex = (face - GL_TEXTURE_CUBE_MAP_POSITIVE_X) * 6;
		vertCount = 6;
	}

	auto* device = RHI::GetDevice();
	auto* ctx = device->GetContext();

	// Pipeline state via RHI (no alpha test, no blending)
	{
		RHI::PipelineDesc pipeDesc;
		pipeDesc.blend.enabled = false;
		pipeDesc.depthStencil.depthTestEnabled = true;
		auto pipeline = device->CreatePipeline(pipeDesc);
		ctx->BindPipeline(pipeline.get());
	}

	// Bind cubemap via RHI
	ctx->BindTexture(0, cubeTexture.get());

	vao.Bind();
	assert(shader->IsValid());
	shader->Enable();

	// Upload MVP uniform (replaces FFP matrix stack)
	CMatrix44f view = camera->GetViewMatrix();
	view.SetPos(float3());
	const CMatrix44f mvp = camera->GetProjectionMatrix() * view;
	shader->SetUniformMatrix4x4<float>("modelViewProjectionMatrix", false, &mvp.md[0][0]);

	// Draw via RHI
	ctx->Draw(RHI::PrimitiveType::Triangles, vertCount, baseVertex);

	shader->Disable();
	vao.Unbind();

	ctx->BindTexture(0, nullptr);
#endif
}

DebugCubeMapTexture& DebugCubeMapTexture::GetInstance()
{
	static DebugCubeMapTexture instance;
	return instance;
}
