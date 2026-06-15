#version 450

/* Gradient vertex shader (push constant version).
 * Stencil pass: path vertices -> NDC via path_matrix.
 * Cover pass: full-screen quad (path_matrix = identity), compute grad_uv from screen pos.
 */

layout(push_constant) uniform GradPushConstants {
    mat3  path_matrix;
    mat3  grad_matrix;
    int   target_width;
    int   target_height;
    int   grad_width;
    int   grad_height;
} pc;

layout(location = 0) in vec2 in_pos;

layout(location = 0) out vec2 grad_uv;

void main()
{
    vec3 transformed = pc.path_matrix * vec3(in_pos, 1.0);
    gl_Position = vec4(transformed.xy, 0.0, 1.0);

    vec2 screen_norm = (transformed.xy + vec2(1.0)) * 0.5;
    vec2 screen_pixel = screen_norm * vec2(pc.target_width, pc.target_height);

    vec3 grad_coords = pc.grad_matrix * vec3(screen_pixel, 1.0);
    grad_uv = grad_coords.xy / grad_coords.z;
}
