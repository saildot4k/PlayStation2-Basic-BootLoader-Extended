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

static int parse_name_entry(const char *name, char *value)
{
    int key_index;
    char key_name[32];

    for (key_index = 0; key_index < KEY_COUNT; key_index++) {
        snprintf(key_name, sizeof(key_name), "NAME_%s", KEYS_ID[key_index]);
        if (ci_eq(name, key_name)) {
            if (value == NULL || *value == '\0')
                GLOBCFG.KEYNAMES[key_index] = NULL;
            else
                GLOBCFG.KEYNAMES[key_index] = value;
            return 1;
        }
    }

    return 0;
}

static int parse_arg_entry(const char *name, char *value)
{
    int key_index;
    int entry_index;
    char key_name[32];

    for (key_index = 0; key_index < KEY_COUNT; key_index++) {
        for (entry_index = 0; entry_index < CONFIG_KEY_INDEXES; entry_index++) {
            snprintf(key_name, sizeof(key_name), "ARG_%s_E%d", KEYS_ID[key_index], entry_index + 1);
            if (!ci_eq(name, key_name))
                continue;

            if (value == NULL || *value == '\0')
                return 1;

            if (GLOBCFG.KEYARGC[key_index][entry_index] < MAX_ARGS_PER_ENTRY) {
                GLOBCFG.KEYARGS[key_index][entry_index][GLOBCFG.KEYARGC[key_index][entry_index]] = value;
                GLOBCFG.KEYARGC[key_index][entry_index]++;
            } else {
                DPRINTF("# Too many args for [%s], max=%d\n", name, MAX_ARGS_PER_ENTRY);
            }

            return 1;
        }
    }

    return 0;
}

static int parse_launch_key_entry(const char *name, char *value)
{
    int key_index;
    int entry_index;
    char key_name[32];

    for (key_index = 0; key_index < KEY_COUNT; key_index++) {
        for (entry_index = 0; entry_index < CONFIG_KEY_INDEXES; entry_index++) {
            snprintf(key_name, sizeof(key_name), "LK_%s_E%d", KEYS_ID[key_index], entry_index + 1);
            if (!ci_eq(name, key_name))
                continue;

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
    int dev_ok[LOADER_DEVICE_COUNT];
    int source;

    if (fp_out != NULL)
        *fp_out = NULL;
    if (path_out != NULL && path_out_size > 0)
        path_out[0] = '\0';

    // Devices are expected to remain stable during runtime, so capture this once.
    LoaderBuildDeviceAvailableCache(dev_ok);

    for (source = SOURCE_CWD; source >= SOURCE_MC0; source--) {
        char *resolved_path;
        FILE *fp;
        const char *config_path = CONFIG_PATHS[source];

        if (poll_fn != NULL)
            poll_fn(rescue_combo_deadline);

#if defined(PSX)
        if (!g_is_psx_desr && source == SOURCE_XCONFIG)
            continue;
#endif

        if (config_path == NULL || *config_path == '\0')
            continue;
        if (!LoaderDeviceAvailableForPathCached(config_path, dev_ok))
            continue;

        resolved_path = CheckPath(config_path);
        if (resolved_path == NULL || *resolved_path == '\0')
            continue;
        fp = fopen(resolved_path, "r");
        if (fp == NULL)
            continue;

        if (fp_out != NULL)
            *fp_out = fp;
        else
            fclose(fp);

        if (path_out != NULL && path_out_size > 0)
            snprintf(path_out, path_out_size, "%s", resolved_path);

        return source;
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
    char *cnf_buf;
    char *name;
    char *value;
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
            parse_name_entry(name, value);
            continue;
        }

        if (ci_starts_with(name, "ARG_")) {
            parse_arg_entry(name, value);
            continue;
        }

        if (ci_starts_with(name, "LK_")) {
            int parse_result = parse_launch_key_entry(name, value);

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
