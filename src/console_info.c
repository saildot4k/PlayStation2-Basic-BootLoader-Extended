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

typedef enum
{
    MASS_TRANSPORT_UNKNOWN = 0,
    MASS_TRANSPORT_USB,
    MASS_TRANSPORT_MX4SIO,
    MASS_TRANSPORT_ATA,
    MASS_TRANSPORT_OTHER,
    MASS_TRANSPORT_MIXED
} MassTransportKind;

static MassTransportKind mass_transport_kind_from_driver_tag(const char *driver_tag)
{
    if (driver_tag == NULL || *driver_tag == '\0')
        return MASS_TRANSPORT_UNKNOWN;
    if (ci_starts_with(driver_tag, "sdc"))
        return MASS_TRANSPORT_MX4SIO;
    if (ci_starts_with(driver_tag, "usb"))
        return MASS_TRANSPORT_USB;
    if (ci_starts_with(driver_tag, "ata"))
        return MASS_TRANSPORT_ATA;
    return MASS_TRANSPORT_OTHER;
}

static int format_mass_transport_label(char *out, size_t out_size, MassTransportKind kind, int cwd_suffix)
{
    if (out == NULL || out_size == 0)
        return 0;

    switch (kind) {
        case MASS_TRANSPORT_USB:
            snprintf(out, out_size, cwd_suffix ? "USB CWD" : "USB");
            return 1;
        case MASS_TRANSPORT_MX4SIO:
            snprintf(out, out_size, cwd_suffix ? "MX4SIO CWD" : "MX4SIO");
            return 1;
        case MASS_TRANSPORT_ATA:
            snprintf(out, out_size, cwd_suffix ? "ATA CWD" : "ATA");
            return 1;
        default:
            break;
    }

    return 0;
}

#ifdef FILEXIO
static MassTransportKind probe_runtime_mass_transport_kind(void)
{
    char mass_path[] = "mass0:";
    int i;
    int seen_usb = 0;
    int seen_mx4 = 0;
    int seen_ata = 0;
    int seen_other = 0;

    for (i = 0; i < 10; i++) {
        int dd;
        char driver_tag[16];
        MassTransportKind kind;

        mass_path[4] = (char)('0' + i);
        dd = fileXioDopen(mass_path);
        if (dd < 0)
            continue;

        memset(driver_tag, 0, sizeof(driver_tag));
        fileXioIoctl(dd, USBMASS_IOCTL_GET_DRIVERNAME, driver_tag);
        fileXioDclose(dd);

        kind = mass_transport_kind_from_driver_tag(driver_tag);
        switch (kind) {
            case MASS_TRANSPORT_MX4SIO:
                seen_mx4 = 1;
                break;
            case MASS_TRANSPORT_USB:
                seen_usb = 1;
                break;
            case MASS_TRANSPORT_ATA:
                seen_ata = 1;
                break;
            case MASS_TRANSPORT_OTHER:
                seen_other = 1;
                break;
            default:
                break;
        }
    }

    if (seen_mx4 + seen_usb + seen_ata + seen_other > 1)
        return MASS_TRANSPORT_MIXED;
    if (seen_mx4)
        return MASS_TRANSPORT_MX4SIO;
    if (seen_usb)
        return MASS_TRANSPORT_USB;
    if (seen_ata)
        return MASS_TRANSPORT_ATA;
    if (seen_other)
        return MASS_TRANSPORT_OTHER;

    return MASS_TRANSPORT_UNKNOWN;
}
#endif

static MassTransportKind boot_mass_transport_kind(const char *boot_hint)
{
    MassTransportKind tag_kind = mass_transport_kind_from_driver_tag(LoaderGetBootDriverTag());

    if (tag_kind != MASS_TRANSPORT_UNKNOWN)
        return tag_kind;

    if (boot_hint == NULL || !ci_starts_with(boot_hint, "mass"))
        return MASS_TRANSPORT_UNKNOWN;

#ifdef FILEXIO
    return probe_runtime_mass_transport_kind();
#endif

    return MASS_TRANSPORT_UNKNOWN;
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
    MassTransportKind boot_mass_kind = boot_mass_transport_kind(boot_hint);

    if (out == NULL || out_size == 0)
        return 0;

    if (mass_unit < 0)
        mass_unit = parse_mass_unit_from_path(boot_hint);

    if (mass_unit >= 0) {
        if (format_mass_transport_label(out, out_size, boot_mass_kind, 0))
            return 1;
        snprintf(out, out_size, "MASS%d", mass_unit);
        return 1;
    }

    if (format_mass_transport_label(out, out_size, boot_mass_kind, 0))
        return 1;

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
    int mass_unit_from_hint;
    MassTransportKind boot_mass_kind = boot_mass_transport_kind(boot_hint);

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

    if (boot_hint != NULL && *boot_hint != '\0') {
        mass_unit_from_hint = parse_mass_unit_from_path(boot_hint);

        if (path_is_disc_root(boot_hint)) {
            snprintf(out, out_size, "CDROM");
            return;
        }
        if (ci_starts_with(boot_hint, "usb")) {
            snprintf(out, out_size, "USB");
            return;
        }
        if (ci_starts_with(boot_hint, "mass")) {
            if (format_mass_transport_label(out, out_size, boot_mass_kind, 0))
                return;
            if (mass_unit_from_hint >= 0)
                snprintf(out, out_size, "MASS%d", mass_unit_from_hint);
            else
                snprintf(out, out_size, "MASS");
            return;
        }
        if (ci_starts_with(boot_hint, "mx4sio") || ci_starts_with(boot_hint, "massx")) {
            snprintf(out, out_size, "MX4SIO");
            return;
        }
        if (ci_starts_with(boot_hint, "mmce")) {
            mmce_slot = parse_mmce_slot_from_path(boot_hint);
            if (mmce_slot >= 0)
                snprintf(out, out_size, "MMCE%d", mmce_slot);
            else
                snprintf(out, out_size, "MMCE");
            return;
        }
        if (ci_starts_with(boot_hint, "mc0")) {
            snprintf(out, out_size, "MC0");
            return;
        }
        if (ci_starts_with(boot_hint, "mc1")) {
            snprintf(out, out_size, "MC1");
            return;
        }
        if (ci_starts_with(boot_hint, "hdd0")) {
            snprintf(out, out_size, "HDD0");
            return;
        }
        if (ci_starts_with(boot_hint, "ata")) {
            snprintf(out, out_size, "ATA");
            return;
        }
        if (ci_starts_with(boot_hint, "ilink")) {
            snprintf(out, out_size, "ILINK");
            return;
        }
        if (ci_starts_with(boot_hint, "xfrom")) {
            snprintf(out, out_size, "XFROM");
            return;
        }
        if (ci_starts_with(boot_hint, "host")) {
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
    const char *requested_path = LoaderGetRequestedConfigPath();
    const char *boot_cwd_path = LoaderGetBootCwdConfigPath();
    int boot_source_hint = LoaderGetBootConfigSourceHint();
    int mass_unit = parse_mass_unit_from_path(resolved_path);
    int mmce_slot = parse_mmce_slot_from_path(resolved_path);
    MassTransportKind boot_mass_kind = boot_mass_transport_kind(boot_hint);

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
            if (format_mass_transport_label(out, out_size, boot_mass_kind, 1))
                return;
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
            if (format_mass_transport_label(out, out_size, boot_mass_kind, 1))
                return;
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

    if (boot_cwd_path != NULL && *boot_cwd_path != '\0') {
        if (ci_starts_with(boot_cwd_path, "mmce0")) {
            snprintf(out, out_size, "MMCE0 CWD");
            return;
        }
        if (ci_starts_with(boot_cwd_path, "mmce1")) {
            snprintf(out, out_size, "MMCE1 CWD");
            return;
        }
        if (ci_starts_with(boot_cwd_path, "mx4sio") || ci_starts_with(boot_cwd_path, "massx")) {
            snprintf(out, out_size, "MX4SIO CWD");
            return;
        }
        if (ci_starts_with(boot_cwd_path, "mass")) {
            int boot_cwd_mass_unit = parse_mass_unit_from_path(boot_cwd_path);

            if (format_mass_transport_label(out, out_size, boot_mass_kind, 1))
                return;
            if (boot_cwd_mass_unit >= 0)
                snprintf(out, out_size, "MASS%d CWD", boot_cwd_mass_unit);
            else
                snprintf(out, out_size, "MASS CWD");
            return;
        }
        if (ci_starts_with(boot_cwd_path, "usb")) {
            snprintf(out, out_size, "USB CWD");
            return;
        }
        if (ci_starts_with(boot_cwd_path, "mc0")) {
            snprintf(out, out_size, "MC0 CWD");
            return;
        }
        if (ci_starts_with(boot_cwd_path, "mc1")) {
            snprintf(out, out_size, "MC1 CWD");
            return;
        }
        if (ci_starts_with(boot_cwd_path, "hdd0")) {
            snprintf(out, out_size, "HDD0 CWD");
            return;
        }
        if (ci_starts_with(boot_cwd_path, "ata")) {
            snprintf(out, out_size, "ATA CWD");
            return;
        }
        if (ci_starts_with(boot_cwd_path, "ilink")) {
            snprintf(out, out_size, "ILINK CWD");
            return;
        }
        if (ci_starts_with(boot_cwd_path, "xfrom")) {
            snprintf(out, out_size, "XFROM CWD");
            return;
        }
    }

    if (requested_path != NULL && *requested_path != '\0') {
        if (ci_starts_with(requested_path, "mmce0")) {
            snprintf(out, out_size, "MMCE0 CWD");
            return;
        }
        if (ci_starts_with(requested_path, "mmce1")) {
            snprintf(out, out_size, "MMCE1 CWD");
            return;
        }
        if (ci_starts_with(requested_path, "mx4sio") || ci_starts_with(requested_path, "massx")) {
            snprintf(out, out_size, "MX4SIO CWD");
            return;
        }
        if (ci_starts_with(requested_path, "mass")) {
            int requested_mass_unit = parse_mass_unit_from_path(requested_path);

            if (format_mass_transport_label(out, out_size, boot_mass_kind, 1))
                return;
            if (requested_mass_unit >= 0)
                snprintf(out, out_size, "MASS%d CWD", requested_mass_unit);
            else
                snprintf(out, out_size, "MASS CWD");
            return;
        }
        if (ci_starts_with(requested_path, "usb")) {
            snprintf(out, out_size, "USB CWD");
            return;
        }
        if (ci_starts_with(requested_path, "mc0")) {
            snprintf(out, out_size, "MC0 CWD");
            return;
        }
        if (ci_starts_with(requested_path, "mc1")) {
            snprintf(out, out_size, "MC1 CWD");
            return;
        }
        if (ci_starts_with(requested_path, "hdd0")) {
            snprintf(out, out_size, "HDD0 CWD");
            return;
        }
        if (ci_starts_with(requested_path, "ata")) {
            snprintf(out, out_size, "ATA CWD");
            return;
        }
        if (ci_starts_with(requested_path, "ilink")) {
            snprintf(out, out_size, "ILINK CWD");
            return;
        }
        if (ci_starts_with(requested_path, "xfrom")) {
            snprintf(out, out_size, "XFROM CWD");
            return;
        }
    }

    switch (boot_source_hint) {
        case SOURCE_MC0:
            snprintf(out, out_size, "MC0 CWD");
            return;
        case SOURCE_MC1:
            snprintf(out, out_size, "MC1 CWD");
            return;
        case SOURCE_MASS:
            if (format_mass_transport_label(out, out_size, boot_mass_kind, 1))
                return;
            snprintf(out, out_size, "MASS CWD");
            return;
#ifdef MX4SIO
        case SOURCE_MX4SIO:
            snprintf(out, out_size, "MX4SIO CWD");
            return;
#endif
#ifdef HDD
        case SOURCE_HDD:
            snprintf(out, out_size, "HDD0 CWD");
            return;
#endif
#ifdef XFROM
        case SOURCE_XFROM:
            snprintf(out, out_size, "XFROM CWD");
            return;
#endif
#ifdef MMCE
        case SOURCE_MMCE0:
            snprintf(out, out_size, "MMCE0 CWD");
            return;
        case SOURCE_MMCE1:
            snprintf(out, out_size, "MMCE1 CWD");
            return;
#endif
        default:
            break;
    }

    snprintf(out, out_size, "BOOT CWD");
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
    if (!config_is_boot_cwd &&
        requested_config_path != NULL &&
        ci_eq(requested_config_path, "CONFIG.INI")) {
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
