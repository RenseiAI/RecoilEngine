// ShadowGenFragProgGL4.metal
// Translated from GLSL/ShadowGenFragProgGL4.glsl
// Modern GL4 shadow generation fragment shader

#include <metal_stdlib>
using namespace metal;

struct VertexOut {
    float4 position [[position]];
    float4 uvCoord;
};

struct Uniforms {
    float4 alphaCtrl;  // default: (0.5, 1.0, 0.0, 0.0) - discard if alpha < 0.5
};

bool AlphaDiscard(float a, float4 alphaCtrl) {
    float alphaTestGT = float(a > alphaCtrl.x) * alphaCtrl.y;
    float alphaTestLT = float(a < alphaCtrl.x) * alphaCtrl.z;
    return ((alphaTestGT + alphaTestLT + alphaCtrl.w) == 0.0);
}

fragment float4 shadowGenFragProgGL4(
    VertexOut in [[stage_in]],
    constant Uniforms& uniforms [[buffer(0)]],
    texture2d<float> tex2 [[texture(0)]],
    sampler texSampler [[sampler(0)]])
{
    if (AlphaDiscard(tex2.sample(texSampler, in.uvCoord.xy).a, uniforms.alphaCtrl)) {
        discard_fragment();
    }

    return float4(0.0);
}
