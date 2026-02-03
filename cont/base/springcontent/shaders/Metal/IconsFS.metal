// IconsFS.metal
// Translated from GLSL/IconsFS.glsl
// Icons fragment shader

#include <metal_stdlib>
using namespace metal;

struct VertexOut {
    float4 position [[position]];
    float3 vuvw;
    float4 vColor;
};

struct Uniforms {
    float4 ucolor;
    float4 alphaCtrl;  // default: (0.0, 0.0, 0.0, 1.0) - always pass
};

bool AlphaDiscard(float a, float4 alphaCtrl) {
    float alphaTestGT = float(a > alphaCtrl.x) * alphaCtrl.y;
    float alphaTestLT = float(a < alphaCtrl.x) * alphaCtrl.z;
    return ((alphaTestGT + alphaTestLT + alphaCtrl.w) == 0.0);
}

fragment float4 iconsFS(
    VertexOut in [[stage_in]],
    constant Uniforms& uniforms [[buffer(0)]],
    texture2d<float> mainTex [[texture(0)]],
    texture2d<float> custTex [[texture(1)]],
    sampler texSampler [[sampler(0)]])
{
    float4 outColor;

    if (in.vuvw.z == 0.0) {
        outColor = mainTex.sample(texSampler, in.vuvw.xy);
    } else {
        outColor = custTex.sample(texSampler, in.vuvw.xy);
    }

    outColor *= in.vColor * uniforms.ucolor;

    if (AlphaDiscard(outColor.a, uniforms.alphaCtrl)) {
        discard_fragment();
    }

    return outColor;
}
