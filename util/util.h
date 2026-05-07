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

#endif
