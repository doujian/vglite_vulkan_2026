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
    /* 0xFFFF0000 = R=0,G=0,B=0xFF,A=0xFF = blue in vg_lite_color_t */
    const vg_lite_color_t blue = 0xFFFF0000;

    CHECK_ERROR(vg_lite_init(0, 0));

    buffer.width  = 1920;
    buffer.height = 1080;
    buffer.format = VG_LITE_RGB565;
    CHECK_ERROR(vg_lite_allocate(&buffer));

    CHECK_ERROR(vg_lite_clear(&buffer, NULL, blue));
    CHECK_ERROR(vg_lite_finish());

    vg_lite_save_png("clear_dl.png", &buffer);

    /* Verify center pixel is blue (tolerance 8 for RGB565 quantization) */
    if (vg_lite_check_pixel(&buffer, 960, 540, blue, 8)) {
        printf("clear_dl OK (1920x1080 RGB565 blue clear)\n");
    } else {
        uint32_t px = vg_lite_read_pixel(&buffer, 960, 540);
        printf("FAIL: center pixel not blue (got 0x%08X)\n", px);
    }

ErrorHandler:
    cleanup();
    return (error == VG_LITE_SUCCESS) ? 0 : -1;
}
