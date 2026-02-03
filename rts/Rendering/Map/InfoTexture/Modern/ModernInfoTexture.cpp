/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "ModernInfoTexture.h"
#include "Rendering/Shaders/Shader.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/GL/myGL.h"  // transitional: GL types still needed by FBO/VAO
#include "Rendering/RHI/RHIContext.h"
#include "Rendering/RHI/RHIDevice.h"
#include "Rendering/RHI/RHIFactory.h"


CModernInfoTexture::CModernInfoTexture(const std::string& _name)
	: CInfoTexture(_name, {}, int2(0, 0))
{}

bool CModernInfoTexture::CreateFBO(const char* fboName)
{
	if (!FBO::IsSupported())
		return false;

	fbo.Bind();
	fbo.AttachTexture(texture.GetId());
	bool status = fbo.CheckStatus(fboName);
	FBO::Unbind();

	return status;
}

void CModernInfoTexture::RunFullScreenPass()
{
	auto device = RHI::CreateDevice(RHI::GetDefaultBackend());
	auto* ctx = device->GetContext();

	fbo.Bind();
	ctx->SetViewport(RHI::Viewport{0.0f, 0.0f, static_cast<float>(texSize.x), static_cast<float>(texSize.y)});
	shader->Enable();
	vao.Bind();
	ctx->Draw(RHI::PrimitiveType::Triangles, 3, 0); // full screen triangle
	vao.Unbind();
	shader->Disable();
	FBO::Unbind();
	globalRendering->LoadViewport();
}
