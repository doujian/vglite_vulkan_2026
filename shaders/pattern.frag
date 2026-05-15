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
    int   pattern_width;
    int   pattern_height;
    int   blend_mode;
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
        /* REFLECT mode: mirror reflection
         * Formula: abs(mod(uv * 2, 2) - 1)
         * This creates a mirror effect at each boundary
         */
        vec2 uv2 = uv * 2.0;
        vec2 wrapped = fract(uv2);
        return abs(wrapped * 2.0 - 1.0);
    }
    return uv;
}

void main()
{
    vec2 final_uv = apply_pattern_mode(pattern_uv);
    
    /* For COLOR mode: check if outside pattern bounds */
    if (pc.pattern_mode == PATTERN_MODE_COLOR && !is_inside_pattern(pattern_uv)) {
        /* Outside bounds: use pattern_color from push constants */
        out_color = vert_color;
        return;
    }
    
    /* Sample pattern texture */
    vec4 pattern_color = texture(pattern_texture, final_uv);
    
    /* Apply blend mode (currently only NONE and SRC_OVER supported) */
    if (pc.blend_mode == BLEND_NONE) {
        out_color = pattern_color;
    } else {
        /* Default: just output pattern color */
        out_color = pattern_color;
    }
}