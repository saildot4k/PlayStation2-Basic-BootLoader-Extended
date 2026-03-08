#include <malloc.h>
#include <stdlib.h>
#include <string.h>

#include <dmaKit.h>
#include <gsKit.h>

#include "splash_assets.h"
#include "splash_render.h"

#define FONT_W 5
#define FONT_H 7
#define FONT_SCALE 2
#define FONT_ADVANCE ((FONT_W + 1) * FONT_SCALE)
#define TEXT_Z 10
#define TEXT_SHADOW_Z (TEXT_Z - 1)
#define BG_Z 1
#define FG_Z 2
#define GS_ALPHA_OPAQUE 0x80
#define LOGO_TRANSPARENCY_PERCENT_DEFAULT 0
#define LOGO_TRANSPARENCY_PERCENT_WITH_HOTKEYS 80
#define LOGO_SHIMMER_ENABLED 1
#define LOGO_SHIMMER_WIDTH_PERCENT 16
#define LOGO_SHIMMER_MIN_WIDTH_PX 8
#define LOGO_SHIMMER_SPEED_PX_PER_FRAME 1
#define LOGO_SHIMMER_SLICE_COUNT 18
#define LOGO_SHIMMER_HIGHLIGHT_OPACITY_PERCENT 5
#define LOGO_SHIMMER_HALO_OPACITY_PERCENT 3
#define LOGO_SHIMMER_DISTORT_OPACITY_PERCENT 16
#define LOGO_SHIMMER_DISTORT_MAX_SHIFT_PX 10
#define GLYPH_SHADOW_TRANSPARENCY_PERCENT 40
#define GLYPH_SHADOW_OFFSET_X 1
#define GLYPH_SHADOW_OFFSET_Y 1

// LOGO_DISPLAY = 2 centered logo fine tuning.
#define MODE2_LOGO_X_FROM_CENTER 0
#define MODE2_LOGO_Y_FROM_CENTER 0

// LOGO_DISPLAY = 3-5 layout:
// - Logo centered using the logo visual-center ratio.
// - Hotkeys image left aligned and vertically centered on screen.
// Logo Y and hotkeys Y values are center-relative tuning offsets in pixels.
#define MODE35_HOTKEYS_LEFT_PERCENT 10
#define MODE35_LOGO_X_FROM_CENTER 0
#define MODE35_LOGO_Y_FROM_CENTER 0
#define MODE35_HOTKEYS_X_ADJUST 0
#define MODE35_HOTKEYS_Y_FROM_CENTER 0

// Visual center of the logo graphic from the top, expressed as a ratio of logo height.
// 21/64 keeps the visual center consistent if the logo is scaled later.
#define LOGO_VISUAL_CENTER_Y_NUMERATOR 21
#define LOGO_VISUAL_CENTER_Y_DENOMINATOR 64

typedef struct
{
    char ch;
    unsigned char rows[FONT_H];
} GLYPH;

typedef struct
{
    GSTEXTURE tex;
    int ready;
} SPLASH_LAYER;

enum
{
    LAYER_BG = 0,
    LAYER_LOGO,
    LAYER_HOTKEYS,
    LAYER_COUNT
};

static GSGLOBAL *g_gs = NULL;
static SPLASH_LAYER g_layers[LAYER_COUNT];
static int g_screen_w = 0;
static int g_screen_h = 0;
static int g_logo_x = -1;
static int g_logo_y = -1;
static int g_logo_visible = 0;
static int g_hotkeys_x = -1;
static int g_hotkeys_y = -1;
static int g_hotkeys_visible = 0;
static int g_logo_shimmer_left = 0;
static int g_logo_shimmer_band_width = 0;

static int logo_visual_center_y(unsigned int logo_height)
{
    return (int)(((logo_height * LOGO_VISUAL_CENTER_Y_NUMERATOR) +
                  (LOGO_VISUAL_CENTER_Y_DENOMINATOR / 2)) /
                 LOGO_VISUAL_CENTER_Y_DENOMINATOR);
}

static unsigned char transparency_percent_to_gs_alpha(unsigned int transparency_percent)
{
    unsigned int opacity_percent;

    if (transparency_percent > 100u)
        transparency_percent = 100u;

    opacity_percent = 100u - transparency_percent;
    return (unsigned char)((opacity_percent * GS_ALPHA_OPAQUE + 50u) / 100u);
}

static unsigned char opacity_percent_to_gs_alpha(unsigned int opacity_percent)
{
    if (opacity_percent > 100u)
        opacity_percent = 100u;

    return (unsigned char)((opacity_percent * GS_ALPHA_OPAQUE + 50u) / 100u);
}

static unsigned char get_logo_base_alpha(void)
{
    const unsigned int logo_transparency_percent = g_hotkeys_visible
                                                       ? LOGO_TRANSPARENCY_PERCENT_WITH_HOTKEYS
                                                       : LOGO_TRANSPARENCY_PERCENT_DEFAULT;

    return transparency_percent_to_gs_alpha(logo_transparency_percent);
}

static int compute_logo_shimmer_band_width(int logo_width)
{
    int band_width;

    if (logo_width <= 0)
        return 0;

    band_width = ((logo_width * LOGO_SHIMMER_WIDTH_PERCENT) + 50) / 100;
    if (band_width < LOGO_SHIMMER_MIN_WIDTH_PX)
        band_width = LOGO_SHIMMER_MIN_WIDTH_PX;
    if (band_width > logo_width)
        band_width = logo_width;

    return band_width;
}

static void reset_logo_shimmer_state(void)
{
    g_logo_shimmer_band_width = 0;
    g_logo_shimmer_left = 0;
}

static void init_logo_shimmer_state(void)
{
    const SPLASH_LAYER *logo = &g_layers[LAYER_LOGO];

    if (!logo->ready || logo->tex.Width <= 0) {
        reset_logo_shimmer_state();
        return;
    }

    g_logo_shimmer_band_width = compute_logo_shimmer_band_width((int)logo->tex.Width);
    g_logo_shimmer_left = -g_logo_shimmer_band_width;
}

// 5x7 glyphs for splash labels.
static const GLYPH g_font[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {'!', {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}},
    {'\"', {0x0A, 0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00}},
    {'#', {0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A}},
    {'$', {0x04, 0x0F, 0x14, 0x0E, 0x05, 0x1E, 0x04}},
    {'%', {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03}},
    {'&', {0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D}},
    {'\'', {0x06, 0x06, 0x04, 0x00, 0x00, 0x00, 0x00}},
    {'(', {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}},
    {')', {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}},
    {'*', {0x00, 0x04, 0x15, 0x0E, 0x15, 0x04, 0x00}},
    {'+', {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}},
    {',', {0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x04}},
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06}},
    {':', {0x00, 0x06, 0x06, 0x00, 0x06, 0x06, 0x00}},
    {';', {0x00, 0x06, 0x06, 0x00, 0x06, 0x04, 0x08}},
    {'<', {0x01, 0x02, 0x04, 0x08, 0x04, 0x02, 0x01}},
    {'=', {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00}},
    {'>', {0x10, 0x08, 0x04, 0x02, 0x04, 0x08, 0x10}},
    {'?', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}},
    {'@', {0x0E, 0x11, 0x15, 0x1D, 0x1D, 0x10, 0x0E}},
    {'/', {0x01, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00}},
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'5', {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E}},
    {'6', {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E}},
    {'A', {0x04, 0x0A, 0x11, 0x11, 0x1F, 0x11, 0x11}},
    {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'D', {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E}},
    {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'J', {0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'N', {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}},
    {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
    {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
    {'[', {0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E}},
    {'\\', {0x10, 0x08, 0x04, 0x02, 0x01, 0x00, 0x00}},
    {']', {0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E}},
    {'^', {0x04, 0x0A, 0x11, 0x00, 0x00, 0x00, 0x00}},
    {'_', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}},
    {'`', {0x04, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {'{', {0x03, 0x04, 0x04, 0x18, 0x04, 0x04, 0x03}},
    {'|', {0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'}', {0x18, 0x04, 0x04, 0x03, 0x04, 0x04, 0x18}},
    {'~', {0x00, 0x00, 0x09, 0x16, 0x00, 0x00, 0x00}},
};

static const GLYPH *find_glyph(char ch)
{
    int i;
    char uc = ch;

    if (uc >= 'a' && uc <= 'z')
        uc = (char)(uc - ('a' - 'A'));

    for (i = 0; i < (int)(sizeof(g_font) / sizeof(g_font[0])); i++) {
        if (g_font[i].ch == uc)
            return &g_font[i];
    }

    return &g_font[0];
}

static u64 color_to_gs(u32 color)
{
    int r = (color >> 16) & 0xff;
    int g = (color >> 8) & 0xff;
    int b = color & 0xff;
    return GS_SETREG_RGBAQ(r, g, b, GS_ALPHA_OPAQUE, 0x00);
}

static unsigned char png_alpha_to_gs_alpha(unsigned char alpha)
{
    return (unsigned char)(((unsigned int)alpha * 0x80u + 127u) / 255u);
}

static int rbga_to_rgba(const SPLASH_IMAGE *img, unsigned char *dst_rgba, size_t dst_size)
{
    size_t pixels;
    size_t i;

    if (img == NULL || dst_rgba == NULL)
        return -1;

    pixels = (size_t)img->width * (size_t)img->height;
    if (dst_size < (pixels * 4))
        return -1;

    for (i = 0; i < pixels; i++) {
        size_t src = i * 4;
        size_t dst = i * 4;
        unsigned char r = img->pixels_rbga[src + 0];
        unsigned char b = img->pixels_rbga[src + 1];
        unsigned char g = img->pixels_rbga[src + 2];
        unsigned char a = img->pixels_rbga[src + 3];
        dst_rgba[dst + 0] = r;
        dst_rgba[dst + 1] = g;
        dst_rgba[dst + 2] = b;
        dst_rgba[dst + 3] = png_alpha_to_gs_alpha(a);
    }

    return 0;
}

static void destroy_layer(SPLASH_LAYER *layer)
{
    if (layer == NULL)
        return;

    if (layer->tex.Mem != NULL)
        free(layer->tex.Mem);
    if (layer->tex.Clut != NULL)
        free(layer->tex.Clut);

    memset(&layer->tex, 0, sizeof(layer->tex));
    layer->ready = 0;
}

static void destroy_frame_state(void)
{
    int i;

    for (i = 0; i < (int)(sizeof(g_layers) / sizeof(g_layers[0])); i++)
        destroy_layer(&g_layers[i]);

    if (g_gs != NULL) {
        gsKit_deinit_global(g_gs);
        g_gs = NULL;
    }

    g_screen_w = 0;
    g_screen_h = 0;
    g_logo_x = -1;
    g_logo_y = -1;
    g_logo_visible = 0;
    g_hotkeys_x = -1;
    g_hotkeys_y = -1;
    g_hotkeys_visible = 0;
    reset_logo_shimmer_state();
}

static int upload_layer_texture(SPLASH_LAYER *layer, const SPLASH_IMAGE *img, int filter)
{
    size_t tex_size;

    if (layer == NULL || img == NULL)
        return 0;

    if (img->pixels_rbga == NULL || img->width == 0 || img->height == 0)
        return 0;

    memset(&layer->tex, 0, sizeof(layer->tex));
    layer->tex.Width = img->width;
    layer->tex.Height = img->height;
    layer->tex.PSM = GS_PSM_CT32;
    layer->tex.Filter = filter;

    tex_size = gsKit_texture_size(layer->tex.Width, layer->tex.Height, layer->tex.PSM);

    layer->tex.Mem = memalign(128, tex_size);
    if (layer->tex.Mem == NULL) {
        destroy_layer(layer);
        return 0;
    }

    memset(layer->tex.Mem, 0, tex_size);
    if (rbga_to_rgba(img, (unsigned char *)layer->tex.Mem, tex_size) != 0) {
        destroy_layer(layer);
        return 0;
    }

    layer->tex.Vram = gsKit_vram_alloc(g_gs, tex_size, GSKIT_ALLOC_USERBUFFER);
#ifdef GSKIT_ALLOC_ERROR
    if (layer->tex.Vram == GSKIT_ALLOC_ERROR) {
        destroy_layer(layer);
        return 0;
    }
#endif

    gsKit_texture_upload(g_gs, &layer->tex);
    layer->ready = 1;
    return 1;
}

static void draw_layer(const SPLASH_LAYER *layer, int x, int y, int z, unsigned char alpha)
{
    if (layer == NULL || !layer->ready)
        return;

    gsKit_prim_sprite_texture(g_gs,
                              (GSTEXTURE *)&layer->tex,
                              (float)x,
                              (float)y,
                              0.0f,
                              0.0f,
                              (float)(x + (int)layer->tex.Width),
                              (float)(y + (int)layer->tex.Height),
                              (float)layer->tex.Width,
                              (float)layer->tex.Height,
                              z,
                              GS_SETREG_RGBAQ(0x80, 0x80, 0x80, alpha, 0x00));
}

static void draw_layer_stretched(const SPLASH_LAYER *layer, int z)
{
    if (layer == NULL || !layer->ready)
        return;

    gsKit_prim_sprite_texture(g_gs,
                              (GSTEXTURE *)&layer->tex,
                              0.0f,
                              0.0f,
                              0.0f,
                              0.0f,
                              (float)g_screen_w,
                              (float)g_screen_h,
                              (float)layer->tex.Width,
                              (float)layer->tex.Height,
                              z,
                              GS_SETREG_RGBAQ(0x80, 0x80, 0x80, GS_ALPHA_OPAQUE, 0x00));
}

static void draw_logo_subrect(int rel_x,
                              int rel_w,
                              int z,
                              unsigned char alpha,
                              unsigned char rgb)
{
    const SPLASH_LAYER *logo = &g_layers[LAYER_LOGO];

    if (!g_logo_visible || !logo->ready || rel_w <= 0 || alpha == 0)
        return;

    if (rel_x < 0) {
        rel_w += rel_x;
        rel_x = 0;
    }
    if (rel_x >= (int)logo->tex.Width)
        return;
    if (rel_x + rel_w > (int)logo->tex.Width)
        rel_w = (int)logo->tex.Width - rel_x;
    if (rel_w <= 0)
        return;

    gsKit_prim_sprite_texture(g_gs,
                              (GSTEXTURE *)&logo->tex,
                              (float)(g_logo_x + rel_x),
                              (float)g_logo_y,
                              (float)rel_x,
                              0.0f,
                              (float)(g_logo_x + rel_x + rel_w),
                              (float)(g_logo_y + (int)logo->tex.Height),
                              (float)(rel_x + rel_w),
                              (float)logo->tex.Height,
                              z,
                              GS_SETREG_RGBAQ(rgb, rgb, rgb, alpha, 0x00));
}

static void draw_logo_subrect_shifted(int rel_x,
                                      int rel_w,
                                      int src_shift_x,
                                      int z,
                                      unsigned char alpha,
                                      unsigned char rgb)
{
    const SPLASH_LAYER *logo = &g_layers[LAYER_LOGO];
    int dst_x0;
    int dst_x1;
    int src_x0;
    int src_x1;

    if (!g_logo_visible || !logo->ready || rel_w <= 0 || alpha == 0)
        return;

    dst_x0 = rel_x;
    dst_x1 = rel_x + rel_w;
    if (dst_x1 <= 0 || dst_x0 >= (int)logo->tex.Width)
        return;

    if (dst_x0 < 0)
        dst_x0 = 0;
    if (dst_x1 > (int)logo->tex.Width)
        dst_x1 = (int)logo->tex.Width;
    if (dst_x0 >= dst_x1)
        return;

    src_x0 = dst_x0 + src_shift_x;
    src_x1 = dst_x1 + src_shift_x;

    if (src_x0 < 0) {
        dst_x0 += -src_x0;
        src_x0 = 0;
    }
    if (src_x1 > (int)logo->tex.Width) {
        dst_x1 -= (src_x1 - (int)logo->tex.Width);
        src_x1 = (int)logo->tex.Width;
    }
    if (dst_x0 >= dst_x1 || src_x0 >= src_x1)
        return;

    gsKit_prim_sprite_texture(g_gs,
                              (GSTEXTURE *)&logo->tex,
                              (float)(g_logo_x + dst_x0),
                              (float)g_logo_y,
                              (float)src_x0,
                              0.0f,
                              (float)(g_logo_x + dst_x1),
                              (float)(g_logo_y + (int)logo->tex.Height),
                              (float)src_x1,
                              (float)logo->tex.Height,
                              z,
                              GS_SETREG_RGBAQ(rgb, rgb, rgb, alpha, 0x00));
}

static void draw_logo_shimmer_band_lens(int band_left,
                                        int band_width,
                                        unsigned char highlight_peak_alpha,
                                        unsigned char halo_peak_alpha,
                                        unsigned char distort_peak_alpha)
{
    int i;

    if (band_width <= 0)
        return;

    for (i = 0; i < LOGO_SHIMMER_SLICE_COUNT; i++) {
        int slice_left = band_left + ((band_width * i) / LOGO_SHIMMER_SLICE_COUNT);
        int slice_right = band_left + ((band_width * (i + 1)) / LOGO_SHIMMER_SLICE_COUNT);
        int slice_width = slice_right - slice_left;
        int center_numer = (2 * i + 1) - LOGO_SHIMMER_SLICE_COUNT;
        int abs_center_numer = abs(center_numer);
        int edge_scale_numer = LOGO_SHIMMER_SLICE_COUNT - abs_center_numer;
        int edge_scale_sq_numer;
        int edge_soft_numer;
        int distort_profile_numer;
        int distort_shift;
        unsigned char highlight_alpha;
        unsigned char halo_alpha;
        unsigned char distort_alpha;

        if (slice_width <= 0 || edge_scale_numer <= 0)
            continue;

        edge_scale_sq_numer = edge_scale_numer * edge_scale_numer;
        edge_soft_numer = (edge_scale_sq_numer * edge_scale_sq_numer);
        highlight_alpha = (unsigned char)((highlight_peak_alpha * edge_soft_numer + 5000) / 10000);
        halo_alpha = (unsigned char)((halo_peak_alpha * edge_scale_sq_numer + 50) / 100);
        distort_profile_numer = 100 - ((abs_center_numer * abs_center_numer * 100) /
                                       (LOGO_SHIMMER_SLICE_COUNT * LOGO_SHIMMER_SLICE_COUNT));
        if (distort_profile_numer < 0)
            distort_profile_numer = 0;
        distort_alpha = (unsigned char)((distort_peak_alpha * edge_scale_numer + (LOGO_SHIMMER_SLICE_COUNT / 2)) /
                                        LOGO_SHIMMER_SLICE_COUNT);
        distort_shift = (center_numer * distort_profile_numer * LOGO_SHIMMER_DISTORT_MAX_SHIFT_PX) /
                        (LOGO_SHIMMER_SLICE_COUNT * 100);

        if (distort_alpha > 0)
            draw_logo_subrect_shifted(slice_left, slice_width, distort_shift, FG_Z + 1, distort_alpha, 0x80);
        if (halo_alpha > 0)
            draw_logo_subrect(slice_left, slice_width, FG_Z + 2, halo_alpha, 0xFF);
        if (highlight_alpha == 0)
            continue;
        draw_logo_subrect(slice_left, slice_width, FG_Z + 3, highlight_alpha, 0xFF);
    }
}

static void draw_logo_shimmer_overlay(void)
{
#if LOGO_SHIMMER_ENABLED
    const SPLASH_LAYER *logo = &g_layers[LAYER_LOGO];
    unsigned char base_logo_alpha;
    unsigned char highlight_peak_alpha;
    unsigned char halo_peak_alpha;
    unsigned char distort_peak_alpha;

    if (!g_logo_visible || !logo->ready || logo->tex.Width == 0)
        return;

    if (g_logo_shimmer_band_width <= 0 || g_logo_shimmer_band_width > (int)logo->tex.Width)
        g_logo_shimmer_band_width = compute_logo_shimmer_band_width((int)logo->tex.Width);
    if (g_logo_shimmer_band_width <= 0)
        return;

    base_logo_alpha = get_logo_base_alpha();
    highlight_peak_alpha = opacity_percent_to_gs_alpha(LOGO_SHIMMER_HIGHLIGHT_OPACITY_PERCENT);
    halo_peak_alpha = opacity_percent_to_gs_alpha(LOGO_SHIMMER_HALO_OPACITY_PERCENT);
    distort_peak_alpha = opacity_percent_to_gs_alpha(LOGO_SHIMMER_DISTORT_OPACITY_PERCENT);
    highlight_peak_alpha = (unsigned char)(((unsigned int)highlight_peak_alpha * (unsigned int)base_logo_alpha + (GS_ALPHA_OPAQUE / 2)) / GS_ALPHA_OPAQUE);
    halo_peak_alpha = (unsigned char)(((unsigned int)halo_peak_alpha * (unsigned int)base_logo_alpha + (GS_ALPHA_OPAQUE / 2)) / GS_ALPHA_OPAQUE);
    distort_peak_alpha = (unsigned char)(((unsigned int)distort_peak_alpha * (unsigned int)base_logo_alpha + (GS_ALPHA_OPAQUE / 2)) / GS_ALPHA_OPAQUE);
    draw_logo_shimmer_band_lens(g_logo_shimmer_left,
                                g_logo_shimmer_band_width,
                                highlight_peak_alpha,
                                halo_peak_alpha,
                                distort_peak_alpha);

    g_logo_shimmer_left += LOGO_SHIMMER_SPEED_PX_PER_FRAME;
    if (g_logo_shimmer_left > ((int)logo->tex.Width + g_logo_shimmer_band_width))
        g_logo_shimmer_left = -g_logo_shimmer_band_width;
#endif
}

void SplashRenderRestoreBackgroundRect(int x, int y, int w, int h)
{
    int x0;
    int y0;
    int x1;
    int y1;
    float u0;
    float v0;
    float u1;
    float v1;
    const SPLASH_LAYER *bg = &g_layers[LAYER_BG];

    if (g_gs == NULL || !bg->ready || g_screen_w <= 0 || g_screen_h <= 0 || w <= 0 || h <= 0)
        return;

    x0 = x;
    y0 = y;
    x1 = x + w;
    y1 = y + h;

    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > g_screen_w)
        x1 = g_screen_w;
    if (y1 > g_screen_h)
        y1 = g_screen_h;

    if (x0 >= x1 || y0 >= y1)
        return;

    u0 = ((float)x0 * (float)bg->tex.Width) / (float)g_screen_w;
    v0 = ((float)y0 * (float)bg->tex.Height) / (float)g_screen_h;
    u1 = ((float)x1 * (float)bg->tex.Width) / (float)g_screen_w;
    v1 = ((float)y1 * (float)bg->tex.Height) / (float)g_screen_h;

    gsKit_prim_sprite_texture(g_gs,
                              (GSTEXTURE *)&bg->tex,
                              (float)x0,
                              (float)y0,
                              u0,
                              v0,
                              (float)x1,
                              (float)y1,
                              u1,
                              v1,
                              FG_Z,
                              GS_SETREG_RGBAQ(0x80, 0x80, 0x80, GS_ALPHA_OPAQUE, 0x00));
}

void SplashRenderSetHotkeysVisible(int visible)
{
    g_hotkeys_visible = (visible != 0);
}

static void draw_static_layers(void)
{
    const unsigned char logo_alpha = get_logo_base_alpha();

    draw_layer_stretched(&g_layers[LAYER_BG], BG_Z);
    if (g_logo_visible) {
        draw_layer(&g_layers[LAYER_LOGO], g_logo_x, g_logo_y, FG_Z, logo_alpha);
        draw_logo_shimmer_overlay();
    }
    if (g_hotkeys_visible)
        draw_layer(&g_layers[LAYER_HOTKEYS], g_hotkeys_x, g_hotkeys_y, FG_Z, GS_ALPHA_OPAQUE);
}

void SplashRenderBeginFrame(void)
{
    if (g_gs == NULL)
        return;

    gsKit_clear(g_gs, GS_SETREG_RGBAQ(0x00, 0x00, 0x00, 0x80, 0x00));
    draw_static_layers();
}

void SplashRenderPresent(void)
{
    if (g_gs == NULL)
        return;

    gsKit_queue_exec(g_gs);
    gsKit_finish();
    gsKit_sync_flip(g_gs);
}

int SplashRenderBegin(int logo_disp, int is_psx_desr)
{
    const SPLASH_IMAGE *bg;
    const SPLASH_IMAGE *logo;
    int logo_center_y;
    int center_x;
    int center_y;
    int pass;

    destroy_frame_state();

    if (logo_disp < 2)
        return 0;

    g_gs = gsKit_init_global();
    if (g_gs == NULL)
        return 0;

    g_gs->DoubleBuffering = GS_SETTING_ON;
    g_gs->ZBuffering = GS_SETTING_OFF;
    g_gs->PrimAlphaEnable = GS_SETTING_ON;
    gsKit_set_primalpha(g_gs, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
    gsKit_init_screen(g_gs);
    gsKit_display_buffer(g_gs);
    gsKit_mode_switch(g_gs, GS_ONESHOT);
    // Prime both framebuffers to opaque black to avoid mode-switch garbage flashes.
    for (pass = 0; pass < 2; pass++) {
        gsKit_clear(g_gs, GS_SETREG_RGBAQ(0x00, 0x00, 0x00, 0x80, 0x00));
        gsKit_queue_exec(g_gs);
        gsKit_finish();
        gsKit_sync_flip(g_gs);
    }

    g_screen_w = (int)g_gs->Width;
    g_screen_h = (int)g_gs->Height;
    center_x = g_screen_w / 2;
    center_y = g_screen_h / 2;

    bg = SplashGetBackgroundImage(is_psx_desr);
    if (!upload_layer_texture(&g_layers[LAYER_BG], bg, GS_FILTER_LINEAR)) {
        destroy_frame_state();
        return 0;
    }

    logo = SplashGetLogoImage(is_psx_desr);
    if (!upload_layer_texture(&g_layers[LAYER_LOGO], logo, GS_FILTER_NEAREST)) {
        destroy_frame_state();
        return 0;
    }
    logo_center_y = logo_visual_center_y(logo->height);

    if (logo_disp == 2) {
        g_logo_x = center_x - ((int)logo->width / 2) + MODE2_LOGO_X_FROM_CENTER;
        g_logo_y = center_y - logo_center_y + MODE2_LOGO_Y_FROM_CENTER;
        g_logo_visible = 1;
        g_hotkeys_visible = 0;
        init_logo_shimmer_state();
        SplashRenderBeginFrame();
        SplashRenderPresent();
        return 1;
    }

    {
        const SPLASH_IMAGE *hotkeys = SplashGetHotkeysImage();
        int logo_x;
        int logo_y;
        int hotkeys_x;
        int hotkeys_y;

        if (!upload_layer_texture(&g_layers[LAYER_HOTKEYS], hotkeys, GS_FILTER_NEAREST)) {
            destroy_frame_state();
            return 0;
        }

        logo_x = center_x - ((int)logo->width / 2) + MODE35_LOGO_X_FROM_CENTER;
        logo_y = center_y - logo_center_y + MODE35_LOGO_Y_FROM_CENTER;

        hotkeys_x = ((g_screen_w * MODE35_HOTKEYS_LEFT_PERCENT) + 50) / 100 + MODE35_HOTKEYS_X_ADJUST;
        hotkeys_y = center_y - ((int)hotkeys->height / 2) + MODE35_HOTKEYS_Y_FROM_CENTER;
        g_logo_x = logo_x;
        g_logo_y = logo_y;
        g_logo_visible = 1;
        g_hotkeys_x = hotkeys_x;
        g_hotkeys_y = hotkeys_y;
        g_hotkeys_visible = 1;
        init_logo_shimmer_state();

        // For LOGO_DISPLAY 3-5, defer the first present until the caller draws
        // hotkey text lines so the image/text appear together.
    }

    return 1;
}

void SplashRenderDrawTextPxScaled(int x, int y, u32 color, const char *text, int scale)
{
    int draw_scale;
    int advance;
    int i;
    int cx;
    int shadow_offset_x;
    int shadow_offset_y;
    u64 gs_color;
    u64 shadow_color;

    if (g_gs == NULL || text == NULL)
        return;

    gs_color = color_to_gs(color);
    shadow_color = GS_SETREG_RGBAQ(0x00,
                                   0x00,
                                   0x00,
                                   transparency_percent_to_gs_alpha(GLYPH_SHADOW_TRANSPARENCY_PERCENT),
                                   0x00);
    draw_scale = (scale > 0) ? scale : 1;
    advance = (FONT_W + 1) * draw_scale;
    shadow_offset_x = GLYPH_SHADOW_OFFSET_X;
    shadow_offset_y = GLYPH_SHADOW_OFFSET_Y;

    // Pass 1: draw all shadows so glyph pixels can always render above them.
    cx = x;
    for (i = 0; text[i] != '\0'; i++) {
        int row;
        const GLYPH *glyph = find_glyph(text[i]);
        for (row = 0; row < FONT_H; row++) {
            unsigned char bits = glyph->rows[row];
            int col;
            for (col = 0; col < FONT_W; col++) {
                if (bits & (1u << (FONT_W - 1 - col))) {
                    int px = cx + (col * draw_scale);
                    int py = y + (row * draw_scale);
                    gsKit_prim_sprite(g_gs,
                                      (float)(px + shadow_offset_x),
                                      (float)(py + shadow_offset_y),
                                      (float)(px + shadow_offset_x + draw_scale),
                                      (float)(py + shadow_offset_y + draw_scale),
                                      TEXT_SHADOW_Z,
                                      shadow_color);
                }
            }
        }
        cx += advance;
    }

    // Pass 2: draw glyph pixels on top of all shadow pixels.
    cx = x;
    for (i = 0; text[i] != '\0'; i++) {
        int row;
        const GLYPH *glyph = find_glyph(text[i]);
        for (row = 0; row < FONT_H; row++) {
            unsigned char bits = glyph->rows[row];
            int col;
            for (col = 0; col < FONT_W; col++) {
                if (bits & (1u << (FONT_W - 1 - col))) {
                    int px = cx + (col * draw_scale);
                    int py = y + (row * draw_scale);
                    gsKit_prim_sprite(g_gs,
                                      (float)px,
                                      (float)py,
                                      (float)(px + draw_scale),
                                      (float)(py + draw_scale),
                                      TEXT_Z,
                                      gs_color);
                }
            }
        }
        cx += advance;
    }
}

void SplashRenderDrawTextPx(int x, int y, u32 color, const char *text)
{
    SplashRenderDrawTextPxScaled(x, y, color, text, FONT_SCALE);
}

int SplashRenderIsActive(void)
{
    return (g_gs != NULL);
}

void SplashRenderEnd(void)
{
    SplashRenderPresent();
    destroy_frame_state();
}

int SplashRenderGetScreenWidth(void)
{
    return (g_screen_w > 0) ? g_screen_w : 640;
}

int SplashRenderGetScreenHeight(void)
{
    return (g_screen_h > 0) ? g_screen_h : 480;
}

int SplashRenderGetScreenCenterX(void)
{
    return SplashRenderGetScreenWidth() / 2;
}

int SplashRenderGetScreenCenterY(void)
{
    return SplashRenderGetScreenHeight() / 2;
}

int SplashRenderGetHotkeysX(void)
{
    return g_hotkeys_x;
}

int SplashRenderGetHotkeysY(void)
{
    return g_hotkeys_y;
}
