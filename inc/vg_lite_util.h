#ifndef VG_LITE_UTIL_H
#define VG_LITE_UTIL_H

#include "vg_lite.h"

#ifdef __cplusplus
extern "C" {
#endif

int vg_lite_load_raw(vg_lite_buffer_t *buffer, const char *name);
int vg_lite_save_png(const char *name, vg_lite_buffer_t *buffer);
int vg_lite_load_png(vg_lite_buffer_t *buffer, const char *name);
int vg_lite_fb_open(vg_lite_buffer_t *buffer);
void vg_lite_fb_close(vg_lite_buffer_t *buffer);
void vg_lite_save_raw(const char *name, vg_lite_buffer_t *buffer);

#ifdef __cplusplus
}
#endif

#endif
