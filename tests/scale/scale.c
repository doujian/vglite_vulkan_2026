/*
 * Test: Scale blit with BI_LINEAR filter and SRC_OVER blend
 * Loads circle.raw, blits at various scales (0.25~1.2x) with centering.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "vg_lite.h"
#include "vg_lite_util.h"
#include "util.h"
#include "Common.h"

static int fb_width = 500;
static int fb_height = 500;
static vg_lite_buffer_t buffer;
static vg_lite_buffer_t source;

static void cleanup(void)
{
    if (buffer.handle != NULL) {
        vg_lite_free(&buffer);
    }
    if (source.handle != NULL) {
        vg_lite_free(&source);
    }
    vg_lite_close();
}

static int g_golden_pass = 0;
static int g_golden_fail = 0;

static vg_lite_error_t blitOperation(vg_lite_buffer_t *src, vg_lite_buffer_t *dst, float scale)
{
    vg_lite_filter_t filter = VG_LITE_FILTER_BI_LINEAR;
    vg_lite_error_t error = VG_LITE_SUCCESS;
    vg_lite_matrix_t matrix;
    int s = (int)(scale * 100);
    float sw, sh, tx, ty;
    char filename[64];

    sprintf(filename, "scale_s%d.png", s);

    CHECK_ERROR(vg_lite_clear(dst, NULL, 0xFF000000));

    sw = (float)src->width * scale;
    sh = (float)src->height * scale;
    tx = ((float)dst->width - sw) / 2.0f;
    ty = ((float)dst->height - sh) / 2.0f;

    vg_lite_identity(&matrix);
    vg_lite_translate(tx, ty, &matrix);
    vg_lite_scale(scale, scale, &matrix);

    CHECK_ERROR(vg_lite_blit(dst, src, &matrix, VG_LITE_BLEND_SRC_OVER, 0, filter));
    CHECK_ERROR(vg_lite_finish());

    vg_lite_save_png(filename, dst);

    {
        vg_lite_expected_buffer_t *eb = vg_lite_expected_create(dst->width, dst->height, dst->format);
        vg_lite_expected_clear(eb, NULL, 0xFF000000);
        vg_lite_expected_blit(eb, src, &matrix, 1, VG_LITE_FILTER_BI_LINEAR);
        int fail = vg_lite_expected_verify(eb, dst, 8);
        vg_lite_expected_destroy(eb);
        if (fail == 0) g_golden_pass++;
        else           g_golden_fail += fail;
    }
    return error;

ErrorHandler:
    return error;
}

int main(int argc, const char *argv[])
{
    vg_lite_error_t error = VG_LITE_SUCCESS;
    int loaded = 0;
    int i;
    float scales[] = {1.2f, 1.0f, 0.25f, 0.4f, 0.55f, 0.7f, 0.85f, 0.95f};
    int num_scales = sizeof(scales) / sizeof(scales[0]);

    CHECK_ERROR(vg_lite_init(fb_width, fb_height));

    buffer.width = fb_width;
    buffer.height = fb_height;
    buffer.format = VG_LITE_RGBA8888;
    CHECK_ERROR(vg_lite_allocate(&buffer));

    loaded = (vg_lite_load_raw(&source, "circle.raw") == 0);
    if (!loaded) loaded = (vg_lite_load_raw(&source, "data/circle.raw") == 0);
    if (!loaded) loaded = (vg_lite_load_raw(&source, "../tests/data/circle.raw") == 0);
    if (!loaded) {
        printf("Cannot load circle.raw\n");
        cleanup();
        return -1;
    }

    for (i = 0; i < num_scales; i++) {
        error = blitOperation(&source, &buffer, scales[i]);
        if (error != VG_LITE_SUCCESS) break;
    }

    printf("Scale test done - saved %d PNGs\n", num_scales);
    printf("Golden verification: %d passed, %d failed\n", g_golden_pass, g_golden_fail);

ErrorHandler:
    cleanup();
    return (error == VG_LITE_SUCCESS && g_golden_fail == 0) ? 0 : -1;
}
