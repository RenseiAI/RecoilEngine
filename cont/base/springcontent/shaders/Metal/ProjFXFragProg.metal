// ProjFXFragProg.metal
// Translated from GLSL/ProjFXFragProg.glsl
// Projectile effects fragment shader

#include <metal_stdlib>
using namespace metal;

struct VertexOut {
    float4 position [[position]];
    float4 vCol;
    float4 vUV [[centroid_perspective]];
    float vLayer;
    float vBF;
    float fogFactor;
    float4 vsPos;
    float2 screenUV [[center_no_perspective]];
};

struct ProjFXUniforms {
    float4x4 projectionMatrix;
    float softenThreshold;
    float2 softenExponent;
    float4 alphaCtrl;  // default: (0.0, 0.0, 0.0, 1.0) - always pass
    float3 fogColor;
};

#define NORM2SNORM(value) ((value) * 2.0 - 1.0)
#define SNORM2NORM(value) ((value) * 0.5 + 0.5)

float GetViewSpaceDepth(float d, float4x4 projMatrix) {
#ifndef DEPTH_CLIP01
    d = NORM2SNORM(d);
#endif
    return -projMatrix[3][2] / (projMatrix[2][2] + d);
}

bool AlphaDiscard(float a, float4 alphaCtrl) {
    float alphaTestGT = float(a > alphaCtrl.x) * alphaCtrl.y;
    float alphaTestLT = float(a < alphaCtrl.x) * alphaCtrl.z;
    return ((alphaTestGT + alphaTestLT + alphaCtrl.w) == 0.0);
}

fragment float4 projFXFragProg(
    VertexOut in [[stage_in]],
    constant ProjFXUniforms& uniforms [[buffer(0)]],
#ifdef USE_TEXTURE_ARRAY
    texture2d_array<float> atlasTex [[texture(0)]],
#else
    texture2d<float> atlasTex [[texture(0)]],
#endif
#ifdef SMOOTH_PARTICLES
    texture2d<float> depthTex [[texture(1)]],
#endif
    sampler texSampler [[sampler(0)]])
{
#ifdef USE_TEXTURE_ARRAY
    float4 c0 = atlasTex.sample(texSampler, in.vUV.xy, uint(in.vLayer));
    float4 c1 = atlasTex.sample(texSampler, in.vUV.zw, uint(in.vLayer));
#else
    float4 c0 = atlasTex.sample(texSampler, in.vUV.xy);
    float4 c1 = atlasTex.sample(texSampler, in.vUV.zw);
#endif

    float4 color = mix(c0, c1, in.vBF);
    float4 fragColor = color * in.vCol;
    fragColor.rgb = mix(fragColor.rgb, uniforms.fogColor * fragColor.a, (1.0 - in.fogFactor));

#ifdef SMOOTH_PARTICLES
    float depthZO = depthTex.sample(texSampler, in.screenUV).x;
    float depthVS = GetViewSpaceDepth(depthZO, uniforms.projectionMatrix);

    if (uniforms.softenThreshold > 0.0) {
        float edgeSmoothness = smoothstep(0.0, uniforms.softenThreshold, in.vsPos.z - depthVS);
        fragColor *= pow(edgeSmoothness, uniforms.softenExponent.x);
    } else {
        float edgeSmoothness = smoothstep(uniforms.softenThreshold, 0.0, in.vsPos.z - depthVS);
        fragColor *= pow(edgeSmoothness, uniforms.softenExponent.y);
    }
#endif

    if (AlphaDiscard(fragColor.a, uniforms.alphaCtrl)) {
        discard_fragment();
    }

    return fragColor;
}
