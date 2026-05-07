#include "vg_lite_format.h"
#include <stdint.h>

#define ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))

uint32_t vg_lite_format_bpp(vg_lite_buffer_format_t format)
{
    switch (format) {
    case VG_LITE_RGBA8888: case VG_LITE_BGRA8888: case VG_LITE_RGBX8888:
    case VG_LITE_BGRX8888: case VG_LITE_ARGB8888: case VG_LITE_ABGR8888:
    case VG_LITE_XBGR8888: case VG_LITE_XRGB8888:
        return 32;
    case VG_LITE_RGB565: case VG_LITE_BGR565:
    case VG_LITE_RGBA4444: case VG_LITE_BGRA4444:
    case VG_LITE_ABGR4444: case VG_LITE_ARGB4444:
    case VG_LITE_BGRA5551: case VG_LITE_RGBA5551:
    case VG_LITE_ARGB1555: case VG_LITE_ABGR1555:
        return 16;
    case VG_LITE_A8: case VG_LITE_L8:
        return 8;
    case VG_LITE_A4:
        return 4;
    default:
        return 32;
    }
}

uint32_t vg_lite_format_stride(vg_lite_buffer_format_t format, uint32_t width)
{
    uint32_t bpp = vg_lite_format_bpp(format);
    uint32_t bytes_per_row = (width * bpp + 7) / 8;
    return ALIGN(bytes_per_row, 64);
}

VkFormat vg_lite_format_to_vk(vg_lite_buffer_format_t format)
{
    switch (format) {
    case VG_LITE_RGBA8888: return VK_FORMAT_R8G8B8A8_UNORM;
    case VG_LITE_BGRA8888: return VK_FORMAT_B8G8R8A8_UNORM;
    case VG_LITE_RGBX8888: return VK_FORMAT_R8G8B8A8_UNORM;
    case VG_LITE_BGRX8888: return VK_FORMAT_B8G8R8A8_UNORM;
    case VG_LITE_RGB565:   return VK_FORMAT_R5G6B5_UNORM_PACK16;
    case VG_LITE_BGR565:   return VK_FORMAT_B5G6R5_UNORM_PACK16;
    case VG_LITE_A8:       return VK_FORMAT_R8_UNORM;
    case VG_LITE_L8:       return VK_FORMAT_R8_UNORM;
    case VG_LITE_ARGB8888: return VK_FORMAT_B8G8R8A8_UNORM;
    case VG_LITE_ABGR8888: return VK_FORMAT_R8G8B8A8_UNORM;
    case VG_LITE_RGBA4444: return VK_FORMAT_R4G4B4A4_UNORM_PACK16;
    case VG_LITE_BGRA4444: return VK_FORMAT_B4G4R4A4_UNORM_PACK16;
    default:               return VK_FORMAT_B8G8R8A8_UNORM;
    }
}

void vg_lite_color_argb_to_vk(vg_lite_color_t color, VkFormat vkfmt, VkClearColorValue *out)
{
    /* vg_lite_color_t = r | (g << 8) | (b << 16) | (a << 24) */
    uint8_t r = (color)       & 0xFF;
    uint8_t g = (color >> 8)  & 0xFF;
    uint8_t b = (color >> 16) & 0xFF;
    uint8_t a = (color >> 24) & 0xFF;

    /* VkClearColorValue float32 is always R,G,B,A regardless of format */
    out->float32[0] = (float)r / 255.0f;
    out->float32[1] = (float)g / 255.0f;
    out->float32[2] = (float)b / 255.0f;
    out->float32[3] = (float)a / 255.0f;
}

void vg_lite_color_argb_to_float(vg_lite_color_t color, float out[4])
{
    /* vg_lite_color_t = r | (g << 8) | (b << 16) | (a << 24) */
    uint8_t r = (color)       & 0xFF;
    uint8_t g = (color >> 8)  & 0xFF;
    uint8_t b = (color >> 16) & 0xFF;
    uint8_t a = (color >> 24) & 0xFF;
    out[0] = (float)r / 255.0f;
    out[1] = (float)g / 255.0f;
    out[2] = (float)b / 255.0f;
    out[3] = (float)a / 255.0f;
}
