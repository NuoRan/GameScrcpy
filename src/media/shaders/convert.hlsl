cbuffer Scale : register(b0)
{
    float4 visible;
};

struct Vertex
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

Vertex vsQuad(uint id : SV_VertexID)
{
    Vertex vertex;
    const float2 uv = float2(id & 1, id >> 1);
    vertex.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    vertex.uv = uv;
    return vertex;
}

#if defined(YUY2)
Texture2D<float4> packed : register(t0);

float psLuma(Vertex input) : SV_Target
{
    const uint2 at = uint2(input.position.xy);
    const float4 pair = packed.Load(int3(int(at.x >> 1), int(at.y), 0));
    return (at.x & 1) != 0 ? pair.b : pair.r;
}

float2 psChroma(Vertex input) : SV_Target
{
    uint width;
    uint height;
    packed.GetDimensions(width, height);
    const uint2 at = uint2(input.position.xy);
    const uint upper = min(at.y * 2, height - 1);
    const uint lower = min(at.y * 2 + 1, height - 1);
    const float4 first = packed.Load(int3(int(at.x), int(upper), 0));
    const float4 second = packed.Load(int3(int(at.x), int(lower), 0));
    return float2(first.g + second.g, first.a + second.a) * 0.5;
}
#else
Texture2DArray<float> luma : register(t0);
Texture2DArray<float2> chroma : register(t1);
SamplerState bilinear : register(s0);

float psLuma(Vertex input) : SV_Target
{
    return luma.SampleLevel(bilinear, float3(input.uv * visible.xy, 0.0), 0.0);
}

float2 psChroma(Vertex input) : SV_Target
{
    return chroma.SampleLevel(bilinear, float3(input.uv * visible.xy, 0.0), 0.0);
}
#endif
