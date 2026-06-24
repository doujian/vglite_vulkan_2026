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
    int   spread_mode;
} pc;

layout(set = 0, binding = 0) uniform sampler2D grad_texture;

layout(location = 0) in  vec2 grad_uv;
layout(location = 0) out vec4 out_color;

void main()
{
    /* Spread mode UV wrapping:
     *   0 (FILL) or 1 (PAD)    — clamp to edge
     *   2 (REPEAT)             — wrap with fract
     *   3 (REFLECT)            — mirror at boundaries */
    vec2 uv;
    if (pc.spread_mode <= 1) {
        uv = clamp(grad_uv, vec2(0.0), vec2(1.0));
    } else if (pc.spread_mode == 2) {
        uv = fract(grad_uv);
    } else {
        vec2 folded = mod(grad_uv, 2.0);
        uv = mix(folded, 2.0 - folded, step(1.0, folded));
    }
    out_color = texture(grad_texture, uv);
}
