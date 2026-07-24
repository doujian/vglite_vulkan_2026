#version 450

/* Fullscreen blit fragment shader — same as blit_native.frag but with
 * UV discard for out-of-bounds samples. The fullscreen triangle covers
 * the entire target, so we need to discard fragments where the source
 * UV falls outside [0,1]. OBB path uses blit_native.frag (no discard)
 * because the quad geometry already limits coverage. */

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
    /* Discard fragments outside source texture bounds — fullscreen triangle
     * covers entire target, so UV can be far outside [0,1]. */
    if (src_uv.x < -0.001 || src_uv.x > 1.001 ||
        src_uv.y < -0.001 || src_uv.y > 1.001)
        discard;

    vec4 src = texture(src_texture, src_uv);

    src = apply_image_mode(src, params.color);

    out_color = src;
}
