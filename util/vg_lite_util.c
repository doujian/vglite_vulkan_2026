#include "vg_lite.h"
#include "vg_lite_util.h"
#include "stb_image.h"
#include "stb_image_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Read a little-endian 32-bit integer from file */
static int read_long(FILE *fp)
{
    unsigned char buf[4];
    if (fread(buf, 1, 4, fp) != 4) return 0;
    return (int)((unsigned)buf[0] |
                 ((unsigned)buf[1] << 8) |
                 ((unsigned)buf[2] << 16) |
                 ((unsigned)buf[3] << 24));
}

int vg_lite_load_raw(vg_lite_buffer_t *buffer, const char *name)
{
    FILE *fp = fopen(name, "rb");
    if (!fp) return 1;

    int format;
    int raw_stride;

    buffer->width  = read_long(fp);
    buffer->height = read_long(fp);
    raw_stride     = read_long(fp);
    format         = read_long(fp);

    switch (format) {
    case 0:
    default:
        buffer->format = VG_LITE_RGBA8888;
        break;
    }

    if (vg_lite_allocate(buffer) != VG_LITE_SUCCESS) {
        fclose(fp);
        return -1;
    }

    fseek(fp, 16, SEEK_SET);

    if (raw_stride == 0 || raw_stride == (int)buffer->stride) {
        int flag = fread(buffer->memory, buffer->stride * buffer->height, 1, fp);
        fclose(fp);
        if (flag != 1) { vg_lite_free(buffer); return -1; }
    } else {
        unsigned char *dst = (unsigned char *)buffer->memory;
        for (int y = 0; y < buffer->height; y++) {
            if (fread(dst, raw_stride, 1, fp) != 1) {
                fclose(fp);
                vg_lite_free(buffer);
                return -1;
            }
            dst += buffer->stride;
        }
        fclose(fp);
    }

    return 0;
}

int vg_lite_save_png(const char *name, vg_lite_buffer_t *buffer)
{
    int channels = 4;
    int bpp = 32;
    switch (buffer->format) {
    case VG_LITE_RGB565: case VG_LITE_BGR565: bpp = 16; channels = 3; break;
    case VG_LITE_A8: case VG_LITE_L8: bpp = 8; channels = 1; break;
    default: break;
    }

    unsigned char *rgba = malloc(buffer->width * buffer->height * 4);
    if (!rgba) return 0;

    int is_bgra = (buffer->format == VG_LITE_BGRA8888 ||
                   buffer->format == VG_LITE_BGRX8888 ||
                   buffer->format == VG_LITE_BGR565);
    unsigned char *src = (unsigned char *)buffer->memory;
    for (int y = 0; y < buffer->height; y++) {
        for (int x = 0; x < buffer->width; x++) {
            int di = (y * buffer->width + x) * 4;
            int si = y * buffer->stride;
            if (bpp == 32) {
                si += x * 4;
                if (is_bgra) {
                    rgba[di+0] = src[si+2];
                    rgba[di+1] = src[si+1];
                    rgba[di+2] = src[si+0];
                    rgba[di+3] = src[si+3];
                } else {
                    rgba[di+0] = src[si+0];
                    rgba[di+1] = src[si+1];
                    rgba[di+2] = src[si+2];
                    rgba[di+3] = src[si+3];
                }
            } else if (bpp == 16) {
                si += x * 2;
                uint16_t p = *(uint16_t*)(src + si);
                rgba[di+0] = (unsigned char)(((p >> 11) & 0x1F) * 255 / 31);
                rgba[di+1] = (unsigned char)(((p >> 5) & 0x3F) * 255 / 63);
                rgba[di+2] = (unsigned char)((p & 0x1F) * 255 / 31);
                rgba[di+3] = 255;
            } else {
                si += x;
                rgba[di+0] = src[si];
                rgba[di+1] = src[si];
                rgba[di+2] = src[si];
                rgba[di+3] = 255;
            }
        }
    }

    int ok = stbi_write_png(name, buffer->width, buffer->height, 4, rgba, buffer->width * 4);
    free(rgba);
    return ok;
}

int vg_lite_load_png(vg_lite_buffer_t *buffer, const char *name)
{
    int w, h, n;
    /* Force 4 channels (RGBA) */
    unsigned char *data = stbi_load(name, &w, &h, &n, 4);
    if (!data) return 1;

    buffer->width = w;
    buffer->height = h;
    buffer->format = VG_LITE_BGRA8888;

    if (vg_lite_allocate(buffer) != VG_LITE_SUCCESS) {
        stbi_image_free(data);
        return -1;
    }

    /* stb_image returns RGBA order, VGLite BGRA8888 expects B,G,R,A in memory */
    unsigned char *dst = (unsigned char *)buffer->memory;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int si = (y * w + x) * 4;
            int di = y * buffer->stride + x * 4;
            dst[di+0] = data[si+2]; /* B */
            dst[di+1] = data[si+1]; /* G */
            dst[di+2] = data[si+0]; /* R */
            dst[di+3] = data[si+3]; /* A */
        }
    }
    stbi_image_free(data);
    return 0;
}

int vg_lite_fb_open(vg_lite_buffer_t *buffer) { (void)buffer; return 0; }
void vg_lite_fb_close(void) {}

void vg_lite_save_raw(const char *name, vg_lite_buffer_t *buffer)
{
    if (!name || !buffer) return;
    FILE *fp = fopen(name, "wb");
    if (!fp) return;

    /* Write 16-byte header */
    int32_t header[4];
    header[0] = buffer->width;
    header[1] = buffer->height;
    header[2] = buffer->stride;
    header[3] = (int32_t)buffer->format;
    fwrite(header, 4, 4, fp);

    /* Write pixel data */
    unsigned char *ptr = (unsigned char *)buffer->memory;
    for (int y = 0; y < buffer->height; y++) {
        fwrite(ptr, 1, buffer->stride, fp);
        ptr += buffer->stride;
    }
    fclose(fp);
}
