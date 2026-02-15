#ifndef SPLASH_ASSETS_H
#define SPLASH_ASSETS_H

#define IMPORT_BIN2C(_n)       \
    extern unsigned char _n[]; \
    extern unsigned int size_##_n

IMPORT_BIN2C(ps2bble_splash_template_png);
IMPORT_BIN2C(psxbble_splash_template_png);
IMPORT_BIN2C(transparent_ps2bble_png);
IMPORT_BIN2C(transparent_psxbble_png);

#endif
