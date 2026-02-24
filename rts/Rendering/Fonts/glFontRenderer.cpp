// RHI MIGRATION STATUS: FULLY MIGRATED
// - Dynamic state calls (blend, depth test) migrated to RHI context methods
// - Legacy CglNoShaderFontRenderer removed (display lists, FFP client state, matrix stack)
// - Shader path uses TypedRenderBuffer with VBOs
// - Shader save/restore uses shaderHandler tracking (no GL queries)

#include "glFontRenderer.h"

#include "CFontTexture.h"
#include "glFont.h"
#include "Rendering/GlobalRendering.h"
#include "System/Matrix44f.h"
#include "Rendering/RHI/RHIFactory.h"
#include "Rendering/RHI/RHIDevice.h"
#include "Rendering/RHI/RHIContext.h"
#include "Rendering/Shaders/Shader.h"
#include "Rendering/Shaders/ShaderHandler.h"
#include "System/Log/ILog.h"
#include "System/SafeUtil.h"

#include "System/Misc/TracyDefs.h"


////////////////////////////////////////
//can't be put in VFS due to initialization order
static constexpr const char* vsFont330 = R"(
#version 150 compatibility
#extension GL_ARB_explicit_attrib_location : enable

uniform mat4 transformMatrix = mat4(1.0);

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
	gl_Position = transformMatrix * vec4(pos, 1.0);
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

uniform mat4 transformMatrix = mat4(1.0);

in vec3 pos;
in vec2 uv;
in vec4 col;

out vec4 vCol;
out vec2 vUV;

void main() {
	vCol = col;
	vUV  = uv;
	gl_Position = transformMatrix * vec4(pos, 1.0);
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

// RHI-compatible font GLSL (for GLSL -> SPIR-V -> MSL cross-compilation)
static constexpr const char* vsFontRHI = R"(
#version 330 core
#extension GL_ARB_separate_shader_objects : require
layout(location = 0) in vec3 pos;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec4 col;
layout(std140) uniform FontUniforms { mat4 transformMatrix; };
layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vCol;
void main() {
	vCol = col;
	vUV  = uv;
	gl_Position = transformMatrix * vec4(pos, 1.0);
}
)";

static constexpr const char* fsFontRHI = R"(
#version 330 core
#extension GL_ARB_separate_shader_objects : require
uniform sampler2D tex;
layout(std140) uniform FontUniforms { mat4 transformMatrix; };
layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vCol;
out vec4 outColor;
void main() {
	vec2 texSize = vec2(textureSize(tex, 0));
	float alpha = texture(tex, vUV / texSize).x;
	outColor = vec4(vCol.r, vCol.g, vCol.b, vCol.a * alpha);
}
)";

static constexpr const char* fsFontColorRHI = R"(
#version 330 core
#extension GL_ARB_separate_shader_objects : require
uniform sampler2D tex;
layout(std140) uniform FontUniforms { mat4 transformMatrix; };
layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vCol;
out vec4 outColor;
void main() {
	vec2 texSize = vec2(textureSize(tex, 0));
	outColor = texture(tex, vUV / texSize) * vCol;
}
)";

std::unique_ptr<RHI::IRHIShader> CglShaderFontRenderer::CreateRHIFontShader(bool colorMode)
{
	auto* device = RHI::GetDevice();
	if (!device)
		return nullptr;

	auto shader = device->CreateShader(colorMode ? "[RHI-FontColor]" : "[RHI-Font]");
	shader->AttachStageFromSource(RHI::ShaderStage::Vertex, std::string(vsFontRHI));
	shader->AttachStageFromSource(RHI::ShaderStage::Fragment,
		std::string(colorMode ? fsFontColorRHI : fsFontRHI));

	shader->BindAttribLocation("pos", 0);
	shader->BindAttribLocation("uv", 1);
	shader->BindAttribLocation("col", 2);

	shader->Link();

	if (!shader->Validate()) {
		LOG_L(L_ERROR, "[CglShaderFontRenderer::%s] Failed to create RHI font shader (color=%d)",
			__func__, colorMode);
		return nullptr;
	}

	// Set texture sampler uniform
	shader->Bind();
	shader->SetUniform1i("tex", 0);
	shader->Unbind();

	LOG("[CglShaderFontRenderer::%s] Created RHI font shader (color=%d)", __func__, colorMode);
	return shader;
}

CglShaderFontRenderer::CglShaderFontRenderer()
{
	RECOIL_DETAILED_TRACY_ZONE;
	primaryBufferTC = TypedRenderBuffer<VA_TYPE_TC>(NUM_TRI_BUFFER_VERTS, NUM_TRI_BUFFER_ELEMS, IStreamBufferConcept::SB_BUFFERSUBDATA);
	outlineBufferTC = TypedRenderBuffer<VA_TYPE_TC>(NUM_TRI_BUFFER_VERTS, NUM_TRI_BUFFER_ELEMS, IStreamBufferConcept::SB_BUFFERSUBDATA);

	++fontShaderRefs;

	if (fontShaderRefs > 1)
		return;

	if (RHI::IsMetalBackend()) {
		// Metal path: create cross-compiled RHI font shaders
		fontShaderRHI = CreateRHIFontShader(false);
		fontShaderColorRHI = CreateRHIFontShader(true);
		return;
	}

	// GL path: create GLSLProgramObjects
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
	fontShaderRHI = nullptr;
	fontShaderColorRHI = nullptr;
}

bool CglShaderFontRenderer::IsValid() const
{
	if (RHI::IsMetalBackend())
		return fontShaderRHI && fontShaderRHI->IsValid();
	return fontShader && fontShader->IsValid();
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
	// Always set explicit transform — either world transform or identity for screen-space NDC
	const CMatrix44f& xform = hasWorldTransform ? worldTransform : CMatrix44f::Identity();

	// On Metal, re-set the shader override before each draw (consumed per-draw by TypedRenderBuffer)
	if (activeRHIFontShader) {
		TypedRenderBuffer<VA_TYPE_TC>::SetExternalShaderOverride(activeRHIFontShader);
	}
	outlineBufferTC.SetTransformMatrix(xform);
	outlineBufferTC.DrawElements(GL_TRIANGLES);

	if (activeRHIFontShader) {
		TypedRenderBuffer<VA_TYPE_TC>::SetExternalShaderOverride(activeRHIFontShader);
	}
	primaryBufferTC.SetTransformMatrix(xform);
	primaryBufferTC.DrawElements(GL_TRIANGLES);
}

void CglShaderFontRenderer::SetWorldTransform(const CMatrix44f& mvp)
{
	worldTransform = mvp;
	hasWorldTransform = true;
}

void CglShaderFontRenderer::ClearWorldTransform()
{
	hasWorldTransform = false;
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

	if (RHI::IsMetalBackend()) {
		// Metal: store active font shader — DrawTraingleElements sets the per-draw override
		activeRHIFontShader = fnt.HasColor() ? fontShaderColorRHI.get() : fontShaderRHI.get();
	} else {
		prevBoundProgram = shaderHandler->GetCurrentlyBoundProgram();
		if (prevBoundProgram)
			prevBoundProgram->Disable();

		if (fnt.HasColor())
			fontShaderColor->Enable();
		else
			fontShader->Enable();
	}
}

void CglShaderFontRenderer::PopGLState(const CglFont& fnt)
{
	RECOIL_DETAILED_TRACY_ZONE;

	if (RHI::IsMetalBackend()) {
		// Metal: clear active font shader
		activeRHIFontShader = nullptr;
	} else {
		if (fnt.HasColor())
			fontShaderColor->Disable();
		else
			fontShader->Disable();

		if (prevBoundProgram)
			prevBoundProgram->Enable();
	}

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
