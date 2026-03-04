#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include "splash_assets.h"
#include "splash_render.h"
#include <dmaKit.h>
#include <gsKit.h>

#define FONT_W 5
#define FONT_H 7
#define FONT_SCALE 2
#define FONT_ADVANCE ((FONT_W + 1) * FONT_SCALE)
#define TEXT_Z 10
#define BG_Z 1

typedef struct
{
    char ch;
    unsigned char rows[FONT_H];
} GLYPH;

static GSGLOBAL *g_gs = NULL;
static int g_image_x = 0;
static int g_image_y = 0;
static GSTEXTURE g_tex;
static int g_tex_ready = 0;

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
        size_t off = i * 4;
        unsigned char r = img->pixels_rbga[off + 0];
        unsigned char b = img->pixels_rbga[off + 1];
        unsigned char g = img->pixels_rbga[off + 2];
        unsigned char a = img->pixels_rbga[off + 3];
        dst_rgba[off + 0] = r;
        dst_rgba[off + 1] = g;
        dst_rgba[off + 2] = b;
        dst_rgba[off + 3] = a;
    }

    return 0;
}

static void destroy_frame_state(void)
{
    if (g_tex_ready) {
        if (g_tex.Mem != NULL)
            free(g_tex.Mem);
        memset(&g_tex, 0, sizeof(g_tex));
        g_tex_ready = 0;
    }
    if (g_gs != NULL) {
        gsKit_deinit_global(g_gs);
        g_gs = NULL;
    }
}

int SplashRenderBegin(int logo_disp, int is_psx_desr)
{
    const SPLASH_IMAGE *img = NULL;
    size_t tex_size;

    destroy_frame_state();
    g_image_x = 0;
    g_image_y = 0;

    if (logo_disp < 2)
        return 0;

    img = (logo_disp == 2) ? SplashGetLogoImage(is_psx_desr) : SplashGetTemplateImage(is_psx_desr);
    if (img == NULL || img->pixels_rbga == NULL || img->width == 0 || img->height == 0)
        return 0;

    g_gs = gsKit_init_global();
    if (g_gs == NULL)
        return 0;

    g_gs->DoubleBuffering = GS_SETTING_ON;
    g_gs->ZBuffering = GS_SETTING_ON;
    gsKit_init_screen(g_gs);
    gsKit_display_buffer(g_gs);
    gsKit_mode_switch(g_gs, GS_ONESHOT);
    gsKit_clear(g_gs, GS_SETREG_RGBAQ(0x00, 0x00, 0x00, 0x00, 0x00));

    memset(&g_tex, 0, sizeof(g_tex));
    g_tex.Width = img->width;
    g_tex.Height = img->height;
    g_tex.PSM = GS_PSM_CT32;
    g_tex.Filter = GS_FILTER_NEAREST;
    tex_size = gsKit_texture_size(g_tex.Width, g_tex.Height, g_tex.PSM);
    g_tex.Mem = memalign(128, tex_size);
    if (g_tex.Mem == NULL) {
        destroy_frame_state();
        return 0;
    }
    if (rbga_to_rgba(img, (unsigned char *)g_tex.Mem, tex_size) != 0) {
        destroy_frame_state();
        return 0;
    }

    g_tex.Vram = gsKit_vram_alloc(g_gs, tex_size, GSKIT_ALLOC_USERBUFFER);
    gsKit_texture_upload(g_gs, &g_tex);
    g_tex_ready = 1;

    g_image_x = ((int)g_gs->Width - (int)img->width) / 2;
    g_image_y = ((int)g_gs->Height - (int)img->height) / 2;
    if (g_image_x < 0)
        g_image_x = 0;
    if (g_image_y < 0)
        g_image_y = 0;

    gsKit_prim_sprite_texture(g_gs,
                              &g_tex,
                              (float)g_image_x,
                              (float)g_image_y,
                              0.0f,
                              0.0f,
                              (float)(g_image_x + (int)img->width),
                              (float)(g_image_y + (int)img->height),
                              (float)img->width,
                              (float)img->height,
                              BG_Z,
                              GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x80, 0x00));
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

int SplashRenderGetImageX(void)
{
    return g_image_x;
}

int SplashRenderGetImageY(void)
{
    return g_image_y;
}
