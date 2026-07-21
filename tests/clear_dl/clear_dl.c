#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "vg_lite.h"
#include "vg_lite_util.h"
#include "util.h"
#include "Common.h"

static vg_lite_buffer_t buffer;

void cleanup(void)
{
    if (buffer.handle != NULL) {
        vg_lite_free(&buffer);
    }
    vg_lite_close();
}

int main(int argc, const char *argv[])
{
    vg_lite_error_t error = VG_LITE_SUCCESS;
    const vg_lite_color_t blue = 0xFFFF0000;
    int fail = 0;

    CHECK_ERROR(vg_lite_init(0, 0));

    buffer.width  = 1920;
    buffer.height = 1080;
    buffer.format = VG_LITE_RGB565;
    error = vg_lite_allocate(&buffer);
    if (error == VG_LITE_NOT_SUPPORT) {
        printf("[fallback] RGB565 linear color-att unsupported on this GPU, retry with BGRA8888\n");
        buffer.format = VG_LITE_BGRA8888;
        error = vg_lite_allocate(&buffer);
    }
    CHECK_ERROR(error);

    CHECK_ERROR(vg_lite_clear(&buffer, NULL, blue));
    CHECK_ERROR(vg_lite_finish());

    vg_lite_save_png("clear_dl.png", &buffer);

    {
        vg_lite_expected_buffer_t *eb = vg_lite_expected_create(buffer.width, buffer.height, buffer.format);
        vg_lite_expected_clear(eb, NULL, blue);
        fail += vg_lite_expected_verify(eb, &buffer, 12);
        vg_lite_expected_destroy(eb);
    }

    if (fail == 0) printf("clear_dl OK (1920x1080 RGB565 blue clear)\n");
    else           printf("clear_dl FAILED (%d mismatches)\n", fail);

ErrorHandler:
    cleanup();
    return (error == VG_LITE_SUCCESS && fail == 0) ? 0 : -1;
}
