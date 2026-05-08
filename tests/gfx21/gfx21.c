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

    loaded = (vg_lite_load_png(&src, "image.png") == 0);
    if (!loaded) loaded = (vg_lite_load_png(&src, "data/image.png") == 0);
    if (!loaded) loaded = (vg_lite_load_png(&src, "../tests/data/image.png") == 0);
    if (!loaded) {
        printf("Cannot load image.png\n");
        vg_lite_close();
        return -1;
    }

    dst.width  = 256;
    dst.height = 256;
    dst.format = VG_LITE_BGRA8888;

    CHECK_ERROR(vg_lite_allocate(&dst));

    CHECK_ERROR(vg_lite_clear(&dst, NULL, 0xFFFF0000));

    CHECK_ERROR(vg_lite_identity(&matrix));
    CHECK_ERROR(vg_lite_translate((float)dst.width / 2.0f, (float)dst.height / 2.0f, &matrix));
    CHECK_ERROR(vg_lite_rotate(-90.0f, &matrix));
    CHECK_ERROR(vg_lite_translate((float)dst.width / -2.0f, (float)dst.height / -2.0f, &matrix));
    CHECK_ERROR(vg_lite_scale((float)dst.width / (float)src.width, (float)dst.height / (float)src.height, &matrix));

    CHECK_ERROR(vg_lite_blit(&dst, &src, &matrix, VG_LITE_BLEND_NONE, 0, VG_LITE_FILTER_POINT));
    CHECK_ERROR(vg_lite_finish());

    vg_lite_save_png("gfx21.png", &dst);
    printf("gfx21 test done - saved gfx21.png\n");

    {
        vg_lite_expected_buffer_t *eb = vg_lite_expected_create(dst.width, dst.height, dst.format);
        vg_lite_expected_clear(eb, NULL, 0xFFFF0000);
        vg_lite_expected_blit(eb, &src, &matrix, 0, 0);
        g_golden_fail += vg_lite_expected_verify(eb, &dst, 1);
        vg_lite_expected_destroy(eb);
        g_golden_pass = (g_golden_fail == 0) ? 1 : 0;
    }
    printf("Golden verification: %d passed, %d failed\n", g_golden_pass, g_golden_fail);

ErrorHandler:
    vg_lite_free(&src);
    vg_lite_free(&dst);
    vg_lite_close();
    return (error == VG_LITE_SUCCESS && g_golden_fail == 0) ? 0 : -1;
}
