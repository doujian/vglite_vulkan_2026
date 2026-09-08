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
const char *vg_lite_dump_dir(void);

/* Debug helper: dump the raw GPU-side memory of a buffer's image to
 * "<dumpdir>/raw_<tag>_<fmt>_<w>x<h>_<lin|opt>_rp<rowPitch>_sz<size>.bin".
 * OPTIMAL buffers: driver-private physical bytes (impl-defined layout, not
 * directly readable as pixels; requires host-visible memory, llvmpipe OK).
 * LINEAR buffers: the API-contract bytes (stride*height; A4/sRGBA dump the
 * VGLite-layout shadow, not the expanded GPU view). */
vg_lite_error_t vg_lite_dump_raw(const char *tag, vg_lite_buffer_t *buffer);
/* Debug: force OPTIMAL image allocations into HOST_VISIBLE memory so
 * vg_lite_dump_raw can map them (call before vg_lite_allocate). */
void vg_lite_dump_enable_host_optimal(int enable);

/* Returns the configuration-specific dump subdirectory that
 * vg_lite_save_png routes output into (e.g. "dump_lin_msaa_obb"). */
const char *vg_lite_dump_subdir(void);

#ifdef __cplusplus
}
#endif

#endif
