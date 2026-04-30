//  command handler and disc-type boot dispatch with splash status updates.
#include <stdint.h>
#include <string.h>

#include "main.h"
#include "egsm_parse.h"
#include "game_id.h"
#include "console_info.h"
#include "splash_render.h"

extern u8 ROMVER[16];

static void AlarmCallback(s32 alarm_id, u16 time, void *common)
{
    (void)alarm_id;
    (void)time;
    iWakeupThread((int)common);
}

static int GetDiscConfigHintFromSource(int source)
{
#ifdef HDD
    if (source == SOURCE_HDD)
        return PS2_DISC_HINT_HDD;
#endif
#ifdef MMCE
    if (source == SOURCE_MMCE1)
        return PS2_DISC_HINT_MC1;
#endif
    if (source == SOURCE_MC1)
        return PS2_DISC_HINT_MC1;
    return PS2_DISC_HINT_MC0;
}

typedef struct
{
    uint32_t egsm_flags;
    const char *egsm_arg;
    int skip_ps2logo;
    int disable_gameid;
    int ps1drv_fast;
    int ps1drv_smooth;
    int ps1drv_use_ps1vn;
} DiscCommandOptions;

static void ParseDiscCommandOptions(int argc, char *argv[], DiscCommandOptions *options_out)
{
    int i;

    if (options_out != NULL)
        memset(options_out, 0, sizeof(*options_out));

    if (options_out == NULL || argc <= 0 || argv == NULL)
        return;

    for (i = 0; i < argc; i++) {
        const char *arg = argv[i];

        if (arg == NULL || arg[0] != '-')
            continue;

        arg++;
        while (*arg == ' ' || *arg == '\t')
            arg++;
        if (*arg == '\0')
            continue;

        if (ci_eq(arg, "nologo")) {
            options_out->skip_ps2logo = 1;
            continue;
        }
        if (ci_eq(arg, "nogameid")) {
            options_out->disable_gameid = 1;
            continue;
        }
        if (ci_eq(arg, "disc_stop")) {
            DPRINTF("Ignoring unsupported CDVD command option '-disc_stop' (disc launch requires media online)\n");
            continue;
        }
        if (ci_eq(arg, "ps1fast")) {
            options_out->ps1drv_fast = 1;
            continue;
        }
        if (ci_eq(arg, "ps1smooth")) {
            options_out->ps1drv_smooth = 1;
            continue;
        }
        if (ci_eq(arg, "ps1vneg")) {
            options_out->ps1drv_use_ps1vn = 1;
            continue;
        }
        if (ci_eq(arg, "dkwdrv") || ci_starts_with(arg, "dkwdrv=")) {
            DPRINTF("Ignoring unsupported CDVD command option '-%s' (DKWDRV path launch is not implemented)\n",
                    arg);
            continue;
        }
        if (ci_starts_with(arg, "gsm=")) {
            const char *value;
            uint32_t flags;

            value = arg + 4;
            while (*value == ' ' || *value == '\t')
                value++;

            if (*value == '\0')
                continue;

            flags = parse_egsm_flags_common(value);
            if (flags == 0) {
                DPRINTF("Ignoring invalid disc command -gsm value '%s'\n", value);
                continue;
            }

            options_out->egsm_flags = flags;
            options_out->egsm_arg = value;
        }
    }
}

int dischandler(int skip_ps2logo, int argc, char *argv[], int wait_for_disc)
{
    int old_disc_type, disc_type, valid_disc_inserted, result, first_run = 1;
    int cancel_requested = 0;
    int start_was_down;
    u64 start_hold_deadline = 0;
    int prompt_dots = 0;
    u64 prompt_next_tick = 0;
    int use_splash_ui;
    int config_source = LoaderGetConfigSource();
    ConsoleInfo console_info;
    const char *model = "";
    const char *ps1ver = "";
    const char *dvdver = "";
    const char *source = "";
    const char *temp_celsius = NULL;
    const char *egsm_override_arg = NULL;
    DiscCommandOptions disc_options;
    char disc_status[64];
    uint32_t egsm_override_flags = 0;
    int effective_skip_ps2logo;
    int effective_cdrom_disable_gameid;
    int effective_ps1drv_enable_fast;
    int effective_ps1drv_enable_smooth;
    int effective_ps1drv_use_ps1vn;
    u32 stat;

    ParseDiscCommandOptions(argc, argv, &disc_options);
    egsm_override_flags = disc_options.egsm_flags;
    egsm_override_arg = disc_options.egsm_arg;
    if (egsm_override_flags != 0)
        DPRINTF("%s: using command -gsm override '%s' flags=0x%08x\n",
                __func__, egsm_override_arg, (unsigned int)egsm_override_flags);

    effective_skip_ps2logo = (skip_ps2logo != 0) ||
                             (disc_options.skip_ps2logo != 0);
    effective_cdrom_disable_gameid = (GLOBCFG.CDROM_DISABLE_GAMEID != 0) || (disc_options.disable_gameid != 0);
    effective_ps1drv_enable_fast = (disc_options.ps1drv_fast != 0);
    effective_ps1drv_enable_smooth = (disc_options.ps1drv_smooth != 0);
    effective_ps1drv_use_ps1vn = (disc_options.ps1drv_use_ps1vn != 0);

    if (!sceCdInit(SCECdINIT))
        DPRINTF("%s: sceCdInit(SCECdINIT) failed, continuing with current CDVD state\n", __func__);

    use_splash_ui = SplashRenderIsActive();

    if (use_splash_ui) {
        ConsoleInfoCapture(&console_info, config_source, ROMVER, sizeof(ROMVER));
        model = console_info.model;
        ps1ver = console_info.ps1ver;
        dvdver = console_info.dvdver;
        source = console_info.source;
        temp_celsius = ConsoleInfoRefreshTemperature(&console_info);
        SplashDrawCenteredStatusWithInfo(wait_for_disc ? "CDVD: Waiting for disc (START=Back)" : "CDVD: Checking disc (AUTO)",
                                         0x00ffff,
                                         model,
                                         console_info.rom_fmt,
                                         dvdver,
                                         ps1ver,
                                         temp_celsius,
                                         source);
    } else {
        scr_clear();
        scr_printf("\n\t%s: Activated\n", __func__);
        scr_printf("\t\tEnabling Diagnosis...\n");
    }

    do {
        result = sceCdAutoAdjustCtrl(0, &stat);
    } while ((stat & 0x08) || (result == 0));

    if (!use_splash_ui) {
        if (wait_for_disc)
            scr_printf("\tWaiting for disc to be inserted...\n\n");
        else
            scr_printf("\tChecking disc for AUTO launch...\n\n");
    }

    valid_disc_inserted = 0;
    old_disc_type = -1;
    start_was_down = (ReadCombinedPadStatus_raw() & PAD_START) ? 1 : 0;
    if (start_was_down)
        start_hold_deadline = Timer() + 150;
    while (!valid_disc_inserted) {
        int pad_state = ReadCombinedPadStatus_raw();
        if (pad_state & PAD_START) {
            if (!start_was_down) {
                cancel_requested = 1;
                break;
            }
            if (start_hold_deadline != 0 && Timer() >= start_hold_deadline) {
                cancel_requested = 1;
                break;
            }
        } else {
            start_was_down = 0;
            start_hold_deadline = 0;
        }

        disc_type = sceCdGetDiskType();
        if (disc_type != old_disc_type) {
            old_disc_type = disc_type;

            switch (disc_type) {
                case SCECdNODISC:
                    if (first_run) {
                        if (GLOBCFG.TRAYEJECT)
                            sceCdTrayReq(0, NULL);
                        first_run = 0;
                    }
                    if (use_splash_ui) {
                        prompt_dots = 0;
                        prompt_next_tick = Timer() + 1000;
                        SplashDrawRetryPromptWithInfo("NO DISC FOUND!",
                                                      0xffff00,
                                                      prompt_dots,
                                                      model,
                                                      console_info.rom_fmt,
                                                      dvdver,
                                                      ps1ver,
                                                      temp_celsius,
                                                      source);
                    } else {
                        scr_printf("\tNew Disc:\t");
                        scr_setfontcolor(0x0000ff);
                        scr_printf("No Disc\n");
                        scr_setfontcolor(0xffffff);
                    }
                    if (!wait_for_disc) {
                        DPRINTF("%s: AUTO disc command skipped (no disc)\n", __func__);
                        return -1;
                    }
                    break;

                case SCECdDETCT:
                case SCECdDETCTCD:
                case SCECdDETCTDVDS:
                case SCECdDETCTDVDD:
                    prompt_next_tick = 0;
                    if (use_splash_ui)
                        SplashDrawCenteredStatusWithInfo("Reading Disc...",
                                                         0xffffff,
                                                         model,
                                                         console_info.rom_fmt,
                                                         dvdver,
                                                         ps1ver,
                                                         temp_celsius,
                                                         source);
                    else {
                        scr_printf("\tNew Disc:\t");
                        scr_printf("Reading...\n");
                    }
                    break;

                case SCECdPSCD:
                case SCECdPSCDDA:
                    prompt_next_tick = 0;
                    if (use_splash_ui)
                        SplashDrawCenteredStatusWithInfo("PlayStation Disc",
                                                         0x00ff00,
                                                         model,
                                                         console_info.rom_fmt,
                                                         dvdver,
                                                         ps1ver,
                                                         temp_celsius,
                                                         source);
                    else {
                        scr_printf("\tNew Disc:\t");
                        scr_setfontcolor(0x00ff00);
                        scr_printf("PlayStation\n");
                        scr_setfontcolor(0xffffff);
                    }
                    valid_disc_inserted = 1;
                    break;

                case SCECdPS2CD:
                case SCECdPS2CDDA:
                case SCECdPS2DVD:
                    prompt_next_tick = 0;
                    if (use_splash_ui)
                        SplashDrawCenteredStatusWithInfo("PlayStation 2 Disc",
                                                         0x00ff00,
                                                         model,
                                                         console_info.rom_fmt,
                                                         dvdver,
                                                         ps1ver,
                                                         temp_celsius,
                                                         source);
                    else {
                        scr_printf("\tNew Disc:\t");
                        scr_setfontcolor(0x00ff00);
                        scr_printf("PlayStation 2\n");
                        scr_setfontcolor(0xffffff);
                    }
                    valid_disc_inserted = 1;
                    break;

                case SCECdCDDA:
                    if (use_splash_ui) {
                        prompt_dots = 0;
                        prompt_next_tick = Timer() + 1000;
                        SplashDrawRetryPromptWithInfo("Audio Disc Not Supported",
                                                      0xffff00,
                                                      prompt_dots,
                                                      model,
                                                      console_info.rom_fmt,
                                                      dvdver,
                                                      ps1ver,
                                                      temp_celsius,
                                                      source);
                    } else {
                        scr_printf("\tNew Disc:\t");
                        scr_setfontcolor(0xffff00);
                        scr_printf("Audio Disc (not supported by this program)\n");
                        scr_setfontcolor(0xffffff);
                    }
                    if (!wait_for_disc) {
                        DPRINTF("%s: AUTO disc command skipped (unsupported audio disc)\n", __func__);
                        return -1;
                    }
                    break;

                case SCECdDVDV:
                    prompt_next_tick = 0;
                    if (use_splash_ui)
                        SplashDrawCenteredStatusWithInfo("DVD Video",
                                                         0x00ff00,
                                                         model,
                                                         console_info.rom_fmt,
                                                         dvdver,
                                                         ps1ver,
                                                         temp_celsius,
                                                         source);
                    else {
                        scr_printf("\tNew Disc:\t");
                        scr_setfontcolor(0x00ff00);
                        scr_printf("DVD Video\n");
                        scr_setfontcolor(0xffffff);
                    }
                    valid_disc_inserted = 1;
                    break;

                default:
                    if (use_splash_ui) {
                        snprintf(disc_status, sizeof(disc_status), "Unknown Disc (%d)", disc_type);
                        prompt_dots = 0;
                        prompt_next_tick = Timer() + 1000;
                        SplashDrawRetryPromptWithInfo(disc_status,
                                                      0x8080ff,
                                                      prompt_dots,
                                                      model,
                                                      console_info.rom_fmt,
                                                      dvdver,
                                                      ps1ver,
                                                      temp_celsius,
                                                      source);
                    } else {
                        scr_printf("\tNew Disc:\t");
                        scr_setfontcolor(0x0000ff);
                        scr_printf("Unknown (%d)\n", disc_type);
                        scr_setfontcolor(0xffffff);
                    }
                    if (!wait_for_disc) {
                        DPRINTF("%s: AUTO disc command skipped (unknown disc type %d)\n", __func__, disc_type);
                        return -1;
                    }
                    break;
            }
        }

        if (use_splash_ui && prompt_next_tick != 0 && Timer() >= prompt_next_tick) {
            switch (disc_type) {
                case SCECdNODISC:
                    prompt_dots = (prompt_dots + 1) % 4;
                    prompt_next_tick = Timer() + 1000;
                    SplashDrawRetryPromptWithInfo("NO DISC FOUND!",
                                                  0xffff00,
                                                  prompt_dots,
                                                  model,
                                                  console_info.rom_fmt,
                                                  dvdver,
                                                  ps1ver,
                                                  temp_celsius,
                                                  source);
                    break;
                case SCECdCDDA:
                    prompt_dots = (prompt_dots + 1) % 4;
                    prompt_next_tick = Timer() + 1000;
                    SplashDrawRetryPromptWithInfo("Audio Disc Not Supported",
                                                  0xffff00,
                                                  prompt_dots,
                                                  model,
                                                  console_info.rom_fmt,
                                                  dvdver,
                                                  ps1ver,
                                                  temp_celsius,
                                                  source);
                    break;
                case SCECdDETCT:
                case SCECdDETCTCD:
                case SCECdDETCTDVDS:
                case SCECdDETCTDVDD:
                case SCECdPSCD:
                case SCECdPSCDDA:
                case SCECdPS2CD:
                case SCECdPS2CDDA:
                case SCECdPS2DVD:
                case SCECdDVDV:
                    break;
                default:
                    prompt_dots = (prompt_dots + 1) % 4;
                    prompt_next_tick = Timer() + 1000;
                    snprintf(disc_status, sizeof(disc_status), "Unknown Disc (%d)", disc_type);
                    SplashDrawRetryPromptWithInfo(disc_status,
                                                  0x8080ff,
                                                  prompt_dots,
                                                  model,
                                                  console_info.rom_fmt,
                                                  dvdver,
                                                  ps1ver,
                                                  temp_celsius,
                                                  source);
                    break;
            }
        }

        SetAlarm(1000 * 16, &AlarmCallback, (void *)GetThreadId());
        SleepThread();
    }

    if (cancel_requested) {
        if (use_splash_ui)
            SplashDrawCenteredStatusWithInfo("CDVD Canceled",
                                             0xffff00,
                                             model,
                                             console_info.rom_fmt,
                                             dvdver,
                                             ps1ver,
                                             temp_celsius,
                                             source);
        else {
            scr_setfontcolor(0xffff00);
            scr_printf("\tCDVD canceled\n");
            scr_setfontcolor(0xffffff);
        }

        while (ReadCombinedPadStatus_raw() & PAD_START) {
            SetAlarm(16 * 16, &AlarmCallback, (void *)GetThreadId());
            SleepThread();
        }
        SetAlarm(80 * 16, &AlarmCallback, (void *)GetThreadId());
        SleepThread();

        return -1;
    }

    if (use_splash_ui) {
        switch (disc_type) {
            case SCECdPSCD:
            case SCECdPSCDDA:
                SplashDrawCenteredStatusWithInfo("Booting PlayStation Disc...",
                                                 0x00ff00,
                                                 model,
                                                 console_info.rom_fmt,
                                                 dvdver,
                                                 ps1ver,
                                                 temp_celsius,
                                                 source);
                break;
            case SCECdPS2CD:
            case SCECdPS2CDDA:
            case SCECdPS2DVD:
                SplashDrawCenteredStatusWithInfo("Booting PlayStation 2 Disc...",
                                                 0x00ff00,
                                                 model,
                                                 console_info.rom_fmt,
                                                 dvdver,
                                                 ps1ver,
                                                 temp_celsius,
                                                 source);
                break;
            case SCECdDVDV:
                SplashDrawCenteredStatusWithInfo("Booting DVD Video...",
                                                 0x00ff00,
                                                 model,
                                                 console_info.rom_fmt,
                                                 dvdver,
                                                 ps1ver,
                                                 temp_celsius,
                                                 source);
                break;
        }
    }

    GameIDSetConfig(GLOBCFG.APP_GAMEID, effective_cdrom_disable_gameid);
    PS1DRVSetOptions(effective_ps1drv_enable_fast,
                     effective_ps1drv_enable_smooth,
                     effective_ps1drv_use_ps1vn);

    switch (disc_type) {
        case SCECdPSCD:
        case SCECdPSCDDA:
            PS1DRVBoot();
            break;

        case SCECdPS2CD:
        case SCECdPS2CDDA:
        case SCECdPS2DVD:
            PS2DiscSetConfigHint(GetDiscConfigHintFromSource(config_source));
            PS2DiscBoot(effective_skip_ps2logo, egsm_override_flags, egsm_override_arg);
            break;

        case SCECdDVDV:
            DVDPlayerBoot();
            break;
    }

    GameIDSetConfig(GLOBCFG.APP_GAMEID, GLOBCFG.CDROM_DISABLE_GAMEID);
    PS1DRVSetOptions(0, 0, 0);

    return 0;
}
