/* Test OPENVG_sRGBA_8888 buffer format as blit source.
 *
 * OpenVG formats are MSB-first named: R=31:24, G=23:16, B=15:8, A=7:0,
 * i.e. the LE memory word packs [A,B,G,R] per pixel (VG_LITE_ABGR8888
 * layout). The "sRGB" prefix is semantic only - values are stored and
 * sampled as-is, no gamma conversion.
 *
 * Case 1: BLEND_NONE passthrough blit (2x scale) -> output must equal the
 *         source colors expanded onto the RGBA8888 target.
 * Case 2: SRC_OVER with gradient alpha over a solid background.
 * Both are verified against the CPU reference model (util/).
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "vg_lite.h"
#include "vg_lite_util.h"
#include "util.h"
#include "Common.h"

#define ALIGMENT(value,base)   ((value + base - 1) & ~(base-1))

#define SRC_W 64
#define SRC_H 64
#define FB_W  128
#define FB_H  128

static vg_lite_buffer_t src;
static vg_lite_buffer_t fb;
static vg_lite_matrix_t matrix;

static void cleanup(void)
{
    if (src.handle != NULL) vg_lite_free(&src);
    if (fb.handle != NULL) vg_lite_free(&fb);
    vg_lite_close();
}

/* Build the source pixels: per-pixel word = (R<<24)|(G<<16)|(B<<8)|A with
 * full-range gradients so every byte value class is exercised. */
static void fill_source(void)
{
    uint32_t *tmp = (uint32_t *)calloc(1, src.stride * src.height);
    for (int y = 0; y < SRC_H; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)tmp + y * src.stride);
        for (int x = 0; x < SRC_W; x++) {
            uint8_t r = (uint8_t)(x * 4 + 3);
            uint8_t g = (uint8_t)(y * 4 + 3);
            uint8_t b = (uint8_t)(255 - x * 4);
            uint8_t a = (uint8_t)(x * 4 + y * 2);
            row[x] = ((uint32_t)r << 24) | ((uint32_t)g << 16) |
                     ((uint32_t)b << 8)  |  (uint32_t)a;
        }
    }
    vg_lite_buffer_write(&src, tmp);
    free(tmp);
}

static int run_case(vg_lite_blend_t blend, vg_lite_color_t color, const char *name)
{
    vg_lite_error_t error = VG_LITE_SUCCESS;
    int fail = 0;

    CHECK_ERROR(vg_lite_clear(&fb, NULL, 0xFF0000FF));
    CHECK_ERROR(vg_lite_finish());

    vg_lite_identity(&matrix);
    vg_lite_scale(2, 2, &matrix);
    vg_lite_translate(0, 0, &matrix);

    CHECK_ERROR(vg_lite_blit(&fb, &src, &matrix, blend, color, VG_LITE_FILTER_POINT));
    CHECK_ERROR(vg_lite_finish());

    vg_lite_save_png(name, &fb);

    {
        vg_lite_expected_buffer_t *eb = vg_lite_expected_create(fb.width, fb.height, fb.format);
        vg_lite_expected_clear(eb, NULL, 0xFF0000FF);
        vg_lite_expected_blit(eb, &src, &matrix, blend, VG_LITE_FILTER_POINT,
                              VG_LITE_NORMAL_IMAGE_MODE, 0, color, NULL);
        fail = vg_lite_expected_verify(eb, &fb, 12);
        vg_lite_expected_destroy(eb);
    }

    printf("  %-28s: %s (%d mismatch pixels)\n", name,
           fail ? "FAIL" : "PASS", fail);

ErrorHandler:
    return fail + (error != VG_LITE_SUCCESS ? 1 : 0);
}

int main(int argc, const char *argv[])
{
    vg_lite_error_t error = VG_LITE_SUCCESS;
    int fail = 0;

    CHECK_ERROR(vg_lite_init(128, 128));

    memset(&src, 0, sizeof(src));
    src.width  = SRC_W;
    src.height = SRC_H;
    src.format = OPENVG_sRGBA_8888;
    CHECK_ERROR(vg_lite_allocate(&src));
    fill_source();
    CHECK_ERROR(vg_lite_finish());

    memset(&fb, 0, sizeof(fb));
    fb.format = VG_LITE_RGBA8888;
    fb.width  = ALIGMENT(FB_W, 64);
    fb.height = FB_H;
    fb.tiled  = VGLITE_TARGET_TILING;
    CHECK_ERROR(vg_lite_allocate(&fb));

    fail += run_case(VG_LITE_BLEND_NONE, 0xFFFFFFFF, "openvg_srgba_none.png");
    fail += run_case(VG_LITE_BLEND_SRC_OVER, 0xFFFFFFFF, "openvg_srgba_srcover.png");

    if (fail == 0) printf("openvg_sRGBA test PASSED\n");
    else           printf("openvg_sRGBA test FAILED\n");

ErrorHandler:
    cleanup();
    return (error == VG_LITE_SUCCESS && fail == 0) ? 0 : -1;
}
