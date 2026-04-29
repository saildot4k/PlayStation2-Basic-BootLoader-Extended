//  command handler and disc-type boot dispatch with splash status updates.
#include <stdint.h>
#include <string.h>

#include "main.h"
#include "egsm_parse.h"
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

static void ParseDiscEgsmOverride(int argc, char *argv[], uint32_t *flags_out, const char **arg_out)
{
    int i;

    if (flags_out != NULL)
        *flags_out = 0;
    if (arg_out != NULL)
        *arg_out = NULL;

    if (argc <= 0 || argv == NULL)
        return;

    for (i = 0; i < argc; i++) {
        const char *value;
        uint32_t flags;

        if (argv[i] == NULL || strncmp(argv[i], "-gsm=", 5) != 0)
            continue;

        value = argv[i] + 5;
        while (*value == ' ' || *value == '\t')
            value++;

        if (*value == '\0')
            continue;

        flags = parse_egsm_flags_common(value);
        if (flags == 0) {
            DPRINTF("Ignoring invalid disc command -gsm value '%s'\n", value);
            continue;
        }

        if (flags_out != NULL)
            *flags_out = flags;
        if (arg_out != NULL)
            *arg_out = value;
    }
}

int dischandler(int skip_ps2logo, int argc, char *argv[])
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
    char disc_status[64];
    uint32_t egsm_override_flags = 0;
    u32 stat;

    ParseDiscEgsmOverride(argc, argv, &egsm_override_flags, &egsm_override_arg);
    if (egsm_override_flags != 0)
        DPRINTF("%s: using command -gsm override '%s' flags=0x%08x\n",
                __func__, egsm_override_arg, (unsigned int)egsm_override_flags);

    use_splash_ui = SplashRenderIsActive();

    if (use_splash_ui) {
        ConsoleInfoCapture(&console_info, config_source, ROMVER, sizeof(ROMVER));
        model = console_info.model;
        ps1ver = console_info.ps1ver;
        dvdver = console_info.dvdver;
        source = console_info.source;
        temp_celsius = ConsoleInfoRefreshTemperature(&console_info);
        SplashDrawCenteredStatusWithInfo("CDVD: Waiting for disc (START=Back)",
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

    if (!use_splash_ui)
        scr_printf("\tWaiting for disc to be inserted...\n\n");

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

    switch (disc_type) {
        case SCECdPSCD:
        case SCECdPSCDDA:
            PS1DRVBoot();
            break;

        case SCECdPS2CD:
        case SCECdPS2CDDA:
        case SCECdPS2DVD:
            PS2DiscSetConfigHint(GetDiscConfigHintFromSource(config_source));
            PS2DiscBoot(skip_ps2logo, egsm_override_flags, egsm_override_arg);
            break;

        case SCECdDVDV:
            DVDPlayerBoot();
            break;
    }

    return 0;
}
