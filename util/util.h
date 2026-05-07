#ifndef __SFT_UTIL_H__
#define __SFT_UTIL_H__

#include "vg_lite.h"
#include "vg_lite_util.h"

#define TRUE 1
#define FALSE 0
typedef int BOOL;

void SaveBMP_SFT(char * name, vg_lite_buffer_t *buffer, BOOL save);

float       Random_r(float, float);
unsigned long int Random_i(unsigned long int, unsigned long int);
void        random_srand(unsigned int seed);
vg_lite_color_t GenColor_r(void);

uint32_t get_bpp(vg_lite_buffer_format_t format);
uint32_t pack_pixel(vg_lite_buffer_format_t format, uint32_t r, uint32_t g, uint32_t b, uint32_t a);
void unpack_rgba(uint32_t pixel, int *r, int *g, int *b, int *a);

extern float WINDSIZEX;
extern float WINDSIZEY;

extern char *error_type[];

int InitBMP(int width, int height);
void DestroyBMP(void);
int SaveBMP(char *image_name, unsigned char* p, int width, int height, vg_lite_buffer_format_t format, int stride);

void Get_Dirs(void);
extern char *MAIN_DIR;
extern char *BMP_DIR;

void SavePNG(vg_lite_buffer_t *buf, char *file_name, int i);

typedef struct {
    int x;
    int y;
    int w;
    int h;
} rect_s;
extern rect_s c_rect[15];

uint32_t vg_lite_read_pixel(vg_lite_buffer_t *buffer, int x, int y);
int vg_lite_check_pixel(vg_lite_buffer_t *buffer, int x, int y, uint32_t expected, int tolerance);

/* Expected buffer: CPU-side pixel array that mirrors GPU operations.
 * Supports multiple clear/blit/copy ops before final verification.
 * Internally stored as RGBA8888 regardless of GPU format. */
typedef struct vg_lite_expected_buffer vg_lite_expected_buffer_t;

vg_lite_expected_buffer_t *vg_lite_expected_create(int width, int height,
                                                    vg_lite_buffer_format_t format);
void vg_lite_expected_destroy(vg_lite_expected_buffer_t *eb);
void vg_lite_expected_clear(vg_lite_expected_buffer_t *eb,
                             vg_lite_rectangle_t *rect,
                             vg_lite_color_t color);
void vg_lite_expected_blit(vg_lite_expected_buffer_t *eb,
                            vg_lite_buffer_t *src,
                            vg_lite_matrix_t *matrix,
                            int blend_mode, int filter);
void vg_lite_expected_copy(vg_lite_expected_buffer_t *eb, vg_lite_buffer_t *buf);
int vg_lite_expected_verify(vg_lite_expected_buffer_t *eb,
                             vg_lite_buffer_t *actual,
                             int tolerance);

#endif
