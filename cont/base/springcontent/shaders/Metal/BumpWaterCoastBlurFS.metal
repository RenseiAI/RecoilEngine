// BumpWaterCoastBlurFS.metal
// Translated from GLSL/BumpWaterCoastBlurFS.glsl
// Water coast blur fragment shader - creates distance-to-land map

#include <metal_stdlib>
using namespace metal;

constant float res = 15.0;

struct VertexOut {
    float4 position [[position]];
    float4 vTexCoord;
};

struct Uniforms {
    int2 args;  // args.x > 0.5 = renderToAtlas, args.y = radius
};

float4 tex2D(float2 offset, float2 vTexCoordST, float2 vTexCoordPQ,
             float2 texelScissor, float2 texel0, bool renderToAtlas,
             texture2d<float> tex0, texture2d<float> tex1, sampler texSampler) {
    if (renderToAtlas) {
        return tex0.sample(texSampler, vTexCoordST + offset * texel0);
    } else {
        float2 scissor = vTexCoordPQ + (offset * texelScissor);
        bool outOfAtlasBound = any(scissor > float2(1.0)) || any(scissor < float2(0.0));
        if (outOfAtlasBound) {
            return tex1.sample(texSampler, vTexCoordST);
        } else {
            return tex1.sample(texSampler, vTexCoordST + offset * texel0);
        }
    }
}

float2 getDistRect(float d, float2 offset) {
    float2 dist;
    float minDist = res - d * res;
    dist.x = floor(minDist);
    float iDist = dist.x * dist.x;
    minDist *= minDist;
    dist.y = sqrt(minDist - iDist);
    dist += offset;
    return dist;
}

float sqlength(float2 v) {
    return dot(v, v);
}

fragment float4 bumpWaterCoastBlurFS(
    VertexOut in [[stage_in]],
    constant Uniforms& uniforms [[buffer(0)]],
    texture2d<float> tex0 [[texture(0)]],
    texture2d<float> tex1 [[texture(1)]],
    sampler texSampler [[sampler(0)]])
{
    float4 fragColor;

    bool renderToAtlas = uniforms.args.x > 0;
    int radius = uniforms.args.y;

    // Calculate texel sizes using derivatives
    float2 texelScissor = float2(dfdx(in.vTexCoord.z), dfdy(in.vTexCoord.w));
    float2 texel0 = float2(dfdx(in.vTexCoord.x), dfdy(in.vTexCoord.y));

    if (radius < 1) {
        // Initialize
        fragColor = tex1.sample(texSampler, in.vTexCoord.xy);
        return fragColor;
    }

    if (radius > 9) {
        // Blur the texture in the final stage
        float2 groundSurrounding = tex2D(float2(1.0, 1.0), in.vTexCoord.xy, in.vTexCoord.zw, texelScissor, texel0, renderToAtlas, tex0, tex1, texSampler).rb;
        groundSurrounding += tex2D(float2(-1.0, 1.0), in.vTexCoord.xy, in.vTexCoord.zw, texelScissor, texel0, renderToAtlas, tex0, tex1, texSampler).rb;
        groundSurrounding += tex2D(float2(-1.0, -1.0), in.vTexCoord.xy, in.vTexCoord.zw, texelScissor, texel0, renderToAtlas, tex0, tex1, texSampler).rb;
        groundSurrounding += tex2D(float2(1.0, -1.0), in.vTexCoord.xy, in.vTexCoord.zw, texelScissor, texel0, renderToAtlas, tex0, tex1, texSampler).rb;

        fragColor = tex1.sample(texSampler, in.vTexCoord.xy);

        if (groundSurrounding.x + fragColor.r == 5.0) {
            fragColor.r = 1.0;
        } else {
            fragColor.r = 0.93 - (groundSurrounding.y + fragColor.b) / 5.0;
        }

        return fragColor;
    } else if (radius > 8) {
        // Blur texture
        float2 blur = tex0.sample(texSampler, in.vTexCoord.xy + float2(1.0, 1.0) * texel0).rg;
        blur += tex0.sample(texSampler, in.vTexCoord.xy + float2(-1.0, 1.0) * texel0).rg;
        blur += tex0.sample(texSampler, in.vTexCoord.xy + float2(-1.0, -1.0) * texel0).rg;
        blur += tex0.sample(texSampler, in.vTexCoord.xy + float2(1.0, -1.0) * texel0).rg;

        fragColor = tex0.sample(texSampler, in.vTexCoord.xy);
        fragColor.r = step(5.0, blur.x + fragColor.r);
        fragColor.g = mix(fragColor.g, blur.y * 0.25, 0.4);
        return fragColor;
    }

    // Main distance calculation loop
    float maxValue = 0.0;
    float3 minDist = float3(1e9);

    for (float i = 0.0; i <= float(radius); i += 1.0) {
        float4 v1, v2;
        v1.x = tex2D(float2(-i, float(radius)), in.vTexCoord.xy, in.vTexCoord.zw, texelScissor, texel0, renderToAtlas, tex0, tex1, texSampler).g;
        v1.y = tex2D(float2(i, float(radius)), in.vTexCoord.xy, in.vTexCoord.zw, texelScissor, texel0, renderToAtlas, tex0, tex1, texSampler).g;
        v1.z = tex2D(float2(-i, float(-radius)), in.vTexCoord.xy, in.vTexCoord.zw, texelScissor, texel0, renderToAtlas, tex0, tex1, texSampler).g;
        v1.w = tex2D(float2(i, float(-radius)), in.vTexCoord.xy, in.vTexCoord.zw, texelScissor, texel0, renderToAtlas, tex0, tex1, texSampler).g;

        v2.x = tex2D(float2(float(radius), i), in.vTexCoord.xy, in.vTexCoord.zw, texelScissor, texel0, renderToAtlas, tex0, tex1, texSampler).g;
        v2.y = tex2D(float2(float(radius), -i), in.vTexCoord.xy, in.vTexCoord.zw, texelScissor, texel0, renderToAtlas, tex0, tex1, texSampler).g;
        v2.z = tex2D(float2(float(-radius), i), in.vTexCoord.xy, in.vTexCoord.zw, texelScissor, texel0, renderToAtlas, tex0, tex1, texSampler).g;
        v2.w = tex2D(float2(float(-radius), -i), in.vTexCoord.xy, in.vTexCoord.zw, texelScissor, texel0, renderToAtlas, tex0, tex1, texSampler).g;

        v1 = max(v1, v2);
        v1.xy = max(v1.xy, v1.zw);
        v1.x = max(v1.x, v1.y);
        v1.x = max(v1.x, maxValue);

        float2 dist = getDistRect(v1.x, float2(float(radius), i));

        if (sqlength(dist) < minDist.z) {
            maxValue = v1.x;
            minDist = float3(dist, sqlength(dist));
        }
    }

    float fDist = 1.0 - (min(res, sqrt(minDist.z)) / res);

    if (renderToAtlas) {
        fragColor = tex0.sample(texSampler, in.vTexCoord.xy);
    } else {
        fragColor = tex1.sample(texSampler, in.vTexCoord.xy);
    }

    fragColor.g = max(fragColor.g, fDist * fDist * fDist);

    return fragColor;
}
