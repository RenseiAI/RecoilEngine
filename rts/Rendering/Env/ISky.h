/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef I_SKY_H
#define I_SKY_H

#include "SkyLight.h"
#include <memory>
#include <string>

struct MapTextureData;
class ISky
{
protected:
	ISky();

public:
	virtual ~ISky();

	virtual void Update() = 0;
	virtual void UpdateSunDir() = 0;
	virtual void UpdateSkyTexture() = 0;

	virtual void Draw() = 0;

	virtual void SetLuaTexture(const MapTextureData& td) {}

	virtual bool IsValid() const = 0;

	virtual std::string GetName() const = 0;

	void IncreaseCloudDensity() { cloudDensity *= 1.05f; }
	void DecreaseCloudDensity() { cloudDensity *= 0.95f; }
	float GetCloudDensity() const { return cloudDensity; }

	ISkyLight* GetLight() const { return skyLight; }

	bool SunVisible(const float3 pos) const;

	bool& WireFrameModeRef() { return wireFrameMode; }

	/**
	 * Formerly set FFP fog via glFog* calls; now a no-op.
	 * Fog parameters are set as explicit shader uniforms via GetFogUniforms().
	 */
	void SetupFog();

	/**
	 * Returns fog color and parameters for use as explicit shader uniforms.
	 * outColor: RGBA fog color (alpha=1)
	 * outParams: (start, end, unused, scale=1/(end-start))
	 */
	void GetFogUniforms(float4& outColor, float4& outParams) const;

	bool IsUpdated() {
		return std::exchange(updated, false);
	}
	void SetUpdated() { updated = true; }
public:
	static void SetSky();
	static auto& GetSky() { return sky; }
	static void KillSky() { sky = nullptr; }
public:
	void SetSkyAxisAngle(const float4& skyAxisAngleRaw);
	const float4& GetSkyAxisAngle() const { return skyAxisAngle; }
public:
	float3 skyColor;
	float3 sunColor;
	float3 cloudColor;
	float4 fogColor;

	float fogStart;
	float fogEnd;
	float cloudDensity;
protected:
	float4 skyAxisAngle;
protected:
	static std::unique_ptr<ISky> sky;

	ISkyLight* skyLight;

	bool wireFrameMode;
private:
	bool updated;
};

#endif // I_SKY_H
