/*
 * test_fillrule_pattern - vg_lite_draw_pattern fill_rule (EVEN_ODD / NON_ZERO)
 * and color (tint) parameter verification.
 *
 * Setup: 256x256 BGRA8888 target, 8x8 solid-red BGRA8888 source image,
 * VG_LITE_PATTERN_PAD (solid source -> every UV gives the same texel, so the
 * fill color is uniform regardless of the pattern matrix), VG_LITE_BLEND_NONE,
 * identity pattern matrix. A uniform fill reduces each frame to a pure
 * fill-rule / tint question.
 *
 * Frames:
 *   1. two overlapping rects, SAME winding   - EVEN_ODD : overlap hollow
 *                                              NON_ZERO : overlap filled
 *      (the minimal case where the two rules diverge)
 *   2. donut (outer rect + inner rect with OPPOSITE winding) - both rules
 *      hollow the center; also calibrates the NON_ZERO front/back INCR/DECR
 *      direction (a reversed sign would fill the center and hollow the ring)
 *   3. triple nested rects, SAME winding     - EVEN_ODD : ring1 fill, ring2
 *      hollow, core fill (parity 1/2/3); NON_ZERO : all filled (winding 1/2/3)
 *   4. donut + mirror matrix (scale(-1,1)) NON_ZERO - winding sign must
 *      survive the negative determinant (front/back ops stay symmetric)
 *   5. tint: three rects, color = 0 (legacy identity), 0xFFFFFFFF (identity),
 *      0xFF808080 (half) - pattern.frag multiplies the texel
 *
 * Verification: analytic point sampling of pixels >=16px away from any edge
 * (4x MSAA only affects boundary pixels, sampled interiors are exact).
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "vg_lite.h"
#include "vg_lite_util.h"

#define W 256
#define H 256

/* VLC S16 path stream: every slot (opcode and coordinate) is one int16.
 * Opcodes: 2 = MOVE, 4 = LINE, 1 = CLOSE, 0 = END. */

/* Frame 1/2: two overlapping rects [32,32]-[160,224] and [96,32]-[224,224],
 * SAME winding (both listed in screen-CW order under y-down). */
static int16_t path_overlap[] = {
    2,  32,  32,    4, 160,  32,    4, 160, 224,    4,  32, 224,   1,
    2,  96,  32,    4, 224,  32,    4, 224, 224,    4,  96, 224,   1,
    0
};

/* Donut: outer [32,32]-[224,224] CW, inner OPPOSITE winding (truly reversed
 * traversal: TL->BL->BR->TR vs outer's TL->TR->BR->BL - rotating the start
 * vertex alone would NOT flip the winding). Fill semantics are
 * direction-agnostic, only the relative winding of outer vs inner matters. */
static int16_t path_donut[] = {
    2,  32,  32,    4, 224,  32,    4, 224, 224,    4,  32, 224,   1,
    2,  96,  96,    4,  96, 160,    4, 160, 160,    4, 160,  96,   1,
    0
};

/* Frame 4: asymmetric donut - inner [64,96]-[160,160], so the mirror is
 * observable (mirrored hollow band x in [96,192]). */
static int16_t path_donut_asym[] = {
    2,  32,  32,    4, 224,  32,    4, 224, 224,    4,  32, 224,   1,
    2,  64,  96,    4,  64, 160,    4, 160, 160,    4, 160,  96,   1,
    0
};

/* Frame 3: triple nested rects [32,32]-[224,224], [64,64]-[192,192],
 * [96,96]-[160,160], SAME winding. */
static int16_t path_triple[] = {
    2,  32,  32,    4, 224,  32,    4, 224, 224,    4,  32, 224,   1,
    2,  64,  64,    4, 192,  64,    4, 192, 192,    4,  64, 192,   1,
    2,  96,  96,    4, 160,  96,    4, 160, 160,    4,  96, 160,   1,
    0
};

/* Frame 5: one small rect [32,32]-[96,96] (tint frame draws three of these
 * at different x offsets in separate draw calls). */
static int16_t path_rect_small[] = {
    2,  32,  32,    4,  96,  32,    4,  96,  96,    4,  32,  96,   1,
    0
};

static vg_lite_buffer_t target_buf, src_img;

static vg_lite_path_t make_path(const int16_t *data, int bytes,
                                float x0, float y0, float x1, float y1)
{
    vg_lite_path_t p;
    memset(&p, 0, sizeof(p));
    p.bounding_box[0] = x0; p.bounding_box[1] = y0;
    p.bounding_box[2] = x1; p.bounding_box[3] = y1;
    p.quality = VG_LITE_HIGH;
    p.format = VG_LITE_S16;
    p.path_length = bytes;
    p.path = (vg_lite_pointer)data;
    p.path_changed = 1;
    return p;
}

static void cleanup(void)
{
    if (target_buf.handle) vg_lite_free(&target_buf);
    if (src_img.handle) vg_lite_free(&src_img);
    vg_lite_close();
}

/* Expected pixel values (BGRA8888 byte order: B, G, R, A).
 * Background: clear 0xFF3020A0 (0xAABBGGRR) -> B=0x30 G=0x20 R=0xA0 A=0xFF.
 * Fill: solid-red source texel -> B=0x00 G=0x00 R=0xFF A=0xFF. */
static const uint8_t COL_BG[4]   = { 0x30, 0x20, 0xA0, 0xFF };
static const uint8_t COL_FILL[4] = { 0x00, 0x00, 0xFF, 0xFF };

static int check_point(const uint8_t *base, uint32_t stride,
                       int x, int y, const uint8_t want[4], int tol,
                       const char *tag)
{
    const uint8_t *px = base + (uint32_t)y * stride + (uint32_t)x * 4;
    for (int c = 0; c < 4; c++) {
        int d = (int)px[c] - (int)want[c];
        if (d < -tol || d > tol) {
            printf("FAIL [%s] px(%d,%d) ch%d: got %u want %u (tol %d)\n",
                   tag, x, y, c, px[c], want[c], tol);
            return 1;
        }
    }
    return 0;
}

/* Clear + draw_pattern + finish, then verify the given sample points. */
static int draw_and_check(vg_lite_path_t *path, vg_lite_fill_t rule,
                          vg_lite_matrix_t *pmat, vg_lite_color_t color,
                          const char *name, const char *tag)
{
    vg_lite_error_t err = vg_lite_clear(&target_buf, NULL, 0xFF3020A0);
    if (err != VG_LITE_SUCCESS) { printf("clear failed: %d\n", err); return 1; }

    vg_lite_matrix_t ident;
    vg_lite_identity(&ident);
    err = vg_lite_draw_pattern(&target_buf, path, rule, pmat ? pmat : &ident,
                               &src_img, &ident, VG_LITE_BLEND_NONE,
                               VG_LITE_PATTERN_PAD, 0, color,
                               VG_LITE_FILTER_POINT);
    if (err != VG_LITE_SUCCESS) {
        printf("FAIL [%s] draw_pattern error %d\n", tag, err);
        return 1;
    }
    vg_lite_finish();

    char png[64];
    snprintf(png, sizeof(png), "fillrule_%s.png", name);
    vg_lite_save_png(png, &target_buf);

    const uint8_t *base = (const uint8_t *)vg_lite_buffer_read_ptr(&target_buf);
    if (!base) { printf("FAIL [%s] read_ptr NULL\n", tag); return 1; }
    return 0; /* caller appends point checks via check_point */
}

int main(void)
{
    vg_lite_error_t error;
    int fails = 0;

    error = vg_lite_init(W, H);
    if (error != VG_LITE_SUCCESS) {
        printf("vg_lite_init failed: %d\n", error);
        return -1;
    }

    /* 8x8 solid-red source (BGRA8888: B=0 G=0 R=255 A=255).
     * vg_lite_buffer_write expects input already padded to buffer->stride
     * (same contract as vg_lite_load_raw), so build a stride-sized blob. */
    memset(&src_img, 0, sizeof(src_img));
    src_img.width = 8; src_img.height = 8;
    src_img.format = VG_LITE_BGRA8888;
    error = vg_lite_allocate(&src_img);
    if (error != VG_LITE_SUCCESS) { printf("src alloc failed: %d\n", error); cleanup(); return -1; }
    {
        uint8_t *red = (uint8_t *)calloc((size_t)src_img.stride * src_img.height, 1);
        if (!red) { printf("src temp alloc failed\n"); cleanup(); return -1; }
        for (uint32_t y = 0; y < src_img.height; y++) {
            uint8_t *row = red + (size_t)y * src_img.stride;
            for (uint32_t x = 0; x < src_img.width; x++) {
                row[x * 4 + 0] = 0x00; row[x * 4 + 1] = 0x00;
                row[x * 4 + 2] = 0xFF; row[x * 4 + 3] = 0xFF;
            }
        }
        error = vg_lite_buffer_write(&src_img, red);
        free(red);
        if (error != VG_LITE_SUCCESS) { printf("src write failed: %d\n", error); cleanup(); return -1; }
    }

    memset(&target_buf, 0, sizeof(target_buf));
    target_buf.width = W; target_buf.height = H;
    target_buf.format = VG_LITE_BGRA8888;
    error = vg_lite_allocate(&target_buf);
    if (error != VG_LITE_SUCCESS) { printf("target alloc failed: %d\n", error); cleanup(); return -1; }

    const uint8_t *base = NULL;
    uint32_t stride = target_buf.stride;
    vg_lite_matrix_t ident, mirror;
    vg_lite_identity(&ident);

    /* ---- Frame 1: overlap, EVEN_ODD -> overlap region hollow ---- */
    {
        vg_lite_path_t p = make_path(path_overlap, sizeof(path_overlap), 32, 32, 224, 224);
        if (draw_and_check(&p, VG_LITE_FILL_EVEN_ODD, NULL, 0, "eo_overlap", "f1")) { fails++; goto next1; }
        base = (const uint8_t *)vg_lite_buffer_read_ptr(&target_buf);
        fails += check_point(base, stride,  64, 128, COL_FILL, 0, "f1 R1-only");
        fails += check_point(base, stride, 128, 128, COL_BG,   0, "f1 overlap=hollow");
        fails += check_point(base, stride, 192, 128, COL_FILL, 0, "f1 R2-only");
        fails += check_point(base, stride,  16,  16, COL_BG,   0, "f1 outside");
    }
next1:

    /* ---- Frame 2: overlap, NON_ZERO -> overlap filled ---- */
    {
        vg_lite_path_t p = make_path(path_overlap, sizeof(path_overlap), 32, 32, 224, 224);
        if (draw_and_check(&p, VG_LITE_FILL_NON_ZERO, NULL, 0, "nz_overlap", "f2")) { fails++; goto next2; }
        base = (const uint8_t *)vg_lite_buffer_read_ptr(&target_buf);
        fails += check_point(base, stride,  64, 128, COL_FILL, 0, "f2 R1-only");
        fails += check_point(base, stride, 128, 128, COL_FILL, 0, "f2 overlap=filled");
        fails += check_point(base, stride, 192, 128, COL_FILL, 0, "f2 R2-only");
        fails += check_point(base, stride,  16,  16, COL_BG,   0, "f2 outside");
    }
next2:

    /* ---- Frame 3: donut, EVEN_ODD -> ring filled, center hollow ---- */
    {
        vg_lite_path_t p = make_path(path_donut, sizeof(path_donut), 32, 32, 224, 224);
        if (draw_and_check(&p, VG_LITE_FILL_EVEN_ODD, NULL, 0, "eo_donut", "f3")) { fails++; goto next3; }
        base = (const uint8_t *)vg_lite_buffer_read_ptr(&target_buf);
        fails += check_point(base, stride,  64,  64, COL_FILL, 0, "f3 ring");
        fails += check_point(base, stride, 128, 128, COL_BG,   0, "f3 center=hollow");
    }
next3:

    /* ---- Frame 4: donut, NON_ZERO -> ring filled, center hollow ----
     * (calibrates INCR/DECR direction: a flipped sign fills the center) */
    {
        vg_lite_path_t p = make_path(path_donut, sizeof(path_donut), 32, 32, 224, 224);
        if (draw_and_check(&p, VG_LITE_FILL_NON_ZERO, NULL, 0, "nz_donut", "f4")) { fails++; goto next4; }
        base = (const uint8_t *)vg_lite_buffer_read_ptr(&target_buf);
        fails += check_point(base, stride,  64,  64, COL_FILL, 0, "f4 ring");
        fails += check_point(base, stride, 128, 128, COL_BG,   0, "f4 center=hollow");
    }
next4:

    /* ---- Frame 5: triple nesting, EVEN_ODD -> parity 1/2/3 = fill/hollow/fill ---- */
    {
        vg_lite_path_t p = make_path(path_triple, sizeof(path_triple), 32, 32, 224, 224);
        if (draw_and_check(&p, VG_LITE_FILL_EVEN_ODD, NULL, 0, "eo_triple", "f5")) { fails++; goto next5; }
        base = (const uint8_t *)vg_lite_buffer_read_ptr(&target_buf);
        fails += check_point(base, stride,  48, 128, COL_FILL, 0, "f5 parity1");
        fails += check_point(base, stride,  80, 128, COL_BG,   0, "f5 parity2=hollow");
        fails += check_point(base, stride, 128, 128, COL_FILL, 0, "f5 parity3");
    }
next5:

    /* ---- Frame 6: triple nesting, NON_ZERO -> all filled ---- */
    {
        vg_lite_path_t p = make_path(path_triple, sizeof(path_triple), 32, 32, 224, 224);
        if (draw_and_check(&p, VG_LITE_FILL_NON_ZERO, NULL, 0, "nz_triple", "f6")) { fails++; goto next6; }
        base = (const uint8_t *)vg_lite_buffer_read_ptr(&target_buf);
        fails += check_point(base, stride,  48, 128, COL_FILL, 0, "f6 winding1");
        fails += check_point(base, stride,  80, 128, COL_FILL, 0, "f6 winding2=filled");
        fails += check_point(base, stride, 128, 128, COL_FILL, 0, "f6 winding3");
    }
next6:

    /* ---- Frame 7: asymmetric donut + mirror matrix, NON_ZERO ----
     * mirror x -> 256 - x maps the inner rect [64,160] to [96,192]:
     * hollow band x in [96,192], y in [96,160]. */
    {
        vg_lite_path_t p = make_path(path_donut_asym, sizeof(path_donut_asym), 32, 32, 224, 224);
        vg_lite_identity(&mirror);
        vg_lite_translate((float)W, 0.0f, &mirror);
        vg_lite_scale(-1.0f, 1.0f, &mirror);
        if (draw_and_check(&p, VG_LITE_FILL_NON_ZERO, &mirror, 0, "nz_mirror", "f7")) { fails++; goto next7; }
        base = (const uint8_t *)vg_lite_buffer_read_ptr(&target_buf);
        fails += check_point(base, stride, 180, 128, COL_BG,   0, "f7 mirrored hollow (was ring)");
        fails += check_point(base, stride,  40, 128, COL_FILL, 0, "f7 ring");
    }
next7:

    /* ---- Frame 8: tint (NON_ZERO + color) ----
     * three rects, color = 0 / 0xFFFFFFFF / 0xFF808080 (half tint).
     * 0xFF808080 in 0xAABBGGRR = alpha FF, B/G/R = 0x80 -> x0.502 modulation. */
    {
        vg_lite_error_t err;
        static const struct { int dx; vg_lite_color_t col; const char *tag; } cases[3] = {
            {  0, 0,          "tint0-identity" },
            { 64, 0xFFFFFFFF, "tintFFF-identity" },
            { 128, 0xFF808080, "tint80-half" },
        };
        err = vg_lite_clear(&target_buf, NULL, 0xFF3020A0);
        if (err != VG_LITE_SUCCESS) { printf("f8 clear failed\n"); fails++; goto next8; }
        for (int i = 0; i < 3; i++) {
            int16_t buf[sizeof(path_rect_small) / sizeof(int16_t)];
            memcpy(buf, path_rect_small, sizeof(buf));
            for (int k = 0; k < (int)(sizeof(buf) / sizeof(int16_t)); k += 3)
                if (buf[k] == 2 || buf[k] == 4) buf[k + 1] = (int16_t)(buf[k + 1] + cases[i].dx);
            vg_lite_path_t p = make_path(buf, sizeof(buf),
                                         (float)(32 + cases[i].dx), 32,
                                         (float)(96 + cases[i].dx), 96);
            err = vg_lite_draw_pattern(&target_buf, &p, VG_LITE_FILL_NON_ZERO,
                                       &ident, &src_img, &ident,
                                       VG_LITE_BLEND_NONE, VG_LITE_PATTERN_PAD,
                                       0, cases[i].col, VG_LITE_FILTER_POINT);
            if (err != VG_LITE_SUCCESS) { printf("FAIL [f8 %s] draw %d\n", cases[i].tag, err); fails++; }
        }
        vg_lite_finish();
        vg_lite_save_png("fillrule_tint.png", &target_buf);
        base = (const uint8_t *)vg_lite_buffer_read_ptr(&target_buf);
        fails += check_point(base, stride,  64, 64, COL_FILL, 0, "f8 color=0 passthrough");
        fails += check_point(base, stride, 128, 64, COL_FILL, 0, "f8 color=FFF identity");
        /* 255 * 128/255 = 128.0 -> 0x80 within +/-1 rounding */
        fails += check_point(base, stride, 192, 64, (const uint8_t[4]){ 0x00, 0x00, 0x80, 0xFF }, 1, "f8 half tint");
    }
next8:

    (void)ident;
    printf("test_fillrule_pattern: %s (%d point failures)\n",
           fails == 0 ? "PASS" : "FAIL", fails);
    cleanup();
    return fails == 0 ? 0 : -1;
}
