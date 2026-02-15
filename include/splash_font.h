#ifndef SPLASH_FONT_H
#define SPLASH_FONT_H

#include <stdint.h>

#define SPLASH_FONT_FIRST 32
#define SPLASH_FONT_LAST 126
#define SPLASH_FONT_COUNT 95

typedef struct
{
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    int16_t xoff;
    int16_t yoff;
    int16_t xadv;
} SplashGlyph;

extern const unsigned char splash_font_rgba[];
extern const unsigned int splash_font_rgba_size;
extern const int splash_font_atlas_w;
extern const int splash_font_atlas_h;
extern const int splash_font_baseline;
extern const int splash_font_line_height;
extern const SplashGlyph splash_font_glyphs[SPLASH_FONT_COUNT];

#endif
