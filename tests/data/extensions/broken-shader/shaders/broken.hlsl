//! PASS broken
float4 broken(float2 uv)
{
    const float4 color = Source.Sample(Linear, uv);
    return float4(color.rgb * undeclaredFactor, color.a);
}
