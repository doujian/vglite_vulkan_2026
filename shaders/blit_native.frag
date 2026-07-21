#version 450

layout(push_constant) uniform BlitParams {
    mat3  matrix;
    int   blend_mode;
    uint  color;
    int   image_mode;
    int   filter_mode;
    int   flags;
} params;

layout(set = 0, binding = 0) uniform sampler2D src_texture;

layout(location = 0) in  vec2 src_uv;   /* precomputed by vertex shader */
layout(location = 0) out vec4 out_color;

#define IMAGE_MODE_NORMAL   0x1F00
#define IMAGE_MODE_MULTIPLY 0x1F01
#define IMAGE_MODE_STENCIL  0x1F02
#define IMAGE_MODE_NONE     0x1F03
#define IMAGE_MODE_RECOLOR  0x1F04

#define FLAG_OUTPUT_L8       1
#define FLAG_OUTPUT_A8       2
#define FLAG_SOURCE_A8       8

vec4 apply_image_mode(vec4 src, uint mix_color)
{
    float mr = float((mix_color      ) & 0xFFu) / 255.0;
    float mg = float((mix_color >>  8) & 0xFFu) / 255.0;
    float mb = float((mix_color >> 16) & 0xFFu) / 255.0;
    float ma = float((mix_color >> 24) & 0xFFu) / 255.0;
    vec4 mix = vec4(mr, mg, mb, ma);

    if (params.image_mode == IMAGE_MODE_NONE) {
        return mix;
    }
    if (params.image_mode == IMAGE_MODE_MULTIPLY) {
        /* A8 source: swizzled to (0,0,0,alpha), multiply uses alpha * color */
        if ((params.flags & FLAG_SOURCE_A8) != 0) {
            return vec4(mix.rgb * src.a, mix.a * src.a);
        }
        return vec4(src.rgb * mix.rgb, src.a * mix.a);
    }
    if (params.image_mode == IMAGE_MODE_STENCIL) {
        return vec4(mix.rgb, src.a * mix.a);
    }
    if (params.image_mode == IMAGE_MODE_RECOLOR) {
        return vec4(mix.rgb * src.a, src.a);
    }
    return src;
}

void main()
{
    /* src_uv interpolated from vertex shader's (matrix * frag_pos).
     * 2D affine => z==1, interpolation is exact (linear transform commutes with interp). */

    if (src_uv.x < -0.001 || src_uv.x > 1.001 || src_uv.y < -0.001 || src_uv.y > 1.001) {
        discard;
    }
    /* UV clamp removed: sampler addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
     * texture() clamps to edge automatically — no per-fragment clamp needed. */

    vec4 src = texture(src_texture, src_uv);

    src = apply_image_mode(src, params.color);

    /* For A8/L8 targets (R8_UNORM), write the appropriate value to R channel.
     * Hardware blend uses src.A for blend factor, so we keep A = src.a for
     * SRC_OVER/ADDITIVE/SUBTRACT to work correctly on single-channel format. */
    if ((params.flags & FLAG_OUTPUT_L8) != 0) {
        float lum = 0.2126 * src.r + 0.7152 * src.g + 0.0722 * src.b;
        out_color = vec4(lum, 0.0, 0.0, src.a);
    } else if ((params.flags & FLAG_OUTPUT_A8) != 0) {
        out_color = vec4(src.a, 0.0, 0.0, src.a);
    } else {
        out_color = src;
    }
}
