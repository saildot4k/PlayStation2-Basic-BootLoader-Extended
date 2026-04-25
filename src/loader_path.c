// Path token resolution and device-aware launch-path validation helpers.
#include <stdint.h>

#include "main.h"
#include "loader_path.h"

#define CHECKPATH_BUF_SIZE 256

enum {
    DEV_UNKNOWN = -1,
    DEV_MC0 = 0,
    DEV_MC1,
    DEV_MASS,
    DEV_MX4SIO,
    DEV_MMCE0,
    DEV_MMCE1,
    DEV_HDD,
    DEV_XFROM,
    DEV_COUNT
};

static int s_usb_modules_loaded = 0;
static int s_mx4sio_modules_loaded = 0;
static int s_mmce_modules_loaded = 0;
static int s_hdd_modules_loaded = 0;

static int s_pending_command_argc = 0;
static char **s_pending_command_argv = NULL;
static int s_cdvd_cancelled = 0;

static char s_resolved_keypaths[KEY_COUNT][CONFIG_KEY_INDEXES][CHECKPATH_BUF_SIZE];

#ifdef MX4SIO
static int s_mx4sio_slot = -2;
#endif

void LoaderPathSetModuleStates(int usb_ready, int mx4sio_ready, int mmce_ready, int hdd_ready)
{
    s_usb_modules_loaded = usb_ready;
    s_mx4sio_modules_loaded = mx4sio_ready;
    s_mmce_modules_loaded = mmce_ready;
    s_hdd_modules_loaded = hdd_ready;

#ifdef MX4SIO
    // Clear discovered legacy MX4SIO slot when modules/family are reloaded.
    s_mx4sio_slot = -2;
#endif
}

void LoaderPathSetPendingCommandArgs(int argc, char *argv[])
{
    if (argc > 0 && argv != NULL) {
        s_pending_command_argc = argc;
        s_pending_command_argv = argv;
    } else {
        s_pending_command_argc = 0;
        s_pending_command_argv = NULL;
    }
}

int LoaderPathConsumeCdvdCancelled(void)
{
    int cancelled = s_cdvd_cancelled;

    s_cdvd_cancelled = 0;
    return cancelled;
}

static int is_command_token(const char *path)
{
    return (path != NULL && path[0] == '$');
}

static int preferred_mc_slot_char(void)
{
    return (LoaderGetConfigSource() == SOURCE_MC1) ? '1' : '0';
}

#ifdef MMCE
static int preferred_mmce_slot_char(void)
{
    int source = LoaderGetConfigSource();

    if (source == SOURCE_MMCE1 || source == SOURCE_MC1)
        return '1';
    return '0';
}
#endif

static int device_modules_ready(int dev)
{
    switch (dev) {
        case DEV_MASS:
            return s_usb_modules_loaded;
        case DEV_MX4SIO:
            return s_mx4sio_modules_loaded;
        case DEV_MMCE0:
        case DEV_MMCE1:
            return s_mmce_modules_loaded;
        case DEV_HDD:
            return s_hdd_modules_loaded;
        default:
            return 1;
    }
}

#ifdef MX4SIO
static int mx4sio_typed_root_available(void)
{
    struct stat st;

    // Newer ps2sdk BDM drivers expose MX4SIO under its own typed root.
    return (stat("mx4sio:/", &st) == 0 || stat("mx4sio0:/", &st) == 0);
}

static int get_legacy_mx4sio_slot(void)
{
    if (s_mx4sio_slot == -2)
        s_mx4sio_slot = LookForBDMDevice();
    return s_mx4sio_slot;
}
#endif

static int path_prefix_matches(const char *path, const char *prefix, size_t prefix_len)
{
    return (ci_starts_with(path, prefix) &&
            (path[prefix_len] == ':' ||
             path[prefix_len] == '?' ||
             (path[prefix_len] >= '0' && path[prefix_len] <= '9')));
}

LoaderPathFamily LoaderPathFamilyFromPath(const char *path)
{
    if (path == NULL || *path == '\0')
        return LOADER_PATH_FAMILY_NONE;
    if (path[0] == '$')
        return LOADER_PATH_FAMILY_NONE;
    if (path_prefix_matches(path, "mc", 2))
        return LOADER_PATH_FAMILY_MC;
    if (path_prefix_matches(path, "mx4sio", 7) || path_prefix_matches(path, "massx", 5))
        return LOADER_PATH_FAMILY_MX4SIO;
    if (path_prefix_matches(path, "mmce", 4))
        return LOADER_PATH_FAMILY_MMCE;
    if (path_prefix_matches(path, "hdd", 3))
        return LOADER_PATH_FAMILY_HDD_APA;
    if (path_prefix_matches(path, "xfrom", 5))
        return LOADER_PATH_FAMILY_XFROM;
    if (path_prefix_matches(path, "usb", 3) ||
        path_prefix_matches(path, "mass", 4) ||
        path_prefix_matches(path, "ata", 3) ||
        path_prefix_matches(path, "ilink", 5))
        return LOADER_PATH_FAMILY_BDM;
    return LOADER_PATH_FAMILY_NONE;
}

static int device_id_from_path(const char *path)
{
    if (path == NULL || *path == '\0')
        return DEV_UNKNOWN;
    if (path_prefix_matches(path, "mc0", 3))
        return DEV_MC0;
    if (path_prefix_matches(path, "mc1", 3))
        return DEV_MC1;
    if (path_prefix_matches(path, "mx4sio", 7))
        return DEV_MX4SIO;
    if (path_prefix_matches(path, "massx", 5))
        return DEV_MX4SIO;
    if (path_prefix_matches(path, "usb", 3))
        return DEV_MASS;
    if (path_prefix_matches(path, "ata", 3))
        return DEV_MASS;
    if (path_prefix_matches(path, "ilink", 5))
        return DEV_MASS;
    if (path_prefix_matches(path, "mass", 4))
        return DEV_MASS;
    if (path_prefix_matches(path, "mmce0", 5))
        return DEV_MMCE0;
    if (path_prefix_matches(path, "mmce1", 5))
        return DEV_MMCE1;
    if (path_prefix_matches(path, "hdd0", 4))
        return DEV_HDD;
    if (path_prefix_matches(path, "xfrom", 5))
        return DEV_XFROM;
    return DEV_UNKNOWN;
}

static int device_available_for_dev(int dev)
{
    if (dev == DEV_UNKNOWN)
        return 1;

    if (!device_modules_ready(dev))
        return 0;
    return 1;
}

void LoaderBuildDeviceAvailableCache(int dev_ok[LOADER_DEVICE_COUNT])
{
    int dev;

    if (dev_ok == NULL)
        return;

    for (dev = 0; dev < DEV_COUNT; dev++)
        dev_ok[dev] = device_available_for_dev(dev) ? 1 : 0;
}

int LoaderDeviceAvailableForPathCached(const char *path, const int dev_ok[LOADER_DEVICE_COUNT])
{
    int dev;

    if (path == NULL || *path == '\0' || dev_ok == NULL)
        return 0;
    if (ci_starts_with(path, "mc?:"))
        return (dev_ok[DEV_MC0] || dev_ok[DEV_MC1]);
#ifdef MMCE
    if (ci_starts_with(path, "mmce?:"))
        return (dev_ok[DEV_MMCE0] || dev_ok[DEV_MMCE1]);
#endif

    dev = device_id_from_path(path);
    if (dev == DEV_UNKNOWN)
        return 1;

    return dev_ok[dev];
}

static int copy_string_safe(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0)
        return 0;

    if (src == NULL)
        src = "";

    snprintf(dst, dst_size, "%s", src);
    return 1;
}

static int path_prefix_with_optional_unit(const char *path,
                                          const char *prefix,
                                          size_t prefix_len,
                                          int *unit_out,
                                          const char **suffix_out)
{
    int unit = -1;
    const char *suffix;

    if (path == NULL || prefix == NULL)
        return 0;
    if (!ci_starts_with(path, prefix))
        return 0;

    if (path[prefix_len] == ':') {
        suffix = path + prefix_len;
    } else if (path[prefix_len] >= '0' &&
               path[prefix_len] <= '9' &&
               path[prefix_len + 1] == ':') {
        unit = path[prefix_len] - '0';
        suffix = path + prefix_len + 1;
    } else {
        return 0;
    }

    if (unit_out != NULL)
        *unit_out = unit;
    if (suffix_out != NULL)
        *suffix_out = suffix;
    return 1;
}

static int build_mass_path(char *out, size_t out_size, const char *suffix, int unit)
{
    if (out == NULL || out_size == 0 || suffix == NULL || suffix[0] != ':')
        return 0;

    if (unit >= 0 && unit <= 9)
        snprintf(out, out_size, "mass%d%s", unit, suffix);
    else
        snprintf(out, out_size, "mass0%s", suffix);

    return 1;
}

static int mass_mount_openable(int unit)
{
    char mountpoint[] = "mass0:";

    if (unit < 0 || unit > 9)
        return 0;

    mountpoint[4] = '0' + unit;

#ifdef FILEXIO
    {
        int dd = fileXioDopen(mountpoint);
        if (dd >= 0) {
            fileXioDclose(dd);
            return 1;
        }
    }
#endif

    {
        struct stat st;
        return (stat(mountpoint, &st) == 0);
    }
}

int LoaderPathCanAttemptNow(const char *path)
{
    if (path == NULL || *path == '\0' || path[0] == '$')
        return 1;

    return 1;
}

static int resolve_pair_path_copy(const char *path,
                                  int slot_index,
                                  char preferred,
                                  char *out,
                                  size_t out_size,
                                  int require_existing)
{
    char alternate;

    if (!copy_string_safe(out, out_size, path))
        return 0;

    if (out[slot_index] == '\0')
        return 0;

    alternate = (preferred == '0') ? '1' : '0';

    out[slot_index] = preferred;
    if (!require_existing || exist(out))
        return 1;

    out[slot_index] = alternate;
    if (!require_existing || exist(out))
        return 1;

    return 0;
}

static const char *resolve_path_tokens(const char *path,
                                       char *out,
                                       size_t out_size,
                                       int require_existing_pairs)
{
    int bdm_unit = -1;
    const char *bdm_suffix = NULL;

    if (!copy_string_safe(out, out_size, path))
        return NULL;

    if (ci_starts_with(path, "mc?")) {
        if (!resolve_pair_path_copy(path,
                                    2,
                                    (char)preferred_mc_slot_char(),
                                    out,
                                    out_size,
                                    require_existing_pairs))
            return NULL;
        return out;
    }

    // Match OSDMenu behavior for USB-family paths:
    // probe legacy massN slots instead of relying on usb:/ alias resolution.
    if (path_prefix_with_optional_unit(path, "usb", 3, &bdm_unit, &bdm_suffix) ||
        path_prefix_with_optional_unit(path, "mass", 4, &bdm_unit, &bdm_suffix)) {
        int i;
        int mount_seen = 0;
        char candidate[CHECKPATH_BUF_SIZE];

        if (bdm_unit >= 0) {
            if (!build_mass_path(out, out_size, bdm_suffix, bdm_unit))
                return NULL;
            return out;
        }

        if (!require_existing_pairs) {
            if (!build_mass_path(out, out_size, bdm_suffix, 0))
                return NULL;
            return out;
        }

        for (i = 0; i < 10; i++) {
            if (!build_mass_path(candidate, sizeof(candidate), bdm_suffix, i))
                continue;
            if (exist(candidate)) {
                DPRINTF("CheckPath BDM match: requested='%s' candidate='%s'\n",
                        path,
                        candidate);
                copy_string_safe(out, out_size, candidate);
                return out;
            }
            if (!mount_seen && mass_mount_openable(i))
                mount_seen = 1;
        }

        if (!mount_seen) {
            copy_string_safe(out, out_size, path);
            return out;
        }

        if (!build_mass_path(out, out_size, bdm_suffix, 0))
            return NULL;
        return out;
    }

#ifdef MMCE
    if (ci_starts_with(path, "mmce?")) {
        char preferred_slot;

        preferred_slot = (char)preferred_mmce_slot_char();
        if (!require_existing_pairs) {
            out[4] = preferred_slot;
            return out;
        }
        if (!resolve_pair_path_copy(path,
                                    4,
                                    preferred_slot,
                                    out,
                                    out_size,
                                    1))
            return NULL;
        return out;
    }
#endif

#ifdef HDD
    if (ci_starts_with(path, "hdd")) {
        const char *pfs_path;

        if (MountParty(path) < 0) {
            DPRINTF("-{%s}-\n", path);
            return out;
        }

        pfs_path = strstr(out, "pfs:");
        DPRINTF("--{%s}--{%s}\n", out, (pfs_path != NULL) ? pfs_path : "<none>");
        return (pfs_path != NULL) ? pfs_path : out;
    }
#endif

#ifdef MX4SIO
    if (ci_starts_with(path, "mx4sio:")) {
        int slot = get_legacy_mx4sio_slot();

        if (mx4sio_typed_root_available())
            return out;
        if (slot >= 0) {
            snprintf(out, out_size, "mass%d:%s", slot, path + 7);
            return out;
        }
        return out;
    }

    if (ci_starts_with(path, "massX:")) {
        int slot = get_legacy_mx4sio_slot();

        if (mx4sio_typed_root_available()) {
            snprintf(out, out_size, "mx4sio:%s", path + 6);
            return out;
        }
        if (slot >= 0)
            out[4] = '0' + slot;
        return out;
    }
#endif

    return out;
}

static int command_display_path(const char *path,
                                const int dev_ok[LOADER_DEVICE_COUNT],
                                char *display_out,
                                size_t display_out_size)
{
    const char *runkelf_prefix = "$RUNKELF:";

    if (path == NULL || *path == '\0' || !is_command_token(path))
        return 0;

#ifndef HDD
    if (!strcmp(path, "$HDDCHECKER"))
        return 0;
#endif

    if (!strncmp(path, runkelf_prefix, strlen(runkelf_prefix))) {
        char resolved[CHECKPATH_BUF_SIZE];
        const char *kelf_path = path + strlen(runkelf_prefix);
        const char *resolved_path;

        if (kelf_path == NULL || *kelf_path == '\0')
            return 0;
        if (strncmp(kelf_path, "mc", 2) != 0 && strncmp(kelf_path, "hdd", 3) != 0)
            return 0;
        if (!LoaderDeviceAvailableForPathCached(kelf_path, dev_ok))
            return 0;

        resolved_path = resolve_path_tokens(kelf_path, resolved, sizeof(resolved), 1);
        if (resolved_path == NULL)
            return 0;
        if (!exist(resolved_path))
            return 0;

        copy_string_safe(display_out, display_out_size, resolved_path);
        return 1;
    }

    copy_string_safe(display_out, display_out_size, path + 1);
    return 1;
}

static inline int is_elf_ext_ci(const char *s, size_t len)
{
    if (len < 4)
        return 0;
    return ((s[len - 4] == '.') &&
            ((s[len - 3] == 'e' || s[len - 3] == 'E')) &&
            ((s[len - 2] == 'l' || s[len - 2] == 'L')) &&
            ((s[len - 1] == 'f' || s[len - 1] == 'F')));
}

#ifdef HDD
static int path_has_patinfo_token(const char *path)
{
    static const char token[] = ":PATINFO";
    size_t token_len = sizeof(token) - 1;
    const char *p;

    if (path == NULL)
        return 0;

    for (p = path; *p != '\0'; p++) {
        size_t k;

        for (k = 0; k < token_len && p[k] != '\0'; k++) {
            unsigned char a = (unsigned char)p[k];
            unsigned char b = (unsigned char)token[k];

            if (a >= 'a' && a <= 'z')
                a -= ('a' - 'A');
            if (b >= 'a' && b <= 'z')
                b -= ('a' - 'A');
            if (a != b)
                break;
        }

        if (k == token_len)
            return 1;
    }

    return 0;
}

static int entry_has_arg_ci(int key_idx, int entry_idx, const char *arg)
{
    int i;

    if (arg == NULL)
        return 0;
    if (key_idx < 0 || key_idx >= KEY_COUNT)
        return 0;
    if (entry_idx < 0 || entry_idx >= CONFIG_KEY_INDEXES)
        return 0;

    for (i = 0; i < GLOBCFG.KEYARGC[key_idx][entry_idx]; i++) {
        const char *entry_arg = GLOBCFG.KEYARGS[key_idx][entry_idx][i];

        if (entry_arg == NULL)
            continue;

        while (*entry_arg == ' ' || *entry_arg == '\t')
            entry_arg++;

        if (ci_eq(entry_arg, arg))
            return 1;
    }

    return 0;
}

static int allow_virtual_patinfo_entry(int key_idx, int entry_idx, const char *path)
{
    return path_has_patinfo_token(path) && entry_has_arg_ci(key_idx, entry_idx, "-patinfo");
}
#else
static int allow_virtual_patinfo_entry(int key_idx, int entry_idx, const char *path)
{
    (void)key_idx;
    (void)entry_idx;
    (void)path;
    return 0;
}
#endif

static const char *path_basename(const char *path)
{
    const char *base = path;
    const char *p = path;

    if (p == NULL)
        return "";

    while (*p) {
        if (*p == '/' || *p == '\\')
            base = p + 1;
        p++;
    }

    return base;
}

int LoaderAllowVirtualPatinfoEntry(int key_idx, int entry_idx, const char *path)
{
    return allow_virtual_patinfo_entry(key_idx, entry_idx, path);
}

void ValidateKeypathsAndSetNames(int display_mode, int scan_paths)
{
    static char name_buf[KEY_COUNT][MAX_LEN];
    int dev_ok[LOADER_DEVICE_COUNT];
    const char *first_valid[KEY_COUNT];
    int logo_disp = GLOBCFG.LOGO_DISP;
    u64 next_loading_refresh_ms = 0;
    int i;
    int j;

    for (i = 0; i < KEY_COUNT; i++)
        first_valid[i] = NULL;

    if (scan_paths) {
        LoaderBuildDeviceAvailableCache(dev_ok);
        if (logo_disp > 0)
            next_loading_refresh_ms = Timer() + 500u;

        for (i = 0; i < KEY_COUNT; i++) {
            int found = 0;

            for (j = 0; j < CONFIG_KEY_INDEXES; j++) {
                char *path = GLOBCFG.KEYPATHS[i][j];

                if (logo_disp > 0) {
                    u64 now_ms = Timer();

                    if (now_ms >= next_loading_refresh_ms) {
                        SplashDrawLoadingStatus(logo_disp);
                        next_loading_refresh_ms = now_ms + 500u;
                    }
                }

                if (found) {
                    GLOBCFG.KEYPATHS[i][j] = "";
                    continue;
                }
                if (path == NULL || *path == '\0') {
                    GLOBCFG.KEYPATHS[i][j] = "";
                    continue;
                }

                if (is_command_token(path)) {
                    char cmd_display[CHECKPATH_BUF_SIZE];

                    if (command_display_path(path, dev_ok, cmd_display, sizeof(cmd_display))) {
                        copy_string_safe(s_resolved_keypaths[i][j],
                                         sizeof(s_resolved_keypaths[i][j]),
                                         cmd_display);
                        if (first_valid[i] == NULL)
                            first_valid[i] = s_resolved_keypaths[i][j];
                        found = 1;
                    }
                    continue; // Commands only run on keypress.
                }

                if (!LoaderDeviceAvailableForPathCached(path, dev_ok)) {
                    GLOBCFG.KEYPATHS[i][j] = "";
                    continue;
                }

                {
                    char resolved[CHECKPATH_BUF_SIZE];
                    const char *resolved_path = resolve_path_tokens(path,
                                                                    resolved,
                                                                    sizeof(resolved),
                                                                    1);

                    if (resolved_path == NULL) {
                        GLOBCFG.KEYPATHS[i][j] = "";
                        continue;
                    }

                    if (allow_virtual_patinfo_entry(i, j, resolved_path) || exist(resolved_path)) {
                        copy_string_safe(s_resolved_keypaths[i][j],
                                         sizeof(s_resolved_keypaths[i][j]),
                                         resolved_path);
                        // Keep raw HDD launch paths so CheckPath() can remount and
                        // refresh PART at launch time (pre-scanned LOGO modes).
                        if (path_prefix_matches(path, "hdd", 3))
                            GLOBCFG.KEYPATHS[i][j] = path;
                        else
                            GLOBCFG.KEYPATHS[i][j] = s_resolved_keypaths[i][j];
                        if (first_valid[i] == NULL)
                            first_valid[i] = GLOBCFG.KEYPATHS[i][j];
                        found = 1;
                    } else {
                        GLOBCFG.KEYPATHS[i][j] = "";
                    }
                }
            }
        }
    }

    if (display_mode < 0 || display_mode > 3)
        display_mode = 0;

    if (display_mode == 0) {
        for (i = 0; i < KEY_COUNT; i++)
            GLOBCFG.KEYNAMES[i] = "";
        return;
    }

    if (display_mode == 1)
        return; // Keep user-defined names.

    for (i = 0; i < KEY_COUNT; i++) {
        if (display_mode == 3) {
            GLOBCFG.KEYNAMES[i] = (first_valid[i] != NULL) ? first_valid[i] : "";
        } else {
            const char *base = (first_valid[i] != NULL) ? path_basename(first_valid[i]) : "";
            size_t len = strlen(base);

            if (is_elf_ext_ci(base, len))
                len -= 4;
            if (len >= MAX_LEN)
                len = MAX_LEN - 1;
            memcpy(name_buf[i], base, len);
            name_buf[i][len] = '\0';
            GLOBCFG.KEYNAMES[i] = name_buf[i];
        }
    }
}

static void runKELF(const char *kelfpath)
{
    char arg3[64];
    char *args[4] = {"-m rom0:SIO2MAN", "-m rom0:MCMAN", "-m rom0:MCSERV", arg3};

    snprintf(arg3, sizeof(arg3), "-x %s", kelfpath);

    PadDeinitPads();
    LoadExecPS2("moduleload", 4, args);
}

char *CheckPath(const char *path)
{
    static char path_buf[CHECKPATH_BUF_SIZE];
    const char *resolved_path;

    path_buf[0] = '\0';

    if (path == NULL)
        return path_buf;

    if (path[0] == '$') {
        s_cdvd_cancelled = 0;

        if (!strcmp("$CDVD", path))
            s_cdvd_cancelled = (dischandler(0, s_pending_command_argc, s_pending_command_argv) < 0);
        if (!strcmp("$CDVD_NO_PS2LOGO", path))
            s_cdvd_cancelled = (dischandler(1, s_pending_command_argc, s_pending_command_argv) < 0);
#ifdef HDD
        if (!strcmp("$HDDCHECKER", path))
            HDDChecker();
#endif
        if (!strcmp("$CREDITS", path))
            s_cdvd_cancelled = credits();
        if (!strcmp("$OSDSYS", path))
            runOSDNoUpdate();

        if (!strncmp("$RUNKELF:", path, strlen("$RUNKELF:"))) {
            char kelf_buf[CHECKPATH_BUF_SIZE];

            resolved_path = resolve_path_tokens(path + strlen("$RUNKELF:"),
                                                kelf_buf,
                                                sizeof(kelf_buf),
                                                1);
            if (resolved_path != NULL && *resolved_path != '\0')
                runKELF(resolved_path);
        }

        copy_string_safe(path_buf, sizeof(path_buf), path);
        return path_buf;
    }

    resolved_path = resolve_path_tokens(path, path_buf, sizeof(path_buf), 1);
    if (resolved_path == NULL) {
        copy_string_safe(path_buf, sizeof(path_buf), path);
        return path_buf;
    }

    if (resolved_path != path_buf)
        copy_string_safe(path_buf, sizeof(path_buf), resolved_path);

    return path_buf;
}
