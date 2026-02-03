// CubeMapFS.metal
// Translated from GLSL/CubeMapFS.glsl
// Cube map fragment shader for skybox rendering

#include <metal_stdlib>
using namespace metal;

struct VertexOut {
    float4 position [[position]];
    float3 uvw;
};

struct Uniforms {
    float4 planeColor;  // .w signals if enabled
};

fragment float4 cubeMapFS(
    VertexOut in [[stage_in]],
    constant Uniforms& uniforms [[buffer(0)]],
    texturecube<float> skybox [[texture(0)]],
    sampler texSampler [[sampler(0)]])
{
    float4 texColor = skybox.sample(texSampler, normalize(in.uvw));
    return mix(texColor, float4(uniforms.planeColor.rgb, 1.0),
               smoothstep(0.0, 0.5, in.uvw.y) * uniforms.planeColor.w);
}
