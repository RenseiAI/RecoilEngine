// SMFFragProg.metal
// Translated from GLSL/SMFFragProg.glsl
// SMF terrain fragment shader
// Note: This shader has many compile-time variants controlled by preprocessor defines

#include <metal_stdlib>
using namespace metal;

// Compile-time variant defines:
// SMF_ADV_SHADING - Advanced shading with normal mapping
// SMF_SPECULAR_LIGHTING - Enable specular lighting
// HAVE_SHADOWS - Shadow mapping enabled
// SMF_WATER_ABSORPTION - Water depth effects
// SMF_DETAIL_TEXTURE_SPLATTING - Detail texture splatting
// SMF_DETAIL_NORMAL_TEXTURE_SPLATTING - Detail normal splatting
// SMF_SKY_REFLECTIONS - Sky reflections
// SMF_BLEND_NORMALS - Normal blending
// SMF_LIGHT_EMISSION - Emissive lighting
// SMF_PARALLAX_MAPPING - Parallax mapping
// DEFERRED_MODE - Deferred rendering output
// HAVE_INFOTEX - Info texture overlay

constant float SMF_INTENSITY_MULT = 210.0 / 255.0;
constant float SMF_SHALLOW_WATER_DEPTH = 10.0;
constant float SMF_SHALLOW_WATER_DEPTH_INV = 1.0 / SMF_SHALLOW_WATER_DEPTH;
constant float SMF_DETAILTEX_RES = 0.02;

struct VertexOut {
    float4 position [[position]];
    float3 halfDir;
    float fogFactor;
    float4 vertexWorldPos;
    float2 diffuseTexCoords;
};

struct SMFUniforms {
    float2 specularTexGen;
    float4 alphaCtrl; // default: (0.0, 0.0, 0.0, 1.0) - always pass
#ifdef SMF_ADV_SHADING
    float2 normalTexGen;
    float3 groundAmbientColor;
    float3 groundDiffuseColor;
    float3 groundSpecularColor;
    float groundSpecularExponent;
    float groundShadowDensity;
    float2 mapHeights;
    float4 lightDir;
    float3 cameraPos;
#endif
    float infoTexIntensityMul;
    float2 infoTexGen;
#ifdef SMF_WATER_ABSORPTION
    float3 waterMinColor;
    float3 waterBaseColor;
    float3 waterAbsorbColor;
#endif
};

struct FogParams {
    float4 color;
    float start;
    float end;
    float scale;
    float density;
};

#ifdef SMF_ADV_SHADING
float3 GetFragmentNormal(float2 uv, texture2d<float> normalsTex, sampler texSampler) {
    float3 normal;
    float4 sample = normalsTex.sample(texSampler, uv);
    normal.xz = float2(sample.r, sample.a);
    normal.y = sqrt(1.0 - dot(normal.xz, normal.xz));
    return normal;
}
#endif

#ifndef SMF_DETAIL_NORMAL_TEXTURE_SPLATTING
float4 GetDetailTextureColor(float4 vertexWorldPos, texture2d<float> detailTex, sampler texSampler
#ifdef SMF_DETAIL_TEXTURE_SPLATTING
    , texture2d<float> splatDetailTex, texture2d<float> splatDistrTex, float4 splatTexScales, float4 splatTexMults, float2 uv
#endif
) {
#ifndef SMF_DETAIL_TEXTURE_SPLATTING
    float2 detailTexCoord = vertexWorldPos.xz * SMF_DETAILTEX_RES;
    float4 detailCol = (detailTex.sample(texSampler, detailTexCoord) * 2.0) - 1.0;
#else
    float4 splatTexCoord0 = vertexWorldPos.xzxz * splatTexScales.rrgg;
    float4 splatTexCoord1 = vertexWorldPos.xzxz * splatTexScales.bbaa;
    float4 splatDetails;
    splatDetails.r = splatDetailTex.sample(texSampler, splatTexCoord0.xy).r;
    splatDetails.g = splatDetailTex.sample(texSampler, splatTexCoord0.zw).g;
    splatDetails.b = splatDetailTex.sample(texSampler, splatTexCoord1.xy).b;
    splatDetails.a = splatDetailTex.sample(texSampler, splatTexCoord1.zw).a;
    splatDetails = (splatDetails * 2.0) - 1.0;
    float4 splatCofac = splatDistrTex.sample(texSampler, uv) * splatTexMults;
    float4 detailCol = float4(dot(splatDetails, splatCofac));
#endif
    return detailCol;
}
#endif

bool AlphaDiscard(float a, float4 alphaCtrl) {
    float alphaTestGT = float(a > alphaCtrl.x) * alphaCtrl.y;
    float alphaTestLT = float(a < alphaCtrl.x) * alphaCtrl.z;
    return ((alphaTestGT + alphaTestLT + alphaCtrl.w) == 0.0);
}

#ifdef DEFERRED_MODE
struct GBufferOut {
    float4 normal [[color(0)]];
    float4 diffuse [[color(1)]];
    float4 specular [[color(2)]];
    float4 emissive [[color(3)]];
    float4 misc [[color(4)]];
};

fragment GBufferOut smfFragProg(
#else
fragment float4 smfFragProg(
#endif
    VertexOut in [[stage_in]],
    constant SMFUniforms& uniforms [[buffer(0)]],
    constant FogParams& fog [[buffer(1)]],
    texture2d<float> diffuseTex [[texture(0)]],
    texture2d<float> normalsTex [[texture(1)]],
    texture2d<float> detailTex [[texture(2)]],
#ifndef SMF_ADV_SHADING
    texture2d<float> shadingTex [[texture(3)]],
#endif
    texture2d<float> infoTex [[texture(4)]],
#ifdef HAVE_SHADOWS
    depth2d<float> shadowTex [[texture(5)]],
    texture2d<float> shadowColorTex [[texture(6)]],
#endif
    sampler texSampler [[sampler(0)]]
#ifdef HAVE_SHADOWS
    , sampler shadowSampler [[sampler(1)]]
#endif
)
{
    float2 diffTexCoords = in.diffuseTexCoords;
    float2 specTexCoords = in.vertexWorldPos.xz * uniforms.specularTexGen;
    float2 infoTexCoords = in.vertexWorldPos.xz * uniforms.infoTexGen;

#ifdef SMF_ADV_SHADING
    float2 normTexCoords = in.vertexWorldPos.xz * uniforms.normalTexGen;
    float3 cameraDir = in.vertexWorldPos.xyz - uniforms.cameraPos;
    float3 normal = GetFragmentNormal(normTexCoords, normalsTex, texSampler);
#endif

    float4 detailCol = GetDetailTextureColor(in.vertexWorldPos, detailTex, texSampler
#ifdef SMF_DETAIL_TEXTURE_SPLATTING
        , splatDetailTex, splatDistrTex, splatTexScales, splatTexMults, specTexCoords
#endif
    );

#if !defined(DEFERRED_MODE) && defined(SMF_ADV_SHADING)
    float cosAngleDiffuse = clamp(dot(uniforms.lightDir.xyz, normal), 0.0, 1.0);
    float cosAngleSpecular = clamp(dot(normalize(in.halfDir), normal), 0.001, 1.0);
#endif

    float4 diffuseCol = diffuseTex.sample(texSampler, diffTexCoords);
    float4 specularCol = float4(0.0, 0.0, 0.0, 1.0);
    float4 emissionCol = float4(0.0);

    float3 shadowCoeff = float3(1.0);

#if !defined(DEFERRED_MODE) && defined(HAVE_SHADOWS)
    // Shadow calculation would go here
#endif

#ifndef DEFERRED_MODE
    float4 fragColor;
#ifdef SMF_ADV_SHADING
    float3 groundShadeInt = uniforms.groundAmbientColor + uniforms.groundDiffuseColor * (cosAngleDiffuse * shadowCoeff);
    groundShadeInt *= SMF_INTENSITY_MULT;
    fragColor.rgb = (diffuseCol.rgb + detailCol.rgb) * groundShadeInt;
    fragColor.a = 1.0;
#else
    fragColor.rgb = (diffuseCol.rgb + detailCol.rgb) * shadingTex.sample(texSampler, specTexCoords).rgb;
    fragColor.a = diffuseCol.a;
#endif

    if (AlphaDiscard(fragColor.a, uniforms.alphaCtrl)) {
        discard_fragment();
    }

    fragColor.rgb = mix(fog.color.rgb, fragColor.rgb, in.fogFactor);
    return fragColor;
#else
    GBufferOut out;
    out.normal = float4((normal + float3(1.0)) * 0.5, 1.0);
    out.diffuse = diffuseCol + detailCol;
    out.specular = specularCol;
    out.emissive = emissionCol;
    out.misc = float4(0.0);
    return out;
#endif
}
