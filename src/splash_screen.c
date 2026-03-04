#include <stdio.h>
#include <string.h>
#include "splash_render.h"
#include "splash_screen.h"

#define HK_MAX_CHARS 70

enum {
    KEY_AUTO = 0,
    KEY_SELECT = 1,
    KEY_L3 = 2,
    KEY_R3 = 3,
    KEY_START = 4,
    KEY_UP = 5,
    KEY_RIGHT = 6,
    KEY_DOWN = 7,
    KEY_LEFT = 8,
    KEY_L2 = 9,
    KEY_R2 = 10,
    KEY_L1 = 11,
    KEY_R1 = 12,
    KEY_TRIANGLE = 13,
    KEY_CIRCLE = 14,
    KEY_CROSS = 15,
    KEY_SQUARE = 16,
};

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
    {KEY_AUTO, "AUTO", 104, 75},
    {KEY_TRIANGLE, "TRIANGLE", 104, 98},
    {KEY_CIRCLE, "CIRCLE", 104, 121},
    {KEY_CROSS, "CROSS", 104, 144},
    {KEY_SQUARE, "SQUARE", 104, 167},
    {KEY_UP, "UP", 104, 190},
    {KEY_DOWN, "DOWN", 104, 213},
    {KEY_LEFT, "LEFT", 104, 236},
    {KEY_RIGHT, "RIGHT", 104, 259},
    {KEY_L1, "L1", 104, 282},
    {KEY_L2, "L2", 104, 305},
    {KEY_L3, "L3", 104, 328},
    {KEY_R1, "R1", 104, 351},
    {KEY_R2, "R2", 104, 374},
    {KEY_R3, "R3", 104, 397},
    {KEY_SELECT, "SELECT", 104, 420},
    {KEY_START, "START", 104, 443},
};

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

static void print_hotkey_lines(const char *const keynames[17])
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
        SplashRenderDrawTextPx(base_x + line->offset_x, base_y + line->offset_y, 0xffffff, render_line);
    }
}

void SplashRenderTextBody(int logo_disp,
                          int is_psx_desr,
                          const char *const keynames[17])
{
    int image_drawn;

    if (logo_disp < 2)
        return;

    image_drawn = SplashRenderBegin(logo_disp, is_psx_desr);

    if (!image_drawn) {
        scr_clear();
        return;
    }

    if (logo_disp >= 3)
        print_hotkey_lines(keynames);
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
