#include "splash_assets.h"

extern const unsigned int splash_ps2bble_logo_width;
extern const unsigned int splash_ps2bble_logo_height;
extern const unsigned char splash_ps2bble_logo_rbga[];

extern const unsigned int splash_psxbble_logo_width;
extern const unsigned int splash_psxbble_logo_height;
extern const unsigned char splash_psxbble_logo_rbga[];

extern const unsigned int splash_ps2bbl_extended_splash_template_640x480_width;
extern const unsigned int splash_ps2bbl_extended_splash_template_640x480_height;
extern const unsigned char splash_ps2bbl_extended_splash_template_640x480_rbga[];

extern const unsigned int splash_psxbbl_extended_splash_template_640x480_width;
extern const unsigned int splash_psxbbl_extended_splash_template_640x480_height;
extern const unsigned char splash_psxbbl_extended_splash_template_640x480_rbga[];

static const SPLASH_IMAGE g_logo_ps2 = {
    splash_ps2bble_logo_rbga,
    0u,
    0u,
};

static const SPLASH_IMAGE g_logo_psx = {
    splash_psxbble_logo_rbga,
    0u,
    0u,
};

static const SPLASH_IMAGE g_template_ps2 = {
    splash_ps2bbl_extended_splash_template_640x480_rbga,
    0u,
    0u,
};

static const SPLASH_IMAGE g_template_psx = {
    splash_psxbbl_extended_splash_template_640x480_rbga,
    0u,
    0u,
};

const SPLASH_IMAGE *SplashGetLogoImage(int is_psx_desr)
{
    static SPLASH_IMAGE logo_ps2;
    static SPLASH_IMAGE logo_psx;
    logo_ps2 = g_logo_ps2;
    logo_ps2.width = splash_ps2bble_logo_width;
    logo_ps2.height = splash_ps2bble_logo_height;
    logo_psx = g_logo_psx;
    logo_psx.width = splash_psxbble_logo_width;
    logo_psx.height = splash_psxbble_logo_height;
    return is_psx_desr ? &logo_psx : &logo_ps2;
}

const SPLASH_IMAGE *SplashGetTemplateImage(int is_psx_desr)
{
    static SPLASH_IMAGE tpl_ps2;
    static SPLASH_IMAGE tpl_psx;
    tpl_ps2 = g_template_ps2;
    tpl_ps2.width = splash_ps2bbl_extended_splash_template_640x480_width;
    tpl_ps2.height = splash_ps2bbl_extended_splash_template_640x480_height;
    tpl_psx = g_template_psx;
    tpl_psx.width = splash_psxbbl_extended_splash_template_640x480_width;
    tpl_psx.height = splash_psxbbl_extended_splash_template_640x480_height;
    return is_psx_desr ? &tpl_psx : &tpl_ps2;
}
