#version 450

/* SSBO: mat3 matrix(48B) + int blend_mode(4B) + uint color(4B) + int image_mode(4B) + int filter_mode(4B) + int flags(4B) = 68B total */
layout(std430, set = 0, binding = 2) readonly buffer BlitParams {
    mat3  matrix;
    int   blend_mode;
    uint  color;
    int   image_mode;
    int   filter_mode;
    int   flags;
} params;

#define FLAG_OUTPUT_L8       1
#define FLAG_OUTPUT_A8       2
#define FLAG_NATIVE_BLEND    4
#define FLAG_SOURCE_A8       8
#define FLAG_SOURCE_INDEX8   16

layout(std430, set = 0, binding = 3) readonly buffer CLUT {
    uint colors[256];
} clut;

layout(set = 0, binding = 0) uniform sampler2D src_texture;
layout(set = 0, binding = 1) uniform sampler2D dst_texture;

layout(location = 0) in  vec2 frag_pos;
layout(location = 0) out vec4 out_color;

/* Blend mode enum values matching vg_lite_blend_t */
#define BLEND_NONE           0
#define BLEND_SRC_OVER       1
#define BLEND_DST_OVER       2
#define BLEND_SRC_IN         3
#define BLEND_DST_IN         4
#define BLEND_MULTIPLY       5
#define BLEND_SCREEN         6
#define BLEND_DARKEN         7
#define BLEND_LIGHTEN        8
#define BLEND_ADDITIVE       9
#define BLEND_SUBTRACT      10
#define BLEND_NORMAL_LVGL   11
#define BLEND_ADDITIVE_LVGL 12
#define BLEND_SUBTRACT_LVGL 13
#define BLEND_MULTIPLY_LVGL 14

/* OpenVG premultiplied blend modes */
#define OPENVG_BLEND_SRC       0x2000
#define OPENVG_BLEND_SRC_OVER  0x2001
#define OPENVG_BLEND_DST_OVER  0x2002
#define OPENVG_BLEND_SRC_IN    0x2003
#define OPENVG_BLEND_DST_IN    0x2004
#define OPENVG_BLEND_MULTIPLY  0x2005
#define OPENVG_BLEND_SCREEN    0x2006
#define OPENVG_BLEND_DARKEN    0x2007
#define OPENVG_BLEND_LIGHTEN   0x2008
#define OPENVG_BLEND_ADDITIVE  0x2009

/* Image mode values matching vg_lite_image_mode_t */
#define IMAGE_MODE_NORMAL   0x1F00
#define IMAGE_MODE_MULTIPLY 0x1F01
#define IMAGE_MODE_STENCIL  0x1F02
#define IMAGE_MODE_NONE     0x1F03
#define IMAGE_MODE_RECOLOR  0x1F04

vec4 apply_image_mode(vec4 src, uint mix_color)
{
    /* VGLite color is ABGR: A at bits 24-31, B at bits 16-23, G at bits 8-15, R at bits 0-7 */
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

vec4 blend_non_premul(vec4 S, vec4 D)
{
    float Sa = S.a;
    float Da = D.a;
    vec3  s  = S.rgb;
    vec3  d  = D.rgb;

    switch (params.blend_mode) {
    case BLEND_NONE:
        /* RGB: S + D*(1 - Sa), A: Sa + Da*(1 - Sa) - like SRC_OVER */
        return vec4(s + d * (1.0 - Sa), Sa + Da * (1.0 - Sa));

    case BLEND_SRC_OVER:
        /* RGB: S + D*(1 - Sa), A: Sa + Da*(1 - Sa) */
        return vec4(s + d * (1.0 - Sa), Sa + Da * (1.0 - Sa));

    case BLEND_DST_OVER:
        /* RGB: S*(1 - Da) + D, A: Sa*(1 - Da) + Da */
        return vec4(s * (1.0 - Da) + d, Sa * (1.0 - Da) + Da);

    case BLEND_SRC_IN:
        /* RGB: S*Da, A: Sa*Da */
        return vec4(s * Da, Sa * Da);

    case BLEND_DST_IN:
        /* RGB: D*Sa, A: Da*Sa */
        return vec4(d * Sa, Da * Sa);

    case BLEND_MULTIPLY:
        /* RGB: S*(1-Da) + D*(1-Sa) + S*D, A: Sa*(1-Da) + Da*(1-Sa) + Sa*Da */
        return vec4(s * (1.0 - Da) + d * (1.0 - Sa) + s * d,
                    Sa * (1.0 - Da) + Da * (1.0 - Sa) + Sa * Da);

    case BLEND_SCREEN:
        /* RGB: S + D - S*D, A: Sa + Da - Sa*Da */
        return vec4(s + d - s * d, Sa + Da - Sa * Da);

    case BLEND_DARKEN:
        /* RGB: min(SrcOver, DstOver), A: min(SrcOver, DstOver) */
    {
        vec3 so_rgb = s + d * (1.0 - Sa);
        vec3 do_rgb = s * (1.0 - Da) + d;
        float so_a  = Sa + Da * (1.0 - Sa);
        float do_a  = Sa * (1.0 - Da) + Da;
        return vec4(min(so_rgb, do_rgb), min(so_a, do_a));
    }

    case BLEND_LIGHTEN:
        /* RGB: max(SrcOver, DstOver), A: max(SrcOver, DstOver) */
    {
        vec3 so_rgb = s + d * (1.0 - Sa);
        vec3 do_rgb = s * (1.0 - Da) + d;
        float so_a  = Sa + Da * (1.0 - Sa);
        float do_a  = Sa * (1.0 - Da) + Da;
        return vec4(max(so_rgb, do_rgb), max(so_a, do_a));
    }

    case BLEND_ADDITIVE:
        /* RGB: S + D, A: Sa + Da */
        return vec4(s + d, Sa + Da);

    case BLEND_SUBTRACT:
        /* RGB: D*(1 - Sa), A: Da*(1 - Sa) */
        return vec4(d * (1.0 - Sa), Da * (1.0 - Sa));

    case BLEND_NORMAL_LVGL:
        /* RGB: S*Sa + D*(1 - Sa), A: 0xFF -> 1.0 */
        return vec4(s * Sa + d * (1.0 - Sa), 1.0);

    case BLEND_ADDITIVE_LVGL:
        /* RGB: (S + D)*Sa + D*(1 - Sa), A: 0xFF -> 1.0 */
        return vec4((s + d) * Sa + d * (1.0 - Sa), 1.0);

    case BLEND_SUBTRACT_LVGL:
        /* RGB: (S - D)*Sa + D*(1 - Sa), A: 0xFF -> 1.0 */
        return vec4((s - d) * Sa + d * (1.0 - Sa), 1.0);

    case BLEND_MULTIPLY_LVGL:
        /* RGB: (S*D)*Sa + D*(1 - Sa), A: 0xFF -> 1.0 */
        return vec4((s * d) * Sa + d * (1.0 - Sa), 1.0);

    default:
        return S;
    }
}

vec4 blend_premul(vec4 S, vec4 D)
{
    /* S and D are premultiplied: SP = S.rgb * S.a, DP = D.rgb * D.a
       But we receive non-premultiplied from texture, so compute premul ourselves. */
    vec3  SP = S.rgb * S.a;
    vec3  DP = D.rgb * D.a;
    float Sa = S.a;
    float Da = D.a;

    switch (params.blend_mode) {
    case OPENVG_BLEND_SRC:
        /* RGB: SP / Sa, A: Sa */
        return vec4(Sa > 0.0 ? SP / Sa : vec3(0.0), Sa);

    case OPENVG_BLEND_SRC_OVER:
    {
        /* RGB: (SP + DP*(1 - Sa)) / (Sa + Da*(1 - Sa)), A: (Sa + Da*(1 - Sa)) */
        float fa = Sa + Da * (1.0 - Sa);
        return vec4(fa > 0.0 ? (SP + DP * (1.0 - Sa)) / fa : vec3(0.0), fa);
    }

    case OPENVG_BLEND_DST_OVER:
    {
        /* RGB: (SP*(1 - Da) + DP) / (Sa*(1 - Da) + Da), A: (Sa*(1 - Da) + Da) */
        float fa = Sa * (1.0 - Da) + Da;
        return vec4(fa > 0.0 ? (SP * (1.0 - Da) + DP) / fa : vec3(0.0), fa);
    }

    case OPENVG_BLEND_SRC_IN:
    {
        /* RGB: (SP*Da) / (Sa*Da), A: (Sa*Da) */
        float fa = Sa * Da;
        return vec4(fa > 0.0 ? (SP * Da) / fa : vec3(0.0), fa);
    }

    case OPENVG_BLEND_DST_IN:
    {
        /* RGB: (DP*Sa) / (Sa*Da), A: (Sa*Da) */
        float fa = Sa * Da;
        return vec4(fa > 0.0 ? (DP * Sa) / fa : vec3(0.0), fa);
    }

    case OPENVG_BLEND_MULTIPLY:
    {
        /* RGB: (SP*DP + SP*(1-Da) + DP*(1-Sa)) / (Sa + Da*(1-Sa)), A: (Sa + Da*(1-Sa)) */
        float fa = Sa + Da * (1.0 - Sa);
        return vec4(fa > 0.0 ? (SP*DP + SP*(1.0-Da) + DP*(1.0-Sa)) / fa : vec3(0.0), fa);
    }

    case OPENVG_BLEND_SCREEN:
    {
        /* RGB: (SP + DP - (SP*DP)) / (Sa + Da*(1-Sa)), A: (Sa + Da*(1-Sa)) */
        float fa = Sa + Da * (1.0 - Sa);
        return vec4(fa > 0.0 ? (SP + DP - SP*DP) / fa : vec3(0.0), fa);
    }

    case OPENVG_BLEND_DARKEN:
    {
        /* RGB: (min(SP*Da, DP*Sa) + SP*(1-Da) + DP*(1-Sa)) / (Sa + Da*(1-Sa)), A: (Sa + Da*(1-Sa)) */
        float fa = Sa + Da * (1.0 - Sa);
        vec3 num = min(SP*Da, DP*Sa) + SP*(1.0-Da) + DP*(1.0-Sa);
        return vec4(fa > 0.0 ? num / fa : vec3(0.0), fa);
    }

    case OPENVG_BLEND_LIGHTEN:
    {
        /* RGB: (max(SP*Da, DP*Sa) + SP*(1-Da) + DP*(1-Sa)) / (Sa + Da*(1-Sa)), A: (Sa + Da*(1-Sa)) */
        float fa = Sa + Da * (1.0 - Sa);
        vec3 num = max(SP*Da, DP*Sa) + SP*(1.0-Da) + DP*(1.0-Sa);
        return vec4(fa > 0.0 ? num / fa : vec3(0.0), fa);
    }

    case OPENVG_BLEND_ADDITIVE:
    {
        /* RGB: (SP + DP) / (Sa + Da), A: (Sa + Da) */
        float fa = Sa + Da;
        return vec4(fa > 0.0 ? (SP + DP) / fa : vec3(0.0), fa);
    }

    default:
        return S;
    }
}

void main()
{
    /* params.matrix is already the inverse transform computed by vg_lite_blit:
     * shader_mat = Scale(1/src_w, 1/src_h) * inv(M) * Scale(dst_w, dst_h)
     * This maps frag_pos [0,1] -> src_uv [0,1] directly */
    vec3 src_coords = params.matrix * vec3(frag_pos, 1.0);
    vec2 src_uv;
    if (abs(src_coords.z - 1.0) < 0.001) {
        src_uv = src_coords.xy;
    } else {
        src_uv = src_coords.xy / src_coords.z;
    }

    bool native_blend = (params.flags & FLAG_NATIVE_BLEND) != 0;

    if (src_uv.x < -0.001 || src_uv.x > 1.001 || src_uv.y < -0.001 || src_uv.y > 1.001) {
        if (native_blend) {
            out_color = vec4(0.0, 0.0, 0.0, 0.0);
            return;
        }
        vec4 dst = texture(dst_texture, frag_pos);
        out_color = dst;
        return;
    }
    /* Clamp to edge like CPU expected buffer calculation */
    if (src_uv.x < 0.0) src_uv.x = 0.0;
    else if (src_uv.x > 1.0) src_uv.x = 1.0;
    if (src_uv.y < 0.0) src_uv.y = 0.0;
    else if (src_uv.y > 1.0) src_uv.y = 1.0;

    vec4 src = texture(src_texture, src_uv);
    
    if ((params.flags & FLAG_SOURCE_INDEX8) != 0) {
        float index_f = src.r * 255.0;
        uint index = uint(round(index_f));
        uint abgr = clut.colors[index];
        float a = float((abgr >> 24) & 0xFFu) / 255.0;
        float b = float((abgr >> 16) & 0xFFu) / 255.0;
        float g = float((abgr >>  8) & 0xFFu) / 255.0;
        float r = float((abgr      ) & 0xFFu) / 255.0;
        src = vec4(r, g, b, a);
    }
    else if (params.flags == 16) {
        src = vec4(1.0, 0.0, 0.0, 1.0);
    }
    
    src = apply_image_mode(src, params.color);

    if (native_blend) {
        if (params.blend_mode == BLEND_NORMAL_LVGL) {
            src = vec4(src.rgb * src.a, src.a);
        }
        if ((params.flags & FLAG_OUTPUT_L8) != 0) {
            float lum = 0.2126 * src.r + 0.7152 * src.g + 0.0722 * src.b;
            out_color = vec4(lum, 0.0, 0.0, src.a);
        } else if ((params.flags & FLAG_OUTPUT_A8) != 0) {
            out_color = vec4(src.a, 0.0, 0.0, src.a);
        } else {
            out_color = src;
        }
        return;
    }

    vec4 dst = texture(dst_texture, frag_pos);
    vec4 result;
    if (params.blend_mode >= OPENVG_BLEND_SRC) {
        result = blend_premul(src, dst);
    } else {
        result = blend_non_premul(src, dst);
    }
    result = clamp(result, 0.0, 1.0);

    if ((params.flags & FLAG_OUTPUT_L8) != 0) {
        float lum = 0.2126 * result.r + 0.7152 * result.g + 0.0722 * result.b;
        out_color = vec4(lum, 0.0, 0.0, 1.0);
    } else if ((params.flags & FLAG_OUTPUT_A8) != 0) {
        out_color = vec4(result.a, 0.0, 0.0, 1.0);
    } else {
        out_color = result;
    }
}
