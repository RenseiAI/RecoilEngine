// ModernSkyVS.metal
// Translated from GLSL/ModernSkyVS.glsl
// Procedural sky vertex shader

#include <metal_stdlib>
using namespace metal;

// Cube vertices for sky dome
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

struct VertexOut {
    float4 position [[position]];
    float3 dir;
};

struct SkyUniforms {
    float4x4 modelViewMatrixInverse;
    float4x4 modelViewProjectionMatrix;
    float3 midMap;
};

vertex VertexOut modernSkyVS(
    uint vertexID [[vertex_id]],
    constant SkyUniforms& uniforms [[buffer(0)]])
{
    VertexOut out;

    float3 camPos = uniforms.modelViewMatrixInverse[3].xyz;

    float R = sqrt(16.0 * (uniforms.midMap.x * uniforms.midMap.x + uniforms.midMap.z * uniforms.midMap.z));

    float3 pos = uniforms.midMap + CUBE_VERT[vertexID] * R;

    out.dir = pos - camPos.xyz;

    float4 clipPos = uniforms.modelViewProjectionMatrix * float4(pos, 1.0);
    out.position = float4(clipPos.xy, clipPos.w, clipPos.w);  // Force to far plane

    return out;
}
