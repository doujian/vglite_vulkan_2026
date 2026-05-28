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
    vg_lite_buffer_t dst_orig = {0};
    vg_lite_matrix_t matrix;
    int fail = 0;

    CHECK_ERROR(vg_lite_init(32, 32));

    loaded = (vg_lite_load_raw(&dst, "landscape.raw") == 0);
    if (!loaded) loaded = (vg_lite_load_raw(&dst, "data/landscape.raw") == 0);
    if (!loaded) loaded = (vg_lite_load_raw(&dst, "../tests/data/landscape.raw") == 0);
    if (!loaded) {
        printf("Cannot load landscape.raw\n");
        cleanup();
        return -1;
    }

    dst_orig.width = dst.width;
    dst_orig.height = dst.height;
    dst_orig.format = dst.format;
    CHECK_ERROR(vg_lite_allocate(&dst_orig));
    memcpy(dst_orig.memory, dst.memory, dst.stride * dst.height);

    src.width = dst.width;
    src.height = dst.height;
    src.format = dst.format;
    CHECK_ERROR(vg_lite_allocate(&src));
    CHECK_ERROR(vg_lite_clear(&src, NULL, 0x0F0000FF));
    CHECK_ERROR(vg_lite_finish());

    vg_lite_identity(&matrix);
    CHECK_ERROR(vg_lite_blit(&dst, &src, &matrix, VG_LITE_BLEND_PREMULTIPLY_SRC_OVER, 0, 0));
    CHECK_ERROR(vg_lite_finish());

    vg_lite_save_png("blend_premultiply.png", &dst);

    {
        vg_lite_expected_buffer_t *eb = vg_lite_expected_create(dst.width, dst.height, dst.format);
        vg_lite_expected_copy(eb, &dst_orig);
        vg_lite_expected_blit(eb, &src, &matrix, 11, 0, 0, 0, 0);
        fail = vg_lite_expected_verify(eb, &dst, 12);
        vg_lite_expected_destroy(eb);
        if (fail == 0) printf("Blend premultiply OK\n");
        else           printf("Blend premultiply FAILED (%d mismatches)\n", fail);
    }

ErrorHandler:
    if (dst_orig.handle != NULL) vg_lite_free(&dst_orig);
    cleanup();
    return (error == VG_LITE_SUCCESS && fail == 0) ? 0 : -1;
}
