// CubeMapVS.metal
// Translated from GLSL/CubeMapVS.glsl
// Cube map vertex shader for skybox rendering

#include <metal_stdlib>
using namespace metal;

struct VertexOut {
    float4 position [[position]];
    float3 uvw;
};

struct Uniforms {
    float4x4 modelViewProjectionMatrix;
    float3 uvFlip;
};

// Cube vertices - 36 vertices for 6 faces
constant float3 CUBE_VERT[36] = {
    // RIGHT
    float3( 1.0, -1.0, -1.0),
    float3( 1.0, -1.0,  1.0),
    float3( 1.0,  1.0,  1.0),
    float3( 1.0,  1.0,  1.0),
    float3( 1.0,  1.0, -1.0),
    float3( 1.0, -1.0, -1.0),
    // LEFT
    float3(-1.0, -1.0,  1.0),
    float3(-1.0, -1.0, -1.0),
    float3(-1.0,  1.0, -1.0),
    float3(-1.0,  1.0, -1.0),
    float3(-1.0,  1.0,  1.0),
    float3(-1.0, -1.0,  1.0),
    // TOP
    float3(-1.0,  1.0, -1.0),
    float3( 1.0,  1.0, -1.0),
    float3( 1.0,  1.0,  1.0),
    float3( 1.0,  1.0,  1.0),
    float3(-1.0,  1.0,  1.0),
    float3(-1.0,  1.0, -1.0),
    // BOTTOM
    float3(-1.0, -1.0, -1.0),
    float3(-1.0, -1.0,  1.0),
    float3( 1.0, -1.0, -1.0),
    float3( 1.0, -1.0, -1.0),
    float3(-1.0, -1.0,  1.0),
    float3( 1.0, -1.0,  1.0),
    // FRONT
    float3(-1.0, -1.0,  1.0),
    float3(-1.0,  1.0,  1.0),
    float3( 1.0,  1.0,  1.0),
    float3( 1.0,  1.0,  1.0),
    float3( 1.0, -1.0,  1.0),
    float3(-1.0, -1.0,  1.0),
    // BACK
    float3(-1.0,  1.0, -1.0),
    float3(-1.0, -1.0, -1.0),
    float3( 1.0, -1.0, -1.0),
    float3( 1.0, -1.0, -1.0),
    float3( 1.0,  1.0, -1.0),
    float3(-1.0,  1.0, -1.0)
};

vertex VertexOut cubeMapVS(
    uint vertexID [[vertex_id]],
    constant Uniforms& uniforms [[buffer(0)]])
{
    VertexOut out;

    float3 pos = CUBE_VERT[vertexID];
    out.uvw = pos * uniforms.uvFlip;

    float4 clipPos = uniforms.modelViewProjectionMatrix * float4(pos, 1.0);
    out.position = float4(clipPos.xy, clipPos.w, clipPos.w);  // Force depth to far plane

    return out;
}
