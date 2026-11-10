#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 area;
    vec2 shift;
    float spacing;
    float radius;
    vec4 tint;
};

void main()
{
    vec2 point = qt_TexCoord0 * area + shift;
    vec2 cell = mod(point, spacing) - 0.5 * spacing;
    float coverage = clamp(radius + 0.5 - length(cell), 0.0, 1.0);
    fragColor = tint * (coverage * qt_Opacity);
}
