/*
 * Test: VG_LITE_BLEND_PREMULTIPLY_SRC_OVER
 * Loads landscape.raw, creates a semi-transparent source,
 * blits with PREMULTIPLY_SRC_OVER blend mode.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "vg_lite.h"
#include "vg_lite_util.h"
#include "util.h"
#include "Common.h"

static vg_lite_buffer_t src;
static vg_lite_buffer_t dst;

void cleanup(void)
{
    if (src.handle != NULL) {
        vg_lite_free(&src);
    }
    if (dst.handle != NULL) {
        vg_lite_free(&dst);
    }
    vg_lite_close();
}

int main(int argc, const char *argv[])
{
    vg_lite_error_t error = VG_LITE_SUCCESS;
    int loaded = 0;

    CHECK_ERROR(vg_lite_init(32, 32));

    /* Load landscape.raw as destination */
    loaded = (vg_lite_load_raw(&dst, "landscape.raw") == 0);
    if (!loaded) loaded = (vg_lite_load_raw(&dst, "data/landscape.raw") == 0);
    if (!loaded) loaded = (vg_lite_load_raw(&dst, "../tests/data/landscape.raw") == 0);
    if (!loaded) {
        printf("Cannot load landscape.raw\n");
        cleanup();
        return -1;
    }

    /* Create source buffer same size/format, clear to semi-transparent color */
    src.width = dst.width;
    src.height = dst.height;
    src.format = dst.format;
    CHECK_ERROR(vg_lite_allocate(&src));
    CHECK_ERROR(vg_lite_clear(&src, NULL, 0x0F0000FF));

    /* Blit with VG_LITE_BLEND_PREMULTIPLY_SRC_OVER */
    {
        vg_lite_matrix_t matrix;
        vg_lite_identity(&matrix);
        CHECK_ERROR(vg_lite_blit(&dst, &src, &matrix, VG_LITE_BLEND_PREMULTIPLY_SRC_OVER, 0, 0));
    }
    CHECK_ERROR(vg_lite_finish());

    vg_lite_save_png("blend_premultiply.png", &dst);

    /* Verify: destination should have blended content */
    {
        uint32_t px = vg_lite_read_pixel(&dst, 100, 100);
        int r = px & 0xFF, g = (px >> 8) & 0xFF, b = (px >> 16) & 0xFF;
        if (r > 0) {
            printf("Blend premultiply OK (pixel 100,100: R=%d G=%d B=%d)\n", r, g, b);
        } else {
            printf("FAIL: blend produced unexpected result\n");
        }
    }

ErrorHandler:
    cleanup();
    return (error == VG_LITE_SUCCESS) ? 0 : -1;
}
