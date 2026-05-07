#include "vg_lite.h"
#include "vg_lite_util.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[])
{
    vg_lite_error_t error = VG_LITE_SUCCESS;
    vg_lite_buffer_t buffer;
    int width = 256, height = 256;
    int fail = 0;

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

    error = vg_lite_clear(&buffer, NULL, 0xFFFF0000);
    if (error != VG_LITE_SUCCESS) printf("clear full failed: %d\n", error);

    vg_lite_rectangle_t rect = {64, 64, 64, 64};
    error = vg_lite_clear(&buffer, &rect, 0xFF0000FF);
    if (error != VG_LITE_SUCCESS) printf("clear rect failed: %d\n", error);

    error = vg_lite_finish();
    if (error != VG_LITE_SUCCESS) printf("finish failed: %d\n", error);

    vg_lite_save_png("clear.png", &buffer);
    printf("clear test done - saved clear.png\n");

    {
        vg_lite_expected_buffer_t *eb = vg_lite_expected_create(buffer.width, buffer.height, buffer.format);
        vg_lite_expected_clear(eb, NULL, 0xFFFF0000);
        vg_lite_expected_clear(eb, &rect, 0xFF0000FF);
        fail += vg_lite_expected_verify(eb, &buffer, 12);
        vg_lite_expected_destroy(eb);
    }

    if (fail == 0) printf("clear test PASSED\n");
    else           printf("clear test FAILED (%d mismatches)\n", fail);

    vg_lite_free(&buffer);
    vg_lite_close();
    return (fail == 0) ? 0 : -1;
}
