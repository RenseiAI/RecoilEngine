// ShadowGenFragProg.metal
// Translated from GLSL/ShadowGenFragProg.glsl
// Shadow generation fragment shader

#include <metal_stdlib>
using namespace metal;

struct VertexOut {
    float4 position [[position]];
    float2 texCoord;
};

struct Uniforms {
    float2 alphaParams;
    float4 alphaCtrl;  // default: (0.0, 0.0, 0.0, 1.0) - always pass
};

bool AlphaDiscard(float a, float4 alphaCtrl) {
    float alphaTestGT = float(a > alphaCtrl.x) * alphaCtrl.y;
    float alphaTestLT = float(a < alphaCtrl.x) * alphaCtrl.z;
    return ((alphaTestGT + alphaTestLT + alphaCtrl.w) == 0.0);
}

fragment float4 shadowGenFragProg(
    VertexOut in [[stage_in]],
    constant Uniforms& uniforms [[buffer(0)]],
    texture2d<float> alphaMaskTex [[texture(0)]],
    sampler texSampler [[sampler(0)]])
{
    // Guard: skip texture sample when alphaCtrl is default "always pass" (w=1.0)
    if (uniforms.alphaCtrl.w < 1.0) {
        if (AlphaDiscard(alphaMaskTex.sample(texSampler, in.texCoord).a, uniforms.alphaCtrl)) {
            discard_fragment();
        }
    }

    return float4(0.0);
}
