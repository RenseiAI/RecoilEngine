// GrassFragProg.metal
// Translated from GLSL/GrassFragProg.glsl
// Grass vegetation fragment shader

#include <metal_stdlib>
using namespace metal;

struct VertexOut {
    float4 position [[position]];
    float4 color;
    float3 normal;
    float4 shadingTexCoords;
    float2 bladeTexCoords;
    float3 ambientDiffuseLightTerm;
#if defined(HAVE_SHADOWS) || defined(SHADOW_GEN)
    float4 shadowTexCoords;
#endif
    float fogFragCoord;
};

struct GrassUniforms {
    float3 specularLightColor;
    float3 ambientLightColor;
    float3 camDir;
    float infoTexIntensityMul;
#ifdef HAVE_SHADOWS
    float groundShadowDensity;
#endif
};

struct FogParams {
    float4 color;
    float start;
    float end;
    float scale;
    float density;
};

fragment float4 grassFragProg(
    VertexOut in [[stage_in]],
    constant GrassUniforms& uniforms [[buffer(0)]],
    constant FogParams& fog [[buffer(1)]],
    texture2d<float> shadingTex [[texture(0)]],
    texture2d<float> grassShadingTex [[texture(1)]],
    texture2d<float> bladeTex [[texture(2)]],
#ifdef HAVE_SHADOWS
    depth2d<float> shadowMap [[texture(3)]],
    texture2d<float> shadowColorTex [[texture(4)]],
#endif
#ifdef HAVE_INFOTEX
    texture2d<float> infoMap [[texture(5)]],
#endif
#if !defined(FLAT_SHADING) && !defined(DISTANCE_FAR)
    texturecube<float> specularTex [[texture(6)]],
#endif
    sampler texSampler [[sampler(0)]]
#ifdef HAVE_SHADOWS
    , sampler shadowSampler [[sampler(1)]]
#endif
)
{
#ifdef SHADOW_GEN
#ifdef DISTANCE_FAR
    return bladeTex.sample(texSampler, in.bladeTexCoords);
#else
    return float4(1.0);
#endif
#endif

    float4 matColor = bladeTex.sample(texSampler, in.bladeTexCoords);
    matColor.rgb *= grassShadingTex.sample(texSampler, in.shadingTexCoords.zw).rgb;
    matColor.rgb *= shadingTex.sample(texSampler, in.shadingTexCoords.zw).rgb * 2.0;

#if defined(FLAT_SHADING) || defined(DISTANCE_FAR)
    float3 specular = float3(0.0);
#else
    float3 reflectDir = reflect(uniforms.camDir, normalize(in.normal));
    float3 specular = specularTex.sample(texSampler, reflectDir).rgb;
#endif

    float4 fragColor;
    fragColor.rgb = matColor.rgb * in.ambientDiffuseLightTerm + 0.1 * specular * uniforms.specularLightColor;
    fragColor.a = matColor.a * in.color.a;

#ifdef HAVE_SHADOWS
    float shadowCoeff = mix(1.0, shadowMap.sample_compare(shadowSampler, in.shadowTexCoords.xy, in.shadowTexCoords.z / in.shadowTexCoords.w), uniforms.groundShadowDensity);
    fragColor.rgb *= mix(uniforms.ambientLightColor, float3(1.0), shadowCoeff);
#endif

#ifdef HAVE_INFOTEX
    fragColor.rgb += (infoMap.sample(texSampler, in.shadingTexCoords.xy).rgb * uniforms.infoTexIntensityMul);
    fragColor.rgb -= (float3(0.5) * float(uniforms.infoTexIntensityMul == 1.0));
#endif

    fragColor.rgb = mix(fog.color.rgb, fragColor.rgb, in.fogFragCoord);

    return fragColor;
}
