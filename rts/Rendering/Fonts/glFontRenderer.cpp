// RHI MIGRATION STATUS: Mostly migrated
// - Dynamic state calls (blend, depth test) migrated to RHI context methods
// - Legacy CglNoShaderFontRenderer removed (display lists, FFP client state, matrix stack)
// - Shader path uses TypedRenderBuffer with VBOs
// - Remaining raw GL: glGetIntegerv(GL_CURRENT_PROGRAM), glUseProgram (shader save/restore)

#include "glFontRenderer.h"

#include "CFontTexture.h"
#include "glFont.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/RHI/RHIFactory.h"
#include "Rendering/RHI/RHIContext.h"
#include "Rendering/Shaders/Shader.h"
#include "System/Log/ILog.h"
#include "System/SafeUtil.h"

#include "System/Misc/TracyDefs.h"


////////////////////////////////////////
//can't be put in VFS due to initialization order
static constexpr const char* vsFont330 = R"(
#version 150 compatibility
#extension GL_ARB_explicit_attrib_location : enable

layout (location = 0) in vec3 pos;
layout (location = 1) in vec2 uv;
layout (location = 2) in vec4 col;

out Data {
	vec4 vCol;
	vec2 vUV;
};

void main() {
	vCol = col;
	vUV  = uv;
	gl_Position = gl_ModelViewProjectionMatrix * vec4(pos, 1.0); // TODO: move to UBO
}
)";

static constexpr const char* fsFont330 = R"(
#version 150

uniform sampler2D tex;

in Data{
	vec4 vCol;
	vec2 vUV;
};

out vec4 outColor;

void main() {
	vec2 texSize = vec2(textureSize(tex, 0));

	float alpha = texture(tex, vUV / texSize).x;
	outColor = vec4(vCol.r, vCol.g, vCol.b, vCol.a * alpha);
}
)";

static constexpr const char* fsFontColor330 = R"(
#version 150

uniform sampler2D tex;

in Data{
	vec4 vCol;
	vec2 vUV;
};

out vec4 outColor;

void main() {
	vec2 texSize = vec2(textureSize(tex, 0));

	outColor = texture(tex, vUV / texSize);
	outColor = outColor*vCol;
}
)";


////////////////////////////////////////////

static constexpr const char* vsFont130 = R"(
#version 130

in vec3 pos;
in vec2 uv;
in vec4 col;

out vec4 vCol;
out vec2 vUV;

void main() {
	vCol = col;
	vUV  = uv;
	gl_Position = gl_ModelViewProjectionMatrix * vec4(pos, 1.0); // TODO: move to UBO
}
)";

static constexpr const char* fsFont130 = R"(
#version 130

uniform sampler2D tex;

in vec4 vCol;
in vec2 vUV;

void main() {
	vec2 texSize = vec2(textureSize(tex, 0));

	float alpha = texture(tex, vUV / texSize).x;
	gl_FragColor = vec4(vCol.r, vCol.g, vCol.b, vCol.a * alpha);
}
)";
static constexpr const char* fsFontColor130 = R"(
#version 130

uniform sampler2D tex;

in vec4 vCol;
in vec2 vUV;

void main() {
	vec2 texSize = vec2(textureSize(tex, 0));

	float4 col = texture(tex, vUV / texSize);
	gl_FragColor = vCol*col;
}
)";

////////////////////////////////////////////

CglShaderFontRenderer::CglShaderFontRenderer()
{
	RECOIL_DETAILED_TRACY_ZONE;
	primaryBufferTC = TypedRenderBuffer<VA_TYPE_TC>(NUM_TRI_BUFFER_VERTS, NUM_TRI_BUFFER_ELEMS, IStreamBufferConcept::SB_BUFFERSUBDATA);
	outlineBufferTC = TypedRenderBuffer<VA_TYPE_TC>(NUM_TRI_BUFFER_VERTS, NUM_TRI_BUFFER_ELEMS, IStreamBufferConcept::SB_BUFFERSUBDATA);

	++fontShaderRefs;

	if (fontShaderRefs > 1)
		return;

	// can't use shaderHandler here because it invalidates the objects on reload
	// but fonts are expected to be available all the time
	fontShader = std::make_unique<Shader::GLSLProgramObject>("[GL-Font]");
	fontShaderColor = std::make_unique<Shader::GLSLProgramObject>("[GL-Font]");

	LOG("[CglFont::%s] Creating Font shaders: GLAD_GL_ARB_explicit_attrib_location = %s", __func__, globalRendering->supportExplicitAttribLoc ? "true" : "false");
	if (globalRendering->supportExplicitAttribLoc) {
		fontShader->AttachShaderObject(new Shader::GLSLShaderObject(GL_VERTEX_SHADER  , vsFont330));
		fontShader->AttachShaderObject(new Shader::GLSLShaderObject(GL_FRAGMENT_SHADER, fsFont330));
		fontShaderColor->AttachShaderObject(new Shader::GLSLShaderObject(GL_VERTEX_SHADER  , vsFont330));
		fontShaderColor->AttachShaderObject(new Shader::GLSLShaderObject(GL_FRAGMENT_SHADER, fsFontColor330));
	}
	else {
		fontShader->AttachShaderObject(new Shader::GLSLShaderObject(GL_VERTEX_SHADER  , vsFont130));
		fontShader->AttachShaderObject(new Shader::GLSLShaderObject(GL_FRAGMENT_SHADER, fsFont130));
		fontShader->BindAttribLocation("pos", 0);
		fontShader->BindAttribLocation("uv" , 1);
		fontShader->BindAttribLocation("col", 2);
		fontShaderColor->AttachShaderObject(new Shader::GLSLShaderObject(GL_VERTEX_SHADER  , vsFont130));
		fontShaderColor->AttachShaderObject(new Shader::GLSLShaderObject(GL_FRAGMENT_SHADER, fsFontColor130));
		fontShaderColor->BindAttribLocation("pos", 0);
		fontShaderColor->BindAttribLocation("uv" , 1);
		fontShaderColor->BindAttribLocation("col", 2);

	}
	fontShader->Link();
	fontShader->Enable();
	fontShader->SetUniform("tex", 0);
	fontShader->Disable();
	fontShader->Validate();
	assert(fontShader->IsValid());

	fontShaderColor->Link();
	fontShaderColor->Enable();
	fontShaderColor->SetUniform("tex", 0);
	fontShaderColor->Disable();
	fontShaderColor->Validate();
	assert(fontShaderColor->IsValid());
}

CglShaderFontRenderer::~CglShaderFontRenderer()
{
	RECOIL_DETAILED_TRACY_ZONE;
	--fontShaderRefs;
	if (fontShaderRefs > 0)
		return;

	fontShader = nullptr; // fontShader->Release() is called implicitly
	fontShaderColor = nullptr; // fontShader->Release() is called implicitly
}

void CglShaderFontRenderer::AddQuadTrianglesPB(VA_TYPE_TC&& tl, VA_TYPE_TC&& tr, VA_TYPE_TC&& br, VA_TYPE_TC&& bl)
{
	RECOIL_DETAILED_TRACY_ZONE;
	primaryBufferTC.AddQuadTriangles(std::move(tl), std::move(tr), std::move(br), std::move(bl));
}

void CglShaderFontRenderer::AddQuadTrianglesOB(VA_TYPE_TC&& tl, VA_TYPE_TC&& tr, VA_TYPE_TC&& br, VA_TYPE_TC&& bl)
{
	RECOIL_DETAILED_TRACY_ZONE;
	outlineBufferTC.AddQuadTriangles(std::move(tl), std::move(tr), std::move(br), std::move(bl));
}

void CglShaderFontRenderer::DrawTraingleElements()
{
	RECOIL_DETAILED_TRACY_ZONE;
	outlineBufferTC.DrawElements(GL_TRIANGLES);
	primaryBufferTC.DrawElements(GL_TRIANGLES);
}

void CglShaderFontRenderer::HandleTextureUpdate(CFontTexture& fnt, bool onlyUpload)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!onlyUpload)
		fnt.UpdateGlyphAtlasTexture();

	fnt.UploadGlyphAtlasTextureImpl();
}

void CglShaderFontRenderer::PushGLState(const CglFont& fnt)
{
	RECOIL_DETAILED_TRACY_ZONE;
	auto* ctx = RHI::GetDevice()->GetContext();

	// Save state (explicit restore replaces glPushAttrib/glPopAttrib)
	ctx->SetDepthTestEnabled(false);
	ctx->SetBlendEnabled(true);
	if (!userDefinedBlending)
		ctx->SetBlendFunc(RHI::BlendFactor::SrcAlpha, RHI::BlendFactor::OneMinusSrcAlpha);

	// Bind font atlas texture via RHI
	if (auto* tex = fnt.GetAtlasTexture())
		tex->Bind(0);

	glGetIntegerv(GL_CURRENT_PROGRAM, &currProgID);

	if (fnt.HasColor()) {
		fontShaderColor->Enable();
	}
	else
		fontShader->Enable();
}

void CglShaderFontRenderer::PopGLState(const CglFont& fnt)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (fnt.HasColor())
		fontShaderColor->Disable();
	else
		fontShader->Disable();

	if (currProgID > 0)
		glUseProgram(currProgID);

	// Unbind font atlas texture via RHI
	if (auto* tex = fnt.GetAtlasTexture())
		tex->Unbind(0);

	// Restore state explicitly
	auto* ctx = RHI::GetDevice()->GetContext();
	ctx->SetDepthTestEnabled(true);
	ctx->SetBlendEnabled(false);
}

void CglShaderFontRenderer::GetStats(std::array<size_t, 8>& stats) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	stats[0 + 0] = primaryBufferTC.SumElems();
	stats[0 + 1] = primaryBufferTC.SumIndcs();
	stats[0 + 2] = primaryBufferTC.NumSubmits(false);
	stats[0 + 3] = primaryBufferTC.NumSubmits(true);

	stats[4 + 0] = outlineBufferTC.SumElems();
	stats[4 + 1] = outlineBufferTC.SumIndcs();
	stats[4 + 2] = outlineBufferTC.NumSubmits(false);
	stats[4 + 3] = outlineBufferTC.NumSubmits(true);
}

std::unique_ptr<CglFontRenderer> CglFontRenderer::CreateInstance()
{
	RECOIL_DETAILED_TRACY_ZONE;
#ifndef HEADLESS
	return std::make_unique<CglShaderFontRenderer>();
#else
	return std::make_unique<CglNullFontRenderer>();
#endif
}

void CglFontRenderer::DeleteInstance(std::unique_ptr<CglFontRenderer>& instance)
{
	RECOIL_DETAILED_TRACY_ZONE;
	instance = nullptr;
}

void CglNullFontRenderer::GetStats(std::array<size_t, 8>& stats) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	std::fill(stats.begin(), stats.end(), 0u);
}
