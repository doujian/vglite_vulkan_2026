#ifndef VG_LITE_FORMAT_H
#define VG_LITE_FORMAT_H

#include "volk.h"
#include "vg_lite.h"

#ifdef __cplusplus
extern "C" {
#endif

uint32_t vg_lite_format_stride(vg_lite_buffer_format_t format, uint32_t width);
VkFormat vg_lite_format_to_vk(vg_lite_buffer_format_t format);
uint32_t vg_lite_format_bpp(vg_lite_buffer_format_t format);
void vg_lite_color_argb_to_vk(vg_lite_color_t argb, VkFormat vkfmt, VkClearColorValue *out);
void vg_lite_color_argb_to_float(vg_lite_color_t argb, float out[4]);

#ifdef __cplusplus
}
#endif

#endif
