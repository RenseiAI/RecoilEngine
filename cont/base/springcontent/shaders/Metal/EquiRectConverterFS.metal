// EquiRectConverterFS.metal
// Translated from GLSL/EquiRectConverterFS.glsl
// Equirectangular to cubemap converter fragment shader

#include <metal_stdlib>
using namespace metal;

struct VertexOut {
    float4 position [[position]];
    float3 uvw;
};

float4 SampleEquiRect(float3 rayDirection, texture2d<float> tex, sampler texSampler) {
    constexpr float PI = 3.1415926535897932384626433832795;
    constexpr float PIm2 = 2.0 * PI;

    float2 uv = float2(
        (atan2(rayDirection.z, rayDirection.x) / PIm2) + 0.5,
        acos(-rayDirection.y) / PI
    );

    return tex.sample(texSampler, uv, level(0.0));
}

fragment float4 equiRectConverterFS(
    VertexOut in [[stage_in]],
    texture2d<float> tex [[texture(0)]],
    sampler texSampler [[sampler(0)]])
{
    return SampleEquiRect(normalize(in.uvw), tex, texSampler);
}
