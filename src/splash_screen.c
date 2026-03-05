#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "splash_render.h"
#include "splash_screen.h"

// Console info text anchor for LOGO_DISPLAY = 2-5, in center-relative pixels.
#define INFO_X_FROM_CENTER (-216)
#define INFO_Y_FROM_CENTER (214)

void SplashRenderTextBody(int logo_disp,
                          int is_psx_desr)
{
    if (logo_disp < 2)
        return;

    if (!SplashRenderBegin(logo_disp, is_psx_desr))
        return;
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
        int x = SplashRenderGetScreenCenterX() + INFO_X_FROM_CENTER;
        int y = SplashRenderGetScreenCenterY() + INFO_Y_FROM_CENTER;
        SplashRenderDrawTextPx(x, y, 0xffffff, info_line);
        SplashRenderEnd();
    }
}
