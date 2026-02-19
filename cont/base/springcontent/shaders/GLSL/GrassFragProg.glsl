#version 130

//#define FLAT_SHADING

uniform sampler2D shadingTex;
uniform sampler2D grassShadingTex;
uniform sampler2D bladeTex;

#ifdef HAVE_SHADOWS
	uniform sampler2DShadow shadowMap;
	uniform sampler2D shadowColorTex;
	uniform float groundShadowDensity;
#endif
uniform float infoTexIntensityMul;

#ifdef HAVE_INFOTEX
	uniform sampler2D infoMap;
#endif

uniform samplerCube specularTex;
uniform vec3 specularLightColor;
uniform vec3 ambientLightColor;
uniform vec3 camDir;
uniform vec4 fogColor;

in vec3 normal;
in vec4 shadingTexCoords;
in vec2 bladeTexCoords;
in vec3 ambientDiffuseLightTerm;
in float alphaFade;
in float fogFactor;
#if defined(HAVE_SHADOWS) || defined(SHADOW_GEN)
	in vec4 shadowTexCoords;
#endif

out vec4 fragColor;

void main() {
#ifdef SHADOW_GEN
	{
  #ifdef DISTANCE_FAR
		fragColor = texture2D(bladeTex, bladeTexCoords);
  #else
		fragColor = vec4(1.0);
  #endif
		return;
	}
#endif

	vec4 matColor = texture2D(bladeTex, bladeTexCoords);
	matColor.rgb *= texture2D(grassShadingTex, shadingTexCoords.pq).rgb;
	matColor.rgb *= texture2D(shadingTex, shadingTexCoords.pq).rgb * 2.0;

#if defined(FLAT_SHADING) || defined(DISTANCE_FAR)
	vec3 specular = vec3(0.0);
#else
	vec3 reflectDir = reflect(camDir, normalize(normal));
	vec3 specular   = textureCube(specularTex, reflectDir).rgb;
#endif
	fragColor.rgb = matColor.rgb * ambientDiffuseLightTerm + 0.1 * specular * specularLightColor; //TODO make `0.1` specular distr. customizable?
	fragColor.a   = matColor.a * alphaFade;

#ifdef HAVE_SHADOWS
	float shadowCoeff = mix(1.0, shadow2DProj(shadowMap, shadowTexCoords).r, groundShadowDensity);

	fragColor.rgb *= mix(ambientLightColor, vec3(1.0), shadowCoeff);
#endif

#ifdef HAVE_INFOTEX
	fragColor.rgb += (texture2D(infoMap, shadingTexCoords.st).rgb * infoTexIntensityMul);
	fragColor.rgb -= (vec3(0.5, 0.5, 0.5) * float(infoTexIntensityMul == 1.0));
#endif

	fragColor.rgb = mix(fogColor.rgb, fragColor.rgb, fogFactor);
}
