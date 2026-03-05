#include <malloc.h>
#include <stdlib.h>
#include <string.h>

#include <dmaKit.h>
#include <gsKit.h>

#include "splash_assets.h"
#include "splash_render.h"

#define FONT_W 5
#define FONT_H 7
#define FONT_SCALE 1
#define FONT_ADVANCE ((FONT_W + 1) * FONT_SCALE)
#define TEXT_Z 10
#define BG_Z 1

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
#define MODE35_LOGO_Y_FROM_CENTER 0
#define MODE35_HOTKEYS_X_FROM_CENTER 0
#define MODE35_HOTKEYS_Y_FROM_CENTER 0

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

static GSGLOBAL *g_gs = NULL;
static SPLASH_LAYER g_layers[2];
static int g_screen_w = 0;
static int g_screen_h = 0;

// 5x7 uppercase glyphs for splash labels.
static const GLYPH g_font[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06}},
    {':', {0x00, 0x06, 0x06, 0x00, 0x06, 0x06, 0x00}},
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
    {'_', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}},
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
    return GS_SETREG_RGBAQ(r, g, b, 0x80, 0x00);
}

static size_t min_size(size_t a, size_t b)
{
    return (a < b) ? a : b;
}

static void rbga_clut_to_rgba(const unsigned char *src_rbga,
                              unsigned char *dst_rgba,
                              unsigned int entry_count)
{
    unsigned int i;

    if (src_rbga == NULL || dst_rgba == NULL)
        return;

    for (i = 0; i < entry_count; i++) {
        unsigned int off = i * 4u;
        dst_rgba[off + 0] = src_rbga[off + 0];
        dst_rgba[off + 1] = src_rbga[off + 2];
        dst_rgba[off + 2] = src_rbga[off + 1];
        dst_rgba[off + 3] = src_rbga[off + 3];
    }
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
}

static int upload_layer_texture(SPLASH_LAYER *layer, const SPLASH_IMAGE *img)
{
    size_t tex_size;
    size_t clut_size;
    size_t raw_tex_size;
    size_t raw_clut_size;

    if (layer == NULL || img == NULL)
        return 0;

    if (img->pixels_t8 == NULL || img->clut_rbga == NULL || img->width == 0 || img->height == 0)
        return 0;

    memset(&layer->tex, 0, sizeof(layer->tex));
    layer->tex.Width = img->width;
    layer->tex.Height = img->height;
    layer->tex.PSM = GS_PSM_T8;
    layer->tex.ClutPSM = GS_PSM_CT32;
    layer->tex.Filter = GS_FILTER_NEAREST;

    tex_size = gsKit_texture_size(layer->tex.Width, layer->tex.Height, layer->tex.PSM);
    clut_size = gsKit_texture_size(16, 16, layer->tex.ClutPSM);
    raw_tex_size = (size_t)img->width * (size_t)img->height;
    raw_clut_size = (size_t)img->clut_entries * 4u;

    layer->tex.Mem = memalign(128, tex_size);
    layer->tex.Clut = memalign(128, clut_size);
    if (layer->tex.Mem == NULL || layer->tex.Clut == NULL) {
        destroy_layer(layer);
        return 0;
    }

    memset(layer->tex.Mem, 0, tex_size);
    memset(layer->tex.Clut, 0, clut_size);
    memcpy(layer->tex.Mem, img->pixels_t8, min_size(raw_tex_size, tex_size));
    rbga_clut_to_rgba(img->clut_rbga,
                      (unsigned char *)layer->tex.Clut,
                      (unsigned int)(min_size(raw_clut_size, clut_size) / 4u));

    layer->tex.Vram = gsKit_vram_alloc(g_gs, tex_size, GSKIT_ALLOC_USERBUFFER);
    layer->tex.VramClut = gsKit_vram_alloc(g_gs, clut_size, GSKIT_ALLOC_USERBUFFER);
#ifdef GSKIT_ALLOC_ERROR
    if (layer->tex.Vram == GSKIT_ALLOC_ERROR || layer->tex.VramClut == GSKIT_ALLOC_ERROR) {
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

int SplashRenderBegin(int logo_disp, int is_psx_desr)
{
    const SPLASH_IMAGE *logo;
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
    gsKit_init_screen(g_gs);
    gsKit_display_buffer(g_gs);
    gsKit_mode_switch(g_gs, GS_ONESHOT);
    gsKit_clear(g_gs, GS_SETREG_RGBAQ(0x00, 0x00, 0x00, 0x00, 0x00));

    g_screen_w = (int)g_gs->Width;
    g_screen_h = (int)g_gs->Height;
    center_x = g_screen_w / 2;
    center_y = g_screen_h / 2;

    logo = SplashGetLogoImage(is_psx_desr);
    if (!upload_layer_texture(&g_layers[0], logo)) {
        destroy_frame_state();
        return 0;
    }

    if (logo_disp == 2) {
        int logo_x = center_x - ((int)logo->width / 2) + MODE2_LOGO_X_FROM_CENTER;
        int logo_y = center_y - ((int)logo->height / 2) + MODE2_LOGO_Y_FROM_CENTER;
        draw_layer(&g_layers[0], logo_x, logo_y, BG_Z);
        return 1;
    }

    {
        const SPLASH_IMAGE *hotkeys = SplashGetHotkeysImage();
        int logo_x;
        int logo_y;
        int hotkeys_x;
        int hotkeys_y;

        if (!upload_layer_texture(&g_layers[1], hotkeys)) {
            destroy_frame_state();
            return 0;
        }

        logo_x = center_x - ((int)logo->width / 2) + MODE35_LOGO_X_FROM_CENTER;
        logo_y = center_y + (-(g_screen_h / 2) + MODE35_TOP_MARGIN_PX + MODE35_LOGO_Y_FROM_CENTER);

        hotkeys_x = center_x + (-(g_screen_w / 2) + MODE35_LEFT_MARGIN_PX + MODE35_HOTKEYS_X_FROM_CENTER);
        hotkeys_y = center_y + (-(g_screen_h / 2) + MODE35_TOP_MARGIN_PX + (int)logo->height + MODE35_STACK_GAP_PX + MODE35_HOTKEYS_Y_FROM_CENTER);

        draw_layer(&g_layers[0], logo_x, logo_y, BG_Z);
        draw_layer(&g_layers[1], hotkeys_x, hotkeys_y, BG_Z);
    }

    return 1;
}

void SplashRenderDrawTextPx(int x, int y, u32 color, const char *text)
{
    int i;
    int cx;
    u64 gs_color;

    if (g_gs == NULL || text == NULL)
        return;

    gs_color = color_to_gs(color);
    cx = x;
    for (i = 0; text[i] != '\0'; i++) {
        int row;
        const GLYPH *g = find_glyph(text[i]);
        for (row = 0; row < FONT_H; row++) {
            unsigned char bits = g->rows[row];
            int col;
            for (col = 0; col < FONT_W; col++) {
                if (bits & (1u << (FONT_W - 1 - col))) {
                    int px = cx + (col * FONT_SCALE);
                    int py = y + (row * FONT_SCALE);
                    gsKit_prim_sprite(g_gs,
                                      (float)px,
                                      (float)py,
                                      (float)(px + FONT_SCALE),
                                      (float)(py + FONT_SCALE),
                                      TEXT_Z,
                                      gs_color);
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
