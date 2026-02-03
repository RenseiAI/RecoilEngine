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
};

// Note: The original shader has the alpha mask test commented out
// If alpha masking is needed, uncomment and use the texture
fragment float4 shadowGenFragProg(
    VertexOut in [[stage_in]],
    constant Uniforms& uniforms [[buffer(0)]],
    texture2d<float> alphaMaskTex [[texture(0)]],
    sampler texSampler [[sampler(0)]])
{
    // Alpha mask test (currently disabled in GLSL)
    // if (alphaMaskTex.sample(texSampler, in.texCoord).a <= uniforms.alphaParams.x) {
    //     discard_fragment();
    // }

    // Empty fragment shader - depth is written automatically
    return float4(0.0);
}
