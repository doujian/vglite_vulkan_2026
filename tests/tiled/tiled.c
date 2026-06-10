#include "vg_lite.h"
#include "vg_lite_util.h"
#include "util.h"
#include "Common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fb_width = 256, fb_height = 256;

static vg_lite_buffer_t buffer;
static vg_lite_buffer_t *fb;
static vg_lite_buffer_t tiled_buffer;

static char path_data[] = {
    2, -5, -10,
    4, 5, -10,
    4, 10, -5,
    4, 0, 0,
    4, 10, 5,
    4, 5, 10,
    4, -5, 10,
    4, -10, 5,
    4, -10, -5,
    0,
};

static vg_lite_path_t path = {
    {-10, -10, 10, 10},
    VG_LITE_HIGH,
    VG_LITE_S8,
    {0},
    sizeof(path_data),
    path_data,
    1
};

static vg_lite_error_t Tiled_001(void)
{
    vg_lite_error_t error = VG_LITE_SUCCESS;
    vg_lite_matrix_t matrix;
    vg_lite_filter_t filter = VG_LITE_FILTER_POINT;

    memset(&buffer, 0, sizeof(buffer));
    buffer.width = fb_width;
    buffer.height = fb_height;
    buffer.format = VG_LITE_RGB565;
    CHECK_ERROR(vg_lite_allocate(&buffer));
    fb = &buffer;

    memset(&tiled_buffer, 0, sizeof(tiled_buffer));
    tiled_buffer.format = VG_LITE_RGBA8888;
    tiled_buffer.width = buffer.width;
    tiled_buffer.height = buffer.height;
    CHECK_ERROR(vg_lite_allocate(&tiled_buffer));

    CHECK_ERROR(vg_lite_clear(&buffer, NULL, 0xFFFF0000));
    CHECK_ERROR(vg_lite_clear(&tiled_buffer, NULL, 0xFFFF0000));
    CHECK_ERROR(vg_lite_finish());

    vg_lite_identity(&matrix);
    vg_lite_translate(tiled_buffer.width / 2.0f, tiled_buffer.height / 2.0f, &matrix);
    vg_lite_scale(10, 10, &matrix);
    vg_lite_scale((float)fb_width / 256.0f, (float)fb_height / 256.0f, &matrix);

    CHECK_ERROR(vg_lite_draw(&tiled_buffer, &path, VG_LITE_FILL_EVEN_ODD, &matrix, VG_LITE_BLEND_NONE, 0xFF0000FF));
    CHECK_ERROR(vg_lite_finish());

    vg_lite_save_png("Tiled_001_0.png", &tiled_buffer);

    vg_lite_identity(&matrix);
    vg_lite_translate((fb_width - tiled_buffer.width) / 2.0f, (fb_height - tiled_buffer.height) / 2.0f, &matrix);
    CHECK_ERROR(vg_lite_blit(fb, &tiled_buffer, &matrix, VG_LITE_BLEND_NONE, 0, filter));
    CHECK_ERROR(vg_lite_finish());
    vg_lite_save_png("Tiled_001_1.png", &buffer);

    printf("  Tiled_001 PASSED\n");

ErrorHandler:
    if (tiled_buffer.handle) vg_lite_free(&tiled_buffer);
    if (buffer.handle) vg_lite_free(&buffer);
    vg_lite_clear_path(&path);
    return error;
}

int main(void)
{
    vg_lite_error_t error;
    printf("Initializing vg_lite...\n");
    error = vg_lite_init(fb_width, fb_height);
    if (error != VG_LITE_SUCCESS) {
        printf("vg_lite_init failed: %d\n", error);
        return 1;
    }
    printf("vg_lite initialized.\n\n");

    printf("=== Tiled Tests ===\n");
    printf("Case: Tiled_001 ::::::::::::: Started\n");
    error = Tiled_001();
    if (error == VG_LITE_SUCCESS)
        printf("\n=== Results: 1 passed, 0 failed ===\n");
    else
        printf("\n=== Results: 0 passed, 1 failed (error=%d) ===\n", error);

    vg_lite_close();
    return 0;
}
