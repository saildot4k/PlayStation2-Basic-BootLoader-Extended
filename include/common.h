#ifndef COMMONDEF
#define COMMONDEF

#define CONFIG_KEY_INDEXES 10       // number of paths to scan per hotkey, however the list below will need to match this value
#define MAX_ARGS_PER_ENTRY 8        // max number of ARG_* entries per LK_* slot
#define MAX_LEN     64              // max length for hotkey display names (after formatting)
#define CNF_LEN_MAX 20480           // 20kb should be enough for massive CNF's
#define LOGO_DISPLAY_DEFAULT 5      // Default LOGO_DISPLAY when no config is found: 0=off, 1=info, 2=logo+info, 3=banner+NAME, 4=banner+filename, 5=banner+path
#define DEFDELAY 6000               // default ammount of time this program will wait for a key press in ms/
#define CDROM_DISABLE_GAMEID_DEFAULT 0
#define PS1DRV_ENABLE_FAST_DEFAULT 0
#define PS1DRV_ENABLE_SMOOTH_DEFAULT 0
#define PS1DRV_USE_PS1VN_DEFAULT 0
#ifdef PSX
#define APP_GAMEID_DEFAULT 0
#else
#define APP_GAMEID_DEFAULT 1
#endif

enum
{
    SOURCE_MC0 = 0,
    SOURCE_MC1,
    SOURCE_MASS,
#ifdef MX4SIO
    SOURCE_MX4SIO,
#endif
#ifdef HDD
    SOURCE_HDD,
#endif
#ifdef XFROM
    SOURCE_XFROM,
#endif
#ifdef MMCE
    SOURCE_MMCE1,
    SOURCE_MMCE0,
#endif
#ifdef PSX
    SOURCE_XCONFIG,
#endif
    SOURCE_CWD,
    SOURCE_INVALID,
    SOURCE_COUNT,
} CONFIG_SOURCES_ID;

char *CONFIG_PATHS[SOURCE_COUNT] = {
    "mc0:/SYS-CONF/PS2BBL.INI",
    "mc1:/SYS-CONF/PS2BBL.INI",
    "mass:/PS2BBL/CONFIG.INI",
#ifdef MX4SIO
    "massX:/PS2BBL/CONFIG.INI",
#endif
#ifdef HDD
    "hdd0:__sysconf:pfs:/PS2BBL/CONFIG.INI",
#endif
#ifdef XFROM
    "xfrom:/PS2BBL/CONFIG.INI",
#endif
#ifdef MMCE
    "mmce0:/PS2BBL/PS2BBL.INI",
    "mmce1:/PS2BBL/PS2BBL.INI",
#endif
#ifdef PSX
    "mc?:/SYS-CONF/PSXBBL.INI",
#endif
    "CONFIG.INI",
    "",
};

static const char *SOURCES[SOURCE_COUNT] = {
    "mc0",
    "mc1",
    "usb",
#ifdef MX4SIO
    "mx4sio",
#endif
#ifdef HDD
    "hdd",
#endif
#ifdef XFROM
    "xfrom",
#endif
#ifdef MMCE
    "mmce0",
    "mmce1",
#endif
#ifdef PSX
    "XCONF",
#endif
    "CWD",
    "NOT FOUND",
};



/** dualshock keys enumerator */
enum
{
    AUTO,
    SELECT,
    L3,
    R3,
    START,
    UP,
    RIGHT,
    DOWN,
    LEFT,
    L2,
    R2,
    L1,
    R1,
    TRIANGLE,
    CIRCLE,
    CROSS,
    SQUARE
} KEYS;

/** string alias of dualshock keys for config file */
const char *KEYS_ID[17] = {
    "AUTO",
    "SELECT",   // 0x0001
    "L3",       // 0x0002
    "R3",       // 0x0004
    "START",    // 0x0008
    "UP",       // 0x0010
    "RIGHT",    // 0x0020
    "DOWN",     // 0x0040
    "LEFT",     // 0x0080
    "L2",       // 0x0100
    "R2",       // 0x0200
    "L1",       // 0x0400
    "R1",       // 0x0800
    "TRIANGLE", // 0x1000
    "CIRCLE",   // 0x2000
    "CROSS",    // 0x4000
    "SQUARE"    // 0x8000
};

/** default hotkey names used when LOGO_DISPLAY = 3 */
#ifdef PSX
    const char *DEFAULT_KEYNAMES_PSX[17] = {
        "wLE ISR (MMCE -> MX -> exFAT",
        "DISC NO LOGO",
        "L3",
        "R3",
        "DISC",
        "OSD-XMB",
        "RETROLauncher",
        "XEB+",
        "",
        "wLE ISR exFAT USB",
        "OPL",
        "PSBBN FORWARDER",
        "NHDDL / NEUTRINO",
        "wLE ISR (MMCE -> MX -> exFAT)",
        "wLE ISR (exFAT -> MX -> MMCE)",
        "DKWDRV",
        "POPSLOADER",
    };
#endif
    const char *DEFAULT_KEYNAMES_PS2[17] = {
        "HACKED OSDSYS",
        "DISC NO LOGO",
        "L3",
        "R3",
        "DISC",
        "OSD-XMB",
        "RETROLauncher",
        "XEB+",
        "",
        "HACKED OSDSYS",
        "OPL",
        "PSBBN FORWARDER",
        "NHDDL / NEUTRINO",
        "wLE ISR (MMCE -> MX -> exFAT)",
        "wLE ISR (exFAT -> MX -> MMCE)",
        "DKWDRV",
        "POPSLOADER",
    };

#if defined(PSX)
#define DEFAULT_KEYNAMES DEFAULT_KEYNAMES_PSX
#else
#define DEFAULT_KEYNAMES DEFAULT_KEYNAMES_PS2
#endif

/** default paths used if config file can't be loaded */
#ifdef PSX
    char *DEFPATH_PSX[] = {
        "mass:/RESCUE.ELF", // AUTO [0]
        "mc?:/APP_WLE-ISR-XF-MM/WLE-ISR-XF-MM.ELF",
        "mc?:/APP_WLE-ISR-XF-MX/WLE-ISR-XF-MX.ELF",
        "mc?:/APP_WLE-ISR-XF/WLE-ISR-XF.ELF",
        "mc?:/BOOT/BOOT2.ELF",
        "",
        "",
        "",
        "",
        "",
        "$CDVD_NO_PS2LOGO", // SELECT [CONFIG_KEY_INDEXES * 1]
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "", // L3 [CONFIG_KEY_INDEXES * 2]
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "", // R3 [CONFIG_KEY_INDEXES * 3]
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "$CDVD", // START [CONFIG_KEY_INDEXES *4]
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "mass:/OSDXMB/OSDXMB.ELF", // UP [CONFIG_KEY_INDEXES * 5]
        "mmce?:/OSDXMB/OSDXMB.ELF",
        "mc?:/APP_OSDXMB/OSDXMB.ELF",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "mass:/RETROLauncher/RETROLauncher.elf", // RIGHT [CONFIG_KEY_INDEXES * 6]
        "mmce?:/RETROLauncher/RETROLauncher.elf",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "mass:/XEBPLUS/XEBPLUS_XMAS.ELF", // DOWN [CONFIG_KEY_INDEXES * 7]
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "", // LEFT [CONFIG_KEY_INDEXES * 8]
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "mass:/RESCUE.ELF", // L2 [CONFIG_KEY_INDEXES * 9]
        "mc?:/APP_WLE-ISR-XF-MM/WLE-ISR-XF-MM.ELF",
        "mc?:/APP_WLE-ISR-XF-MX/WLE-ISR-XF-MX.ELF",
        "mc?:/APP_WLE-ISR-XF/WLE-ISR-XF.ELF",
        "mc?:/BOOT/BOOT2.ELF",
        "",
        "",
        "",
        "",
        "",
        "mass:/APPS/APP_OPL/OPL.ELF", // R2 [CONFIG_KEY_INDEXES * 10]
        "mmce?:/APPS/APP_OPL/OPL.ELF",
        "mc?:/APP_OPL/OPL.ELF",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "mc:?/APP_PSBBN-FORWARDER/PSBBN-FORWARDER.ELF", // L1 [CONFIG_KEY_INDEXES * 11]
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "mass:/NEUTRINO/nhddl.elf", // R1 [CONFIG_KEY_INDEXES * 12]
        "mmce?:/NEUTRINO/nhddl.elf",
        "mc?:/APP_NHDDL/nhddl.elf",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "mass:/RESCUE.ELF", // TRIANGLE [CONFIG_KEY_INDEXES * 13]
        "mc?:/APP_WLE-ISR-XF-MM/WLE-ISR-XF-MM.ELF",
        "mc?:/APP_WLE-ISR-XF-MX/WLE-ISR-XF-MX.ELF",
        "mc?:/APP_WLE-ISR-XF/WLE-ISR-XF.ELF",
        "mc?:/BOOT/BOOT2.ELF",
        "",
        "",
        "",
        "",
        "",
        "mass:/RESCUE.ELF", // CIRCLE [CONFIG_KEY_INDEXES * 14]
        "mc?:/BOOT/BOOT2.ELF",
        "mc?:/APP_WLE-ISR-XF/WLE-ISR-XF.ELF",
        "mc?:/APP_WLE-ISR-XF-MX/WLE-ISR-XF-MX.ELF",
        "mc?:/APP_WLE-ISR-XF-MM/WLE-ISR-XF-MM.ELF",
        "",
        "",
        "",
        "",
        "",
        "mc?:/PS1_DKWDRV/DKWDRV.ELF", // CROSS [CONFIG_KEY_INDEXES * 15]
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "mass:/APPS/PS1_POPSLOADER/POPSLOADER.ELF", // SQUARE [CONFIG_KEY_INDEXES * 16]
        "mmce?:/APPS/PS1_POPSLOADER/POPSLOADER.ELF",
        "mc?:/PS1_POPSLOADER/POPSLOADER.ELF",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
    };
#endif
    char *DEFPATH_PS2[] = {
        "mc?:/SYS_OSDMENU/osdmenu.elf", // AUTO [0]
        "mc?:/SYS_FMCBD-1966/FMDBD-1966.ELF",
        "mc?:/SYS_FMCBD-1965/FMCBD-1965.ELF",
        "mc?:/SYS_FMCBD-1953/FMCBD-1953.ELF",
        "mc?:/SYS_FMCBD-18C/FMCBD-18C.ELF",
        "mc?:/BOOT/BOOT2.ELF",
        "",
        "",
        "",
        "",
        "$CDVD_NO_PS2LOGO", // SELECT [CONFIG_KEY_INDEXES * 1]
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "", // L3 [CONFIG_KEY_INDEXES * 2]
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "", // R3 [CONFIG_KEY_INDEXES * 3]
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "$CDVD", // START [CONFIG_KEY_INDEXES * 4]
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "mass:/OSDXMB/OSDXMB.ELF", // UP [CONFIG_KEY_INDEXES * 5]
        "mmce?:/OSDXMB/OSDXMB.ELF",
        "mc?:/APP_OSDXMB/OSDXMB.ELF",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "mass:/RETROLauncher/RETROLauncher.elf", // RIGHT [CONFIG_KEY_INDEXES * 6]
        "mmce?:/RETROLauncher/RETROLauncher.elf",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "mass:/XEBPLUS/XEBPLUS_XMAS.ELF", // DOWN [CONFIG_KEY_INDEXES * 7]
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "", // LEFT [CONFIG_KEY_INDEXES * 8]
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "mc?:/SYS_OSDMENU/osdmenu.elf", // L2 [CONFIG_KEY_INDEXES * 9]
        "mc?:/SYS_FMCBD-1966/FMCBD-1966.ELF",
        "mc?:/SYS_FMCBD-1965/FMCBD-1965.ELF",
        "mc?:/SYS_FMCBD-1953/FMCBD-1953.ELF",
        "mc?:/SYS_FMCBD-18C/FMCBD-18C.ELF",
        "mc?:/BOOT/BOOT2.ELF",
        "",
        "",
        "",
        "",
        "mass:/APPS/APP_OPL/OPL.ELF", // R2 [CONFIG_KEY_INDEXES * 10]
        "mmce?:/APPS/APP_OPL/OPL.ELF",
        "mc?:/APP_OPL/OPL.ELF",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "mc:?/APP_PSBBN-FORWARDER/PSBBN-FORWARDER.ELF", // L1 [CONFIG_KEY_INDEXES * 11]
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "mass:/NEUTRINO/nhddl.elf", // R1 [CONFIG_KEY_INDEXES * 12]
        "mmce?:/NEUTRINO/nhddl.elf",
        "mc?:/APP_NHDDL/nhddl.elf",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "mass:/RESCUE.ELF", // TRIANGLE [CONFIG_KEY_INDEXES * 13]
        "mc?:/APP_WLE-ISR-XF-MM/WLE-ISR-XF-MM.ELF",
        "mc?:/APP_WLE-ISR-XF-MX/WLE-ISR-XF-MX.ELF",
        "mc?:/APP_WLE-ISR-XF/WLE-ISR-XF.ELF",
        "mc?:/BOOT/BOOT2.ELF",
        "",
        "",
        "",
        "",
        "",
        "mass:/RESCUE.ELF", // CIRCLE [CONFIG_KEY_INDEXES * 14]
        "mc?:/BOOT/BOOT2.ELF",
        "mc?:/APP_WLE-ISR-XF/WLE-ISR-XF.ELF",
        "mc?:/APP_WLE-ISR-XF-MX/WLE-ISR-XF-MX.ELF",
        "mc?:/APP_WLE-ISR-XF-MM/WLE-ISR-XF-MM.ELF",
        "",
        "",
        "",
        "",
        "",
        "mc?:/PS1_DKWDRV/DKWDRV.ELF", // CROSS [CONFIG_KEY_INDEXES * 15]
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "mass:/APPS/PS1_POPSLOADER/POPSLOADER.ELF", // SQUARE [CONFIG_KEY_INDEXES * 16]
        "mmce?:/APPS/PS1_POPSLOADER/POPSLOADER.ELF",
        "mc?:/PS1_POPSLOADER/POPSLOADER.ELF",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
    };

#if defined(PSX)
#define DEFPATH DEFPATH_PSX
#else
#define DEFPATH DEFPATH_PS2
#endif

#ifndef COMMIT_HASH
#define COMMIT_HASH "UNKNOWn"
#endif

#endif // COMMONDEF
