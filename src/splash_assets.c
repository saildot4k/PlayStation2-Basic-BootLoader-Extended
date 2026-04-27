// Embedded splash image/font asset metadata and accessors.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debugprintf.h"
#include "splash_assets.h"

#define CUSTOM_LOGO_WIDTH 256u
#define CUSTOM_LOGO_HEIGHT 64u
#define CUSTOM_LOGO_PIXELS_SIZE (CUSTOM_LOGO_WIDTH * CUSTOM_LOGO_HEIGHT * 4u)
#define CUSTOM_LOGO_INDEXED_MAGIC "LGB1"
#define CUSTOM_LOGO_INDEXED_HEADER_SIZE 12u
#define CUSTOM_LOGO_PATH_MAX 256u

enum {
    CUSTOM_LOGO_UNCHECKED = 0,
    CUSTOM_LOGO_READY,
    CUSTOM_LOGO_UNAVAILABLE
};

static int g_custom_logo_state = CUSTOM_LOGO_UNCHECKED;
static unsigned char *g_custom_logo_pixels = NULL;
static SPLASH_IMAGE g_custom_logo;

extern char *CheckPath(const char *path);
extern const char *LoaderGetBootCwdConfigPath(void);
extern const char *LoaderGetRequestedConfigPath(void);
extern const char *LoaderGetResolvedConfigPath(void);
extern int ci_eq(const char *a, const char *b);

extern const unsigned int splash_bg_ps2bble_width;
extern const unsigned int splash_bg_ps2bble_height;
extern const unsigned int splash_bg_ps2bble_lgb1_size;
extern const unsigned char splash_bg_ps2bble_lgb1[];

extern const unsigned int splash_bg_psxbble_width;
extern const unsigned int splash_bg_psxbble_height;
extern const unsigned int splash_bg_psxbble_lgb1_size;
extern const unsigned char splash_bg_psxbble_lgb1[];

extern const unsigned int splash_logo_ps2bble_width;
extern const unsigned int splash_logo_ps2bble_height;
extern const unsigned int splash_logo_ps2bble_lgb1_size;
extern const unsigned char splash_logo_ps2bble_lgb1[];

extern const unsigned int splash_logo_psxbble_width;
extern const unsigned int splash_logo_psxbble_height;
extern const unsigned int splash_logo_psxbble_lgb1_size;
extern const unsigned char splash_logo_psxbble_lgb1[];

extern const unsigned int splash_hotkeys_width;
extern const unsigned int splash_hotkeys_height;
extern const unsigned int splash_hotkeys_lgb1_size;
extern const unsigned char splash_hotkeys_lgb1[];

static unsigned char *g_bg_ps2_pixels = NULL;
static unsigned char *g_bg_psx_pixels = NULL;
static unsigned char *g_logo_ps2_pixels = NULL;
static unsigned char *g_logo_psx_pixels = NULL;
static unsigned char *g_hotkeys_pixels = NULL;

static unsigned int read_u16_le(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static int decode_lgb1_to_rbga(const unsigned char *blob,
                               unsigned int blob_size,
                               unsigned int expected_width,
                               unsigned int expected_height,
                               unsigned char **pixels_out)
{
    unsigned int width;
    unsigned int height;
    unsigned int palette_count;
    unsigned int pixel_count;
    unsigned int expected_size;
    const unsigned char *palette;
    const unsigned char *indices;
    unsigned char *out;
    unsigned int i;

    if (pixels_out == NULL)
        return 0;
    if (blob == NULL || blob_size < CUSTOM_LOGO_INDEXED_HEADER_SIZE)
        return 0;
    if (memcmp(blob, CUSTOM_LOGO_INDEXED_MAGIC, 4) != 0)
        return 0;

    width = read_u16_le(&blob[4]);
    height = read_u16_le(&blob[6]);
    if (width != expected_width || height != expected_height)
        return 0;

    palette_count = (unsigned int)blob[8];
    if (palette_count == 0)
        palette_count = 256u;
    if (palette_count < 1u || palette_count > 256u)
        return 0;

    pixel_count = width * height;
    expected_size = CUSTOM_LOGO_INDEXED_HEADER_SIZE + (palette_count * 4u) + pixel_count;
    if (blob_size != expected_size)
        return 0;

    palette = blob + CUSTOM_LOGO_INDEXED_HEADER_SIZE;
    indices = palette + (palette_count * 4u);
    out = (unsigned char *)malloc(pixel_count * 4u);
    if (out == NULL)
        return 0;

    for (i = 0; i < pixel_count; i++) {
        unsigned int idx = (unsigned int)indices[i];
        unsigned int dst = i * 4u;

        if (idx >= palette_count)
            idx = 0;

        out[dst + 0] = palette[(idx * 4u) + 0u];
        out[dst + 1] = palette[(idx * 4u) + 1u];
        out[dst + 2] = palette[(idx * 4u) + 2u];
        out[dst + 3] = palette[(idx * 4u) + 3u];
    }

    *pixels_out = out;
    return 1;
}

static void init_embedded_image(SPLASH_IMAGE *img,
                                unsigned char **cache,
                                unsigned int width,
                                unsigned int height,
                                const unsigned char *lgb1,
                                unsigned int lgb1_size,
                                const char *label)
{
    if (img == NULL || cache == NULL)
        return;

    if (*cache == NULL) {
        if (!decode_lgb1_to_rbga(lgb1, lgb1_size, width, height, cache)) {
            DPRINTF("Failed to decode embedded splash asset: %s\n", label);
            img->pixels_rbga = NULL;
            img->width = 0;
            img->height = 0;
            return;
        }
    }

    img->pixels_rbga = *cache;
    img->width = width;
    img->height = height;
}

static int load_custom_logo_raw_rbga(FILE *fp, const char *path)
{
    size_t bytes_read;

    g_custom_logo_pixels = (unsigned char *)malloc(CUSTOM_LOGO_PIXELS_SIZE);
    if (g_custom_logo_pixels == NULL) {
        DPRINTF("Ignoring custom splash logo '%s': out of memory\n", path);
        return 0;
    }

    bytes_read = fread(g_custom_logo_pixels, 1, CUSTOM_LOGO_PIXELS_SIZE, fp);
    if (bytes_read != CUSTOM_LOGO_PIXELS_SIZE) {
        DPRINTF("Ignoring custom splash logo '%s': read %u/%u bytes\n",
                path,
                (unsigned int)bytes_read,
                (unsigned int)CUSTOM_LOGO_PIXELS_SIZE);
        free(g_custom_logo_pixels);
        g_custom_logo_pixels = NULL;
        return 0;
    }

    return 1;
}

static int load_custom_logo_indexed(FILE *fp, long file_size, const char *path)
{
    unsigned char header[CUSTOM_LOGO_INDEXED_HEADER_SIZE];
    unsigned int width;
    unsigned int height;
    unsigned int palette_count;
    unsigned int pixel_count = CUSTOM_LOGO_WIDTH * CUSTOM_LOGO_HEIGHT;
    unsigned int expected_size;
    unsigned char *palette = NULL;
    unsigned char *indices = NULL;
    unsigned int i;

    if (fread(header, 1, sizeof(header), fp) != sizeof(header)) {
        DPRINTF("Ignoring custom splash logo '%s': could not read indexed header\n", path);
        return 0;
    }

    if (memcmp(header, CUSTOM_LOGO_INDEXED_MAGIC, 4) != 0)
        return 0;

    width = read_u16_le(&header[4]);
    height = read_u16_le(&header[6]);
    palette_count = (unsigned int)header[8];
    if (palette_count == 0)
        palette_count = 256u;

    if (width != CUSTOM_LOGO_WIDTH || height != CUSTOM_LOGO_HEIGHT) {
        DPRINTF("Ignoring custom splash logo '%s': indexed header size mismatch (%ux%u)\n",
                path,
                width,
                height);
        return 0;
    }

    if (palette_count < 1u || palette_count > 256u) {
        DPRINTF("Ignoring custom splash logo '%s': invalid palette count %u\n", path, palette_count);
        return 0;
    }

    expected_size = CUSTOM_LOGO_INDEXED_HEADER_SIZE + (palette_count * 4u) + pixel_count;
    if (file_size < 0 || (unsigned long)file_size != (unsigned long)expected_size) {
        DPRINTF("Ignoring custom splash logo '%s': indexed size mismatch (%ld, expected %u)\n",
                path,
                file_size,
                expected_size);
        return 0;
    }

    palette = (unsigned char *)malloc(palette_count * 4u);
    indices = (unsigned char *)malloc(pixel_count);
    g_custom_logo_pixels = (unsigned char *)malloc(CUSTOM_LOGO_PIXELS_SIZE);
    if (palette == NULL || indices == NULL || g_custom_logo_pixels == NULL) {
        DPRINTF("Ignoring custom splash logo '%s': out of memory\n", path);
        free(palette);
        free(indices);
        free(g_custom_logo_pixels);
        g_custom_logo_pixels = NULL;
        return 0;
    }

    if (fread(palette, 1, palette_count * 4u, fp) != palette_count * 4u ||
        fread(indices, 1, pixel_count, fp) != pixel_count) {
        DPRINTF("Ignoring custom splash logo '%s': could not read indexed payload\n", path);
        free(palette);
        free(indices);
        free(g_custom_logo_pixels);
        g_custom_logo_pixels = NULL;
        return 0;
    }

    for (i = 0; i < pixel_count; i++) {
        unsigned int idx = (unsigned int)indices[i];
        unsigned int dst = i * 4u;

        if (idx >= palette_count)
            idx = 0;

        g_custom_logo_pixels[dst + 0] = palette[(idx * 4u) + 0];
        g_custom_logo_pixels[dst + 1] = palette[(idx * 4u) + 1];
        g_custom_logo_pixels[dst + 2] = palette[(idx * 4u) + 2];
        g_custom_logo_pixels[dst + 3] = palette[(idx * 4u) + 3];
    }

    free(palette);
    free(indices);
    return 1;
}

static int load_custom_logo_from_path(const char *requested_path)
{
    FILE *fp;
    long file_size;
    int loaded = 0;
    char *resolved_path;
    const char *path;

    if (requested_path == NULL || *requested_path == '\0')
        return 0;

    resolved_path = CheckPath(requested_path);
    if (resolved_path != NULL && *resolved_path != '\0')
        path = resolved_path;
    else
        path = requested_path;

    fp = fopen(path, "rb");
    if (fp == NULL)
        return 0;

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return 0;
    }

    file_size = ftell(fp);
    if (file_size < 0) {
        fclose(fp);
        return 0;
    }

    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return 0;
    }

    if ((unsigned long)file_size == CUSTOM_LOGO_PIXELS_SIZE)
        loaded = load_custom_logo_raw_rbga(fp, path);
    else
        loaded = load_custom_logo_indexed(fp, file_size, path);

    fclose(fp);
    if (!loaded) {
        if ((unsigned long)file_size != CUSTOM_LOGO_PIXELS_SIZE) {
            DPRINTF("Ignoring custom splash logo '%s': unsupported file format or size (%ld)\n",
                    path,
                    file_size);
        }
        return 0;
    }

    g_custom_logo.pixels_rbga = g_custom_logo_pixels;
    g_custom_logo.width = CUSTOM_LOGO_WIDTH;
    g_custom_logo.height = CUSTOM_LOGO_HEIGHT;
    DPRINTF("Loaded custom splash logo from CWD: requested='%s' resolved='%s' (%s)\n",
            requested_path,
            path,
            ((unsigned long)file_size == CUSTOM_LOGO_PIXELS_SIZE) ? "raw RBGA" : "indexed");
    return 1;
}

static int build_boot_cwd_logo_path(char *out, size_t out_size)
{
    const char *boot_cwd_config;
    const char *slash;
    size_t prefix_len;

    if (out == NULL || out_size == 0)
        return 0;
    out[0] = '\0';

    boot_cwd_config = LoaderGetBootCwdConfigPath();
    if (boot_cwd_config == NULL || *boot_cwd_config == '\0')
        return 0;

    // Keep CWD logo discovery strict: always derive LOGO.BIN from the boot CWD
    // path itself (not from resolved fallback config locations).
    slash = strrchr(boot_cwd_config, '/');
    if (slash == NULL)
        return 0;

    prefix_len = (size_t)(slash - boot_cwd_config + 1);
    if (prefix_len + strlen("LOGO.BIN") + 1 > out_size)
        return 0;

    memcpy(out, boot_cwd_config, prefix_len);
    memcpy(out + prefix_len, "LOGO.BIN", sizeof("LOGO.BIN"));
    return 1;
}

static void probe_custom_logo_once(void)
{
    const char *candidates[2];
    char boot_logo_path[CUSTOM_LOGO_PATH_MAX];
    unsigned int i;
    unsigned int candidate_count = 0;

    if (g_custom_logo_state != CUSTOM_LOGO_UNCHECKED)
        return;

    // Keep logo discovery fail-fast: CWD only, no retries/timeouts.
    candidates[candidate_count++] = "LOGO.BIN";
    if (build_boot_cwd_logo_path(boot_logo_path, sizeof(boot_logo_path)))
        candidates[candidate_count++] = boot_logo_path;

    for (i = 0; i < candidate_count; i++) {
        if (load_custom_logo_from_path(candidates[i])) {
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

    init_embedded_image(&bg_ps2,
                        &g_bg_ps2_pixels,
                        splash_bg_ps2bble_width,
                        splash_bg_ps2bble_height,
                        splash_bg_ps2bble_lgb1,
                        splash_bg_ps2bble_lgb1_size,
                        "bg_ps2bble");

    init_embedded_image(&bg_psx,
                        &g_bg_psx_pixels,
                        splash_bg_psxbble_width,
                        splash_bg_psxbble_height,
                        splash_bg_psxbble_lgb1,
                        splash_bg_psxbble_lgb1_size,
                        "bg_psxbble");

    return is_psx_desr ? &bg_psx : &bg_ps2;
}

const SPLASH_IMAGE *SplashGetLogoImage(int is_psx_desr)
{
    static SPLASH_IMAGE logo_ps2;
    static SPLASH_IMAGE logo_psx;

    probe_custom_logo_once();
    if (g_custom_logo_state == CUSTOM_LOGO_READY)
        return &g_custom_logo;

    init_embedded_image(&logo_ps2,
                        &g_logo_ps2_pixels,
                        splash_logo_ps2bble_width,
                        splash_logo_ps2bble_height,
                        splash_logo_ps2bble_lgb1,
                        splash_logo_ps2bble_lgb1_size,
                        "logo_ps2bble");

    init_embedded_image(&logo_psx,
                        &g_logo_psx_pixels,
                        splash_logo_psxbble_width,
                        splash_logo_psxbble_height,
                        splash_logo_psxbble_lgb1,
                        splash_logo_psxbble_lgb1_size,
                        "logo_psxbble");

    return is_psx_desr ? &logo_psx : &logo_ps2;
}

const SPLASH_IMAGE *SplashGetHotkeysImage(void)
{
    static SPLASH_IMAGE hotkeys;

    init_embedded_image(&hotkeys,
                        &g_hotkeys_pixels,
                        splash_hotkeys_width,
                        splash_hotkeys_height,
                        splash_hotkeys_lgb1,
                        splash_hotkeys_lgb1_size,
                        "hotkeys");

    return &hotkeys;
}
