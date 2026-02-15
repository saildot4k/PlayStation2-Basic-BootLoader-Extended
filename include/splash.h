#ifndef SPLASH_H
#define SPLASH_H

#include <stdint.h>

typedef struct
{
    int start_x; // pixel coordinates
    int start_y; // pixel coordinates
    int max_chars; // per line, 0 = no limit
    int coords_are_image; // 1=coords in image space, 0=screen space
} SplashTextConfig;

typedef struct
{
    const unsigned char *data;
    unsigned int size;
} SplashImage;

typedef struct
{
    void *gs;
    int screen_w;
    int screen_h;
    int img_w;
    int img_h;
    float img_scale;
    int img_off_x;
    int img_off_y;
} SplashContext;

int SplashBegin(SplashContext *ctx);
void SplashEnd(SplashContext *ctx);
int SplashGetImageForLogoDisplay(int logo_disp, SplashImage *out);
int SplashDrawImage(SplashContext *ctx, const SplashImage *image);
void SplashDrawText(SplashContext *ctx, const char *text, const SplashTextConfig *cfg, uint32_t rgb);
void SplashGetScreenSize(const SplashContext *ctx, int *out_w, int *out_h);
void SplashTransformPoint(const SplashContext *ctx, int in_x, int in_y, int *out_x, int *out_y, int coords_are_image);

#endif
