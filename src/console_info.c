// Console metadata cache and formatting (model, ROM, DVD/PS1 versions, temperature).
#include "console_info.h"
#include "main.h"

#define MODEL_NAME_MAX_LEN 17

static char g_model_name[MODEL_NAME_MAX_LEN];

extern char ConsoleROMVER[];

static int ReadModelName(char *name)
{
    int result;
    u32 stat;

    if (name == NULL)
        return -1;

    memset(name, 0, MODEL_NAME_MAX_LEN);

    if (ConsoleROMVER[0] == '0' && ConsoleROMVER[1] == '1' && ConsoleROMVER[2] == '0') {
        if (ConsoleROMVER[3] == '0')
            strcpy(name, "SCPH-10000");
        else {
            int fd;
            if ((fd = open("rom0:OSDSYS", O_RDONLY)) >= 0) {
                lseek(fd, 0x8C808, SEEK_SET);
                read(fd, name, MODEL_NAME_MAX_LEN);
                close(fd);
            } else
                strcpy(name, "Unknown");
        }
        return 0;
    }

    if ((result = sceCdRM(name, &stat)) == 1) {
        if (stat & 0x80)
            return -2;
        if ((stat & 0x40) || name[0] == '\0')
            strcpy(name, "unknown");
        return 0;
    }

    return -2;
}

int ConsoleInfoInit(void)
{
    return ReadModelName(g_model_name);
}

static void FormatROMVersion(char *out, size_t out_size, const u8 *romver, size_t romver_len)
{
    char rom_raw[ROMVER_MAX_LEN + 1];
    char rom_buf[32];
    const char *romver_str;
    char major = '?';
    char minor1 = '?';
    char minor2 = '?';
    char region = '?';

    if (out == NULL || out_size == 0)
        return;

    out[0] = '\0';

    memset(rom_raw, 0, sizeof(rom_raw));
    if (romver != NULL && romver_len > 0) {
        size_t copy_len = romver_len;
        if (copy_len > ROMVER_MAX_LEN)
            copy_len = ROMVER_MAX_LEN;
        memcpy(rom_raw, romver, copy_len);
    }

    romver_str = strip_crlf_copy(rom_raw, rom_buf, sizeof(rom_buf));
    if (romver_str[1] != '\0')
        major = romver_str[1];
    if (romver_str[2] != '\0')
        minor1 = romver_str[2];
    if (romver_str[3] != '\0')
        minor2 = romver_str[3];
    if (romver_str[4] != '\0')
        region = romver_str[4];

    snprintf(out, out_size, "%c.%c%c%c", major, minor1, minor2, region);
}

void ConsoleInfoCapture(ConsoleInfo *info, int config_source, const u8 *romver, size_t romver_len)
{
    const char *source_name = "";

    if (info == NULL)
        return;

    if (g_model_name[0] == '\0')
        ConsoleInfoInit();

    memset(info, 0, sizeof(*info));

    strip_crlf_copy(g_model_name, info->model, sizeof(info->model));
    strip_crlf_copy(PS1DRVGetVersion(), info->ps1ver, sizeof(info->ps1ver));
    strip_crlf_copy(DVDPlayerGetVersion(), info->dvdver, sizeof(info->dvdver));

    if (config_source >= SOURCE_MC0 && config_source < SOURCE_COUNT && SOURCES[config_source] != NULL)
        source_name = SOURCES[config_source];
    strip_crlf_copy(source_name, info->source, sizeof(info->source));

    FormatROMVersion(info->rom_fmt, sizeof(info->rom_fmt), romver, romver_len);
}

const char *ConsoleInfoRefreshTemperature(ConsoleInfo *info)
{
#ifndef NO_TEMP_DISP
    if (info == NULL)
        return NULL;

    // Query once to discover support, then refresh continuously only on supported consoles.
    if (!info->temp_supported && info->temp_checked)
        return NULL;

    info->temp_checked = 1;
    if (QueryTemperatureCelsius(info->temp_celsius, sizeof(info->temp_celsius))) {
        info->temp_supported = 1;
        return info->temp_celsius;
    }

    if (info->temp_supported)
        return info->temp_celsius;

    return NULL;
#else
    (void)info;
    return NULL;
#endif
}
