// Draws one video picture into its content rectangle (PLAN 4.5). Compiled by fxc at build time:
// vsMain, and psMain with NV12 (decoder surfaces: luma + interleaved chroma) or PLANAR (software
// pictures: luma, Cb and Cr planes) defined. With sharpen.x above 0 the luma gets a clamped unsharp
// mask over its four neighbouring texels; chroma is left as it is.

cbuffer Parameters : register(b0)
{
    float4 corners[4];
    float4 toRgb[3];
    float4 extra;
    float4 sharpen;
    float4 crop;
};

struct Vertex
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

Vertex vsMain(uint id : SV_VertexID)
{
    Vertex vertex;
    vertex.position = corners[id];
    vertex.uv = (crop.xy + float2(id & 1, id >> 1) * crop.zw) * extra.yz;
    return vertex;
}

Vertex vsFullScreen(uint id : SV_VertexID)
{
    Vertex vertex;
    const float2 uv = float2((id << 1) & 2, id & 2);
    vertex.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    vertex.uv = uv;
    return vertex;
}

SamplerState bilinear : register(s0);

#if defined(COPY)
Texture2D<float4> image : register(t0);

float4 psCopy(Vertex input) : SV_Target
{
    return float4(image.Sample(bilinear, input.uv).rgb * extra.x, extra.x);
}
#else
Texture2DArray<float> luma : register(t0);
#if defined(NV12)
Texture2DArray<float2> chroma : register(t1);
#else
Texture2DArray<float> cb : register(t1);
Texture2DArray<float> cr : register(t2);
#endif

float neighbour(float2 uv, float2 offset)
{
    const float2 last = extra.yz - 0.5 * sharpen.yz;
    return luma.SampleLevel(bilinear, float3(min(uv + offset, last), 0.0), 0.0);
}

float lumaAt(float2 uv)
{
    float y = luma.Sample(bilinear, float3(uv, 0.0));
    [branch] if (sharpen.x > 0.0) {
        const float left = neighbour(uv, float2(-sharpen.y, 0.0));
        const float right = neighbour(uv, float2(sharpen.y, 0.0));
        const float up = neighbour(uv, float2(0.0, -sharpen.z));
        const float down = neighbour(uv, float2(0.0, sharpen.z));
        const float lowest = min(y, min(min(left, right), min(up, down)));
        const float highest = max(y, max(max(left, right), max(up, down)));
        const float blur = (left + right + up + down) * 0.25;
        y = clamp(y + sharpen.x * 2.0 * (y - blur), lowest, highest);
    }
    return y;
}

float4 psMain(Vertex input) : SV_Target
{
    const float3 position = float3(input.uv, 0.0);
#if defined(NV12)
    const float3 yuv = float3(lumaAt(input.uv), chroma.Sample(bilinear, position));
#else
    const float3 yuv = float3(lumaAt(input.uv), cb.Sample(bilinear, position),
                              cr.Sample(bilinear, position));
#endif
    const float3 rgb = saturate(float3(dot(toRgb[0].xyz, yuv) + toRgb[0].w,
                                       dot(toRgb[1].xyz, yuv) + toRgb[1].w,
                                       dot(toRgb[2].xyz, yuv) + toRgb[2].w));
    return float4(rgb * extra.x, extra.x);
}
#endif
