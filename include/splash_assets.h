#ifndef SPLASH_ASSETS_H
#define SPLASH_ASSETS_H

typedef struct
{
    const unsigned char *pixels_rbga;
    unsigned int width;
    unsigned int height;
} SPLASH_IMAGE;

const SPLASH_IMAGE *SplashGetBackgroundImage(int is_psx_desr);
const SPLASH_IMAGE *SplashGetLogoImage(int is_psx_desr);
const SPLASH_IMAGE *SplashGetHotkeysImage(void);
const SPLASH_IMAGE *SplashGetFontBitmapImage(void);

#endif
