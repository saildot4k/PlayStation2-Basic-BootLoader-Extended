// Main launch countdown loop, hotkey dispatch, and AUTO fallback handling.
#include "main.h"
#include "console_info.h"
#include "loader_launch.h"
#include "loader_path.h"
#include "splash_render.h"
#include "splash_screen.h"

#define PAD_MASK_ANY 0xffff

int LoaderRunLaunchWorkflow(int splash_early_presented,
                            int pre_scanned,
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
    int dev_ok[LOADER_DEVICE_COUNT];
    int x, j, button;
    u64 deadline;
    ConsoleInfo console_info;
    char autoboot_text[48];
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

    if (!SplashRenderIsActive())
        SplashRenderTextBody(GLOBCFG.LOGO_DISP, is_psx_desr);

    ConsoleInfoCapture(&console_info, config_source, romver, romver_size);
    model = console_info.model;
    ps1ver = console_info.ps1ver;
    dvdver = console_info.dvdver;
    source = console_info.source;
    temp_celsius = ConsoleInfoRefreshTemperature(&console_info);

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

        deadline = Timer() + GLOBCFG.DELAY;
        LoaderBuildDeviceAvailableCache(dev_ok);
        while (Timer() <= deadline) {
            u64 now = Timer();
            int pad_state;

            if (poll_emergency_combo_window != NULL)
                poll_emergency_combo_window(rescue_combo_deadline);

            if (SplashRenderIsActive()) {
                const char *render_temp = ConsoleInfoRefreshTemperature(&console_info);
                u64 remaining_ms = (now <= deadline) ? (deadline - now) : 0;
                unsigned int remaining_sec = (unsigned int)(remaining_ms / 1000u);
                unsigned int remaining_tenths = (unsigned int)((remaining_ms % 1000u) / 100u);

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
                        int is_command;

                        // Skip empty/unset entries (common when config has blank LK_* values)
                        if (entry_path == NULL || *entry_path == '\0')
                            continue;

                        is_command = (entry_path[0] == '$');
                        if (pre_scanned && !is_command) {
                            execpaths[j] = CheckPath(entry_path);
                            if (execpaths[j] == NULL || *execpaths[j] == '\0')
                                continue;
                            ShowLaunchStatus(execpaths[j]);
                            CleanUp();
                            RunLoaderElf(execpaths[j], MPART, GLOBCFG.KEYARGC[x + 1][j], GLOBCFG.KEYARGS[x + 1][j]);
                            break;
                        }

                        if (!LoaderDeviceAvailableForPathCached(entry_path, dev_ok))
                            continue;

                        if (is_command) {
                            ShowLaunchStatus(entry_path);
                            LoaderPathSetPendingCommandArgs(GLOBCFG.KEYARGC[x + 1][j], GLOBCFG.KEYARGS[x + 1][j]);
                        }

                        execpaths[j] = CheckPath(entry_path);

                        if (is_command) {
                            LoaderPathSetPendingCommandArgs(0, NULL);
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

                        if (!LoaderAllowVirtualPatinfoEntry(x + 1, j, execpaths[j]) && !exist(execpaths[j])) {
                            scr_setfontcolor(0x00ffff);
                            DPRINTF("%s not found\n", execpaths[j]);
                            scr_setfontcolor(0xffffff);
                            continue;
                        }

                        if (execpaths[j] != NULL && *execpaths[j] != '\0') {
                            ShowLaunchStatus(execpaths[j]);
                            CleanUp();
                            RunLoaderElf(execpaths[j], MPART, GLOBCFG.KEYARGC[x + 1][j], GLOBCFG.KEYARGS[x + 1][j]);
                        }
                    }
                    if (!command_cancelled) {
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
        LoaderBuildDeviceAvailableCache(dev_ok);

        for (j = 0; j < CONFIG_KEY_INDEXES; j++) {
            const char *entry_path = GLOBCFG.KEYPATHS[0][j];
            int is_command;

            // Skip empty/unset AUTO entries too
            if (entry_path == NULL || *entry_path == '\0')
                continue;

            is_command = (entry_path[0] == '$');
            if (is_command)
                continue; // Don't execute commands without a key press.
            if (pre_scanned) {
                execpaths[j] = CheckPath(entry_path);
                if (execpaths[j] == NULL || *execpaths[j] == '\0')
                    continue;
                ShowLaunchStatus(execpaths[j]);
                CleanUp();
                RunLoaderElf(execpaths[j], MPART, GLOBCFG.KEYARGC[0][j], GLOBCFG.KEYARGS[0][j]);
                break;
            }

            if (!LoaderDeviceAvailableForPathCached(entry_path, dev_ok))
                continue;

            execpaths[j] = CheckPath(entry_path);
            if (!LoaderAllowVirtualPatinfoEntry(0, j, execpaths[j]) && !exist(execpaths[j])) {
                scr_printf("%s %-15s\r", execpaths[j], "not found");
                continue;
            }

            if (execpaths[j] != NULL && *execpaths[j] != '\0') {
                ShowLaunchStatus(execpaths[j]);
                CleanUp();
                RunLoaderElf(execpaths[j], MPART, GLOBCFG.KEYARGC[0][j], GLOBCFG.KEYARGS[0][j]);
            }
        }

        {
            int retry_requested;

            TimerInit();
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
