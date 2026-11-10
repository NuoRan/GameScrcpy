//! PARAM strength float 0.35 0 1 0.05
//! PARAM lineHeight float 3 1 8 0.5
//! PASS scanlines
float4 scanlines(float2 uv)
{
    const float4 color = Source.Sample(Linear, uv);
    const float row = uv.y * OutputSize.y / lineHeight;
    const float shade = 1.0 - strength * (0.5 + 0.5 * cos(row * 6.28318530718));
    return float4(color.rgb * shade, color.a);
}
