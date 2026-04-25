// Config discovery, parsing, and bootstrap flow (defaults + splash preparation).
#include <stdint.h>

#include "main.h"
#include "loader_config.h"
#include "loader_path.h"
#include "splash_render.h"
#include "splash_screen.h"

extern unsigned char *config_buf;
extern int g_is_psx_desr;

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
    {"PS1DRV_ENABLE_FAST", offsetof(CONFIG, PS1DRV_ENABLE_FAST)},
    {"PS1DRV_ENABLE_SMOOTH", offsetof(CONFIG, PS1DRV_ENABLE_SMOOTH)},
    {"PS1DRV_USE_PS1VN", offsetof(CONFIG, PS1DRV_USE_PS1VN)},
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

int LoaderFindConfigFile(FILE **fp_out,
                         char *path_out,
                         size_t path_out_size,
                         u64 *rescue_combo_deadline,
                         LoaderEmergencyPollFn poll_fn)
{
    const char *boot_cwd_config;
    const char *boot_family_config;
    int boot_family_source_hint;
    int source;

    if (fp_out != NULL)
        *fp_out = NULL;
    if (path_out != NULL && path_out_size > 0)
        path_out[0] = '\0';

    boot_cwd_config = LoaderGetBootCwdConfigPath();
    boot_family_config = LoaderGetBootConfigPath();
    boot_family_source_hint = LoaderGetBootConfigSourceHint();

    for (source = 0; source < 5; source++) {
        const char *config_path = NULL;
        int source_hint = SOURCE_INVALID;
        int attempt = 0;
        int max_attempts = 1;
        unsigned int retry_delay_us = 0;
        char *resolved_path;
        FILE *fp = NULL;

        if (poll_fn != NULL)
            poll_fn(rescue_combo_deadline);

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
            config_path = "mc?:/SYS-CONF/PS2BBL.INI";
            source_hint = SOURCE_MC0;
        } else {
            config_path = "mc?:/SYS-CONF/PSXBBL.INI";
            source_hint = SOURCE_MC0;
        }

        if (config_path == NULL || *config_path == '\0')
            continue;
        if (source == 2 && ci_eq(config_path, boot_cwd_config))
            continue;
        if (!LoaderPathFamilyReadyWithoutReload(config_path))
            continue;

        // Boot-family devices (especially USB/BDM) can appear shortly after
        // modules load. Retry briefly before falling through to next location.
        if (source == 1 || source == 2) {
            LoaderPathFamily family = LoaderPathFamilyFromPath(config_path);

            if (family == LOADER_PATH_FAMILY_BDM) {
                max_attempts = 30;
                retry_delay_us = 100000;
            } else if (family == LOADER_PATH_FAMILY_MMCE ||
                       family == LOADER_PATH_FAMILY_MX4SIO) {
                max_attempts = 15;
                retry_delay_us = 100000;
            }
        }

        for (attempt = 0; attempt < max_attempts; attempt++) {
            if (poll_fn != NULL)
                poll_fn(rescue_combo_deadline);

            resolved_path = CheckPath(config_path);
            if (resolved_path != NULL && *resolved_path != '\0')
                fp = fopen(resolved_path, "r");
            if (fp != NULL)
                break;

            if (attempt + 1 < max_attempts && retry_delay_us > 0)
                usleep(retry_delay_us);
        }

        if (fp == NULL)
            continue;

        if (fp_out != NULL)
            *fp_out = fp;
        else
            fclose(fp);

        if (path_out != NULL && path_out_size > 0)
            snprintf(path_out, path_out_size, "%s", resolved_path);

        DPRINTF("Config found: requested='%s' resolved='%s' source_hint=%d\n",
                config_path,
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
    sleep(3);
    scr_setbgcolor(0x000000);
    scr_clear();
#endif
    if (fp != NULL)
        fclose(fp);
    return -1;
}

int LoaderBootstrapConfigAndSplash(int *pre_scanned_out,
                                   int *splash_early_presented_out,
                                   char *config_path_in_use,
                                   size_t config_path_in_use_size,
                                   int usb_modules_loaded,
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
    int pre_scanned = 0;
    int splash_early_presented = 0;
    int video_mode_applied = 0;
    int config_has_launch_key_entries = 0;
    int config_read_success = 0;
    int x, j;

    if (pre_scanned_out != NULL)
        *pre_scanned_out = 0;
    if (splash_early_presented_out != NULL)
        *splash_early_presented_out = 0;
    if (config_path_in_use != NULL && config_path_in_use_size > 0)
        config_path_in_use[0] = '\0';

    LoaderPathSetModuleStates(usb_modules_loaded,
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
        pre_scanned = (GLOBCFG.HOTKEY_DISPLAY == 2 || GLOBCFG.HOTKEY_DISPLAY == 3);
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
        // while path validation runs. For LOGO_DISPLAY=3, skip the transient
        // Loading... overlay so the first visible hotkey frame is the final
        // NAME_* splash/countdown render.
        if (GLOBCFG.LOGO_DISP > 0) {
            int show_loading_overlay = (GLOBCFG.HOTKEY_DISPLAY != 1);

            if (GLOBCFG.HOTKEY_DISPLAY == 2 || GLOBCFG.HOTKEY_DISPLAY == 3) {
                for (x = 0; x < KEY_COUNT; x++)
                    GLOBCFG.KEYNAMES[x] = "";
            }
            SplashRenderSetVideoMode(GLOBCFG.VIDEO_MODE, native_video_mode);
            SplashRenderTextBody(GLOBCFG.LOGO_DISP, is_psx_desr);
            if (show_loading_overlay) {
                SplashDrawLoadingStatus(GLOBCFG.LOGO_DISP);
                splash_early_presented = 1;
            }
        }

        ValidateKeypathsAndSetNames(GLOBCFG.HOTKEY_DISPLAY, pre_scanned);
    } else {
        const char *no_config_status = "Can't find config, loading hardcoded paths";
        char **default_paths = DEFPATH;

#if defined(PSX)
        if (!is_psx_desr)
            default_paths = DEFPATH_PS2;
#endif
        for (x = 0; x < KEY_COUNT; x++)
            for (j = 0; j < CONFIG_KEY_INDEXES; j++)
                GLOBCFG.KEYPATHS[x][j] = default_paths[CONFIG_KEY_INDEXES * x + j];

        GLOBCFG.LOGO_DISP = normalize_logo_display(LOGO_DISPLAY_DEFAULT);
        GLOBCFG.HOTKEY_DISPLAY = logo_to_hotkey_display(GLOBCFG.LOGO_DISP);
        pre_scanned = (GLOBCFG.HOTKEY_DISPLAY == 2 || GLOBCFG.HOTKEY_DISPLAY == 3);
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

            if (GLOBCFG.HOTKEY_DISPLAY == 2 || GLOBCFG.HOTKEY_DISPLAY == 3) {
                for (x = 0; x < KEY_COUNT; x++)
                    GLOBCFG.KEYNAMES[x] = "";
            }
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
            splash_early_presented = 1;
            if (show_loading_overlay && !pre_scanned) {
                SplashDrawLoadingStatus(GLOBCFG.LOGO_DISP);
                splash_early_presented = 1;
            }
        }

        ValidateKeypathsAndSetNames(GLOBCFG.HOTKEY_DISPLAY, pre_scanned);
    }

    if (pre_scanned_out != NULL)
        *pre_scanned_out = pre_scanned;
    if (splash_early_presented_out != NULL)
        *splash_early_presented_out = splash_early_presented;

    return config_source;
}
