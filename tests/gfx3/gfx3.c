#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "vg_lite.h"
#include "vg_lite_util.h"
#include "util.h"
#include "Common.h"

static vg_lite_buffer_t buffer;
static vg_lite_buffer_t image;

void cleanup(void)
{
    if (buffer.handle != NULL) {
        vg_lite_free(&buffer);
    }
    if (image.handle != NULL) {
        vg_lite_free(&image);
    }
    vg_lite_close();
}

int main(int argc, const char *argv[])
{
    vg_lite_error_t error = VG_LITE_SUCCESS;
    vg_lite_matrix_t matrix;
    vg_lite_filter_t filter = VG_LITE_FILTER_POINT;
    int loaded = 0;

    CHECK_ERROR(vg_lite_init(640, 480));

    /* vg_lite_load_png returns 0 on success */
    loaded = (vg_lite_load_png(&image, "image.png") == 0);
    if (!loaded) loaded = (vg_lite_load_png(&image, "data/image.png") == 0);
    if (!loaded) loaded = (vg_lite_load_png(&image, "data/gfx3_image.png") == 0);
    if (!loaded) loaded = (vg_lite_load_png(&image, "../tests/data/image.png") == 0);
    if (!loaded) loaded = (vg_lite_load_png(&image, "../tests/data/gfx3_image.png") == 0);
    if (!loaded) {
        printf("Cannot load image.png\n");
        cleanup();
        return -1;
    }

    buffer.width = 640;
    buffer.height = 480;
    buffer.format = VG_LITE_BGRA8888;
    CHECK_ERROR(vg_lite_allocate(&buffer));

    CHECK_ERROR(vg_lite_clear(&buffer, NULL, 0xFFFF0000));

    /* Center-rotate 33 degrees + scale to fit */
    vg_lite_identity(&matrix);
    vg_lite_translate(buffer.width / 2.0f, buffer.height / 2.0f, &matrix);
    vg_lite_rotate(33.0f, &matrix);
    vg_lite_translate(buffer.width / -2.0f, buffer.height / -2.0f, &matrix);
    vg_lite_scale((float)buffer.width / (float)image.width,
                  (float)buffer.height / (float)image.height, &matrix);

    CHECK_ERROR(vg_lite_blit(&buffer, &image, &matrix, VG_LITE_BLEND_NONE, 0, filter));
    CHECK_ERROR(vg_lite_finish());

    vg_lite_save_png("gfx3.png", &buffer);

    {
        vg_lite_expected_buffer_t *eb = vg_lite_expected_create(buffer.width, buffer.height, buffer.format);
        vg_lite_expected_clear(eb, NULL, 0xFFFF0000);
        vg_lite_expected_blit(eb, &image, &matrix, 0, 0);
        int fail = vg_lite_expected_verify(eb, &buffer, 0);
        vg_lite_expected_destroy(eb);
        if (fail == 0) printf("gfx3 OK\n");
        else           printf("gfx3 FAILED (%d mismatches)\n", fail);
    }

ErrorHandler:
    cleanup();
    return (error == VG_LITE_SUCCESS) ? 0 : -1;
}
