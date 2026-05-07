//-----------------------------------------------------------------------------
//Description: The test cases test the different size and position of Clear-Rectangle
//-----------------------------------------------------------------------------
#include "util.h"
#include "Common.h"
#include <stdio.h>
#include <string.h>

#define FORMAT_COUNT 2
static vg_lite_buffer_format_t formats[] =
{
    VG_LITE_RGB565,
    VG_LITE_RGBA8888,
};

static int g_golden_pass = 0;
static int g_golden_fail = 0;

static int verify_full_clear(vg_lite_buffer_t *buf, vg_lite_color_t cc)
{
    int tol = (buf->format == VG_LITE_RGB565) ? 12 : 0;
    vg_lite_expected_buffer_t *eb = vg_lite_expected_create(buf->width, buf->height, buf->format);
    vg_lite_expected_clear(eb, NULL, cc);
    int fail = vg_lite_expected_verify(eb, buf, tol);
    vg_lite_expected_destroy(eb);
    if (fail == 0) g_golden_pass++; else g_golden_fail++;
    return fail;
}

static int verify_rect_clear(vg_lite_buffer_t *buf, vg_lite_rectangle_t *rect, vg_lite_color_t cc, vg_lite_color_t bg)
{
    int tol = (buf->format == VG_LITE_RGB565) ? 12 : 0;
    vg_lite_expected_buffer_t *eb = vg_lite_expected_create(buf->width, buf->height, buf->format);
    vg_lite_expected_clear(eb, NULL, bg);
    vg_lite_expected_clear(eb, rect, cc);
    int fail = vg_lite_expected_verify(eb, buf, tol);
    vg_lite_expected_destroy(eb);
    if (fail == 0) g_golden_pass++; else g_golden_fail++;
    return fail;
}

/* different buffer formats, different clear modes, and part region. */
vg_lite_error_t    Clear_001()
{
    int i;
    int x, y, w, h;
    vg_lite_error_t error = VG_LITE_SUCCESS;
    vg_lite_color_t cc;
    vg_lite_buffer_t buffer[FORMAT_COUNT];
    vg_lite_rectangle_t rect;

    for(i = 0; i < FORMAT_COUNT; i++)
    {
        cc = GenColor_r();
        memset(&buffer[i],0,sizeof(vg_lite_buffer_t));
        buffer[i].width = (int)WINDSIZEX;
        buffer[i].height = (int)WINDSIZEY;
        buffer[i].format = formats[i];
        buffer[i].stride = 0;
        buffer[i].handle = NULL;
        buffer[i].memory = NULL;
        buffer[i].address = 0;
        buffer[i].tiled = 0;

        x = (int)Random_r(0, WINDSIZEX);
        y = (int)Random_r(0, WINDSIZEY);
        w = (int)Random_r(1.0f, WINDSIZEX);
        h = (int)Random_r(1.0f, WINDSIZEY);
        rect.x = x;
        rect.y = y;
        rect.width = w;
        rect.height = h;

        CHECK_ERROR(vg_lite_allocate(&buffer[i]));

        printf("color: 0x%x, dst format: 0x%x\n", cc, formats[i]);
        printf("clear with rectangle mode.\n");
        CHECK_ERROR(vg_lite_clear(&buffer[i], NULL, 0xffffffff));
        CHECK_ERROR(vg_lite_clear(&buffer[i],&rect,cc));
        CHECK_ERROR(vg_lite_finish());
        SaveBMP_SFT("Clear_001_",&buffer[i],TRUE);
        verify_rect_clear(&buffer[i], &rect, cc, 0xffffffff);
        printf("clear with scanline mode.\n");
        CHECK_ERROR(vg_lite_clear(&buffer[i], NULL, 0xffffffff));
        CHECK_ERROR(vg_lite_clear(&buffer[i],&rect,cc));
        CHECK_ERROR(vg_lite_finish());
        SaveBMP_SFT("Clear_001_",&buffer[i],TRUE);
        verify_rect_clear(&buffer[i], &rect, cc, 0xffffffff);
        vg_lite_free(&buffer[i]);
    }
    return VG_LITE_SUCCESS;

ErrorHandler:
    if(buffer[i].handle != NULL)
        vg_lite_free(&buffer[i]);
    return error;
}

/* different buffer formats, different clear modes, and full screen. */
vg_lite_error_t    Clear_002()
{
    int i;
    vg_lite_error_t error = VG_LITE_SUCCESS;
    vg_lite_color_t cc;
    vg_lite_buffer_t buffer[FORMAT_COUNT];

    for(i = 0; i < FORMAT_COUNT; i++)
    {
        cc = GenColor_r();
        memset(&buffer[i],0,sizeof(vg_lite_buffer_t));
        buffer[i].width = (int)WINDSIZEX;
        buffer[i].height = (int)WINDSIZEY;
        buffer[i].format = formats[i];
        buffer[i].stride = 0;
        buffer[i].handle = NULL;
        buffer[i].memory = NULL;
        buffer[i].address = 0;
        buffer[i].tiled = 0;

        CHECK_ERROR(vg_lite_allocate(&buffer[i]));

        printf("color: 0x%x, dst format: 0x%x\n", cc, formats[i]);
        printf("clear with rectangle mode.\n");
        CHECK_ERROR(vg_lite_clear(&buffer[i],NULL,cc));
        CHECK_ERROR(vg_lite_finish());
        SaveBMP_SFT("Clear_002_",&buffer[i],TRUE);
        verify_full_clear(&buffer[i], cc);
        vg_lite_free(&buffer[i]);
    }
    return VG_LITE_SUCCESS;

ErrorHandler:
    if(buffer[i].handle != NULL)
        vg_lite_free(&buffer[i]);
    return error;
}

/* different buffer formats, different clear modes, and part region for 1080p resolution. */
vg_lite_error_t    Clear_003()
{
    int i;
    int x, y, w, h;
    vg_lite_error_t error = VG_LITE_SUCCESS;
    vg_lite_color_t cc;
    vg_lite_buffer_t buffer[FORMAT_COUNT];
    vg_lite_rectangle_t rect;

    for(i = 0; i < FORMAT_COUNT; i++)
    {
        cc = GenColor_r();
        memset(&buffer[i],0,sizeof(vg_lite_buffer_t));
        buffer[i].width = 1920;
        buffer[i].height = 1080;
        buffer[i].format = formats[i];
        buffer[i].stride = 0;
        buffer[i].handle = NULL;
        buffer[i].memory = NULL;
        buffer[i].address = 0;
        buffer[i].tiled = 0;

        x = (int)Random_r(0, WINDSIZEX);
        y = (int)Random_r(0, WINDSIZEY);
        w = (int)Random_r(1.0f, WINDSIZEX);
        h = (int)Random_r(1.0f, WINDSIZEY);
        rect.x = x;
        rect.y = y;
        rect.width = w;
        rect.height = h;

        CHECK_ERROR(vg_lite_allocate(&buffer[i]));

        printf("color: 0x%x, dst format: 0x%x\n", cc, formats[i]);
        printf("clear with rectangle mode.\n");
        CHECK_ERROR(vg_lite_clear(&buffer[i], NULL, 0xffffffff));
        CHECK_ERROR(vg_lite_clear(&buffer[i],&rect,cc));
        CHECK_ERROR(vg_lite_finish());
        SaveBMP_SFT("Clear_003_",&buffer[i],TRUE);
        verify_rect_clear(&buffer[i], &rect, cc, 0xffffffff);
        vg_lite_free(&buffer[i]);
    }
    return VG_LITE_SUCCESS;

ErrorHandler:
    if(buffer[i].handle != NULL)
        vg_lite_free(&buffer[i]);
    return error;
}

/* different buffer formats, different clear modes, and full screen for 1080p resolution. */
vg_lite_error_t    Clear_004()
{
    int i;
    vg_lite_error_t error = VG_LITE_SUCCESS;
    vg_lite_color_t cc;
    vg_lite_buffer_t buffer[FORMAT_COUNT];

    for(i = 0; i < FORMAT_COUNT; i++)
    {
        cc = GenColor_r();
        memset(&buffer[i],0,sizeof(vg_lite_buffer_t));
        buffer[i].width = 1920;
        buffer[i].height = 1080;
        buffer[i].format = formats[i];
        buffer[i].stride = 0;
        buffer[i].handle = NULL;
        buffer[i].memory = NULL;
        buffer[i].address = 0;
        buffer[i].tiled = 0;

        CHECK_ERROR(vg_lite_allocate(&buffer[i]));

        printf("color: 0x%x, dst format: 0x%x\n", cc, formats[i]);
        printf("clear with rectangle mode.\n");
        CHECK_ERROR(vg_lite_clear(&buffer[i],NULL,cc));
        CHECK_ERROR(vg_lite_finish());
        SaveBMP_SFT("Clear_004_",&buffer[i],TRUE);
        verify_full_clear(&buffer[i], cc);
        vg_lite_free(&buffer[i]);
    }
    return VG_LITE_SUCCESS;

ErrorHandler:
    if(buffer[i].handle != NULL)
        vg_lite_free(&buffer[i]);
    return error;
}

vg_lite_error_t Clear()
{
    vg_lite_error_t error = VG_LITE_SUCCESS;
    printf("\nCase: Clear_001:::::::::::::::::::::Started\n");
    CHECK_ERROR(Clear_001());
    printf("\nCase: Clear_001:::::::::::::::::::::Ended\n");
    printf("\nCase: Clear_002:::::::::::::::::::::Started\n");
    CHECK_ERROR(Clear_002());
    printf("\nCase: Clear_002:::::::::::::::::::::Ended\n");
    printf("\nCase: Clear_003:::::::::::::::::::::Started\n");
    CHECK_ERROR(Clear_003());
    printf("\nCase: Clear_003:::::::::::::::::::::Ended\n");
    printf("\nCase: Clear_004:::::::::::::::::::::Started\n");
    CHECK_ERROR(Clear_004());
    printf("\nCase: Clear_004:::::::::::::::::::::Ended\n");

ErrorHandler:
    return error;
}

void Clear_Log()
{
}

int main(int argc, char *argv[])
{
    vg_lite_error_t error;

    (void)argc; (void)argv;

    error = vg_lite_init(0, 0);
    if (error != VG_LITE_SUCCESS) {
        printf("vg_lite_init failed: %d\n", error);
        return -1;
    }

    error = Clear();

    printf("\nGolden verification: %d passed, %d failed\n", g_golden_pass, g_golden_fail);

    vg_lite_close();
    return (error == VG_LITE_SUCCESS && g_golden_fail == 0) ? 0 : -1;
}
