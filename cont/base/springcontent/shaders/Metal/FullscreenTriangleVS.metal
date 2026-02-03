// FullscreenTriangleVS.metal
// Translated from GLSL/FullscreenTriangleVS.glsl
// Fullscreen triangle vertex shader - generates a triangle covering the screen

#include <metal_stdlib>
using namespace metal;

struct VertexOut {
    float4 position [[position]];
    float2 uv;
};

vertex VertexOut fullscreenTriangleVS(uint vertexID [[vertex_id]]) {
    VertexOut out;

    // Generate UV coordinates using vertex ID
    out.uv = float2((vertexID << 1) & 2, vertexID & 2);
    out.position = float4(out.uv * 2.0 - 1.0, 0.0, 1.0);

    return out;
}
