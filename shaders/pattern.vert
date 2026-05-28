#version 450

/* Push constants for pattern fill:
 * - path_matrix: transforms path vertices to screen space (mat3)
 * - pattern_matrix: transforms screen position to pattern UV (mat3)
 * - pattern_mode: COLOR(0), PAD(1), REPEAT(2), REFLECT(3)
 * - pattern_color: color for COLOR mode (uint ARGB)
 * - pattern_width/height: pattern image dimensions
 * - blend_mode: blend mode enum
 */
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
} pc;

layout(location = 0) in vec2 in_pos;

/* Output to fragment shader:
 * - screen_pos: normalized screen position [0,1]
 * - pattern_uv: pattern texture coordinates (may be outside [0,1])
 * - vert_color: unpacked pattern_color for COLOR mode fallback
 */
layout(location = 0) out vec2 screen_pos;
layout(location = 1) out vec2 pattern_uv;
layout(location = 2) out vec4 vert_color;

vec4 unpackColorARGB(uint c)
{
    /* VGLite color format: 0xAARRGGBB */
    float a = float((c >> 24) & 0xFFu) / 255.0;
    float r = float((c >> 16) & 0xFFu) / 255.0;
    float g = float((c >>  8) & 0xFFu) / 255.0;
    float b = float((c      ) & 0xFFu) / 255.0;
    return vec4(r, g, b, a);
}

void main()
{
    vec3 transformed = pc.path_matrix * vec3(in_pos, 1.0);
    gl_Position = vec4(transformed.xy, 0.0, 1.0);
    
    vec2 screen_pos_norm = (transformed.xy + vec2(1.0)) * 0.5;
    vec2 screen_pos_pixel = screen_pos_norm * vec2(pc.target_width, pc.target_height);
    
    vec3 pattern_coords = pc.pattern_matrix * vec3(screen_pos_pixel, 1.0);
    pattern_uv = pattern_coords.xy / pattern_coords.z;
    
    vert_color = unpackColorARGB(pc.pattern_color);
}