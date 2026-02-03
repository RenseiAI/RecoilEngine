// ModelFragProgGL4.metal
// Translated from GLSL/ModelFragProgGL4.glsl
// Modern GL4 model fragment shader

#include <metal_stdlib>
using namespace metal;

struct UniformParamsBuffer {
    float3 rndVec3;
    uint renderCaps;

    float4 timeInfo;
    float4 viewGeometry;
    float4 mapSize;
    float4 mapHeight;

    float4 fogColor;
    float4 fogParams;

    float4 sunDir;

    float4 sunAmbientModel;
    float4 sunAmbientMap;
    float4 sunDiffuseModel;
    float4 sunDiffuseMap;
    float4 sunSpecularModel;
    float4 sunSpecularMap;

    float4 shadowDensity;

    float4 windInfo;
    float2 mouseScreenPos;
    uint mouseStatus;
    uint mouseUnused;
    float4 mouseWorldPos;

    float4 teamColor[255];
};

struct VertexOut {
    float4 position [[position]];
    float4 uvCoord [[centroid_perspective]];
    float4 teamCol;
    float4 worldPos;
    float3 worldNormal;
    float3 worldCameraDir;
    float4 shadowVertexPos;
    float fogFactor;
};

struct FragmentUniforms {
    int shadingMode;  // 0 = NORMAL_SHADING
    float4 alphaCtrl;  // default: (0.0, 0.0, 0.0, 1.0)
    float4 colorMult;
    float4 nanoColor;
};

#define NORM2SNORM(value) ((value) * 2.0 - 1.0)
#define SNORM2NORM(value) ((value) * 0.5 + 0.5)

bool AlphaDiscard(float a, float4 alphaCtrl) {
    float alphaTestGT = float(a > alphaCtrl.x) * alphaCtrl.y;
    float alphaTestLT = float(a < alphaCtrl.x) * alphaCtrl.z;
    return ((alphaTestGT + alphaTestLT + alphaCtrl.w) == 0.0);
}

#ifdef USE_SHADOWS
float3 GetShadowMult(
    float3 shadowCoord,
    float NdotL,
    float shadowDensityY,
    depth2d<float> shadowTex,
    texture2d<float> shadowColorTex,
    sampler shadowSampler)
{
    float sh = min(shadowTex.sample_compare(shadowSampler, shadowCoord.xy, shadowCoord.z),
                   smoothstep(0.0, 0.35, NdotL));
    float3 shColor = shadowColorTex.sample(shadowSampler, shadowCoord.xy).rgb;
    return mix(float3(1.0), sh * shColor, shadowDensityY);
}
#endif

#ifdef DEFERRED_MODE
struct GBufferOut {
    float4 normal [[color(0)]];
    float4 diffuse [[color(1)]];
    float4 specular [[color(2)]];
    float4 emissive [[color(3)]];
    float4 misc [[color(4)]];
};

fragment GBufferOut modelFragProgGL4(
#else
fragment float4 modelFragProgGL4(
#endif
    VertexOut in [[stage_in]],
    constant UniformParamsBuffer& params [[buffer(0)]],
    constant FragmentUniforms& uniforms [[buffer(1)]],
    texture2d<float> tex1 [[texture(0)]],
    texture2d<float> tex2 [[texture(1)]],
#ifdef USE_SHADOWS
    depth2d<float> shadowTex [[texture(2)]],
    texture2d<float> shadowColorTex [[texture(3)]],
#endif
    texturecube<float> reflectTex [[texture(4)]],
    sampler texSampler [[sampler(0)]]
#ifdef USE_SHADOWS
    , sampler shadowSampler [[sampler(1)]]
#endif
)
{
    float4 texColor1 = tex1.sample(texSampler, in.uvCoord.xy);
    float4 texColor2 = tex2.sample(texSampler, in.uvCoord.xy);

    float alpha = in.teamCol.a * float(texColor2.a >= 0.5);
    if (AlphaDiscard(alpha, uniforms.alphaCtrl)) {
        discard_fragment();
    }

    texColor1.rgb = mix(texColor1.rgb, in.teamCol.rgb, texColor1.a);
    float3 finalColor = texColor1.rgb;

    if (uniforms.shadingMode == 0) {  // NORMAL_SHADING
        float3 L = normalize(params.sunDir.xyz);
        float3 V = normalize(in.worldCameraDir);
        float3 H = normalize(L + V);
        float3 N = normalize(in.worldNormal);
        float3 R = -reflect(V, N);

        float3 reflColor = reflectTex.sample(texSampler, R).rgb;

        float NdotL = clamp(dot(N, L), 0.0, 1.0);
        float HdotN = clamp(dot(N, H), 0.0, 1.0);

#ifdef USE_SHADOWS
        float3 shadowMult = GetShadowMult(
            in.shadowVertexPos.xyz / in.shadowVertexPos.w,
            NdotL,
            params.shadowDensity.y,
            shadowTex, shadowColorTex, shadowSampler);
#else
        float3 shadowMult = float3(1.0);
#endif

        float3 light = params.sunAmbientModel.rgb + (NdotL * params.sunDiffuseModel.rgb);

        float specPower = min(pow(HdotN, 2.5 * params.sunSpecularModel.a) +
                              0.3 * pow(HdotN, 2.0 * 3.0), 1.0);
        float3 specular = params.sunSpecularModel.rgb * specPower;
        specular *= (texColor2.g * 4.0);
        specular *= shadowMult;

        light = mix(params.sunAmbientModel.rgb, light, shadowMult);
        light = mix(light, reflColor, texColor2.g);
        light += texColor2.rrr;

        finalColor = finalColor * light + specular;
    }

#ifdef DEFERRED_MODE
    float4 diffColor = uniforms.colorMult * float4(mix(texColor1.rgb, uniforms.nanoColor.rgb, uniforms.nanoColor.a), alpha);
    GBufferOut out;
    out.normal = float4(SNORM2NORM(in.worldNormal), 1.0);
    out.diffuse = diffColor;
    out.specular = float4(texColor2.rgb, alpha);
    out.emissive = float4(0.0);
    out.misc = float4(0.0);
    return out;
#else
    float4 fragColor;
    fragColor.rgb = mix(finalColor.rgb, params.fogColor.rgb, (1.0 - in.fogFactor) * int(uniforms.shadingMode == 0));
    fragColor.rgb = mix(fragColor.rgb, uniforms.nanoColor.rgb, uniforms.nanoColor.a * int(uniforms.shadingMode == 0));
    fragColor.a = alpha;
    fragColor *= uniforms.colorMult;
    return fragColor;
#endif
}
