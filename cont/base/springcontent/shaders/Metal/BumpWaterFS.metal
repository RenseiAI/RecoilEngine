// BumpWaterFS.metal
// Translated from GLSL/BumpWaterFS.glsl
// Bump-mapped water fragment shader
// Note: Many features are compile-time variants

#include <metal_stdlib>
using namespace metal;

constant float CausticDepth = 0.5;
constant float CausticRange = 0.45;

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
    float4x4 projectionMatrix;
    float4 TexGenPlane;
    float2 ScreenTextureSizeInverse;
    float2 ScreenInverse;
    float2 ViewPos;
    float4 SurfaceColor;
    float3 DiffuseColor;
    float3 SpecularColor;
    float SpecularPower;
    float SpecularFactor;
    float AmbientFactor;
    float DiffuseFactor;
    float FresnelMin;
    float FresnelMax;
    float FresnelPower;
    float ReflDistortion;
    float PerlinAmp;
    float CausticsResolution;
    float CausticsStrength;
    float frame;
    float3 eyePos;
#ifdef opt_shadows
    float4x4 shadowMatrix;
    float shadowDensity;
#endif
};

struct FogParams {
    float4 color;
    float start;
    float end;
    float scale;
    float density;
};

float3 GetNormal(float4 texCoords1, float4 texCoords2, float PerlinAmp,
                 texture2d<float> normalmap, sampler texSampler, thread float3& octave) {
    float3 octave1 = normalmap.sample(texSampler, texCoords1.xy).rgb;
    float3 octave2 = normalmap.sample(texSampler, texCoords1.zw).rgb;
    float3 octave3 = normalmap.sample(texSampler, texCoords2.xy).rgb;
    float3 octave4 = normalmap.sample(texSampler, texCoords2.zw).rgb;

    float a = PerlinAmp;
    octave1 = (octave1 * 2.0 - 1.0) * a;
    octave2 = (octave2 * 2.0 - 1.0) * a * a;
    octave3 = (octave3 * 2.0 - 1.0) * a * a * a;
    octave4 = (octave4 * 2.0 - 1.0) * a * a * a * a;

    float3 normal = octave1 + octave2 + octave3 + octave4;
    normal = normalize(normal).xzy;

    octave = octave3;

    return normal;
}

#ifdef opt_depth
float ConvertDepthToEyeZ(float d, float4x4 projMatrix) {
    float pm14 = projMatrix[3][2];
    float pm10 = projMatrix[2][2];
    return (pm14 / (d * -2.0 + 1.0 - pm10));
}
#endif

fragment float4 bumpWaterFS(
    VertexOut in [[stage_in]],
    constant WaterUniforms& uniforms [[buffer(0)]],
    constant FogParams& fog [[buffer(1)]],
    texture2d<float> normalmap [[texture(0)]],
    texture2d<float> heightmap [[texture(1)]],
    texture2d<float> caustic [[texture(2)]],
    texture2d<float> foam [[texture(3)]],
#ifdef opt_reflection
    texture2d<float> reflection [[texture(4)]],
#endif
#ifdef opt_refraction
    texture2d<float> refraction [[texture(5)]],
#endif
    texture2d<float> coastmap [[texture(6)]],
#ifdef opt_depth
    texture2d<float> depthmap [[texture(7)]],
#endif
#ifdef opt_shadows
    depth2d<float> shadowmap [[texture(8)]],
    texture2d<float> shadowColorTex [[texture(9)]],
#endif
    sampler texSampler [[sampler(0)]]
#ifdef opt_shadows
    , sampler shadowSampler [[sampler(1)]]
#endif
)
{
    float4 fragColor;
    fragColor.a = 1.0;

    float2 clampedWorldPos = clamp(in.worldPos.xz, float2(0.0), float2(1.0) / uniforms.TexGenPlane.xy);

    // Get water depth
    float outside = 0.0;
    float2 coast = float2(0.0);
    float invwaterdepth = heightmap.sample(texSampler, in.texCoords0.xy).a;
    float waterdepth = 1.0 - invwaterdepth;

    // Normal map
    float3 octave;
    float3 normal = GetNormal(in.texCoords1, in.texCoords2, uniforms.PerlinAmp, normalmap, texSampler, octave);

    float3 eVec = normalize(in.eyeVec);
    float eyeNormalCos = dot(-eVec, normal);
    float angle = (1.0 - abs(eyeNormalCos));

    // Ambient & diffuse
    float3 reflectDir = reflect(normalize(-in.ligVec), normal);
    float specular = angle * pow(max(dot(reflectDir, eVec), 0.0), uniforms.SpecularPower) * uniforms.SpecularFactor;
    float3 SunLow = float3(1.0, 0.1, 1.0);  // Would need SunDir from uniforms
    float diffuse = pow(max(dot(normal, SunLow), 0.0), 3.0) * uniforms.DiffuseFactor;
    float ambient = smoothstep(-1.3, 0.0, eyeNormalCos) * uniforms.AmbientFactor;
    float3 waterSurface = uniforms.SurfaceColor.rgb + uniforms.DiffuseColor * diffuse + float3(ambient);
    float surfaceMix = (uniforms.SurfaceColor.a + diffuse);

#ifdef opt_refraction
    float2 screenPos = in.position.xy - uniforms.ViewPos;
    float2 screencoord = screenPos * uniforms.ScreenTextureSizeInverse;
    float refractDistortion = 60.0 * (1.0 - pow(in.position.z, 80.0));

    float2 refrUV = screencoord + normal.xz * refractDistortion * uniforms.ScreenInverse;
    float3 refrColor = refraction.sample(texSampler, refrUV).rgb;
    fragColor.rgb = mix(refrColor, waterSurface, 0.1 + surfaceMix * 0.100);
#else
    fragColor.rgb = waterSurface;
    fragColor.a = surfaceMix + specular;
#endif

    // Caustics
    if (waterdepth > 0.0) {
        float3 caust = caustic.sample(texSampler, in.texCoords0.zw * uniforms.CausticsResolution).rgb;
#ifdef opt_refraction
        float caustBlend = smoothstep(CausticRange, 0.0, abs(waterdepth - CausticDepth));
        fragColor.rgb += caust * caustBlend * uniforms.CausticsStrength;
#else
        fragColor.a *= min(waterdepth * 4.0, 1.0);
        fragColor.rgb += caust * (1.0 - waterdepth) * 7.5 * uniforms.CausticsStrength;
#endif
    }

#ifdef opt_reflection
    // Reflection with Fresnel
    float fresnel = uniforms.FresnelMin + uniforms.FresnelMax * pow(angle, uniforms.FresnelPower);
    float2 reftexcoord = (in.position.xy - uniforms.ViewPos) * uniforms.ScreenInverse;
    reftexcoord = float2(reftexcoord.x, 1.0 - reftexcoord.y);
    reftexcoord += float2(0.0, 3.0 * uniforms.ScreenInverse.y) + normal.xz * 0.09 * uniforms.ReflDistortion;
    float3 reflColor = reflection.sample(texSampler, reftexcoord).rgb;
    fragColor.rgb = mix(fragColor.rgb, reflColor, fresnel);
#endif

    // Specular
    fragColor.rgb += specular * uniforms.SpecularColor;

    // Fog
    float fogFactor = clamp((fog.end - abs(in.fogFragCoord)) * fog.scale, 0.0, 1.0);
    fragColor.rgb = mix(fog.color.rgb, fragColor.rgb, fogFactor);

    return fragColor;
}
