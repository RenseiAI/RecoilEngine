// BumpWaterVS.metal
// Translated from GLSL/BumpWaterVS.glsl
// Bump-mapped water vertex shader

#include <metal_stdlib>
using namespace metal;

// Runtime-defined constants (passed as uniforms or function constants)
// TexGenPlane, ShadingPlane, PerlinStartFreq, PerlinLacunarity,
// SunDir, MapMid are expected to be provided

struct VertexIn {
    float3 pos [[attribute(0)]];
};

struct VertexOut {
    float4 position [[position]];
    float eyeVertexZ;
    float3 eyeVec;
    float3 ligVec;
    float3 worldPos;
    float4 texCoords0;
    float4 texCoords1;
    float4 texCoords2;
    float4 texCoords3;
    float4 texCoords4;
    float4 texCoords5;
    float fogFragCoord;
};

struct WaterUniforms {
    float4x4 modelViewMatrix;
    float4x4 modelViewProjectionMatrix;
    float4 TexGenPlane;
    float4 ShadingPlane;
    float3 SunDir;
    float3 MapMid;
    float PerlinStartFreq;
    float PerlinLacunarity;
    float frame;
    float3 eyePos;
    float2 windVector;
};

vertex VertexOut bumpWaterVS(
    VertexIn in [[stage_in]],
    constant WaterUniforms& uniforms [[buffer(0)]])
{
    VertexOut out;

    float4 pos4 = float4(in.pos, 1.0);

    // Compute texcoords
    out.texCoords0 = uniforms.TexGenPlane * pos4.xzxz;
    out.texCoords5.xy = uniforms.ShadingPlane.xy * pos4.xz;

    // Compute wave texture coords
    float fstart = uniforms.PerlinStartFreq;
    float f = uniforms.PerlinLacunarity;
    out.texCoords1.xy = (float2(-1.0, -1.0) + out.texCoords0.zw + 0.75) * fstart + uniforms.frame * uniforms.windVector;
    out.texCoords1.zw = (float2(-1.0, 1.0) + out.texCoords0.zw + 0.50) * fstart * f - uniforms.frame * uniforms.windVector;
    out.texCoords2.xy = (float2(1.0, -1.0) + out.texCoords0.zw + 0.25) * fstart * f * f + uniforms.frame * uniforms.windVector;
    out.texCoords2.zw = (float2(1.0, 1.0) + out.texCoords0.zw + 0.00) * fstart * f * f * f + uniforms.frame * uniforms.windVector;

    out.texCoords3.xy = out.texCoords0.zw * 160.0 + uniforms.frame * 2.5;
    out.texCoords3.zw = out.texCoords0.zw * 90.0 - uniforms.frame * 2.0;
    out.texCoords4.xy = out.texCoords0.zw * 2.0;
    out.texCoords4.zw = out.texCoords0.zw * 6.0 + uniforms.frame * 0.37;

    // Simulate waves
    float4 waveVertex;
    waveVertex.xzw = pos4.xzw;
    waveVertex.y = 3.0 * (cos(uniforms.frame * 500.0 + pos4.z) * sin(uniforms.frame * 500.0 + pos4.x / 1000.0));

    // Compute light vectors
    out.eyeVec = uniforms.eyePos - waveVertex.xyz;
    out.ligVec = normalize(uniforms.SunDir * 20000.0 + uniforms.MapMid - waveVertex.xyz);

    // Fog
    out.worldPos = waveVertex.xyz;
    out.fogFragCoord = (uniforms.modelViewMatrix * waveVertex).z;

    out.position = uniforms.modelViewProjectionMatrix * waveVertex;

    return out;
}
