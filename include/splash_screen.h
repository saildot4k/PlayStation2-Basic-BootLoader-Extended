#ifndef SPLASH_SCREEN_H
#define SPLASH_SCREEN_H

#include <tamtypes.h>

void SplashRenderTextBody(int logo_disp,
                          int is_psx_desr);

void SplashRenderHotkeyLines(int logo_disp,
                             const char *const hotkey_lines[17]);
void SplashRenderHotkeyClockDate(int logo_disp, u64 tick_ms);

void SplashRenderConsoleInfoLine(int logo_disp,
                                 const char *model,
                                 const char *rom_fmt,
                                 const char *dvdver,
                                 const char *ps1ver,
                                 const char *temp_celsius,
                                 const char *autoboot_countdown,
                                 const char *config_source_name);

void SplashRenderConsoleInfoCountdownOnly(const char *autoboot_countdown);
void SplashRenderConsoleInfoTemperatureOnly(const char *temp_celsius);

#endif
