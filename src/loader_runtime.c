// Runtime utilities (ROM detection, cleanup, emergency/rescue mode).
#include "main.h"
#include "loader_path.h"
#include "splash_render.h"
#include "splash_screen.h"

#define ROMVER_MODEL_PREFIX_LEN 5
#define CREDITS_POLL_INTERVAL_MS 50u
#define EMERGENCY_RESCUE_POLL_INTERVAL_MS 100u

extern u8 ROMVER[16];
extern int g_is_psx_desr;
extern int g_native_video_mode;

static const char *const g_psx_desr_rom_prefixes[] = {
    "0180J",
    "0210J",
};

static int ROMVERMatchesAnyPrefix(const u8 romver[16], const char *const *prefixes, size_t prefix_count)
{
    size_t i;

    if (romver == NULL || prefixes == NULL)
        return 0;

    for (i = 0; i < prefix_count; i++) {
        if (!strncmp((const char *)romver, prefixes[i], ROMVER_MODEL_PREFIX_LEN))
            return 1;
    }

    return 0;
}

static int IsPSXDESRROMVER(const u8 romver[16])
{
    return ROMVERMatchesAnyPrefix(romver,
                                  g_psx_desr_rom_prefixes,
                                  sizeof(g_psx_desr_rom_prefixes) / sizeof(g_psx_desr_rom_prefixes[0]));
}

void ReadROMVEROnce(void)
{
    int fd;
    int nread;

    memset(ROMVER, 0, sizeof(ROMVER));
    if ((fd = open("rom0:ROMVER", O_RDONLY)) >= 0) {
        nread = read(fd, ROMVER, sizeof(ROMVER));
        close(fd);
        if (nread != (int)sizeof(ROMVER))
            memset(ROMVER, 0, sizeof(ROMVER));
    }

    g_is_psx_desr = IsPSXDESRROMVER(ROMVER);
}

void LogDetectedPlatform(void)
{
    char rom_prefix[ROMVER_MODEL_PREFIX_LEN + 1];
    const char *platform_name;

#if defined(PSX)
    platform_name = g_is_psx_desr ? "PSX-DESR" : "PS2";
#else
    platform_name = "PS2";
#endif
    (void)platform_name;

    if (ROMVER[0] != '\0') {
        memcpy(rom_prefix, ROMVER, ROMVER_MODEL_PREFIX_LEN);
        rom_prefix[ROMVER_MODEL_PREFIX_LEN] = '\0';
    } else {
        strcpy(rom_prefix, "N/A");
    }

    DPRINTF("Detected platform: %s (ROMVER prefix: %s)\n", platform_name, rom_prefix);
}

void CleanUp(void)
{
    if (SplashRenderIsActive())
        SplashRenderEnd();

    sceCdInit(SCECdEXIT);
    // Keep config_buf alive so argv pointers remain valid during ELF load.
    PadDeinitPads();
}

static void DrawCreditsSplashFrame(void)
{
    const char *lines[] = {
        "PLAYSTATION2 BASIC BOOTLOADER EXTENDED",
        "BY MATIAS ISRAELSON (EL_ISRA). EXTENDED BY R3Z3N and AI",
        "BASED ON SP193 OSD INIT SAMPLES",
        "THANKS: FJTRUJY, UYJULIAN, ASMBLUR, AKUHAK, PCM720, BERION, RIPTO, NUNO, GHOSTTOWNUS",
        "v"VERSION"."SUBVERSION"."PATCHLEVEL" - "STATUS"",
        "Compiled on "__DATE__"",  
        "PRESS START TO RETURN TO LAUNCH KEYS",
    };
    const u32 colors[] = {
        0x00ffff,
        0xffffff,
        0xffffff,
        0xffffff,
        0x15d670,
    };
    int screen_w = SplashRenderGetScreenWidth();
    int screen_h = SplashRenderGetScreenHeight();
    int logo_x = SplashRenderGetLogoX();
    int logo_y = SplashRenderGetLogoY();
    int logo_w = SplashRenderGetLogoWidth();
    int logo_h = SplashRenderGetLogoHeight();
    int anchor_center_x = screen_w / 2;
    int y = screen_h / 2;
    int i;

    if (logo_x >= 0 && logo_y >= 0 && logo_w > 0 && logo_h > 0) {
        anchor_center_x = logo_x + (logo_w / 2);
        y = logo_y + logo_h + 6;
    }

    if (y > screen_h - 70)
        y = screen_h - 70;
    if (y < 8)
        y = 8;

    SplashRenderSetHotkeysVisible(0);
    SplashRenderBeginFrame();
    for (i = 0; i < (int)(sizeof(lines) / sizeof(lines[0])); i++) {
        int line_w = (int)strlen(lines[i]) * 6;
        int x = anchor_center_x - (line_w / 2);

        if (x < 8)
            x = 8;
        if (x + line_w > screen_w - 8)
            x = screen_w - line_w - 8;
        if (x < 8)
            x = 8;

        SplashRenderDrawTextPxScaled(x, y + (i * 16), colors[i], lines[i], 1);
    }
    SplashRenderPresent();
}

int credits(void)
{
    int use_splash = SplashRenderIsActive();
    int prev_pad = ReadCombinedPadStatus_raw();

    scr_clear();
    scr_printf("\n\n");
    if (!use_splash) {
        scr_printf("\tPlayStation2 Basic BootLoader Extended\n");
        scr_printf("\tBy Matias Israelson (AKA: El_isra). Extended by R3Z3N and AI\n");
        scr_printf("\thttps://ps2homebrewstore.com - Modified - 9 Paths\n");
        scr_printf("\tv" VERSION "-" SUBVERSION "-" PATCHLEVEL " - " STATUS
#ifdef DEBUG
                   " - DEBUG"
#endif
                   "\n");
        scr_printf("\n"
                   "\n"
                   "\tBased on SP193 OSD Init samples.\n"
                   "\t\tall credits go to him\n"
                   "\tThanks to: fjtrujy, uyjulian, asmblur, AKuHAK, PCM720, BERION, RIPTO\n"
                   "\tbuild hash [" COMMIT_HASH "]\n"
                   "\t\tcompiled on "__DATE__
                   " "__TIME__
                   "\n"
#ifdef MX4SIO
                   " MX4SIO"
#endif
#ifdef HDD
                   " HDD "
#endif
                   "\n"
                   "\n"
                   "\tPress START to return to Launch Keys.\n");
    }

    while (1) {
        int pad = ReadCombinedPadStatus_raw();

        if (use_splash)
            DrawCreditsSplashFrame();
        if (!(prev_pad & PAD_START) && (pad & PAD_START))
            break;

        prev_pad = pad;
        delay_ms(CREDITS_POLL_INTERVAL_MS);
    }

    return 1;
}

void runOSDNoUpdate(void)
{
    char *args[3] = {"SkipHdd", "BootBrowser", "SkipMc"};
    CleanUp();
    SifExitCmd();
    ExecOSD(3, args);
}

void LoaderRunEmergencyMode(const char *reason)
{
    const char *rescue_path = "mass:/RESCUE.ELF";
    int rescue_family_ready = 0;

    if (!SplashRenderIsActive()) {
        int emergency_logo_disp = normalize_logo_display(GLOBCFG.LOGO_DISP);

        if (emergency_logo_disp < 1)
            emergency_logo_disp = 1;
        SplashRenderSetVideoMode(GLOBCFG.VIDEO_MODE, g_native_video_mode);
        SplashRenderTextBody(emergency_logo_disp, g_is_psx_desr);
    }

    SplashDrawEmergencyModeStatus(reason);
    while (1) {
        char *resolved_rescue_path = NULL;

        delay_ms(EMERGENCY_RESCUE_POLL_INTERVAL_MS);

        // Match launch-entry behavior: ensure BDM family is active first.
        // If booted on MC/MMCE/HDD, this may reboot IOP once to load USB stack.
        if (!rescue_family_ready) {
            int ensure_ready = LoaderEnsurePathFamilyReady(rescue_path);

            if (ensure_ready < 0) {
                DPRINTF("Emergency mode: failed to ready path family for '%s' (ret=%d), retrying\n",
                        rescue_path,
                        ensure_ready);
                continue;
            }

            rescue_family_ready = 1;
            DPRINTF("Emergency mode: path family ready for '%s'\n", rescue_path);
        }

        resolved_rescue_path = CheckPath(rescue_path);
        if (resolved_rescue_path != NULL &&
            *resolved_rescue_path != '\0' &&
            exist(resolved_rescue_path)) {
            if (SplashRenderIsActive())
                SplashRenderEnd();
            CleanUp();
            RunLoaderElf(resolved_rescue_path, NULL, 0, NULL);
        }
    }
}

void EMERGENCY(void)
{
    LoaderRunEmergencyMode(NULL);
}
