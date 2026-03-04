#include "main.h"
#include "splash_render.h"
#include "splash_screen.h"

#define HK_MAX_CHARS 70

typedef struct
{
    int key_index;
    const char *label;
    int offset_x;
    int offset_y;
} HOTKEY_LINE;

// Required display order for splash hotkey lines.
// offset_x/offset_y are pixel offsets from the splash image top-left (image is centered on screen).
static const HOTKEY_LINE g_hotkey_lines[] = {
    {AUTO, "AUTO", 104, 75},
    {TRIANGLE, "TRIANGLE", 104, 98},
    {CIRCLE, "CIRCLE", 104, 121},
    {CROSS, "CROSS", 104, 144},
    {SQUARE, "SQUARE", 104, 167},
    {UP, "UP", 104, 190},
    {DOWN, "DOWN", 104, 213},
    {LEFT, "LEFT", 104, 236},
    {RIGHT, "RIGHT", 104, 259},
    {L1, "L1", 104, 282},
    {L2, "L2", 104, 305},
    {L3, "L3", 104, 328},
    {R1, "R1", 104, 351},
    {R2, "R2", 104, 374},
    {R3, "R3", 104, 397},
    {SELECT, "SELECT", 104, 420},
    {START, "START", 104, 443},
};

#define INFO_LINE_X 1
#define INFO_LINE_Y 26
#define INFO_OFFSET_X 104
#define INFO_OFFSET_Y 466

static void trim_line_to_max_chars(const char *src, char *dst, int max_chars)
{
    size_t i = 0;

    if (dst == NULL || max_chars <= 0)
        return;

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    while (src[i] != '\0' && i < (size_t)max_chars)
        i++;

    if ((int)i == max_chars && src[i] != '\0' && max_chars >= 3) {
        size_t cut = (size_t)(max_chars - 3);
        memcpy(dst, src, cut);
        dst[cut] = '.';
        dst[cut + 1] = '.';
        dst[cut + 2] = '.';
        dst[cut + 3] = '\0';
        return;
    }

    memcpy(dst, src, i);
    dst[i] = '\0';
}

static void print_hotkey_lines(const char *const keynames[17], int use_gs)
{
    int i;
    int base_x = SplashRenderGetImageX();
    int base_y = SplashRenderGetImageY();

    if (keynames == NULL)
        return;

    for (i = 0; i < (int)(sizeof(g_hotkey_lines) / sizeof(g_hotkey_lines[0])); i++) {
        char limited_name[HK_MAX_CHARS + 1];
        char render_line[128];
        const HOTKEY_LINE *line = &g_hotkey_lines[i];
        const char *name = keynames[line->key_index];
        trim_line_to_max_chars(name, limited_name, HK_MAX_CHARS);
        snprintf(render_line, sizeof(render_line), "%-8s %s", line->label, limited_name);
        if (use_gs) {
            SplashRenderDrawTextPx(base_x + line->offset_x, base_y + line->offset_y, 0xffffff, render_line);
        } else {
            scr_printf("  %s\n", render_line);
        }
    }
}

void SplashRenderTextBody(int logo_disp,
                          int hotkey_display,
                          int config_source,
                          int is_psx_desr,
                          u32 banner_color,
                          const char *active_banner,
                          const char *active_hotkeys_banner,
                          const char *const keynames[17])
{
    int image_drawn = SplashRenderBegin(logo_disp, is_psx_desr);

    if (!image_drawn)
        scr_clear();

    if (logo_disp >= 3) {
        if (!image_drawn) {
            scr_setfontcolor(banner_color);
            scr_setXY(1, 1);
            scr_printf("\n%s", active_hotkeys_banner);
        }
        if (image_drawn) {
            print_hotkey_lines(keynames, 1);
        } else {
            scr_setfontcolor(0xffffff);
            if (hotkey_display == 3) {
                if (config_source == SOURCE_INVALID) {
                    scr_setfontcolor(0x00ffff);
                    scr_setXY(1, 4);
                    scr_printf("%s", BANNER_HOTKEYS_PATHS_HEADER_NOCONFIG);
                    scr_setfontcolor(0xffffff);
                } else {
                    scr_setXY(1, 4);
                    scr_printf("%s", BANNER_HOTKEYS_PATHS_HEADER);
                }
            }
            print_hotkey_lines(keynames, 0);
        }
    } else if (logo_disp > 1) {
        if (!image_drawn) {
            scr_setfontcolor(banner_color);
            scr_printf("\n\n\n\n%s", active_banner);
        }
    }

    scr_setfontcolor(0xffffff);
    if (logo_disp > 1 && logo_disp < 3)
        scr_printf(BANNER_FOOTER);
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

    if (SplashRenderIsActive()) {
        int base_x = SplashRenderGetImageX();
        int base_y = SplashRenderGetImageY();
        SplashRenderDrawTextPx(base_x + INFO_OFFSET_X, base_y + INFO_OFFSET_Y, 0xffffff, info_line);
        SplashRenderEnd();
    } else {
        // Shared console-info location for LOGO_DISPLAY=2..5 (text-mode fallback).
        scr_setXY(INFO_LINE_X, INFO_LINE_Y);
        scr_printf("%s", info_line);
    }
}
