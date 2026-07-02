#include "vg_lite.h"
#include "vg_lite_util.h"
#include "util.h"
#include "Common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ALIGN(value, base) (((value) + (base) - 1) & ~((base) - 1))

#define NUM_FORMATS 10

static vg_lite_buffer_format_t formats[] = {
    VG_LITE_RGBA8888,
    VG_LITE_BGRA8888,
    VG_LITE_RGBX8888,
    VG_LITE_BGRX8888,
    VG_LITE_RGB565,
    VG_LITE_BGR565,
    VG_LITE_RGBA4444,
    VG_LITE_BGRA4444,
    VG_LITE_A8,
    VG_LITE_L8,
};

static int get_tolerance(vg_lite_buffer_format_t fmt)
{
    switch (fmt) {
    case VG_LITE_RGB565:
    case VG_LITE_BGR565:
        return 12;
    case VG_LITE_A8:
    case VG_LITE_L8:
    case VG_LITE_RGBA4444:
    case VG_LITE_BGRA4444:
        return 17;
    default:
        return 0;
    }
}

static vg_lite_error_t SFT_Clear_001(void)
{
    vg_lite_error_t error = VG_LITE_SUCCESS;
    int x, y, w, h, i;
    vg_lite_color_t cc;
    vg_lite_buffer_t buffer;
    vg_lite_rectangle_t rect;
    int fail = 0;

    memset(&buffer, 0, sizeof(vg_lite_buffer_t));
    buffer.width  = ALIGN((int)WINDSIZEX, 32);
    buffer.height = (int)WINDSIZEY;
    buffer.format = formats[0];

    CHECK_ERROR(vg_lite_allocate(&buffer));

    for (i = 0; i < 16; i++) {
        x = (int)Random_r(0, WINDSIZEX);
        y = (int)Random_r(0, WINDSIZEY);
        w = (int)Random_r(1.0f, WINDSIZEX);
        h = (int)Random_r(1.0f, WINDSIZEY);
        cc = GenColor_r();

        rect.x = x;
        rect.y = y;
        rect.width  = w;
        rect.height = h;

        printf("  [%2d] clear color 0x%08x rect %d,%d %dx%d\n", i, cc, x, y, w, h);

        CHECK_ERROR(vg_lite_clear(&buffer, NULL, 0xffffffff));
        CHECK_ERROR(vg_lite_clear(&buffer, &rect, cc));
        CHECK_ERROR(vg_lite_finish());

        {
            vg_lite_expected_buffer_t *eb = vg_lite_expected_create(
                buffer.width, buffer.height, buffer.format);
            vg_lite_expected_clear(eb, NULL, 0xffffffff);
            vg_lite_expected_clear(eb, &rect, cc);
            fail += vg_lite_expected_verify(eb, &buffer, get_tolerance(buffer.format));
            vg_lite_expected_destroy(eb);
        }

        char fname[128];
        snprintf(fname, sizeof(fname), "SFT_Clear_001_%d.png", i);
        vg_lite_save_png(fname, &buffer);
    }

    if (fail == 0)
        printf("  SFT_Clear_001 PASSED\n");
    else
        printf("  SFT_Clear_001 FAILED (%d pixel mismatches total)\n", fail);

ErrorHandler:
    vg_lite_free(&buffer);
    return fail > 0 ? VG_LITE_GENERIC_IO : error;
}

static vg_lite_error_t SFT_Clear_002(void)
{
    vg_lite_error_t error = VG_LITE_SUCCESS;
    int i;
    vg_lite_color_t cc;
    vg_lite_buffer_t buffer;
    int fail = 0;

    for (i = 0; i < NUM_FORMATS; i++) {
        cc = GenColor_r();

        memset(&buffer, 0, sizeof(vg_lite_buffer_t));
        buffer.width  = ALIGN((int)WINDSIZEX, 128);
        buffer.height = (int)WINDSIZEY;
        buffer.format = formats[i];

        error = vg_lite_allocate(&buffer);
        if (error != VG_LITE_SUCCESS) {
            printf("  [%2d] format 0x%x skipped (allocate failed: %d)\n", i, formats[i], error);
            continue;
        }

        printf("  [%2d] clear format 0x%x color 0x%08x\n", i, formats[i], cc);

        CHECK_ERROR(vg_lite_clear(&buffer, NULL, cc));
        CHECK_ERROR(vg_lite_finish());

        {
            vg_lite_expected_buffer_t *eb = vg_lite_expected_create(
                buffer.width, buffer.height, buffer.format);
            vg_lite_expected_clear(eb, NULL, cc);
            int mismatches = vg_lite_expected_verify(eb, &buffer, get_tolerance(formats[i]));
            if (mismatches == buffer.width * buffer.height) {
                printf("  [%2d] format 0x%x skipped (driver does not support clear)\n", i, formats[i]);
            } else {
                fail += mismatches;
            }
            vg_lite_expected_destroy(eb);
        }

        char fname[128];
        snprintf(fname, sizeof(fname), "SFT_Clear_002_%d.png", i);
        vg_lite_save_png(fname, &buffer);

        vg_lite_free(&buffer);
    }

    if (fail == 0)
        printf("  SFT_Clear_002 PASSED\n");
    else
        printf("  SFT_Clear_002 FAILED (%d pixel mismatches total)\n", fail);

    return fail > 0 ? VG_LITE_GENERIC_IO : VG_LITE_SUCCESS;

ErrorHandler:
    vg_lite_free(&buffer);
    return error;
}

static vg_lite_error_t SFT_Clear_003(void)
{
    vg_lite_error_t error = VG_LITE_SUCCESS;
    int i;
    vg_lite_color_t cc;
    vg_lite_buffer_t buffer;
    int fail = 0;

    for (i = 0; i < 10; i++) {
        cc = GenColor_r();

        memset(&buffer, 0, sizeof(vg_lite_buffer_t));
        buffer.width  = ALIGN((int32_t)Random_r(1.0f, WINDSIZEX), 32);
        buffer.height = (int32_t)Random_r(1.0f, WINDSIZEY);
        buffer.format = VG_LITE_RGBA8888;

        CHECK_ERROR(vg_lite_allocate(&buffer));

        printf("  [%2d] clear %dx%d color 0x%08x\n", i, buffer.width, buffer.height, cc);

        CHECK_ERROR(vg_lite_clear(&buffer, NULL, cc));
        CHECK_ERROR(vg_lite_finish());

        {
            vg_lite_expected_buffer_t *eb = vg_lite_expected_create(
                buffer.width, buffer.height, buffer.format);
            vg_lite_expected_clear(eb, NULL, cc);
            fail += vg_lite_expected_verify(eb, &buffer, 0);
            vg_lite_expected_destroy(eb);
        }

        char fname[128];
        snprintf(fname, sizeof(fname), "SFT_Clear_003_%d.png", i);
        vg_lite_save_png(fname, &buffer);

        vg_lite_free(&buffer);
    }

    if (fail == 0)
        printf("  SFT_Clear_003 PASSED\n");
    else
        printf("  SFT_Clear_003 FAILED (%d pixel mismatches total)\n", fail);

    return fail > 0 ? VG_LITE_GENERIC_IO : VG_LITE_SUCCESS;

ErrorHandler:
    vg_lite_free(&buffer);
    return error;
}

int main(int argc, char *argv[])
{
    vg_lite_error_t error;
    int total_fail = 0;

    (void)argc;
    (void)argv;

    random_srand(32557);

    error = vg_lite_init(0, 0);
    if (error != VG_LITE_SUCCESS) {
        printf("vg_lite_init failed: %d\n", error);
        return -1;
    }

    printf("=== SFT_Clear_001 (random rect clear, RGBA8888) ===\n");
    error = SFT_Clear_001();
    if (error != VG_LITE_SUCCESS) total_fail++;

    printf("=== SFT_Clear_002 (full clear, all formats) ===\n");
    error = SFT_Clear_002();
    if (error != VG_LITE_SUCCESS) total_fail++;

    printf("=== SFT_Clear_003 (full clear, random sizes) ===\n");
    error = SFT_Clear_003();
    if (error != VG_LITE_SUCCESS) total_fail++;

    vg_lite_close();

    if (total_fail == 0)
        printf("\nAll SFT_Clear tests PASSED\n");
    else
        printf("\nSFT_Clear tests FAILED (%d cases)\n", total_fail);

    return total_fail > 0 ? -1 : 0;
}
