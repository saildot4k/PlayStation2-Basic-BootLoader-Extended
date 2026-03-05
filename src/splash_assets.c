#include "splash_assets.h"

extern const unsigned int splash_ps2bble_logo_width;
extern const unsigned int splash_ps2bble_logo_height;
extern const unsigned char splash_ps2bble_logo_rbga[];
extern const unsigned char splash_ps2bble_logo_idx8[];
extern const unsigned char splash_ps2bble_logo_clut_rgba[];

extern const unsigned int splash_psxbble_logo_width;
extern const unsigned int splash_psxbble_logo_height;
extern const unsigned char splash_psxbble_logo_rbga[];
extern const unsigned char splash_psxbble_logo_idx8[];
extern const unsigned char splash_psxbble_logo_clut_rgba[];

extern const unsigned int splash_ps2bbl_extended_splash_template_640x480_width;
extern const unsigned int splash_ps2bbl_extended_splash_template_640x480_height;
extern const unsigned char splash_ps2bbl_extended_splash_template_640x480_idx8[];
extern const unsigned char splash_ps2bbl_extended_splash_template_640x480_clut_rgba[];

extern const unsigned int splash_psxbbl_extended_splash_template_640x480_width;
extern const unsigned int splash_psxbbl_extended_splash_template_640x480_height;
extern const unsigned char splash_psxbbl_extended_splash_template_640x480_idx8[];
extern const unsigned char splash_psxbbl_extended_splash_template_640x480_clut_rgba[];

static const SPLASH_IMAGE g_logo_ps2 = {
    splash_ps2bble_logo_idx8,
    splash_ps2bble_logo_clut_rgba,
    256u,
    0u,
    0u,
    SPLASH_PIXFMT_IDX8_CLUT32,
};

static const SPLASH_IMAGE g_logo_psx = {
    splash_psxbble_logo_idx8,
    splash_psxbble_logo_clut_rgba,
    256u,
    0u,
    0u,
    SPLASH_PIXFMT_IDX8_CLUT32,
};

static const SPLASH_IMAGE g_template_ps2 = {
    splash_ps2bbl_extended_splash_template_640x480_idx8,
    splash_ps2bbl_extended_splash_template_640x480_clut_rgba,
    256u,
    0u,
    0u,
    SPLASH_PIXFMT_IDX8_CLUT32,
};

static const SPLASH_IMAGE g_template_psx = {
    splash_psxbbl_extended_splash_template_640x480_idx8,
    splash_psxbbl_extended_splash_template_640x480_clut_rgba,
    256u,
    0u,
    0u,
    SPLASH_PIXFMT_IDX8_CLUT32,
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
