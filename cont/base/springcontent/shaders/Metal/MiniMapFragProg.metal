// MiniMapFragProg.metal
// Translated from GLSL/MiniMapFragProg.glsl
// MiniMap fragment shader

#include <metal_stdlib>
using namespace metal;

struct VertexOut {
    float4 position [[position]];
    float2 vTexCoords;
};

struct Uniforms {
    float2 uvMult;
    float infotexMul;
};

fragment float4 miniMapFragProg(
    VertexOut in [[stage_in]],
    constant Uniforms& uniforms [[buffer(0)]],
    texture2d<float> shadingTex [[texture(0)]],
    texture2d<float> minimapTex [[texture(1)]],
    texture2d<float> infomapTex [[texture(2)]],
    sampler texSampler [[sampler(0)]])
{
    constexpr float depthBias = -2.0;

    float4 shadingColor = shadingTex.sample(texSampler, in.vTexCoords);
    float4 minimapColor = minimapTex.sample(texSampler, in.vTexCoords, bias(depthBias));
    float4 infomapColor = infomapTex.sample(texSampler, in.vTexCoords * uniforms.uvMult, bias(depthBias)) - float4(0.5);

    return shadingColor * minimapColor + (infomapColor * uniforms.infotexMul);
}
