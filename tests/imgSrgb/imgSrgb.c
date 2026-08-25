#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "vg_lite.h"
#include "vg_lite_util.h"
#include "util.h"

/* Test: blit with sRGB-class (OPENVG_s*) source buffer formats.
 * Modeled after tests/imgIndex/imgIndex.c.
 *
 * For each sRGB source format:
 *   1. Allocate a source buffer in that format.
 *   2. Fill it with a deterministic color-bar/gradient pattern.
 *   3. Blit (scale + rotate) into a BGRA8888 target.
 *   4. Save output PNG and check a few sampled pixels.
 * Pass criteria per case: allocate/blit/finish return VG_LITE_SUCCESS
 * and the output is not uniform (source data actually reached the target).
 */

static vg_lite_buffer_t target;

typedef struct {
    vg_lite_buffer_format_t format;
    const char            *name;
    uint32_t               bpp;   /* bits per pixel */
} srgb_case_t;

static const srgb_case_t g_cases[] = {
    { OPENVG_sRGBA_8888,  "sRGBA_8888",  32 },
    { OPENVG_sRGBX_8888,  "sRGBX_8888",  32 },
    { OPENVG_sRGB_565,    "sRGB_565",    16 },
    { OPENVG_sRGBA_4444,  "sRGBA_4444",  16 },
};
#define CASE_COUNT (int)(sizeof(g_cases) / sizeof(g_cases[0]))

/* Write an sRGB pixel into memory. OPENVG formats pack channels MSB-to-LSB
 * in the order given by the name, so on little-endian the memory bytes for
 * sRGBA_8888 are [a][b][g][r] and for sRGB_565 the 16-bit word is
 * (r<<11)|(g<<5)|b. Channel values are sRGB-encoded bytes (used as-is). */
static void put_pixel(uint8_t *row, uint32_t x, uint32_t bpp,
                      uint32_t r, uint32_t g, uint32_t b, uint32_t a)
{
    if (bpp == 32) {
        uint32_t pixel = (r << 24) | (g << 16) | (b << 8) | a; /* sRGBA_8888, X treated as a=255 */
        memcpy(row + x * 4, &pixel, 4);
    } else {
        uint16_t pixel = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        if (bpp == 16)
            memcpy(row + x * 2, &pixel, 2);
    }
}

/* Fill source with horizontal red ramp + vertical constant channels:
 * R = x * 255 / (w-1), G = y * 255 / (h-1), B = 0x40, A = 0xFF. */
static void fill_source(vg_lite_buffer_t *src, uint32_t bpp)
{
    for (uint32_t y = 0; y < src->height; y++) {
        uint8_t *row = (uint8_t *)src->memory + (size_t)y * src->stride;
        for (uint32_t x = 0; x < src->width; x++) {
            uint32_t r = src->width  > 1 ? (x * 255) / (src->width  - 1) : 255;
            uint32_t g = src->height > 1 ? (y * 255) / (src->height - 1) : 255;
            put_pixel(row, x, bpp, r, g, 0x40, 0xFF);
        }
    }
}

static int run_case(const srgb_case_t *tc, vg_lite_matrix_t *matrix)
{
    vg_lite_buffer_t src;
    vg_lite_error_t error;
    char filename[128];
    int fail = 0;

    memset(&src, 0, sizeof(src));
    src.format = tc->format;
    src.width  = 256;
    src.height = 256;
    error = vg_lite_allocate(&src);
    if (error != VG_LITE_SUCCESS) {
        printf("[%s] vg_lite_allocate failed: %d\n", tc->name, error);
        return 1;
    }
    fill_source(&src, tc->bpp);

    vg_lite_clear(&target, NULL, 0xFF000000);
    error = vg_lite_blit(&target, &src, matrix,
                         VG_LITE_BLEND_NONE, 0, VG_LITE_FILTER_BI_LINEAR);
    if (error != VG_LITE_SUCCESS) {
        printf("[%s] vg_lite_blit failed: %d\n", tc->name, error);
        vg_lite_free(&src);
        return 1;
    }
    error = vg_lite_finish();
    if (error != VG_LITE_SUCCESS) {
        printf("[%s] vg_lite_finish failed: %d\n", tc->name, error);
        vg_lite_free(&src);
        return 1;
    }

    snprintf(filename, sizeof(filename), "imgSrgb_%s_output.png", tc->name);
    vg_lite_save_png(filename, &target);
    printf("[%s] blit OK, output saved to %s\n", tc->name, filename);

    /* Sanity: the scaled blit covers the whole target, so corners must
     * differ (R ramp is horizontal). Sample a few points. */
    {
        uint32_t lu = vg_lite_read_pixel(&target, 4, target.height / 2);
        uint32_t ru = vg_lite_read_pixel(&target, target.width - 5, target.height / 2);
        printf("[%s] mid-row pixels: left=0x%08x right=0x%08x\n", tc->name, lu, ru);
        if (lu == ru) {
            printf("[%s] WARNING: output looks uniform, source may not have been sampled\n", tc->name);
            fail = 1;
        }
    }

    vg_lite_free(&src);
    return fail;
}

int main(int argc, const char *argv[])
{
    vg_lite_error_t error;
    vg_lite_matrix_t matrix;
    int failed_cases = 0;

    printf("=== imgSrgb Test (sRGB source formats) ===\n");

    error = vg_lite_init(320, 480);
    if (error != VG_LITE_SUCCESS) {
        printf("vg_lite_init failed: %d\n", error);
        return 1;
    }
    printf("vg_lite_init OK\n");

    target.format = VG_LITE_BGRA8888;
    target.width  = 320;
    target.height = 480;
    error = vg_lite_allocate(&target);
    if (error != VG_LITE_SUCCESS) {
        printf("vg_lite_allocate target failed: %d\n", error);
        vg_lite_close();
        return 1;
    }
    printf("BGRA8888 target buffer OK (%ux%u)\n", target.width, target.height);

    vg_lite_identity(&matrix);
    vg_lite_translate(target.width / 2.0f, target.height / 2.0f, &matrix);
    vg_lite_rotate(33.0f, &matrix);
    vg_lite_translate(-target.width / 2.0f, -target.height / 2.0f, &matrix);
    vg_lite_scale((vg_lite_float_t)target.width / 256.0f,
                  (vg_lite_float_t)target.height / 256.0f, &matrix);

    for (int i = 0; i < CASE_COUNT; i++) {
        failed_cases += run_case(&g_cases[i], &matrix);
    }

    vg_lite_free(&target);
    vg_lite_close();

    printf("=== Test Complete: %d/%d cases passed ===\n",
           CASE_COUNT - failed_cases, CASE_COUNT);
    if (failed_cases == 0) printf("imgSrgb test PASSED\n");
    else                   printf("imgSrgb test FAILED\n");

    return failed_cases ? 1 : 0;
}
