#include "splash.h"
#include "splash_assets.h"
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

int SplashBegin(SplashContext *ctx)
{
    if (!ctx)
        return -1;

    memset(ctx, 0, sizeof(*ctx));
    GSGLOBAL *gs = gsKit_init_global();
    if (!gs)
        return -1;

    gs->DoubleBuffering = GS_SETTING_ON;
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
    return 0;
}

void SplashEnd(SplashContext *ctx)
{
    if (!ctx || !ctx->gs)
        return;
    GSGLOBAL *gs = (GSGLOBAL *)ctx->gs;
    gsKit_queue_exec(gs);
    gsKit_finish();
    gsKit_sync_flip(gs);
    gsKit_deinit_global(gs);
    ctx->gs = NULL;
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

    u32 *decoded = NULL;
    unsigned int w = 0, h = 0;
    if (decode_png_rgba(image->data, image->size, &decoded, &w, &h) != 0)
        return -1;

    GSGLOBAL *gs = (GSGLOBAL *)ctx->gs;
    GSTEXTURE tex;
    memset(&tex, 0, sizeof(tex));
    tex.Width = w;
    tex.Height = h;
    tex.PSM = GS_PSM_CT32;
    tex.Mem = decoded;
    tex.Filter = GS_FILTER_LINEAR;
    tex.Vram = gsKit_vram_alloc(gs, gsKit_texture_size(w, h, tex.PSM), GSKIT_ALLOC_USERBUFFER);
    if (tex.Vram == 0) {
        free(decoded);
        return -1;
    }
    gsKit_texture_upload(gs, &tex);

    int screen_w = gs->Width;
    int screen_h = gs->Height;
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

    tex.Filter = (scale < 1.0f) ? GS_FILTER_LINEAR : GS_FILTER_NEAREST;
    gsKit_prim_sprite_texture(gs,
                              &tex,
                              x,
                              y,
                              0.0f,
                              0.0f,
                              x + draw_w,
                              y + draw_h,
                              w,
                              h,
                              0,
                              GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));

    ctx->img_w = (int)w;
    ctx->img_h = (int)h;
    ctx->img_scale = scale;
    ctx->img_off_x = x;
    ctx->img_off_y = y;

    free(decoded);
    return 0;
}

void SplashDrawText(SplashContext *ctx, const char *text, const SplashTextConfig *cfg, uint32_t rgb)
{
    (void)ctx;
    (void)text;
    (void)cfg;
    (void)rgb;
}
