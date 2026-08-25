#include "vg_lite_format.h"
#include <stdint.h>

#define ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))

uint32_t vg_lite_format_bpp(vg_lite_buffer_format_t format)
{
    switch (format) {
    case VG_LITE_RGBA8888: case VG_LITE_BGRA8888: case VG_LITE_RGBX8888:
    case VG_LITE_BGRX8888: case VG_LITE_ARGB8888: case VG_LITE_ABGR8888:
    case VG_LITE_XBGR8888: case VG_LITE_XRGB8888:
    case OPENVG_sRGBA_8888:
        return 32;
    case VG_LITE_RGB565: case VG_LITE_BGR565:
    case VG_LITE_RGBA4444: case VG_LITE_BGRA4444:
    case VG_LITE_ABGR4444: case VG_LITE_ARGB4444:
    case VG_LITE_BGRA5551: case VG_LITE_RGBA5551:
    case VG_LITE_ARGB1555: case VG_LITE_ABGR1555:
        return 16;
    case VG_LITE_A8: case VG_LITE_L8: case VG_LITE_INDEX_8:
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
    case VG_LITE_RGB565:   return VK_FORMAT_B5G6R5_UNORM_PACK16;
    case VG_LITE_BGR565:   return VK_FORMAT_R5G6B5_UNORM_PACK16;
    case VG_LITE_A8:       return VK_FORMAT_R8_UNORM;
    case VG_LITE_A4:       return VK_FORMAT_R8_UNORM; /* 4bpp expanded to 1B/px on GPU */
    case VG_LITE_L8:       return VK_FORMAT_R8_UNORM;
    case VG_LITE_INDEX_8:  return VK_FORMAT_R8_UNORM;
    case VG_LITE_ARGB8888: return VK_FORMAT_R8G8B8A8_UNORM;
    /* OpenVG sRGBA_8888 is MSB-first named: word bits R=31:24,G=23:16,B=15:8,
     * A=7:0, i.e. CPU/VGLite memory [A,B,G,R]. The GPU image stores rotated
     * [R,G,B,A] words (vg_lite_srgb_sync_to_gpu rotates on upload) so the
     * _SRGB hardware decode hits exactly R,G,B and alpha passes through.
     * A view swizzle cannot be used: llvmpipe decodes BEFORE the swizzle,
     * which would decode the VGLite alpha byte instead. */
    case OPENVG_sRGBA_8888: return VK_FORMAT_R8G8B8A8_SRGB;
    case VG_LITE_ABGR8888: return VK_FORMAT_A8B8G8R8_UNORM_PACK32;
    case VG_LITE_RGBA4444: return VK_FORMAT_R4G4B4A4_UNORM_PACK16;
    case VG_LITE_BGRA4444: return VK_FORMAT_B4G4R4A4_UNORM_PACK16;
    /* VGLite names 16-bit formats LSB-first (first letter = lowest bits),
     * VK PACK16 names are MSB-first. Verified against the RGB565->B5G6R5
     * anchor. RGBA5551 aliases BGRA5551 onto A1R5G5B5 because llvmpipe does
     * not accept A1B5G5R5 (extension-only token) as an attachment: the CPU
     * pack/read helpers follow the physical VK layout, so the VGLite doc
     * bit positions are just an alias and no swizzle is needed. */
    case VG_LITE_RGBA5551: return VK_FORMAT_A1R5G5B5_UNORM_PACK16;
    case VG_LITE_BGRA5551: return VK_FORMAT_A1R5G5B5_UNORM_PACK16;
    case VG_LITE_ARGB1555: return VK_FORMAT_B5G5R5A1_UNORM_PACK16;
    case VG_LITE_ABGR1555: return VK_FORMAT_R5G5B5A1_UNORM_PACK16;
    default:               return VK_FORMAT_B8G8R8A8_UNORM;
    }
}

int vg_lite_is_yuv_format(vg_lite_buffer_format_t format)
{
    switch (format) {
    case VG_LITE_NV12: case VG_LITE_NV16: case VG_LITE_NV24:
    case VG_LITE_ANV12: case VG_LITE_AYUY2:
    case VG_LITE_YV12: case VG_LITE_YV16: case VG_LITE_YV24:
    case VG_LITE_YUYV: case VG_LITE_YUY2:
    case VG_LITE_NV12_TILED: case VG_LITE_ANV12_TILED:
    case VG_LITE_AYUY2_TILED: case VG_LITE_YUY2_TILED:
    case VG_LITE_NV24_TILED:
        return 1;
    default:
        return 0;
    }
}

void vg_lite_color_argb_to_vk(vg_lite_color_t color, VkFormat vkfmt, VkClearColorValue *out)
{
    uint8_t r = (color)       & 0xFF;
    uint8_t g = (color >> 8)  & 0xFF;
    uint8_t b = (color >> 16) & 0xFF;
    uint8_t a = (color >> 24) & 0xFF;

    out->float32[0] = (float)r / 255.0f;
    out->float32[1] = (float)g / 255.0f;
    out->float32[2] = (float)b / 255.0f;
    out->float32[3] = (float)a / 255.0f;
}

void vg_lite_color_argb_to_float(vg_lite_color_t color, float out[4])
{
    uint8_t r = (color)       & 0xFF;
    uint8_t g = (color >> 8)  & 0xFF;
    uint8_t b = (color >> 16) & 0xFF;
    uint8_t a = (color >> 24) & 0xFF;
    out[0] = (float)r / 255.0f;
    out[1] = (float)g / 255.0f;
    out[2] = (float)b / 255.0f;
    out[3] = (float)a / 255.0f;
}
