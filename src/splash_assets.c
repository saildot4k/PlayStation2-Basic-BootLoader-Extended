// Embedded splash image/font asset metadata and accessors.
#include <png.h>
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
    "LOGO.PNG",
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

static void clear_custom_logo_pixels(void)
{
    if (g_custom_logo_pixels != NULL) {
        free(g_custom_logo_pixels);
        g_custom_logo_pixels = NULL;
    }
}

static int load_custom_logo_from_path(const char *path)
{
    FILE *fp;
    png_structp png_ptr = NULL;
    png_infop info_ptr = NULL;
    png_bytep *row_ptrs = NULL;
    unsigned char sig[8];
    png_uint_32 width;
    png_uint_32 height;
    int bit_depth;
    int color_type;
    png_size_t rowbytes;
    unsigned char *rgba_pixels = NULL;
    unsigned int y;
    unsigned int x;
    int loaded = 0;

    if (path == NULL || *path == '\0')
        return 0;

    fp = fopen(path, "rb");
    if (fp == NULL)
        return 0;

    if (fread(sig, 1, sizeof(sig), fp) != sizeof(sig) || png_sig_cmp(sig, 0, sizeof(sig)) != 0) {
        DPRINTF("Ignoring custom splash logo '%s': invalid PNG signature\n", path);
        fclose(fp);
        return 0;
    }

    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (png_ptr == NULL) {
        fclose(fp);
        return 0;
    }

    info_ptr = png_create_info_struct(png_ptr);
    if (info_ptr == NULL) {
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        fclose(fp);
        return 0;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        DPRINTF("Ignoring custom splash logo '%s': failed while decoding PNG\n", path);
        goto cleanup;
    }

    png_init_io(png_ptr, fp);
    png_set_sig_bytes(png_ptr, sizeof(sig));
    png_read_info(png_ptr, info_ptr);

    width = png_get_image_width(png_ptr, info_ptr);
    height = png_get_image_height(png_ptr, info_ptr);
    if (width != CUSTOM_LOGO_WIDTH || height != CUSTOM_LOGO_HEIGHT) {
        DPRINTF("Ignoring custom splash logo '%s': expected %ux%u but got %ux%u\n",
                path,
                (unsigned int)CUSTOM_LOGO_WIDTH,
                (unsigned int)CUSTOM_LOGO_HEIGHT,
                (unsigned int)width,
                (unsigned int)height);
        goto cleanup;
    }

    bit_depth = png_get_bit_depth(png_ptr, info_ptr);
    color_type = png_get_color_type(png_ptr, info_ptr);

    if (bit_depth == 16)
        png_set_strip_16(png_ptr);
    if (color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png_ptr);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
        png_set_expand_gray_1_2_4_to_8(png_ptr);
    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
        png_set_tRNS_to_alpha(png_ptr);
    if ((color_type & PNG_COLOR_MASK_ALPHA) == 0)
        png_set_add_alpha(png_ptr, 0xFF, PNG_FILLER_AFTER);
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png_ptr);

    png_read_update_info(png_ptr, info_ptr);

    rowbytes = png_get_rowbytes(png_ptr, info_ptr);
    if (rowbytes != (png_size_t)(CUSTOM_LOGO_WIDTH * 4u)) {
        DPRINTF("Ignoring custom splash logo '%s': unexpected row stride %u\n",
                path,
                (unsigned int)rowbytes);
        goto cleanup;
    }

    rgba_pixels = (unsigned char *)malloc((size_t)rowbytes * (size_t)CUSTOM_LOGO_HEIGHT);
    row_ptrs = (png_bytep *)malloc(sizeof(png_bytep) * CUSTOM_LOGO_HEIGHT);
    clear_custom_logo_pixels();
    g_custom_logo_pixels = (unsigned char *)malloc(CUSTOM_LOGO_PIXELS_SIZE);
    if (rgba_pixels == NULL || row_ptrs == NULL || g_custom_logo_pixels == NULL) {
        DPRINTF("Ignoring custom splash logo '%s': out of memory\n", path);
        goto cleanup;
    }

    for (y = 0; y < CUSTOM_LOGO_HEIGHT; y++)
        row_ptrs[y] = rgba_pixels + ((size_t)y * (size_t)rowbytes);

    png_read_image(png_ptr, row_ptrs);
    png_read_end(png_ptr, NULL);

    for (y = 0; y < CUSTOM_LOGO_HEIGHT; y++) {
        const unsigned char *src_row = rgba_pixels + ((size_t)y * (size_t)rowbytes);
        unsigned char *dst_row = g_custom_logo_pixels + ((size_t)y * (size_t)CUSTOM_LOGO_WIDTH * 4u);

        for (x = 0; x < CUSTOM_LOGO_WIDTH; x++) {
            size_t src = (size_t)x * 4u;
            size_t dst = (size_t)x * 4u;
            unsigned char r = src_row[src + 0u];
            unsigned char g = src_row[src + 1u];
            unsigned char b = src_row[src + 2u];
            unsigned char a = src_row[src + 3u];

            dst_row[dst + 0u] = r;
            dst_row[dst + 1u] = b;
            dst_row[dst + 2u] = g;
            dst_row[dst + 3u] = a;
        }
    }

    g_custom_logo.pixels_rbga = g_custom_logo_pixels;
    g_custom_logo.width = CUSTOM_LOGO_WIDTH;
    g_custom_logo.height = CUSTOM_LOGO_HEIGHT;
    DPRINTF("Loaded custom splash logo from CWD: %s\n", path);
    loaded = 1;

cleanup:
    if (row_ptrs != NULL)
        free(row_ptrs);
    if (rgba_pixels != NULL)
        free(rgba_pixels);
    if (png_ptr != NULL)
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    fclose(fp);

    if (!loaded)
        clear_custom_logo_pixels();

    return loaded;
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
