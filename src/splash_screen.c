#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "splash_render.h"
#include "splash_screen.h"

// Console info text anchor for LOGO_DISPLAY = 2-5, in center-relative pixels.
#define INFO_CENTER_ADJUST_X 0
#define INFO_Y_FROM_CENTER (205)
#define INFO_TEXT_COLOR 0x707070
#define INFO_AUTOBOOT_COLOR 0xffff00
#define GLYPH_ADVANCE_PX 6
#define GLYPH_HEIGHT_PX 7
#define AUTOBOOT_PREFIX "  AUTOBOOT in "
#define AUTOBOOT_VALUE_DEFAULT_WIDTH 6

static int g_countdown_x = 0;
static int g_countdown_y = 0;
static int g_countdown_visible = 0;
static int g_last_countdown_chars = 0;

// Hotkey text layout for LOGO_DISPLAY = 3-5.
// AUTO line anchors from the hotkeys image top-left.
#define HOTKEY_TEXT_X_FROM_HOTKEYS_LEFT 50
#define HOTKEY_TEXT_Y_FROM_HOTKEYS_TOP 5
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
                                 const char *autoboot_countdown,
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
        g_countdown_visible = 0;
        g_last_countdown_chars = 0;
        if (autoboot_countdown != NULL && autoboot_countdown[0] != '\0')
            scr_printf("\n%s%s", info_line, autoboot_countdown);
        else
            scr_printf("\n%s", info_line);
        return;
    }

    if (SplashRenderIsActive()) {
        int line_chars;
        int countdown_layout_chars;
        int line_width_px;
        int suffix_x;
        int prefix_width_px;
        int x;
        int y = SplashRenderGetScreenCenterY() + INFO_Y_FROM_CENTER;

        countdown_layout_chars = AUTOBOOT_VALUE_DEFAULT_WIDTH;
        if (autoboot_countdown != NULL && autoboot_countdown[0] != '\0') {
            int provided_chars = (int)strlen(autoboot_countdown);
            if (provided_chars > countdown_layout_chars)
                countdown_layout_chars = provided_chars;
        }

        line_chars = (int)strlen(info_line) + (int)strlen(AUTOBOOT_PREFIX) + countdown_layout_chars;
        line_width_px = line_chars * GLYPH_ADVANCE_PX;
        x = SplashRenderGetScreenCenterX() - (line_width_px / 2) + INFO_CENTER_ADJUST_X;

        SplashRenderDrawTextPxScaled(x, y, INFO_TEXT_COLOR, info_line, 1);

        suffix_x = x + ((int)strlen(info_line) * GLYPH_ADVANCE_PX);
        SplashRenderDrawTextPxScaled(suffix_x, y, INFO_AUTOBOOT_COLOR, AUTOBOOT_PREFIX, 1);
        prefix_width_px = (int)strlen(AUTOBOOT_PREFIX) * GLYPH_ADVANCE_PX;
        g_countdown_x = suffix_x + prefix_width_px;
        g_countdown_y = y;
        g_countdown_visible = 1;
        g_last_countdown_chars = 0;

        if (autoboot_countdown == NULL || autoboot_countdown[0] == '\0')
            return;

        SplashRenderDrawTextPxScaled(g_countdown_x, y, INFO_AUTOBOOT_COLOR, autoboot_countdown, 1);
        g_last_countdown_chars = (int)strlen(autoboot_countdown);
    }
}

void SplashRenderConsoleInfoCountdownOnly(const char *autoboot_countdown)
{
    int countdown_chars;
    int clear_chars;

    if (!g_countdown_visible || !SplashRenderIsActive())
        return;

    if (autoboot_countdown == NULL)
        autoboot_countdown = "";

    countdown_chars = (int)strlen(autoboot_countdown);
    clear_chars = (countdown_chars > g_last_countdown_chars) ? countdown_chars : g_last_countdown_chars;
    if (clear_chars > 0) {
        SplashRenderRestoreBackgroundRect(g_countdown_x,
                                          g_countdown_y,
                                          clear_chars * GLYPH_ADVANCE_PX,
                                          GLYPH_HEIGHT_PX);
    }

    if (countdown_chars > 0)
        SplashRenderDrawTextPxScaled(g_countdown_x, g_countdown_y, INFO_AUTOBOOT_COLOR, autoboot_countdown, 1);

    g_last_countdown_chars = countdown_chars;
}
