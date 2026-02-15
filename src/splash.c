#include "splash.h"
#include "splash_assets.h"
#include "splash_font.h"
#include <dmaKit.h>
#include <gsKit.h>
#include <zlib.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PNG_SIG_SIZE 8

static uint32_t read_be32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int decode_png_rgba(const unsigned char *data,
                           size_t size,
                           u32 **out_rgba,
                           unsigned int *out_w,
                           unsigned int *out_h)
{
    if (!data || size < PNG_SIG_SIZE || !out_rgba || !out_w || !out_h)
        return -1;

    if (memcmp(data, "\x89PNG\r\n\x1a\n", PNG_SIG_SIZE) != 0)
        return -1;

    unsigned int width = 0;
    unsigned int height = 0;
    int have_ihdr = 0;
    unsigned char *idat = NULL;
    size_t idat_size = 0;

    size_t offset = PNG_SIG_SIZE;
    while (offset + 8 <= size) {
        uint32_t length = read_be32(&data[offset]);
        offset += 4;
        if (offset + 4 > size)
            break;
        const unsigned char *type = &data[offset];
        offset += 4;
        if (offset + length + 4 > size)
            break;
        const unsigned char *chunk = &data[offset];
        offset += length;
        offset += 4; // CRC

        if (memcmp(type, "IHDR", 4) == 0) {
            if (length < 13)
                break;
            width = read_be32(&chunk[0]);
            height = read_be32(&chunk[4]);
            unsigned char bit_depth = chunk[8];
            unsigned char color_type = chunk[9];
            unsigned char compression = chunk[10];
            unsigned char filter_method = chunk[11];
            unsigned char interlace = chunk[12];
            if (bit_depth != 8 || color_type != 6 || compression != 0 || filter_method != 0 || interlace != 0)
                break;
            have_ihdr = 1;
        } else if (memcmp(type, "IDAT", 4) == 0) {
            unsigned char *new_buf = (unsigned char *)realloc(idat, idat_size + length);
            if (!new_buf)
                break;
            idat = new_buf;
            memcpy(idat + idat_size, chunk, length);
            idat_size += length;
        } else if (memcmp(type, "IEND", 4) == 0) {
            break;
        }
    }

    if (!have_ihdr || idat_size == 0 || width == 0 || height == 0) {
        free(idat);
        return -1;
    }

    size_t row_bytes = (size_t)width * 4;
    size_t expected = (row_bytes + 1) * (size_t)height;
    unsigned char *raw = (unsigned char *)malloc(expected);
    if (!raw) {
        free(idat);
        return -1;
    }

    uLongf raw_size = (uLongf)expected;
    if (uncompress(raw, &raw_size, idat, (uLongf)idat_size) != Z_OK || raw_size != expected) {
        free(raw);
        free(idat);
        return -1;
    }

    unsigned char *out = (unsigned char *)malloc(row_bytes * (size_t)height);
    if (!out) {
        free(raw);
        free(idat);
        return -1;
    }

    size_t y;
    for (y = 0; y < height; y++) {
        const unsigned char *src = raw + y * (row_bytes + 1);
        unsigned char filter = *src++;
        unsigned char *dst = out + y * row_bytes;
        const unsigned char *prior = (y > 0) ? (out + (y - 1) * row_bytes) : NULL;
        size_t x;
        switch (filter) {
            case 0: // None
                memcpy(dst, src, row_bytes);
                break;
            case 1: // Sub
                for (x = 0; x < row_bytes; x++) {
                    unsigned char left = (x >= 4) ? dst[x - 4] : 0;
                    dst[x] = (unsigned char)(src[x] + left);
                }
                break;
            case 2: // Up
                for (x = 0; x < row_bytes; x++) {
                    unsigned char up = prior ? prior[x] : 0;
                    dst[x] = (unsigned char)(src[x] + up);
                }
                break;
            case 3: // Average
                for (x = 0; x < row_bytes; x++) {
                    unsigned char left = (x >= 4) ? dst[x - 4] : 0;
                    unsigned char up = prior ? prior[x] : 0;
                    dst[x] = (unsigned char)(src[x] + ((left + up) >> 1));
                }
                break;
            case 4: { // Paeth
                for (x = 0; x < row_bytes; x++) {
                    int a = (x >= 4) ? dst[x - 4] : 0;
                    int b = prior ? prior[x] : 0;
                    int c = (prior && x >= 4) ? prior[x - 4] : 0;
                    int p = a + b - c;
                    int pa = p > a ? p - a : a - p;
                    int pb = p > b ? p - b : b - p;
                    int pc = p > c ? p - c : c - p;
                    int pr = (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
                    dst[x] = (unsigned char)(src[x] + pr);
                }
                break;
            }
            default:
                free(out);
                free(raw);
                free(idat);
                return -1;
        }
    }

    free(raw);
    free(idat);

    *out_rgba = (u32 *)out;
    *out_w = width;
    *out_h = height;
    return 0;
}

static const SplashGlyph *get_splash_glyph(char c)
{
    if (c < SPLASH_FONT_FIRST || c > SPLASH_FONT_LAST)
        c = '?';
    return &splash_font_glyphs[(int)(c - SPLASH_FONT_FIRST)];
}

int SplashBegin(SplashContext *ctx)
{
    if (!ctx)
        return -1;

    memset(ctx, 0, sizeof(*ctx));
    GSGLOBAL *gs = gsKit_init_global();
    if (!gs)
        return -1;

    // Single buffer to maximize VRAM for full-screen textures.
    gs->DoubleBuffering = GS_SETTING_OFF;
    gs->ZBuffering = GS_SETTING_OFF;
    gs->PrimAlphaEnable = GS_SETTING_ON;
    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC, D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    if (dmaKit_chan_init(DMA_CHANNEL_GIF)) {
        gsKit_deinit_global(gs);
        return -1;
    }

    gsKit_init_screen(gs);
    gsKit_display_buffer(gs);
    gsKit_mode_switch(gs, GS_ONESHOT);
    gsKit_clear(gs, GS_SETREG_RGBA(0x00, 0x00, 0x00, 0x00));

    ctx->gs = gs;
    ctx->screen_w = gs->Width;
    ctx->screen_h = gs->Height;
    ctx->img_w = 0;
    ctx->img_h = 0;
    ctx->img_scale = 1.0f;
    ctx->img_off_x = 0;
    ctx->img_off_y = 0;
    ctx->needs_present = 0;
    ctx->img_pixels = NULL;
    return 0;
}

void SplashEnd(SplashContext *ctx)
{
    if (!ctx || !ctx->gs)
        return;
    if (ctx->needs_present)
        SplashPresent(ctx);
    GSGLOBAL *gs = (GSGLOBAL *)ctx->gs;
    gsKit_deinit_global(gs);
    ctx->gs = NULL;
    if (ctx->img_pixels) {
        free(ctx->img_pixels);
        ctx->img_pixels = NULL;
    }
}

void SplashPresent(SplashContext *ctx)
{
    if (!ctx || !ctx->gs)
        return;
    if (!ctx->needs_present)
        return;
    GSGLOBAL *gs = (GSGLOBAL *)ctx->gs;
    if (ctx->img_pixels) {
        GSTEXTURE tex;
        memset(&tex, 0, sizeof(tex));
        tex.Width = ctx->img_w;
        tex.Height = ctx->img_h;
        tex.PSM = GS_PSM_CT32;
        tex.Mem = ctx->img_pixels;
        tex.Filter = GS_FILTER_LINEAR;
        tex.Vram = gsKit_vram_alloc(gs, gsKit_texture_size(tex.Width, tex.Height, tex.PSM), GSKIT_ALLOC_USERBUFFER);
        if (tex.Vram != 0) {
            gsKit_texture_upload(gs, &tex);
            int draw_w = (int)(ctx->img_w * ctx->img_scale);
            int draw_h = (int)(ctx->img_h * ctx->img_scale);
            int x = ctx->img_off_x;
            int y = ctx->img_off_y;
            tex.Filter = (ctx->img_scale < 1.0f) ? GS_FILTER_LINEAR : GS_FILTER_NEAREST;
            gsKit_prim_sprite_texture(gs,
                                      &tex,
                                      x,
                                      y,
                                      0.0f,
                                      0.0f,
                                      x + draw_w,
                                      y + draw_h,
                                      ctx->img_w,
                                      ctx->img_h,
                                      0,
                                      GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0xFF));
        }
    }
    gsKit_queue_exec(gs);
    gsKit_finish();
    gsKit_sync_flip(gs);
    if (ctx->img_pixels) {
        free(ctx->img_pixels);
        ctx->img_pixels = NULL;
    }
    ctx->needs_present = 0;
}

void SplashGetScreenSize(const SplashContext *ctx, int *out_w, int *out_h)
{
    if (out_w)
        *out_w = 0;
    if (out_h)
        *out_h = 0;
    if (!ctx || !ctx->gs)
        return;
    const GSGLOBAL *gs = (const GSGLOBAL *)ctx->gs;
    if (out_w)
        *out_w = gs->Width;
    if (out_h)
        *out_h = gs->Height;
}

void SplashTransformPoint(const SplashContext *ctx, int in_x, int in_y, int *out_x, int *out_y, int coords_are_image)
{
    if (out_x)
        *out_x = in_x;
    if (out_y)
        *out_y = in_y;
    if (!ctx || !ctx->gs)
        return;
    if (coords_are_image && ctx->img_scale > 0.0f) {
        if (out_x)
            *out_x = ctx->img_off_x + (int)(in_x * ctx->img_scale);
        if (out_y)
            *out_y = ctx->img_off_y + (int)(in_y * ctx->img_scale);
    }
}

int SplashGetImageForLogoDisplay(int logo_disp, SplashImage *out)
{
    if (!out)
        return -1;
    out->data = NULL;
    out->size = 0;

    if (logo_disp >= 3) {
#ifdef PSX
        out->data = psxbble_splash_template_png;
        out->size = size_psxbble_splash_template_png;
#else
        out->data = ps2bble_splash_template_png;
        out->size = size_ps2bble_splash_template_png;
#endif
        return 0;
    }
    if (logo_disp == 2) {
#ifdef PSX
        out->data = transparent_psxbble_png;
        out->size = size_transparent_psxbble_png;
#else
        out->data = transparent_ps2bble_png;
        out->size = size_transparent_ps2bble_png;
#endif
        return 0;
    }
    return -1;
}

int SplashDrawImage(SplashContext *ctx, const SplashImage *image)
{
    if (!ctx || !ctx->gs || !image || !image->data || image->size == 0)
        return -1;

    if (ctx->img_pixels) {
        free(ctx->img_pixels);
        ctx->img_pixels = NULL;
    }

    u32 *decoded = NULL;
    unsigned int w = 0, h = 0;
    if (decode_png_rgba(image->data, image->size, &decoded, &w, &h) != 0)
        return -1;

    // Force opaque since splash images don't need alpha.
    {
        size_t pixels = (size_t)w * (size_t)h;
        unsigned char *rgba = (unsigned char *)decoded;
        size_t i;
        for (i = 0; i < pixels; i++) {
            rgba[i * 4 + 3] = 0xFF;
        }
    }

    int screen_w = ((GSGLOBAL *)ctx->gs)->Width;
    int screen_h = ((GSGLOBAL *)ctx->gs)->Height;
    float scale_w = (float)screen_w / (float)w;
    float scale_h = (float)screen_h / (float)h;
    float scale = 1.0f;
    if (scale_w < 1.0f || scale_h < 1.0f) {
        scale = (scale_w < scale_h) ? scale_w : scale_h;
    }

    int draw_w = (int)(w * scale);
    int draw_h = (int)(h * scale);
    int x = (screen_w - draw_w) / 2;
    int y = (screen_h - draw_h) / 2;

    ctx->img_w = (int)w;
    ctx->img_h = (int)h;
    ctx->img_scale = scale;
    ctx->img_off_x = x;
    ctx->img_off_y = y;
    ctx->img_pixels = decoded;
    ctx->needs_present = 1;

    return 0;
}

void SplashDrawText(SplashContext *ctx, const char *text, const SplashTextConfig *cfg, uint32_t rgb)
{
    if (!ctx || !ctx->gs || !text || !cfg)
        return;

    if (!ctx->img_pixels)
        return;

    int max_chars = cfg->max_chars;
    int line_height = splash_font_line_height;

    uint8_t r = (rgb >> 16) & 0xFF;
    uint8_t g = (rgb >> 8) & 0xFF;
    uint8_t b = rgb & 0xFF;

    int start_x = cfg->start_x;
    int start_y = cfg->start_y;
    if (cfg->coords_are_image) {
        // Already in image space.
    } else if (ctx->img_scale > 0.0f) {
        start_x = (int)((start_x - ctx->img_off_x) / ctx->img_scale);
        start_y = (int)((start_y - ctx->img_off_y) / ctx->img_scale);
    }

    int cursor_x = start_x;
    int cursor_y = start_y + splash_font_baseline;
    int line_chars = 0;

    const char *p = text;
    while (*p) {
        char c = *p++;
        if (c == '\r')
            continue;
        if (c == '\n') {
            cursor_x = start_x;
            cursor_y += line_height;
            line_chars = 0;
            continue;
        }
        if (max_chars > 0 && line_chars >= max_chars)
            continue;

        const SplashGlyph *glyph = get_splash_glyph(c);
        if (glyph->w > 0 && glyph->h > 0) {
            int gx = cursor_x + glyph->xoff;
            int gy = cursor_y + glyph->yoff;
            int px, py;
            for (py = 0; py < glyph->h; py++) {
                int dy = gy + py;
                if (dy < 0 || dy >= ctx->img_h)
                    continue;
                for (px = 0; px < glyph->w; px++) {
                    int dx = gx + px;
                    if (dx < 0 || dx >= ctx->img_w)
                        continue;
                    int atlas_x = glyph->x + px;
                    int atlas_y = glyph->y + py;
                    size_t atlas_idx = ((size_t)atlas_y * (size_t)splash_font_atlas_w + (size_t)atlas_x) * 4;
                    uint8_t a = splash_font_rgba[atlas_idx + 3];
                    if (a == 0)
                        continue;
                    size_t dst_idx = ((size_t)dy * (size_t)ctx->img_w + (size_t)dx) * 4;
                    uint8_t *dst = (uint8_t *)ctx->img_pixels + dst_idx;
                    if (a == 255) {
                        dst[0] = r;
                        dst[1] = g;
                        dst[2] = b;
                        dst[3] = 0xFF;
                    } else {
                        uint8_t inv = (uint8_t)(255 - a);
                        dst[0] = (uint8_t)((r * a + dst[0] * inv + 127) / 255);
                        dst[1] = (uint8_t)((g * a + dst[1] * inv + 127) / 255);
                        dst[2] = (uint8_t)((b * a + dst[2] * inv + 127) / 255);
                        dst[3] = 0xFF;
                    }
                }
            }
        }
        cursor_x += glyph->xadv;
        line_chars++;
    }
    ctx->needs_present = 1;
}
