/*
 * Resolution: 320 x 480
 * Format: VG_LITE_RGB565
 * Transformation: Rotate/Scale/Translate
 * Alpha Blending: None
 * Related APIs: vg_lite_clear/vg_lite_translate/vg_lite_scale/vg_lite_rotate/vg_lite_blit
 * Description: Load outside landscape image, then blit it to blue dest buffer with rotate/scale/translate.
 */
#include "vg_lite.h"
#include "vg_lite_util.h"
#include "util.h"
#include "Common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_golden_pass = 0;
static int g_golden_fail = 0;

static int   fb_width = 320, fb_height = 480;
static float fb_scale = 1.0f;

#define DEFAULT_SIZE   320.0f;

static vg_lite_buffer_t buffer;
static vg_lite_buffer_t * fb;

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

int main(int argc, char *argv[])
{
    vg_lite_filter_t filter;
    vg_lite_matrix_t matrix;
    vg_lite_error_t error = VG_LITE_SUCCESS;
    int loaded = 0;

    /* Initialize vg_lite engine. */
    CHECK_ERROR(vg_lite_init(32, 32));

    /* Set image filter type. */
    filter = VG_LITE_FILTER_POINT;

    /* Load the image. */
    loaded = (vg_lite_load_raw(&image, "landscape.raw") == 0);
    if (!loaded) loaded = (vg_lite_load_raw(&image, "data/landscape.raw") == 0);
    if (!loaded) loaded = (vg_lite_load_raw(&image, "../tests/data/landscape.raw") == 0);
    if (!loaded) {
        printf("load raw file error\n");
        cleanup();
        return -1;
    }

    fb_scale = (float)fb_width / DEFAULT_SIZE;
    printf("Framebuffer size: %d x %d\n", fb_width, fb_height);

    /* Allocate the off-screen buffer. */
    buffer.width  = fb_width;
    buffer.height = fb_height;
    buffer.format = VG_LITE_RGB565;

    CHECK_ERROR(vg_lite_allocate(&buffer));
    fb = &buffer;

    /* Clear the buffer with blue. */
    CHECK_ERROR(vg_lite_clear(fb, NULL, 0xFFFF0000));
    /* Build a 33 degree rotation matrix from the center of the buffer. */
    CHECK_ERROR(vg_lite_identity(&matrix));
    /* Translate the matrix to the center of the buffer. */
    CHECK_ERROR(vg_lite_translate(fb_width / 2.0f, fb_height / 2.0f, &matrix));
    /* Do a 33 degree rotation. */
    CHECK_ERROR(vg_lite_rotate(33.0f, &matrix));
    /* Translate the matrix again to adjust matrix location. */
    CHECK_ERROR(vg_lite_translate(fb_width / -2.0f, fb_height / -2.0f, &matrix));
    /* Scale matrix according to buffer size. */
    CHECK_ERROR(vg_lite_scale((vg_lite_float_t) fb_width / (vg_lite_float_t) image.width,
                              (vg_lite_float_t) fb_height / (vg_lite_float_t) image.height, &matrix));

    /* Blit the image using the matrix. */
    CHECK_ERROR(vg_lite_blit(fb, &image, &matrix, VG_LITE_BLEND_NONE, 0, filter));
    CHECK_ERROR(vg_lite_finish());
    vg_lite_save_png("rotate.png", fb);

    {
        vg_lite_expected_buffer_t *eb = vg_lite_expected_create(fb->width, fb->height, fb->format);
        vg_lite_expected_clear(eb, NULL, 0xFFFF0000);
        vg_lite_expected_blit(eb, &image, &matrix, 0, 0, 0, 0, 0, NULL);
        g_golden_fail += vg_lite_expected_verify(eb, fb, 50);
        vg_lite_expected_destroy(eb);
        g_golden_pass = (g_golden_fail == 0) ? 1 : 0;
    }
    printf("Golden verification: %d passed, %d failed\n", g_golden_pass, g_golden_fail);

ErrorHandler:
    /* Cleanup. */
    cleanup();
    return (error == VG_LITE_SUCCESS && g_golden_fail == 0) ? 0 : -1;
}
