// Default runtime configuration values and key/path initializers.
#include "main.h"

extern int g_is_psx_desr;

void SetDefaultSettings(void)
{
    int i, j;
#if defined(PSX)
    const char **default_keynames = g_is_psx_desr ? DEFAULT_KEYNAMES_PSX : DEFAULT_KEYNAMES_PS2;
#else
    const char **default_keynames = DEFAULT_KEYNAMES;
#endif

    for (i = 0; i < KEY_COUNT; i++)
        for (j = 0; j < CONFIG_KEY_INDEXES; j++) {
            GLOBCFG.KEYPATHS[i][j] = "isra:/";
            GLOBCFG.KEYARGC[i][j] = 0;
            memset(GLOBCFG.KEYARGS[i][j], 0, sizeof(GLOBCFG.KEYARGS[i][j]));
        }

    for (i = 0; i < KEY_COUNT; i++)
        GLOBCFG.KEYNAMES[i] = default_keynames[i];

    GLOBCFG.OSDHISTORY_READ = 1;
    GLOBCFG.DELAY = DEFDELAY;
    GLOBCFG.TRAYEJECT = 0;
    GLOBCFG.LOGO_DISP = 3;
    GLOBCFG.HOTKEY_DISPLAY = logo_to_hotkey_display(GLOBCFG.LOGO_DISP);
    GLOBCFG.CDROM_DISABLE_GAMEID = CDROM_DISABLE_GAMEID_DEFAULT;
#if defined(PSX)
    GLOBCFG.APP_GAMEID = g_is_psx_desr ? 0 : 1;
#else
    GLOBCFG.APP_GAMEID = APP_GAMEID_DEFAULT;
#endif
    GLOBCFG.PS1DRV_ENABLE_FAST = PS1DRV_ENABLE_FAST_DEFAULT;
    GLOBCFG.PS1DRV_ENABLE_SMOOTH = PS1DRV_ENABLE_SMOOTH_DEFAULT;
    GLOBCFG.PS1DRV_USE_PS1VN = PS1DRV_USE_PS1VN_DEFAULT;
    GLOBCFG.VIDEO_MODE = CFG_VIDEO_MODE_AUTO;
    GameIDSetConfig(GLOBCFG.APP_GAMEID, GLOBCFG.CDROM_DISABLE_GAMEID);
}
