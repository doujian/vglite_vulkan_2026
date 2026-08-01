#version 450

/* Radial gradient fragment shader (cover pass).
 *
 * Evaluates the OpenVG radial gradient formula per pixel:
 *   g = gLin + sqrt(gRad)
 * where gLin and gRad are quadratic polynomials in pixel coordinates whose
 * nine coefficients are supplied via push constant (packed in radial_coef).
 * g is the normalized gradient position: g == 0 at focal, g == 1 at the
 * radius edge. Spread (PAD/REPEAT/REFLECT/FILL) is applied in this [0,1]
 * normalized domain, then the 1D ramp LUT is sampled at the resulting u.
 *
 * This matches the official gpu-vglite hardware semantics (rad_tile register)
 * and ThorVG's _clamp implementation, where spread operates around g == 1.0
 * (the radius boundary), NOT around the last ramp stop offset.
 */

/* Spread mode constants (match VG_LITE_RADIAL_GRAD_SPREAD_* enums) */
#define SPREAD_FILL     0
#define SPREAD_PAD      1
#define SPREAD_REPEAT   2
#define SPREAD_REFLECT  3

layout(push_constant) uniform RadialPushConstants {
    mat3  path_matrix;
    mat3  radial_coef;       /* column-major:
                              * col0 = (StepXLin,    StepYLin,    ConstantLin)
                              * col1 = (StepXXRad,   StepYYRad,   StepXYRad)
                              * col2 = (StepXRad,    StepYRad,    ConstantRad) */
    int   spread_mode;
    uint  paint_color;
    int   target_width;
    int   target_height;
    int   lut_width;
    int   lut_height;
    int   blend_mode;
} pc;

layout(set = 0, binding = 0) uniform sampler2D radial_lut;   /* 1D ramp LUT (height == 1) */

layout(location = 0) out vec4 out_color;

void main()
{
    /* gl_FragCoord.xy is the pixel-center coordinate (x+0.5, y+0.5) in
     * framebuffer space, which in Vulkan has the same top-left origin and
     * +y-down orientation as the CPU reference (util.c sx=x+0.5, sy=y+0.5).
     * This avoids the NDC y-flip issue that plagues vert-derived varyings. */
    vec2 px = gl_FragCoord.xy;

    /* Unpack the nine coefficients (mat3 is column-major: [col][row]). */
    float stepXLin    = pc.radial_coef[0][0];
    float stepYLin    = pc.radial_coef[0][1];
    float constantLin = pc.radial_coef[0][2];
    float stepXXRad   = pc.radial_coef[1][0];
    float stepYYRad   = pc.radial_coef[1][1];
    float stepXYRad   = pc.radial_coef[1][2];
    float stepXRad    = pc.radial_coef[2][0];
    float stepYRad    = pc.radial_coef[2][1];
    float constantRad = pc.radial_coef[2][2];

    /* gLin: linear part. gRad: quadratic part under the sqrt. */
    float gLin = px.x * stepXLin + px.y * stepYLin + constantLin;
    float gRad = px.x * px.x * stepXXRad
               + px.y * px.y * stepYYRad
               + px.x * px.y * stepXYRad
               + px.x * stepXRad
               + px.y * stepYRad
               + constantRad;

    /* gRad can go slightly negative due to floating-point error when the
     * pixel sits exactly on a locus where gRad == 0 analytically. Clamp to
     * zero and fall back to gLin only (sqrt(0) == 0). */
    float g;
    if (gRad < 0.0f) {
        g = gLin;
    } else {
        g = gLin + sqrt(gRad);
    }

    /* Apply spread in the [0,1] normalized domain (g == 1.0 == radius edge).
     * FILL is treated as PAD to match ThorVG (vg_lite_tvg.cpp fill_spread_conv
     * maps VG_LITE_GRADIENT_SPREAD_FILL -> FillSpread::Pad). */
    float u;
    if (pc.spread_mode == SPREAD_PAD || pc.spread_mode == SPREAD_FILL) {
        u = clamp(g, 0.0, 1.0);
    } else if (pc.spread_mode == SPREAD_REPEAT) {
        u = fract(g);
    } else if (pc.spread_mode == SPREAD_REFLECT) {
        float m = mod(g, 2.0);
        u = mix(m, 2.0 - m, step(1.0, m));
    } else {
        u = clamp(g, 0.0, 1.0);
    }

    /* Sample the 1D ramp LUT. Texture is width x 1; v = 0.5 samples the single
     * row. LINEAR filtering gives smooth interpolation between LUT entries. */
    out_color = texture(radial_lut, vec2(u, 0.5));
}
