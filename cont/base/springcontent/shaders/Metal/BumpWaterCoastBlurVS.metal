// BumpWaterCoastBlurVS.metal
// Translated from GLSL/BumpWaterCoastBlurVS.glsl
// Water coast blur vertex shader

#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float3 pos [[attribute(0)]];
    float4 uv [[attribute(1)]];
};

struct VertexOut {
    float4 position [[position]];
    float4 vTexCoord;
};

struct Uniforms {
    float4x4 modelViewProjectionMatrix;
};

vertex VertexOut bumpWaterCoastBlurVS(
    VertexIn in [[stage_in]],
    constant Uniforms& uniforms [[buffer(0)]])
{
    VertexOut out;
    out.vTexCoord = in.uv;
    out.position = uniforms.modelViewProjectionMatrix * float4(in.pos, 1.0);
    return out;
}
