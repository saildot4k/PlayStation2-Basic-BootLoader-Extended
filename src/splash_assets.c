// Embedded splash image/font asset metadata and accessors.
#include "splash_assets.h"

extern const unsigned int splash_bg_ps2bble_width;
extern const unsigned int splash_bg_ps2bble_height;
extern const unsigned char splash_bg_ps2bble_rbga[];

extern const unsigned int splash_bg_psxbble_width;
extern const unsigned int splash_bg_psxbble_height;
extern const unsigned char splash_bg_psxbble_rbga[];

extern const unsigned int splash_logo_ps2bble_width;
extern const unsigned int splash_logo_ps2bble_height;
extern const unsigned char splash_logo_ps2bble_rbga[];

extern const unsigned int splash_logo_psxbble_width;
extern const unsigned int splash_logo_psxbble_height;
extern const unsigned char splash_logo_psxbble_rbga[];

extern const unsigned int splash_hotkeys_width;
extern const unsigned int splash_hotkeys_height;
extern const unsigned char splash_hotkeys_rbga[];

const SPLASH_IMAGE *SplashGetBackgroundImage(int is_psx_desr)
{
    static SPLASH_IMAGE bg_ps2;
    static SPLASH_IMAGE bg_psx;

    bg_ps2.pixels_rbga = splash_bg_ps2bble_rbga;
    bg_ps2.width = splash_bg_ps2bble_width;
    bg_ps2.height = splash_bg_ps2bble_height;

    bg_psx.pixels_rbga = splash_bg_psxbble_rbga;
    bg_psx.width = splash_bg_psxbble_width;
    bg_psx.height = splash_bg_psxbble_height;

    return is_psx_desr ? &bg_psx : &bg_ps2;
}

const SPLASH_IMAGE *SplashGetLogoImage(int is_psx_desr)
{
    static SPLASH_IMAGE logo_ps2;
    static SPLASH_IMAGE logo_psx;

    logo_ps2.pixels_rbga = splash_logo_ps2bble_rbga;
    logo_ps2.width = splash_logo_ps2bble_width;
    logo_ps2.height = splash_logo_ps2bble_height;

    logo_psx.pixels_rbga = splash_logo_psxbble_rbga;
    logo_psx.width = splash_logo_psxbble_width;
    logo_psx.height = splash_logo_psxbble_height;

    return is_psx_desr ? &logo_psx : &logo_ps2;
}

const SPLASH_IMAGE *SplashGetHotkeysImage(void)
{
    static SPLASH_IMAGE hotkeys;

    hotkeys.pixels_rbga = splash_hotkeys_rbga;
    hotkeys.width = splash_hotkeys_width;
    hotkeys.height = splash_hotkeys_height;

    return &hotkeys;
}
