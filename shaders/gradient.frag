#version 450

/* Gradient fragment shader (push constant version).
 * Samples gradient texture using PAD (clamp-to-edge).
 */

layout(push_constant) uniform GradPushConstants {
    mat3  path_matrix;
    mat3  grad_matrix;
    int   target_width;
    int   target_height;
    int   grad_width;
    int   grad_height;
} pc;

layout(set = 0, binding = 0) uniform sampler2D grad_texture;

layout(location = 0) in  vec2 grad_uv;
layout(location = 0) out vec4 out_color;

void main()
{
    vec2 uv = clamp(grad_uv, vec2(0.0), vec2(1.0));
    out_color = texture(grad_texture, uv);
}
