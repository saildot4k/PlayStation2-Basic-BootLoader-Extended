#include "splash_assets.h"

extern const unsigned int splash_ps2bble_logo_unified_width;
extern const unsigned int splash_ps2bble_logo_unified_height;
extern const unsigned char splash_ps2bble_logo_unified_rbg[];

extern const unsigned int splash_psxbble_logo_unified_width;
extern const unsigned int splash_psxbble_logo_unified_height;
extern const unsigned char splash_psxbble_logo_unified_rbg[];

extern const unsigned int splash_hotkeys_width;
extern const unsigned int splash_hotkeys_height;
extern const unsigned char splash_hotkeys_rbg[];

const SPLASH_IMAGE *SplashGetLogoImage(int is_psx_desr)
{
    static SPLASH_IMAGE logo_ps2;
    static SPLASH_IMAGE logo_psx;

    logo_ps2.pixels_rbg = splash_ps2bble_logo_unified_rbg;
    logo_ps2.width = splash_ps2bble_logo_unified_width;
    logo_ps2.height = splash_ps2bble_logo_unified_height;

    logo_psx.pixels_rbg = splash_psxbble_logo_unified_rbg;
    logo_psx.width = splash_psxbble_logo_unified_width;
    logo_psx.height = splash_psxbble_logo_unified_height;

    return is_psx_desr ? &logo_psx : &logo_ps2;
}

const SPLASH_IMAGE *SplashGetHotkeysImage(void)
{
    static SPLASH_IMAGE hotkeys;

    hotkeys.pixels_rbg = splash_hotkeys_rbg;
    hotkeys.width = splash_hotkeys_width;
    hotkeys.height = splash_hotkeys_height;

    return &hotkeys;
}
