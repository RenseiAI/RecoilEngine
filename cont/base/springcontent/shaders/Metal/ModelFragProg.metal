// ModelFragProg.metal
// Translated from GLSL/ModelFragProg.glsl
// Legacy model fragment shader (GLSL 120 style)

#include <metal_stdlib>
using namespace metal;

struct VertexOut {
    float4 position [[position]];
    float4 vertexWorldPos;
    float3 cameraDir;
    float fogFactor;
    float3 normalv;
    float2 texCoord;
#ifdef USE_SHADOWS
    float4 shadowVertexPos;
#endif
};

struct FogParams {
    float4 color;
    float start;
    float end;
    float scale;
    float density;
};

struct SunParams {
    float3 sunDir;
    float3 sunDiffuse;
    float3 sunAmbient;
    float3 sunSpecular;
};

struct ModelUniforms {
    float4 teamColor;
    float4 nanoColor;
    float4 alphaCtrl;  // default: (0.0, 0.0, 0.0, 1.0) - always pass
#ifdef USE_SHADOWS
    float shadowDensity;
#endif
};

#ifdef DEFERRED_MODE
struct GBufferOut {
    float4 normal [[color(0)]];
    float4 diffuse [[color(1)]];
    float4 specular [[color(2)]];
    float4 emissive [[color(3)]];
    float4 misc [[color(4)]];
};
#endif

bool AlphaDiscard(float a, float4 alphaCtrl) {
    float alphaTestGT = float(a > alphaCtrl.x) * alphaCtrl.y;
    float alphaTestLT = float(a < alphaCtrl.x) * alphaCtrl.z;
    return ((alphaTestGT + alphaTestLT + alphaCtrl.w) == 0.0);
}

#ifdef USE_SHADOWS
float3 GetShadowMult(
    float NdotL,
    float4 shadowVertexPos,
    float shadowDensity,
    depth2d<float> shadowTex,
    texture2d<float> shadowColorTex,
    sampler shadowSampler)
{
    float3 shadowCoord = shadowVertexPos.xyz / shadowVertexPos.w;
    float sh = min(shadowTex.sample_compare(shadowSampler, shadowCoord.xy, shadowCoord.z),
                   smoothstep(0.0, 0.35, NdotL));
    float3 shColor = shadowColorTex.sample(shadowSampler, shadowCoord.xy).rgb;
    return mix(float3(1.0), sh * shColor, shadowDensity);
}
#endif

// Dynamic lighting stub - simplified version
float3 DynamicLighting(
    float3 normal,
    float3 diffuse,
    float3 specular,
    float4 vertexWorldPos)
{
    // Dynamic lighting would require light source uniform buffer
    // This is a placeholder - actual implementation needs gl_LightSource[] equivalent
    return float3(0.0);
}

#ifdef DEFERRED_MODE
fragment GBufferOut modelFragProg(
#else
fragment float4 modelFragProg(
#endif
    VertexOut in [[stage_in]],
    constant FogParams& fog [[buffer(0)]],
    constant SunParams& sun [[buffer(1)]],
    constant ModelUniforms& uniforms [[buffer(2)]],
    texture2d<float> diffuseTex [[texture(0)]],
    texture2d<float> shadingTex [[texture(1)]],
    texturecube<float> specularTex [[texture(2)]],
    texturecube<float> reflectTex [[texture(3)]],
#ifdef USE_SHADOWS
    depth2d<float> shadowTex [[texture(4)]],
    texture2d<float> shadowColorTex [[texture(5)]],
#endif
    sampler texSampler [[sampler(0)]]
#ifdef USE_SHADOWS
    , sampler shadowSampler [[sampler(1)]]
#endif
)
{
    float3 normal = normalize(in.normalv);

    float NdotLu = dot(normal, sun.sunDir);
    float NdotL = max(NdotLu, 1e-3);
    float3 light = NdotL * sun.sunDiffuse + sun.sunAmbient;

    float4 diffuse = diffuseTex.sample(texSampler, in.texCoord);
    float4 extraColor = shadingTex.sample(texSampler, in.texCoord);

    float3 reflectDir = reflect(in.cameraDir, normal);
    float3 specular = specularTex.sample(texSampler, reflectDir).rgb * sun.sunSpecular;
    float3 reflection = reflectTex.sample(texSampler, reflectDir).rgb;

#ifdef USE_SHADOWS
    float3 shadowMult = GetShadowMult(NdotL, in.shadowVertexPos, uniforms.shadowDensity,
                                       shadowTex, shadowColorTex, shadowSampler);
#else
    float3 shadowMult = float3(1.0);
#endif

    float alpha = uniforms.teamColor.a * extraColor.a;

    if (AlphaDiscard(alpha, uniforms.alphaCtrl)) {
        discard_fragment();
    }

    specular *= (extraColor.g * 4.0);
    specular *= shadowMult;
    light = mix(sun.sunAmbient, light, shadowMult);

    reflection = mix(light, reflection, extraColor.g);
    reflection += extraColor.rrr;  // self-illum

#ifdef DEFERRED_MODE
    GBufferOut out;
    out.normal = float4((normal + float3(1.0)) * 0.5, 1.0);
    out.diffuse = float4(mix(diffuse.rgb, uniforms.teamColor.rgb, diffuse.a), alpha);
    out.diffuse = float4(mix(out.diffuse.rgb, uniforms.nanoColor.rgb, uniforms.nanoColor.a), alpha);
    out.specular = float4(extraColor.rgb, alpha);
    out.emissive = float4(0.0);
    out.misc = float4(0.0);
    return out;
#else
    float4 fragColor = diffuse;
    fragColor.rgb = mix(fragColor.rgb, uniforms.teamColor.rgb, fragColor.a);
    fragColor.rgb = fragColor.rgb * reflection + specular;

    // Dynamic lighting would be added here if MAX_DYNAMIC_MODEL_LIGHTS > 0

    fragColor.rgb = mix(fog.color.rgb, fragColor.rgb, in.fogFactor);
    fragColor.rgb = mix(fragColor.rgb, uniforms.nanoColor.rgb, uniforms.nanoColor.a);
    fragColor.a = alpha;

    return fragColor;
#endif
}
