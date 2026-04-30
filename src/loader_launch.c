// Main launch countdown loop, hotkey dispatch, and AUTO fallback handling.
#include "main.h"
#include "console_info.h"
#include "loader_launch.h"
#include "loader_path.h"
#include "splash_render.h"
#include "splash_screen.h"

#define PAD_MASK_ANY 0xffff
// Default fast poll cadence for most launch families.
#define LAUNCH_PATH_WAIT_STEP_DEFAULT_MS 50u
// Coarser poll cadence for MX4SIO legacy massX/mx4sio mounts.
#define LAUNCH_PATH_WAIT_STEP_MX4SIO_MS 1000u
#define LAUNCH_PATH_WAIT_BDM_MS 5000u
#define LAUNCH_PATH_WAIT_MX4SIO_MS 3000u
#define LAUNCH_PATH_WAIT_MMCE_MS 2000u
#define LAUNCH_PATH_WAIT_HDD_MS 5000u

static void EnsurePadsReadyForInput(void)
{
    if (!PadIsInitialized())
        PadInitPads();
}

static unsigned int launch_wait_timeout_for_path(const char *entry_path, int wait_for_mount)
{
    LoaderPathFamily family;

    if (!wait_for_mount || entry_path == NULL || *entry_path == '\0')
        return 0;

    family = LoaderPathFamilyFromPath(entry_path);
    if (family == LOADER_PATH_FAMILY_XFROM)
        family = LOADER_PATH_FAMILY_MC;

    switch (family) {
        case LOADER_PATH_FAMILY_BDM:
#ifdef MX4SIO
            if (ci_starts_with(entry_path, "massX:") || ci_starts_with(entry_path, "mx4sio"))
                return LAUNCH_PATH_WAIT_MX4SIO_MS;
#endif
            return LAUNCH_PATH_WAIT_BDM_MS;
        case LOADER_PATH_FAMILY_MX4SIO:
            return LAUNCH_PATH_WAIT_MX4SIO_MS;
        case LOADER_PATH_FAMILY_MMCE:
            return LAUNCH_PATH_WAIT_MMCE_MS;
        case LOADER_PATH_FAMILY_HDD_APA:
            return LAUNCH_PATH_WAIT_HDD_MS;
        default:
            return 0;
    }
}

static unsigned int launch_wait_step_for_path(const char *entry_path, int wait_for_mount)
{
    LoaderPathFamily family;

    if (!wait_for_mount || entry_path == NULL || *entry_path == '\0')
        return LAUNCH_PATH_WAIT_STEP_DEFAULT_MS;

    family = LoaderPathFamilyFromPath(entry_path);
    if (family == LOADER_PATH_FAMILY_XFROM)
        family = LOADER_PATH_FAMILY_MC;

    switch (family) {
        case LOADER_PATH_FAMILY_BDM:
#ifdef MX4SIO
            if (ci_starts_with(entry_path, "massX:") || ci_starts_with(entry_path, "mx4sio"))
                return LAUNCH_PATH_WAIT_STEP_MX4SIO_MS;
#endif
            return LAUNCH_PATH_WAIT_STEP_DEFAULT_MS;
        case LOADER_PATH_FAMILY_MX4SIO:
            return LAUNCH_PATH_WAIT_STEP_MX4SIO_MS;
        default:
            return LAUNCH_PATH_WAIT_STEP_DEFAULT_MS;
    }
}

static int ResolveLaunchPathForEntry(const char *entry_path,
                                     int key_index,
                                     int entry_index,
                                     char **resolved_path,
                                     int wait_for_mount)
{
    unsigned int timeout_ms;
    unsigned int step_ms;
    u64 wait_deadline_ms = 0;
    char *candidate = NULL;
    int logged_wait = 0;

    if (entry_path == NULL || *entry_path == '\0' || resolved_path == NULL)
        return -1;

    timeout_ms = launch_wait_timeout_for_path(entry_path, wait_for_mount);
    step_ms = launch_wait_step_for_path(entry_path, wait_for_mount);

    if (timeout_ms > 0)
        wait_deadline_ms = Timer() + timeout_ms;
    if (step_ms == 0)
        step_ms = LAUNCH_PATH_WAIT_STEP_DEFAULT_MS;

    while (1) {
        if (LoaderPathCanAttemptNow(entry_path)) {
            candidate = CheckPath(entry_path);
            if (candidate != NULL && *candidate != '\0') {
                if (LoaderAllowVirtualPatinfoEntry(key_index, entry_index, candidate) || exist(candidate)) {
                    *resolved_path = candidate;
                    return 0;
                }
            }
        }

        if (timeout_ms == 0 || Timer() >= wait_deadline_ms)
            break;

        if (!logged_wait) {
            DPRINTF("Launch wait: path='%s' timeout=%u ms\n", entry_path, timeout_ms);
            logged_wait = 1;
        }

        {
            u64 now_ms = Timer();
            unsigned int sleep_ms = step_ms;

            if (now_ms < wait_deadline_ms) {
                u64 remaining_ms = wait_deadline_ms - now_ms;

                if (remaining_ms < (u64)sleep_ms)
                    sleep_ms = (unsigned int)remaining_ms;
            }

            if (sleep_ms > 0)
                usleep(sleep_ms * 1000u);
        }
    }

    if (candidate != NULL && *candidate != '\0')
        *resolved_path = candidate;

    if (logged_wait) {
        DPRINTF("Launch wait timeout: path='%s' last='%s'\n",
                entry_path,
                (candidate != NULL && *candidate != '\0') ? candidate : "<none>");
    }

    return -1;
}

static int PrepareLaunchPathForExec(const char *entry_path,
                                    int key_index,
                                    int entry_index,
                                    char **resolved_path,
                                    int show_not_found_line)
{
    int prep_result;
    LoaderPathFamily target_family;
    char *rechecked_path = NULL;

    if (entry_path == NULL || *entry_path == '\0' || resolved_path == NULL)
        return -1;
    if (*resolved_path == NULL || **resolved_path == '\0')
        return -1;

    prep_result = LoaderPrepareFinalLaunch(entry_path);
    if (prep_result < 0)
        return -1;
    if (prep_result == 0)
        return 0;

    target_family = LoaderPathFamilyFromPath(entry_path);
    if (target_family == LOADER_PATH_FAMILY_XFROM)
        target_family = LOADER_PATH_FAMILY_MC;

    if (target_family == LOADER_PATH_FAMILY_MC) {
        // Avoid additional MC path probing immediately after sanitize reboot.
        // Launch using the already-resolved mcN path.
        DPRINTF("Launch sanitize ready (MC): '%s'\n", *resolved_path);
        return 0;
    }

    // We just rebooted/reloaded for a clean launch.
    // Resolve and validate the same entry once more before execution.
    if (ResolveLaunchPathForEntry(entry_path, key_index, entry_index, &rechecked_path, 1) < 0) {
        const char *not_found_path = (rechecked_path != NULL && *rechecked_path != '\0') ? rechecked_path : entry_path;

        if (show_not_found_line) {
            scr_printf("%s %-15s\r", not_found_path, "not found");
        } else {
            scr_setfontcolor(0x00ffff);
            DPRINTF("%s not found after launch sanitize\n", not_found_path);
            scr_setfontcolor(0xffffff);
        }
        return -1;
    }

    *resolved_path = rechecked_path;
    return 0;
}

int LoaderRunLaunchWorkflow(int splash_early_presented,
                            int *hotkey_launches_enabled,
                            int *block_hotkeys_until_release,
                            int pad_button,
                            int num_buttons,
                            int is_psx_desr,
                            int config_source,
                            int native_video_mode,
                            const u8 *romver,
                            size_t romver_size,
                            char **execpaths,
                            u64 *rescue_combo_deadline,
                            LoaderPollEmergencyComboWindowFn poll_emergency_combo_window)
{
    (void)native_video_mode;
    int x, j, button;
    u64 deadline;
    ConsoleInfo console_info;
    char autoboot_text[48];
    int console_info_ready = 0;
    const char *model;
    const char *ps1ver;
    const char *dvdver;
    const char *source;
    const char *temp_celsius;
    const char *hotkey_lines[KEY_COUNT] = {
        GLOBCFG.KEYNAMES[AUTO],
        GLOBCFG.KEYNAMES[TRIANGLE],
        GLOBCFG.KEYNAMES[CIRCLE],
        GLOBCFG.KEYNAMES[CROSS],
        GLOBCFG.KEYNAMES[SQUARE],
        GLOBCFG.KEYNAMES[UP],
        GLOBCFG.KEYNAMES[DOWN],
        GLOBCFG.KEYNAMES[LEFT],
        GLOBCFG.KEYNAMES[RIGHT],
        GLOBCFG.KEYNAMES[L1],
        GLOBCFG.KEYNAMES[L2],
        GLOBCFG.KEYNAMES[L3],
        GLOBCFG.KEYNAMES[R1],
        GLOBCFG.KEYNAMES[R2],
        GLOBCFG.KEYNAMES[R3],
        GLOBCFG.KEYNAMES[SELECT],
        GLOBCFG.KEYNAMES[START],
    };

    if (hotkey_launches_enabled == NULL || block_hotkeys_until_release == NULL ||
        romver == NULL || romver_size == 0 || execpaths == NULL)
        return 1;

    model = "";
    ps1ver = "";
    dvdver = "";
    source = "";
    temp_celsius = NULL;

    if (!SplashRenderIsActive())
        SplashRenderTextBody(GLOBCFG.LOGO_DISP, is_psx_desr);

    if (GLOBCFG.LOGO_DISP > 0 || SplashRenderIsActive()) {
        ConsoleInfoCapture(&console_info, config_source, romver, romver_size);
        model = console_info.model;
        ps1ver = console_info.ps1ver;
        dvdver = console_info.dvdver;
        source = console_info.source;
        console_info_ready = 1;
    }

    if (GLOBCFG.LOGO_DISP > 0) {
        if (GLOBCFG.LOGO_DISP == 1) {
            SplashRenderConsoleInfoLine(GLOBCFG.LOGO_DISP,
                                        model,
                                        console_info.rom_fmt,
                                        dvdver,
                                        ps1ver,
                                        temp_celsius,
                                        "",
                                        source);
        }
    } else {
        console_info.rom_fmt[0] = '\0';
    }

    if (SplashRenderIsActive()) {
        int pass_count = splash_early_presented ? 1 : 2;
        int pass;

        if (!console_info_ready) {
            ConsoleInfoCapture(&console_info, config_source, romver, romver_size);
            model = console_info.model;
            ps1ver = console_info.ps1ver;
            dvdver = console_info.dvdver;
            source = console_info.source;
            console_info_ready = 1;
        }

        for (pass = 0; pass < pass_count; pass++) {
            SplashRenderBeginFrame();
            SplashRenderHotkeyLines(GLOBCFG.LOGO_DISP, hotkey_lines);
            SplashRenderConsoleInfoLine(GLOBCFG.LOGO_DISP,
                                        model,
                                        console_info.rom_fmt,
                                        dvdver,
                                        ps1ver,
                                        temp_celsius,
                                        "",
                                        source);
            SplashRenderHotkeyClockDate(GLOBCFG.LOGO_DISP, 0);
            SplashRenderPresent();
        }
    }

    while (1) {
        DPRINTF("Timer starts!\n");
        if (poll_emergency_combo_window != NULL)
            poll_emergency_combo_window(rescue_combo_deadline);
        LoaderDiscStopPoll();

        deadline = Timer() + GLOBCFG.DELAY;
        while (Timer() <= deadline) {
            u64 now = Timer();
            int pad_state;

            if (poll_emergency_combo_window != NULL)
                poll_emergency_combo_window(rescue_combo_deadline);
            LoaderDiscStopPoll();

            if (SplashRenderIsActive()) {
                const char *render_temp;
                u64 remaining_ms = (now <= deadline) ? (deadline - now) : 0;
                unsigned int remaining_sec = (unsigned int)(remaining_ms / 1000u);
                unsigned int remaining_tenths = (unsigned int)((remaining_ms % 1000u) / 100u);

                if (!console_info_ready) {
                    ConsoleInfoCapture(&console_info, config_source, romver, romver_size);
                    model = console_info.model;
                    ps1ver = console_info.ps1ver;
                    dvdver = console_info.dvdver;
                    source = console_info.source;
                    console_info_ready = 1;
                }
                render_temp = ConsoleInfoRefreshTemperature(&console_info);

                snprintf(autoboot_text, sizeof(autoboot_text), "%02u.%u", remaining_sec, remaining_tenths);
                if (GLOBCFG.DELAY > 0)
                    SplashRenderSetLogoShimmerCountdown(remaining_ms, (u64)GLOBCFG.DELAY);
                SplashRenderBeginFrame();
                SplashRenderHotkeyLines(GLOBCFG.LOGO_DISP, hotkey_lines);
                SplashRenderConsoleInfoLine(GLOBCFG.LOGO_DISP,
                                            model,
                                            console_info.rom_fmt,
                                            dvdver,
                                            ps1ver,
                                            render_temp,
                                            autoboot_text,
                                            source);
                SplashRenderHotkeyClockDate(GLOBCFG.LOGO_DISP, now);
                SplashRenderPresent();
            }

            button = pad_button; // reset the value so we can iterate (bit-shift) again
            EnsurePadsReadyForInput();
            pad_state = ReadCombinedPadStatus_raw();
            if (!*hotkey_launches_enabled)
                continue;

            if (*block_hotkeys_until_release) {
                if (pad_state & PAD_MASK_ANY)
                    continue;
                *block_hotkeys_until_release = 0;
            }

            for (x = 0; x < num_buttons; x++) { // check all pad buttons
                if (pad_state & button) {
                    int command_cancelled = 0;
                    int retry_requested = 0;
                    const char *button_name = KEYS_ID[x + 1];
                    DPRINTF("PAD detected\n");
                    // if button detected, copy path to corresponding index
                    for (j = 0; j < CONFIG_KEY_INDEXES; j++) {
                        const char *entry_path = GLOBCFG.KEYPATHS[x + 1][j];
                        int ensure_family_result = 0;
                        int is_command;

                        // Skip empty/unset entries (common when config has blank LK_* values)
                        if (entry_path == NULL || *entry_path == '\0')
                            continue;

                        is_command = LoaderPathIsCommandToken(entry_path);
                        if (is_command) {
                            ShowLaunchStatus(entry_path);
                            LoaderPathSetPendingCommandAutoMode(0);
                            LoaderPathSetPendingCommandArgs(GLOBCFG.KEYARGC[x + 1][j], GLOBCFG.KEYARGS[x + 1][j]);
                        } else {
                            ensure_family_result = LoaderEnsurePathFamilyReady(entry_path);
                            if (ensure_family_result < 0)
                                continue;
                        }

                        if (!is_command &&
                            !LoaderPathCanAttemptNow(entry_path) &&
                            !(ensure_family_result > 0 &&
                              launch_wait_timeout_for_path(entry_path, 1) > 0)) {
                            continue;
                        }

                        if (is_command)
                            execpaths[j] = CheckPath(entry_path);

                        if (is_command) {
                            LoaderPathSetPendingCommandArgs(0, NULL);
                            LoaderPathSetPendingCommandAutoMode(0);
                            if (LoaderPathConsumeCdvdCancelled()) {
                                command_cancelled = 1;
                                if (SplashRenderIsActive())
                                    RestoreSplashInteractiveUi(GLOBCFG.LOGO_DISP,
                                                               hotkey_lines,
                                                               model,
                                                               console_info.rom_fmt,
                                                               dvdver,
                                                               ps1ver,
                                                               temp_celsius,
                                                               source);
                                else
                                    scr_clear();
                                *block_hotkeys_until_release = 1;
                                break;
                            }
                            continue;
                        }

                        execpaths[j] = NULL;
                        if (ResolveLaunchPathForEntry(entry_path,
                                                      x + 1,
                                                      j,
                                                      &execpaths[j],
                                                      (ensure_family_result > 0)) < 0) {
                            const char *not_found_path = (execpaths[j] != NULL && *execpaths[j] != '\0') ? execpaths[j] : entry_path;
                            scr_setfontcolor(0x00ffff);
                            DPRINTF("%s not found\n", not_found_path);
                            scr_setfontcolor(0xffffff);
                            continue;
                        }

                        if (execpaths[j] != NULL && *execpaths[j] != '\0') {
                            if (PrepareLaunchPathForExec(entry_path, x + 1, j, &execpaths[j], 0) < 0)
                                continue;
                            ShowLaunchStatus(execpaths[j]);
                            CleanUp();
                            RunLoaderElf(execpaths[j], MPART, GLOBCFG.KEYARGC[x + 1][j], GLOBCFG.KEYARGS[x + 1][j]);
                        }
                    }
                    if (!command_cancelled) {
                        EnsurePadsReadyForInput();
                        retry_requested = WaitForMissingPathAction(button_name,
                                                                   model,
                                                                   console_info.rom_fmt,
                                                                   dvdver,
                                                                   ps1ver,
                                                                   temp_celsius,
                                                                   source);
                        if (retry_requested) {
                            if (SplashRenderIsActive())
                                RestoreSplashInteractiveUi(GLOBCFG.LOGO_DISP,
                                                           hotkey_lines,
                                                           model,
                                                           console_info.rom_fmt,
                                                           dvdver,
                                                           ps1ver,
                                                           temp_celsius,
                                                           source);
                            else
                                scr_clear();
                            *block_hotkeys_until_release = 1;
                        }
                    }
                    if (command_cancelled || retry_requested)
                        deadline = Timer() + GLOBCFG.DELAY;
                    break;
                }
                button = button << 1; // sll of 1 cleared bit to move to next pad button
            }
        }
        if (SplashRenderIsActive())
            SplashRenderEnd();

        DPRINTF("Wait time consummed. Running AUTO entry\n");
        if (rescue_combo_deadline != NULL)
            *rescue_combo_deadline = 0;
        TimerEnd();

        for (j = 0; j < CONFIG_KEY_INDEXES; j++) {
            const char *entry_path = GLOBCFG.KEYPATHS[0][j];
            int ensure_family_result = 0;
            int is_command;

            // Skip empty/unset AUTO entries too
            if (entry_path == NULL || *entry_path == '\0')
                continue;

            is_command = LoaderPathIsCommandToken(entry_path);
            if (is_command) {
                ShowLaunchStatus(entry_path);
                LoaderPathSetPendingCommandAutoMode(1);
                LoaderPathSetPendingCommandArgs(GLOBCFG.KEYARGC[0][j], GLOBCFG.KEYARGS[0][j]);
                execpaths[j] = CheckPath(entry_path);
                LoaderPathSetPendingCommandArgs(0, NULL);
                LoaderPathSetPendingCommandAutoMode(0);
                if (LoaderPathConsumeCdvdCancelled())
                    continue;
                continue;
            }
            ensure_family_result = LoaderEnsurePathFamilyReady(entry_path);
            if (ensure_family_result < 0)
                continue;
            if (!LoaderPathCanAttemptNow(entry_path) &&
                !(ensure_family_result > 0 &&
                  launch_wait_timeout_for_path(entry_path, 1) > 0))
                continue;

            execpaths[j] = NULL;
            if (ResolveLaunchPathForEntry(entry_path,
                                          0,
                                          j,
                                          &execpaths[j],
                                          (ensure_family_result > 0)) < 0) {
                const char *not_found_path = (execpaths[j] != NULL && *execpaths[j] != '\0') ? execpaths[j] : entry_path;
                scr_printf("%s %-15s\r", not_found_path, "not found");
                continue;
            }

            if (execpaths[j] != NULL && *execpaths[j] != '\0') {
                if (PrepareLaunchPathForExec(entry_path, 0, j, &execpaths[j], 1) < 0)
                    continue;
                ShowLaunchStatus(execpaths[j]);
                CleanUp();
                RunLoaderElf(execpaths[j], MPART, GLOBCFG.KEYARGC[0][j], GLOBCFG.KEYARGS[0][j]);
            }
        }

        {
            int retry_requested;

            TimerInit();
            EnsurePadsReadyForInput();
            retry_requested = WaitForMissingPathAction("AUTO",
                                                       model,
                                                       console_info.rom_fmt,
                                                       dvdver,
                                                       ps1ver,
                                                       temp_celsius,
                                                       source);
            if (retry_requested) {
                if (SplashRenderIsActive())
                    RestoreSplashInteractiveUi(GLOBCFG.LOGO_DISP,
                                               hotkey_lines,
                                               model,
                                               console_info.rom_fmt,
                                               dvdver,
                                               ps1ver,
                                               temp_celsius,
                                               source);
                else
                    scr_clear();
                *block_hotkeys_until_release = 1;
                if (rescue_combo_deadline != NULL)
                    *rescue_combo_deadline = 0;
                continue;
            }
            TimerEnd();
        }

        return 1;
    }
}
