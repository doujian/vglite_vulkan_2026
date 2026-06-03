#version 450

layout(std430, set = 0, binding = 1) readonly buffer BlitParams {
    mat3  matrix;
    int   blend_mode;
    uint  color;
    int   image_mode;
    int   filter_mode;
    int   flags;
} params;

#define FLAG_SOURCE_A8       8

layout(set = 0, binding = 0) uniform sampler2D src_texture;

layout(location = 0) in  vec2 frag_pos;
layout(location = 0) out vec4 out_color;

#define IMAGE_MODE_NORMAL   0x1F00
#define IMAGE_MODE_MULTIPLY 0x1F01
#define IMAGE_MODE_STENCIL  0x1F02

vec4 apply_image_mode(vec4 src, uint mix_color)
{
    float mr = float((mix_color      ) & 0xFFu) / 255.0;
    float mg = float((mix_color >>  8) & 0xFFu) / 255.0;
    float mb = float((mix_color >> 16) & 0xFFu) / 255.0;
    float ma = float((mix_color >> 24) & 0xFFu) / 255.0;
    vec4 mix = vec4(mr, mg, mb, ma);

    if (params.image_mode == IMAGE_MODE_MULTIPLY) {
        if ((params.flags & FLAG_SOURCE_A8) != 0) {
            return vec4(mix.rgb * src.a, mix.a * src.a);
        }
        return vec4(src.rgb * mix.rgb, src.a * mix.a);
    }
    if (params.image_mode == IMAGE_MODE_STENCIL) {
        return vec4(mix.rgb, src.a * mix.a);
    }
    return src;
}

void main()
{
    vec3 src_coords = params.matrix * vec3(frag_pos, 1.0);
    vec2 src_uv;
    if (abs(src_coords.z - 1.0) < 0.001) {
        src_uv = src_coords.xy;
    } else {
        src_uv = src_coords.xy / src_coords.z;
    }

    if (src_uv.x < -0.001 || src_uv.x > 1.001 || src_uv.y < -0.001 || src_uv.y > 1.001) {
        out_color = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }
    if (src_uv.x < 0.0) src_uv.x = 0.0;
    else if (src_uv.x > 1.0) src_uv.x = 1.0;
    if (src_uv.y < 0.0) src_uv.y = 0.0;
    else if (src_uv.y > 1.0) src_uv.y = 1.0;

    vec4 src = texture(src_texture, src_uv);

    src = apply_image_mode(src, params.color);

    out_color = src;
}
