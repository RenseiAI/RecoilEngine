// ShapesVertProg.metal
// Translated from GLSL/ShapesVertProg.glsl
// Simple shapes vertex shader

#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float3 vertexPos [[attribute(0)]];
};

struct VertexOut {
    float4 position [[position]];
};

struct Uniforms {
    float4x4 viewProjMat;
    float4x4 worldMat;
};

vertex VertexOut shapesVertProg(
    VertexIn in [[stage_in]],
    constant Uniforms& uniforms [[buffer(0)]])
{
    VertexOut out;
    out.position = uniforms.viewProjMat * uniforms.worldMat * float4(in.vertexPos, 1.0);
    return out;
}
