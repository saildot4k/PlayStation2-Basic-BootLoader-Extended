#include <malloc.h>
#include <stdlib.h>
#include <string.h>

#include <dmaKit.h>
#include <gsKit.h>

#include "splash_assets.h"
#include "splash_render.h"

#define FONT_SRC_W 5
#define FONT_SRC_H 7
#define FONT_W 8
#define FONT_H 16
#define FONT_ADVANCE FONT_W
#define FONT_AA_SAMPLES 4
#define FONT_AA_LEVELS (FONT_AA_SAMPLES * FONT_AA_SAMPLES)
#define TEXT_Z 10
#define BG_Z 1
#define FG_Z 2

// LOGO_DISPLAY = 2 centered logo fine tuning.
#define MODE2_LOGO_X_FROM_CENTER 0
#define MODE2_LOGO_Y_FROM_CENTER 0

// LOGO_DISPLAY = 3-5 layout:
// - Logo top-center with overscan margin.
// - Hotkeys image left aligned and stacked under the logo.
// X/Y values are center-relative tuning offsets in pixels.
#define MODE35_TOP_MARGIN_PX 10
#define MODE35_LEFT_MARGIN_PX 10
#define MODE35_STACK_GAP_PX 0
#define MODE35_LOGO_X_FROM_CENTER 0
#define MODE35_LOGO_Y_FROM_CENTER -5
#define MODE35_HOTKEYS_X_FROM_CENTER 10
#define MODE35_HOTKEYS_Y_FROM_CENTER -10

typedef struct
{
    char ch;
    unsigned char rows[FONT_SRC_H];
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
static int g_hotkeys_x = -1;
static int g_hotkeys_y = -1;

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

static unsigned char g_font_aa[sizeof(g_font) / sizeof(g_font[0])][FONT_H][FONT_W];
static int g_font_aa_ready = 0;

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

static int glyph_source_pixel_on(const GLYPH *g, int sx, int sy)
{
    if (g == NULL)
        return 0;
    if (sx < 0 || sx >= FONT_SRC_W || sy < 0 || sy >= FONT_SRC_H)
        return 0;
    return (g->rows[sy] & (1u << (FONT_SRC_W - 1 - sx))) ? 1 : 0;
}

static void build_font_aa_table(void)
{
    int gi;

    if (g_font_aa_ready)
        return;

    for (gi = 0; gi < (int)(sizeof(g_font) / sizeof(g_font[0])); gi++) {
        int dy;
        for (dy = 0; dy < FONT_H; dy++) {
            int dx;
            for (dx = 0; dx < FONT_W; dx++) {
                int hits = 0;
                int sy;
                for (sy = 0; sy < FONT_AA_SAMPLES; sy++) {
                    int sx;
                    for (sx = 0; sx < FONT_AA_SAMPLES; sx++) {
                        int src_x_num = (((dx * FONT_AA_SAMPLES) + sx) * 2 + 1) * FONT_SRC_W;
                        int src_y_num = (((dy * FONT_AA_SAMPLES) + sy) * 2 + 1) * FONT_SRC_H;
                        int src_den = FONT_W * FONT_AA_SAMPLES * 2;
                        int src_x = src_x_num / src_den;
                        int src_y = src_y_num / (FONT_H * FONT_AA_SAMPLES * 2);
                        hits += glyph_source_pixel_on(&g_font[gi], src_x, src_y);
                    }
                }
                g_font_aa[gi][dy][dx] = (unsigned char)hits;
            }
        }
    }

    g_font_aa_ready = 1;
}

static void color_to_rgb(u32 color, int *r, int *g, int *b)
{
    if (r != NULL)
        *r = (color >> 16) & 0xff;
    if (g != NULL)
        *g = (color >> 8) & 0xff;
    if (b != NULL)
        *b = color & 0xff;
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
    g_hotkeys_x = -1;
    g_hotkeys_y = -1;
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

static void draw_layer(const SPLASH_LAYER *layer, int x, int y, int z)
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
                              GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x80, 0x00));
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
                              GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x80, 0x00));
}

int SplashRenderBegin(int logo_disp, int is_psx_desr)
{
    const SPLASH_IMAGE *bg;
    const SPLASH_IMAGE *logo;
    int logo_y_offset;
    int center_x;
    int center_y;

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
    gsKit_clear(g_gs, GS_SETREG_RGBAQ(0x00, 0x00, 0x00, 0x00, 0x00));

    g_screen_w = (int)g_gs->Width;
    g_screen_h = (int)g_gs->Height;
    logo_y_offset = 6;
    center_x = g_screen_w / 2;
    center_y = g_screen_h / 2;

    bg = SplashGetBackgroundImage(is_psx_desr);
    if (!upload_layer_texture(&g_layers[LAYER_BG], bg, GS_FILTER_LINEAR)) {
        destroy_frame_state();
        return 0;
    }

    draw_layer_stretched(&g_layers[LAYER_BG], BG_Z);

    logo = SplashGetLogoImage(is_psx_desr);
    if (!upload_layer_texture(&g_layers[LAYER_LOGO], logo, GS_FILTER_NEAREST)) {
        destroy_frame_state();
        return 0;
    }

    if (logo_disp == 2) {
        int logo_x = center_x - ((int)logo->width / 2) + MODE2_LOGO_X_FROM_CENTER;
        int logo_y = center_y - ((int)logo->height / 2) + MODE2_LOGO_Y_FROM_CENTER + logo_y_offset;
        draw_layer(&g_layers[LAYER_LOGO], logo_x, logo_y, FG_Z);
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
        logo_y = center_y + (-(g_screen_h / 2) + MODE35_TOP_MARGIN_PX + MODE35_LOGO_Y_FROM_CENTER + logo_y_offset);

        hotkeys_x = center_x + (-(g_screen_w / 2) + MODE35_LEFT_MARGIN_PX + MODE35_HOTKEYS_X_FROM_CENTER);
        hotkeys_y = center_y + (-(g_screen_h / 2) + MODE35_TOP_MARGIN_PX + (int)logo->height + MODE35_STACK_GAP_PX + MODE35_HOTKEYS_Y_FROM_CENTER);
        g_hotkeys_x = hotkeys_x;
        g_hotkeys_y = hotkeys_y;

        draw_layer(&g_layers[LAYER_LOGO], logo_x, logo_y, FG_Z);
        draw_layer(&g_layers[LAYER_HOTKEYS], hotkeys_x, hotkeys_y, FG_Z);
    }

    return 1;
}

void SplashRenderDrawTextPx(int x, int y, u32 color, const char *text)
{
    int i;
    int cx;
    int r;
    int g_ch;
    int b;
    u64 aa_colors[FONT_AA_LEVELS + 1];
    int level;

    if (g_gs == NULL || text == NULL)
        return;

    build_font_aa_table();

    color_to_rgb(color, &r, &g_ch, &b);
    for (level = 0; level <= FONT_AA_LEVELS; level++) {
        unsigned char alpha255 = (unsigned char)((level * 255 + (FONT_AA_LEVELS / 2)) / FONT_AA_LEVELS);
        aa_colors[level] = GS_SETREG_RGBAQ(r, g_ch, b, png_alpha_to_gs_alpha(alpha255), 0x00);
    }

    cx = x;
    for (i = 0; text[i] != '\0'; i++) {
        int glyph_index;
        int row;
        const GLYPH *glyph = find_glyph(text[i]);
        glyph_index = (int)(glyph - g_font);
        for (row = 0; row < FONT_H; row++) {
            int col;
            for (col = 0; col < FONT_W; col++) {
                unsigned char coverage = g_font_aa[glyph_index][row][col];
                if (coverage != 0) {
                    int px = cx + col;
                    int py = y + row;
                    gsKit_prim_sprite(g_gs,
                                      (float)px,
                                      (float)py,
                                      (float)(px + 1),
                                      (float)(py + 1),
                                      TEXT_Z,
                                      aa_colors[coverage]);
                }
            }
        }
        cx += FONT_ADVANCE;
    }
}

int SplashRenderIsActive(void)
{
    return (g_gs != NULL);
}

void SplashRenderEnd(void)
{
    if (g_gs != NULL) {
        gsKit_queue_exec(g_gs);
        gsKit_finish();
        gsKit_sync_flip(g_gs);
    }
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
