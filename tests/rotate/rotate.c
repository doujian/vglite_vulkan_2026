#include "vg_lite.h"
#include "vg_lite_util.h"
#include "util.h"
#include "Common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_golden_pass = 0;
static int g_golden_fail = 0;

int main(int argc, char *argv[])
{
    vg_lite_error_t error = VG_LITE_SUCCESS;
    vg_lite_buffer_t src = {0};
    vg_lite_buffer_t dst = {0};
    vg_lite_matrix_t matrix;
    int loaded = 0;

    error = vg_lite_init(0, 0);
    if (error != VG_LITE_SUCCESS) {
        printf("vg_lite_init failed: %d\n", error);
        return -1;
    }

    loaded = (vg_lite_load_raw(&src, "landscape.raw") == 0);
    if (!loaded) loaded = (vg_lite_load_raw(&src, "data/landscape.raw") == 0);
    if (!loaded) loaded = (vg_lite_load_raw(&src, "../tests/data/landscape.raw") == 0);
    if (!loaded) {
        printf("Cannot load landscape.raw\n");
        vg_lite_close();
        return -1;
    }

    dst.width  = src.width;
    dst.height = src.height;
    dst.format = VG_LITE_BGRA8888;

    CHECK_ERROR(vg_lite_allocate(&dst));

    CHECK_ERROR(vg_lite_clear(&dst, NULL, 0xFF000000));
    CHECK_ERROR(vg_lite_finish());

    if (vg_lite_check_pixel(&dst, 0, 0, 0xFF000000, 0) &&
        vg_lite_check_pixel(&dst, dst.width / 2, dst.height / 2, 0xFF000000, 0)) {
        g_golden_pass++;
        printf("Background clear OK\n");
    } else {
        g_golden_fail++;
    }

    CHECK_ERROR(vg_lite_identity(&matrix));
    CHECK_ERROR(vg_lite_translate((float)dst.width / 2.0f, (float)dst.height / 2.0f, &matrix));
    CHECK_ERROR(vg_lite_rotate(33.0f, &matrix));
    CHECK_ERROR(vg_lite_translate(-(float)src.width / 2.0f, -(float)src.height / 2.0f, &matrix));

    CHECK_ERROR(vg_lite_blit(&dst, &src, &matrix, VG_LITE_BLEND_NONE, 0, VG_LITE_FILTER_POINT));
    CHECK_ERROR(vg_lite_finish());

    vg_lite_save_png("rotate.png", &dst);
    printf("rotate test done - saved rotate.png\n");

    /* Verify blit produced content: center pixel should differ from black background */
    {
        uint32_t center = vg_lite_read_pixel(&dst, dst.width / 2, dst.height / 2);
        int r = center & 0xFF, g = (center >> 8) & 0xFF, b = (center >> 16) & 0xFF;
        if (r != 0 || g != 0 || b != 0) { g_golden_pass++; printf("Blit content OK\n"); }
        else { printf("  FAIL: center pixel is black (should have rotated content)\n"); g_golden_fail++; }
    }
    printf("Golden verification: %d passed, %d failed\n", g_golden_pass, g_golden_fail);

ErrorHandler:
    vg_lite_free(&src);
    vg_lite_free(&dst);
    vg_lite_close();
    return (error == VG_LITE_SUCCESS && g_golden_fail == 0) ? 0 : -1;
}
