#ifndef SPLASH_ASSETS_H
#define SPLASH_ASSETS_H

typedef struct
{
    const unsigned char *pixels_t8;
    const unsigned char *clut_rbga;
    unsigned int width;
    unsigned int height;
    unsigned int clut_entries;
} SPLASH_IMAGE;

const SPLASH_IMAGE *SplashGetLogoImage(int is_psx_desr);
const SPLASH_IMAGE *SplashGetHotkeysImage(void);

#endif
