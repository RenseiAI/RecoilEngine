// ModernSkyFS.metal
// Translated from GLSL/ModernSkyFS.glsl
// Procedural sky fragment shader with clouds

#include <metal_stdlib>
using namespace metal;

constant float cirrus1 = 0.9;
constant float cumulus1 = 1.8;

struct VertexOut {
    float4 position [[position]];
    float3 dir;
};

struct SkyUniforms {
    float time;
    float4 cloudInfo;  // rgb = cloud color, w = cloud density
    float3 skyColor;
    float3 fogColor;
    float4 planeColor;  // w signals if enabled
    float3 sunDir;
    float4 sunColor;
};

// Value noise function
float Value3D(float3 P) {
    float3 Pi = floor(P);
    float3 Pf = P - Pi;
    float3 Pf_min1 = Pf - 1.0;

    Pi.xyz = Pi.xyz - floor(Pi.xyz * (1.0 / 69.0)) * 69.0;
    float3 Pi_inc1 = step(Pi, float3(69.0 - 1.5)) * (Pi + 1.0);

    float4 Pt = float4(Pi.xy, Pi_inc1.xy) + float2(50.0, 161.0).xyxy;
    Pt *= Pt;
    Pt = Pt.xzxz * Pt.yyww;
    float2 hash_mod = float2(1.0 / (635.298681 + float2(Pi.z, Pi_inc1.z) * 48.500388));
    float4 hash_lowz = fract(Pt * hash_mod.xxxx);
    float4 hash_highz = fract(Pt * hash_mod.yyyy);

    float3 blend = Pf * Pf * Pf * (Pf * (Pf * 6.0 - 15.0) + 10.0);
    float4 res0 = mix(hash_lowz, hash_highz, blend.z);
    float4 blend2 = float4(blend.xy, float2(1.0 - blend.xy));
    return dot(res0, blend2.zxzx * blend2.wwyy);
}

constant float3x3 m = float3x3(0.0, 1.60, 1.20, -1.6, 0.72, -0.96, -1.2, -0.96, 1.28);

#if SIMPLIFIED_RENDERING == 0
float fbm(float3 p) {
    float f = 0.0;
    f += Value3D(p) / 2; p = m * p * 1.1;
    f += Value3D(p) / 4; p = m * p * 1.2;
    f += Value3D(p) / 6; p = m * p * 1.3;
    f += Value3D(p) / 12; p = m * p * 1.4;
    f += Value3D(p) / 24;
    return f;
}
#else
float fbm(float3 p) {
    float f = 0.0;
    f += Value3D(p) / 2; p = m * p * 1.1;
    f += Value3D(p) / 4; p = m * p * 2.2;
    f += Value3D(p) / 12;
    f *= 1.25;
    return f;
}
#endif

float csstep(float m0, float m1, float n0, float n1, float v) {
    return smoothstep(m0, m1, v) * (1.0 - smoothstep(n0, n1, v));
}

fragment float4 modernSkyFS(
    VertexOut in [[stage_in]],
    constant SkyUniforms& uniforms [[buffer(0)]])
{
    float3 pos = normalize(in.dir);

    float cirrus = uniforms.cloudInfo.w * cirrus1;
    float cumulus = uniforms.cloudInfo.w * cumulus1;

    float sunContrib = pow(max(0.0, dot(pos, normalize(uniforms.sunDir))), 64.0);

    float wpContrib = (1.0 - smoothstep(-0.5, -0.2, pos.y)) * uniforms.planeColor.w;
    float3 fragColorRGB = mix(uniforms.skyColor, uniforms.planeColor.rgb, wpContrib);
    fragColorRGB = mix(fragColorRGB, uniforms.sunColor.rgb * uniforms.sunColor.w * 1.3, sunContrib);

    float3 day_extinction = float3(1.0);
    float3 night_extinction = float3(1.0 - exp(uniforms.sunDir.y)) * 0.2;
    float3 extinction = mix(day_extinction, night_extinction, -uniforms.sunDir.y * 0.2 + 0.5);

    // Cirrus clouds
    float density = smoothstep(1.0 - cirrus, 1.0, fbm(pos.xyz / pos.y * 2.0 + uniforms.time * 0.05)) * 0.3;
    fragColorRGB = mix(fragColorRGB, uniforms.cloudInfo.rgb * extinction * 4.0, density * max(pos.y, 0.0));

    // Cumulus clouds
#if SIMPLIFIED_RENDERING == 0
    for (int i = 0; i < 2; i++) {
        float3 cpos = pos;
        cpos.y = smoothstep(-0.5, 1.5, pos.y);
        cpos.xz /= cpos.y;
        float cDensity = smoothstep(1.0 - cumulus, 1.0, fbm((0.7 + float(i) * 0.01) * cpos + uniforms.time * 0.3));
        fragColorRGB = mix(fragColorRGB, uniforms.cloudInfo.rgb * extinction * cDensity * 5.0, min(cDensity, 1.0) * max(pos.y, 0.0));
    }
#else
    {
        float3 cpos = pos;
        cpos.y = smoothstep(-0.5, 1.5, pos.y);
        cpos.xz /= cpos.y;
        float cDensity = smoothstep(1.0 - cumulus, 1.0, fbm(0.7 * cpos + uniforms.time * 0.3));
        fragColorRGB = mix(fragColorRGB, uniforms.cloudInfo.rgb * extinction * cDensity * 5.0, min(cDensity, 1.0) * max(pos.y, 0.0));
    }
#endif

    float4 fragColor;
    fragColor.rgb = fragColorRGB;
    fragColor.a = (0.5 - csstep(-0.8, -0.0, -0.5, 0.3, pos.y));

    return fragColor;
}
