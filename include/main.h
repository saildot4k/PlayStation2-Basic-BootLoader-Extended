#ifndef MAIN_H
#define MAIN_H
#define NEWLIB_PORT_AWARE

#include <tamtypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>
#include <fcntl.h>

#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <debug.h>
#include <iopcontrol.h>
#include <iopheap.h>
#include <sbv_patches.h>
#include <ps2sdkapi.h>
#include <usbhdfsd-common.h>

#include <osd_config.h>

#include <libpad.h>
#include <libmc.h>
#include <libcdvd.h>

#include "debugprintf.h"
#include "pad.h"
#include "util.h"
#include "common.h"

#include "libcdvd_add.h"
#include "dvdplayer.h"
#include "OSDInit.h"
#include "OSDConfig.h"
#include "OSDHistory.h"
#include "ps1.h"
#include "ps2.h"

#ifdef PSX
#include <iopcontrol_special.h>
#include "psx/plibcdvd_add.h"
#endif

#ifdef DEV9
int loadDEV9(void);
#endif

// For avoiding define NEWLIB_AWARE
void fioInit();
#define RBG2INT(R, G, B) ((0 << 24) + (R << 16) + (G << 8) + B)
#include "irx_import.h"

char *trim_ws_inplace(char *s);
const char *strip_crlf_copy(const char *s, char *buf, size_t buf_len);
int ci_eq(const char *a, const char *b);
int ci_starts_with(const char *s, const char *prefix);
int ci_starts_with_n(const char *s, size_t s_len, const char *prefix);
int path_is_disc_root(const char *path);
int normalize_logo_display(int value);
int logo_to_hotkey_display(int logo_disp);

typedef struct
{
    char *KEYPATHS[KEY_COUNT][CONFIG_KEY_INDEXES];
    char **KEYARGS[KEY_COUNT][CONFIG_KEY_INDEXES];
    int KEYARGC[KEY_COUNT][CONFIG_KEY_INDEXES];
    int KEYARGCAP[KEY_COUNT][CONFIG_KEY_INDEXES];
    const char *KEYNAMES[KEY_COUNT];
    int HOTKEY_DISPLAY; // Derived from LOGO_DISP (0=off, 1=name).
    int DELAY;
    int OSDHISTORY_READ;
    int TRAYEJECT;
    int LOGO_DISP; // 0=off, 1=console info, 2=logo+info, 3=banner+names.
    int CDROM_DISABLE_GAMEID;
    int APP_GAMEID;
    int DISC_STOP;
    int VIDEO_MODE;
} CONFIG;

extern CONFIG GLOBCFG;

enum {
    CFG_VIDEO_MODE_AUTO = 0,
    CFG_VIDEO_MODE_NTSC,
    CFG_VIDEO_MODE_PAL,
    CFG_VIDEO_MODE_480P
};

void RunLoaderElf(const char *filename, const char *party, int argc, char *argv[]);
void EMERGENCY(void);

void SplashDrawLoadingStatus(int logo_disp);
void SplashDrawCenteredStatusWithInfo(const char *text,
                                      u32 color,
                                      const char *model,
                                      const char *rom_fmt,
                                      const char *dvdver,
                                      const char *ps1ver,
                                      const char *temp_celsius,
                                      const char *source);
void SplashDrawRetryPromptWithInfo(const char *line1,
                                   u32 line1_color,
                                   int dots,
                                   const char *model,
                                   const char *rom_fmt,
                                   const char *dvdver,
                                   const char *ps1ver,
                                   const char *temp_celsius,
                                   const char *source);
int WaitForMissingPathAction(const char *button_name,
                             const char *model,
                             const char *rom_fmt,
                             const char *dvdver,
                             const char *ps1ver,
                             const char *temp_celsius,
                             const char *source);
void SplashDrawEmergencyModeStatus(const char *reason);
void RestoreSplashInteractiveUi(int logo_disp,
                                const char *const hotkey_lines[KEY_COUNT],
                                const char *model,
                                const char *rom_fmt,
                                const char *dvdver,
                                const char *ps1ver,
                                const char *temp_celsius,
                                const char *source);
void ShowLaunchStatus(const char *path);
// Reboot the PS1 CPU and perform additional tasks if building for PSX DESR.
void LoaderPlatformClearStaleEEDebugState(void);
void ResetIOP(void);
void ReadROMVEROnce(void);
void LogDetectedPlatform(void);
void LoaderRunEmergencyMode(const char *reason);
void SetDefaultSettings(void);
void TimerInit(void);
u64 Timer(void);
void TimerEnd(void);

/// check path for processing pseudo-devices like `mc?:/`
char *CheckPath(const char *path);
int LoaderGetConfigSource(void);
const char *LoaderGetResolvedConfigPath(void);
const char *LoaderGetRequestedConfigPath(void);
int dischandler(int skip_ps2logo, int argc, char *argv[], int wait_for_disc);
// There is no need to call this on a PSX DESR since OSDSYS performs it at boot.
void CDVDBootCertify(u8 romver[16]);
int credits(void);
void CleanUp(void);
void LoaderSetBootPathHint(const char *boot_path);
const char *LoaderGetBootPathHint(void);
const char *LoaderGetBootCwdConfigPath(void);
const char *LoaderGetBootConfigPath(void);
const char *LoaderGetBootDriverTag(void);
int LoaderGetBootConfigSourceHint(void);
int LoaderPathFamilyReadyWithoutReload(const char *path);
int LoaderEnsurePathFamilyReady(const char *path);
int LoaderPrepareFinalLaunch(const char *path);
int LoaderLoadBdmTransportsForHint(const char *path_hint);
void LoaderLoadSystemModules(int *bdm_modules_loaded,
                             int *mx4sio_modules_loaded,
                             int *mmce_modules_loaded,
                             int *hdd_modules_loaded);
// Execute OSDSYS with parameters to avoid booting memory card or HDD updates.
void runOSDNoUpdate(void);

#ifndef NO_TEMP_DISP
int QueryTemperatureCelsius(char *temp_buf, size_t temp_buf_size);
/// @brief Print console temperature on screen.
/// @note only supported by DRAGON Mechacons. Function will do nothing on older mechacon.
void PrintTemperature(void);
#endif

#ifdef UDPTTY
void loadUDPTTY(void);
#endif

#ifdef HDD
#include <hdd-ioctl.h>
#include <io_common.h>
#include <assert.h>
#include <libpwroff.h>

extern char PART[128];
extern int HDD_USABLE;
#define MPART PART

int CheckHDD(void);
int LoadHDDIRX(void);             // Load HDD IRXes.
int MountParty(const char *path); // Process `hdd0:/$PARTITION:pfs:$PATH_TO_FILE/` and mount partition.
int mnt(const char *path);        // Mount partition specified on path.
void HDDChecker(void);
void poweroffCallback(void *arg);
#else
// Ensures loaded ELFs do not receive extra HDD arg when HDD support is unavailable.
#define MPART NULL
#endif

#ifdef MX4SIO
int LookForBDMDevice(void);
#endif

#ifdef FILEXIO
#include <fileXio_rpc.h>
int LoadFIO(void); // Load FileXio and its dependencies.
#endif

#endif
