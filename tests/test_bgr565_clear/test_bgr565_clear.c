#include "vg_lite.h"
#include "vg_lite_util.h"
#include "util.h"
#include "Common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
    vg_lite_error_t err;
    err = vg_lite_init(32, 32); if (err) { printf("init fail %d\n", err); return 1; }

    vg_lite_color_t colors[] = {0xFF0000FF, 0xFF00FF00, 0xFFFF0000, 0xFFA0A0A0};
    const char* names[] = {"red(R=FF)", "green(G=FF)", "blue(B=FF)", "gray(A0)"};

    for (int c = 0; c < 4; c++) {
        vg_lite_buffer_t target = {0};
        target.width = 2; target.height = 1; target.format = VG_LITE_BGR565;
        err = vg_lite_allocate(&target); if (err) return 1;
        err = vg_lite_clear(&target, NULL, colors[c]); if (err) return 1;
        err = vg_lite_finish(); if (err) return 1;
        uint16_t *p = (uint16_t*)target.memory;
        uint8_t r5 = p[0] & 0x1F;
        uint8_t g6 = (p[0] >> 5) & 0x3F;
        uint8_t b5 = (p[0] >> 11) & 0x1F;
        printf("BGR565 %s: raw=0x%04X R5=%d G6=%d B5=%d\n", names[c], p[0], r5, g6, b5);
        vg_lite_free(&target);
    }

    /* Same test for RGB565 */
    for (int c = 0; c < 4; c++) {
        vg_lite_buffer_t target = {0};
        target.width = 2; target.height = 1; target.format = VG_LITE_RGB565;
        err = vg_lite_allocate(&target);
        if (err == VG_LITE_NOT_SUPPORT) { printf("RGB565 (B5G6R5) not supported on this GPU, skipped\n"); break; }
        if (err) return 1;
        err = vg_lite_clear(&target, NULL, colors[c]); if (err) return 1;
        err = vg_lite_finish(); if (err) return 1;
        uint16_t *p = (uint16_t*)target.memory;
        uint8_t hi = (p[0] >> 11) & 0x1F;
        uint8_t mid = (p[0] >> 5) & 0x3F;
        uint8_t lo = p[0] & 0x1F;
        printf("RGB565 %s: raw=0x%04X hi=%d mid=%d lo=%d\n", names[c], p[0], hi, mid, lo);
        vg_lite_free(&target);
    }

    /* Same test for BGRA8888 */
    for (int c = 0; c < 4; c++) {
        vg_lite_buffer_t target = {0};
        target.width = 2; target.height = 1; target.format = VG_LITE_BGRA8888;
        err = vg_lite_allocate(&target); if (err) return 1;
        err = vg_lite_clear(&target, NULL, colors[c]); if (err) return 1;
        err = vg_lite_finish(); if (err) return 1;
        uint32_t *p = (uint32_t*)target.memory;
        uint8_t b = p[0] & 0xFF, g = (p[0]>>8) & 0xFF, r = (p[0]>>16) & 0xFF, a = (p[0]>>24) & 0xFF;
        printf("BGRA8888 %s: raw=0x%08X R=%d G=%d B=%d A=%d\n", names[c], p[0], r, g, b, a);
        vg_lite_free(&target);
    }

    vg_lite_close();
    return 0;
}
