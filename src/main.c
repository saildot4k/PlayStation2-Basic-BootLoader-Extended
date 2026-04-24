// Program entrypoint: orchestrates initialization, config bootstrap, and launch flow.
#include <stdint.h>

#include "main.h"
#include "game_id.h"
#include "console_info.h"
#include "loader_config.h"
#include "loader_launch.h"
#include "loader_video.h"
#include "loader_video_selector.h"
#include "splash_render.h"

int g_is_psx_desr = 0;

#define PAD_MASK_TRIANGLE 0x1000
#define PAD_MASK_CROSS 0x4000

int g_native_video_mode = CFG_VIDEO_MODE_NTSC;

// --------------- glob stuff --------------- //
CONFIG GLOBCFG;
static int g_pre_scanned = 0;
static int g_usb_modules_loaded = 0;
static int g_mx4sio_modules_loaded = 0;
static int g_mmce_modules_loaded = 0;
static int g_hdd_modules_loaded = 0;
static int config_source = SOURCE_INVALID;

int LoaderGetConfigSource(void)
{
    return config_source;
}

char *EXECPATHS[CONFIG_KEY_INDEXES];
u8 ROMVER[16];
unsigned char *config_buf = NULL; // pointer to allocated config file
static int g_video_mode_selector_requested = 0;
static int g_block_hotkeys_until_release = 0;
static int g_hotkey_launches_enabled = 1;
static char g_config_path_in_use[256] = "";

#define RESCUE_COMBO_WINDOW_MS 2000

static void PollEmergencyComboWindow(u64 *window_deadline_ms)
{
    int pad_state;

    if (window_deadline_ms == NULL || *window_deadline_ms == 0)
        return;

    if (Timer() > *window_deadline_ms) {
        *window_deadline_ms = 0;
        return;
    }

    // Keep emergency combos responsive during long-running setup steps.
    pad_state = ReadCombinedPadStatus_raw();
    if ((pad_state & PAD_R1) && (pad_state & PAD_START))
        EMERGENCY();
    if ((pad_state & PAD_MASK_TRIANGLE) && (pad_state & PAD_MASK_CROSS))
        g_video_mode_selector_requested = 1;
}

int main(int argc, char *argv[])
{
    u32 STAT;
    u64 rescue_combo_deadline = 0;
    int result;
    int splash_early_presented = 0;

    // Early runtime bring-up: establish IOP/RPC state and debug output.
    LoaderPlatformClearStaleEEDebugState();
    ReadROMVEROnce();
    ResetIOP();
    SifInitIopHeap(); // Initialize SIF services for loading modules and files.
    SifLoadFileInit();
    fioInit(); // NO scr_printf BEFORE here
    init_scr();
    scr_setCursor(0); // get rid of annoying that cursor.
    DPRINTF_INIT()
#ifndef NO_DPRINTF
    {
        int arg_index;

        DPRINTF("PS2BBL: starting with %d argumments:\n", argc);
        for (arg_index = 0; arg_index < argc; arg_index++)
            DPRINTF("\targv[%d] = [%s]\n", arg_index, argv[arg_index]);
    }
#endif
    LogDetectedPlatform();
    DPRINTF("enabling LoadModuleBuffer\n");
    sbv_patch_enable_lmb(); // The old IOP kernel has no support for LoadModuleBuffer. Apply the patch to enable it.

    DPRINTF("disabling MODLOAD device blacklist/whitelist\n");
    sbv_patch_disable_prefix_check(); /* disable the MODLOAD module black/white list, allowing executables to be freely loaded from any device. */
    LoaderSetBootPathHint((argc > 0) ? argv[0] : NULL);
    LoaderLoadSystemModules(&g_usb_modules_loaded,
                            &g_mx4sio_modules_loaded,
                            &g_mmce_modules_loaded,
                            &g_hdd_modules_loaded);

    // Core system services: CDVD/OSD init and console capability probing.
    // Initialize libcdvd & supplement functions (which are not part of the ancient libcdvd library we use).
    sceCdInit(SCECdINoD);
    cdInitAdd();

    DPRINTF("init OSD system paths\n");
    OSDInitSystemPaths();

#if defined(PSX)
    if (!g_is_psx_desr) {
        DPRINTF("Certifying CDVD Boot\n");
        CDVDBootCertify(ROMVER); /* Not needed on PSX-DESR, but required on standard PS2 ROMs. */
    }
#else
    DPRINTF("Certifying CDVD Boot\n");
    CDVDBootCertify(ROMVER); /* This is not required for the PSX, as its OSDSYS will do it before booting the update. */
#endif

    DPRINTF("init OSD\n");
    InitOsd(); // Initialize OSD so kernel patches can do their magic

    DPRINTF("init ROMVER, model name ps1dvr and dvdplayer ver\n");
    OSDInitROMVER(); // Initialize ROM version (must be done first).
    // Refresh ROMVER/platform after IOP services are fully initialized.
    ReadROMVEROnce();
    LogDetectedPlatform();
    ConsoleInfoInit(); // Initialize console info cache (model name, etc.)
    PS1DRVInit();    // Initialize PlayStation Driver (PS1DRV)
    DVDPlayerInit(); // Initialize ROM DVD player. It is normal for this to fail on consoles that have no DVD ROM chip (i.e. DEX or the SCPH-10000/SCPH-15000).

    if (OSDConfigLoad() != 0) // Load OSD configuration
    {                         // OSD configuration not initialized. Defaults loaded.
        scr_setfontcolor(0x00ffff);
        DPRINTF("OSD Configuration not initialized. Defaults loaded.\n");
        scr_setfontcolor(0xffffff);
    }
    DPRINTF("Saving OSD configuration\n");
    OSDConfigApply();

    /*  Try to enable the remote control, if it is enabled.
        Indicate no hardware support for it, if it cannot be enabled. */
    DPRINTF("trying to enable remote control\n");
    do {
        result = sceCdRcBypassCtl(OSDConfigGetRcGameFunction() ^ 1, &STAT);
        if (STAT & 0x100) { // Not supported by the PlayStation 2.
            // Note: it does not seem like the browser updates the NVRAM here to change this status.
            OSDConfigSetRcEnabled(0);
            OSDConfigSetRcSupported(0);
            break;
        }
    } while ((STAT & 0x80) || (result == 0));

    // Remember to set the video output option (RGB or Y Cb/Pb Cr/Pr) accordingly, before SetGsCrt() is called.
    DPRINTF("Setting vmode\n");
    SetGsVParam(OSDConfigGetVideoOutput() == VIDEO_OUTPUT_RGB ? VIDEO_OUTPUT_RGB : VIDEO_OUTPUT_COMPONENT);
    g_native_video_mode = LoaderDetectNativeVideoMode();
    DPRINTF("Init pads\n");
    PadInitPads();
    DPRINTF("Init timer and start non-blocking rescue key window\n");
    TimerInit();
    rescue_combo_deadline = Timer() + RESCUE_COMBO_WINDOW_MS;
    PollEmergencyComboWindow(&rescue_combo_deadline);
    DPRINTF("load default settings\n");
    SetDefaultSettings();

    // Config bootstrap: locate CNF, apply defaults/fallbacks, and render early splash status.
    config_source = LoaderBootstrapConfigAndSplash(&g_pre_scanned,
                                                   &splash_early_presented,
                                                   g_config_path_in_use,
                                                   sizeof(g_config_path_in_use),
                                                   g_usb_modules_loaded,
                                                   g_mx4sio_modules_loaded,
                                                   g_mmce_modules_loaded,
                                                   g_hdd_modules_loaded,
                                                   g_native_video_mode,
                                                   g_is_psx_desr,
                                                   &rescue_combo_deadline,
                                                   PollEmergencyComboWindow,
                                                   LoaderParseVideoModeValue,
                                                   LoaderApplyVideoMode);

    // Optional rescue flow: allow user to adjust VIDEO_MODE before launch dispatch.
    if (g_video_mode_selector_requested) {
        LoaderRunEmergencyVideoModeSelector(&g_pre_scanned,
                                            &g_hotkey_launches_enabled,
                                            &g_block_hotkeys_until_release,
                                            g_is_psx_desr,
                                            g_native_video_mode,
                                            ROMVER,
                                            sizeof(ROMVER),
                                            g_config_path_in_use,
                                            sizeof(g_config_path_in_use));
        g_video_mode_selector_requested = 0;
    }

    GameIDSetConfig(GLOBCFG.APP_GAMEID, GLOBCFG.CDROM_DISABLE_GAMEID);
    PS1DRVSetOptions(GLOBCFG.PS1DRV_ENABLE_FAST, GLOBCFG.PS1DRV_ENABLE_SMOOTH, GLOBCFG.PS1DRV_USE_PS1VN);
    SplashRenderSetVideoMode(GLOBCFG.VIDEO_MODE, g_native_video_mode);

    // Main launch workflow: countdown, hotkeys, AUTO fallback, then emergency mode.
    if (LoaderRunLaunchWorkflow(splash_early_presented,
                                g_pre_scanned,
                                &g_hotkey_launches_enabled,
                                &g_block_hotkeys_until_release,
                                0x0001,
                                16,
                                g_is_psx_desr,
                                config_source,
                                g_native_video_mode,
                                ROMVER,
                                sizeof(ROMVER),
                                EXECPATHS,
                                &rescue_combo_deadline,
                                PollEmergencyComboWindow)) {
        LoaderRunEmergencyMode("COULD NOT FIND ANY DEFAULT APPLICATIONS");
    }

    return 0;
}

#ifndef NO_TEMP_DISP
void PrintTemperature(void)
{
    char temp_buf[16];

    if (QueryTemperatureCelsius(temp_buf, sizeof(temp_buf)))
        scr_printf("  Temp: %s\n", temp_buf);
}
#endif

/* BELOW THIS POINT ALL MACROS and MISC STUFF MADE TO REDUCE BINARY SIZE WILL BE PLACED */

#if defined(DUMMY_TIMEZONE)
void _libcglue_timezone_update()
{
}
#endif

#if defined(KERNEL_NOPATCH)
DISABLE_PATCHED_FUNCTIONS();
#endif

DISABLE_EXTRA_TIMERS_FUNCTIONS();
PS2_DISABLE_AUTOSTART_PTHREAD();
