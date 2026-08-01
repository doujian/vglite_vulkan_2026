#version 450

/* Push constants for radial gradient fill (stencil + cover dual-pass).
 * Layout mirrors pattern.vert (124B) but the second mat3 carries the nine
 * radial-gradient coefficients instead of a pattern matrix.
 *
 * - path_matrix:  transforms path vertices to NDC (mat3)
 * - radial_coef:  nine gLin/gRad coefficients packed as column-major mat3
 *                    col0 = (StepXLin,    StepYLin,    ConstantLin)
 *                    col1 = (StepXXRad,   StepYYRad,   StepXYRad)
 *                    col2 = (StepXRad,    StepYRad,    ConstantRad)
 * - spread_mode:  FILL=0, PAD=1, REPEAT=2, REFLECT=3
 * - paint_color:  reserved (unused by radial)
 * - target_width/height: render target dimensions in pixels
 * - lut_width/lut_height: 1D ramp LUT texture dims (height == 1)
 * - blend_mode:   blend mode enum
 */
layout(push_constant) uniform RadialPushConstants {
    mat3  path_matrix;
    mat3  radial_coef;
    int   spread_mode;
    uint  paint_color;
    int   target_width;
    int   target_height;
    int   lut_width;
    int   lut_height;
    int   blend_mode;
} pc;

layout(location = 0) in vec2 in_pos;

/* screen_pos_pixel: pixel-space coordinate (origin top-left, +y down) passed
 * to the fragment shader so it can evaluate g = gLin + sqrt(gRad) without
 * relying on gl_FragCoord (whose y orientation is implementation-defined). */
layout(location = 0) out vec2 screen_pos_pixel;

void main()
{
    vec3 transformed = pc.path_matrix * vec3(in_pos, 1.0);
    gl_Position = vec4(transformed.xy, 0.0, 1.0);

    /* path_screen_to_ndc flips y (screen y=0/top -> NDC y=+1). Undo the flip
     * so screen_pos_pixel uses the same top-left-origin, +y-down convention as
     * the CPU reference (util.c uses psx=x+0.5, psy=y+0.5 directly) and as the
     * radial coefficients computed in vg_lite_draw_radial_grad (which assume
     * screen pixel (0,0) is the top-left). Without this, the focal point at
     * screen (fx,fy) would appear at spp (fx, height-fy) in the shader and the
     * g computation would be wrong. */
    vec2 screen_pos_norm = vec2(transformed.x + 1.0, 1.0 - transformed.y) * 0.5;
    screen_pos_pixel = screen_pos_norm * vec2(pc.target_width, pc.target_height);
}
