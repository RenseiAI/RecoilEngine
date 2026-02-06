/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

/**
 * RHI Migration Status: COMPLETE
 *
 * This file has been fully migrated to the RHI abstraction layer.
 *
 * Migrated patterns:
 *   - FBO creation/management -> IRHIDevice::CreateFramebuffer()
 *   - Texture creation        -> IRHIDevice::CreateTexture()
 *   - Texture parameters      -> IRHITexture::Set{Min,Mag}Filter(), SetWrap{S,T}()
 *   - Depth texture compare   -> IRHITexture::SetCompareMode()
 *   - Framebuffer blit        -> IRHIContext::BlitFramebuffer()
 *   - FBO attachment          -> IRHIFramebuffer::AttachDepth()
 *
 * No direct OpenGL calls remain in this file.
 */

#include "DepthBufferCopy.h"

#include <array>
#include <cassert>

#include "System/EventHandler.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/RHI/RHIDevice.h"
#include "Rendering/RHI/RHIContext.h"
#include "Rendering/RHI/RHIFactory.h"

namespace {
	RHI::TextureFormat DepthBitsToRHIFormat(int bits) {
		switch (bits) {
			case 16: return RHI::TextureFormat::Depth16;
			case 24: return RHI::TextureFormat::Depth24;
			case 32: return RHI::TextureFormat::Depth32F;
			default: return RHI::TextureFormat::Depth24;
		}
	}
}

std::unique_ptr<DepthBufferCopy> depthBufferCopy = nullptr;

DepthBufferCopy::DepthBufferCopy()
	: CEventClient("[DepthBufferCopy]", 012345, false)
{
	eventHandler.AddClient(this);
	ViewResize();
}

DepthBufferCopy::~DepthBufferCopy()
{
	assert(consumersCount[false] == 0);
	assert(consumersCount[true ] == 0);

	eventHandler.RemoveClient(this);
	autoLinkedEvents.clear();
}

void DepthBufferCopy::Init()
{
	depthBufferCopy = std::make_unique<DepthBufferCopy>();
}

void DepthBufferCopy::Kill()
{
	depthBufferCopy = nullptr;
}

void DepthBufferCopy::AddConsumer(bool ms)
{
	if (consumersCount[ms] == 0)
		CreateTextureAndFBO(ms);

	consumersCount[ms]++;
}

void DepthBufferCopy::DelConsumer(bool ms)
{
	consumersCount[ms]--;

	if (consumersCount[ms] == 0)
		DestroyTextureAndFBO(ms);
}

void DepthBufferCopy::ViewResize()
{
	for (size_t ms = 0; ms < depthFBOs.size(); ++ms) {
		if (consumersCount[ms] == 0)
			continue;

		RecreateTextureAndFBO(static_cast<bool>(ms));
	}
}

bool DepthBufferCopy::IsValid(bool ms) const {
	const auto& depthFBO = depthFBOs[ms];
	return depthFBO && depthFBO->IsComplete() && depthTextures[ms];
}

void DepthBufferCopy::MakeDepthBufferCopy() const
{
	auto* ctx = RHI::GetDevice()->GetContext();

	const std::array<int, 4> srcScreenRect = { globalRendering->viewPosX, globalRendering->viewPosY, globalRendering->viewPosX + globalRendering->viewSizeX, globalRendering->viewPosY + globalRendering->viewSizeY };
	const std::array<int, 4> dstScreenRect = { 0, 0, globalRendering->viewSizeX, globalRendering->viewSizeY };

	if (consumersCount[true ] > 0)
		ctx->BlitFramebuffer(
			nullptr, depthFBOs[true].get(),
			srcScreenRect[0], srcScreenRect[1], srcScreenRect[2], srcScreenRect[3],
			dstScreenRect[0], dstScreenRect[1], dstScreenRect[2], dstScreenRect[3],
			false, true);

	if (consumersCount[false] > 0) {
		RHI::IRHIFramebuffer* srcFBO = depthFBOs[true] ? depthFBOs[true].get() : nullptr;
		ctx->BlitFramebuffer(
			srcFBO, depthFBOs[false].get(),
			srcScreenRect[0], srcScreenRect[1], srcScreenRect[2], srcScreenRect[3],
			dstScreenRect[0], dstScreenRect[1], dstScreenRect[2], dstScreenRect[3],
			false, true);
	}
}

void DepthBufferCopy::DestroyTextureAndFBO(bool ms)
{
	auto& depthFBO = depthFBOs[ms];
	auto& depthTexture = depthTextures[ms];

	assert(depthFBO);
	if (depthFBO) {
		if (depthFBO->IsComplete()) {
			depthFBO->Bind();
			depthFBO->DetachAll();
			depthFBO->Unbind();
		}
		depthFBO = nullptr;
	}

	assert(depthTexture);
	depthTexture = nullptr;
}

void DepthBufferCopy::CreateTextureAndFBO(bool ms)
{
	auto* device = RHI::GetDevice();
	auto& depthTexture = depthTextures[ms];
	auto& depthFBO     = depthFBOs[ms];

	const auto texType = ms ? RHI::TextureType::Texture2DMS : RHI::TextureType::Texture2D;
	const auto depthFormat = DepthBitsToRHIFormat(device->GetDepthBufferBitDepth());

	assert(!depthTexture);
	depthTexture = device->CreateTexture(
		texType,
		depthFormat,
		globalRendering->viewSizeX,
		globalRendering->viewSizeY);

	if (!ms) {
		depthTexture->SetMagFilter(RHI::TextureFilter::Nearest);
		depthTexture->SetMinFilter(RHI::TextureFilter::Nearest);
		depthTexture->SetWrapS(RHI::TextureWrap::ClampToEdge);
		depthTexture->SetWrapT(RHI::TextureWrap::ClampToEdge);
		depthTexture->SetCompareMode(false);
	}

	assert(depthFBO == nullptr);
	depthFBO = device->CreateFramebuffer();

	depthFBO->Bind();
	depthFBO->AttachDepth(depthTexture.get());
	depthFBO->Unbind();
}
