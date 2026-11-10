//! PASS tint
float4 tint(float2 uv)
{
    const float4 color = Source.Sample(Linear, uv);
    return float4(color.rgb * float3(1.0, 0.95, 0.9), color.a);
}
