#ifndef SPLASH_ASSETS_H
#define SPLASH_ASSETS_H

typedef enum
{
    SPLASH_PIXFMT_RBGA32 = 0,
    SPLASH_PIXFMT_IDX8_CLUT32 = 1,
} SPLASH_PIXFMT;

typedef struct
{
    const unsigned char *pixels;
    const unsigned char *clut_rgba;
    unsigned int clut_entries;
    unsigned int width;
    unsigned int height;
    SPLASH_PIXFMT format;
} SPLASH_IMAGE;

const SPLASH_IMAGE *SplashGetLogoImage(int is_psx_desr);
const SPLASH_IMAGE *SplashGetTemplateImage(int is_psx_desr);

#endif
