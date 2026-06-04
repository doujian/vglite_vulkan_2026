#include "vg_lite.h"
#include "vg_lite_util.h"
#include "util.h"
#include "Common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_SIZE   320.0f;

static int   fb_width = 320, fb_height = 480;

static vg_lite_buffer_t buffer;
static vg_lite_buffer_t *fb;
static vg_lite_buffer_t image;

void cleanup(void)
{
    if (buffer.handle != NULL)
        vg_lite_free(&buffer);
    if (image.handle != NULL)
        vg_lite_free(&image);
    vg_lite_close();
}

static int run_test(const char *name, vg_lite_buffer_format_t dst_format, vg_lite_color_t recolor_color)
{
    vg_lite_error_t error = VG_LITE_SUCCESS;
    vg_lite_filter_t filter = VG_LITE_FILTER_POINT;
    vg_lite_matrix_t matrix;
    int loaded = 0;
    int fail_count = 0;

    CHECK_ERROR(vg_lite_init(32, 32));

    loaded = (vg_lite_load_raw(&image, "landscape.raw") == 0);
    if (!loaded) loaded = (vg_lite_load_raw(&image, "data/landscape.raw") == 0);
    if (!loaded) loaded = (vg_lite_load_raw(&image, "../tests/data/landscape.raw") == 0);
    if (!loaded) loaded = (vg_lite_load_raw(&image, "../data/landscape.raw") == 0);
    if (!loaded) loaded = (vg_lite_load_raw(&image, "../../tests/data/landscape.raw") == 0);
    if (!loaded) {
        printf("  load landscape.raw failed\n");
        cleanup();
        return -1;
    }

    buffer.width  = fb_width;
    buffer.height = fb_height;
    buffer.format = dst_format;

    CHECK_ERROR(vg_lite_allocate(&buffer));
    fb = &buffer;

    CHECK_ERROR(vg_lite_clear(fb, NULL, 0xFFFF0000));

    vg_lite_identity(&matrix);
    vg_lite_translate(fb_width / 2.0f, fb_height / 2.0f, &matrix);
    vg_lite_rotate(33.0f, &matrix);
    vg_lite_translate(fb_width / -2.0f, fb_height / -2.0f, &matrix);
    vg_lite_scale((vg_lite_float_t)fb_width / (vg_lite_float_t)image.width,
                  (vg_lite_float_t)fb_height / (vg_lite_float_t)image.height, &matrix);

    image.image_mode = VG_LITE_RECOLOR_MODE;
    CHECK_ERROR(vg_lite_blit(fb, &image, &matrix, VG_LITE_BLEND_NONE, recolor_color, filter));
    CHECK_ERROR(vg_lite_finish());
    char png_name[128];
    snprintf(png_name, sizeof(png_name), "recolor_%s.png", name);
    vg_lite_save_png(png_name, fb);

    {
        vg_lite_expected_buffer_t *eb = vg_lite_expected_create(fb->width, fb->height, fb->format);
        vg_lite_expected_clear(eb, NULL, 0xFFFF0000);
        vg_lite_expected_blit(eb, &image, &matrix, 0, 0, (int)VG_LITE_RECOLOR_MODE, 0, recolor_color, NULL);
        fail_count = vg_lite_expected_verify(eb, fb, 16);
        vg_lite_expected_destroy(eb);
    }

    vg_lite_free(&buffer);
    memset(&buffer, 0, sizeof(buffer));
    vg_lite_free(&image);
    memset(&image, 0, sizeof(image));
    vg_lite_close();

    return fail_count;

ErrorHandler:
    if (buffer.handle) vg_lite_free(&buffer);
    if (image.handle) vg_lite_free(&image);
    vg_lite_close();
    return -1;
}

int main(int argc, char *argv[])
{
    int total_pass = 0, total_fail = 0;

    struct {
        vg_lite_buffer_format_t format;
        const char *name;
        vg_lite_color_t color;
    } cases[] = {
        { VG_LITE_BGRA8888, "BGRA8888_red",    0xFF0000FF },
        { VG_LITE_BGRA8888, "BGRA8888_green",  0xFF00FF00 },
        { VG_LITE_BGRA8888, "BGRA8888_blue",   0xFFFF0000 },
        { VG_LITE_BGR565,   "BGR565_red",      0xFF0000FF },
        { VG_LITE_BGR565,   "BGR565_green",    0xFF00FF00 },
        { VG_LITE_BGR565,   "BGR565_blue",     0xFFFF0000 },
    };
    int num_cases = sizeof(cases) / sizeof(cases[0]);

    printf("=== Recolor_001: %d cases ===\n", num_cases);

    for (int i = 0; i < num_cases; i++) {
        printf("[%02d] %s color=0x%08X ", i, cases[i].name, cases[i].color);
        int rc = run_test(cases[i].name, cases[i].format, cases[i].color);
        if (rc == 0) {
            printf("PASS\n");
            total_pass++;
        } else if (rc > 0) {
            printf("FAIL (%d pixel mismatches)\n", rc);
            total_fail++;
        } else {
            printf("FAIL (error)\n");
            total_fail++;
        }
    }

    printf("\n  Recolor_001: %d cases, %d passed, %d failed\n", num_cases, total_pass, total_fail);
    printf("=== Results: %d passed, %d failed, %d total ===\n",
           total_fail == 0 ? 1 : 0, total_fail > 0 ? 1 : 0, 1);
    return total_fail > 0 ? 1 : 0;
}
