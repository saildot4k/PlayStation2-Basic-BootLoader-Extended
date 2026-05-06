// IOP module loading pipeline (core + per-device-family lazy loading).
#include "main.h"
#include "loader_path.h"

// Tracks whether the BDM family stack is available for path probing/launch.
// This is broader than just USB transport readiness.
static int s_bdm_modules_loaded = 0;
static int s_mx4sio_modules_loaded = 0;
static int s_mmce_modules_loaded = 0;
static int s_hdd_modules_loaded = 0;
static int s_bdm_core_loaded = 0;
static int s_bdm_usb_transport_loaded = 0;
static int s_bdm_ata_transport_loaded = 0;

static LoaderPathFamily s_boot_family = LOADER_PATH_FAMILY_MC;
static LoaderPathFamily s_current_family = LOADER_PATH_FAMILY_NONE;
static char s_boot_config_path[64] = "";
static char s_boot_cwd_config_path[256] = "";
static char s_boot_path_hint[256] = "";
static char s_boot_driver_tag[16] = "";
static int s_boot_config_source_hint = SOURCE_INVALID;

#ifdef DEV9
static int dev9_loaded = 0;
#endif
#ifdef FILEXIO
static int s_fio_loaded = 0;
#endif
#ifdef DISC_STOP_AT_BOOT
static int s_disc_boot_cdfs_ready = 0;
#endif
#if defined(PSX)
static int s_xfrom_modules_loaded = 0;
extern int g_is_psx_desr;
#endif

static int starts_with(const char *s, const char *prefix)
{
    if (s == NULL || prefix == NULL)
        return 0;
    return ci_starts_with(s, prefix);
}

typedef enum
{
    BDM_TRANSPORT_SCOPE_BOOT_HINT = 0,
    BDM_TRANSPORT_SCOPE_LAUNCH_ENTRY,
} BdmTransportScope;

static int extract_legacy_mass_unit(const char *path)
{
    if (path == NULL || !ci_starts_with(path, "mass"))
        return -1;

    if (path[4] >= '0' && path[4] <= '9' && path[5] == ':')
        return path[4] - '0';

    return -1;
}

static int extract_optional_unit_after_prefix(const char *path, const char *prefix, size_t prefix_len)
{
    if (path == NULL || prefix == NULL)
        return -2;
    if (!ci_starts_with(path, prefix))
        return -2;

    if (path[prefix_len] == ':')
        return -1;
    if (path[prefix_len] >= '0' &&
        path[prefix_len] <= '9' &&
        path[prefix_len + 1] == ':')
        return (int)(path[prefix_len] - '0');

    return -2;
}

static int build_mass_unit_path(const char *path, int unit, char *out, size_t out_size)
{
    const char *suffix;

    if (path == NULL || out == NULL || out_size == 0 || unit < 0 || unit > 9)
        return 0;
    if (!ci_starts_with(path, "mass"))
        return 0;

    if (path[4] == ':')
        suffix = path + 4;
    else if (path[4] >= '0' && path[4] <= '9' && path[5] == ':')
        suffix = path + 5;
    else
        return 0;

    if (suffix[1] == '/' || suffix[1] == '\0')
        snprintf(out, out_size, "mass%d%s", unit, suffix);
    else
        snprintf(out, out_size, "mass%d:/%s", unit, suffix + 1);
    return 1;
}

static int normalize_mass_path_to_unit_or_generic(const char *path, int unit, char *out, size_t out_size)
{
    const char *suffix;

    if (out == NULL || out_size == 0)
        return 0;

    if (path == NULL || !ci_starts_with(path, "mass"))
        return 0;

    if (build_mass_unit_path(path, unit, out, out_size))
        return 1;

    // Keep generic legacy mass: paths generic when we cannot map to a concrete
    // slot yet (for example, right after loading transport drivers).
    if (path[4] == ':')
        suffix = path + 4;
    else if (path[4] >= '0' && path[4] <= '9' && path[5] == ':')
        suffix = path + 5;
    else
        return 0;

    if (suffix[1] == '/' || suffix[1] == '\0')
        snprintf(out, out_size, "mass%s", suffix);
    else
        snprintf(out, out_size, "mass:/%s", suffix + 1);
    return 1;
}

typedef enum
{
    MASS_CLASS_UNKNOWN = 0,
    MASS_CLASS_USB,
    MASS_CLASS_MX4SIO,
    MASS_CLASS_ATA,
    MASS_CLASS_OTHER
} MassClass;

static MassClass mass_class_from_driver_tag(const char *driver_tag)
{
    if (driver_tag == NULL || driver_tag[0] == '\0')
        return MASS_CLASS_UNKNOWN;
    if (ci_starts_with(driver_tag, "sdc"))
        return MASS_CLASS_MX4SIO;
    if (ci_starts_with(driver_tag, "ata"))
        return MASS_CLASS_ATA;
    if (ci_starts_with(driver_tag, "usb"))
        return MASS_CLASS_USB;
    return MASS_CLASS_OTHER;
}

static const char *mass_class_name(MassClass mass_class)
{
    switch (mass_class) {
        case MASS_CLASS_USB:
            return "usb";
        case MASS_CLASS_MX4SIO:
            return "mx4sio";
        case MASS_CLASS_ATA:
            return "bdm_hdd";
        case MASS_CLASS_OTHER:
            return "other";
        default:
            return "unknown";
    }
}

static int pick_mass_slot_by_preference(const int *slots, const MassClass *classes, int count)
{
    static const MassClass preferred_order[] = {
        MASS_CLASS_USB,
        MASS_CLASS_MX4SIO,
        MASS_CLASS_ATA,
        MASS_CLASS_OTHER,
        MASS_CLASS_UNKNOWN,
    };
    int order_idx;

    if (slots == NULL || classes == NULL || count <= 0)
        return -1;

    for (order_idx = 0; order_idx < (int)(sizeof(preferred_order) / sizeof(preferred_order[0])); order_idx++) {
        MassClass wanted = preferred_order[order_idx];
        int i;

        for (i = 0; i < count; i++) {
            if (classes[i] == wanted)
                return slots[i];
        }
    }

    return slots[0];
}

#ifdef FILEXIO
static int mass_mount_openable(int unit)
{
    char mountpoint[] = "mass0:";
    int dd;

    if (unit < 0 || unit > 9)
        return 0;

    mountpoint[4] = '0' + unit;
    dd = fileXioDopen(mountpoint);
    if (dd < 0)
        return 0;

    fileXioDclose(dd);
    return 1;
}

static void normalize_driver_tag(char *tag)
{
    int i;

    if (tag == NULL)
        return;

    for (i = 0; tag[i] != '\0'; i++) {
        char c = tag[i];

        if (c < 32 || c > 126) {
            tag[i] = '\0';
            break;
        }
        if (c >= 'A' && c <= 'Z')
            tag[i] = c + ('a' - 'A');
    }
}

static int read_mass_driver_tag(int unit, char *tag_out, size_t tag_out_size)
{
    char mountpoint[] = "mass0:";
    char ioctl_tag[16];
    int dd;
    int ioctl_ret;

    if (tag_out == NULL || tag_out_size == 0 || unit < 0 || unit > 9)
        return -1;
    tag_out[0] = '\0';

    mountpoint[4] = '0' + unit;
    dd = fileXioDopen(mountpoint);
    if (dd < 0)
        return -1;

    memset(ioctl_tag, 0, sizeof(ioctl_tag));
    ioctl_ret = fileXioIoctl(dd, USBMASS_IOCTL_GET_DRIVERNAME, ioctl_tag);
    fileXioDclose(dd);

    if (ioctl_tag[0] != '\0') {
        snprintf(tag_out, tag_out_size, "%s", ioctl_tag);
    } else if (ioctl_ret >= 0) {
        char packed_tag[sizeof(int) + 1];

        memcpy(packed_tag, &ioctl_ret, sizeof(int));
        packed_tag[sizeof(int)] = '\0';
        snprintf(tag_out, tag_out_size, "%s", packed_tag);
    } else {
        return -1;
    }

    normalize_driver_tag(tag_out);
    return (tag_out[0] != '\0') ? 0 : -1;
}
#endif

static int resolve_legacy_mass_boot_unit(const char *boot_path)
{
    int unit = extract_legacy_mass_unit(boot_path);
    int i;
    const char *suffix;
    int found_slots[10];
    MassClass found_classes[10];
    int found_count = 0;
#ifdef FILEXIO
    int mounted_slots[10];
    MassClass mounted_classes[10];
    int mounted_count = 0;
#endif

    if (unit >= 0)
        return unit;
    if (boot_path == NULL || !ci_starts_with(boot_path, "mass") || boot_path[4] != ':')
        return -1;

    suffix = boot_path + 4;
    for (i = 0; i < 10; i++) {
        int found = 0;
        MassClass mass_class = MASS_CLASS_UNKNOWN;
#ifdef FILEXIO
        int mounted = 0;
        char driver_tag[16];
#endif
        char candidate[256];

#ifdef FILEXIO
        mounted = mass_mount_openable(i);
        driver_tag[0] = '\0';
        if (mounted && read_mass_driver_tag(i, driver_tag, sizeof(driver_tag)) == 0)
            mass_class = mass_class_from_driver_tag(driver_tag);

        if (mounted) {
            mounted_slots[mounted_count] = i;
            mounted_classes[mounted_count] = mass_class;
            mounted_count++;
        }
#endif

        snprintf(candidate, sizeof(candidate), "mass%d%s", i, suffix);
        if (exist(candidate))
            found = 1;
        if (!found && suffix[1] != '\0' && suffix[1] != '/') {
            snprintf(candidate, sizeof(candidate), "mass%d:/%s", i, suffix + 1);
            if (exist(candidate))
                found = 1;
        }
        if (found) {
#ifdef FILEXIO
            if (mass_class == MASS_CLASS_UNKNOWN) {
                char driver_tag[16];
                driver_tag[0] = '\0';
                if (read_mass_driver_tag(i, driver_tag, sizeof(driver_tag)) == 0)
                    mass_class = mass_class_from_driver_tag(driver_tag);
            }
#endif
            found_slots[found_count] = i;
            found_classes[found_count] = mass_class;
            found_count++;
        }
    }

    if (found_count > 0) {
        int picked = pick_mass_slot_by_preference(found_slots, found_classes, found_count);
        DPRINTF("Boot mass refine candidate: picked mass%d using class order USB->MX4SIO->ATA\n", picked);
        return picked;
    }

#ifdef FILEXIO
    if (mounted_count > 0)
        return pick_mass_slot_by_preference(mounted_slots, mounted_classes, mounted_count);
#endif

    return -1;
}

static const char *classify_mass_driver_tag(const char *driver_tag)
{
    return mass_class_name(mass_class_from_driver_tag(driver_tag));
}

static void normalize_disc_separators(char *path)
{
    char *p;

    if (path == NULL)
        return;

    for (p = path; *p != '\0'; p++) {
        if (*p == '/')
            *p = '\\';
    }
}

static void refine_boot_hint_from_legacy_mass(void)
{
    int mass_unit;
    char resolved[256];
    char boot_config_resolved[sizeof(s_boot_config_path)];

    if (!ci_starts_with(s_boot_path_hint, "mass")) {
        s_boot_driver_tag[0] = '\0';
        return;
    }

    mass_unit = resolve_legacy_mass_boot_unit(s_boot_path_hint);
    if (mass_unit < 0) {
        if (normalize_mass_path_to_unit_or_generic(s_boot_cwd_config_path, -1, resolved, sizeof(resolved)))
            snprintf(s_boot_cwd_config_path, sizeof(s_boot_cwd_config_path), "%s", resolved);

        DPRINTF("Boot mass refine: argv0='%s' unit=<unknown> cwd_cfg='%s'\n",
                s_boot_path_hint,
                (s_boot_cwd_config_path[0] != '\0') ? s_boot_cwd_config_path : "<none>");
        s_boot_driver_tag[0] = '\0';
        return;
    }

    if (build_mass_unit_path(s_boot_path_hint, mass_unit, resolved, sizeof(resolved)))
        snprintf(s_boot_path_hint, sizeof(s_boot_path_hint), "%s", resolved);
    if (normalize_mass_path_to_unit_or_generic(s_boot_cwd_config_path, mass_unit, resolved, sizeof(resolved)))
        snprintf(s_boot_cwd_config_path, sizeof(s_boot_cwd_config_path), "%s", resolved);
    if (build_mass_unit_path(s_boot_config_path,
                             mass_unit,
                             boot_config_resolved,
                             sizeof(boot_config_resolved)))
        memcpy(s_boot_config_path, boot_config_resolved, sizeof(s_boot_config_path));

#ifdef FILEXIO
    if (read_mass_driver_tag(mass_unit, s_boot_driver_tag, sizeof(s_boot_driver_tag)) < 0)
        s_boot_driver_tag[0] = '\0';
#else
    s_boot_driver_tag[0] = '\0';
#endif

    DPRINTF("Boot mass refine: unit=%d tag='%s' class=%s cwd_cfg='%s' family_cfg='%s'\n",
            mass_unit,
            (s_boot_driver_tag[0] != '\0') ? s_boot_driver_tag : "<none>",
            classify_mass_driver_tag(s_boot_driver_tag),
            (s_boot_cwd_config_path[0] != '\0') ? s_boot_cwd_config_path : "<none>",
            (s_boot_config_path[0] != '\0') ? s_boot_config_path : "<none>");
}

static void set_boot_cwd_config_path(const char *boot_path)
{
    const char *slash;
    const char *backslash;
    const char *separator;
    const char *colon;
    const char *cwd_file_name;
    const char *cwd_tail_from_root;
    size_t prefix_len;
    size_t max_prefix_len;
    int use_disc_paths;

    s_boot_cwd_config_path[0] = '\0';

    if (boot_path == NULL || *boot_path == '\0' || boot_path[0] == '$')
        return;

#ifdef DISC_STOP_AT_BOOT
    if (path_is_disc_root(boot_path))
        return;
#endif

    use_disc_paths = path_is_disc_root(boot_path);
    cwd_file_name = use_disc_paths ? "CONFIG.INI;1" : "CONFIG.INI";
    cwd_tail_from_root = use_disc_paths ? "\\CONFIG.INI;1" : "/CONFIG.INI";

    slash = strrchr(boot_path, '/');
    backslash = strrchr(boot_path, '\\');
    separator = slash;
    if (backslash != NULL && (separator == NULL || backslash > separator))
        separator = backslash;

    if (separator != NULL) {
        max_prefix_len = sizeof(s_boot_cwd_config_path) - strlen(cwd_file_name) - 1;
        prefix_len = (size_t)(separator - boot_path + 1);
        if (prefix_len > max_prefix_len)
            prefix_len = max_prefix_len;
        memcpy(s_boot_cwd_config_path, boot_path, prefix_len);
        memcpy(s_boot_cwd_config_path + prefix_len, cwd_file_name, strlen(cwd_file_name) + 1);
        if (use_disc_paths)
            normalize_disc_separators(s_boot_cwd_config_path);
        return;
    }

    colon = strchr(boot_path, ':');
    if (colon != NULL) {
        max_prefix_len = sizeof(s_boot_cwd_config_path) - strlen(cwd_tail_from_root) - 1;
        prefix_len = (size_t)(colon - boot_path + 1);
        if (prefix_len > max_prefix_len)
            prefix_len = max_prefix_len;
        memcpy(s_boot_cwd_config_path, boot_path, prefix_len);
        memcpy(s_boot_cwd_config_path + prefix_len,
               cwd_tail_from_root,
               strlen(cwd_tail_from_root) + 1);
        if (use_disc_paths)
            normalize_disc_separators(s_boot_cwd_config_path);
    }
}

static int source_hint_for_family(LoaderPathFamily family)
{
    switch (family) {
        case LOADER_PATH_FAMILY_BDM:
            return SOURCE_MASS;
#ifdef MX4SIO
        case LOADER_PATH_FAMILY_MX4SIO:
            return SOURCE_MX4SIO;
#endif
#ifdef MMCE
        case LOADER_PATH_FAMILY_MMCE:
            return SOURCE_MMCE0;
#endif
#ifdef HDD
        case LOADER_PATH_FAMILY_HDD_APA:
            return SOURCE_HDD;
#endif
#ifdef XFROM
        case LOADER_PATH_FAMILY_XFROM:
            return SOURCE_XFROM;
#endif
        default:
            break;
    }

    return SOURCE_INVALID;
}

static const char *boot_family_name(LoaderPathFamily family)
{
    switch (family) {
        case LOADER_PATH_FAMILY_NONE:
            return "NONE";
        case LOADER_PATH_FAMILY_MC:
            return "MC";
        case LOADER_PATH_FAMILY_BDM:
            return "BDM";
        case LOADER_PATH_FAMILY_MX4SIO:
            return "MX4SIO";
        case LOADER_PATH_FAMILY_MMCE:
            return "MMCE";
        case LOADER_PATH_FAMILY_HDD_APA:
            return "HDD_APA";
        case LOADER_PATH_FAMILY_XFROM:
            return "XFROM";
        default:
            return "UNKNOWN";
    }
}

static void reset_module_flags(void)
{
    s_bdm_modules_loaded = 0;
    s_mx4sio_modules_loaded = 0;
    s_mmce_modules_loaded = 0;
    s_hdd_modules_loaded = 0;
    s_bdm_core_loaded = 0;
    s_bdm_usb_transport_loaded = 0;
    s_bdm_ata_transport_loaded = 0;
#if defined(PSX)
    s_xfrom_modules_loaded = 0;
#endif
#ifdef DISC_STOP_AT_BOOT
    s_disc_boot_cdfs_ready = 0;
#endif
}

static void publish_module_states(void)
{
    LoaderPathSetModuleStates(s_bdm_modules_loaded,
                              s_mx4sio_modules_loaded,
                              s_mmce_modules_loaded,
                              s_hdd_modules_loaded);
}

static int load_core_modules(void)
{
    int j, x;

#ifdef PPCTTY
    // no error handling bc nothing to do in this case
    SifExecModuleBuffer(ppctty_irx, size_ppctty_irx, 0, NULL, NULL);
#endif
#ifdef UDPTTY
    if (loadDEV9())
        loadUDPTTY();
#endif

#ifdef FILEXIO
    if (LoadFIO() < 0)
        DPRINTF(" [CORE_FIO]: failed to load IOMANX/FILEXIO; will retry when needed\n");
#endif

#ifdef USE_ROM_SIO2MAN
    j = SifLoadStartModule("rom0:SIO2MAN", 0, NULL, &x);
    DPRINTF(" [SIO2MAN]: ID=%d, ret=%d\n", j, x);
#else
    j = SifExecModuleBuffer(sio2man_irx, size_sio2man_irx, 0, NULL, &x);
    DPRINTF(" [SIO2MAN]: ID=%d, ret=%d\n", j, x);
#endif
#ifdef USE_ROM_MCMAN
    j = SifLoadStartModule("rom0:MCMAN", 0, NULL, &x);
    DPRINTF(" [MCMAN]: ID=%d, ret=%d\n", j, x);
    j = SifLoadStartModule("rom0:MCSERV", 0, NULL, &x);
    DPRINTF(" [MCSERV]: ID=%d, ret=%d\n", j, x);
    mcInit(MC_TYPE_MC);
#else
    j = SifExecModuleBuffer(mcman_irx, size_mcman_irx, 0, NULL, &x);
    DPRINTF(" [MCMAN]: ID=%d, ret=%d\n", j, x);
    j = SifExecModuleBuffer(mcserv_irx, size_mcserv_irx, 0, NULL, &x);
    DPRINTF(" [MCSERV]: ID=%d, ret=%d\n", j, x);
    mcInit(MC_TYPE_XMC);
#endif
#ifdef USE_ROM_PADMAN
    j = SifLoadStartModule("rom0:PADMAN", 0, NULL, &x);
    DPRINTF(" [PADMAN]: ID=%d, ret=%d\n", j, x);
#else
    j = SifExecModuleBuffer(padman_irx, size_padman_irx, 0, NULL, &x);
    DPRINTF(" [PADMAN]: ID=%d, ret=%d\n", j, x);
#endif

    j = SifLoadModule("rom0:ADDDRV", 0, NULL); // Load ADDDRV. The OSD has it listed in rom0:OSDCNF/IOPBTCONF, but it is otherwise not loaded automatically.
    DPRINTF(" [ADDDRV]: %d\n", j);

    return 0;
}

static int load_bdm_core_modules(void)
{
    int ID, RET;

#ifdef HAS_EMBEDDED_IRX
    ID = SifExecModuleBuffer(bdm_irx, size_bdm_irx, 0, NULL, &RET);
#else
    ID = SifLoadStartModule(CheckPath("mc?:/PS2BBL/BDM.IRX"), 0, NULL, &RET);
#endif
    DPRINTF(" [BDM]: ret=%d, ID=%d\n", RET, ID);
    if (ID < 0 || RET == 1)
        return -1;

#ifdef HAS_EMBEDDED_IRX
    ID = SifExecModuleBuffer(bdmfs_fatfs_irx, size_bdmfs_fatfs_irx, 0, NULL, &RET);
#else
    ID = SifLoadStartModule(CheckPath("mc?:/PS2BBL/BDMFS_FATFS.IRX"), 0, NULL, &RET);
#endif
    DPRINTF(" [BDMFS_FATFS]: ret=%d, ID=%d\n", RET, ID);
    if (ID < 0 || RET == 1)
        return -2;

    return 0;
}

static int load_usb_transport_modules(void)
{
    int ID, RET;

#ifdef HAS_EMBEDDED_IRX
    ID = SifExecModuleBuffer(usbd_mini_irx, size_usbd_mini_irx, 0, NULL, &RET);
#else
    ID = SifLoadStartModule(CheckPath("mc?:/PS2BBL/USBD.IRX"), 0, NULL, &RET);
#endif
    delay(3);
    DPRINTF(" [USBD]: ret=%d, ID=%d\n", RET, ID);
    if (ID < 0 || RET == 1)
        return -1;

#ifdef HAS_EMBEDDED_IRX
    ID = SifExecModuleBuffer(usbmass_bd_mini_irx, size_usbmass_bd_mini_irx, 0, NULL, &RET);
#else
    ID = SifLoadStartModule(CheckPath("mc?:/PS2BBL/USBMASS_BD.IRX"), 0, NULL, &RET);
#endif
    DPRINTF(" [USBMASS_BD]: ret=%d, ID=%d\n", RET, ID);
    if (ID < 0 || RET == 1)
        return -2;

    // Give USB transport time to enumerate before first filesystem probe.
    sleep(1);

    return 0;
}

static int load_ata_transport_modules(void)
{
#ifdef BDM_ATA
    int ID, RET;

#ifdef DEV9
    if (!loadDEV9())
        return -1;
#endif

    ID = SifExecModuleBuffer(ata_bd_irx, size_ata_bd_irx, 0, NULL, &RET);
    DPRINTF(" [ATA_BD]: ret=%d, ID=%d\n", RET, ID);
    if (ID < 0 || RET == 1)
        return -2;

    // Match OSDMenu launcher behavior: let ata_bd settle briefly before probes.
    sleep(1);
#endif

    return 0;
}

static int load_mx4sio_transport_modules(void)
{
#ifdef MX4SIO
    int ID, RET;

    ID = SifExecModuleBuffer(mx4sio_bd_mini_irx, size_mx4sio_bd_mini_irx, 0, NULL, &RET);
    DPRINTF(" [MX4SIO_BD]: ID=%d, ret=%d\n", ID, RET);
    if (ID < 0 || RET == 1)
        return -1;
#endif

    return 0;
}

static void derive_bdm_transport_needs(const char *path_hint,
                                       BdmTransportScope scope,
                                       int *want_usb,
                                       int *want_ata,
                                       int *want_mx4sio,
                                       int *strict_usb,
                                       int *strict_ata,
                                       int *strict_mx4sio)
{
    int local_want_usb = 0;
    int local_want_ata = 0;
    int local_want_mx4sio = 0;
    int local_strict_usb = 0;
    int local_strict_ata = 0;
    int local_strict_mx4sio = 0;

    if (path_hint != NULL && *path_hint != '\0') {
        if (starts_with(path_hint, "ata")) {
            local_want_ata = 1;
            local_strict_ata = 1;
        } else if (starts_with(path_hint, "mx4sio") || starts_with(path_hint, "massx")) {
#ifdef MX4SIO
            local_want_mx4sio = 1;
            local_strict_mx4sio = 1;
#endif
        } else if (starts_with(path_hint, "mass")) {
            if (scope == BDM_TRANSPORT_SCOPE_LAUNCH_ENTRY) {
                // For user launch entries, treat legacy mass/massN the same as
                // usb/usbN and avoid loading ATA transports.
                local_want_usb = 1;
                local_strict_usb = 1;
            } else {
                int mass_unit = extract_legacy_mass_unit(path_hint);

                // For PS2BBL boot argv0 mass/massN:
                // - mass:/ or mass0-4:/ can be USB or MX4SIO
                // - mass5-9:/ is USB-only
                // ATA stack is only selected for explicit ata:/ boot hints.
                local_want_usb = 1;
                local_strict_usb = 1;
#ifdef MX4SIO
                if (mass_unit < 0 || mass_unit <= 4)
                    local_want_mx4sio = 1;
#endif
            }
        } else if (starts_with(path_hint, "usb") || starts_with(path_hint, "ilink")) {
            local_want_usb = 1;
            local_strict_usb = 1;
        }
    }

    // Fallback for unknown/empty hints: keep both transports available.
    if (!local_want_usb && !local_want_ata && !local_want_mx4sio) {
        local_want_usb = 1;
        local_want_ata = 1;
    }

    if (want_usb != NULL)
        *want_usb = local_want_usb;
    if (want_ata != NULL)
        *want_ata = local_want_ata;
    if (want_mx4sio != NULL)
        *want_mx4sio = local_want_mx4sio;
    if (strict_usb != NULL)
        *strict_usb = local_strict_usb;
    if (strict_ata != NULL)
        *strict_ata = local_strict_ata;
    if (strict_mx4sio != NULL)
        *strict_mx4sio = local_strict_mx4sio;
}

static int load_bdm_transports_for_path_with_scope(const char *path_hint, BdmTransportScope scope);

static int load_bdm_transports_for_path(const char *path_hint)
{
    return load_bdm_transports_for_path_with_scope(path_hint, BDM_TRANSPORT_SCOPE_BOOT_HINT);
}

static int load_bdm_transports_for_path_with_scope(const char *path_hint, BdmTransportScope scope)
{
    int want_usb = 0;
    int want_ata = 0;
    int want_mx4sio = 0;
    int strict_usb = 0;
    int strict_ata = 0;
    int strict_mx4sio = 0;
    int active_transports = 0;
    int had_error = 0;
    int newly_loaded = 0;

    derive_bdm_transport_needs(path_hint,
                               scope,
                               &want_usb,
                               &want_ata,
                               &want_mx4sio,
                               &strict_usb,
                               &strict_ata,
                               &strict_mx4sio);

    if (want_usb && !s_bdm_usb_transport_loaded) {
        if (load_usb_transport_modules() < 0) {
            had_error = 1;
            if (strict_usb)
                return -1;
        } else {
            s_bdm_usb_transport_loaded = 1;
            newly_loaded++;
        }
    }
    if (want_usb && s_bdm_usb_transport_loaded)
        active_transports++;

    if (want_ata && !s_bdm_ata_transport_loaded) {
        if (load_ata_transport_modules() < 0) {
            had_error = 1;
            if (strict_ata)
                return -2;
        } else {
            s_bdm_ata_transport_loaded = 1;
            newly_loaded++;
        }
    }
    if (want_ata && s_bdm_ata_transport_loaded)
        active_transports++;

#ifdef MX4SIO
    if (want_mx4sio && !s_mx4sio_modules_loaded) {
        if (load_mx4sio_transport_modules() < 0) {
            had_error = 1;
            if (strict_mx4sio)
                return -3;
        } else {
            s_mx4sio_modules_loaded = 1;
            newly_loaded++;
        }
    }
    if (want_mx4sio && s_mx4sio_modules_loaded)
        active_transports++;
#endif

    if (active_transports <= 0 && had_error)
        return -1;

    // Return count of newly loaded transports so callers can decide whether
    // to allow a post-load mount readiness window.
    return newly_loaded;
}

static int load_family_modules(LoaderPathFamily family, const char *path_hint, BdmTransportScope scope)
{
    int j, x;

    if (family == LOADER_PATH_FAMILY_NONE || family == LOADER_PATH_FAMILY_MC || family == LOADER_PATH_FAMILY_XFROM)
        return 0;

    switch (family) {
        case LOADER_PATH_FAMILY_BDM:
        {
#ifdef FILEXIO
            if (LoadFIO() < 0)
                return -1;
#endif
            // For explicit ATA-BDM paths, bring up DEV9 before BDM core so the
            // sequence is: LoadFIO -> DEV9 -> BDM -> BDMFS -> ATA_BD.
            if (path_hint != NULL && starts_with(path_hint, "ata")) {
#ifdef DEV9
                if (!dev9_loaded) {
                    if (!loadDEV9())
                        return -2;
                }
#endif
            }
            if (!s_bdm_core_loaded) {
                if (load_bdm_core_modules() < 0)
                    return -2;
                s_bdm_core_loaded = 1;
            }
            if (load_bdm_transports_for_path_with_scope(path_hint, scope) < 0)
                return -3;
            s_bdm_modules_loaded = 1;
            return 0;
        }

        case LOADER_PATH_FAMILY_MX4SIO:
#ifdef MX4SIO
#ifdef FILEXIO
            if (LoadFIO() < 0)
                return -1;
#endif
            if (load_bdm_core_modules() < 0)
                return -2;
            s_bdm_core_loaded = 1;
            j = SifExecModuleBuffer(mx4sio_bd_mini_irx, size_mx4sio_bd_mini_irx, 0, NULL, &x);
            DPRINTF(" [MX4SIO_BD]: ID=%d, ret=%d\n", j, x);
            if (j < 0 || x == 1)
                return -3;
            s_mx4sio_modules_loaded = 1;
            return 0;
#else
            return -1;
#endif

        case LOADER_PATH_FAMILY_MMCE:
#ifdef MMCE
#ifdef FILEXIO
            if (LoadFIO() < 0)
                return -1;
#endif
            j = SifExecModuleBuffer(mmceman_irx, size_mmceman_irx, 0, NULL, &x);
            DPRINTF(" [MMCEMAN]: ID=%d, ret=%d\n", j, x);
            if (j < 0 || x == 1)
                return -2;
            s_mmce_modules_loaded = 1;
            return 0;
#else
            return -1;
#endif

        case LOADER_PATH_FAMILY_HDD_APA:
#ifdef HDD
#ifdef FILEXIO
            if (LoadFIO() < 0)
                return -1;
#endif
            if (LoadHDDIRX() < 0)
                return -2;
            s_hdd_modules_loaded = 1;
            return 0;
#else
            return -1;
#endif

        default:
            return -1;
    }
}

static int reload_for_family(LoaderPathFamily family,
                             int reboot_iop,
                             int reinit_pad,
                             const char *path_hint,
                             BdmTransportScope scope)
{
    int family_load_result;

    if (family == LOADER_PATH_FAMILY_NONE)
        family = LOADER_PATH_FAMILY_MC;
    if (family == LOADER_PATH_FAMILY_XFROM)
        family = LOADER_PATH_FAMILY_MC;

    if (reboot_iop) {
        PadDeinitPads();
        ResetIOP();
        SifInitRpc(0);
        SifInitIopHeap();
        SifLoadFileInit();
        fioInit();
        sbv_patch_enable_lmb();
        sbv_patch_disable_prefix_check();
        sbv_patch_fileio();
#ifdef DEV9
        dev9_loaded = 0;
#endif
#ifdef FILEXIO
        s_fio_loaded = 0;
#endif
    }

    reset_module_flags();
    publish_module_states();

    load_core_modules();
    s_current_family = LOADER_PATH_FAMILY_MC;
    family_load_result = load_family_modules(family, path_hint, scope);

    if (family_load_result == 0)
        s_current_family = family;

    publish_module_states();

    if (reboot_iop) {
        sceCdInit(SCECdINoD);
        cdInitAdd();
        if (reinit_pad)
            PadInitPads();
    }

    return family_load_result;
}

void LoaderSetBootPathHint(const char *boot_path)
{
    LoaderPathFamily family = LoaderPathFamilyFromPath(boot_path);
    int legacy_mass_unit = extract_legacy_mass_unit(boot_path);
    int mx4sio_unit = -2;
    int ata_unit = -2;
    int ilink_unit = -2;
    int usb_unit = -2;
    const char *boot_path_for_ops = boot_path;
    char normalized_mass_path[256];

    s_boot_family = (family == LOADER_PATH_FAMILY_NONE || family == LOADER_PATH_FAMILY_XFROM)
                        ? LOADER_PATH_FAMILY_MC
                        : family;
    s_boot_config_path[0] = '\0';
    s_boot_path_hint[0] = '\0';
    s_boot_driver_tag[0] = '\0';
    s_boot_config_source_hint = SOURCE_INVALID;
    if (boot_path != NULL && *boot_path != '\0') {
        snprintf(s_boot_path_hint, sizeof(s_boot_path_hint), "%s", boot_path);
        if (starts_with(s_boot_path_hint, "mass") &&
            normalize_mass_path_to_unit_or_generic(s_boot_path_hint,
                                                   legacy_mass_unit,
                                                   normalized_mass_path,
                                                   sizeof(normalized_mass_path))) {
            if (!ci_eq(s_boot_path_hint, normalized_mass_path)) {
                DPRINTF("Boot hint normalize: '%s' -> '%s'\n",
                        s_boot_path_hint,
                        normalized_mass_path);
            }
            snprintf(s_boot_path_hint, sizeof(s_boot_path_hint), "%s", normalized_mass_path);
        }
        boot_path_for_ops = s_boot_path_hint;
        mx4sio_unit = extract_optional_unit_after_prefix(boot_path_for_ops, "mx4sio", 6);
        ata_unit = extract_optional_unit_after_prefix(boot_path_for_ops, "ata", 3);
        ilink_unit = extract_optional_unit_after_prefix(boot_path_for_ops, "ilink", 5);
        usb_unit = extract_optional_unit_after_prefix(boot_path_for_ops, "usb", 3);
    }
    set_boot_cwd_config_path(boot_path_for_ops);

    switch (family) {
        case LOADER_PATH_FAMILY_MMCE:
            snprintf(s_boot_config_path, sizeof(s_boot_config_path), "mmce?:/PS2BBL/CONFIG.INI");
            break;
        case LOADER_PATH_FAMILY_MX4SIO:
            if (mx4sio_unit >= 0)
                snprintf(s_boot_config_path,
                         sizeof(s_boot_config_path),
                         "mx4sio%d:/PS2BBL/CONFIG.INI",
                         mx4sio_unit);
            else
                snprintf(s_boot_config_path, sizeof(s_boot_config_path), "mx4sio:/PS2BBL/CONFIG.INI");
            break;
        case LOADER_PATH_FAMILY_HDD_APA:
            snprintf(s_boot_config_path, sizeof(s_boot_config_path), "hdd0:__sysconf:pfs:/PS2BBL/CONFIG.INI");
            break;
        case LOADER_PATH_FAMILY_BDM:
            if (starts_with(boot_path_for_ops, "ata")) {
                if (ata_unit >= 0)
                    snprintf(s_boot_config_path,
                             sizeof(s_boot_config_path),
                             "ata%d:/PS2BBL/CONFIG.INI",
                             ata_unit);
                else
                    snprintf(s_boot_config_path, sizeof(s_boot_config_path), "ata:/PS2BBL/CONFIG.INI");
            } else if (starts_with(boot_path_for_ops, "ilink")) {
                if (ilink_unit >= 0)
                    snprintf(s_boot_config_path,
                             sizeof(s_boot_config_path),
                             "ilink%d:/PS2BBL/CONFIG.INI",
                             ilink_unit);
                else
                    snprintf(s_boot_config_path, sizeof(s_boot_config_path), "ilink:/PS2BBL/CONFIG.INI");
            }
            else if (legacy_mass_unit >= 0)
                snprintf(s_boot_config_path,
                         sizeof(s_boot_config_path),
                         "mass%d:/PS2BBL/CONFIG.INI",
                         legacy_mass_unit);
            else if (starts_with(boot_path_for_ops, "mass"))
                snprintf(s_boot_config_path, sizeof(s_boot_config_path), "mass:/PS2BBL/CONFIG.INI");
            else if (usb_unit >= 0)
                snprintf(s_boot_config_path,
                         sizeof(s_boot_config_path),
                         "mass%d:/PS2BBL/CONFIG.INI",
                         usb_unit);
            else
                snprintf(s_boot_config_path, sizeof(s_boot_config_path), "mass:/PS2BBL/CONFIG.INI");
            break;
        case LOADER_PATH_FAMILY_XFROM:
            snprintf(s_boot_config_path, sizeof(s_boot_config_path), "xfrom:/PS2BBL/CONFIG.INI");
            break;
        default:
            break;
    }

    s_boot_config_source_hint = source_hint_for_family(family);
    DPRINTF("Boot hint: argv0='%s' raw_family=%s boot_family=%s cwd_cfg='%s'\n",
            (boot_path_for_ops != NULL && *boot_path_for_ops != '\0') ? boot_path_for_ops : "<null>",
            boot_family_name(family),
            boot_family_name(s_boot_family),
            (s_boot_cwd_config_path[0] != '\0') ? s_boot_cwd_config_path : "<none>");
}

const char *LoaderGetBootCwdConfigPath(void)
{
    return s_boot_cwd_config_path;
}

const char *LoaderGetBootPathHint(void)
{
    return s_boot_path_hint;
}

const char *LoaderGetBootConfigPath(void)
{
    return s_boot_config_path;
}

const char *LoaderGetBootDriverTag(void)
{
    return s_boot_driver_tag;
}

int LoaderGetBootConfigSourceHint(void)
{
    return s_boot_config_source_hint;
}

int LoaderPathFamilyReadyWithoutReload(const char *path)
{
    LoaderPathFamily target_family = LoaderPathFamilyFromPath(path);

    if (target_family == LOADER_PATH_FAMILY_NONE ||
        target_family == LOADER_PATH_FAMILY_MC ||
        target_family == LOADER_PATH_FAMILY_XFROM)
        return 1;

    return (s_current_family == target_family);
}

int LoaderEnsurePathFamilyReady(const char *path)
{
    LoaderPathFamily target_family = LoaderPathFamilyFromPath(path);
    int ret;
    int reinit_pad_after_reboot = 0;

    if (target_family == LOADER_PATH_FAMILY_NONE)
        return 0;
    if (target_family == LOADER_PATH_FAMILY_XFROM) {
#if defined(PSX)
        ret = LoaderEnsureXFromModulesLoaded();
        if (ret < 0)
            return ret;
#endif
        return 0;
    }
    // MC is part of the always-loaded core set.
    // Do not reboot/switch away from another active family just to touch mc paths.
    if (target_family == LOADER_PATH_FAMILY_MC)
        return 0;
    if (s_current_family == target_family) {
        if (target_family == LOADER_PATH_FAMILY_BDM)
            return load_bdm_transports_for_path_with_scope(path, BDM_TRANSPORT_SCOPE_LAUNCH_ENTRY);
        return 0;
    }

    // MX4SIO probing relies on active SIO2/pad traffic on some setups.
    // Reopen pads immediately after family switch so first-try path probes
    // can see the mounted media.
    if (target_family == LOADER_PATH_FAMILY_MX4SIO)
        reinit_pad_after_reboot = 1;

    DPRINTF("Switching IOP driver family from %d to %d for path '%s'\n",
            (int)s_current_family,
            (int)target_family,
            (path != NULL) ? path : "");
    ret = reload_for_family(target_family,
                            1,
                            reinit_pad_after_reboot,
                            path,
                            BDM_TRANSPORT_SCOPE_LAUNCH_ENTRY);
    if (ret < 0)
        return ret;
    return 1;
}

int LoaderPrepareFinalLaunch(const char *path)
{
    LoaderPathFamily target_family = LoaderPathFamilyFromPath(path);
    int need_reboot = 0;
    int reinit_pad_after_reboot = 0;

    if (target_family == LOADER_PATH_FAMILY_NONE)
        return 0;
    if (target_family == LOADER_PATH_FAMILY_XFROM) {
#if defined(PSX)
        int xfrom_ret = LoaderEnsureXFromModulesLoaded();
        if (xfrom_ret < 0)
            return xfrom_ret;
#endif
        return 0;
    }

    if (target_family == LOADER_PATH_FAMILY_MX4SIO)
        reinit_pad_after_reboot = 1;

    if (target_family == LOADER_PATH_FAMILY_MC) {
        // Keep launch IOP clean: when launching from MC after probing other
        // families, reboot once back to core-only (MC) modules.
        need_reboot = (s_current_family != LOADER_PATH_FAMILY_MC);
    } else if (s_current_family != target_family) {
        need_reboot = 1;
    } else if (target_family == LOADER_PATH_FAMILY_BDM) {
        int want_usb = 0;
        int want_ata = 0;
        int want_mx4sio = 0;

        derive_bdm_transport_needs(path,
                                   BDM_TRANSPORT_SCOPE_LAUNCH_ENTRY,
                                   &want_usb,
                                   &want_ata,
                                   &want_mx4sio,
                                   NULL,
                                   NULL,
                                   NULL);

        if ((s_bdm_usb_transport_loaded ? 1 : 0) != (want_usb ? 1 : 0) ||
            (s_bdm_ata_transport_loaded ? 1 : 0) != (want_ata ? 1 : 0))
            need_reboot = 1;
#ifdef MX4SIO
        if ((s_mx4sio_modules_loaded ? 1 : 0) != (want_mx4sio ? 1 : 0))
            need_reboot = 1;
#else
        (void)want_mx4sio;
#endif
    }

    if (!need_reboot)
        return 0;

    DPRINTF("Launch sanitize: rebooting IOP for final path '%s' (from=%d to=%d)\n",
            (path != NULL) ? path : "",
            (int)s_current_family,
            (int)target_family);

    if (reload_for_family(target_family,
                          1,
                          reinit_pad_after_reboot,
                          path,
                          BDM_TRANSPORT_SCOPE_LAUNCH_ENTRY) < 0)
        return -1;

    return 1;
}

int LoaderLoadBdmTransportsForHint(const char *path_hint)
{
    int ret;

    if (s_current_family != LOADER_PATH_FAMILY_BDM)
        return -1;
    if (!s_bdm_core_loaded)
        return -2;

    ret = load_bdm_transports_for_path(path_hint);
    if (ret < 0)
        return ret;

    if (s_bdm_usb_transport_loaded || s_bdm_ata_transport_loaded || s_mx4sio_modules_loaded)
        s_bdm_modules_loaded = 1;
    publish_module_states();
    return 0;
}

int LoaderEnsureXFromModulesLoaded(void)
{
#if defined(PSX)
    int ID;
    int RET;

    if (!g_is_psx_desr)
        return 0;
    if (s_xfrom_modules_loaded)
        return 0;

#ifdef DEV9
    if (!dev9_loaded) {
        if (!loadDEV9()) {
            DPRINTF(" [XFROM]: DEV9 load failed\n");
            return -1;
        }
    }
#endif

    ID = SifExecModuleBuffer(extflash_irx, size_extflash_irx, 0, NULL, &RET);
    DPRINTF(" [EXTFLASH]: ID=%d, ret=%d\n", ID, RET);
    if (ID < 0 || RET == 1)
        return -2;

    ID = SifExecModuleBuffer(xfromman_irx, size_xfromman_irx, 0, NULL, &RET);
    DPRINTF(" [XFROMMAN]: ID=%d, ret=%d\n", ID, RET);
    if (ID < 0 || RET == 1)
        return -3;

    s_xfrom_modules_loaded = 1;
#endif

    return 0;
}

void LoaderLoadSystemModules(int *bdm_modules_loaded,
                             int *mx4sio_modules_loaded,
                             int *mmce_modules_loaded,
                             int *hdd_modules_loaded)
{
    const char *boot_hint = s_boot_config_path;

    if (boot_hint == NULL || *boot_hint == '\0')
        boot_hint = NULL;
    reload_for_family(s_boot_family,
                      0,
                      0,
                      boot_hint,
                      BDM_TRANSPORT_SCOPE_BOOT_HINT);
    refine_boot_hint_from_legacy_mass();

    if (bdm_modules_loaded != NULL)
        *bdm_modules_loaded = s_bdm_modules_loaded;
    if (mx4sio_modules_loaded != NULL)
        *mx4sio_modules_loaded = s_mx4sio_modules_loaded;
    if (mmce_modules_loaded != NULL)
        *mmce_modules_loaded = s_mmce_modules_loaded;
    if (hdd_modules_loaded != NULL)
        *hdd_modules_loaded = s_hdd_modules_loaded;
}

int LoaderEnsureDiscBootCdfsReady(void)
{
#ifdef DISC_STOP_AT_BOOT
    int ID, RET;

    if (s_disc_boot_cdfs_ready)
        return 0;

#ifdef FILEXIO
    if (LoadFIO() < 0) {
        DPRINTF(" [DISC_CDFS]: failed to load IOMANX/FILEXIO\n");
        return -1;
    }
#endif

    // Mirror wLaunchELF's startup order for disc filesystem access:
    // ensure CDVD is initialized, then register cdfs before probing cdrom0:.
    sceCdInit(SCECdINoD);
    ID = SifExecModuleBuffer(cdfs_irx, size_cdfs_irx, 0, NULL, &RET);
    DPRINTF(" [CDFS]: ret=%d, ID=%d\n", RET, ID);
    if (ID < 0 || RET == 1)
        return -2;

    sceCdDiskReady(0);
    s_disc_boot_cdfs_ready = 1;
#endif

    return 0;
}

#ifdef MX4SIO
int LookForBDMDevice(void)
{
    static char mass_path[] = "massX:";
    static char DEVID[16];
    int dd;
    int x = 0;
    for (x = 0; x < 5; x++) {
        mass_path[4] = '0' + x;
        if ((dd = fileXioDopen(mass_path)) >= 0) {
            memset(DEVID, 0, sizeof(DEVID));
            fileXioIoctl(dd, USBMASS_IOCTL_GET_DRIVERNAME, DEVID);
            fileXioDclose(dd);
            if (!strncmp(DEVID, "sdc", 3)) {
                DPRINTF("%s: Found MX4SIO device at mass%d:/\n", __func__, x);
                return x;
            }
        }
    }
    return -1;
}
#endif

#ifdef FILEXIO
int LoadFIO(void)
{
    int ID, RET;

    if (s_fio_loaded)
        return 0;

    ID = SifExecModuleBuffer(&iomanX_irx, size_iomanX_irx, 0, NULL, &RET);
    DPRINTF(" [IOMANX]: ret=%d, ID=%d\n", RET, ID);
    if (ID < 0 || RET == 1)
        return -1;

    /* FILEXIO.IRX */
    ID = SifExecModuleBuffer(&fileXio_irx, size_fileXio_irx, 0, NULL, &RET);
    DPRINTF(" [FILEXIO]: ret=%d, ID=%d\n", RET, ID);
    if (ID < 0 || RET == 1)
        return -2;

    RET = fileXioInit();
    DPRINTF("fileXioInit: %d\n", RET);
    if (RET < 0)
        return -3;

    s_fio_loaded = 1;
    return 0;
}
#endif

#ifdef DEV9
int loadDEV9(void)
{
    if (!dev9_loaded) {
        int ID, RET;
        ID = SifExecModuleBuffer(&ps2dev9_irx, size_ps2dev9_irx, 0, NULL, &RET);
        DPRINTF("[DEV9]: ret=%d, ID=%d\n", RET, ID);
        // Either a modload error (ID < 0) or non-resident return (RET == 1)
        // means the module is not available for use.
        if (ID < 0 || RET == 1)
            return 0;
        dev9_loaded = 1;
    }
    return 1;
}
#endif

#ifdef UDPTTY
void loadUDPTTY(void)
{
    int ID, RET;
    ID = SifExecModuleBuffer(&netman_irx, size_netman_irx, 0, NULL, &RET);
    DPRINTF(" [NETMAN]: ret=%d, ID=%d\n", RET, ID);
    ID = SifExecModuleBuffer(&smap_irx, size_smap_irx, 0, NULL, &RET);
    DPRINTF(" [SMAP]: ret=%d, ID=%d\n", RET, ID);
    ID = SifExecModuleBuffer(&ps2ip_irx, size_ps2ip_irx, 0, NULL, &RET);
    DPRINTF(" [PS2IP]: ret=%d, ID=%d\n", RET, ID);
    ID = SifExecModuleBuffer(&udptty_irx, size_udptty_irx, 0, NULL, &RET);
    DPRINTF(" [UDPTTY]: ret=%d, ID=%d\n", RET, ID);
    sleep(3);
}
#endif
