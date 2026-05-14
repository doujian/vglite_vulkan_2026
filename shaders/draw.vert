#version 450

layout(push_constant) uniform DrawPushConstants {
    vec4  m0;  
    vec4  m1;
    vec4  m2;
    int   blend_mode;
    uint  color;
} pc;

layout(location = 0) in vec2 in_pos;
layout(location = 0) out vec4 vert_color;

vec4 unpackColor(uint c)
{
    float r = float((c      ) & 0xFFu) / 255.0;
    float g = float((c >>  8) & 0xFFu) / 255.0;
    float b = float((c >> 16) & 0xFFu) / 255.0;
    float a = float((c >> 24) & 0xFFu) / 255.0;
    return vec4(r, g, b, a);
}

void main()
{
    mat3 matrix = transpose(mat3(pc.m0.xyz, pc.m1.xyz, pc.m2.xyz));
    vec3 transformed = matrix * vec3(in_pos, 1.0);
    gl_Position = vec4(transformed.xy, 0.0, 1.0);
    vert_color = unpackColor(pc.color);
}