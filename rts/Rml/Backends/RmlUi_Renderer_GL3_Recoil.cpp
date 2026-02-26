/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

/*
 * This source file is derived from the source code of RmlUi, the HTML/CSS Interface Middleware
 *
 * For the latest information, see http://github.com/mikke89/RmlUi
 *
 * Copyright (c) 2008-2010 CodePoint Ltd, Shift Technology Ltd
 * Copyright (c) 2019-2023 The RmlUi Team, and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include "RmlUi_Renderer_GL3_Recoil.h"
#include <RmlUi/Core/Log.h>

#include "Rendering/GL/myGL.h"

#include "Rendering/Shaders/Shader.h"
#include "Rendering/Shaders/ShaderHandler.h"
#include "Rendering/Textures/Bitmap.h"

#include "Rendering/RHI/RHIFactory.h"
#include "Rendering/RHI/RHIDevice.h"
#include "Rendering/RHI/RHIContext.h"
#include "Rendering/RHI/RHITexture.h"
#include "Rendering/RHI/RHIFramebuffer.h"
#include "Rendering/RHI/RHIBuffer.h"
#include "System/Log/ILog.h"
#include "RmlUi/Core/Mesh.h"
#include "RmlUi/Core/Colour.h"
#include "RmlUi/Core/MeshUtilities.h"
#include "RmlUi/Core/Dictionary.h"
#include "RmlUi/Core/Core.h"
#include "RmlUi/Core/SystemInterface.h"
#include "RmlUi/Core/DecorationTypes.h"

// Determines the anti-aliasing quality when creating layers. Enables better-looking visuals, especially when transforms are applied.
static constexpr int NUM_MSAA_SAMPLES = 2;

#define MAX_NUM_STOPS 16
#define BLUR_SIZE 7
#define BLUR_NUM_WEIGHTS ((BLUR_SIZE + 1) / 2)

#define RMLUI_STRINGIFY_IMPL(x) #x
#define RMLUI_STRINGIFY(x) RMLUI_STRINGIFY_IMPL(x)

#define RMLUI_SHADER_HEADER_VERSION "#version 330\n"
#define RMLUI_SHADER_HEADER \
    RMLUI_SHADER_HEADER_VERSION "#define MAX_NUM_STOPS " RMLUI_STRINGIFY(MAX_NUM_STOPS) "\n"

static const char* shader_vert_main = RMLUI_SHADER_HEADER R"(
uniform vec2 _translate;
uniform mat4 _transform;

in vec2 inPosition;
in vec4 inColor0;
in vec2 inTexCoord0;

out vec2 fragTexCoord;
out vec4 fragColor;

void main() {
	fragTexCoord = inTexCoord0;
	fragColor = inColor0;

	vec2 translatedPos = inPosition + _translate;
	vec4 outPos = _transform * vec4(translatedPos, 0.0, 1.0);

    gl_Position = outPos;
}
)";

static const char* shader_frag_texture = RMLUI_SHADER_HEADER R"(
uniform sampler2D _tex;
in vec2 fragTexCoord;
in vec4 fragColor;

out vec4 finalColor;

void main() {
	vec4 texColor = texture(_tex, fragTexCoord);
	finalColor = fragColor * texColor;
}
)";

static const char* shader_frag_color = RMLUI_SHADER_HEADER R"(
in vec2 fragTexCoord;
in vec4 fragColor;

out vec4 finalColor;

void main() {
	finalColor = fragColor;
}
)";

enum class ShaderGradientFunction
{
	Linear, Radial, Conic, RepeatingLinear, RepeatingRadial, RepeatingConic
}; // Must match shader definitions below.

static const char* shader_frag_gradient = RMLUI_SHADER_HEADER R"(
#define LINEAR 0
#define RADIAL 1
#define CONIC 2
#define REPEATING_LINEAR 3
#define REPEATING_RADIAL 4
#define REPEATING_CONIC 5
#define PI 3.14159265

uniform int _func; // one of the above definitions
uniform vec2 _p;   // linear: starting point,         radial: center,                        conic: center
uniform vec2 _v;   // linear: vector to ending point, radial: 2d curvature (inverse radius), conic: angled unit vector
uniform vec4 _stop_colors[MAX_NUM_STOPS];
uniform float _stop_positions[MAX_NUM_STOPS]; // normalized, 0 -> starting point, 1 -> ending point
uniform int _num_stops;

in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

vec4 mix_stop_colors(float t) {
	vec4 color = _stop_colors[0];

	for (int i = 1; i < _num_stops; i++)
		color = mix(color, _stop_colors[i], smoothstep(_stop_positions[i-1], _stop_positions[i], t));

	return color;
}

void main() {
	float t = 0.0;

	if (_func == LINEAR || _func == REPEATING_LINEAR)
	{
		float dist_square = dot(_v, _v);
		vec2 V = fragTexCoord - _p;
		t = dot(_v, V) / dist_square;
	}
	else if (_func == RADIAL || _func == REPEATING_RADIAL)
	{
		vec2 V = fragTexCoord - _p;
		t = length(_v * V);
	}
	else if (_func == CONIC || _func == REPEATING_CONIC)
	{
		mat2 R = mat2(_v.x, -_v.y, _v.y, _v.x);
		vec2 V = R * (fragTexCoord - _p);
		t = 0.5 + atan(-V.x, V.y) / (2.0 * PI);
	}

	if (_func == REPEATING_LINEAR || _func == REPEATING_RADIAL || _func == REPEATING_CONIC)
	{
		float t0 = _stop_positions[0];
		float t1 = _stop_positions[_num_stops - 1];
		t = t0 + mod(t - t0, t1 - t0);
	}

	finalColor = fragColor * mix_stop_colors(t);
}
)";

// "Creation" by Danilo Guanabara, based on: https://www.shadertoy.com/view/XsXXDn
static const char* shader_frag_creation = RMLUI_SHADER_HEADER R"(
uniform float _value;
uniform vec2 _dimensions;

in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

void main() {
	float t = _value;
	vec3 c;
	float l;
	for (int i = 0; i < 3; i++) {
		vec2 p = fragTexCoord;
		vec2 uv = p;
		p -= .5;
		p.x *= _dimensions.x / _dimensions.y;
		float z = t + float(i) * .07;
		l = length(p);
		uv += p / l * (sin(z) + 1.) * abs(sin(l * 9. - z - z));
		c[i] = .01 / length(mod(uv, 1.) - .5);
	}
	finalColor = vec4(c / l, fragColor.a);
}
)";

static const char* shader_vert_passthrough = RMLUI_SHADER_HEADER R"(
in vec2 inPosition;
in vec2 inTexCoord0;

out vec2 fragTexCoord;

void main() {
	fragTexCoord = inTexCoord0;
    gl_Position = vec4(inPosition, 0.0, 1.0);
}
)";

static const char* shader_frag_passthrough = RMLUI_SHADER_HEADER R"(
uniform sampler2D _tex;
in vec2 fragTexCoord;
out vec4 finalColor;

void main() {
	finalColor = texture(_tex, fragTexCoord);
}
)";

static const char* shader_frag_color_matrix = RMLUI_SHADER_HEADER R"(
uniform sampler2D _tex;
uniform mat4 _color_matrix;

in vec2 fragTexCoord;
out vec4 finalColor;

void main() {
	// The general case uses a 4x5 color matrix for full rgba transformation, plus a constant term with the last column.
	// However, we only consider the case of rgb transformations. Thus, we could in principle use a 3x4 matrix, but we
	// keep the alpha row for simplicity.
	// In the general case we should do the matrix transformation in non-premultiplied space. However, without alpha
	// transformations, we can do it directly in premultiplied space to avoid the extra division and multiplication
	// steps. In this space, the constant term needs to be multiplied by the alpha value, instead of unity.
	vec4 texColor = texture(_tex, fragTexCoord);
	vec3 transformedColor = vec3(_color_matrix * texColor);
	finalColor = vec4(transformedColor, texColor.a);
}
)";

static const char* shader_frag_blend_mask = RMLUI_SHADER_HEADER R"(
uniform sampler2D _tex;
uniform sampler2D _texMask;

in vec2 fragTexCoord;
out vec4 finalColor;

void main() {
	vec4 texColor = texture(_tex, fragTexCoord);
	float maskAlpha = texture(_texMask, fragTexCoord).a;
	finalColor = texColor * maskAlpha;
}
)";

#define RMLUI_SHADER_BLUR_HEADER \
    RMLUI_SHADER_HEADER "\n#define BLUR_SIZE " RMLUI_STRINGIFY(BLUR_SIZE) "\n#define BLUR_NUM_WEIGHTS " RMLUI_STRINGIFY(BLUR_NUM_WEIGHTS)

static const char* shader_vert_blur = RMLUI_SHADER_BLUR_HEADER R"(
uniform vec2 _texelOffset;

in vec3 inPosition;
in vec2 inTexCoord0;

out vec2 fragTexCoord[BLUR_SIZE];

void main() {
	for(int i = 0; i < BLUR_SIZE; i++)
		fragTexCoord[i] = inTexCoord0 - float(i - BLUR_NUM_WEIGHTS + 1) * _texelOffset;
    gl_Position = vec4(inPosition, 1.0);
}
)";

static const char* shader_frag_blur = RMLUI_SHADER_BLUR_HEADER R"(
uniform sampler2D _tex;
uniform float _weights[BLUR_NUM_WEIGHTS];
uniform vec2 _texCoordMin;
uniform vec2 _texCoordMax;

in vec2 fragTexCoord[BLUR_SIZE];
out vec4 finalColor;

void main() {
	vec4 color = vec4(0.0, 0.0, 0.0, 0.0);
	for(int i = 0; i < BLUR_SIZE; i++)
	{
		vec2 in_region = step(_texCoordMin, fragTexCoord[i]) * step(fragTexCoord[i], _texCoordMax);
		color += texture(_tex, fragTexCoord[i]) * in_region.x * in_region.y * _weights[abs(i - BLUR_NUM_WEIGHTS + 1)];
	}
	finalColor = color;
}
)";

static const char* shader_frag_drop_shadow = RMLUI_SHADER_HEADER R"(
uniform sampler2D _tex;
uniform vec2 _texCoordMin;
uniform vec2 _texCoordMax;
uniform vec4 _color;

in vec2 fragTexCoord;
out vec4 finalColor;

void main() {
	vec2 in_region = step(_texCoordMin, fragTexCoord) * step(fragTexCoord, _texCoordMax);
	finalColor = texture(_tex, fragTexCoord).a * in_region.x * in_region.y * _color;
}
)";

enum class ProgramId
{
	None = 0,
	Color,
	Texture,
	Gradient,
	Creation,
	Passthrough,
	ColorMatrix,
	BlendMask,
	Blur,
	DropShadow,
	Count,
};
enum class VertShaderId
{
	Main = 0,
	Passthrough,
	Blur,
	Count,
};
enum class FragShaderId
{
	Color = 0,
	Texture,
	Gradient,
	Creation,
	Passthrough,
	ColorMatrix,
	BlendMask,
	Blur,
	DropShadow,
	Count,
};

namespace Uniform
{
#define UniformStr(N, S) const char *const N = S
UniformStr(Translate, "_translate");
UniformStr(Transform, "_transform");
UniformStr(Tex, "_tex");
UniformStr(Color, "_color");
UniformStr(ColorMatrix, "_color_matrix");
UniformStr(TexelOffset, "_texelOffset");
UniformStr(TexCoordMin, "_texCoordMin");
UniformStr(TexCoordMax, "_texCoordMax");
UniformStr(TexMask, "_texMask");
UniformStr(Weights, "_weights");
UniformStr(Func, "_func");
UniformStr(P, "_p");
UniformStr(V, "_v");
UniformStr(StopColors, "_stop_colors");
UniformStr(StopPositions, "_stop_positions");
UniformStr(NumStops, "_num_stops");
UniformStr(Value, "_value");
UniformStr(Dimensions, "_dimensions");
#undef UniformStr
}

// RHI vertex layout for Rml::Vertex (position float2, colour ubyte4norm, texcoord float2)
static const RHI::VertexAttribute rmlVertexAttribs[] = {
	{ 0, static_cast<uint32_t>(offsetof(Rml::Vertex, position)),  RHI::VertexFormat::Float2,     0 }, // inPosition
	{ 1, static_cast<uint32_t>(offsetof(Rml::Vertex, colour)),    RHI::VertexFormat::UByte4Norm, 0 }, // inColor0
	{ 2, static_cast<uint32_t>(offsetof(Rml::Vertex, tex_coord)), RHI::VertexFormat::Float2,     0 }, // inTexCoord0
};
static const RHI::VertexLayout rmlVertexLayout = {
	rmlVertexAttribs, 3, sizeof(Rml::Vertex)
};

namespace Gfx
{

struct VertShaderDefinition
{
	VertShaderId id;
	const char* name_str;
	const char* code_str;
};
struct FragShaderDefinition
{
	FragShaderId id;
	const char* name_str;
	const char* code_str;
};
struct ProgramDefinition
{
	ProgramId id;
	const char* name_str;
	VertShaderId vert_shader;
	FragShaderId frag_shader;
};

// clang-format off
static const VertShaderDefinition vert_shader_definitions[] = {
	{VertShaderId::Main,        "main",        shader_vert_main},
	{VertShaderId::Passthrough, "passthrough", shader_vert_passthrough},
	{VertShaderId::Blur,        "blur",        shader_vert_blur},
};
static const FragShaderDefinition frag_shader_definitions[] = {
	{FragShaderId::Color,       "color",        shader_frag_color},
	{FragShaderId::Texture,     "texture",      shader_frag_texture},
	{FragShaderId::Gradient,    "gradient",     shader_frag_gradient},
	{FragShaderId::Creation,    "creation",     shader_frag_creation},
	{FragShaderId::Passthrough, "passthrough",  shader_frag_passthrough},
	{FragShaderId::ColorMatrix, "color_matrix", shader_frag_color_matrix},
	{FragShaderId::BlendMask,   "blend_mask",   shader_frag_blend_mask},
	{FragShaderId::Blur,        "blur",         shader_frag_blur},
	{FragShaderId::DropShadow,  "drop_shadow",  shader_frag_drop_shadow},
};
static const ProgramDefinition program_definitions[] = {
	{ProgramId::Color,       "color",        VertShaderId::Main,        FragShaderId::Color},
	{ProgramId::Texture,     "texture",      VertShaderId::Main,        FragShaderId::Texture},
	{ProgramId::Gradient,    "gradient",     VertShaderId::Main,        FragShaderId::Gradient},
	{ProgramId::Creation,    "creation",     VertShaderId::Main,        FragShaderId::Creation},
	{ProgramId::Passthrough, "passthrough",  VertShaderId::Passthrough, FragShaderId::Passthrough},
	{ProgramId::ColorMatrix, "color_matrix", VertShaderId::Passthrough, FragShaderId::ColorMatrix},
	{ProgramId::BlendMask,   "blend_mask",   VertShaderId::Passthrough, FragShaderId::BlendMask},
	{ProgramId::Blur,        "blur",         VertShaderId::Blur,        FragShaderId::Blur},
	{ProgramId::DropShadow,  "drop_shadow",  VertShaderId::Passthrough, FragShaderId::DropShadow},
};
// clang-format on

template<typename T, typename Enum>
class EnumArray
{
public:
	const T& operator[](Enum id) const
	{
		RMLUI_ASSERT((size_t) id < (size_t) Enum::Count)
		return ids[size_t(id)];
	}

	T& operator[](Enum id)
	{
		RMLUI_ASSERT((size_t) id < (size_t) Enum::Count)
		return ids[size_t(id)];
	}

	[[nodiscard]] auto begin() const
	{ return ids.begin(); }

	[[nodiscard]] auto end() const
	{ return ids.end(); }

private:
	Rml::Array<T, (size_t) Enum::Count> ids = {};
};

using Programs = EnumArray<Shader::IProgramObject*, ProgramId>;

struct ProgramData
{
	Programs programs;
};

struct CompiledGeometryData
{
	std::unique_ptr<RHI::IRHIBuffer> vbo;
	std::unique_ptr<RHI::IRHIBuffer> ibo;
	uint32_t num_indices = 0;
};

struct FramebufferData
{
	int width = 0, height = 0;
	std::unique_ptr<RHI::IRHIFramebuffer> fbo;
	std::unique_ptr<RHI::IRHITexture> colorTex;        // color texture (readable for non-MSAA)
	std::unique_ptr<RHI::IRHITexture> depthStencilTex; // owned depth-stencil (when not sharing)
	bool ownsDepthStencil = false;
	bool isMSAA = false;
};

enum class FramebufferAttachment
{
	None, Depth, DepthStencil
};

struct CheckGLToken {
	CheckGLToken(const CheckGLToken&) = delete;
	CheckGLToken(CheckGLToken&&) noexcept = delete;
	CheckGLToken& operator=(const CheckGLToken&) = delete;
	CheckGLToken& operator=(CheckGLToken&&) noexcept = delete;

	CheckGLToken(const char* operation_name)
		: opn{ operation_name }
	{
#ifdef RMLUI_DEBUG
		for (int count = 0; (glGetError() != GL_NO_ERROR) && (count < 10000); count++);
#endif
	}
	~CheckGLToken() {
#ifdef RMLUI_DEBUG
		GLenum error_code = glGetError();
		if (error_code != GL_NO_ERROR) {
			static const Rml::Pair<GLenum, const char*> error_names[] = { {GL_INVALID_ENUM,      "GL_INVALID_ENUM"},
																		 {GL_INVALID_VALUE,     "GL_INVALID_VALUE"},
																		 {GL_INVALID_OPERATION, "GL_INVALID_OPERATION"},
																		 {GL_OUT_OF_MEMORY,     "GL_OUT_OF_MEMORY"} };
			const char* error_str = "''";
			for (auto& err : error_names) {
				if (err.first == error_code) {
					error_str = err.second;
					break;
				}
			}
			Rml::Log::Message(Rml::Log::LT_ERROR, "OpenGL error during %s. Error code 0x%x (%s).", opn,
				error_code, error_str);
		}
#endif
	}
private:
	const char* opn = nullptr;
};

[[nodiscard]] static CheckGLToken CheckGLError(const char* operation_name)
{
	return CheckGLToken(operation_name);
}

static bool CreateFramebuffer(
	FramebufferData& out_fb, int width, int height,
	int samples, FramebufferAttachment attachment,
	RHI::IRHITexture* shared_depth_stencil
)
{
	auto* device = RHI::GetDevice();

	out_fb = {};
	out_fb.width = width;
	out_fb.height = height;
	out_fb.isMSAA = (samples > 0);

	out_fb.fbo = device->CreateFramebuffer();

	// Create color attachment: MSAA renderbuffer or regular texture.
	if (samples > 0) {
		auto msaaColorTex = device->CreateTexture(
			RHI::TextureType::Texture2DMS, RHI::TextureFormat::RGBA8,
			width, height, 1, 1, samples);
		out_fb.fbo->AttachColor(msaaColorTex.get(), 0);
		out_fb.colorTex = std::move(msaaColorTex);
	} else {
		auto colorTex = device->CreateTexture(
			RHI::TextureType::Texture2D, RHI::TextureFormat::RGBA8,
			width, height);
		colorTex->SetMinFilter(RHI::TextureFilter::Nearest);
		colorTex->SetMagFilter(RHI::TextureFilter::Nearest);
#ifdef RMLUI_PLATFORM_EMSCRIPTEN
		colorTex->SetWrapS(RHI::TextureWrap::ClampToEdge);
		colorTex->SetWrapT(RHI::TextureWrap::ClampToEdge);
#else
		colorTex->SetWrapS(RHI::TextureWrap::ClampToBorder);
		colorTex->SetWrapT(RHI::TextureWrap::ClampToBorder);
		colorTex->SetBorderColor(0.f, 0.f, 0.f, 0.f);
#endif
		out_fb.fbo->AttachColor(colorTex.get(), 0);
		out_fb.colorTex = std::move(colorTex);
	}

	// Create depth/stencil attachment.
	if (attachment != FramebufferAttachment::None) {
		if (shared_depth_stencil) {
			// Share existing depth-stencil texture.
			if (attachment == FramebufferAttachment::DepthStencil)
				out_fb.fbo->AttachDepthStencil(shared_depth_stencil);
			else
				out_fb.fbo->AttachDepth(shared_depth_stencil);
			out_fb.ownsDepthStencil = false;
		} else {
			// Create new depth-stencil texture.
			const auto dsFormat = (attachment == FramebufferAttachment::DepthStencil)
				? RHI::TextureFormat::Depth24Stencil8 : RHI::TextureFormat::Depth24;
			auto dsTex = device->CreateTexture(
				samples > 0 ? RHI::TextureType::Texture2DMS : RHI::TextureType::Texture2D,
				dsFormat, width, height, 1, 1, samples > 0 ? samples : 1);
			if (attachment == FramebufferAttachment::DepthStencil)
				out_fb.fbo->AttachDepthStencil(dsTex.get());
			else
				out_fb.fbo->AttachDepth(dsTex.get());
			out_fb.depthStencilTex = std::move(dsTex);
			out_fb.ownsDepthStencil = true;
		}
	}

	if (!out_fb.fbo->IsComplete()) {
		Rml::Log::Message(Rml::Log::LT_ERROR, "RHI framebuffer could not be completed.");
		return false;
	}

	return true;
}

static void DestroyFramebuffer(FramebufferData& fb)
{
	fb = {};
}

static void BindTexture(const FramebufferData& fb)
{
	RMLUI_ASSERTMSG(!fb.isMSAA && fb.colorTex,
		"Only non-MSAA framebuffers with color textures can be bound. MSAA needs a blit step first.")
	fb.colorTex->Bind(0);
}

static bool CreateShaders(ProgramData& data)
{
	RMLUI_ASSERT(std::ranges::all_of(data.programs, [](auto* ptr) { return ptr == nullptr; }))

#define sh shaderHandler
	for (const ProgramDefinition& def: program_definitions) {
		auto vert_def = vert_shader_definitions[(size_t) def.vert_shader];
		auto frag_def = frag_shader_definitions[(size_t) def.frag_shader];

		auto program = sh->CreateProgramObject("[Rml RenderInterface]", def.name_str);
		program->AttachShaderObject(sh->CreateShaderObject(vert_def.code_str, "", GL_VERTEX_SHADER));
		program->AttachShaderObject(sh->CreateShaderObject(frag_def.code_str, "", GL_FRAGMENT_SHADER));
		program->BindAttribLocation("inPosition", 0);
		program->BindAttribLocation("inColor0", 1);
		program->BindAttribLocation("inTexCoord0", 2);
		program->Link();

		if (!program->IsValid()) {
			const char* fmt = "RMLUI Shader '%s' (vert: '%s' frag: '%s') compilation error: %s";
			LOG_L(L_ERROR, fmt, program->GetName().c_str(), vert_def.name_str, frag_def.name_str,
				  program->GetLog().c_str());
			return false;
		}

		data.programs[def.id] = program;
#undef sh
	}

	auto blend_mask_prog = data.programs[ProgramId::BlendMask];
	blend_mask_prog->Enable();
	blend_mask_prog->SetUniform(Uniform::TexMask, 1);
	blend_mask_prog->Disable();

	return true;
}

} // namespace Gfx

RenderInterface_GL3_Recoil::RenderInterface_GL3_Recoil()
{
	auto mut_program_data = Rml::MakeUnique<Gfx::ProgramData>();
	if (Gfx::CreateShaders(*mut_program_data)) {
		program_data = std::move(mut_program_data);
		Rml::Mesh mesh;
		Rml::MeshUtilities::GenerateQuad(mesh, Rml::Vector2f(-1), Rml::Vector2f(2), {});
		fullscreen_quad_geometry = RenderInterface_GL3_Recoil::CompileGeometry(mesh.vertices, mesh.indices);
	}
}

RenderInterface_GL3_Recoil::~RenderInterface_GL3_Recoil()
{
	if (fullscreen_quad_geometry) {
		RenderInterface_GL3_Recoil::ReleaseGeometry(fullscreen_quad_geometry);
		fullscreen_quad_geometry = {};
	}

	if (program_data) {
		shaderHandler->ReleaseProgramObjects("[Rml RenderInterface]");
		program_data.reset();
	}
}

void RenderInterface_GL3_Recoil::SetViewport(int width, int height)
{
	viewport_width = Rml::Math::Max(width, 1);
	viewport_height = Rml::Math::Max(height, 1);
	projection = Rml::Matrix4f::ProjectOrtho(0, (float) viewport_width, (float) viewport_height, 0, -10000, 10000);
}

void RenderInterface_GL3_Recoil::BeginFrame()
{
	RMLUI_ASSERT(viewport_width >= 1 && viewport_height >= 1);
	auto tok = Gfx::CheckGLError("BeginFrame");

	// Setup expected state via RHI context.
	// The game rendering pipeline resets state at frame boundaries, so we don't
	// need to query and restore GL state. We just set what RmlUi needs.
	auto* rhiCtx = RHI::GetDevice()->GetContext();

	rhiCtx->SetViewport({0, 0, static_cast<float>(viewport_width), static_cast<float>(viewport_height), 0.f, 1.f});

	rhiCtx->ClearStencil(0);
	rhiCtx->ClearColor(0, 0, 0, 0);

	rhiCtx->SetScissorTestEnabled(false);
	rhiCtx->SetCullFaceEnabled(false);

	// Set blending function for premultiplied alpha.
	rhiCtx->SetBlendEnabled(true);
	rhiCtx->SetBlendEquation(RHI::BlendOp::Add);
	rhiCtx->SetBlendFunc(RHI::BlendFactor::One, RHI::BlendFactor::OneMinusSrcAlpha);

	// We do blending in nonlinear sRGB space because that is the common practice and gives results that we are used to.
	rhiCtx->SetFramebufferSRGBEnabled(false);

	rhiCtx->SetStencilTestEnabled(true);
	rhiCtx->SetStencilFunc(RHI::CompareFunc::Always, 1, 0xFFFFFFFF);
	rhiCtx->SetStencilMask(0xFFFFFFFF);
	rhiCtx->SetStencilOp(RHI::StencilOp::Keep, RHI::StencilOp::Keep, RHI::StencilOp::Keep);

	rhiCtx->SetDepthTestEnabled(false);
	rhiCtx->SetColorMask(true, true, true, true);

	SetTransform(nullptr);

	render_layers.BeginFrame(viewport_width, viewport_height);
	render_layers.GetTopLayer().fbo->Bind();
	rhiCtx->Clear(true, false, false);

	UseProgram(ProgramId::None);
	program_transform_dirty.set();
	scissor_state = Rml::Rectanglei::MakeInvalid();
}

void RenderInterface_GL3_Recoil::EndFrame()
{
	auto tok = Gfx::CheckGLError("EndFrame");

	auto* rhiCtx = RHI::GetDevice()->GetContext();

	const Gfx::FramebufferData& fb_active = render_layers.GetTopLayer();
	const Gfx::FramebufferData& fb_postprocess = render_layers.GetPostprocessPrimary();

	// Resolve MSAA to postprocess framebuffer.
	rhiCtx->BlitFramebuffer(
		fb_active.fbo.get(), fb_postprocess.fbo.get(),
		0, 0, fb_active.width, fb_active.height,
		0, 0, fb_postprocess.width, fb_postprocess.height,
		true, false, false);

	// Draw to backbuffer
	rhiCtx->BindDefaultFramebuffer();

	// Assuming we have an opaque background, we can just write to it with the premultiplied alpha blend mode and we'll get the correct result.
	Gfx::BindTexture(fb_postprocess);
	UseProgram(ProgramId::Passthrough);
	DrawFullscreenQuad();

	render_layers.EndFrame();

	UseProgram(ProgramId::None);

	// Restore state to engine defaults via RHI.
	// The game rendering pipeline resets state at the start of each frame, so
	// we only need to undo the most critical changes RmlUi made.
	rhiCtx->SetScissorTestEnabled(false);
	rhiCtx->SetStencilTestEnabled(false);
	rhiCtx->SetDepthTestEnabled(true);
	rhiCtx->SetCullFaceEnabled(true);
	rhiCtx->SetBlendEnabled(false);
	rhiCtx->SetColorMask(true, true, true, true);
	rhiCtx->SetFramebufferSRGBEnabled(true);
}

void RenderInterface_GL3_Recoil::Clear()
{
	auto* rhiCtx = RHI::GetDevice()->GetContext();
	rhiCtx->ClearColor(0, 0, 0, 1);
	rhiCtx->Clear(true, false, false);
}

Rml::CompiledGeometryHandle
RenderInterface_GL3_Recoil::CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices)
{
	auto* device = RHI::GetDevice();

	auto vbo = device->CreateBuffer(RHI::BufferType::Vertex, RHI::BufferUsage::Static,
		vertices.size() * sizeof(Rml::Vertex), vertices.data());
	auto ibo = device->CreateBuffer(RHI::BufferType::Index, RHI::BufferUsage::Static,
		indices.size() * sizeof(int), indices.data());

	return (Rml::CompiledGeometryHandle) new Gfx::CompiledGeometryData{
		std::move(vbo), std::move(ibo), static_cast<uint32_t>(indices.size())
	};
}

void RenderInterface_GL3_Recoil::RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation,
												Rml::TextureHandle texture)
{
	auto tok = Gfx::CheckGLError("RenderCompiledGeometry");

	auto* geometry = (Gfx::CompiledGeometryData*) handle;
	auto* rhiCtx = RHI::GetDevice()->GetContext();

	if (texture == TexturePostprocess) {
		// Do nothing.
	} else if (texture) {
		UseProgram(ProgramId::Texture);
		SubmitTransformUniform(translation);
		if (texture != TextureEnableWithoutBinding)
			reinterpret_cast<RHI::IRHITexture*>(texture)->Bind(0);
	} else {
		UseProgram(ProgramId::Color);
		SubmitTransformUniform(translation);
	}

	rhiCtx->BindVertexBuffer(geometry->vbo.get());
	rhiCtx->SetVertexLayout(rmlVertexLayout);
	rhiCtx->BindIndexBuffer(geometry->ibo.get(), RHI::IndexType::UInt32);
	rhiCtx->DrawIndexed(RHI::PrimitiveType::Triangles, geometry->num_indices);
	rhiCtx->ClearVertexLayout();

	if (texture != TexturePostprocess) {
		UseProgram(ProgramId::None);
	}
}

void RenderInterface_GL3_Recoil::ReleaseGeometry(Rml::CompiledGeometryHandle handle)
{
	delete (Gfx::CompiledGeometryData*) handle;
}

/// Flip vertical axis of the rectangle, and move its origin to the vertically opposite side of the viewport.
/// @note Changes coordinate system from RmlUi to OpenGL, or equivalently in reverse.
/// @note The Rectangle::Top and Rectangle::Bottom members will have reverse meaning in the returned rectangle.
static Rml::Rectanglei VerticallyFlipped(Rml::Rectanglei rect, int viewport_height)
{
	RMLUI_ASSERT(rect.Valid())
	Rml::Rectanglei flipped_rect = rect;
	flipped_rect.p0.y = viewport_height - rect.p1.y;
	flipped_rect.p1.y = viewport_height - rect.p0.y;
	return flipped_rect;
}

void RenderInterface_GL3_Recoil::SetScissor(Rml::Rectanglei region, bool vertically_flip)
{
	auto* rhiCtx = RHI::GetDevice()->GetContext();

	if (region.Valid() != scissor_state.Valid()) {
		rhiCtx->SetScissorTestEnabled(region.Valid());
	}

	if (region.Valid() && vertically_flip)
		region = VerticallyFlipped(region, viewport_height);

	if (region.Valid() && region != scissor_state) {
		// Some render APIs don't like offscreen positions (WebGL in particular), so clamp them to the viewport.
		const int x = Rml::Math::Clamp(region.Left(), 0, viewport_width);
		const int y = Rml::Math::Clamp(viewport_height - region.Bottom(), 0, viewport_height);

		rhiCtx->SetScissor({x, y, static_cast<uint32_t>(region.Width()), static_cast<uint32_t>(region.Height())});
	}

	scissor_state = region;
}

void RenderInterface_GL3_Recoil::EnableScissorRegion(bool enable)
{
	// Assume enable is immediately followed by a SetScissorRegion() call, and ignore it here.
	if (!enable)
		SetScissor(Rml::Rectanglei::MakeInvalid(), false);
}

void RenderInterface_GL3_Recoil::SetScissorRegion(Rml::Rectanglei region)
{
	SetScissor(region);
}

void RenderInterface_GL3_Recoil::EnableClipMask(bool enable)
{
	RHI::GetDevice()->GetContext()->SetStencilTestEnabled(enable);
}

void
RenderInterface_GL3_Recoil::RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry,
											 Rml::Vector2f translation)
{
	auto* rhiCtx = RHI::GetDevice()->GetContext();
	using Rml::ClipMaskOperation;

	// Track stencil ref locally instead of querying GL state.
	static int32_t stencil_test_value = 0;

	const bool clear_stencil = (operation == ClipMaskOperation::Set || operation == ClipMaskOperation::SetInverse);
	if (clear_stencil) {
		rhiCtx->Clear(false, false, true);
	}

	rhiCtx->SetColorMask(false, false, false, false);
	rhiCtx->SetStencilFunc(RHI::CompareFunc::Always, 1, 0xFFFFFFFF);

	switch (operation) {
		case ClipMaskOperation::Set: {
			rhiCtx->SetStencilOp(RHI::StencilOp::Keep, RHI::StencilOp::Keep, RHI::StencilOp::Replace);
			stencil_test_value = 1;
		}
			break;
		case ClipMaskOperation::SetInverse: {
			rhiCtx->SetStencilOp(RHI::StencilOp::Keep, RHI::StencilOp::Keep, RHI::StencilOp::Replace);
			stencil_test_value = 0;
		}
			break;
		case ClipMaskOperation::Intersect: {
			rhiCtx->SetStencilOp(RHI::StencilOp::Keep, RHI::StencilOp::Keep, RHI::StencilOp::IncrClamp);
			stencil_test_value += 1;
		}
			break;
	}

	RenderGeometry(geometry, translation, {});

	// Restore state
	rhiCtx->SetColorMask(true, true, true, true);
	rhiCtx->SetStencilOp(RHI::StencilOp::Keep, RHI::StencilOp::Keep, RHI::StencilOp::Keep);
	rhiCtx->SetStencilFunc(RHI::CompareFunc::Equal, stencil_test_value, 0xFFFFFFFF);
}

Rml::TextureHandle RenderInterface_GL3_Recoil::LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source)
{
	CBitmap bmp;
	if (!bmp.Load(source)) {
		return false;
	}
	texture_dimensions.x = bmp.xsize;
	texture_dimensions.y = bmp.ysize;
	auto rhiTex = bmp.CreateTextureRHI();
	if (!rhiTex)
		return false;
	return reinterpret_cast<Rml::TextureHandle>(rhiTex.release());
}

Rml::TextureHandle
RenderInterface_GL3_Recoil::GenerateTexture(Rml::Span<const Rml::byte> source_data, Rml::Vector2i source_dimensions)
{
	auto* device = RHI::GetDevice();
	auto tex = device->CreateTexture(
		RHI::TextureType::Texture2D, RHI::TextureFormat::RGBA8,
		source_dimensions.x, source_dimensions.y);
	if (!tex) {
		Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to generate RHI texture.");
		return false;
	}

	tex->Upload(0, 0, 0, source_dimensions.x, source_dimensions.y, source_data.data());
	tex->SetMinFilter(RHI::TextureFilter::Nearest);
	tex->SetMagFilter(RHI::TextureFilter::Nearest);
	tex->SetWrapS(RHI::TextureWrap::Repeat);
	tex->SetWrapT(RHI::TextureWrap::Repeat);

	return reinterpret_cast<Rml::TextureHandle>(tex.release());
}

void RenderInterface_GL3_Recoil::DrawFullscreenQuad()
{
	RenderGeometry(fullscreen_quad_geometry, {}, RenderInterface_GL3_Recoil::TexturePostprocess);
}

void RenderInterface_GL3_Recoil::DrawFullscreenQuad(Rml::Vector2f uv_offset, Rml::Vector2f uv_scaling)
{
	Rml::Mesh mesh;
	Rml::MeshUtilities::GenerateQuad(mesh, Rml::Vector2f(-1), Rml::Vector2f(2), {});
	if (uv_offset != Rml::Vector2f() || uv_scaling != Rml::Vector2f(1.f)) {
		for (Rml::Vertex& vertex: mesh.vertices)
			vertex.tex_coord = (vertex.tex_coord * uv_scaling) + uv_offset;
	}
	const Rml::CompiledGeometryHandle geometry = CompileGeometry(mesh.vertices, mesh.indices);
	RenderGeometry(geometry, {}, RenderInterface_GL3_Recoil::TexturePostprocess);
	ReleaseGeometry(geometry);
}

static Rml::Colourf ConvertToColorf(Rml::ColourbPremultiplied c0)
{
	Rml::Colourf result;
	for (int i = 0; i < 4; i++)
		result[i] = (1.f / 255.f) * float(c0[i]);
	return result;
}

static void SigmaToParameters(const float desired_sigma, int& out_pass_level, float& out_sigma)
{
	constexpr int max_num_passes = 10;
	static_assert(max_num_passes < 31);
	constexpr float max_single_pass_sigma = 3.0f;
	out_pass_level = Rml::Math::Clamp(Rml::Math::Log2(int(desired_sigma * (2.f / max_single_pass_sigma))), 0,
									  max_num_passes);
	out_sigma = Rml::Math::Clamp(desired_sigma / float(1 << out_pass_level), 0.0f, max_single_pass_sigma);
}

static void
SetTexCoordLimits(Shader::IProgramObject* program, Rml::Rectanglei rectangle_flipped, Rml::Vector2i framebuffer_size)
{
	// Offset by half-texel values so that texture lookups are clamped to fragment centers, thereby avoiding color
	// bleeding from neighboring texels due to bilinear interpolation.
	const Rml::Vector2f min =
		(Rml::Vector2f(rectangle_flipped.p0) + Rml::Vector2f(0.5f)) / Rml::Vector2f(framebuffer_size);
	const Rml::Vector2f max =
		(Rml::Vector2f(rectangle_flipped.p1) - Rml::Vector2f(0.5f)) / Rml::Vector2f(framebuffer_size);

	program->SetUniform(Uniform::TexCoordMin, min.x, min.y);
	program->SetUniform(Uniform::TexCoordMax, max.x, max.y);
}

static void SetBlurWeights(Shader::IProgramObject* program, float sigma)
{
	constexpr int num_weights = BLUR_NUM_WEIGHTS;
	float weights[num_weights];
	float normalization = 0.0f;
	for (int i = 0; i < num_weights; i++) {
		if (Rml::Math::Absolute(sigma) < 0.1f)
			weights[i] = float(i == 0);
		else
			weights[i] = Rml::Math::Exp(-float(i * i) / (2.0f * sigma * sigma)) /
						 (Rml::Math::SquareRoot(2.f * Rml::Math::RMLUI_PI) * sigma);

		normalization += (i == 0 ? 1.f : 2.0f) * weights[i];
	}

	for (float& weight: weights)
		weight /= normalization;

	program->SetUniform1v(Uniform::Weights, num_weights, weights);
}

void RenderInterface_GL3_Recoil::RenderBlur(float sigma, const Gfx::FramebufferData& source_destination,
											const Gfx::FramebufferData& temp,
											const Rml::Rectanglei window_flipped)
{
	RMLUI_ASSERT(&source_destination != &temp && source_destination.width == temp.width &&
				 source_destination.height == temp.height)
	RMLUI_ASSERT(window_flipped.Valid())

	auto tok = Gfx::CheckGLError("Blur");

	int pass_level = 0;
	SigmaToParameters(sigma, pass_level, sigma);

	const Rml::Rectanglei original_scissor = scissor_state;

	// Begin by downscaling so that the blur pass can be done at a reduced resolution for large sigma.
	Rml::Rectanglei scissor = window_flipped;

	UseProgram(ProgramId::Passthrough);
	SetScissor(scissor, true);

	auto* rhiCtx = RHI::GetDevice()->GetContext();

	// Downscale by iterative half-scaling with bilinear filtering, to reduce aliasing.
	rhiCtx->SetViewport({0, 0, static_cast<float>(source_destination.width / 2), static_cast<float>(source_destination.height / 2), 0.f, 1.f});

	// Scale UVs if we have even dimensions, such that texture fetches align perfectly between texels, thereby producing a 50% blend of
	// neighboring texels.
	const Rml::Vector2f uv_scaling = {
		(source_destination.width % 2 == 1) ? (1.f - 1.f / float(source_destination.width)) : 1.f,
		(source_destination.height % 2 == 1) ? (1.f - 1.f / float(source_destination.height)) : 1.f};

	for (int i = 0; i < pass_level; i++) {
		scissor.p0 = (scissor.p0 + Rml::Vector2i(1)) / 2;
		scissor.p1 = Rml::Math::Max(scissor.p1 / 2, scissor.p0);
		const bool from_source = (i % 2 == 0);
		Gfx::BindTexture(from_source ? source_destination : temp);
		(from_source ? temp : source_destination).fbo->Bind();
		SetScissor(scissor, true);

		DrawFullscreenQuad({}, uv_scaling);
	}

	rhiCtx->SetViewport({0, 0, static_cast<float>(source_destination.width), static_cast<float>(source_destination.height), 0.f, 1.f});

	// Ensure texture data end up in the temp buffer. Depending on the last downscaling, we might need to move it from the source_destination buffer.
	const bool transfer_to_temp_buffer = (pass_level % 2 == 0);
	if (transfer_to_temp_buffer) {
		Gfx::BindTexture(source_destination);
		temp.fbo->Bind();
		DrawFullscreenQuad();
	}

	// Set up uniforms.
	auto blur_prog = UseProgram(ProgramId::Blur);
	SetBlurWeights(blur_prog, sigma);
	SetTexCoordLimits(blur_prog, scissor, {source_destination.width, source_destination.height});

	auto SetTexelOffset = [&blur_prog](Rml::Vector2f blur_direction, int texture_dimension) {
		const Rml::Vector2f texel_offset = blur_direction * (1.0f / float(texture_dimension));
		blur_prog->SetUniform(Uniform::TexelOffset, texel_offset.x, texel_offset.y);
	};

	// Blur render pass - vertical.
	Gfx::BindTexture(temp);
	source_destination.fbo->Bind();

	SetTexelOffset({0.f, 1.f}, temp.height);
	DrawFullscreenQuad();

	// Blur render pass - horizontal.
	Gfx::BindTexture(source_destination);
	temp.fbo->Bind();

	// Add a 1px transparent border around the blur region by first clearing with a padded scissor. This helps prevent
	// artifacts when upscaling the blur result in the later step. On Intel and AMD, we have observed that during
	// blitting with linear filtering, pixels outside the 'src' region can be blended into the output. On the other
	// hand, it looks like Nvidia clamps the pixels to the source edge, which is what we really want. Regardless, we
	// work around the issue with this extra step.
	SetScissor(scissor.Extend(1), true);
	rhiCtx->Clear(true, false, false);
	SetScissor(scissor, true);

	SetTexelOffset({1.f, 0.f}, source_destination.width);
	DrawFullscreenQuad();

	// Blit the blurred image to the scissor region with upscaling.
	SetScissor(window_flipped, true);

	const Rml::Vector2i src_min = scissor.p0;
	const Rml::Vector2i src_max = scissor.p1;
	const Rml::Vector2i dst_min = window_flipped.p0;
	const Rml::Vector2i dst_max = window_flipped.p1;
	rhiCtx->BlitFramebuffer(
		temp.fbo.get(), source_destination.fbo.get(),
		src_min.x, src_min.y, src_max.x, src_max.y,
		dst_min.x, dst_min.y, dst_max.x, dst_max.y,
		true, false, true); // filterLinear=true for upscaling

	// The above upscale blit might be jittery at low resolutions (large pass levels). This is especially noticeable when moving an element with
	// backdrop blur around or when trying to click/hover an element within a blurred region since it may be rendered at an offset. For more stable
	// and accurate rendering we next upscale the blur image by an exact power-of-two. However, this may not fill the edges completely so we need to
	// do the above first. Note that this strategy may sometimes result in visible seams. Alternatively, we could try to enlarge the window to the
	// next power-of-two size and then downsample and blur that.
	const Rml::Vector2i target_min = src_min * (1 << pass_level);
	const Rml::Vector2i target_max = src_max * (1 << pass_level);
	if (target_min != dst_min || target_max != dst_max) {
		rhiCtx->BlitFramebuffer(
			temp.fbo.get(), source_destination.fbo.get(),
			src_min.x, src_min.y, src_max.x, src_max.y,
			target_min.x, target_min.y, target_max.x, target_max.y,
			true, false, true); // filterLinear=true for upscaling
	}

	// Restore render state.
	SetScissor(original_scissor);
}

void RenderInterface_GL3_Recoil::ReleaseTexture(Rml::TextureHandle texture_handle)
{
	delete reinterpret_cast<RHI::IRHITexture*>(texture_handle);
}

void RenderInterface_GL3_Recoil::SetTransform(const Rml::Matrix4f* new_transform)
{
	transform = (new_transform ? (projection * (*new_transform)) : projection);
	program_transform_dirty.set();
}

enum class FilterType
{
	Invalid = 0, Passthrough, Blur, DropShadow, ColorMatrix, MaskImage
};
struct CompiledFilter
{
	FilterType type;

	// Passthrough
	float blend_factor;

	// Blur
	float sigma;

	// Drop shadow
	Rml::Vector2f offset;
	Rml::ColourbPremultiplied color;

	// ColorMatrix
	Rml::Matrix4f color_matrix;
};

Rml::CompiledFilterHandle
RenderInterface_GL3_Recoil::CompileFilter(const Rml::String& name, const Rml::Dictionary& parameters)
{
	CompiledFilter filter = {};

	if (name == "opacity") {
		filter.type = FilterType::Passthrough;
		filter.blend_factor = Rml::Get(parameters, "value", 1.0f);
	} else if (name == "blur") {
		filter.type = FilterType::Blur;
		filter.sigma = Rml::Get(parameters, "sigma", 1.0f);
	} else if (name == "drop-shadow") {
		filter.type = FilterType::DropShadow;
		filter.sigma = Rml::Get(parameters, "sigma", 0.f);
		filter.color = Rml::Get(parameters, "color", Rml::Colourb()).ToPremultiplied();
		filter.offset = Rml::Get(parameters, "offset", Rml::Vector2f(0.f));
	} else if (name == "brightness") {
		filter.type = FilterType::ColorMatrix;
		const float value = Rml::Get(parameters, "value", 1.0f);
		filter.color_matrix = Rml::Matrix4f::Diag(value, value, value, 1.f);
	} else if (name == "contrast") {
		filter.type = FilterType::ColorMatrix;
		const float value = Rml::Get(parameters, "value", 1.0f);
		const float grayness = 0.5f - 0.5f * value;
		filter.color_matrix = Rml::Matrix4f::Diag(value, value, value, 1.f);
		filter.color_matrix.SetColumn(3, Rml::Vector4f(grayness, grayness, grayness, 1.f));
	} else if (name == "invert") {
		filter.type = FilterType::ColorMatrix;
		const float value = Rml::Math::Clamp(Rml::Get(parameters, "value", 1.0f), 0.f, 1.f);
		const float inverted = 1.f - 2.f * value;
		filter.color_matrix = Rml::Matrix4f::Diag(inverted, inverted, inverted, 1.f);
		filter.color_matrix.SetColumn(3, Rml::Vector4f(value, value, value, 1.f));
	} else if (name == "grayscale") {
		filter.type = FilterType::ColorMatrix;
		const float value = Rml::Get(parameters, "value", 1.0f);
		const float rev_value = 1.f - value;
		const Rml::Vector3f gray = value * Rml::Vector3f(0.2126f, 0.7152f, 0.0722f);
		// clang-format off
		filter.color_matrix = Rml::Matrix4f::FromRows(
			{gray.x + rev_value, gray.y, gray.z, 0.f},
			{gray.x, gray.y + rev_value, gray.z, 0.f},
			{gray.x, gray.y, gray.z + rev_value, 0.f},
			{0.f, 0.f, 0.f, 1.f}
		);
		// clang-format on
	} else if (name == "sepia") {
		filter.type = FilterType::ColorMatrix;
		const float value = Rml::Get(parameters, "value", 1.0f);
		const float rev_value = 1.f - value;
		const Rml::Vector3f r_mix = value * Rml::Vector3f(0.393f, 0.769f, 0.189f);
		const Rml::Vector3f g_mix = value * Rml::Vector3f(0.349f, 0.686f, 0.168f);
		const Rml::Vector3f b_mix = value * Rml::Vector3f(0.272f, 0.534f, 0.131f);
		// clang-format off
		filter.color_matrix = Rml::Matrix4f::FromRows(
			{r_mix.x + rev_value, r_mix.y, r_mix.z, 0.f},
			{g_mix.x, g_mix.y + rev_value, g_mix.z, 0.f},
			{b_mix.x, b_mix.y, b_mix.z + rev_value, 0.f},
			{0.f, 0.f, 0.f, 1.f}
		);
		// clang-format on
	} else if (name == "hue-rotate") {
		// Hue-rotation and saturation values based on: https://www.w3.org/TR/filter-effects-1/#attr-valuedef-type-huerotate
		filter.type = FilterType::ColorMatrix;
		const float value = Rml::Get(parameters, "value", 1.0f);
		const float s = Rml::Math::Sin(value);
		const float c = Rml::Math::Cos(value);
		// clang-format off
		filter.color_matrix = Rml::Matrix4f::FromRows(
			{0.213f + 0.787f * c - 0.213f * s, 0.715f - 0.715f * c - 0.715f * s, 0.072f - 0.072f * c + 0.928f * s, 0.f},
			{0.213f - 0.213f * c + 0.143f * s, 0.715f + 0.285f * c + 0.140f * s, 0.072f - 0.072f * c - 0.283f * s, 0.f},
			{0.213f - 0.213f * c - 0.787f * s, 0.715f - 0.715f * c + 0.715f * s, 0.072f + 0.928f * c + 0.072f * s, 0.f},
			{0.f, 0.f, 0.f, 1.f}
		);
		// clang-format on
	} else if (name == "saturate") {
		filter.type = FilterType::ColorMatrix;
		const float value = Rml::Get(parameters, "value", 1.0f);
		// clang-format off
		filter.color_matrix = Rml::Matrix4f::FromRows(
			{0.213f + 0.787f * value, 0.715f - 0.715f * value, 0.072f - 0.072f * value, 0.f},
			{0.213f - 0.213f * value, 0.715f + 0.285f * value, 0.072f - 0.072f * value, 0.f},
			{0.213f - 0.213f * value, 0.715f - 0.715f * value, 0.072f + 0.928f * value, 0.f},
			{0.f, 0.f, 0.f, 1.f}
		);
		// clang-format on
	}

	if (filter.type != FilterType::Invalid)
		return reinterpret_cast<Rml::CompiledFilterHandle>(new CompiledFilter(std::move(filter)));

	Rml::Log::Message(Rml::Log::LT_WARNING, "Unsupported filter type '%s'.", name.c_str());
	return {};
}

void RenderInterface_GL3_Recoil::ReleaseFilter(Rml::CompiledFilterHandle filter)
{
	delete reinterpret_cast<CompiledFilter*>(filter);
}

enum class CompiledShaderType
{
	Invalid = 0, Gradient, Creation
};
struct CompiledShader
{
	CompiledShaderType type;

	// Gradient
	ShaderGradientFunction gradient_function;
	Rml::Vector2f p;
	Rml::Vector2f v;
	Rml::Vector<float> stop_positions;
	Rml::Vector<Rml::Colourf> stop_colors;

	// Shader
	Rml::Vector2f dimensions;
};

Rml::CompiledShaderHandle
RenderInterface_GL3_Recoil::CompileShader(const Rml::String& name, const Rml::Dictionary& parameters)
{
	auto ApplyColorStopList = [](CompiledShader& shader, const Rml::Dictionary& shader_parameters) {
		auto it = shader_parameters.find("color_stop_list");
		RMLUI_ASSERT(it != shader_parameters.end() && it->second.GetType() == Rml::Variant::COLORSTOPLIST)
		const auto& color_stop_list = it->second.GetReference<Rml::ColorStopList>();
		const int num_stops = Rml::Math::Min((int) color_stop_list.size(), MAX_NUM_STOPS);

		shader.stop_positions.resize(num_stops);
		shader.stop_colors.resize(num_stops);
		for (int i = 0; i < num_stops; i++) {
			const Rml::ColorStop& stop = color_stop_list[i];
			RMLUI_ASSERT(stop.position.unit == Rml::Unit::NUMBER)
			shader.stop_positions[i] = stop.position.number;
			shader.stop_colors[i] = ConvertToColorf(stop.color);
		}
	};

	CompiledShader shader = {};

	if (name == "linear-gradient") {
		shader.type = CompiledShaderType::Gradient;
		const bool repeating = Rml::Get(parameters, "repeating", false);
		shader.gradient_function = (
			repeating ? ShaderGradientFunction::RepeatingLinear : ShaderGradientFunction::Linear
		);
		shader.p = Rml::Get(parameters, "p0", Rml::Vector2f(0.f));
		shader.v = Rml::Get(parameters, "p1", Rml::Vector2f(0.f)) - shader.p;
		ApplyColorStopList(shader, parameters);
	} else if (name == "radial-gradient") {
		shader.type = CompiledShaderType::Gradient;
		const bool repeating = Rml::Get(parameters, "repeating", false);
		shader.gradient_function = (
			repeating ? ShaderGradientFunction::RepeatingRadial : ShaderGradientFunction::Radial
		);
		shader.p = Rml::Get(parameters, "center", Rml::Vector2f(0.f));
		shader.v = Rml::Vector2f(1.f) / Rml::Get(parameters, "radius", Rml::Vector2f(1.f));
		ApplyColorStopList(shader, parameters);
	} else if (name == "conic-gradient") {
		shader.type = CompiledShaderType::Gradient;
		const bool repeating = Rml::Get(parameters, "repeating", false);
		shader.gradient_function = (repeating ? ShaderGradientFunction::RepeatingConic : ShaderGradientFunction::Conic);
		shader.p = Rml::Get(parameters, "center", Rml::Vector2f(0.f));
		const float angle = Rml::Get(parameters, "angle", 0.f);
		shader.v = {Rml::Math::Cos(angle), Rml::Math::Sin(angle)};
		ApplyColorStopList(shader, parameters);
	} else if (name == "shader") {
		const Rml::String value = Rml::Get(parameters, "value", Rml::String());
		if (value == "creation") {
			shader.type = CompiledShaderType::Creation;
			shader.dimensions = Rml::Get(parameters, "dimensions", Rml::Vector2f(0.f));
		}
	}

	if (shader.type != CompiledShaderType::Invalid)
		return reinterpret_cast<Rml::CompiledShaderHandle>(new CompiledShader(std::move(shader)));

	Rml::Log::Message(Rml::Log::LT_WARNING, "Unsupported shader type '%s'.", name.c_str());
	return {};
}

void RenderInterface_GL3_Recoil::RenderShader(Rml::CompiledShaderHandle shader_handle,
											  Rml::CompiledGeometryHandle geometry_handle,
											  Rml::Vector2f translation, Rml::TextureHandle /*texture*/)
{
	RMLUI_ASSERT(shader_handle && geometry_handle)

	auto tok = Gfx::CheckGLError("RenderShader");
	const CompiledShader& shader = *reinterpret_cast<CompiledShader*>(shader_handle);
	const CompiledShaderType type = shader.type;
	const auto* geometry = (Gfx::CompiledGeometryData*) geometry_handle;

	auto* rhiCtx = RHI::GetDevice()->GetContext();

	switch (type) {
		case CompiledShaderType::Gradient: {
			RMLUI_ASSERT(shader.stop_positions.size() == shader.stop_colors.size())
			const int num_stops = (int) shader.stop_positions.size();

			auto gradient_prog = UseProgram(ProgramId::Gradient);
			gradient_prog->SetUniform(Uniform::Func, static_cast<int>(shader.gradient_function));
			gradient_prog->SetUniform(Uniform::P, shader.p.x, shader.p.y);
			gradient_prog->SetUniform(Uniform::V, shader.v.x, shader.v.y);
			gradient_prog->SetUniform(Uniform::NumStops, num_stops);
			gradient_prog->SetUniform1v(Uniform::StopPositions, num_stops, shader.stop_positions.data());
			gradient_prog->SetUniform4v(Uniform::StopColors, num_stops, (float*) &shader.stop_colors[0]);

			SubmitTransformUniform(translation);
			rhiCtx->BindVertexBuffer(geometry->vbo.get());
			rhiCtx->SetVertexLayout(rmlVertexLayout);
			rhiCtx->BindIndexBuffer(geometry->ibo.get(), RHI::IndexType::UInt32);
			rhiCtx->DrawIndexed(RHI::PrimitiveType::Triangles, geometry->num_indices);
			rhiCtx->ClearVertexLayout();
		}
			break;
		case CompiledShaderType::Creation: {
			const double time = Rml::GetSystemInterface()->GetElapsedTime();

			auto creation_prog = UseProgram(ProgramId::Creation);
			creation_prog->SetUniform(Uniform::Value, (float) time);
			creation_prog->SetUniform(Uniform::Dimensions, shader.dimensions.x, shader.dimensions.y);

			SubmitTransformUniform(translation);
			rhiCtx->BindVertexBuffer(geometry->vbo.get());
			rhiCtx->SetVertexLayout(rmlVertexLayout);
			rhiCtx->BindIndexBuffer(geometry->ibo.get(), RHI::IndexType::UInt32);
			rhiCtx->DrawIndexed(RHI::PrimitiveType::Triangles, geometry->num_indices);
			rhiCtx->ClearVertexLayout();
		}
			break;
		case CompiledShaderType::Invalid: {
			Rml::Log::Message(Rml::Log::LT_WARNING, "Unhandled render shader %d.", (int) type);
		}
			break;
	}
}

void RenderInterface_GL3_Recoil::ReleaseShader(Rml::CompiledShaderHandle shader_handle)
{
	delete reinterpret_cast<CompiledShader*>(shader_handle);
}

void RenderInterface_GL3_Recoil::BlitLayerToPostprocessPrimary(Rml::LayerHandle layer_handle)
{
	auto* rhiCtx = RHI::GetDevice()->GetContext();
	const Gfx::FramebufferData& source = render_layers.GetLayer(layer_handle);
	const Gfx::FramebufferData& destination = render_layers.GetPostprocessPrimary();

	// Blit and resolve MSAA. Any active scissor state will restrict the size of the blit region.
	rhiCtx->BlitFramebuffer(
		source.fbo.get(), destination.fbo.get(),
		0, 0, source.width, source.height,
		0, 0, destination.width, destination.height,
		true, false, false);
}

void RenderInterface_GL3_Recoil::RenderFilters(Rml::Span<const Rml::CompiledFilterHandle> filter_handles)
{
	auto tok = Gfx::CheckGLError("RenderFilter");
	auto* rhiCtx = RHI::GetDevice()->GetContext();

	for (const Rml::CompiledFilterHandle filter_handle: filter_handles) {
		const CompiledFilter& filter = *reinterpret_cast<const CompiledFilter*>(filter_handle);
		const FilterType type = filter.type;

		switch (type) {
			case FilterType::Passthrough: {
				UseProgram(ProgramId::Passthrough);
				rhiCtx->SetBlendFunc(RHI::BlendFactor::ConstantColor, RHI::BlendFactor::Zero);
				rhiCtx->SetBlendColor(0.0f, 0.0f, 0.0f, filter.blend_factor);

				const Gfx::FramebufferData& source = render_layers.GetPostprocessPrimary();
				const Gfx::FramebufferData& destination = render_layers.GetPostprocessSecondary();
				Gfx::BindTexture(source);
				destination.fbo->Bind();

				DrawFullscreenQuad();

				render_layers.SwapPostprocessPrimarySecondary();
				rhiCtx->SetBlendFunc(RHI::BlendFactor::One, RHI::BlendFactor::OneMinusSrcAlpha);
			}
				break;
			case FilterType::Blur: {
				rhiCtx->SetBlendEnabled(false);

				const Gfx::FramebufferData& source_destination = render_layers.GetPostprocessPrimary();
				const Gfx::FramebufferData& temp = render_layers.GetPostprocessSecondary();

				const Rml::Rectanglei window_flipped = VerticallyFlipped(scissor_state, viewport_height);
				RenderBlur(filter.sigma, source_destination, temp, window_flipped);

				rhiCtx->SetBlendEnabled(true);
			}
				break;
			case FilterType::DropShadow: {
				auto drop_shadow_prog = UseProgram(ProgramId::DropShadow);
				rhiCtx->SetBlendEnabled(false);

				Rml::Colourf color = ConvertToColorf(filter.color);
				drop_shadow_prog->SetUniform4v(Uniform::Color, &color[0]);

				const Gfx::FramebufferData& primary = render_layers.GetPostprocessPrimary();
				const Gfx::FramebufferData& secondary = render_layers.GetPostprocessSecondary();
				Gfx::BindTexture(primary);
				secondary.fbo->Bind();

				const Rml::Rectanglei window_flipped = VerticallyFlipped(scissor_state, viewport_height);
				SetTexCoordLimits(drop_shadow_prog, window_flipped, {primary.width, primary.height});

				const Rml::Vector2f uv_offset =
					filter.offset / Rml::Vector2f(-(float) viewport_width, (float) viewport_height);
				DrawFullscreenQuad(uv_offset);

				if (filter.sigma >= 0.5f) {
					const Gfx::FramebufferData& tertiary = render_layers.GetPostprocessTertiary();
					RenderBlur(filter.sigma, secondary, tertiary, window_flipped);
				}

				UseProgram(ProgramId::Passthrough);
				BindTexture(primary);
				rhiCtx->SetBlendEnabled(true);
				DrawFullscreenQuad();

				render_layers.SwapPostprocessPrimarySecondary();
			}
				break;
			case FilterType::ColorMatrix: {
				auto color_matrix_prog = UseProgram(ProgramId::ColorMatrix);
				rhiCtx->SetBlendEnabled(false);

				constexpr bool transpose = std::is_same<decltype(filter.color_matrix), Rml::RowMajorMatrix4f>::value;
				color_matrix_prog->SetUniformMatrix4x4(Uniform::ColorMatrix, transpose, filter.color_matrix.data());

				const Gfx::FramebufferData& source = render_layers.GetPostprocessPrimary();
				const Gfx::FramebufferData& destination = render_layers.GetPostprocessSecondary();
				Gfx::BindTexture(source);
				destination.fbo->Bind();

				DrawFullscreenQuad();

				render_layers.SwapPostprocessPrimarySecondary();
				rhiCtx->SetBlendEnabled(true);
			}
				break;
			case FilterType::MaskImage: {
				UseProgram(ProgramId::BlendMask);
				rhiCtx->SetBlendEnabled(false);

				const Gfx::FramebufferData& source = render_layers.GetPostprocessPrimary();
				const Gfx::FramebufferData& blend_mask = render_layers.GetBlendMask();
				const Gfx::FramebufferData& destination = render_layers.GetPostprocessSecondary();

				Gfx::BindTexture(source);
				blend_mask.colorTex->Bind(1);

				destination.fbo->Bind();

				DrawFullscreenQuad();

				render_layers.SwapPostprocessPrimarySecondary();
				rhiCtx->SetBlendEnabled(true);
			}
				break;
			case FilterType::Invalid: {
				Rml::Log::Message(Rml::Log::LT_WARNING, "Unhandled render filter %d.", (int) type);
			}
				break;
		}
	}
}

Rml::LayerHandle RenderInterface_GL3_Recoil::PushLayer()
{
	const Rml::LayerHandle layer_handle = render_layers.PushLayer();

	auto* rhiCtx = RHI::GetDevice()->GetContext();
	render_layers.GetLayer(layer_handle).fbo->Bind();
	rhiCtx->Clear(true, false, false);

	return layer_handle;
}

void RenderInterface_GL3_Recoil::CompositeLayers(Rml::LayerHandle source_handle, Rml::LayerHandle destination_handle,
												 Rml::BlendMode blend_mode,
												 Rml::Span<const Rml::CompiledFilterHandle> filters)
{
	using Rml::BlendMode;

	auto tok = Gfx::CheckGLError("CompositeLayers");

	// Blit source layer to postprocessing buffer. Do this regardless of whether we actually have any filters to be
	// applied, because we need to resolve the multi-sampled framebuffer in any case.
	// @performance If we have BlendMode::Replace and no filters or mask then we can just blit directly to the destination.
	BlitLayerToPostprocessPrimary(source_handle);

	// Render the filters, the PostprocessPrimary framebuffer is used for both input and output.
	RenderFilters(filters);

	// Render to the destination layer.
	auto* rhiCtx = RHI::GetDevice()->GetContext();
	render_layers.GetLayer(destination_handle).fbo->Bind();
	Gfx::BindTexture(render_layers.GetPostprocessPrimary());

	UseProgram(ProgramId::Passthrough);

	if (blend_mode == BlendMode::Replace)
		rhiCtx->SetBlendEnabled(false);

	DrawFullscreenQuad();

	if (blend_mode == BlendMode::Replace)
		rhiCtx->SetBlendEnabled(true);

	if (destination_handle != render_layers.GetTopLayerHandle())
		render_layers.GetTopLayer().fbo->Bind();
}

void RenderInterface_GL3_Recoil::PopLayer()
{
	render_layers.PopLayer();
	render_layers.GetTopLayer().fbo->Bind();
}

Rml::TextureHandle RenderInterface_GL3_Recoil::SaveLayerAsTexture()
{
	auto tok = Gfx::CheckGLError("SaveLayerAsTexture");

	RMLUI_ASSERT(scissor_state.Valid());
	const Rml::Rectanglei bounds = scissor_state;

	Rml::TextureHandle render_texture = GenerateTexture({}, bounds.Size());
	if (!render_texture)
		return {};

	auto* device = RHI::GetDevice();
	auto* rhiCtx = device->GetContext();

	BlitLayerToPostprocessPrimary(render_layers.GetTopLayerHandle());

	EnableScissorRegion(false);

	const Gfx::FramebufferData& source = render_layers.GetPostprocessPrimary();
	const Gfx::FramebufferData& destination = render_layers.GetPostprocessSecondary();

	// Flip the image vertically, as that convention is used for textures, and move to origin.
	rhiCtx->BlitFramebuffer(
		source.fbo.get(), destination.fbo.get(),
		bounds.Left(), source.height - bounds.Bottom(), // src0
		bounds.Right(), source.height - bounds.Top(),   // src1
		0, bounds.Height(),                             // dst0
		bounds.Width(), 0,                              // dst1
		true, false, false);

	// Blit from the destination postprocess FBO into a temporary FBO with the
	// render_texture attached, replacing the glCopyTexSubImage2D approach.
	auto* rhiTex = reinterpret_cast<RHI::IRHITexture*>(render_texture);
	auto tempFbo = device->CreateFramebuffer();
	tempFbo->AttachColor(rhiTex, 0);

	rhiCtx->BlitFramebuffer(
		destination.fbo.get(), tempFbo.get(),
		0, 0, bounds.Width(), bounds.Height(),
		0, 0, bounds.Width(), bounds.Height(),
		true, false, false);

	SetScissor(bounds);
	render_layers.GetTopLayer().fbo->Bind();

	return render_texture;
}

Rml::CompiledFilterHandle RenderInterface_GL3_Recoil::SaveLayerAsMaskImage()
{
	auto tok = Gfx::CheckGLError("SaveLayerAsMaskImage");

	auto* rhiCtx = RHI::GetDevice()->GetContext();

	BlitLayerToPostprocessPrimary(render_layers.GetTopLayerHandle());

	const Gfx::FramebufferData& source = render_layers.GetPostprocessPrimary();
	const Gfx::FramebufferData& destination = render_layers.GetBlendMask();

	destination.fbo->Bind();
	BindTexture(source);
	UseProgram(ProgramId::Passthrough);
	rhiCtx->SetBlendEnabled(false);

	DrawFullscreenQuad();

	rhiCtx->SetBlendEnabled(true);
	render_layers.GetTopLayer().fbo->Bind();

	CompiledFilter filter = {};
	filter.type = FilterType::MaskImage;
	return reinterpret_cast<Rml::CompiledFilterHandle>(new CompiledFilter(std::move(filter)));
}

Shader::IProgramObject* RenderInterface_GL3_Recoil::UseProgram(ProgramId program_id)
{
	RMLUI_ASSERT(program_data)
	if (active_program_id != program_id) {
		if (active_program_id != ProgramId::None)
			program_data->programs[active_program_id]->Disable();
		if (program_id != ProgramId::None)
			program_data->programs[program_id]->Enable();
		active_program_id = program_id;
	}

	return program_data->programs[active_program_id];
}

void RenderInterface_GL3_Recoil::SubmitTransformUniform(Rml::Vector2f translation)
{
	static_assert((size_t) ProgramId::Count < MaxNumPrograms, "Maximum number of programs exceeded.");
	auto tok = Gfx::CheckGLError("SubmitTransformUniform");

	const auto program_index = (size_t) active_program_id;
	auto program = program_data->programs[active_program_id];

	if (program_transform_dirty.test(program_index)) {
		program->SetUniformMatrix4x4(Uniform::Transform, false, transform.data());
		program_transform_dirty.set(program_index, false);
	}

	program->SetUniform(Uniform::Translate, translation.x, translation.y);
}

RenderInterface_GL3_Recoil::operator bool() const
{
	if (!program_data)
		return false;

	bool result = true;
	for (auto i = 1; i < (int) ProgramId::Count; i++) {
		auto prog = program_data->programs[(ProgramId) i];
		result = result && prog != nullptr && prog->IsValid();
	}
	return result;
}

RenderInterface_GL3_Recoil::RenderLayerStack::RenderLayerStack()
{
	fb_postprocess.resize(4);
}

RenderInterface_GL3_Recoil::RenderLayerStack::~RenderLayerStack()
{
	DestroyFramebuffers();
}

Rml::LayerHandle RenderInterface_GL3_Recoil::RenderLayerStack::PushLayer()
{
	RMLUI_ASSERT(layers_size <= (int) fb_layers.size())

	if (layers_size == (int) fb_layers.size()) {
		// All framebuffers should share a single depth-stencil texture.
		RHI::IRHITexture* shared_ds = sharedDepthStencil.get();

		fb_layers.push_back(Gfx::FramebufferData{});
		Gfx::CreateFramebuffer(fb_layers.back(), width, height, NUM_MSAA_SAMPLES,
							   Gfx::FramebufferAttachment::DepthStencil, shared_ds);

		// First layer creates the depth-stencil; store it for sharing with subsequent layers.
		if (!sharedDepthStencil && fb_layers.back().depthStencilTex) {
			sharedDepthStencil = std::move(fb_layers.back().depthStencilTex);
			fb_layers.back().ownsDepthStencil = false;
		}
	}

	layers_size += 1;
	return GetTopLayerHandle();
}

void RenderInterface_GL3_Recoil::RenderLayerStack::PopLayer()
{
	RMLUI_ASSERT(layers_size > 0)
	layers_size -= 1;
}

const Gfx::FramebufferData& RenderInterface_GL3_Recoil::RenderLayerStack::GetLayer(Rml::LayerHandle layer) const
{
	RMLUI_ASSERT((size_t) layer < (size_t) layers_size)
	return fb_layers[layer];
}

const Gfx::FramebufferData& RenderInterface_GL3_Recoil::RenderLayerStack::GetTopLayer() const
{
	return GetLayer(GetTopLayerHandle());
}

Rml::LayerHandle RenderInterface_GL3_Recoil::RenderLayerStack::GetTopLayerHandle() const
{
	RMLUI_ASSERT(layers_size > 0)
	return static_cast<Rml::LayerHandle>(layers_size - 1);
}

void RenderInterface_GL3_Recoil::RenderLayerStack::SwapPostprocessPrimarySecondary()
{
	std::swap(fb_postprocess[0], fb_postprocess[1]);
}

void RenderInterface_GL3_Recoil::RenderLayerStack::BeginFrame(int new_width, int new_height)
{
	RMLUI_ASSERT(layers_size == 0)

	if (new_width != width || new_height != height) {
		width = new_width;
		height = new_height;

		DestroyFramebuffers();
	}

	PushLayer();
}

void RenderInterface_GL3_Recoil::RenderLayerStack::EndFrame()
{
	RMLUI_ASSERT(layers_size == 1)
	PopLayer();
}

void RenderInterface_GL3_Recoil::RenderLayerStack::DestroyFramebuffers()
{
	RMLUI_ASSERTMSG(layers_size == 0,
					"Do not call this during frame rendering, that is, between BeginFrame() and EndFrame().")

	for (Gfx::FramebufferData& fb: fb_layers)
		Gfx::DestroyFramebuffer(fb);

	fb_layers.clear();
	sharedDepthStencil.reset();

	for (Gfx::FramebufferData& fb: fb_postprocess)
		Gfx::DestroyFramebuffer(fb);
}

const Gfx::FramebufferData& RenderInterface_GL3_Recoil::RenderLayerStack::EnsureFramebufferPostprocess(int index)
{
	RMLUI_ASSERT(index < (int) fb_postprocess.size())
	Gfx::FramebufferData& fb = fb_postprocess[index];
	if (!fb.fbo)
		Gfx::CreateFramebuffer(fb, width, height, 0, Gfx::FramebufferAttachment::None, nullptr);
	return fb;
}
