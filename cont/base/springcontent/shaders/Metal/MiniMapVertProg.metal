// MiniMapVertProg.metal
// Translated from GLSL/MiniMapVertProg.glsl
// MiniMap vertex shader

#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float2 vertexPos [[attribute(0)]];
    float2 texCoords [[attribute(1)]];
};

struct VertexOut {
    float4 position [[position]];
    float2 vTexCoords;
};

struct Uniforms {
    float4x4 modelViewProjectionMatrix;
};

vertex VertexOut miniMapVertProg(
    VertexIn in [[stage_in]],
    constant Uniforms& uniforms [[buffer(0)]])
{
    VertexOut out;
    out.position = uniforms.modelViewProjectionMatrix * float4(in.vertexPos, 0.0, 1.0);
    out.vTexCoords = in.texCoords;
    return out;
}
