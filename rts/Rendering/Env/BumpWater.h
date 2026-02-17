/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef BUMP_WATER_H
#define BUMP_WATER_H

#include "Rendering/GL/FBO.h"
#include "Rendering/GL/myGL.h"
#include "Rendering/GL/RenderBuffers.h"
#include "Rendering/RHI/RHIDevice.h"
#include "Rendering/RHI/RHITexture.h"
#include "Rendering/RHI/RHIFramebuffer.h"
#include "Rendering/RHI/RHIPipeline.h"
#include "Rendering/RHI/RHIFactory.h"
#include "IWater.h"

#include "System/EventClient.h"
#include "System/Misc/RectangleOverlapHandler.h"

#include <memory>
#include <vector>

namespace Shader {
	struct IProgramObject;
}

class CBumpWater : public IWater, public CEventClient
{
public:
	//! CEventClient interface
	bool WantsEvent(const std::string& eventName) {
		return shoreWaves && (eventName == "UnsyncedHeightMapUpdate");
	}
	bool GetFullRead() const { return true; }
	int GetReadAllyTeam() const { return AllAccessTeam; }

public:
	CBumpWater();
	~CBumpWater() override;
	void InitResources(bool loadShader) override;
	void FreeResources() override;

	void Update() override;
	void UpdateWater(const CGame* game) override;
	void DrawReflection(const CGame* game);
	void DrawRefraction(const CGame* game);
	void Draw() override;
	WATER_RENDERER GetID() const override { return WATER_RENDERER_BUMPMAPPED; }

	bool CanDrawReflectionPass() const override { return true; }
	bool CanDrawRefractionPass() const override { return true; }
private:
	//! coastmap (needed for shorewaves)
	struct CoastAtlasRect {
		explicit CoastAtlasRect(const SRectangle& rect);
		bool isCoastline; ///< if false, then the whole rect is either above water or below water (no coastline -> no need to calc/render distfield)
		int ix1, iy1;
		int ix2, iy2;
		int xsize, ysize;
		float x1, y1;
		float x2, y2;
		float tx1, ty1;
		float tx2, ty2;
	};

	std::vector<CoastAtlasRect> coastmapAtlasRects;
	CRectangleOverlapHandler heightmapUpdates;

	void UploadCoastline(const bool forceFull = false);
	void UpdateCoastmap(const bool initialize = false);
	void UpdateDynWaves(const bool initialize = false);

	int atlasX,atlasY;

	void UnsyncedHeightMapUpdate(const SRectangle& rect);

private:
	//! user options
	char  reflection;   ///< 0:=off, 1:=don't render the terrain, 2:=render everything+terrain
	char  refraction;   ///< 0:=off, 1:=screencopy, 2:=own rendering cycle
	int   reflTexSize;
	bool  depthCopy;    ///< uses a screen depth copy, which allows a nicer interpolation between deep sea and shallow water
	float anisotropy;
	char  depthBits;    ///< depthBits for reflection/refraction RBO
	bool  blurRefl;
	bool  shoreWaves;
	bool  endlessOcean; ///< render the water around the whole map
	bool  dynWaves;     ///< only usable if bumpmap/normal texture is a TileSet

	std::vector<uint8_t> tileOffsets; ///< used to randomize the wave/bumpmap/normal texture
	int  normalTextureX; ///< needed for dynamic waves
	int  normalTextureY;

	int  screenTextureX;
	int  screenTextureY;

	// RHI framebuffers
	std::unique_ptr<RHI::IRHIFramebuffer> reflectFBO;
	std::unique_ptr<RHI::IRHIFramebuffer> refractFBO;
	std::unique_ptr<RHI::IRHIFramebuffer> coastFBO;
	std::unique_ptr<RHI::IRHIFramebuffer> dynWavesFBO;

	TypedRenderBuffer<VA_TYPE_0> rb;

	// RHI textures
	std::unique_ptr<RHI::IRHITexture> refractTexture;
	std::unique_ptr<RHI::IRHITexture> reflectTexture;
	std::unique_ptr<RHI::IRHITexture> depthTexture;   ///< screen depth copy
	std::unique_ptr<RHI::IRHITexture> waveRandTexture;
	std::unique_ptr<RHI::IRHITexture> foamTexture;
	std::unique_ptr<RHI::IRHITexture> normalTexture;  ///< final used
	std::unique_ptr<RHI::IRHITexture> normalTexture2; ///< updates normalTexture with dynamic waves turned on
	std::unique_ptr<RHI::IRHITexture> coastTexture;
	GLuint coastUpdateTextureGL = 0; ///< Raw GL ID for cleanup (atlas-owned lifecycle)
	std::unique_ptr<RHI::IRHITexture> coastUpdateTexture; ///< Non-owning RHI wrapper for binding
	std::vector<std::unique_ptr<RHI::IRHITexture>> caustTextures;

	Shader::IProgramObject* waterShader;
	Shader::IProgramObject* blurShader;

private:
	static RHI::IRHIDevice* GetRHIDevice() { return RHI::GetDevice(); }
};

#endif // BUMP_WATER_H

