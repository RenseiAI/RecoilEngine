// Icons2DVS.metal
// Translated from GLSL/Icons2DVS.glsl
// 2D Icons vertex shader

#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float2 pos [[attribute(0)]];
    float3 uvw [[attribute(1)]];
    float4 color [[attribute(2)]];
};

struct VertexOut {
    float4 position [[position]];
    float3 vuvw;
    float4 vColor;
};

struct Uniforms {
    float4x4 modelViewProjectionMatrix;
};

vertex VertexOut icons2DVS(
    VertexIn in [[stage_in]],
    constant Uniforms& uniforms [[buffer(0)]])
{
    VertexOut out;
    out.vuvw = in.uvw;
    out.vColor = in.color;
    out.position = uniforms.modelViewProjectionMatrix * float4(in.pos.x, in.pos.y, 0.0, 1.0);
    return out;
}
