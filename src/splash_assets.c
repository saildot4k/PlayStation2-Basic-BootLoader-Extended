#include "splash_assets.h"

extern const unsigned int splash_ps2bble_logo_unified_width;
extern const unsigned int splash_ps2bble_logo_unified_height;
extern const unsigned int splash_ps2bble_logo_unified_clut_entries;
extern const unsigned char splash_ps2bble_logo_unified_t8[];
extern const unsigned char splash_ps2bble_logo_unified_clut_rbga[];

extern const unsigned int splash_psxbble_logo_unified_width;
extern const unsigned int splash_psxbble_logo_unified_height;
extern const unsigned int splash_psxbble_logo_unified_clut_entries;
extern const unsigned char splash_psxbble_logo_unified_t8[];
extern const unsigned char splash_psxbble_logo_unified_clut_rbga[];

extern const unsigned int splash_hotkeys_width;
extern const unsigned int splash_hotkeys_height;
extern const unsigned int splash_hotkeys_clut_entries;
extern const unsigned char splash_hotkeys_t8[];
extern const unsigned char splash_hotkeys_clut_rbga[];

const SPLASH_IMAGE *SplashGetLogoImage(int is_psx_desr)
{
    static SPLASH_IMAGE logo_ps2;
    static SPLASH_IMAGE logo_psx;

    logo_ps2.pixels_t8 = splash_ps2bble_logo_unified_t8;
    logo_ps2.clut_rbga = splash_ps2bble_logo_unified_clut_rbga;
    logo_ps2.width = splash_ps2bble_logo_unified_width;
    logo_ps2.height = splash_ps2bble_logo_unified_height;
    logo_ps2.clut_entries = splash_ps2bble_logo_unified_clut_entries;

    logo_psx.pixels_t8 = splash_psxbble_logo_unified_t8;
    logo_psx.clut_rbga = splash_psxbble_logo_unified_clut_rbga;
    logo_psx.width = splash_psxbble_logo_unified_width;
    logo_psx.height = splash_psxbble_logo_unified_height;
    logo_psx.clut_entries = splash_psxbble_logo_unified_clut_entries;

    return is_psx_desr ? &logo_psx : &logo_ps2;
}

const SPLASH_IMAGE *SplashGetHotkeysImage(void)
{
    static SPLASH_IMAGE hotkeys;

    hotkeys.pixels_t8 = splash_hotkeys_t8;
    hotkeys.clut_rbga = splash_hotkeys_clut_rbga;
    hotkeys.width = splash_hotkeys_width;
    hotkeys.height = splash_hotkeys_height;
    hotkeys.clut_entries = splash_hotkeys_clut_entries;

    return &hotkeys;
}
