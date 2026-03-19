// Embedded splash image/font asset metadata and accessors.
#include <stdio.h>
#include <stdlib.h>

#include "debugprintf.h"
#include "splash_assets.h"

#define CUSTOM_LOGO_WIDTH 256u
#define CUSTOM_LOGO_HEIGHT 64u
#define CUSTOM_LOGO_PIXELS_SIZE (CUSTOM_LOGO_WIDTH * CUSTOM_LOGO_HEIGHT * 4u)

enum {
    CUSTOM_LOGO_UNCHECKED = 0,
    CUSTOM_LOGO_READY,
    CUSTOM_LOGO_UNAVAILABLE
};

static int g_custom_logo_state = CUSTOM_LOGO_UNCHECKED;
static unsigned char *g_custom_logo_pixels = NULL;
static SPLASH_IMAGE g_custom_logo;

static const char *const g_custom_logo_candidates[] = {
    "LOGO.BIN",
};

extern const unsigned int splash_bg_ps2bble_width;
extern const unsigned int splash_bg_ps2bble_height;
extern const unsigned char splash_bg_ps2bble_rbga[];

extern const unsigned int splash_bg_psxbble_width;
extern const unsigned int splash_bg_psxbble_height;
extern const unsigned char splash_bg_psxbble_rbga[];

extern const unsigned int splash_logo_ps2bble_width;
extern const unsigned int splash_logo_ps2bble_height;
extern const unsigned char splash_logo_ps2bble_rbga[];

extern const unsigned int splash_logo_psxbble_width;
extern const unsigned int splash_logo_psxbble_height;
extern const unsigned char splash_logo_psxbble_rbga[];

extern const unsigned int splash_hotkeys_width;
extern const unsigned int splash_hotkeys_height;
extern const unsigned char splash_hotkeys_rbga[];

static int load_custom_logo_from_path(const char *path)
{
    FILE *fp;
    long file_size;
    size_t bytes_read;

    if (path == NULL || *path == '\0')
        return 0;

    fp = fopen(path, "rb");
    if (fp == NULL)
        return 0;

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return 0;
    }

    file_size = ftell(fp);
    if (file_size < 0 || (unsigned long)file_size != CUSTOM_LOGO_PIXELS_SIZE) {
        fclose(fp);
        DPRINTF("Ignoring custom splash logo '%s': expected %u bytes (256x64 RBGA) but got %ld\n",
                path,
                (unsigned int)CUSTOM_LOGO_PIXELS_SIZE,
                file_size);
        return 0;
    }

    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return 0;
    }

    g_custom_logo_pixels = (unsigned char *)malloc(CUSTOM_LOGO_PIXELS_SIZE);
    if (g_custom_logo_pixels == NULL) {
        fclose(fp);
        DPRINTF("Ignoring custom splash logo '%s': out of memory\n", path);
        return 0;
    }

    bytes_read = fread(g_custom_logo_pixels, 1, CUSTOM_LOGO_PIXELS_SIZE, fp);
    fclose(fp);
    if (bytes_read != CUSTOM_LOGO_PIXELS_SIZE) {
        DPRINTF("Ignoring custom splash logo '%s': read %u/%u bytes\n",
                path,
                (unsigned int)bytes_read,
                (unsigned int)CUSTOM_LOGO_PIXELS_SIZE);
        free(g_custom_logo_pixels);
        g_custom_logo_pixels = NULL;
        return 0;
    }

    g_custom_logo.pixels_rbga = g_custom_logo_pixels;
    g_custom_logo.width = CUSTOM_LOGO_WIDTH;
    g_custom_logo.height = CUSTOM_LOGO_HEIGHT;
    DPRINTF("Loaded custom splash logo from CWD: %s\n", path);
    return 1;
}

static void probe_custom_logo_once(void)
{
    unsigned int i;

    if (g_custom_logo_state != CUSTOM_LOGO_UNCHECKED)
        return;

    for (i = 0; i < (unsigned int)(sizeof(g_custom_logo_candidates) / sizeof(g_custom_logo_candidates[0])); i++) {
        if (load_custom_logo_from_path(g_custom_logo_candidates[i])) {
            g_custom_logo_state = CUSTOM_LOGO_READY;
            return;
        }
    }

    g_custom_logo_state = CUSTOM_LOGO_UNAVAILABLE;
}

const SPLASH_IMAGE *SplashGetBackgroundImage(int is_psx_desr)
{
    static SPLASH_IMAGE bg_ps2;
    static SPLASH_IMAGE bg_psx;

    bg_ps2.pixels_rbga = splash_bg_ps2bble_rbga;
    bg_ps2.width = splash_bg_ps2bble_width;
    bg_ps2.height = splash_bg_ps2bble_height;

    bg_psx.pixels_rbga = splash_bg_psxbble_rbga;
    bg_psx.width = splash_bg_psxbble_width;
    bg_psx.height = splash_bg_psxbble_height;

    return is_psx_desr ? &bg_psx : &bg_ps2;
}

const SPLASH_IMAGE *SplashGetLogoImage(int is_psx_desr)
{
    static SPLASH_IMAGE logo_ps2;
    static SPLASH_IMAGE logo_psx;

    probe_custom_logo_once();
    if (g_custom_logo_state == CUSTOM_LOGO_READY)
        return &g_custom_logo;

    logo_ps2.pixels_rbga = splash_logo_ps2bble_rbga;
    logo_ps2.width = splash_logo_ps2bble_width;
    logo_ps2.height = splash_logo_ps2bble_height;

    logo_psx.pixels_rbga = splash_logo_psxbble_rbga;
    logo_psx.width = splash_logo_psxbble_width;
    logo_psx.height = splash_logo_psxbble_height;

    return is_psx_desr ? &logo_psx : &logo_ps2;
}

const SPLASH_IMAGE *SplashGetHotkeysImage(void)
{
    static SPLASH_IMAGE hotkeys;

    hotkeys.pixels_rbga = splash_hotkeys_rbga;
    hotkeys.width = splash_hotkeys_width;
    hotkeys.height = splash_hotkeys_height;

    return &hotkeys;
}
