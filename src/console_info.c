// Console metadata cache and formatting (model, ROM, DVD/PS1 versions, temperature).
#include "console_info.h"
#include "main.h"

#define MODEL_NAME_MAX_LEN 17

static char g_model_name[MODEL_NAME_MAX_LEN];

extern char ConsoleROMVER[];

static int parse_mass_unit_from_path(const char *path)
{
    if (path == NULL || !ci_starts_with(path, "mass"))
        return -1;
    if (path[4] >= '0' && path[4] <= '9' && path[5] == ':')
        return path[4] - '0';
    return -1;
}

static int parse_mmce_slot_from_path(const char *path)
{
    if (path == NULL || !ci_starts_with(path, "mmce"))
        return -1;
    if (path[4] >= '0' && path[4] <= '1' && path[5] == ':')
        return path[4] - '0';
    return -1;
}

static int parse_mass_alias(const char *path, const char **suffix_out, int *unit_out)
{
    const char *suffix = NULL;
    int unit = -1;

    if (path == NULL || !ci_starts_with(path, "mass"))
        return 0;

    if (path[4] == ':') {
        suffix = path + 4;
        unit = -1;
    } else if (path[4] >= '0' && path[4] <= '9' && path[5] == ':') {
        suffix = path + 5;
        unit = path[4] - '0';
    } else {
        return 0;
    }

    if (suffix == NULL || suffix[0] != ':')
        return 0;

    if (suffix_out != NULL)
        *suffix_out = suffix;
    if (unit_out != NULL)
        *unit_out = unit;
    return 1;
}

static int mass_paths_equivalent_for_cwd(const char *a, const char *b)
{
    const char *suffix_a = NULL;
    const char *suffix_b = NULL;
    int unit_a = -1;
    int unit_b = -1;

    if (!parse_mass_alias(a, &suffix_a, &unit_a) ||
        !parse_mass_alias(b, &suffix_b, &unit_b))
        return 0;
    if (!ci_eq(suffix_a, suffix_b))
        return 0;

    // Explicit units must match. A generic legacy "mass:" alias is treated as
    // equivalent to any specific massN path with the same suffix.
    if (unit_a >= 0 && unit_b >= 0 && unit_a != unit_b)
        return 0;

    return 1;
}

static int path_equivalent_for_cwd_label(const char *path, const char *boot_cwd_path)
{
    if (path == NULL || boot_cwd_path == NULL || *path == '\0' || *boot_cwd_path == '\0')
        return 0;
    if (ci_eq(path, boot_cwd_path))
        return 1;
    if (mass_paths_equivalent_for_cwd(path, boot_cwd_path))
        return 1;

    return 0;
}

static int format_mass_device_name(char *out,
                                   size_t out_size,
                                   const char *resolved_path,
                                   const char *boot_hint)
{
    int mass_unit = parse_mass_unit_from_path(resolved_path);

    if (out == NULL || out_size == 0)
        return 0;

    if (mass_unit < 0)
        mass_unit = parse_mass_unit_from_path(boot_hint);

    if (mass_unit >= 0) {
        snprintf(out, out_size, "MASS%d", mass_unit);
        return 1;
    }

    if (resolved_path != NULL && *resolved_path != '\0') {
        if (ci_starts_with(resolved_path, "ata")) {
            snprintf(out, out_size, "ATA");
            return 1;
        }
        if (ci_starts_with(resolved_path, "ilink")) {
            snprintf(out, out_size, "ILINK");
            return 1;
        }
        if (ci_starts_with(resolved_path, "mx4sio")) {
            snprintf(out, out_size, "MX4SIO");
            return 1;
        }
        if (ci_starts_with(resolved_path, "usb")) {
            snprintf(out, out_size, "USB");
            return 1;
        }
    }

    return 0;
}

static void format_device_source_name(char *out,
                                      size_t out_size,
                                      int config_source,
                                      const char *resolved_path,
                                      const char *boot_hint)
{
    int mmce_slot;

    if (out == NULL || out_size == 0)
        return;
    out[0] = '\0';

    if (format_mass_device_name(out, out_size, resolved_path, boot_hint))
        return;

    mmce_slot = parse_mmce_slot_from_path(resolved_path);
    if (mmce_slot >= 0) {
        snprintf(out, out_size, "MMCE%d", mmce_slot);
        return;
    }

    if (resolved_path != NULL && *resolved_path != '\0') {
        if (path_is_disc_root(resolved_path)) {
            snprintf(out, out_size, "CDROM");
            return;
        }
        if (ci_starts_with(resolved_path, "mc0")) {
            snprintf(out, out_size, "MC0");
            return;
        }
        if (ci_starts_with(resolved_path, "mc1")) {
            snprintf(out, out_size, "MC1");
            return;
        }
        if (ci_starts_with(resolved_path, "hdd0")) {
            snprintf(out, out_size, "HDD0");
            return;
        }
        if (ci_starts_with(resolved_path, "xfrom")) {
            snprintf(out, out_size, "XFROM");
            return;
        }
        if (ci_starts_with(resolved_path, "host")) {
            snprintf(out, out_size, "HOST");
            return;
        }
    }

    if (config_source >= SOURCE_MC0 && config_source < SOURCE_COUNT && SOURCES[config_source] != NULL)
        snprintf(out, out_size, "%s", SOURCES[config_source]);
    else
        snprintf(out, out_size, "UNKNOWN");
}

static void format_cwd_source_name(char *out, size_t out_size)
{
    const char *boot_hint = LoaderGetBootPathHint();
    const char *resolved_path = LoaderGetResolvedConfigPath();
    int mass_unit = parse_mass_unit_from_path(resolved_path);
    int mmce_slot = parse_mmce_slot_from_path(resolved_path);

    if (out == NULL || out_size == 0)
        return;

    if (mass_unit < 0)
        mass_unit = parse_mass_unit_from_path(boot_hint);
    if (mmce_slot < 0)
        mmce_slot = parse_mmce_slot_from_path(boot_hint);

    if (boot_hint != NULL && *boot_hint != '\0') {
        if (path_is_disc_root(boot_hint)) {
            snprintf(out, out_size, "CDROM");
            return;
        }
        if (ci_starts_with(boot_hint, "usb")) {
            snprintf(out, out_size, "USB CWD");
            return;
        }
        if (ci_starts_with(boot_hint, "mass")) {
            if (mass_unit >= 0)
                snprintf(out, out_size, "MASS%d CWD", mass_unit);
            else
                snprintf(out, out_size, "MASS CWD");
            return;
        }
        if (ci_starts_with(boot_hint, "mx4sio") || ci_starts_with(boot_hint, "massx")) {
            snprintf(out, out_size, "MX4SIO CWD");
            return;
        }
        if (ci_starts_with(boot_hint, "mmce")) {
            if (mmce_slot >= 0)
                snprintf(out, out_size, "MMCE%d CWD", mmce_slot);
            else
                snprintf(out, out_size, "MMCE CWD");
            return;
        }
        if (ci_starts_with(boot_hint, "hdd0")) {
            snprintf(out, out_size, "HDD0 CWD");
            return;
        }
        if (ci_starts_with(boot_hint, "ata")) {
            snprintf(out, out_size, "ATA CWD");
            return;
        }
        if (ci_starts_with(boot_hint, "ilink")) {
            snprintf(out, out_size, "ILINK CWD");
            return;
        }
        if (ci_starts_with(boot_hint, "xfrom")) {
            snprintf(out, out_size, "XFROM CWD");
            return;
        }
    }

    if (resolved_path != NULL && *resolved_path != '\0') {
        if (path_is_disc_root(resolved_path)) {
            snprintf(out, out_size, "CDROM");
            return;
        }
        if (ci_starts_with(resolved_path, "usb")) {
            snprintf(out, out_size, "USB CWD");
            return;
        }
        if (mass_unit >= 0) {
            snprintf(out, out_size, "MASS%d CWD", mass_unit);
            return;
        }
        if (ci_starts_with(resolved_path, "mx4sio")) {
            snprintf(out, out_size, "MX4SIO CWD");
            return;
        }
        if (mmce_slot >= 0) {
            snprintf(out, out_size, "MMCE%d CWD", mmce_slot);
            return;
        }
        if (ci_starts_with(resolved_path, "hdd0")) {
            snprintf(out, out_size, "HDD0 CWD");
            return;
        }
        if (ci_starts_with(resolved_path, "ata")) {
            snprintf(out, out_size, "ATA CWD");
            return;
        }
    }

    snprintf(out, out_size, "CWD");
}

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
    char source_buf[32];
    const char *requested_config_path = LoaderGetRequestedConfigPath();
    const char *resolved_config_path = LoaderGetResolvedConfigPath();
    const char *boot_hint = LoaderGetBootPathHint();
    const char *boot_cwd_config_path = LoaderGetBootCwdConfigPath();
    int config_is_boot_cwd = 0;

    if (info == NULL)
        return;

    if (g_model_name[0] == '\0')
        ConsoleInfoInit();

    memset(info, 0, sizeof(*info));

    strip_crlf_copy(g_model_name, info->model, sizeof(info->model));
    strip_crlf_copy(PS1DRVGetVersion(), info->ps1ver, sizeof(info->ps1ver));
    strip_crlf_copy(DVDPlayerGetVersion(), info->dvdver, sizeof(info->dvdver));

    if (path_equivalent_for_cwd_label(resolved_config_path, boot_cwd_config_path)) {
        config_is_boot_cwd = 1;
    }
    if (!config_is_boot_cwd &&
        (resolved_config_path == NULL || *resolved_config_path == '\0') &&
        path_equivalent_for_cwd_label(requested_config_path, boot_cwd_config_path)) {
        config_is_boot_cwd = 1;
    }

    if (config_is_boot_cwd) {
        format_cwd_source_name(source_buf, sizeof(source_buf));
        source_name = source_buf;
    } else if (config_source == SOURCE_CWD) {
        format_device_source_name(source_buf,
                                  sizeof(source_buf),
                                  config_source,
                                  resolved_config_path,
                                  boot_hint);
        source_name = source_buf;
    } else if (config_source == SOURCE_MASS) {
        format_device_source_name(source_buf,
                                  sizeof(source_buf),
                                  config_source,
                                  resolved_config_path,
                                  boot_hint);
        source_name = source_buf;
    } else if (config_source >= SOURCE_MC0 && config_source < SOURCE_COUNT && SOURCES[config_source] != NULL) {
        source_name = SOURCES[config_source];
    }
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
