/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */


#include "Picture.h"

#include "Rendering/GL/RenderBuffers.h"
#include "Rendering/RHI/RHIFactory.h"
#include "Rendering/RHI/RHITexture.h"
#include "Rendering/Shaders/Shader.h"
#include "System/Matrix44f.h"
#include "Rendering/Textures/Bitmap.h"
#include "System/Log/ILog.h"

namespace agui
{

	Picture::Picture(GuiElement* parent)
		: GuiElement(parent)
	{
	}

	Picture::~Picture()
	{
		rhiTexture.reset();
	}

	void Picture::Load(const std::string& _file)
	{
		file = _file;

		CBitmap bmp;
		if (bmp.Load(file)) {
			rhiTexture = bmp.CreateTextureRHI();
		}
		else {
			LOG_L(L_WARNING, "Failed to load: %s", file.c_str());
			rhiTexture.reset();
		}
	}

#ifdef HEADLESS
	void Picture::DrawSelf() {}
#else
	void Picture::DrawSelf()
	{
		if (rhiTexture) {
			auto& rb = RenderBuffer::GetTypedRenderBuffer<VA_TYPE_2DTC>();
			const SColor color = { 1.0f, 1.0f, 1.0f, 1.0f };

			rb.AddQuadTriangles(
				{ pos[0]          , pos[1]          , 0.0f, 1.0f, color },
				{ pos[0] + size[0], pos[1]          , 1.0f, 1.0f, color },
				{ pos[0] + size[0], pos[1] + size[1], 1.0f, 0.0f, color },
				{ pos[0]          , pos[1] + size[1], 0.0f, 0.0f, color }
			);

			rhiTexture->Bind(0);
			if (!RHI::IsMetalBackend()) {
				auto& sh = rb.GetShader();
				sh.Enable();
			}
			rb.SetTransformMatrix(CMatrix44f::ClipOrthoProj01());
			rb.DrawElements(GL_TRIANGLES);
			if (!RHI::IsMetalBackend()) {
				auto& sh = rb.GetShader();
				sh.Disable();
			}
		}
	}
#endif

} // namespace agui
