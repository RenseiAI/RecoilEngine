// ProjFXFragShadowProg.metal
// Translated from GLSL/ProjFXFragShadowProg.glsl
// Projectile effects shadow fragment shader

#include <metal_stdlib>
using namespace metal;

struct VertexOut {
    float4 position [[position]];
    float4 vCol;
    float4 vUV;
    float vLayer;
    float vBF;
};

struct ProjFXShadowUniforms {
    float4 alphaCtrl;  // default: (0.0, 1.0, 0.0, 0.0) - always pass
    float shadowColorMode;  // 1.0 = color, 0.0 = grayscale
};

constant float3 LUMA = float3(0.299, 0.587, 0.114);

bool AlphaDiscard(float a, float4 alphaCtrl) {
    float alphaTestGT = float(a > alphaCtrl.x) * alphaCtrl.y;
    float alphaTestLT = float(a < alphaCtrl.x) * alphaCtrl.z;
    return ((alphaTestGT + alphaTestLT + alphaCtrl.w) == 0.0);
}

fragment float3 projFXFragShadowProg(
    VertexOut in [[stage_in]],
    constant ProjFXShadowUniforms& uniforms [[buffer(0)]],
#ifdef USE_TEXTURE_ARRAY
    texture2d_array<float> atlasTex [[texture(0)]],
#else
    texture2d<float> atlasTex [[texture(0)]],
#endif
    sampler texSampler [[sampler(0)]])
{
#ifdef USE_TEXTURE_ARRAY
    float4 c0 = atlasTex.sample(texSampler, in.vUV.xy, uint(in.vLayer));
    float4 c1 = atlasTex.sample(texSampler, in.vUV.zw, uint(in.vLayer));
#else
    float4 c0 = atlasTex.sample(texSampler, in.vUV.xy);
    float4 c1 = atlasTex.sample(texSampler, in.vUV.zw);
#endif

    float4 color = mix(c0, c1, in.vBF);
    color *= in.vCol;

    color.rgb = mix(float3(dot(color.rgb, LUMA)), color.rgb, uniforms.shadowColorMode);

    if (AlphaDiscard(color.a, uniforms.alphaCtrl)) {
        discard_fragment();
    }

    // Use "multiply" blending: R = D * (1 - Sa + S * Sa)
    return float3(1.0) - color.aaa + color.rgb * color.aaa;
}
