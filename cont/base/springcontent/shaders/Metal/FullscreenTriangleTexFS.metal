// FullscreenTriangleTexFS.metal
// Translated from GLSL/FullscreenTriangleTexFS.glsl
// Fullscreen triangle fragment shader with texture sampling

#include <metal_stdlib>
using namespace metal;

struct VertexOut {
    float4 position [[position]];
    float2 uv;
};

struct Uniforms {
    float4 ucolor;
};

fragment float4 fullscreenTriangleTexFS(
    VertexOut in [[stage_in]],
    constant Uniforms& uniforms [[buffer(0)]],
    texture2d<float> tex [[texture(0)]],
    sampler texSampler [[sampler(0)]])
{
    return uniforms.ucolor * tex.sample(texSampler, in.uv);
}
