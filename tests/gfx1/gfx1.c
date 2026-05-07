#include <stdio.h>
#include <string.h>
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

    CHECK_ERROR(vg_lite_init(640, 480));

    buffer.width = 640;
    buffer.height = 480;
    buffer.format = VG_LITE_BGRA8888;
    CHECK_ERROR(vg_lite_allocate(&buffer));

    CHECK_ERROR(vg_lite_clear(&buffer, NULL, 0xFFFF0000));
    CHECK_ERROR(vg_lite_finish());

    vg_lite_save_png("gfx1.png", &buffer);

    /* Verify center pixel is blue (tolerance 0 for BGRA8888) */
    if (vg_lite_check_pixel(&buffer, 320, 240, 0xFFFF0000, 0)) {
        printf("gfx1 OK (640x480 BGRA8888 blue clear)\n");
    } else {
        printf("FAIL: center pixel not blue\n");
    }

ErrorHandler:
    cleanup();
    return (error == VG_LITE_SUCCESS) ? 0 : -1;
}
