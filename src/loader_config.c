// Config discovery, parsing, and bootstrap flow (defaults + splash preparation).
#include <stdint.h>

#include "main.h"
#include "loader_config.h"
#include "loader_path.h"
#include "splash_render.h"
#include "splash_screen.h"

#define CONFIG_DEVICE_READY_POLL_INTERVAL_MS 200u
#define CONFIG_FATAL_ERROR_DISPLAY_MS 3000u

extern unsigned char *config_buf;
extern int g_is_psx_desr;
static char s_resolved_config_path[256] = "";
static char s_requested_config_path[256] = "";

typedef struct
{
    const char *key;
    size_t field_offset;
} ConfigIntKey;

static const ConfigIntKey g_int_keys[] = {
    {"OSDHISTORY_READ", offsetof(CONFIG, OSDHISTORY_READ)},
    {"KEY_READ_WAIT_TIME", offsetof(CONFIG, DELAY)},
    {"EJECT_TRAY", offsetof(CONFIG, TRAYEJECT)},
    {"LOGO_DISPLAY", offsetof(CONFIG, LOGO_DISP)},
    {"CDROM_DISABLE_GAMEID", offsetof(CONFIG, CDROM_DISABLE_GAMEID)},
    {"APP_GAMEID", offsetof(CONFIG, APP_GAMEID)},
    {"DISC_STOP", offsetof(CONFIG, DISC_STOP)},
};

static int parse_scalar_int_key(const char *name, const char *value)
{
    size_t i;

    for (i = 0; i < sizeof(g_int_keys) / sizeof(g_int_keys[0]); i++) {
        if (ci_eq(name, g_int_keys[i].key)) {
            int *target = (int *)((char *)&GLOBCFG + g_int_keys[i].field_offset);
            const char *safe_value = (value != NULL) ? value : "0";

            *target = atoi(safe_value);
            return 1;
        }
    }

    return 0;
}

static int parse_name_entry(const char *name, char *value, int *key_index_out)
{
    int key_index;
    char key_name[32];

    if (key_index_out != NULL)
        *key_index_out = -1;

    for (key_index = 0; key_index < KEY_COUNT; key_index++) {
        snprintf(key_name, sizeof(key_name), "NAME_%s", KEYS_ID[key_index]);
        if (ci_eq(name, key_name)) {
            if (value == NULL || *value == '\0')
                GLOBCFG.KEYNAMES[key_index] = NULL;
            else
                GLOBCFG.KEYNAMES[key_index] = value;
            if (key_index_out != NULL)
                *key_index_out = key_index;
            return 1;
        }
    }

    return 0;
}

static int find_arg_entry_indices(const char *name, int *key_index_out, int *entry_index_out)
{
    int key_index;
    int entry_index;
    char key_name[32];

    for (key_index = 0; key_index < KEY_COUNT; key_index++) {
        for (entry_index = 0; entry_index < CONFIG_KEY_INDEXES; entry_index++) {
            snprintf(key_name, sizeof(key_name), "ARG_%s_E%d", KEYS_ID[key_index], entry_index + 1);
            if (!ci_eq(name, key_name))
                continue;

            if (key_index_out != NULL)
                *key_index_out = key_index;
            if (entry_index_out != NULL)
                *entry_index_out = entry_index;
            return 1;
        }
    }

    return 0;
}

static void prepare_arg_storage_from_counts(const int arg_counts[KEY_COUNT][CONFIG_KEY_INDEXES])
{
    int key_index;
    int entry_index;

    for (key_index = 0; key_index < KEY_COUNT; key_index++) {
        for (entry_index = 0; entry_index < CONFIG_KEY_INDEXES; entry_index++) {
            int count = arg_counts[key_index][entry_index];

            if (GLOBCFG.KEYARGS[key_index][entry_index] != NULL) {
                free(GLOBCFG.KEYARGS[key_index][entry_index]);
                GLOBCFG.KEYARGS[key_index][entry_index] = NULL;
            }

            GLOBCFG.KEYARGC[key_index][entry_index] = 0;
            GLOBCFG.KEYARGCAP[key_index][entry_index] = 0;

            if (count > 0) {
                GLOBCFG.KEYARGS[key_index][entry_index] = malloc(sizeof(char *) * (size_t)count);
                if (GLOBCFG.KEYARGS[key_index][entry_index] == NULL) {
                    DPRINTF("# Failed to allocate %d ARG entries for [%s][E%d]\n",
                            count,
                            KEYS_ID[key_index],
                            entry_index + 1);
                    continue;
                }

                GLOBCFG.KEYARGCAP[key_index][entry_index] = count;
            }
        }
    }
}

static int append_arg_entry(int key_index, int entry_index, char *value)
{
    int argc;
    int argcap;
    int new_cap;
    char **new_args;

    if (value == NULL || *value == '\0')
        return 1;

    argc = GLOBCFG.KEYARGC[key_index][entry_index];
    argcap = GLOBCFG.KEYARGCAP[key_index][entry_index];

    if (argc >= argcap) {
        // Two-pass parsing should pre-size this exactly. If counts drift for any
        // reason, grow by one to keep behavior deterministic.
        new_cap = argc + 1;
        new_args = realloc(GLOBCFG.KEYARGS[key_index][entry_index], sizeof(char *) * (size_t)new_cap);
        if (new_args == NULL) {
            DPRINTF("# Failed to grow args list for [%s][E%d] to %d entries\n",
                    KEYS_ID[key_index],
                    entry_index + 1,
                    new_cap);
            return 1;
        }

        GLOBCFG.KEYARGS[key_index][entry_index] = new_args;
        GLOBCFG.KEYARGCAP[key_index][entry_index] = new_cap;
    }

    GLOBCFG.KEYARGS[key_index][entry_index][argc] = value;
    GLOBCFG.KEYARGC[key_index][entry_index] = argc + 1;

    return 1;
}

static int parse_arg_entry(const char *name, char *value)
{
    int key_index;
    int entry_index;

    if (!find_arg_entry_indices(name, &key_index, &entry_index))
        return 0;

    return append_arg_entry(key_index, entry_index, value);
}

static int parse_launch_key_entry(const char *name, char *value, u32 *parsed_launch_key_mask)
{
    int key_index;
    int entry_index;
    char key_name[32];

    for (key_index = 0; key_index < KEY_COUNT; key_index++) {
        for (entry_index = 0; entry_index < CONFIG_KEY_INDEXES; entry_index++) {
            snprintf(key_name, sizeof(key_name), "LK_%s_E%d", KEYS_ID[key_index], entry_index + 1);
            if (!ci_eq(name, key_name))
                continue;

            // First explicit LK_<KEY>_E* seen for this key:
            // clear all entries so unspecified E2..E10 do not inherit defaults.
            if (parsed_launch_key_mask != NULL &&
                key_index < (int)(sizeof(*parsed_launch_key_mask) * 8u) &&
                ((*parsed_launch_key_mask & ((u32)1u << key_index)) == 0)) {
                int clear_index;

                for (clear_index = 0; clear_index < CONFIG_KEY_INDEXES; clear_index++)
                    GLOBCFG.KEYPATHS[key_index][clear_index] = NULL;

                *parsed_launch_key_mask |= ((u32)1u << key_index);
            }

            if (value == NULL || *value == '\0') {
                GLOBCFG.KEYPATHS[key_index][entry_index] = NULL;
                return 1;
            }

            GLOBCFG.KEYPATHS[key_index][entry_index] = value;
            return 2;
        }
    }

    return 0;
}

static void set_fallback_entry_args(const DefaultLaunchArgEntry *default_args, int default_arg_count)
{
    int arg_counts[KEY_COUNT][CONFIG_KEY_INDEXES];
    int i;

    memset(arg_counts, 0, sizeof(arg_counts));

    if (default_args != NULL && default_arg_count > 0) {
        for (i = 0; i < default_arg_count; i++) {
            int key_index = default_args[i].key_index;
            int entry_index = default_args[i].entry_index;
            const char *arg = default_args[i].arg;

            if (key_index < 0 || key_index >= KEY_COUNT ||
                entry_index < 0 || entry_index >= CONFIG_KEY_INDEXES)
                continue;
            if (arg == NULL || *arg == '\0')
                continue;

            arg_counts[key_index][entry_index]++;
        }
    }

    prepare_arg_storage_from_counts(arg_counts);

    if (default_args == NULL || default_arg_count <= 0)
        return;

    for (i = 0; i < default_arg_count; i++) {
        int key_index = default_args[i].key_index;
        int entry_index = default_args[i].entry_index;
        const char *arg = default_args[i].arg;

        if (key_index < 0 || key_index >= KEY_COUNT ||
            entry_index < 0 || entry_index >= CONFIG_KEY_INDEXES)
            continue;
        if (arg == NULL || *arg == '\0')
            continue;

        append_arg_entry(key_index, entry_index, (char *)arg);
    }
}

static int path_is_legacy_mass(const char *path)
{
    if (path == NULL || *path == '\0')
        return 0;

    return ci_starts_with(path, "mass");
}

static int path_is_generic_legacy_mass(const char *path)
{
    if (path == NULL || *path == '\0')
        return 0;

    return (ci_starts_with(path, "mass:"));
}

static int extract_legacy_mass_unit(const char *path)
{
    if (path == NULL || *path == '\0')
        return -1;
    if (!ci_starts_with(path, "mass"))
        return -1;
    if (path[4] >= '0' && path[4] <= '9' && path[5] == ':')
        return (int)(path[4] - '0');
    return -1;
}

static int build_mass_generic_alias(const char *path, char *out, size_t out_size)
{
    const char *suffix;

    if (path == NULL || out == NULL || out_size == 0)
        return 0;
    if (!ci_starts_with(path, "mass"))
        return 0;
    if (!(path[4] >= '0' && path[4] <= '9' && path[5] == ':'))
        return 0;

    suffix = path + 5; // points to ':' in "massN:..."
    if (suffix[1] == '/' || suffix[1] == '\0')
        snprintf(out, out_size, "mass%s", suffix);
    else
        snprintf(out, out_size, "mass:/%s", suffix + 1);
    return 1;
}

static int build_mass_typed_alias(const char *path,
                                  const char *typed_prefix,
                                  int max_unit,
                                  char *out,
                                  size_t out_size)
{
    const char *suffix;
    int unit = -1;

    if (path == NULL || typed_prefix == NULL || out == NULL || out_size == 0)
        return 0;
    if (!ci_starts_with(path, "mass"))
        return 0;

    if (path[4] == ':') {
        suffix = path + 4; // points to ':' in "mass:..."
    } else if (path[4] >= '0' && path[4] <= '9' && path[5] == ':') {
        unit = path[4] - '0';
        if (max_unit >= 0 && unit > max_unit)
            return 0;
        suffix = path + 5; // points to ':' in "massN:..."
    } else {
        return 0;
    }

    if (suffix[1] == '/' || suffix[1] == '\0')
        snprintf(out, out_size, "%s%s", typed_prefix, suffix);
    else
        snprintf(out, out_size, "%s:/%s", typed_prefix, suffix + 1);

    return 1;
}

static int build_mass_usb_alias(const char *path, char *out, size_t out_size)
{
    return build_mass_typed_alias(path, "usb", -1, out, out_size);
}

#ifdef MX4SIO
static int build_mass_mx4_alias(const char *path, char *out, size_t out_size)
{
    const char *suffix;
    int unit = -1;

    if (path == NULL || out == NULL || out_size == 0)
        return 0;
    if (!ci_starts_with(path, "mass"))
        return 0;

    if (path[4] == ':') {
        suffix = path + 4; // points to ':' in "mass:..."
    } else if (path[4] >= '0' && path[4] <= '9' && path[5] == ':') {
        unit = path[4] - '0';
        if (unit > 4)
            return 0;
        suffix = path + 5; // points to ':' in "massN:..."
    } else {
        return 0;
    }

    if (suffix[1] == '/' || suffix[1] == '\0') {
        if (unit >= 0)
            snprintf(out, out_size, "mx4sio%d%s", unit, suffix);
        else
            snprintf(out, out_size, "mx4sio%s", suffix);
    } else {
        if (unit >= 0)
            snprintf(out, out_size, "mx4sio%d:/%s", unit, suffix + 1);
        else
            snprintf(out, out_size, "mx4sio:/%s", suffix + 1);
    }

    return 1;
}
#endif

static int append_unique_candidate(const char **paths,
                                   int count,
                                   int max_count,
                                   const char *candidate)
{
    int i;

    if (paths == NULL || max_count <= 0 || candidate == NULL || *candidate == '\0')
        return count;

    for (i = 0; i < count; i++) {
        if (ci_eq(paths[i], candidate))
            return count;
    }
    if (count >= max_count)
        return count;

    paths[count] = candidate;
    return count + 1;
}

static int config_candidate_should_probe_now(const char *candidate_path)
{
    LoaderPathFamily family;

    if (candidate_path == NULL || *candidate_path == '\0')
        return 0;

    family = LoaderPathFamilyFromPath(candidate_path);

    // Keep strict readiness gating only for APA HDD paths.
    if (family == LOADER_PATH_FAMILY_HDD_APA)
        return LoaderPathCanAttemptNow(candidate_path);

    return 1;
}

static FILE *open_first_config_candidate(const char **paths,
                                         int path_count,
                                         const char **matched_path_out,
                                         char *resolved_path_out,
                                         size_t resolved_path_out_size)
{
    int i;

    if (matched_path_out != NULL)
        *matched_path_out = NULL;
    if (resolved_path_out != NULL && resolved_path_out_size > 0)
        resolved_path_out[0] = '\0';

    if (paths == NULL || path_count <= 0)
        return NULL;

    for (i = 0; i < path_count; i++) {
        const char *candidate_path = paths[i];
        char *resolved_path;
        FILE *fp = NULL;

        if (candidate_path == NULL || *candidate_path == '\0')
            continue;

        if (!config_candidate_should_probe_now(candidate_path))
            continue;

        resolved_path = CheckPath(candidate_path);
        if (resolved_path == NULL || *resolved_path == '\0')
            continue;

        fp = fopen(resolved_path, "r");
        if (fp == NULL)
            continue;

        if (matched_path_out != NULL)
            *matched_path_out = candidate_path;
        if (resolved_path_out != NULL && resolved_path_out_size > 0)
            snprintf(resolved_path_out, resolved_path_out_size, "%s", resolved_path);
        return fp;
    }

    return NULL;
}

static int should_wait_for_config_device_family(LoaderPathFamily family)
{
    return (family == LOADER_PATH_FAMILY_BDM ||
            family == LOADER_PATH_FAMILY_MX4SIO ||
            family == LOADER_PATH_FAMILY_MMCE ||
            family == LOADER_PATH_FAMILY_HDD_APA);
}

static void wait_for_config_device_ready(const char *ready_path,
                                         const char *requested_path,
                                         u64 *rescue_combo_deadline,
                                         LoaderEmergencyPollFn poll_fn)
{
    LoaderPathFamily family;

    if (ready_path == NULL || *ready_path == '\0')
        return;

    family = LoaderPathFamilyFromPath(ready_path);
    if (!should_wait_for_config_device_family(family))
        return;

    if (LoaderPathCanAttemptNow(ready_path))
        return;

    DPRINTF("Config wait: requested='%s' waiting for device readiness path='%s'\n",
            (requested_path != NULL && *requested_path != '\0') ? requested_path : ready_path,
            ready_path);
    while (!LoaderPathCanAttemptNow(ready_path)) {
        if (poll_fn != NULL)
            poll_fn(rescue_combo_deadline);
        delay_ms(CONFIG_DEVICE_READY_POLL_INTERVAL_MS);
    }
    DPRINTF("Config wait ready: requested='%s' path='%s'\n",
            (requested_path != NULL && *requested_path != '\0') ? requested_path : ready_path,
            ready_path);
}

const char *LoaderGetResolvedConfigPath(void)
{
    return s_resolved_config_path;
}

const char *LoaderGetRequestedConfigPath(void)
{
    return s_requested_config_path;
}

int LoaderFindConfigFile(FILE **fp_out,
                         char *path_out,
                         size_t path_out_size,
                         u64 *rescue_combo_deadline,
                         LoaderEmergencyPollFn poll_fn)
{
    const char *boot_cwd_config;
    const char *boot_family_config;
    const char *boot_path_hint;
    int boot_family_source_hint;
    LoaderPathFamily boot_cwd_family;
    int boot_from_legacy_mass = 0;
    int boot_legacy_mass_unit = -1;
    const char *mc_sysconf_config_path = g_is_psx_desr
                                             ? "mc?:/SYS-CONF/PSXBBL.INI"
                                             : "mc?:/SYS-CONF/PS2BBL.INI";
#ifdef DISC_STOP_AT_BOOT
    int disc_boot_mc_fallback_profile = 0;
#endif
#ifdef MX4SIO
    int allow_mx4_on_legacy_mass = 1;
#endif
    int source;

    if (fp_out != NULL)
        *fp_out = NULL;
    if (path_out != NULL && path_out_size > 0)
        path_out[0] = '\0';
    s_resolved_config_path[0] = '\0';
    s_requested_config_path[0] = '\0';

    boot_cwd_config = LoaderGetBootCwdConfigPath();
    boot_family_config = LoaderGetBootConfigPath();
    boot_path_hint = LoaderGetBootPathHint();
    boot_family_source_hint = LoaderGetBootConfigSourceHint();
    boot_cwd_family = LoaderPathFamilyFromPath(boot_cwd_config);
#ifdef DISC_STOP_AT_BOOT
    // Disc-stop profile: do not probe disc CWD.
    // When enabled, force a deterministic startup search order that does not
    // depend on argv[0] boot hints: prefer user-mutable MC config first, then
    // immutable disc config fallback.
    disc_boot_mc_fallback_profile = 1;
#endif
    boot_from_legacy_mass = path_is_legacy_mass(boot_path_hint);
    boot_legacy_mass_unit = extract_legacy_mass_unit(boot_path_hint);
#ifdef MX4SIO
    if (boot_from_legacy_mass && boot_legacy_mass_unit >= 5) {
        allow_mx4_on_legacy_mass = 0;
        DPRINTF("Config probe: boot argv0 mass%d -> skipping MX4SIO config discovery path\n",
                boot_legacy_mass_unit);
    }
#else
    (void)boot_legacy_mass_unit;
#endif
#if defined(PSX)
    if (g_is_psx_desr) {
        if (LoaderPathFamilyFromPath(boot_path_hint) == LOADER_PATH_FAMILY_XFROM ||
            LoaderPathFamilyFromPath(boot_cwd_config) == LOADER_PATH_FAMILY_XFROM ||
            LoaderPathFamilyFromPath(boot_family_config) == LOADER_PATH_FAMILY_XFROM) {
            int xfrom_ret;

            xfrom_ret = LoaderEnsureXFromModulesLoaded();
            DPRINTF("Config probe: PSX xfrom boot preload ret=%d\n", xfrom_ret);
        }
    }
#endif

    {
        int boot_device_wait_done = 0;
        int legacy_mass_transports_primed = 0;

        for (source = 0; source < 5; source++) {
        const char *config_path = NULL;
        const char *ready_path = NULL;
        int source_hint = SOURCE_INVALID;
        int attempt = 0;
        LoaderPathFamily ready_family = LOADER_PATH_FAMILY_NONE;
        int generic_mass_boot_path = 0;
        char generic_config_path_buf[256];
        char usb_config_path_buf[256];
        char generic_secondary_path_buf[256];
        char usb_secondary_path_buf[256];
        const char *config_path_generic = NULL;
        const char *config_path_usb = NULL;
#ifdef MX4SIO
        char mx4_config_path_buf[256];
        char mx4_secondary_path_buf[256];
        const char *config_path_mx4 = NULL;
#endif
        const char *primary_candidates[8];
        int primary_count = 0;
        const char *secondary_candidates[8];
        int secondary_count = 0;
        const char *probe_candidates[16];
        int probe_count = 0;
        const char *matched_path = NULL;
        const char *report_requested_path = NULL;
        char matched_resolved_path[256];
        char resolved_path_from_match[256];
        const char *resolved_path_hint = NULL;
        char *resolved_path;
        FILE *fp = NULL;

        matched_resolved_path[0] = '\0';
        resolved_path_from_match[0] = '\0';

        if (poll_fn != NULL)
            poll_fn(rescue_combo_deadline);

#ifdef DISC_STOP_AT_BOOT
        if (disc_boot_mc_fallback_profile) {
            if (source == 0) {
                config_path = mc_sysconf_config_path;
                source_hint = SOURCE_MC0;
            } else if (source == 1) {
                config_path = "cdrom0:\\PS2BBL\\CONFIG.INI;1";
                source_hint = SOURCE_CWD;
            } else {
                continue;
            }
        } else
#endif
        {
            if (source == 0) {
                config_path = "CONFIG.INI";
                source_hint = SOURCE_CWD;
            } else if (source == 1) {
                config_path = boot_cwd_config;
                source_hint = boot_family_source_hint;
            } else if (source == 2) {
                config_path = boot_family_config;
                source_hint = boot_family_source_hint;
            } else if (source == 3) {
                config_path = mc_sysconf_config_path;
                source_hint = SOURCE_MC0;
            } else if (source == 4) {
                if (!g_is_psx_desr)
                    continue;
#if defined(PSX)
                {
                    int xfrom_ret = LoaderEnsureXFromModulesLoaded();
                    if (xfrom_ret < 0) {
                        DPRINTF("Config probe: xfrom fallback unavailable (module load ret=%d)\n", xfrom_ret);
                        continue;
                    }
                }
#endif
                config_path = "xfrom:/PS2BBL/CONFIG.INI";
#ifdef XFROM
                source_hint = SOURCE_XFROM;
#else
                source_hint = SOURCE_INVALID;
#endif
            } else {
                continue;
            }
        }

        if (config_path == NULL || *config_path == '\0')
            continue;
        report_requested_path = config_path;
        if (source == 2 && ci_eq(config_path, boot_cwd_config))
            continue;
        if (!LoaderPathFamilyReadyWithoutReload(config_path))
            continue;

        if (boot_from_legacy_mass &&
            (source == 1 || source == 2) &&
            path_is_legacy_mass(config_path) &&
            !legacy_mass_transports_primed) {
            int usb_ret;
#ifdef MX4SIO
            int mx4_ret;
#endif

            usb_ret = LoaderLoadBdmTransportsForHint("usb:/");
#ifdef MX4SIO
            if (allow_mx4_on_legacy_mass) {
                mx4_ret = LoaderLoadBdmTransportsForHint("mx4sio:/");
                DPRINTF("Config probe: legacy mass boot transport prime usb=%d mx4=%d\n",
                        usb_ret,
                        mx4_ret);
            } else {
                DPRINTF("Config probe: legacy mass boot transport prime usb=%d mx4=skipped(unit=%d)\n",
                        usb_ret,
                        boot_legacy_mass_unit);
            }
#else
            DPRINTF("Config probe: legacy mass boot transport prime usb=%d\n", usb_ret);
#endif
            legacy_mass_transports_primed = 1;
        }

        if (build_mass_generic_alias(config_path, generic_config_path_buf, sizeof(generic_config_path_buf)) &&
            !ci_eq(generic_config_path_buf, config_path))
            config_path_generic = generic_config_path_buf;
        if (build_mass_usb_alias(config_path, usb_config_path_buf, sizeof(usb_config_path_buf)) &&
            !ci_eq(usb_config_path_buf, config_path))
            config_path_usb = usb_config_path_buf;
#ifdef MX4SIO
        if (allow_mx4_on_legacy_mass) {
            if (build_mass_mx4_alias(config_path, mx4_config_path_buf, sizeof(mx4_config_path_buf)) &&
                !ci_eq(mx4_config_path_buf, config_path))
                config_path_mx4 = mx4_config_path_buf;
            if (config_path_mx4 == NULL &&
                config_path_generic != NULL &&
                build_mass_mx4_alias(config_path_generic, mx4_config_path_buf, sizeof(mx4_config_path_buf)) &&
                !ci_eq(mx4_config_path_buf, config_path_generic))
                config_path_mx4 = mx4_config_path_buf;
        }
#endif

        generic_mass_boot_path = (boot_from_legacy_mass && path_is_generic_legacy_mass(config_path));
        if (!generic_mass_boot_path) {
            primary_count = append_unique_candidate(primary_candidates, primary_count, 8, config_path);
            primary_count = append_unique_candidate(primary_candidates, primary_count, 8, config_path_generic);
        }
        if (boot_from_legacy_mass && path_is_legacy_mass(config_path)) {
            primary_count = append_unique_candidate(primary_candidates, primary_count, 8, config_path_usb);
#ifdef MX4SIO
            primary_count = append_unique_candidate(primary_candidates, primary_count, 8, config_path_mx4);
#endif
            if (generic_mass_boot_path)
                primary_count = append_unique_candidate(primary_candidates, primary_count, 8, config_path);
        }

        if (source == 1 &&
            boot_family_config != NULL &&
            *boot_family_config != '\0' &&
            !ci_eq(boot_family_config, config_path) &&
            LoaderPathFamilyReadyWithoutReload(boot_family_config)) {
            const char *secondary_generic = NULL;
            const char *secondary_usb = NULL;
#ifdef MX4SIO
            const char *secondary_mx4 = NULL;
#endif

            int secondary_generic_mass_boot_path = (boot_from_legacy_mass && path_is_generic_legacy_mass(boot_family_config));

            if (!secondary_generic_mass_boot_path)
                secondary_count = append_unique_candidate(secondary_candidates, secondary_count, 8, boot_family_config);
            if (build_mass_generic_alias(boot_family_config, generic_secondary_path_buf, sizeof(generic_secondary_path_buf)) &&
                !ci_eq(generic_secondary_path_buf, boot_family_config))
                secondary_generic = generic_secondary_path_buf;
            if (build_mass_usb_alias(boot_family_config, usb_secondary_path_buf, sizeof(usb_secondary_path_buf)) &&
                !ci_eq(usb_secondary_path_buf, boot_family_config))
                secondary_usb = usb_secondary_path_buf;
#ifdef MX4SIO
            if (allow_mx4_on_legacy_mass &&
                build_mass_mx4_alias(boot_family_config, mx4_secondary_path_buf, sizeof(mx4_secondary_path_buf)) &&
                !ci_eq(mx4_secondary_path_buf, boot_family_config))
                secondary_mx4 = mx4_secondary_path_buf;
#endif

            if (!secondary_generic_mass_boot_path)
                secondary_count = append_unique_candidate(secondary_candidates, secondary_count, 8, secondary_generic);
            if (boot_from_legacy_mass && path_is_legacy_mass(boot_family_config)) {
                secondary_count = append_unique_candidate(secondary_candidates, secondary_count, 8, secondary_usb);
#ifdef MX4SIO
                secondary_count = append_unique_candidate(secondary_candidates, secondary_count, 8, secondary_mx4);
#endif
                if (secondary_generic_mass_boot_path)
                    secondary_count = append_unique_candidate(secondary_candidates, secondary_count, 8, boot_family_config);
            }
        }

        for (attempt = 0; attempt < primary_count; attempt++)
            probe_count = append_unique_candidate(probe_candidates, probe_count, 16, primary_candidates[attempt]);
        for (attempt = 0; attempt < secondary_count; attempt++)
            probe_count = append_unique_candidate(probe_candidates, probe_count, 16, secondary_candidates[attempt]);

        if (source <= 2 && !boot_device_wait_done) {
            if (source == 0) {
                ready_path = boot_cwd_config;
                ready_family = boot_cwd_family;
                if ((ready_path == NULL || *ready_path == '\0') &&
                    boot_family_config != NULL &&
                    *boot_family_config != '\0') {
                    ready_path = boot_family_config;
                    ready_family = LoaderPathFamilyFromPath(ready_path);
                }
            } else {
                ready_path = config_path;
                ready_family = LoaderPathFamilyFromPath(ready_path);
            }

            if (should_wait_for_config_device_family(ready_family)) {
                wait_for_config_device_ready(ready_path,
                                             config_path,
                                             rescue_combo_deadline,
                                             poll_fn);
                boot_device_wait_done = 1;
            }
        }

        fp = open_first_config_candidate(probe_candidates,
                                         probe_count,
                                         &matched_path,
                                         matched_resolved_path,
                                         sizeof(matched_resolved_path));
        if (fp != NULL && matched_path != NULL) {
            config_path = matched_path;
            if (matched_resolved_path[0] != '\0') {
                snprintf(resolved_path_from_match,
                         sizeof(resolved_path_from_match),
                         "%s",
                         matched_resolved_path);
                resolved_path_hint = resolved_path_from_match;
            }
        }

        if (fp == NULL) {
            if (source <= 2)
                DPRINTF("Config miss: requested='%s'\n", config_path);
            continue;
        }

        // Keep "requested" aligned with the actual matched candidate path.
        report_requested_path = config_path;

        if (resolved_path_hint != NULL && *resolved_path_hint != '\0')
            resolved_path = (char *)resolved_path_hint;
        else
            resolved_path = CheckPath(config_path);
        if (resolved_path == NULL || *resolved_path == '\0')
            resolved_path = (char *)config_path;

        if (fp_out != NULL)
            *fp_out = fp;
        else
            fclose(fp);

        if (path_out != NULL && path_out_size > 0)
            snprintf(path_out, path_out_size, "%s", resolved_path);
        snprintf(s_requested_config_path,
                 sizeof(s_requested_config_path),
                 "%s",
                 (report_requested_path != NULL && *report_requested_path != '\0') ? report_requested_path : config_path);
        snprintf(s_resolved_config_path, sizeof(s_resolved_config_path), "%s", resolved_path);

        DPRINTF("Config found: requested='%s' resolved='%s' source_hint=%d\n",
                (report_requested_path != NULL && *report_requested_path != '\0') ? report_requested_path : config_path,
                resolved_path,
                source_hint);

        if (source_hint == SOURCE_MC0) {
            if (ci_starts_with(resolved_path, "mc1:"))
                return SOURCE_MC1;
            return SOURCE_MC0;
        }
#ifdef MMCE
        if (source_hint == SOURCE_MMCE0) {
            if (ci_starts_with(resolved_path, "mmce1:"))
                return SOURCE_MMCE1;
            return SOURCE_MMCE0;
        }
#endif
        if (source_hint == SOURCE_INVALID)
            return SOURCE_CWD;
        return source_hint;
    }
    }

    return SOURCE_INVALID;
}

int LoaderParseConfigFile(FILE *fp,
                          LoaderConfigParseResult *result,
                          LoaderParseVideoModeFn parse_video_mode_fn,
                          LoaderApplyVideoModeFn apply_video_mode_fn,
                          u64 *rescue_combo_deadline,
                          LoaderEmergencyPollFn poll_fn)
{
    LoaderConfigParseResult default_result;
    char *cnf_scan_buf;
    char *cnf_buf;
    char *name;
    char *value;
    char *scan_copy;
    int arg_counts[KEY_COUNT][CONFIG_KEY_INDEXES];
    u32 parsed_launch_key_mask = 0;
    long cnf_size;
    size_t bytes_read;

    if (result == NULL) {
        memset(&default_result, 0, sizeof(default_result));
        result = &default_result;
    } else {
        memset(result, 0, sizeof(*result));
    }

    if (fp == NULL)
        return -1;

    if (fseek(fp, 0, SEEK_END) != 0)
        goto fail;

    cnf_size = ftell(fp);
    if (cnf_size < 0)
        goto fail;

    if (fseek(fp, 0, SEEK_SET) != 0)
        goto fail;

    DPRINTF("Allocating %ld bytes for config\n", cnf_size);

    if (config_buf != NULL) {
        free(config_buf);
        config_buf = NULL;
    }

    config_buf = (unsigned char *)malloc((size_t)cnf_size + 1u);
    if (config_buf == NULL)
        goto fail;

    bytes_read = fread(config_buf, 1, (size_t)cnf_size, fp);
    fclose(fp);
    fp = NULL;

    if (bytes_read != (size_t)cnf_size) {
        DPRINTF("\tERROR: could not read %ld bytes of config file, only %u read\n", cnf_size, (unsigned int)bytes_read);
#ifdef REPORT_FATAL_ERRORS
        scr_setfontcolor(0x0000ff);
        scr_printf("\tERROR: could not read %ld bytes of config file, only %u read\n", cnf_size, (unsigned int)bytes_read);
        scr_setfontcolor(0xffffff);
#endif
        return -1;
    }

    result->read_success = 1;
    cnf_buf = (char *)config_buf;
    cnf_buf[cnf_size] = '\0';
    memset(arg_counts, 0, sizeof(arg_counts));

    scan_copy = malloc((size_t)cnf_size + 1u);
    if (scan_copy != NULL) {
        memcpy(scan_copy, config_buf, (size_t)cnf_size + 1u);
        cnf_scan_buf = scan_copy;

        while (get_CNF_string(&cnf_scan_buf, &name, &value)) {
            int key_index;
            int entry_index;

            if (poll_fn != NULL)
                poll_fn(rescue_combo_deadline);

            name = trim_ws_inplace(name);
            value = trim_ws_inplace(value);

            if (name == NULL || *name == '\0')
                continue;
            if (!ci_starts_with(name, "ARG_"))
                continue;
            if (value == NULL || *value == '\0')
                continue;
            if (!find_arg_entry_indices(name, &key_index, &entry_index))
                continue;

            arg_counts[key_index][entry_index]++;
        }

        prepare_arg_storage_from_counts(arg_counts);
        free(scan_copy);
    } else {
        DPRINTF("# Failed to allocate temporary scan buffer for exact ARG preallocation\n");
    }

    while (get_CNF_string(&cnf_buf, &name, &value)) {
        if (poll_fn != NULL)
            poll_fn(rescue_combo_deadline);

        name = trim_ws_inplace(name);
        value = trim_ws_inplace(value);

        if (name == NULL || *name == '\0')
            continue;

        if (parse_scalar_int_key(name, value))
            continue;

        if (ci_starts_with(name, "LOAD_IRX_E")) {
            int module_ret;
            int load_result;

            if (value == NULL || *value == '\0') {
                DPRINTF("# Skipping empty IRX path for config entry [%s]\n", name);
                continue;
            }
            if (LoaderEnsurePathFamilyReady(value) < 0) {
                DPRINTF("# Failed to initialize driver family for config IRX [%s] -> [%s]\n",
                        name,
                        value);
                continue;
            }

            load_result = SifLoadStartModule(CheckPath(value), 0, NULL, &module_ret);
            if (load_result < 0) {
                DPRINTF("# Failed to load IRX from config entry [%s] -> [%s]: ret=%d\n",
                        name,
                        value,
                        load_result);
            } else {
                DPRINTF("# Loaded IRX from config entry [%s] -> [%s]: ID=%d, ret=%d\n",
                        name,
                        value,
                        load_result,
                        module_ret);
            }
            continue;
        }

        if (ci_eq(name, "VIDEO_MODE")) {
            int parsed_video_mode;

            if (parse_video_mode_fn != NULL &&
                parse_video_mode_fn(value, &parsed_video_mode)) {
                GLOBCFG.VIDEO_MODE = parsed_video_mode;
                if (apply_video_mode_fn != NULL)
                    apply_video_mode_fn(parsed_video_mode);
                result->video_mode_applied = 1;
            } else {
                DPRINTF("Ignoring invalid VIDEO_MODE value '%s'\n", value);
            }
            continue;
        }

        if (ci_starts_with(name, "NAME_")) {
            int key_index = -1;

            if (parse_name_entry(name, value, &key_index) &&
                key_index >= 0 && key_index < (int)(sizeof(result->parsed_name_mask) * 8u)) {
                result->parsed_name_mask |= ((u32)1u << key_index);
            }
            continue;
        }

        if (ci_starts_with(name, "ARG_")) {
            parse_arg_entry(name, value);
            continue;
        }

        if (ci_starts_with(name, "LK_")) {
            int parse_result = parse_launch_key_entry(name, value, &parsed_launch_key_mask);

            if (parse_result == 2)
                result->has_launch_key_entries = 1;
            continue;
        }
    }

    return 0;

fail:
#ifdef REPORT_FATAL_ERRORS
    scr_setbgcolor(0x0000ff);
    scr_clear();
    scr_printf("\tFailed to load config file\n");
    delay_ms(CONFIG_FATAL_ERROR_DISPLAY_MS);
    scr_setbgcolor(0x000000);
    scr_clear();
#endif
    if (fp != NULL)
        fclose(fp);
    return -1;
}

int LoaderBootstrapConfigAndSplash(int *splash_early_presented_out,
                                   char *config_path_in_use,
                                   size_t config_path_in_use_size,
                                   int bdm_modules_loaded,
                                   int mx4sio_modules_loaded,
                                   int mmce_modules_loaded,
                                   int hdd_modules_loaded,
                                   int native_video_mode,
                                   int is_psx_desr,
                                   u64 *rescue_combo_deadline,
                                   LoaderEmergencyPollFn poll_fn,
                                   LoaderParseVideoModeFn parse_video_mode_fn,
                                   LoaderApplyVideoModeFn apply_video_mode_fn)
{
    FILE *fp = NULL;
    LoaderConfigParseResult parse_result;
    int config_source;
    int splash_early_presented = 0;
    int video_mode_applied = 0;
    int config_has_launch_key_entries = 0;
    int config_read_success = 0;
    int x, j;

    if (splash_early_presented_out != NULL)
        *splash_early_presented_out = 0;
    if (config_path_in_use != NULL && config_path_in_use_size > 0)
        config_path_in_use[0] = '\0';

    LoaderPathSetModuleStates(bdm_modules_loaded,
                              mx4sio_modules_loaded,
                              mmce_modules_loaded,
                              hdd_modules_loaded);

    config_source = LoaderFindConfigFile(&fp,
                                         config_path_in_use,
                                         config_path_in_use_size,
                                         rescue_combo_deadline,
                                         poll_fn);
    if (config_source != SOURCE_INVALID) {
        DPRINTF("valid config on device '%s', reading now\n", SOURCES[config_source]);

        // Config found: do not inherit embedded fallback LK_* defaults.
        // Only entries explicitly present in the loaded config should be usable.
        for (x = 0; x < KEY_COUNT; x++)
            for (j = 0; j < CONFIG_KEY_INDEXES; j++)
                GLOBCFG.KEYPATHS[x][j] = NULL;

        LoaderParseConfigFile(fp,
                              &parse_result,
                              parse_video_mode_fn,
                              apply_video_mode_fn,
                              rescue_combo_deadline,
                              poll_fn);
        config_read_success = parse_result.read_success;
        config_has_launch_key_entries = parse_result.has_launch_key_entries;
        video_mode_applied = parse_result.video_mode_applied;
#ifdef HDD
        if (config_source == SOURCE_HDD) {
            if (fileXioUmount("pfs0:") < 0)
                DPRINTF("ERROR: Could not unmount 'pfs0:'\n");
        }
#endif
        GLOBCFG.LOGO_DISP = normalize_logo_display(GLOBCFG.LOGO_DISP);
        GLOBCFG.HOTKEY_DISPLAY = logo_to_hotkey_display(GLOBCFG.LOGO_DISP);
        // For LOGO_DISPLAY=3 (name mode), show only NAME_* entries that were
        // explicitly present in the loaded config. Missing/commented NAME_*
        // keys should render as blank instead of falling back to built-ins.
        if (config_read_success && GLOBCFG.HOTKEY_DISPLAY == 1) {
            for (x = 0; x < KEY_COUNT; x++) {
                int has_explicit_name = 0;

                if (x < (int)(sizeof(parse_result.parsed_name_mask) * 8u))
                    has_explicit_name = ((parse_result.parsed_name_mask & ((u32)1u << x)) != 0);

                if (!has_explicit_name || GLOBCFG.KEYNAMES[x] == NULL)
                    GLOBCFG.KEYNAMES[x] = "";
            }
        }
        // If config parsing did not produce a valid VIDEO_MODE, fall back to
        // the default AUTO/native mode now.
        if (!video_mode_applied && apply_video_mode_fn != NULL)
            apply_video_mode_fn(GLOBCFG.VIDEO_MODE);
        if (config_read_success && !config_has_launch_key_entries)
            LoaderRunEmergencyMode("CONFIG FILE HAS NO LAUNCH KEY ENTRIES");

        // Show splash immediately after video mode is known so users can read it
        // before launch workflow begins. For LOGO_DISPLAY=3, skip the transient
        // Loading... overlay so the first visible hotkey frame is the final
        // NAME_* splash/countdown render.
        if (GLOBCFG.LOGO_DISP > 0) {
            int show_loading_overlay = (GLOBCFG.HOTKEY_DISPLAY != 1);

            SplashRenderSetVideoMode(GLOBCFG.VIDEO_MODE, native_video_mode);
            SplashRenderTextBody(GLOBCFG.LOGO_DISP, is_psx_desr);
            if (show_loading_overlay) {
                SplashDrawLoadingStatus(GLOBCFG.LOGO_DISP);
                splash_early_presented = 1;
            }
        }

        LoaderApplyDisplayNameMode(GLOBCFG.HOTKEY_DISPLAY);
    } else {
        const char *no_config_status = "Can't find config, loading hardcoded paths";
        char **default_paths = DEFPATH;
        const DefaultLaunchArgEntry *default_args = DEFAULT_LAUNCH_ARGS;
        int default_arg_count = DEFAULT_LAUNCH_ARGS_COUNT;

#if defined(PSX)
        if (!is_psx_desr) {
            default_paths = DEFPATH_PS2;
            default_args = DEFAULT_LAUNCH_ARGS_PS2;
            default_arg_count = DEFAULT_LAUNCH_ARGS_PS2_COUNT;
        }
#endif
        for (x = 0; x < KEY_COUNT; x++)
            for (j = 0; j < CONFIG_KEY_INDEXES; j++)
                GLOBCFG.KEYPATHS[x][j] = default_paths[CONFIG_KEY_INDEXES * x + j];
        set_fallback_entry_args(default_args, default_arg_count);

        GLOBCFG.LOGO_DISP = normalize_logo_display(LOGO_DISPLAY_DEFAULT);
        GLOBCFG.HOTKEY_DISPLAY = logo_to_hotkey_display(GLOBCFG.LOGO_DISP);
        // No config means no valid VIDEO_MODE was parsed, so apply the default
        // AUTO/native mode now.
        if (!video_mode_applied && apply_video_mode_fn != NULL)
            apply_video_mode_fn(GLOBCFG.VIDEO_MODE);

        // Keep fallback path consistent: show a quick loading overlay once
        // video mode is selected (AUTO/native by default). For LOGO_DISPLAY=3,
        // skip the transient Loading... overlay so the first visible hotkey
        // frame is the final NAME_* splash/countdown render.
        if (GLOBCFG.LOGO_DISP > 0) {
            int show_loading_overlay = (GLOBCFG.HOTKEY_DISPLAY != 1);

            SplashRenderSetVideoMode(GLOBCFG.VIDEO_MODE, native_video_mode);
            SplashRenderTextBody(GLOBCFG.LOGO_DISP, is_psx_desr);
            SplashDrawCenteredStatusWithInfo(no_config_status,
                                             0xffff00,
                                             "",
                                             "",
                                             "",
                                             "",
                                             NULL,
                                             "");
            // Centered status temporarily hides hotkeys; restore visibility for
            // LOGO_DISPLAY=3 so countdown/hotkey UI keeps the banner image.
            SplashRenderSetHotkeysVisible(GLOBCFG.LOGO_DISP >= 3);
            splash_early_presented = 1;
            if (show_loading_overlay) {
                SplashDrawLoadingStatus(GLOBCFG.LOGO_DISP);
                splash_early_presented = 1;
            }
        }

        LoaderApplyDisplayNameMode(GLOBCFG.HOTKEY_DISPLAY);
    }

    if (splash_early_presented_out != NULL)
        *splash_early_presented_out = splash_early_presented;

    return config_source;
}
