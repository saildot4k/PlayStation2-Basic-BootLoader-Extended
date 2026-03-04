#ifndef SPLASH_SCREEN_H
#define SPLASH_SCREEN_H

#include <tamtypes.h>

void SplashRenderTextBody(int logo_disp,
                          int is_psx_desr,
                          const char *const keynames[17]);

void SplashRenderConsoleInfoLine(int logo_disp,
                                 const char *model,
                                 const char *rom_fmt,
                                 const char *dvdver,
                                 const char *ps1ver,
                                 const char *config_source_name);

#endif
