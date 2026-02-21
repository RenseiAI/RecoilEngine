/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

/**
 * GL Extra Drawing Utilities
 *
 * RHI Migration Status: PARTIALLY MIGRATED
 * -----------------------------------------
 * glDrawVolume: FULLY MIGRATED to RHI dynamic state context
 *   (depth, cull, color mask, stencil test/func/op/mask, depth clamp)
 *
 * GL::Shapes vertex attribs: MIGRATED to RHI SetVertexLayout/ClearVertexLayout
 * GL::Shapes buffers+draw: MIGRATED to dual-path IRHIBuffer + RHI DrawIndexed (Phase 6.2)
 *
 * glSurfaceCircle/glBallisticCircle: FULLY MIGRATED to TypedRenderBuffer
 *   (including Lua variants)
 */

#include "glExtra.h"
#include "RenderBuffers.h"
#include "Rendering/RHI/RHIFactory.h"
#include "Rendering/RHI/RHIContext.h"
#include "Rendering/RHI/RHIDevice.h"

#include "Map/Ground.h"
#include "Game/Camera.h"
#include "Sim/Weapons/Weapon.h"
#include "Sim/Weapons/WeaponDef.h"
#include "System/SpringMath.h"
#include "System/Threading/ThreadPool.h"

#include "System/Misc/TracyDefs.h"


/**
 *  Draws a trigonometric circle in 'resolution' steps.
 */
namespace {
	template<typename Func>
	void glSurfaceCircleImpl(const float3& center, float radius, const SColor& col, uint32_t res, Func&& func)
	{
		for (uint32_t i = 0; i < res; ++i) {
			const float radians = math::TWOPI * (float)i / (float)res;
			float3 pos;
			pos.x = center.x + (fastmath::sin(radians) * radius);
			pos.z = center.z + (fastmath::cos(radians) * radius);
			pos.y = CGround::GetHeightAboveWater(pos.x, pos.z, false) + 5.0f;
			func(std::forward<float3>(pos), col);
		}
	}
}
void glSurfaceCircle(const float3& center, float radius, const SColor& col, uint32_t res)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const float4 fColor = col;
	if (fColor.a == 0.0f)
		return;

	auto& rb = RenderBuffer::GetTypedRenderBuffer<VA_TYPE_0>();
	rb.AssertSubmission();
	auto& sh = rb.GetShader();

	const auto addFunc = [&rb](auto&& pos, const auto& col) {
		rb.AddVertex({ std::forward<float3>(pos) });
	};

	glSurfaceCircleImpl(center, radius, col, res, addFunc);

	sh.Enable();
	sh.SetUniform("ucolor", fColor.x, fColor.y, fColor.z, fColor.w);
	rb.DrawArrays(GL_LINE_LOOP);
	sh.SetUniform("ucolor", 1.0f, 1.0f, 1.0f, 1.0f);
	sh.Disable();
}

void glSurfaceCircleLua(const float3& center, float radius, const SColor& col, uint32_t res)
{
	RECOIL_DETAILED_TRACY_ZONE;
	auto& rb = RenderBuffer::GetTypedRenderBuffer<VA_TYPE_C>();
	rb.AssertSubmission();
	auto& sh = rb.GetShader();

	const auto addFunc = [&rb](auto&& pos, const auto& col) {
		rb.AddVertex({ std::forward<float3>(pos), col });
	};

	glSurfaceCircleImpl(center, radius, col, res, addFunc);

	sh.Enable();
	rb.DrawArrays(GL_LINE_LOOP);
	sh.Disable();
}

static constexpr float (*weaponRangeFuncs[])(const CWeapon*, const WeaponDef*, float, float) = {
	CWeapon::GetStaticRange2D,
	CWeapon::GetLiveRange2D,
};

namespace {
	std::vector<VA_TYPE_0> glBallisticCircleImpl(const CWeapon* weapon, const WeaponDef* weaponDef, uint32_t resolution, const float3& center, const float3& params)
	{
		static constexpr int resDiv = 50;

		std::vector<VA_TYPE_0> vertices;
		vertices.resize(resolution);

		const float radius = params.x;
		const float slope = params.y;

		const float wdHeightMod = weaponDef->heightmod;
		const float wdProjGravity = mix(params.z, -weaponDef->myGravity, weaponDef->myGravity != 0.0f);

		for_mt(0, resolution, [&](const int i) {
			const float radians = math::TWOPI * (float)i / (float)resolution;

			const float sinR = fastmath::sin(radians);
			const float cosR = fastmath::cos(radians);

			float maxWeaponRange = radius;

			float3 pos;
			pos.x = center.x + (sinR * maxWeaponRange);
			pos.z = center.z + (cosR * maxWeaponRange);
			pos.y = CGround::GetHeightAboveWater(pos.x, pos.z, false);

			float posHeightDelta = (pos.y - center.y) * 0.5f;
			float posWeaponRange = weaponRangeFuncs[weapon != nullptr](weapon, weaponDef, posHeightDelta* wdHeightMod, wdProjGravity);
			float rangeIncrement = (maxWeaponRange -= (posHeightDelta * slope)) * 0.5f;
			float ydiff = 0.0f;

			// "binary search" for the maximum positional range per angle, accounting for terrain height
			for (int j = 0; j < resDiv && (std::fabs(posWeaponRange - maxWeaponRange) + ydiff) >(0.01f * maxWeaponRange); j++) {
				if (posWeaponRange > maxWeaponRange) {
					maxWeaponRange += rangeIncrement;
				}
				else {
					// overshot, reduce step-size
					maxWeaponRange -= rangeIncrement;
					rangeIncrement *= 0.5f;
				}

				pos.x = center.x + (sinR * maxWeaponRange);
				pos.z = center.z + (cosR * maxWeaponRange);

				const float newY = CGround::GetHeightAboveWater(pos.x, pos.z, false);
				ydiff = std::fabs(pos.y - newY);
				pos.y = newY;

				posHeightDelta = pos.y - center.y;
				posWeaponRange = weaponRangeFuncs[weapon != nullptr](weapon, weaponDef, posHeightDelta* wdHeightMod, wdProjGravity);
			}

			pos.x = center.x + (sinR * posWeaponRange);
			pos.z = center.z + (cosR * posWeaponRange);
			pos.y = CGround::GetHeightAboveWater(pos.x, pos.z, false) + 5.0f;

			vertices[i].pos = std::move(pos);
		});

		return vertices;
	}
}

/*
 *  Draws a trigonometric circle in 'resolution' steps, with a slope modifier
 */

void glBallisticCircle(const CWeapon* weapon, const WeaponDef* weaponDef, const SColor& color, uint32_t resolution, const float3& center, const float3& params)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const float4 fColor = color;
	if (fColor.a == 0.0f)
		return;

	auto& rb = RenderBuffer::GetTypedRenderBuffer<VA_TYPE_0>();
	rb.AssertSubmission();


	auto vertices = glBallisticCircleImpl(weapon, weaponDef, resolution, center, params);
	rb.AddVertices(vertices);

	auto& sh = rb.GetShader();
	sh.Enable();
	sh.SetUniform("ucolor", fColor.x, fColor.y, fColor.z, fColor.w);
	rb.DrawArrays(GL_LINE_LOOP);
	sh.SetUniform("ucolor", 1.0f, 1.0f, 1.0f, 1.0f);
	sh.Disable();
}

void glBallisticCircleLua(const CWeapon* weapon, const WeaponDef* weaponDef, const SColor& color, uint32_t resolution, const float3& center, const float3& params)
{
	RECOIL_DETAILED_TRACY_ZONE;
	auto& rb = RenderBuffer::GetTypedRenderBuffer<VA_TYPE_C>();
	rb.AssertSubmission();
	auto& sh = rb.GetShader();

	auto vertices = glBallisticCircleImpl(weapon, weaponDef, resolution, center, params);

	for (auto&& vert : vertices) {
		rb.AddVertex({ std::forward<float3>(vert.pos), color });
	}

	sh.Enable();
	rb.DrawArrays(GL_LINE_LOOP);
	sh.Disable();
}

void glBallisticCircle(const CWeapon* weapon     , const SColor& color, uint32_t resolution, const float3& center, const float3& params)
{
	RECOIL_DETAILED_TRACY_ZONE;
	glBallisticCircle(weapon, weapon->weaponDef, color, resolution, center, params);
}

void glBallisticCircle(const WeaponDef* weaponDef, const SColor& color, uint32_t resolution, const float3& center, const float3& params)
{
	RECOIL_DETAILED_TRACY_ZONE;
	glBallisticCircle(nullptr, weaponDef, color, resolution, center, params);
}

void glBallisticCircleLua(const CWeapon* weapon, const SColor& color, uint32_t resolution, const float3& center, const float3& params)
{
	RECOIL_DETAILED_TRACY_ZONE;
	glBallisticCircleLua(weapon, weapon->weaponDef, color, resolution, center, params);
}

void glBallisticCircleLua(const WeaponDef* weaponDef, const SColor& color, uint32_t resolution, const float3& center, const float3& params)
{
	RECOIL_DETAILED_TRACY_ZONE;
	glBallisticCircleLua(nullptr, weaponDef, color, resolution, center, params);
}

/******************************************************************************/

void glDrawVolume(DrawVolumeFunc drawFunc, const void* data)
{
	auto* ctx = RHI::GetDevice()->GetContext();

	ctx->SetDepthWriteEnabled(false);
	ctx->SetCullFaceEnabled(false);
	ctx->SetDepthTestEnabled(true);
	ctx->SetDepthClampEnabled(true);

	ctx->SetColorMask(false, false, false, false);

		ctx->SetStencilTestEnabled(true);
		ctx->SetStencilMask(0x1);
		ctx->SetStencilFunc(RHI::CompareFunc::Always, 0, 0x1);
		ctx->SetStencilOp(RHI::StencilOp::Keep, RHI::StencilOp::IncrClamp, RHI::StencilOp::Keep);
		drawFunc(data); // draw

	ctx->SetDepthTestEnabled(false);

	ctx->SetStencilFunc(RHI::CompareFunc::NotEqual, 0, 0x1);
	ctx->SetStencilOp(RHI::StencilOp::Zero, RHI::StencilOp::Zero, RHI::StencilOp::Zero); // clear as we go

	ctx->SetColorMask(true, true, true, true);

	ctx->SetCullFaceEnabled(true);
	ctx->SetCullFace(RHI::CullMode::Front);

	drawFunc(data);   // draw

	ctx->SetDepthClampEnabled(false);
	ctx->SetStencilTestEnabled(false);
	ctx->SetCullFaceEnabled(false);
	ctx->SetDepthTestEnabled(true);
}

/******************************************************************************/

void GL::Shapes::Init()
{
	assert(!shader);

	assert(solidSpheresMap.empty());
	assert(wireSpheresMap.empty());
	assert(wireCylindersMap.empty());
	assert(wireBoxIdx == size_t(-1));

	assert(allObjects.empty());
}

void GL::Shapes::Kill()
{
	solidSpheresMap.clear();
	wireSpheresMap.clear();
	wireCylindersMap.clear();
	wireBoxIdx = size_t(-1);

	allObjects.clear();

	shaderHandler->ReleaseProgramObjects("[GL::Shapes]");
	shader = nullptr;
}

Shader::IProgramObject* GL::Shapes::GetShader()
{
	if unlikely(!shader) {
		shader = shaderHandler->CreateProgramObject("[GL::Shapes]", "Default");
		shader->AttachShaderObject(shaderHandler->CreateShaderObject("GLSL/ShapesVertProg.glsl", "", GL_VERTEX_SHADER));
		shader->AttachShaderObject(shaderHandler->CreateShaderObject("GLSL/ShapesFragProg.glsl", "", GL_FRAGMENT_SHADER));
		shader->BindAttribLocation("vertexPos", 0);
		shader->Link();

		shader->Enable();
		shader->SetUniform("meshColor", 0.0f, 0.0f, 0.0f, 0.0f);
		shader->SetUniformMatrix4x4("viewProjMat", false, CMatrix44f::Identity().m);
		shader->SetUniformMatrix4x4("worldMat", false, CMatrix44f::Identity().m);
		shader->Disable();

		shader->Validate();
	}
	return shader;
}

void GL::Shapes::DrawSolidSphere(uint32_t numRows, uint32_t numCols, const CMatrix44f& m, const float* color)
{
#ifndef HEADLESS
	auto shToken = FillShaderUniforms(m, color);
	DrawSolidSphere(numRows, numCols);
#endif
}

void GL::Shapes::DrawWireSphere(uint32_t numRows, uint32_t numCols, const CMatrix44f& m, const float* color)
{
#ifndef HEADLESS
	auto shToken = FillShaderUniforms(m, color);
	DrawWireSphere(numRows, numCols);
#endif
}

void GL::Shapes::DrawWireCylinder(uint32_t numDivs, const CMatrix44f& m, const float* color)
{
#ifndef HEADLESS
	auto shToken = FillShaderUniforms(m, color);
	DrawWireCylinder(numDivs);
#endif
}

void GL::Shapes::DrawWireBox(const CMatrix44f& m, const float* color)
{
#ifndef HEADLESS
	auto shToken = FillShaderUniforms(m, color);
	DrawWireBox();
#endif
}

void GL::Shapes::DrawSolidSphere(uint32_t numRows, uint32_t numCols)
{
#ifndef HEADLESS
	auto it = solidSpheresMap.find(std::make_tuple(numRows, numCols));
	if (it == solidSpheresMap.end())
		it = CreateSolidSphere(numRows, numCols);

	const auto& [vao, vertVBO, indxVBO, rhiVertBuf, rhiIndxBuf] = allObjects[it->second];
	const uint32_t indexCount = static_cast<uint32_t>(indxVBO.GetSize() / sizeof(uint32_t));

	vao.Bind();
	auto* device = RHI::GetDevice();
	if (device) {
		auto* ctx = device->GetContext();
		if (rhiVertBuf) ctx->BindVertexBuffer(rhiVertBuf.get(), 0);
		if (rhiIndxBuf) ctx->BindIndexBuffer(rhiIndxBuf.get(), RHI::IndexType::UInt32);
		ctx->DrawIndexed(RHI::PrimitiveType::Triangles, indexCount, 0, 0);
	} else {
		glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount), GL_UNSIGNED_INT, nullptr);
	}
	vao.Unbind();
#endif // !HEADLESS
}

void GL::Shapes::DrawWireSphere(uint32_t numRows, uint32_t numCols)
{
#ifndef HEADLESS
	auto it = wireSpheresMap.find(std::make_tuple(numRows, numCols));
	if (it == wireSpheresMap.end())
		it = CreateWireSphere(numRows, numCols);

	const auto& [vao, vertVBO, indxVBO, rhiVertBuf, rhiIndxBuf] = allObjects[it->second];
	const uint32_t indexCount = static_cast<uint32_t>(indxVBO.GetSize() / sizeof(uint32_t));

	vao.Bind();
	auto* device = RHI::GetDevice();
	if (device) {
		auto* ctx = device->GetContext();
		if (rhiVertBuf) ctx->BindVertexBuffer(rhiVertBuf.get(), 0);
		if (rhiIndxBuf) ctx->BindIndexBuffer(rhiIndxBuf.get(), RHI::IndexType::UInt32);
		ctx->DrawIndexed(RHI::PrimitiveType::Lines, indexCount, 0, 0);
	} else {
		glDrawElements(GL_LINES, static_cast<GLsizei>(indexCount), GL_UNSIGNED_INT, nullptr);
	}
	vao.Unbind();
#endif // !HEADLESS
}

void GL::Shapes::DrawWireCylinder(uint32_t numDivs)
{
#ifndef HEADLESS
	auto it = wireCylindersMap.find(numDivs);
	if (it == wireCylindersMap.end())
		it = CreateWireCylinder(numDivs);

	const auto& [vao, vertVBO, indxVBO, rhiVertBuf, rhiIndxBuf] = allObjects[it->second];
	const uint32_t indexCount = static_cast<uint32_t>(indxVBO.GetSize() / sizeof(uint32_t));

	vao.Bind();
	auto* device = RHI::GetDevice();
	if (device) {
		auto* ctx = device->GetContext();
		if (rhiVertBuf) ctx->BindVertexBuffer(rhiVertBuf.get(), 0);
		if (rhiIndxBuf) ctx->BindIndexBuffer(rhiIndxBuf.get(), RHI::IndexType::UInt32);
		ctx->DrawIndexed(RHI::PrimitiveType::Lines, indexCount, 0, 0);
	} else {
		glDrawElements(GL_LINES, static_cast<GLsizei>(indexCount), GL_UNSIGNED_INT, nullptr);
	}
	vao.Unbind();
#endif // !HEADLESS
}

void GL::Shapes::DrawWireBox()
{
#ifndef HEADLESS
	if (wireBoxIdx == size_t(-1)) {
		const std::vector<float3> BOX_VERTS = {
			// bottom-face, ccw
			float3{-0.5f, -0.5f,  0.5f},
			float3{ 0.5f, -0.5f,  0.5f},
			float3{ 0.5f, -0.5f, -0.5f},
			float3{-0.5f, -0.5f, -0.5f},
			// top-face, ccw
			float3{-0.5f,  0.5f,  0.5f},
			float3{ 0.5f,  0.5f,  0.5f},
			float3{ 0.5f,  0.5f, -0.5f},
			float3{-0.5f,  0.5f, -0.5f}
		};

		const std::vector<uint32_t> BOX_INDCS = {
			0, 1, 1, 2, 2, 3, 3, 0, // bottom
			4, 5, 5, 6, 6, 7, 7, 4, // top
			0, 1, 1, 5, 5, 4, 4, 0, // front
			1, 2, 2, 6, 6, 5, 5, 1, // right
			2, 3, 3, 7, 7, 6, 6, 2, // back
			3, 0, 0, 4, 4, 7, 7, 3  // left
		};

		wireBoxIdx = CreateGLObjects(BOX_VERTS, BOX_INDCS);
	}

	const auto& [vao, vertVBO, indxVBO, rhiVertBuf, rhiIndxBuf] = allObjects[wireBoxIdx];
	const uint32_t indexCount = static_cast<uint32_t>(indxVBO.GetSize() / sizeof(uint32_t));

	vao.Bind();
	auto* device = RHI::GetDevice();
	if (device) {
		auto* ctx = device->GetContext();
		if (rhiVertBuf) ctx->BindVertexBuffer(rhiVertBuf.get(), 0);
		if (rhiIndxBuf) ctx->BindIndexBuffer(rhiIndxBuf.get(), RHI::IndexType::UInt32);
		ctx->DrawIndexed(RHI::PrimitiveType::Lines, indexCount, 0, 0);
	} else {
		glDrawElements(GL_LINES, static_cast<GLsizei>(indexCount), GL_UNSIGNED_INT, nullptr);
	}
	vao.Unbind();
#endif // !HEADLESS
}

Shader::ShaderEnabledToken GL::Shapes::FillShaderUniforms(const CMatrix44f& m, const float* color) const
{
	auto* shader = GL::shapes.GetShader();
	auto shToken = shader->EnableScoped();

	shader->SetUniform4v("meshColor", color);
	shader->SetUniformMatrix4x4("viewProjMat", false, camera->GetViewProjectionMatrix().m);
	shader->SetUniformMatrix4x4("worldMat", false, m.m);

	return shToken;
}

void GL::Shapes::EnableAttribs()
{
	auto* device = RHI::GetDevice();
	if (!device) {
		glEnableVertexAttribArray(0);
		glVertexAttribDivisor(0, 0);
		glVertexAttribPointer(0, 3, GL_FLOAT, false, sizeof(float3), 0);
		return;
	}
	static const RHI::VertexAttribute shapeAttrib[] = {
		{0, 0, RHI::VertexFormat::Float3, 0},
	};
	static const RHI::VertexLayout shapeLayout = {shapeAttrib, 1, sizeof(float3)};
	device->GetContext()->SetVertexLayout(shapeLayout);
}

void GL::Shapes::DisableAttribs()
{
	auto* device = RHI::GetDevice();
	if (!device) {
		glDisableVertexAttribArray(0);
		glVertexAttribDivisor(0, 0);
		return;
	}
	device->GetContext()->ClearVertexLayout();
}

size_t GL::Shapes::CreateGLObjects(
	const std::vector<float3>& verts,
	const std::vector<uint32_t>& indcs
) {
	auto& [vao, vertVBO, indxVBO, rhiVertBuf, rhiIndxBuf] = allObjects.emplace_back(
		VAO{ },
		VBO{ GL_ARRAY_BUFFER, false },
		VBO{ GL_ELEMENT_ARRAY_BUFFER, false },
		std::unique_ptr<RHI::IRHIBuffer>{},
		std::unique_ptr<RHI::IRHIBuffer>{}
	);

	vao.Bind();

	vertVBO.Bind();
	vertVBO.New(verts, GL_STATIC_DRAW);
	indxVBO.Bind();
	indxVBO.New(indcs, GL_STATIC_DRAW);

	EnableAttribs();

	vao.Unbind();

	vertVBO.Unbind();
	indxVBO.Unbind();

	DisableAttribs();

	// RHI path: create buffers for Metal
	if (auto* device = RHI::GetDevice()) {
		const size_t vertSz = verts.size() * sizeof(float3);
		const size_t indxSz = indcs.size() * sizeof(uint32_t);
		rhiVertBuf = device->CreateBuffer(RHI::BufferType::Vertex, RHI::BufferUsage::Static, vertSz);
		rhiVertBuf->Upload(verts.data(), 0, vertSz);
		rhiIndxBuf = device->CreateBuffer(RHI::BufferType::Index, RHI::BufferUsage::Static, indxSz);
		rhiIndxBuf->Upload(indcs.data(), 0, indxSz);
	}

	return allObjects.size() - 1;
}

auto GL::Shapes::CreateSolidSphere(uint32_t numRows, uint32_t numCols) -> decltype(solidSpheresMap)::iterator
{
	std::vector<float3  > verts; verts.resize ((numRows + 1) * numCols);
	std::vector<uint32_t> indcs; indcs.reserve((numCols + 1) * 3 * 2 + (numRows - 2) * numCols * 6);

	for (uint32_t row = 0; row <= numRows; row++) {
		for (uint32_t col = 0; col < numCols; col++) {
			const float a = (col * (math::TWOPI / numCols));
			const float b = (row * (math::PI    / numRows));

			float3& v = verts[row * numCols + col];

			v.x = std::cos(a) * std::sin(b);
			v.y = std::sin(a) * std::sin(b);
			v.z = std::cos(b);
		}
	}

	// top slice
	for (uint32_t col = 0; col <= numCols; col++) {
		const auto i = 1 * numCols + ((col + 0) % numCols);
		const auto j = 1 * numCols + ((col + 1) % numCols);

		indcs.push_back(0);
		indcs.push_back(i);
		indcs.push_back(j);
	}

	// bottom slice
	for (uint32_t col = 0; col <= numCols; col++) {
		const auto i = ((numRows - 1) * numCols) + ((col + 0) % numCols);
		const auto j = ((numRows - 1) * numCols) + ((col + 1) % numCols);

		indcs.push_back(static_cast<uint32_t>(verts.size() - 1));
		indcs.push_back(i);
		indcs.push_back(j);
	}

	// middle slices
	for (uint32_t row = 1; row < (numRows - 1); row++) {
		for (uint32_t col = 0; col < numCols; col++) {
			const auto i0 = (row + 0) * numCols + (col + 0);
			const auto i1 = (row + 1) * numCols + (col + 0);
			const auto i2 = (row + 0) * numCols + (col + 1) % numCols;
			const auto i3 = (row + 1) * numCols + (col + 1) % numCols;

			indcs.push_back(i0);
			indcs.push_back(i1);
			indcs.push_back(i2);

			indcs.push_back(i2);
			indcs.push_back(i3);
			indcs.push_back(i1);
		}
	}

	return solidSpheresMap.emplace(
		std::make_tuple(numRows, numCols),
		CreateGLObjects(verts, indcs)
	).first;
}

auto GL::Shapes::CreateWireSphere(uint32_t numRows, uint32_t numCols) -> decltype(wireSpheresMap)::iterator
{
	std::vector<float3  > verts; verts.resize((numRows + 1)* numCols);
	std::vector<uint32_t> indcs; indcs.reserve((numCols + 1) * 6 * 2 + (numRows - 2) * numCols * 8);

	for (uint32_t row = 0; row <= numRows; row++) {
		for (uint32_t col = 0; col < numCols; col++) {
			const float a = (col * (math::TWOPI / numCols));
			const float b = (row * (math::PI / numRows));

			float3& v = verts[row * numCols + col];

			v.x = std::cos(a) * std::sin(b);
			v.y = std::sin(a) * std::sin(b);
			v.z = std::cos(b);
		}
	}

	// top slice
	for (uint32_t col = 0; col <= numCols; col++) {
		const auto i = 1 * numCols + ((col + 0) % numCols);
		const auto j = 1 * numCols + ((col + 1) % numCols);

		indcs.push_back(0);
		indcs.push_back(i);

		indcs.push_back(i);
		indcs.push_back(j);

		indcs.push_back(j);
		indcs.push_back(0);
	}

	// bottom slice
	for (uint32_t col = 0; col <= numCols; col++) {
		const auto i = ((numRows - 1) * numCols) + ((col + 0) % numCols);
		const auto j = ((numRows - 1) * numCols) + ((col + 1) % numCols);

		indcs.push_back(static_cast<uint32_t>(verts.size() - 1));
		indcs.push_back(i);

		indcs.push_back(i);
		indcs.push_back(j);

		indcs.push_back(j);
		indcs.push_back(static_cast<uint32_t>(verts.size() - 1));
	}

	// middle slices
	for (uint32_t row = 1; row < (numRows - 1); row++) {
		for (uint32_t col = 0; col < numCols; col++) {
			const auto i0 = (row + 0) * numCols + (col + 0);
			const auto i1 = (row + 1) * numCols + (col + 0);
			const auto i2 = (row + 0) * numCols + (col + 1) % numCols;
			const auto i3 = (row + 1) * numCols + (col + 1) % numCols;

			indcs.push_back(i0);
			indcs.push_back(i1);

			indcs.push_back(i1);
			indcs.push_back(i3);

			indcs.push_back(i3);
			indcs.push_back(i2);

			indcs.push_back(i2);
			indcs.push_back(i0);
		}
	}

	return wireSpheresMap.emplace(
		std::make_tuple(numRows, numCols),
		CreateGLObjects(verts, indcs)
	).first;
}

auto GL::Shapes::CreateWireCylinder(uint32_t numDivs) -> decltype(wireCylindersMap)::iterator
{
	std::vector<float3  > verts; verts.resize(2 + numDivs * 2);
	std::vector<uint32_t> indcs; indcs.reserve(numDivs * 2);

	// front end-cap
	verts[0] = float3{ 0.0f, 0.0f, 0.0f };
	for (unsigned int n = 0; n < numDivs; n++) {
		const unsigned int i = 2 + (n + 0) % numDivs;
		const unsigned int j = 2 + (n + 1) % numDivs;

		verts[i].x = std::cos(i * (math::TWOPI / numDivs));
		verts[i].y = std::sin(i * (math::TWOPI / numDivs));
		verts[i].z = 0.0f;

		indcs.push_back(0);
		indcs.push_back(i);

		indcs.push_back(i);
		indcs.push_back(j);

		indcs.push_back(j);
		indcs.push_back(0);
	}

	// back end-cap
	verts[1] = float3{ 0.0f, 0.0f, 1.0f };
	for (unsigned int n = 0; n < numDivs; n++) {
		const unsigned int i = 2 + (n + 0) % numDivs;
		const unsigned int j = 2 + (n + 1) % numDivs;

		verts[i + numDivs].x = verts[i].x;
		verts[i + numDivs].y = verts[i].y;
		verts[i + numDivs].z = 1.0f;

		indcs.push_back(1);
		indcs.push_back(i + numDivs);

		indcs.push_back(i + numDivs);
		indcs.push_back(j + numDivs);

		indcs.push_back(j + numDivs);
		indcs.push_back(1);
	}

	// sides
	for (unsigned int n = 0; n < numDivs; n++) {
		const unsigned int i0 = 2 + (n + 0);
		const unsigned int i1 = 2 + (n + 0) + numDivs;
		const unsigned int i2 = 2 + (n + 1) % numDivs;
		const unsigned int i3 = 2 + (n + 1) % numDivs + numDivs;

		indcs.push_back(i0);
		indcs.push_back(i1);

		indcs.push_back(i1);
		indcs.push_back(i3);

		indcs.push_back(i3);
		indcs.push_back(i2);

		indcs.push_back(i2);
		indcs.push_back(i0);
	}

	return wireCylindersMap.emplace(
		numDivs,
		CreateGLObjects(verts, indcs)
	).first;
}


namespace GL {
	Shapes shapes;
}
