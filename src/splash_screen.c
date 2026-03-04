#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "splash_render.h"
#include "splash_screen.h"

#define INFO_OFFSET_X 104
#define INFO_OFFSET_Y 466

void SplashRenderTextBody(int logo_disp,
                          int is_psx_desr)
{
    int image_drawn;

    if (logo_disp < 2)
        return;

    image_drawn = SplashRenderBegin(logo_disp, is_psx_desr);

    if (!image_drawn) {
        scr_clear();
        return;
    }
}

void SplashRenderConsoleInfoLine(int logo_disp,
                                 const char *model,
                                 const char *rom_fmt,
                                 const char *dvdver,
                                 const char *ps1ver,
                                 const char *config_source_name)
{
    char info_line[256];

    if (logo_disp <= 0)
        return;

    snprintf(info_line,
             sizeof(info_line),
             "MODEL: %s  ROMVER: %s  DVD: %s  PS1DRV: %s  CFG SRC: %s",
             model,
             rom_fmt,
             dvdver,
             ps1ver,
             config_source_name);

    if (logo_disp == 1) {
        scr_printf("\n%s", info_line);
        return;
    }

    if (SplashRenderIsActive()) {
        int base_x = SplashRenderGetImageX();
        int base_y = SplashRenderGetImageY();
        SplashRenderDrawTextPx(base_x + INFO_OFFSET_X, base_y + INFO_OFFSET_Y, 0xffffff, info_line);
        SplashRenderEnd();
    }
}
