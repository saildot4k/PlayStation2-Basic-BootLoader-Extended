// Video mode parsing/application and VIDEO_MODE config persistence helpers.
#include <stdint.h>

#include "main.h"
#include "loader_video.h"

extern int g_native_video_mode;

enum {
    GS_VIDEO_MODE_NTSC = 2,
    GS_VIDEO_MODE_PAL = 3,
    GS_VIDEO_MODE_480P = 0x50
};

int LoaderParseVideoModeValue(const char *value, int *out_mode)
{
    int parsed_mode;

    if (value == NULL)
        return 0;

    if (ci_eq(value, "AUTO"))
        parsed_mode = CFG_VIDEO_MODE_AUTO;
    else if (ci_eq(value, "NTSC"))
        parsed_mode = CFG_VIDEO_MODE_NTSC;
    else if (ci_eq(value, "PAL"))
        parsed_mode = CFG_VIDEO_MODE_PAL;
    else if (ci_eq(value, "480P"))
        parsed_mode = CFG_VIDEO_MODE_480P;
    else
        return 0;

    if (out_mode != NULL)
        *out_mode = parsed_mode;

    return 1;
}

int LoaderDetectNativeVideoMode(void)
{
    return (OSDGetVideoMode() != 0) ? CFG_VIDEO_MODE_PAL : CFG_VIDEO_MODE_NTSC;
}

void LoaderApplyVideoMode(int cfg_mode)
{
    int effective_mode = cfg_mode;
    short interlace = 1;
    short ffmd = 1;
    short gs_mode = GS_VIDEO_MODE_NTSC;

    if (effective_mode == CFG_VIDEO_MODE_AUTO)
        effective_mode = g_native_video_mode;

    switch (effective_mode) {
        case CFG_VIDEO_MODE_PAL:
            gs_mode = GS_VIDEO_MODE_PAL;
            break;
        case CFG_VIDEO_MODE_480P:
            interlace = 0;
            gs_mode = GS_VIDEO_MODE_480P;
            break;
        case CFG_VIDEO_MODE_NTSC:
        default:
            gs_mode = GS_VIDEO_MODE_NTSC;
            break;
    }

    SetGsCrt(interlace, gs_mode, ffmd);
}

int LoaderResolveEffectiveVideoMode(int cfg_mode)
{
    if (cfg_mode == CFG_VIDEO_MODE_AUTO)
        return g_native_video_mode;

    switch (cfg_mode) {
        case CFG_VIDEO_MODE_PAL:
            return CFG_VIDEO_MODE_PAL;
        case CFG_VIDEO_MODE_480P:
            return CFG_VIDEO_MODE_480P;
        case CFG_VIDEO_MODE_NTSC:
        default:
            return CFG_VIDEO_MODE_NTSC;
    }
}

const char *LoaderVideoModeLabel(int cfg_mode)
{
    switch (cfg_mode) {
        case CFG_VIDEO_MODE_NTSC:
            return "NTSC";
        case CFG_VIDEO_MODE_PAL:
            return "PAL";
        case CFG_VIDEO_MODE_480P:
            return "480P";
        case CFG_VIDEO_MODE_AUTO:
        default:
            return "AUTO";
    }
}

int LoaderStepVideoMode(int current_mode, int direction)
{
    static const int mode_order[] = {
        CFG_VIDEO_MODE_AUTO,
        CFG_VIDEO_MODE_NTSC,
        CFG_VIDEO_MODE_PAL,
        CFG_VIDEO_MODE_480P
    };
    int i;
    int index = 0;
    int count = (int)(sizeof(mode_order) / sizeof(mode_order[0]));

    for (i = 0; i < count; i++) {
        if (mode_order[i] == current_mode) {
            index = i;
            break;
        }
    }

    if (direction > 0)
        index = (index + 1) % count;
    else
        index = (index + count - 1) % count;

    return mode_order[index];
}

static int is_space_or_tab(char c)
{
    return (c == ' ' || c == '\t');
}

static int classify_video_mode_config_line(const char *line, size_t len)
{
    size_t i = 0;

    while (i < len && is_space_or_tab(line[i]))
        i++;
    if (i >= len)
        return 0;

    if (line[i] == '#' || line[i] == ';')
        return 0;

    if (!ci_starts_with_n(line + i, len - i, "VIDEO_MODE"))
        return 0;
    i += strlen("VIDEO_MODE");
    while (i < len && is_space_or_tab(line[i]))
        i++;
    if (i < len && line[i] == '=')
        return 1;

    return 0;
}

int LoaderSaveVideoModeToConfigFile(int cfg_mode,
                                    int config_source,
                                    char *config_path_in_use,
                                    size_t config_path_in_use_size,
                                    char *saved_path_out,
                                    size_t saved_path_out_size)
{
    FILE *fp = NULL;
    char *in_buf = NULL;
    char *out_buf = NULL;
    char mode_line[32];
    char *resolved_path;
    const char *path = "";
    size_t in_size;
    size_t out_cap;
    size_t out_size;
    long file_size;
    int use_crlf = 0;
    int replaced_video_mode_line = 0;
    int success = 0;
    const char *cursor;
    const char *end;
    char *out;

    if (saved_path_out != NULL && saved_path_out_size > 0)
        saved_path_out[0] = '\0';

    if (config_path_in_use != NULL)
        path = config_path_in_use;

    if ((path == NULL || path[0] == '\0') && !(config_source >= SOURCE_MC0 && config_source < SOURCE_COUNT))
        return 0;

    snprintf(mode_line, sizeof(mode_line), "VIDEO_MODE = %s", LoaderVideoModeLabel(cfg_mode));

    fp = fopen(path, "rb");
    if (fp == NULL && config_source >= SOURCE_MC0 && config_source < SOURCE_COUNT &&
        config_path_in_use != NULL && config_path_in_use_size > 0) {
        resolved_path = CheckPath(CONFIG_PATHS[config_source]);
        if (resolved_path != NULL && *resolved_path != '\0') {
            snprintf(config_path_in_use, config_path_in_use_size, "%s", resolved_path);
            path = config_path_in_use;
            fp = fopen(path, "rb");
        }
    }
    if (fp == NULL)
        goto cleanup;

    if (fseek(fp, 0, SEEK_END) != 0)
        goto cleanup;
    file_size = ftell(fp);
    if (file_size < 0)
        goto cleanup;
    if (fseek(fp, 0, SEEK_SET) != 0)
        goto cleanup;

    in_size = (size_t)file_size;
    in_buf = (char *)malloc(in_size + 1);
    if (in_buf == NULL)
        goto cleanup;
    if (in_size > 0 && fread(in_buf, 1, in_size, fp) != in_size)
        goto cleanup;
    fclose(fp);
    fp = NULL;
    in_buf[in_size] = '\0';

    {
        size_t i;

        for (i = 0; i + 1 < in_size; i++) {
            if (in_buf[i] == '\r' && in_buf[i + 1] == '\n') {
                use_crlf = 1;
                break;
            }
        }
    }

    out_cap = (in_size * 2) + 128;
    out_buf = (char *)malloc(out_cap);
    if (out_buf == NULL)
        goto cleanup;

    out = out_buf;
    cursor = in_buf;
    end = in_buf + in_size;
    while (cursor < end) {
        const char *line_end = cursor;
        const char *newline_ptr = NULL;
        size_t line_content_len;
        size_t newline_len = 0;
        int kind;

        while (line_end < end && *line_end != '\n')
            line_end++;

        if (line_end < end) {
            if (line_end > cursor && line_end[-1] == '\r') {
                line_content_len = (size_t)((line_end - 1) - cursor);
                newline_ptr = line_end - 1;
                newline_len = 2;
            } else {
                line_content_len = (size_t)(line_end - cursor);
                newline_ptr = line_end;
                newline_len = 1;
            }
        } else {
            line_content_len = (size_t)(end - cursor);
        }

        kind = classify_video_mode_config_line(cursor, line_content_len);
        if (kind == 1) {
            size_t mode_line_len = strlen(mode_line);

            memcpy(out, mode_line, mode_line_len);
            out += mode_line_len;

            if (newline_len > 0) {
                memcpy(out, newline_ptr, newline_len);
                out += newline_len;
            }
            replaced_video_mode_line = 1;
        } else {
            size_t original_len = line_content_len + newline_len;

            memcpy(out, cursor, original_len);
            out += original_len;
        }

        cursor = (line_end < end) ? (line_end + 1) : end;
    }

    if (!replaced_video_mode_line) {
        size_t mode_line_len = strlen(mode_line);
        size_t newline_len = use_crlf ? 2 : 1;
        size_t prefix_len = mode_line_len + newline_len;
        size_t existing_len = (size_t)(out - out_buf);

        if (existing_len + prefix_len > out_cap)
            goto cleanup;

        memmove(out_buf + prefix_len, out_buf, existing_len);
        memcpy(out_buf, mode_line, mode_line_len);
        if (use_crlf) {
            out_buf[mode_line_len] = '\r';
            out_buf[mode_line_len + 1] = '\n';
        } else {
            out_buf[mode_line_len] = '\n';
        }
        out = out_buf + prefix_len + existing_len;
    }

    out_size = (size_t)(out - out_buf);
    fp = fopen(path, "wb");
    if (fp == NULL)
        goto cleanup;
    if (out_size > 0 && fwrite(out_buf, 1, out_size, fp) != out_size)
        goto cleanup;

    success = 1;
    if (saved_path_out != NULL && saved_path_out_size > 0)
        snprintf(saved_path_out, saved_path_out_size, "%s", path);

cleanup:
    if (!success && saved_path_out != NULL && saved_path_out_size > 0 && path != NULL && *path != '\0')
        snprintf(saved_path_out, saved_path_out_size, "%s", path);
    if (fp != NULL)
        fclose(fp);
    if (in_buf != NULL)
        free(in_buf);
    if (out_buf != NULL)
        free(out_buf);
    return success;
}
