#include "vg_lite.h"
#include "vg_lite_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[])
{
    vg_lite_error_t error = VG_LITE_SUCCESS;
    vg_lite_buffer_t buffer;
    int width = 256, height = 256;

    error = vg_lite_init(0, 0);
    if (error != VG_LITE_SUCCESS) {
        printf("vg_lite_init failed: %d\n", error);
        return -1;
    }

    memset(&buffer, 0, sizeof(buffer));
    buffer.width  = width;
    buffer.height = height;
    buffer.format = VG_LITE_RGB565;

    error = vg_lite_allocate(&buffer);
    if (error != VG_LITE_SUCCESS) {
        printf("vg_lite_allocate failed: %d\n", error);
        vg_lite_close();
        return -1;
    }

    /* Clear entire buffer to blue (ARGB: 0xFFFF0000) */
    error = vg_lite_clear(&buffer, NULL, 0xFFFF0000);
    if (error != VG_LITE_SUCCESS) printf("clear full failed: %d\n", error);

    /* Clear a 64x64 sub-rectangle at (64,64) to red (ARGB: 0xFF0000FF) */
    vg_lite_rectangle_t rect = {64, 64, 64, 64};
    error = vg_lite_clear(&buffer, &rect, 0xFF0000FF);
    if (error != VG_LITE_SUCCESS) printf("clear rect failed: %d\n", error);

    error = vg_lite_finish();
    if (error != VG_LITE_SUCCESS) printf("finish failed: %d\n", error);

    /* Save result */
    vg_lite_save_png("clear.png", &buffer);
    printf("clear test done - saved clear.png\n");

    /* Compare with golden: pixel at (0,0) should be blue, pixel at (96,96) should be red */
    unsigned char *ptr = (unsigned char *)buffer.memory;
    int ok = 1;
    /* Check pixel at (128, 128) - should be blue (0x001F in RGB565) */
    uint16_t pixel_blue = *(uint16_t*)(ptr + 128 * buffer.stride + 128 * 2);
    if ((pixel_blue & 0x1F) != 0x1F) {
        printf("FAIL: pixel at (128,128) is not blue: 0x%04x\n", pixel_blue);
        ok = 0;
    }
    /* Check pixel at (96, 96) - should be red (in rect area) */
    uint16_t pixel_red = *(uint16_t*)(ptr + 96 * buffer.stride + 96 * 2);
    if ((pixel_red & 0xF800) == 0) {
        printf("FAIL: pixel at (96,96) is not red: 0x%04x\n", pixel_red);
        ok = 0;
    }

    if (ok) printf("clear test PASSED\n");
    else printf("clear test FAILED\n");

    vg_lite_free(&buffer);
    vg_lite_close();
    return ok ? 0 : -1;
}
