#version 450

/* Pattern mode constants */
#define PATTERN_MODE_COLOR   0
#define PATTERN_MODE_PAD     1
#define PATTERN_MODE_REPEAT  2
#define PATTERN_MODE_REFLECT 3

/* Blend mode constants (same as blit.frag) */
#define BLEND_NONE           0
#define BLEND_SRC_OVER       1

layout(push_constant) uniform PatternPushConstants {
    mat3  path_matrix;
    mat3  pattern_matrix;
    int   pattern_mode;
    uint  pattern_color;
    int   target_width;
    int   target_height;
    int   pattern_width;
    int   pattern_height;
    int   blend_mode;
    uint  color;
} pc;

layout(set = 0, binding = 0) uniform sampler2D pattern_texture;

layout(location = 0) in  vec2 screen_pos;
layout(location = 1) in  vec2 pattern_uv;
layout(location = 2) in  vec4 vert_color;
layout(location = 0) out vec4 out_color;

/* Check if UV is within pattern bounds [0,1] */
bool is_inside_pattern(vec2 uv)
{
    return uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0;
}

/* Apply pattern mode to get final UV */
vec2 apply_pattern_mode(vec2 uv)
{
    if (pc.pattern_mode == PATTERN_MODE_COLOR) {
        /* COLOR mode: use UV directly, fragment shader will check bounds */
        return uv;
    }
    else if (pc.pattern_mode == PATTERN_MODE_PAD) {
        /* PAD mode: clamp to edge */
        return clamp(uv, vec2(0.0), vec2(1.0));
    }
    else if (pc.pattern_mode == PATTERN_MODE_REPEAT) {
        /* REPEAT mode: wrap/tiling */
        return fract(uv);
    }
    else if (pc.pattern_mode == PATTERN_MODE_REFLECT) {
        /* REFLECT mode: mirror reflection at each integer boundary.
         * [0,1] → identity, [1,2] → 2-uv, [2,3] → uv-2, ...
         * Equivalent to VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT.
         */
        vec2 m = mod(uv, 2.0);
        return mix(m, 2.0 - m, step(1.0, m));
    }
    return uv;
}

/* vg_lite_color_t = 0xAABBGGRR: R in the lower 8 bits, then G, B, alpha in
 * the upper 8 bits (same helper as pattern.vert). */
vec4 unpackColorARGB(uint c)
{
    float a = float((c >> 24) & 0xFFu) / 255.0;
    float b = float((c >> 16) & 0xFFu) / 255.0;
    float g = float((c >>  8) & 0xFFu) / 255.0;
    float r = float((c      ) & 0xFFu) / 255.0;
    return vec4(r, g, b, a);
}

void main()
{
    vec2 final_uv = apply_pattern_mode(pattern_uv);

    /* For COLOR mode: check if outside pattern bounds */
    if (pc.pattern_mode == PATTERN_MODE_COLOR && !is_inside_pattern(pattern_uv)) {
        /* Outside bounds: use pattern_color from push constants (tint does
         * not apply to the COLOR-mode fallback) */
        out_color = vert_color;
        return;
    }

    /* Sample pattern texture */
    vec4 pattern_color = texture(pattern_texture, final_uv);

    /* Tint: API (color) modulates the sampled texel. 0 = identity
     * passthrough — legacy callers pass 0 and expect the untinted texel
     * (compatibility baseline, 0 is NOT transparent black). */
    out_color = (pc.color != 0u) ? pattern_color * unpackColorARGB(pc.color)
                                 : pattern_color;
}