#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "splash_render.h"
#include "splash_screen.h"

// Console info text anchor for LOGO_DISPLAY = 2-5, in center-relative pixels.
#define INFO_X_FROM_CENTER (-216)
#define INFO_Y_FROM_CENTER (205)
#define INFO_TEXT_COLOR 0xF0F0F0

// Hotkey text layout for LOGO_DISPLAY = 3-5.
// AUTO line anchors from the hotkeys image top-left.
#define HOTKEY_TEXT_X_FROM_HOTKEYS_LEFT 50
#define HOTKEY_TEXT_Y_FROM_HOTKEYS_TOP 2
#define HOTKEY_TEXT_LINE_SPACING 21
#define HK_MAX_CHARS 70

static void copy_clamped(char *dst, size_t dst_size, const char *src, int max_chars)
{
    size_t i = 0;

    if (dst == NULL || dst_size == 0)
        return;

    if (src == NULL)
        src = "";

    while (src[i] != '\0' && (int)i < max_chars && (i + 1) < dst_size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

void SplashRenderTextBody(int logo_disp,
                          int is_psx_desr)
{
    if (logo_disp < 2)
        return;

    if (!SplashRenderBegin(logo_disp, is_psx_desr))
        return;
}

void SplashRenderHotkeyLines(int logo_disp,
                             const char *const hotkey_lines[17])
{
    int i;
    int x;
    int y;
    int hotkeys_x;
    int hotkeys_y;

    if (logo_disp < 3 || hotkey_lines == NULL)
        return;
    if (!SplashRenderIsActive())
        return;

    hotkeys_x = SplashRenderGetHotkeysX();
    hotkeys_y = SplashRenderGetHotkeysY();
    if (hotkeys_x < 0 || hotkeys_y < 0)
        return;

    x = hotkeys_x + HOTKEY_TEXT_X_FROM_HOTKEYS_LEFT;
    y = hotkeys_y + HOTKEY_TEXT_Y_FROM_HOTKEYS_TOP;
    for (i = 0; i < 17; i++) {
        char clamped[HK_MAX_CHARS + 1];
        copy_clamped(clamped, sizeof(clamped), hotkey_lines[i], HK_MAX_CHARS);
        if (clamped[0] == '\0')
            continue;
        SplashRenderDrawTextPxScaled(x, y + (i * HOTKEY_TEXT_LINE_SPACING), 0xffffff, clamped, 1);
    }
}

void SplashRenderConsoleInfoLine(int logo_disp,
                                 const char *model,
                                 const char *rom_fmt,
                                 const char *dvdver,
                                 const char *ps1ver,
                                 const char *temp_celsius,
                                 const char *config_source_name)
{
    char info_line[320];

    if (logo_disp <= 0)
        return;

    if (temp_celsius != NULL && temp_celsius[0] != '\0') {
        snprintf(info_line,
                 sizeof(info_line),
                 "MODEL: %s  ROMVER: %s  DVD: %s  PS1DRV: %s  TEMP: %s  CFG SRC: %s",
                 model,
                 rom_fmt,
                 dvdver,
                 ps1ver,
                 temp_celsius,
                 config_source_name);
    } else {
        snprintf(info_line,
                 sizeof(info_line),
                 "MODEL: %s  ROMVER: %s  DVD: %s  PS1DRV: %s  CFG SRC: %s",
                 model,
                 rom_fmt,
                 dvdver,
                 ps1ver,
                 config_source_name);
    }

    if (logo_disp == 1) {
        scr_printf("\n%s", info_line);
        return;
    }

    if (SplashRenderIsActive()) {
        int x = SplashRenderGetScreenCenterX() + INFO_X_FROM_CENTER;
        int y = SplashRenderGetScreenCenterY() + INFO_Y_FROM_CENTER;
        SplashRenderDrawTextPxScaled(x, y, INFO_TEXT_COLOR, info_line, 1);
        SplashRenderEnd();
    }
}
