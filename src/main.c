#include "main.h"
#include "game_id.h"

// Whitespace/CRLF trimming for config values (in-place)
// Returns a pointer to the first non-whitespace character (may be inside the original buffer).
static char *trim_ws_inplace(char *s)
{
    char *end;
    if (!s)
        return s;

    // Left trim
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
        s++;

    if (*s == '\0')
        return s;

    // Right trim
    end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
        end--;
    *end = '\0';

    return s;
}

// Copy into buf and stop at CR/LF (does not trim other whitespace).
static const char *strip_crlf_copy(const char *s, char *buf, size_t buf_len)
{
    size_t i = 0;
    if (buf_len == 0)
        return "";
    if (s == NULL) {
        buf[0] = '\0';
        return buf;
    }
    while (s[i] != '\0' && s[i] != '\r' && s[i] != '\n' && i + 1 < buf_len) {
        buf[i] = s[i];
        i++;
    }
    buf[i] = '\0';
    return buf;
}

static int ci_eq(const char *a, const char *b)
{
    unsigned char ca, cb;
    if (a == NULL || b == NULL)
        return 0;
    while (*a != '\0' && *b != '\0') {
        ca = (unsigned char)*a;
        cb = (unsigned char)*b;
        if (ca >= 'a' && ca <= 'z')
            ca -= ('a' - 'A');
        if (cb >= 'a' && cb <= 'z')
            cb -= ('a' - 'A');
        if (ca != cb)
            return 0;
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0');
}

static int ci_starts_with(const char *s, const char *prefix)
{
    unsigned char cs, cp;
    if (s == NULL || prefix == NULL)
        return 0;
    while (*prefix != '\0') {
        cs = (unsigned char)*s;
        cp = (unsigned char)*prefix;
        if (cs == '\0')
            return 0;
        if (cs >= 'a' && cs <= 'z')
            cs -= ('a' - 'A');
        if (cp >= 'a' && cp <= 'z')
            cp -= ('a' - 'A');
        if (cs != cp)
            return 0;
        s++;
        prefix++;
    }
    return 1;
}
// --------------- glob stuff --------------- //
typedef struct
{
    int SKIPLOGO;
    char *KEYPATHS[17][CONFIG_KEY_INDEXES];
    char *KEYARGS[17][CONFIG_KEY_INDEXES][MAX_ARGS_PER_ENTRY];
    int KEYARGC[17][CONFIG_KEY_INDEXES];
    const char *KEYNAMES[17];
    int HOTKEY_DISPLAY; // derived from LOGO_DISP (0=off, 1=defined name, 2=filename, 3=path)
    int DELAY;
    int OSDHISTORY_READ;
    int TRAYEJECT;
    int LOGO_DISP; // 0=off, 1=console info, 2=logo+info, 3=banner+names, 4=banner+filename, 5=banner+full path
    int CDROM_DISABLE_GAMEID;
    int APP_GAMEID;
    int PS1DRV_ENABLE_FAST;
    int PS1DRV_ENABLE_SMOOTH;
    int PS1DRV_USE_PS1VN;
} CONFIG;
CONFIG GLOBCFG;
static int g_pre_scanned = 0;
static int g_usb_modules_loaded = 0;
static int g_mx4sio_modules_loaded = 0;
static int g_mmce_modules_loaded = 0;
static int g_hdd_modules_loaded = 0;
#ifdef MX4SIO
static int g_mx4sio_slot = -2;
#endif
static int config_source = SOURCE_INVALID;

enum {
    DEV_UNKNOWN = -1,
    DEV_MC0 = 0,
    DEV_MC1,
    DEV_MASS,
    DEV_MX4SIO,
    DEV_MMCE0,
    DEV_MMCE1,
    DEV_HDD,
    DEV_XFROM,
    DEV_COUNT
};

static int dev_state[DEV_COUNT] = {
    -1, -1, -1, -1, -1, -1, -1, -1
};

#ifdef HDD
static int CheckHDD(void);
#endif

static const char *get_hotkey_name(int key)
{
    const char *name = GLOBCFG.KEYNAMES[key];
    if (name != NULL && *name != '\0')
        return name;
    return "";
}

static void PrintHotkeyNamesTemplate(const char *tmpl)
{
    char buf[128];
    int bi = 0;
    const char *p = tmpl;

    while (*p) {
        if (*p == '{' && !strncmp(p, "{NAME_", 6)) {
            const char *start = p + 6;
            const char *end = strchr(start, '}');
            if (end != NULL) {
                size_t len = (size_t)(end - start);
                int k, matched = 0;

                if (bi > 0) {
                    buf[bi] = '\0';
                    scr_printf("%s", buf);
                    bi = 0;
                }

                for (k = 0; k < 17; k++) {
                    if (len == strlen(KEYS_ID[k]) && !strncmp(start, KEYS_ID[k], len)) {
                        scr_printf("%s", get_hotkey_name(k));
                        matched = 1;
                        break;
                    }
                }

                if (!matched) {
                    scr_printf("{NAME_%.*s}", (int)len, start);
                }

                p = end + 1;
                continue;
            }
        }

        buf[bi++] = *p++;
        if (bi >= (int)sizeof(buf) - 1) {
            buf[bi] = '\0';
            scr_printf("%s", buf);
            bi = 0;
        }
    }

    if (bi > 0) {
        buf[bi] = '\0';
        scr_printf("%s", buf);
    }
}

static int is_command_token(const char *path)
{
    return (path != NULL && path[0] == '$');
}

static int device_modules_ready(int dev)
{
    switch (dev) {
        case DEV_MASS:
            return g_usb_modules_loaded;
        case DEV_MX4SIO:
            return g_mx4sio_modules_loaded;
        case DEV_MMCE0:
        case DEV_MMCE1:
            return g_mmce_modules_loaded;
        case DEV_HDD:
            return g_hdd_modules_loaded;
        default:
            return 1;
    }
}

#ifdef MX4SIO
static int get_mx4sio_slot(void)
{
    if (g_mx4sio_slot == -2)
        g_mx4sio_slot = LookForBDMDevice();
    if (g_mx4sio_slot >= 0) {
        dev_state[DEV_MC0] = 1;
        dev_state[DEV_MC1] = 0;
    }
    return g_mx4sio_slot;
}
#endif

static int device_id_from_path(const char *path)
{
    if (path == NULL || *path == '\0')
        return DEV_UNKNOWN;
    if (!strncmp(path, "mc0:", 4))
        return DEV_MC0;
    if (!strncmp(path, "mc1:", 4))
        return DEV_MC1;
    if (!strncmp(path, "massX:", 6))
        return DEV_MX4SIO;
    if (!strncmp(path, "mass", 4))
        return DEV_MASS;
    if (!strncmp(path, "mmce0:", 6))
        return DEV_MMCE0;
    if (!strncmp(path, "mmce1:", 6))
        return DEV_MMCE1;
    if (!strncmp(path, "hdd0:", 5))
        return DEV_HDD;
    if (!strncmp(path, "xfrom:", 6))
        return DEV_XFROM;
    return DEV_UNKNOWN;
}

static int device_root_available(int dev)
{
    struct stat st;
    switch (dev) {
        case DEV_MC0:
            return (stat("mc0:/", &st) == 0);
        case DEV_MC1:
            return (stat("mc1:/", &st) == 0);
        case DEV_MASS:
            return (stat("mass:/", &st) == 0);
#ifdef MX4SIO
        case DEV_MX4SIO:
            return (get_mx4sio_slot() >= 0);
#endif
#ifdef MMCE
        case DEV_MMCE0:
            return (stat("mmce0:/", &st) == 0);
        case DEV_MMCE1:
            return (stat("mmce1:/", &st) == 0);
#endif
#ifdef HDD
        case DEV_HDD:
            return (CheckHDD() >= 0);
#endif
        case DEV_XFROM:
            return 1;
        default:
            return 1;
    }
}

static int device_available_for_dev(int dev)
{
    if (dev == DEV_UNKNOWN)
        return 1;
    if (!device_modules_ready(dev)) {
        dev_state[dev] = 0;
        return 0;
    }
#ifdef MMCE
    if (dev == DEV_MMCE0 || dev == DEV_MMCE1) {
        int mc_dev = (dev == DEV_MMCE0) ? DEV_MC0 : DEV_MC1;
        if (dev_state[dev] >= 0)
            return dev_state[dev];
        dev_state[dev] = device_root_available(dev) ? 1 : 0;
        if (dev_state[dev] > 0)
            dev_state[mc_dev] = 1;
        return dev_state[dev];
    }
#endif
    if (dev_state[dev] >= 0)
        return dev_state[dev];
    dev_state[dev] = device_root_available(dev) ? 1 : 0;
    return dev_state[dev];
}

static void build_device_available_cache(int *dev_ok, int count)
{
    int dev;
    if (dev_ok == NULL || count <= 0)
        return;
    for (dev = 0; dev < count; dev++)
        dev_ok[dev] = device_available_for_dev(dev) ? 1 : 0;
}

static int device_available_for_path_cached(const char *path, const int *dev_ok)
{
    int dev;
    if (path == NULL || *path == '\0')
        return 0;
    if (!strncmp(path, "mc?:", 4))
        return (dev_ok[DEV_MC0] || dev_ok[DEV_MC1]);
#ifdef MMCE
    if (!strncmp(path, "mmce?:", 6))
        return (dev_ok[DEV_MMCE0] || dev_ok[DEV_MMCE1]);
#endif
    dev = device_id_from_path(path);
    if (dev == DEV_UNKNOWN)
        return 1;
    return dev_ok[dev];
}

static int resolve_pair_path(char *path, int slot_index, char preferred, char **out_path);

static int command_display_path(char *path, const int *dev_ok, const char **out_display)
{
    const char *runkelf_prefix = "$RUNKELF:";
    if (path == NULL || *path == '\0' || !is_command_token(path))
        return 0;

#ifndef HDD
    if (!strcmp(path, "$HDDCHECKER"))
        return 0;
#endif

    if (!strncmp(path, runkelf_prefix, strlen(runkelf_prefix))) {
        char *kelf_path = path + strlen(runkelf_prefix);
        if (kelf_path == NULL || *kelf_path == '\0')
            return 0;
        if (strncmp(kelf_path, "mc", 2) != 0 && strncmp(kelf_path, "hdd", 3) != 0)
            return 0;
        if (!device_available_for_path_cached(kelf_path, dev_ok))
            return 0;
        if (!strncmp(kelf_path, "mc?:", 4)) {
            char preferred = (config_source == SOURCE_MC1) ? '1' : '0';
            if (resolve_pair_path(kelf_path, 2, preferred, &kelf_path)) {
                if (out_display)
                    *out_display = kelf_path;
                return 1;
            }
            return 0;
        }
        kelf_path = CheckPath(kelf_path);
        if (exist(kelf_path)) {
            if (out_display)
                *out_display = kelf_path;
            return 1;
        }
        return 0;
    }

    if (out_display)
        *out_display = path + 1;
    return 1;
}

static inline int is_elf_ext_ci(const char *s, size_t len)
{
    if (len < 4)
        return 0;
    return ((s[len - 4] == '.') &&
            ((s[len - 3] == 'e' || s[len - 3] == 'E')) &&
            ((s[len - 2] == 'l' || s[len - 2] == 'L')) &&
            ((s[len - 1] == 'f' || s[len - 1] == 'F')));
}

static int resolve_pair_path(char *path, int slot_index, char preferred, char **out_path)
{
    char alternate = (preferred == '0') ? '1' : '0';

    path[slot_index] = preferred;
    if (exist(path)) {
        if (out_path)
            *out_path = path;
        return 1;
    }
    path[slot_index] = alternate;
    if (exist(path)) {
        if (out_path)
            *out_path = path;
        return 1;
    }
    if (out_path)
        *out_path = path;
    return 0;
}

#ifdef MMCE
static char preferred_mmce_slot(void)
{
    if (config_source == SOURCE_MMCE1 || config_source == SOURCE_MC1)
        return '1';
    return '0';
}
#endif

static const char *path_basename(const char *path)
{
    const char *base = path;
    const char *p = path;
    if (p == NULL)
        return "";
    while (*p) {
        if (*p == '/' || *p == '\\')
            base = p + 1;
        p++;
    }
    return base;
}

static int normalize_logo_display(int value)
{
    return (value >= 0 && value <= 5) ? value : 2;
}

static int logo_to_hotkey_display(int logo_disp)
{
    switch (logo_disp) {
        case 3:
            return 1;
        case 4:
            return 2;
        case 5:
            return 3;
        default:
            return 0;
    }
}

// Validate paths, clear invalid entries, and set display names per mode.
static void ValidateKeypathsAndSetNames(int display_mode, int scan_paths)
{
    static char name_buf[17][MAX_LEN];
    int dev_ok[DEV_COUNT];
    const char *first_valid[17];
    int i, j;

    for (i = 0; i < 17; i++)
        first_valid[i] = NULL;

    if (scan_paths) {
        build_device_available_cache(dev_ok, DEV_COUNT);
        for (i = 0; i < 17; i++) {
            int found = 0;
            for (j = 0; j < CONFIG_KEY_INDEXES; j++) {
                char *path = GLOBCFG.KEYPATHS[i][j];
                if (found) {
                    GLOBCFG.KEYPATHS[i][j] = "";
                    continue;
                }
                if (path == NULL || *path == '\0') {
                    GLOBCFG.KEYPATHS[i][j] = "";
                    continue;
                }
                if (is_command_token(path)) {
                    const char *cmd_display = NULL;
                    if (command_display_path(path, dev_ok, &cmd_display)) {
                        if (first_valid[i] == NULL)
                            first_valid[i] = (cmd_display != NULL) ? cmd_display : path;
                        found = 1;
                    }
                    continue; // Commands only run on keypress.
                }
                if (!device_available_for_path_cached(path, dev_ok)) {
                    GLOBCFG.KEYPATHS[i][j] = "";
                    continue;
                }
                if (!strncmp(path, "mc?:", 4)) {
                    int slot_index = 2;
                    char preferred = (config_source == SOURCE_MC1) ? '1' : '0';
                    if (resolve_pair_path(path, slot_index, preferred, &path)) {
                        GLOBCFG.KEYPATHS[i][j] = path;
                        if (first_valid[i] == NULL)
                            first_valid[i] = path;
                        found = 1;
                    } else {
                        GLOBCFG.KEYPATHS[i][j] = "";
                    }
                    continue;
                }
#ifdef MMCE
                if (!strncmp(path, "mmce?:", 6)) {
                    int slot_index = 4;
                    char preferred = preferred_mmce_slot();
                    if (resolve_pair_path(path, slot_index, preferred, &path)) {
                        GLOBCFG.KEYPATHS[i][j] = path;
                        if (first_valid[i] == NULL)
                            first_valid[i] = path;
                        found = 1;
                    } else {
                        GLOBCFG.KEYPATHS[i][j] = "";
                    }
                    continue;
                }
#endif
                path = CheckPath(path);
                if (exist(path)) {
                    GLOBCFG.KEYPATHS[i][j] = path;
                    if (first_valid[i] == NULL)
                        first_valid[i] = path;
                    found = 1;
                } else {
                    GLOBCFG.KEYPATHS[i][j] = "";
                }
            }
        }
    }

    if (display_mode < 0 || display_mode > 3)
        display_mode = 0;
    if (display_mode == 0) {
        for (i = 0; i < 17; i++)
            GLOBCFG.KEYNAMES[i] = "";
        return;
    }
    if (display_mode == 1)
        return; // keep user-defined names

    for (i = 0; i < 17; i++) {
        if (display_mode == 3) {
            GLOBCFG.KEYNAMES[i] = (first_valid[i] != NULL) ? first_valid[i] : "";
        } else {
            const char *base = (first_valid[i] != NULL) ? path_basename(first_valid[i]) : "";
            size_t len = strlen(base);
            if (is_elf_ext_ci(base, len))
                len -= 4;
            if (len >= MAX_LEN)
                len = MAX_LEN - 1;
            memcpy(name_buf[i], base, len);
            name_buf[i][len] = '\0';
            GLOBCFG.KEYNAMES[i] = name_buf[i];
        }
    }
}

char *EXECPATHS[CONFIG_KEY_INDEXES];
u8 ROMVER[16];
int PAD = 0;
unsigned char *config_buf = NULL; // pointer to allocated config file

int main(int argc, char *argv[])
{
    u32 STAT;
    u64 tstart;
    int button, x, j, cnf_size, fd, result;
    static int num_buttons = 16, pad_button = 0x0001; // Scan all 16 buttons
    char *CNFBUFF, *name, *value;

    ResetIOP();
    SifInitIopHeap(); // Initialize SIF services for loading modules and files.
    SifLoadFileInit();
    fioInit(); // NO scr_printf BEFORE here
    init_scr();
    scr_setCursor(0); // get rid of annoying that cursor.
    DPRINTF_INIT()
#ifndef NO_DPRINTF
    DPRINTF("PS2BBL: starting with %d argumments:\n", argc);
    for (x = 0; x < argc; x++)
        DPRINTF("\targv[%d] = [%s]\n", x, argv[x]);
#endif
    scr_setfontcolor(0x101010);
    scr_printf(".\n"); // GBS control does not detect image output with scr debug till the first char is printed
    scr_setfontcolor(0xffffff);
    // print a simple dot to allow gbs control to start displaying video before banner and pad timeout begins to run. othersiwe, users with timeout lower than 4000 will have issues to respond in time, then resets back to white text
    DPRINTF("enabling LoadModuleBuffer\n");
    sbv_patch_enable_lmb(); // The old IOP kernel has no support for LoadModuleBuffer. Apply the patch to enable it.

    DPRINTF("disabling MODLOAD device blacklist/whitelist\n");
    sbv_patch_disable_prefix_check(); /* disable the MODLOAD module black/white list, allowing executables to be freely loaded from any device. */

#ifdef PPCTTY
    //no error handling bc nothing to do in this case
    SifExecModuleBuffer(ppctty_irx, size_ppctty_irx, 0, NULL, NULL);
#endif
#ifdef UDPTTY
    if (loadDEV9())
        loadUDPTTY();
#endif

#ifdef USE_ROM_SIO2MAN
    j = SifLoadStartModule("rom0:SIO2MAN", 0, NULL, &x);
    DPRINTF(" [SIO2MAN]: ID=%d, ret=%d\n", j, x);
#else
    j = SifExecModuleBuffer(sio2man_irx, size_sio2man_irx, 0, NULL, &x);
    DPRINTF(" [SIO2MAN]: ID=%d, ret=%d\n", j, x);
#endif
#ifdef USE_ROM_MCMAN
    j = SifLoadStartModule("rom0:MCMAN", 0, NULL, &x);
    DPRINTF(" [MCMAN]: ID=%d, ret=%d\n", j, x);
    j = SifLoadStartModule("rom0:MCSERV", 0, NULL, &x);
    DPRINTF(" [MCSERV]: ID=%d, ret=%d\n", j, x);
    mcInit(MC_TYPE_MC);
#else
    j = SifExecModuleBuffer(mcman_irx, size_mcman_irx, 0, NULL, &x);
    DPRINTF(" [MCMAN]: ID=%d, ret=%d\n", j, x);
    j = SifExecModuleBuffer(mcserv_irx, size_mcserv_irx, 0, NULL, &x);
    DPRINTF(" [MCSERV]: ID=%d, ret=%d\n", j, x);
    mcInit(MC_TYPE_XMC);
#endif
#ifdef USE_ROM_PADMAN
    j = SifLoadStartModule("rom0:PADMAN", 0, NULL, &x);
    DPRINTF(" [PADMAN]: ID=%d, ret=%d\n", j, x);
#else
    j = SifExecModuleBuffer(padman_irx, size_padman_irx, 0, NULL, &x);
    DPRINTF(" [PADMAN]: ID=%d, ret=%d\n", j, x);
#endif

    j = LoadUSBIRX();
    g_usb_modules_loaded = (j == 0);
    if (j != 0) {
        scr_setfontcolor(0x0000ff);
        scr_printf("ERROR: could not load USB modules (%d)\n", j);
        scr_setfontcolor(0xffffff);
#ifdef HAS_EMBEDDED_IRX //we have embedded IRX... something bad is going on if this condition executes. add a wait time for user to know something is wrong
        sleep(1);
#endif
    }

#ifdef FILEXIO
    if (LoadFIO() < 0) {
        scr_setbgcolor(0xff0000);
        scr_clear();
        sleep(4);
    }
#endif

#ifdef MMCE
    j = SifExecModuleBuffer(mmceman_irx, size_mmceman_irx, 0, NULL, &x);
    DPRINTF(" [MMCEMAN]: ID=%d, ret=%d\n", j, x);
    g_mmce_modules_loaded = (j >= 0);
#endif

#ifdef MX4SIO
    j = SifExecModuleBuffer(mx4sio_bd_irx, size_mx4sio_bd_irx, 0, NULL, &x);
    DPRINTF(" [MX4SIO_BD]: ID=%d, ret=%d\n", j, x);
    g_mx4sio_modules_loaded = (j >= 0);
#endif

#ifdef HDD
    else {
        int hdd_ret = LoadHDDIRX(); // only load HDD crap if filexio and iomanx are up and running
        if (hdd_ret < 0) {
            scr_setbgcolor(0x0000ff);
            scr_clear();
            sleep(4);
        } else {
            g_hdd_modules_loaded = 1;
        }
    }
#endif

    if ((fd = open("rom0:ROMVER", O_RDONLY)) >= 0) {
        read(fd, ROMVER, sizeof(ROMVER));
        close(fd);
    }
    j = SifLoadModule("rom0:ADDDRV", 0, NULL); // Load ADDDRV. The OSD has it listed in rom0:OSDCNF/IOPBTCONF, but it is otherwise not loaded automatically.
    DPRINTF(" [ADDDRV]: %d\n", j);

    // Initialize libcdvd & supplement functions (which are not part of the ancient libcdvd library we use).
    sceCdInit(SCECdINoD);
    cdInitAdd();

    DPRINTF("init OSD system paths\n");
    OSDInitSystemPaths();

#ifndef PSX
    DPRINTF("Certifying CDVD Boot\n");
    CDVDBootCertify(ROMVER); /* This is not required for the PSX, as its OSDSYS will do it before booting the update. */
#endif

    DPRINTF("init OSD\n");
    InitOsd(); // Initialize OSD so kernel patches can do their magic

    DPRINTF("init ROMVER, model name ps1dvr and dvdplayer ver\n");
    OSDInitROMVER(); // Initialize ROM version (must be done first).
    ModelNameInit(); // Initialize model name
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
    DPRINTF("Init pads\n");
    PadInitPads();
    DPRINTF("Init timer and wait for rescue mode key\n");
    TimerInit();
    tstart = Timer();
    while (Timer() <= (tstart + 2000)) {
        PAD = ReadCombinedPadStatus();
        if ((PAD & PAD_R1) && (PAD & PAD_START)) // if ONLY R1+START are pressed...
            EMERGENCY();
    }
    TimerEnd();
    DPRINTF("load default settings\n");
    SetDefaultSettings();
    FILE *fp;
    for (x = SOURCE_CWD; x >= SOURCE_MC0; x--) {
        char *T = CheckPath(CONFIG_PATHS[x]);
        fp = fopen(T, "r");
        if (fp != NULL) {
            config_source = x;
            break;
        }
    }

    if (config_source != SOURCE_INVALID) {
        DPRINTF("valid config on device '%s', reading now\n", SOURCES[config_source]);
        pad_button = 0x0001; // on valid config, change the value of `pad_button` so the pad detection loop iterates all the buttons instead of only those configured on default paths
        num_buttons = 16;
        fseek(fp, 0, SEEK_END);
        cnf_size = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        DPRINTF("Allocating %d bytes for config\n", cnf_size);
        config_buf = (unsigned char *)malloc(cnf_size + 1);
        if (config_buf != NULL) {
            CNFBUFF = (char *)config_buf;
            int temp;
            if ((temp = fread(config_buf, 1, cnf_size, fp)) == cnf_size) {
                DPRINTF("Reading finished... Closing fp*\n");
                fclose(fp);
                CNFBUFF[cnf_size] = '\0';
                int var_cnt = 0;
                char TMP[64];
                for (var_cnt = 0; get_CNF_string(&CNFBUFF, &name, &value); var_cnt++) {
                    // Normalize parsed tokens (handle CRLF + surrounding whitespace)
                    name = trim_ws_inplace(name);
                    value = trim_ws_inplace(value);

                    if (name == NULL || *name == '\0')
                        continue;

                    // DPRINTF("reading entry %d", var_cnt);
                    if (ci_eq(name, "OSDHISTORY_READ")) {
                        GLOBCFG.OSDHISTORY_READ = atoi(value);
                        continue;
                    }
                    if (ci_starts_with(name, "LOAD_IRX_E")) {
                        if (value == NULL || *value == '\0') {
                            DPRINTF("# Skipping empty IRX path for config entry [%s]\n", name);
                            continue;
                        }
                        j = SifLoadStartModule(CheckPath(value), 0, NULL, &x);
                        DPRINTF("# Loaded IRX from config entry [%s] -> [%s]: ID=%d, ret=%d\n", name, value, j, x);
                        continue;
                    }
                    if (ci_eq(name, "SKIP_PS2LOGO")) {
                        GLOBCFG.SKIPLOGO = atoi(value);
                        continue;
                    }
                    if (ci_eq(name, "KEY_READ_WAIT_TIME")) {
                        GLOBCFG.DELAY = atoi(value);
                        continue;
                    }
                    if (ci_eq(name, "EJECT_TRAY")) {
                        GLOBCFG.TRAYEJECT = atoi(value);
                        continue;
                    }
                    if (ci_eq(name, "LOGO_DISPLAY")) {
                        GLOBCFG.LOGO_DISP = atoi(value);
                        continue;
                    }
                    if (ci_eq(name, "CDROM_DISABLE_GAMEID")) {
                        GLOBCFG.CDROM_DISABLE_GAMEID = atoi(value);
                        continue;
                    }
                    if (ci_eq(name, "APP_GAMEID")) {
                        GLOBCFG.APP_GAMEID = atoi(value);
                        continue;
                    }
                    if (ci_eq(name, "PS1DRV_ENABLE_FAST")) {
                        GLOBCFG.PS1DRV_ENABLE_FAST = atoi(value);
                        continue;
                    }
                    if (ci_eq(name, "PS1DRV_ENABLE_SMOOTH")) {
                        GLOBCFG.PS1DRV_ENABLE_SMOOTH = atoi(value);
                        continue;
                    }
                    if (ci_eq(name, "PS1DRV_USE_PS1VN")) {
                        GLOBCFG.PS1DRV_USE_PS1VN = atoi(value);
                        continue;
                    }
                    if (ci_starts_with(name, "NAME_")) {
                        for (x = 0; x < 17; x++) {
                            sprintf(TMP, "NAME_%s", KEYS_ID[x]);
                            if (ci_eq(name, TMP)) {
                                if (value == NULL || *value == '\0')
                                    GLOBCFG.KEYNAMES[x] = NULL;
                                else
                                    GLOBCFG.KEYNAMES[x] = value;
                                break;
                            }
                        }
                        continue;
                    }
                    if (ci_starts_with(name, "ARG_")) {
                        for (x = 0; x < 17; x++) {
                            for (j = 0; j < CONFIG_KEY_INDEXES; j++) {
                                sprintf(TMP, "ARG_%s_E%d", KEYS_ID[x], j + 1);
                                if (ci_eq(name, TMP)) {
                                    if (value == NULL || *value == '\0')
                                        break;
                                    if (GLOBCFG.KEYARGC[x][j] < MAX_ARGS_PER_ENTRY) {
                                        GLOBCFG.KEYARGS[x][j][GLOBCFG.KEYARGC[x][j]] = value;
                                        GLOBCFG.KEYARGC[x][j]++;
                                    } else {
                                        DPRINTF("# Too many args for [%s], max=%d\n", name, MAX_ARGS_PER_ENTRY);
                                    }
                                    break;
                                }
                            }
                        }
                        continue;
                    }
                    if (ci_starts_with(name, "LK_")) {
                        for (x = 0; x < 17; x++) {
                            for (j = 0; j < CONFIG_KEY_INDEXES; j++) {
                                sprintf(TMP, "LK_%s_E%d", KEYS_ID[x], j + 1);
                                if (ci_eq(name, TMP)) {
                                    // Empty string means: skip this slot (try next E# when executing)
                                    if (value == NULL || *value == '\0')
                                        GLOBCFG.KEYPATHS[x][j] = NULL;
                                    else
                                        GLOBCFG.KEYPATHS[x][j] = value;
                                    break;
                                }
                            }
                        }
                    }
                }
            } else {
                fclose(fp);
                DPRINTF("\tERROR: could not read %d bytes of config file, only %d read\n", cnf_size, temp);
#ifdef REPORT_FATAL_ERRORS
                scr_setfontcolor(0x0000ff);
                scr_printf("\tERROR: could not read %d bytes of config file, only %d read\n", cnf_size, temp);
                scr_setfontcolor(0xffffff);
#endif
            }
        } else {
            DPRINTF("\tFailed to allocate %d+1 bytes!\n", cnf_size);
#ifdef REPORT_FATAL_ERRORS
            scr_setbgcolor(0x0000ff);
            scr_clear();
            scr_printf("\tFailed to allocate %d+1 bytes!\n", cnf_size);
            sleep(3);
            scr_setbgcolor(0x000000);
            scr_clear();
#endif
        }
#ifdef HDD
        if (config_source == SOURCE_HDD) {

            if (fileXioUmount("pfs0:") < 0)
                DPRINTF("ERROR: Could not unmount 'pfs0:'\n");
        }
#endif
        GLOBCFG.LOGO_DISP = normalize_logo_display(GLOBCFG.LOGO_DISP);
        GLOBCFG.HOTKEY_DISPLAY = logo_to_hotkey_display(GLOBCFG.LOGO_DISP);
        g_pre_scanned = (GLOBCFG.HOTKEY_DISPLAY == 2 || GLOBCFG.HOTKEY_DISPLAY == 3);
        ValidateKeypathsAndSetNames(GLOBCFG.HOTKEY_DISPLAY, g_pre_scanned);
    } else {
        scr_printf("Can't find config, loading hardcoded paths\n");
        for (x = 0; x < 17; x++)
            for (j = 0; j < CONFIG_KEY_INDEXES; j++)
                GLOBCFG.KEYPATHS[x][j] = DEFPATH[CONFIG_KEY_INDEXES * x + j];
        GLOBCFG.LOGO_DISP = normalize_logo_display(LOGO_DISPLAY_DEFAULT);
        GLOBCFG.HOTKEY_DISPLAY = logo_to_hotkey_display(GLOBCFG.LOGO_DISP);
        g_pre_scanned = (GLOBCFG.HOTKEY_DISPLAY == 2 || GLOBCFG.HOTKEY_DISPLAY == 3);
        ValidateKeypathsAndSetNames(GLOBCFG.HOTKEY_DISPLAY, g_pre_scanned);
        sleep(1);
    }

    GameIDSetConfig(GLOBCFG.APP_GAMEID, GLOBCFG.CDROM_DISABLE_GAMEID);
    PS1DRVSetOptions(GLOBCFG.PS1DRV_ENABLE_FAST, GLOBCFG.PS1DRV_ENABLE_SMOOTH, GLOBCFG.PS1DRV_USE_PS1VN);

    int R = 0x80, G = 0x80, B = 0x80;
    u32 banner_color = 0xffffff;
    if (GLOBCFG.OSDHISTORY_READ && (GLOBCFG.LOGO_DISP > 1)) {
        j = 1;
        // Try to load the history file from memory card slot 1
        if (LoadHistoryFile(0) < 0) { // Try memory card slot 2
            if (LoadHistoryFile(1) < 0) {
                DPRINTF("no history files found\n\n");
                j = 0;
            }
        }

        if (j) {
            for (j = 0; j < MAX_HISTORY_ENTRIES; j++) {
                switch (j % 3) {
                    case 0:
                        R += (HistoryEntries[j].LaunchCount * 2);
                        break;
                    case 1:
                        G += (HistoryEntries[j].LaunchCount * 2);
                        break;
                    case 2:
                        B += (HistoryEntries[j].LaunchCount * 2);
                        break;
                    default:
                        B += (HistoryEntries[j].LaunchCount * 2);
                }
            }
            banner_color = RBG2INT(B, G, R);
            DPRINTF("New banner color is: #%8x\n", banner_color);
        } else {
            DPRINTF("can't find any osd history for banner color\n");
        }
    }
    // Stores last key during DELAY msec
    scr_clear();
    if (GLOBCFG.LOGO_DISP >= 3) {
        scr_setfontcolor(banner_color);
        scr_printf("\n%s", BANNER_HOTKEYS);
        scr_setfontcolor(0xffffff);
        if (GLOBCFG.HOTKEY_DISPLAY == 3) {
            if (config_source == SOURCE_INVALID) {
                scr_setfontcolor(0x00ffff);
                scr_printf("%s", BANNER_HOTKEYS_PATHS_HEADER_NOCONFIG);
                scr_setfontcolor(0xffffff);
            } else {
                scr_printf("%s", BANNER_HOTKEYS_PATHS_HEADER);
            }
        }
        PrintHotkeyNamesTemplate((GLOBCFG.HOTKEY_DISPLAY == 3) ? BANNER_HOTKEYS_PATHS : BANNER_HOTKEYS_NAMES);
    } else if (GLOBCFG.LOGO_DISP > 1) {
        scr_setfontcolor(banner_color);
        scr_printf("\n\n\n\n%s", BANNER);
    }
    scr_setfontcolor(0xffffff);
    if (GLOBCFG.LOGO_DISP > 1 && GLOBCFG.LOGO_DISP < 3)
        scr_printf(BANNER_FOOTER);
    if (GLOBCFG.LOGO_DISP > 0) {
        char model_buf[64];
        char ps1_buf[64];
        char dvd_buf[64];
        char src_buf[32];
        char rom_raw[ROMVER_MAX_LEN + 1];
        char rom_buf[32];
        char rom_fmt[8];
        const char *model = strip_crlf_copy(ModelNameGet(), model_buf, sizeof(model_buf));
        const char *ps1ver = strip_crlf_copy(PS1DRVGetVersion(), ps1_buf, sizeof(ps1_buf));
        const char *dvdver = strip_crlf_copy(DVDPlayerGetVersion(), dvd_buf, sizeof(dvd_buf));
        const char *source = strip_crlf_copy(SOURCES[config_source], src_buf, sizeof(src_buf));

        memcpy(rom_raw, ROMVER, ROMVER_MAX_LEN);
        rom_raw[ROMVER_MAX_LEN] = '\0';
        {
            const char *romver = strip_crlf_copy(rom_raw, rom_buf, sizeof(rom_buf));
            char major = (romver[1] != '\0') ? romver[1] : '?';
            char minor1 = (romver[2] != '\0') ? romver[2] : '?';
            char minor2 = (romver[3] != '\0') ? romver[3] : '?';
            char region = (romver[4] != '\0') ? romver[4] : '?';
            snprintf(rom_fmt, sizeof(rom_fmt), "%c.%c%c%c", major, minor1, minor2, region);
        }

        scr_printf("\n  MODEL: %s  ROMVER: %s  DVD: %s  PS1DRV: %s  CFG SRC: %s \n",
                    model,
                    rom_fmt,
                    dvdver,
                    ps1ver,
                    source);
#ifndef NO_TEMP_DISP
        PrintTemperature();
#endif
    }
    DPRINTF("Timer starts!\n");
    TimerInit();
    tstart = Timer();
    int dev_ok[DEV_COUNT];
    build_device_available_cache(dev_ok, DEV_COUNT);
    while (Timer() <= (tstart + GLOBCFG.DELAY)) {
        button = pad_button; // reset the value so we can iterate (bit-shift) again
        PAD = ReadCombinedPadStatus_raw();
        for (x = 0; x < num_buttons; x++) { // check all pad buttons
            if (PAD & button) {
                DPRINTF("PAD detected\n");
                // if button detected, copy path to corresponding index
                for (j = 0; j < CONFIG_KEY_INDEXES; j++) {
                    // Skip empty/unset entries (common when config has blank LK_* values)
                    if (GLOBCFG.KEYPATHS[x + 1][j] == NULL || *GLOBCFG.KEYPATHS[x + 1][j] == '\0')
                        continue;
                    if (g_pre_scanned && !is_command_token(GLOBCFG.KEYPATHS[x + 1][j])) {
                        scr_setfontcolor(0x00ff00);
                        scr_printf("  Loading %s\n", GLOBCFG.KEYPATHS[x + 1][j]);
                        CleanUp();
                        RunLoaderElf(GLOBCFG.KEYPATHS[x + 1][j], MPART, GLOBCFG.KEYARGC[x + 1][j], GLOBCFG.KEYARGS[x + 1][j]);
                        break;
                    }
                    if (!device_available_for_path_cached(GLOBCFG.KEYPATHS[x + 1][j], dev_ok))
                        continue;
                    if (!strncmp(GLOBCFG.KEYPATHS[x + 1][j], "mc?:", 4)) {
                        char preferred = (config_source == SOURCE_MC1) ? '1' : '0';
                        if (!resolve_pair_path(GLOBCFG.KEYPATHS[x + 1][j], 2, preferred, &EXECPATHS[j])) {
                            scr_setfontcolor(0x00ffff);
                            DPRINTF("%s not found\n", EXECPATHS[j]);
                            scr_setfontcolor(0xffffff);
                            continue;
                        }
#ifdef MMCE
                    } else if (!strncmp(GLOBCFG.KEYPATHS[x + 1][j], "mmce?:", 6)) {
                        char preferred = preferred_mmce_slot();
                        if (!resolve_pair_path(GLOBCFG.KEYPATHS[x + 1][j], 4, preferred, &EXECPATHS[j])) {
                            scr_setfontcolor(0x00ffff);
                            DPRINTF("%s not found\n", EXECPATHS[j]);
                            scr_setfontcolor(0xffffff);
                            continue;
                        }
#endif
                    } else {
                        EXECPATHS[j] = CheckPath(GLOBCFG.KEYPATHS[x + 1][j]);
                        if (!exist(EXECPATHS[j])) {
                            scr_setfontcolor(0x00ffff);
                            DPRINTF("%s not found\n", EXECPATHS[j]);
                            scr_setfontcolor(0xffffff);
                            continue;
                        }
                    }
                    if (EXECPATHS[j] != NULL && *EXECPATHS[j] != '\0') {
                        scr_setfontcolor(0x00ff00);
                        scr_printf("  Loading %s\n", EXECPATHS[j]);
                        CleanUp();
                        RunLoaderElf(EXECPATHS[j], MPART, GLOBCFG.KEYARGC[x + 1][j], GLOBCFG.KEYARGS[x + 1][j]);
                    }
                }
                break;
            }
            button = button << 1; // sll of 1 cleared bit to move to next pad button
        }
    }
    DPRINTF("Wait time consummed. Running AUTO entry\n");
    TimerEnd();
    build_device_available_cache(dev_ok, DEV_COUNT);
    for (j = 0; j < CONFIG_KEY_INDEXES; j++) {
        // Skip empty/unset AUTO entries too
        if (GLOBCFG.KEYPATHS[0][j] == NULL || *GLOBCFG.KEYPATHS[0][j] == '\0')
            continue;
        if (is_command_token(GLOBCFG.KEYPATHS[0][j]))
            continue; // Don't execute commands without a key press.
        if (g_pre_scanned && !is_command_token(GLOBCFG.KEYPATHS[0][j])) {
            scr_setfontcolor(0x00ff00);
            scr_printf("  Loading %s\n", GLOBCFG.KEYPATHS[0][j]);
            CleanUp();
            RunLoaderElf(GLOBCFG.KEYPATHS[0][j], MPART, GLOBCFG.KEYARGC[0][j], GLOBCFG.KEYARGS[0][j]);
            break;
        }
        if (!device_available_for_path_cached(GLOBCFG.KEYPATHS[0][j], dev_ok))
            continue;
        if (!strncmp(GLOBCFG.KEYPATHS[0][j], "mc?:", 4)) {
            char preferred = (config_source == SOURCE_MC1) ? '1' : '0';
            if (!resolve_pair_path(GLOBCFG.KEYPATHS[0][j], 2, preferred, &EXECPATHS[j])) {
                scr_printf("%s %-15s\r", EXECPATHS[j], "not found");
                continue;
            }
#ifdef MMCE
        } else if (!strncmp(GLOBCFG.KEYPATHS[0][j], "mmce?:", 6)) {
            char preferred = preferred_mmce_slot();
            if (!resolve_pair_path(GLOBCFG.KEYPATHS[0][j], 4, preferred, &EXECPATHS[j])) {
                scr_printf("%s %-15s\r", EXECPATHS[j], "not found");
                continue;
            }
#endif
        } else {
            EXECPATHS[j] = CheckPath(GLOBCFG.KEYPATHS[0][j]);
            if (!exist(EXECPATHS[j])) {
                scr_printf("%s %-15s\r", EXECPATHS[j], "not found");
                continue;
            }
        }
        if (EXECPATHS[j] != NULL && *EXECPATHS[j] != '\0') {
            scr_setfontcolor(0x00ff00);
            scr_printf("  Loading %s\n", EXECPATHS[j]);
            CleanUp();
            RunLoaderElf(EXECPATHS[j], MPART, GLOBCFG.KEYARGC[0][j], GLOBCFG.KEYARGS[0][j]);
        }
    }

    scr_clear();
    scr_setfontcolor(0x00ffff);
    scr_printf("\n\n\t\tEND OF EXECUTION REACHED\n\t\tCould not find any of the default applications\n\t\tCheck your config file for the LK_AUTO_E# entries\n\t\tOr press a key while logo displays to run the bound application\n\n\t\tPress R1+START to launch mass:/RESCUE.ELF");
    scr_setfontcolor(0xffffff);
    while (1) {
        sleep(1);
        PAD = ReadCombinedPadStatus_raw();
        if ((PAD & PAD_R1) && (PAD & PAD_START)) // if ONLY R1+START are pressed...
            EMERGENCY();
    }

    return 0;
}

void EMERGENCY(void)
{
    scr_clear();
    scr_setfontcolor(0x0000ff);
    scr_printf("\n\n\n\t\tUSB EMERGENCY MODE!\n\n");
    scr_setfontcolor(0x00ffff);
    scr_printf("\t\tSearching for mass:/RESCUE.ELF\n\n\t\tTIP: Download uLaunchELF/wLaunchELF\n\t\t\tand rename to RESCUE.ELF\n");
    scr_setfontcolor(0xffffff);
    const int dot_width = 40;
    char dots[41];
    int dot_count = 0;

    dots[0] = '\0';
    while (1) {
        dot_count = (dot_count % dot_width) + 1;
        memset(dots, '.', dot_count);
        dots[dot_count] = '\0';
        scr_printf("\r\t\t%-*s", dot_width, dots);
        sleep(100000);
        if (exist("mass:/RESCUE.ELF")) {
            CleanUp();
            RunLoaderElf("mass:/RESCUE.ELF", NULL, 0, NULL);
        }
    }
}

void runKELF(const char *kelfpath)
{
    char arg3[64];
    char *args[4] = {"-m rom0:SIO2MAN", "-m rom0:MCMAN", "-m rom0:MCSERV", arg3};
    sprintf(arg3, "-x %s", kelfpath);

    PadDeinitPads();
    LoadExecPS2("moduleload", 4, args);
}

char *CheckPath(char *path)
{
    if (path[0] == '$') // we found a program command
    {
        if (!strcmp("$CDVD", path))
            dischandler();
        if (!strcmp("$CDVD_NO_PS2LOGO", path)) {
            GLOBCFG.SKIPLOGO = 1;
            dischandler();
        }
#ifdef HDD
        if (!strcmp("$HDDCHECKER", path))
            HDDChecker();
#endif
        if (!strcmp("$CREDITS", path))
            credits();
        if (!strcmp("$OSDSYS", path))
            runOSDNoUpdate();
        if (!strncmp("$RUNKELF:", path, strlen("$RUNKELF:"))) {
            runKELF(CheckPath(path + strlen("$RUNKELF:"))); // pass to runKELF the path without the command token, digested again by CheckPath()
        }
    }
    if (!strncmp("mc?", path, 3)) {
        char preferred = (config_source == SOURCE_MC1) ? '1' : '0';
        if (resolve_pair_path(path, 2, preferred, &path))
            return path;
#ifdef MMCE
    } else if (!strncmp("mmce?", path, 5)) {
        char preferred = preferred_mmce_slot();
        if (resolve_pair_path(path, 4, preferred, &path))
            return path;
#endif
#ifdef HDD
    } else if (!strncmp("hdd", path, 3)) {
        if (MountParty(path) < 0) {
            DPRINTF("-{%s}-\n", path);
            return path;
        } else {
            DPRINTF("--{%s}--{%s}\n", path, strstr(path, "pfs:"));
            return strstr(path, "pfs:");
        } // leave path as pfs:/blabla
        if (!MountParty(path))
            return strstr(path, "pfs:");
#endif
#ifdef MX4SIO
    } else if (!strncmp("massX:", path, 6)) {
        int x = get_mx4sio_slot();
        if (x >= 0)
            path[4] = '0' + x;
#endif
    }
    return path;
}

void SetDefaultSettings(void)
{
    int i, j;
    for (i = 0; i < 17; i++)
        for (j = 0; j < CONFIG_KEY_INDEXES; j++) {
            GLOBCFG.KEYPATHS[i][j] = "isra:/";
            GLOBCFG.KEYARGC[i][j] = 0;
            memset(GLOBCFG.KEYARGS[i][j], 0, sizeof(GLOBCFG.KEYARGS[i][j]));
        }
    for (i = 0; i < 17; i++)
        GLOBCFG.KEYNAMES[i] = DEFAULT_KEYNAMES[i];
    GLOBCFG.SKIPLOGO = 0;
    GLOBCFG.OSDHISTORY_READ = 1;
    GLOBCFG.DELAY = DEFDELAY;
    GLOBCFG.TRAYEJECT = 0;
    GLOBCFG.LOGO_DISP = 3;
    GLOBCFG.HOTKEY_DISPLAY = logo_to_hotkey_display(GLOBCFG.LOGO_DISP);
    GLOBCFG.CDROM_DISABLE_GAMEID = CDROM_DISABLE_GAMEID_DEFAULT;
    GLOBCFG.APP_GAMEID = APP_GAMEID_DEFAULT;
    GLOBCFG.PS1DRV_ENABLE_FAST = PS1DRV_ENABLE_FAST_DEFAULT;
    GLOBCFG.PS1DRV_ENABLE_SMOOTH = PS1DRV_ENABLE_SMOOTH_DEFAULT;
    GLOBCFG.PS1DRV_USE_PS1VN = PS1DRV_USE_PS1VN_DEFAULT;
    GameIDSetConfig(GLOBCFG.APP_GAMEID, GLOBCFG.CDROM_DISABLE_GAMEID);
}

int LoadUSBIRX(void)
{
    int ID, RET;

// ------------------------------------------------------------------------------------ //
#ifdef HAS_EMBEDDED_IRX
    ID = SifExecModuleBuffer(bdm_irx, size_bdm_irx, 0, NULL, &RET);
#else
    ID = SifLoadStartModule(CheckPath("mc?:/PS2BBL/BDM.IRX"), 0, NULL, &RET);
#endif
    DPRINTF(" [BDM]: ret=%d, ID=%d\n", RET, ID);
    if (ID < 0 || RET == 1)
        return -1;
// ------------------------------------------------------------------------------------ //
#ifdef HAS_EMBEDDED_IRX
    ID = SifExecModuleBuffer(bdmfs_fatfs_irx, size_bdmfs_fatfs_irx, 0, NULL, &RET);
#else
    ID = SifLoadStartModule(CheckPath("mc?:/PS2BBL/BDMFS_FATFS.IRX"), 0, NULL, &RET);
#endif
    DPRINTF(" [BDMFS_FATFS]: ret=%d, ID=%d\n", RET, ID);
    if (ID < 0 || RET == 1)
        return -2;
// ------------------------------------------------------------------------------------ //
#ifdef HAS_EMBEDDED_IRX
    ID = SifExecModuleBuffer(usbd_irx, size_usbd_irx, 0, NULL, &RET);
#else
    ID = SifLoadStartModule(CheckPath("mc?:/PS2BBL/USBD.IRX"), 0, NULL, &RET);
#endif
    delay(3);
    DPRINTF(" [USBD]: ret=%d, ID=%d\n", RET, ID);
    if (ID < 0 || RET == 1)
        return -3;
// ------------------------------------------------------------------------------------ //
#ifdef HAS_EMBEDDED_IRX
    ID = SifExecModuleBuffer(usbmass_bd_irx, size_usbmass_bd_irx, 0, NULL, &RET);
#else
    ID = SifLoadStartModule(CheckPath("mc?:/PS2BBL/USBMASS_BD.IRX"), 0, NULL, &RET);
#endif
    DPRINTF(" [USBMASS_BD]: ret=%d, ID=%d\n", RET, ID);
    if (ID < 0 || RET == 1)
        return -4;
    // ------------------------------------------------------------------------------------ //
    struct stat buffer;
    int ret = -1;
    int retries = 50;

    while (ret != 0 && retries > 0) {
        ret = stat("mass:/", &buffer);
        /* Wait until the device is ready */
        nopdelay();

        retries--;
    }
    return 0;
}


#ifdef MX4SIO
int LookForBDMDevice(void)
{
    static char mass_path[] = "massX:";
    static char DEVID[5];
    int dd;
    int x = 0;
    for (x = 0; x < 5; x++) {
        mass_path[4] = '0' + x;
        if ((dd = fileXioDopen(mass_path)) >= 0) {
            int *intptr_ctl = (int *)DEVID;
            *intptr_ctl = fileXioIoctl(dd, USBMASS_IOCTL_GET_DRIVERNAME, "");
            close(dd);
            if (!strncmp(DEVID, "sdc", 3)) {
                DPRINTF("%s: Found MX4SIO device at mass%d:/\n", __func__, x);
                return x;
            }
        }
    }
    return -1;
}
#endif


#ifdef FILEXIO
int LoadFIO(void)
{
    int ID, RET;
    ID = SifExecModuleBuffer(&iomanX_irx, size_iomanX_irx, 0, NULL, &RET);
    DPRINTF(" [IOMANX]: ret=%d, ID=%d\n", RET, ID);
    if (ID < 0 || RET == 1)
        return -1;

    /* FILEXIO.IRX */
    ID = SifExecModuleBuffer(&fileXio_irx, size_fileXio_irx, 0, NULL, &RET);
    DPRINTF(" [FILEXIO]: ret=%d, ID=%d\n", RET, ID);
    if (ID < 0 || RET == 1)
        return -2;

    RET = fileXioInit();
    DPRINTF("fileXioInit: %d\n", RET);
    return 0;
}
#endif

#ifdef DEV9
int loadDEV9(void)
{
    if (!dev9_loaded) {
        int ID, RET;
        ID = SifExecModuleBuffer(&ps2dev9_irx, size_ps2dev9_irx, 0, NULL, &RET);
        DPRINTF("[DEV9]: ret=%d, ID=%d\n", RET, ID);
        if (ID < 0 && RET == 1) // ID smaller than 0: issue reported from modload | RET == 1: driver returned no resident end
            return 0;
        dev9_loaded = 1;
    }
    return 1;
}
#endif

#ifdef UDPTTY
void loadUDPTTY()
{
    int ID, RET;
    ID = SifExecModuleBuffer(&netman_irx, size_netman_irx, 0, NULL, &RET);
    DPRINTF(" [NETMAN]: ret=%d, ID=%d\n", RET, ID);
    ID = SifExecModuleBuffer(&smap_irx, size_smap_irx, 0, NULL, &RET);
    DPRINTF(" [SMAP]: ret=%d, ID=%d\n", RET, ID);
    ID = SifExecModuleBuffer(&ps2ip_irx, size_ps2ip_irx, 0, NULL, &RET);
    DPRINTF(" [PS2IP]: ret=%d, ID=%d\n", RET, ID);
    ID = SifExecModuleBuffer(&udptty_irx, size_udptty_irx, 0, NULL, &RET);
    DPRINTF(" [UDPTTY]: ret=%d, ID=%d\n", RET, ID);
    sleep(3);
}
#endif

#ifdef HDD
static int CheckHDD(void)
{
    int ret = fileXioDevctl("hdd0:", HDIOC_STATUS, NULL, 0, NULL, 0);
    /* 0 = HDD connected and formatted, 1 = not formatted, 2 = HDD not usable, 3 = HDD not connected. */
    DPRINTF("%s: HDD status is %d\n", __func__, ret);
    if ((ret >= 3) || (ret < 0))
        return -1;
    return ret;
}

int LoadHDDIRX(void)
{
    int ID, RET, HDDSTAT;
    static const char hddarg[] = "-o"
                                 "\0"
                                 "4"
                                 "\0"
                                 "-n"
                                 "\0"
                                 "20";
    //static const char pfsarg[] = "-n\0" "24\0" "-o\0" "8";

    if (!loadDEV9())
        return -1;

    ID = SifExecModuleBuffer(&poweroff_irx, size_poweroff_irx, 0, NULL, &RET);
    DPRINTF(" [POWEROFF]: ret=%d, ID=%d\n", RET, ID);
    if (ID < 0 || RET == 1)
        return -2;

    poweroffInit();
    poweroffSetCallback(&poweroffCallback, NULL);
    DPRINTF("PowerOFF Callback installed...\n");

    ID = SifExecModuleBuffer(&ps2atad_irx, size_ps2atad_irx, 0, NULL, &RET);
    DPRINTF(" [ATAD]: ret=%d, ID=%d\n", RET, ID);
    if (ID < 0 || RET == 1)
        return -3;

    ID = SifExecModuleBuffer(&ps2hdd_irx, size_ps2hdd_irx, sizeof(hddarg), hddarg, &RET);
    DPRINTF(" [PS2HDD]: ret=%d, ID=%d\n", RET, ID);
    if (ID < 0 || RET == 1)
        return -4;

    HDDSTAT = CheckHDD();
    HDD_USABLE = !(HDDSTAT < 0);

    /* PS2FS.IRX */
    if (HDD_USABLE) {
        ID = SifExecModuleBuffer(&ps2fs_irx, size_ps2fs_irx, 0, NULL, &RET);
        DPRINTF(" [PS2FS]: ret=%d, ID=%d\n", RET, ID);
        if (ID < 0 || RET == 1)
            return -5;
    }

    return 0;
}

int MountParty(const char *path)
{
    int ret = -1;
    DPRINTF("%s: %s\n", __func__, path);
    char *BUF = NULL;
    BUF = strdup(path); //use strdup, otherwise, path will become `hdd0:`
    char MountPoint[40];
    if (getMountInfo(BUF, NULL, MountPoint, NULL)) {
        mnt(MountPoint);
        if (BUF != NULL)
            free(BUF);
        strcpy(PART, MountPoint);
        strcat(PART, ":");
        return 0;
    } else {
        DPRINTF("ERROR: could not process path '%s'\n", path);
        PART[0] = '\0';
    }
    if (BUF != NULL)
        free(BUF);
    return ret;
}

int mnt(const char *path)
{
    DPRINTF("Mounting '%s'\n", path);
    if (fileXioMount("pfs0:", path, FIO_MT_RDONLY) < 0) // mount
    {
        DPRINTF("Mount failed. unmounting pfs0 and trying again...\n");
        if (fileXioUmount("pfs0:") < 0) //try to unmount then mount again in case it got mounted by something else
        {
            DPRINTF("Unmount failed!!!\n");
        }
        if (fileXioMount("pfs0:", path, FIO_MT_RDONLY) < 0) {
            DPRINTF("mount failed again!\n");
            return -4;
        } else {
            DPRINTF("Second mount succed!\n");
        }
    } else
        DPRINTF("mount successfull on first attemp\n");
    return 0;
}

void HDDChecker()
{
    char ErrorPartName[64];
    const char *HEADING = "HDD Diagnosis routine";
    int ret = -1;
    scr_clear();
    scr_printf("\n\n%*s%s\n", ((80 - strlen(HEADING)) / 2), "", HEADING);
    scr_setfontcolor(0x0000FF);
    ret = fileXioDevctl("hdd0:", HDIOC_STATUS, NULL, 0, NULL, 0);
    if (ret == 0 || ret == 1)
        scr_setfontcolor(0x00FF00);
    if (ret != 3) {
        scr_printf("\t\t - HDD CONNECTION STATUS: %d\n", ret);
        /* Check ATA device S.M.A.R.T. status. */
        ret = fileXioDevctl("hdd0:", HDIOC_SMARTSTAT, NULL, 0, NULL, 0);
        if (ret != 0)
            scr_setfontcolor(0x0000ff);
        else
            scr_setfontcolor(0x00FF00);
        scr_printf("\t\t - S.M.A.R.T STATUS: %d\n", ret);
        /* Check for unrecoverable I/O errors on sectors. */
        ret = fileXioDevctl("hdd0:", HDIOC_GETSECTORERROR, NULL, 0, NULL, 0);
        if (ret != 0)
            scr_setfontcolor(0x0000ff);
        else
            scr_setfontcolor(0x00FF00);
        scr_printf("\t\t - SECTOR ERRORS: %d\n", ret);
        /* Check for partitions that have errors. */
        ret = fileXioDevctl("hdd0:", HDIOC_GETERRORPARTNAME, NULL, 0, ErrorPartName, sizeof(ErrorPartName));
        if (ret != 0)
            scr_setfontcolor(0x0000ff);
        else
            scr_setfontcolor(0x00FF00);
        scr_printf("\t\t - CORRUPTED PARTITIONS: %d\n", ret);
        if (ret != 0) {
            scr_printf("\t\tpartition: %s\n", ErrorPartName);
        }
    } else
        scr_setfontcolor(0x00FFFF), scr_printf("Skipping test, HDD is not connected\n");
    scr_setfontcolor(0xFFFFFF);
    scr_printf("\t\tWaiting for 10 seconds...\n");
    sleep(10);
}
/// @brief poweroff callback function
/// @note only expansion bay models will properly make use of this. the other models will run the callback but will poweroff themselves before reaching function end...
void poweroffCallback(void *arg)
{
    fileXioDevctl("pfs:", PDIOC_CLOSEALL, NULL, 0, NULL, 0);
    while (fileXioDevctl("dev9x:", DDIOC_OFF, NULL, 0, NULL, 0) < 0) {};
    // As required by some (typically 2.5") HDDs, issue the SCSI STOP UNIT command to avoid causing an emergency park.
    fileXioDevctl("mass:", USBMASS_DEVCTL_STOP_ALL, NULL, 0, NULL, 0);
    /* Power-off the PlayStation 2. */
    poweroffShutdown();
}

#endif
int dischandler()
{
    int OldDiscType, DiscType, ValidDiscInserted, result, first_run = 1;
    u32 STAT;

    scr_clear();
    scr_printf("\n\t%s: Activated\n", __func__);

    scr_printf("\t\tEnabling Diagnosis...\n");
    do { // 0 = enable, 1 = disable.
        result = sceCdAutoAdjustCtrl(0, &STAT);
    } while ((STAT & 0x08) || (result == 0));

    // For this demo, wait for a valid disc to be inserted.
    scr_printf("\tWaiting for disc to be inserted...\n\n");

    ValidDiscInserted = 0;
    OldDiscType = -1;
    while (!ValidDiscInserted) {
        DiscType = sceCdGetDiskType();
        if (DiscType != OldDiscType) {
            scr_printf("\tNew Disc:\t");
            OldDiscType = DiscType;

            switch (DiscType) {
                case SCECdNODISC:
                    if (first_run) {
                        if (GLOBCFG.TRAYEJECT) // if tray eject is allowed on empty tray...
                            sceCdTrayReq(0, NULL);
                        first_run = 0;
                    }
                    scr_setfontcolor(0x0000ff);
                    scr_printf("No Disc\n");
                    scr_setfontcolor(0xffffff);
                    break;

                case SCECdDETCT:
                case SCECdDETCTCD:
                case SCECdDETCTDVDS:
                case SCECdDETCTDVDD:
                    scr_printf("Reading...\n");
                    break;

                case SCECdPSCD:
                case SCECdPSCDDA:
                    scr_setfontcolor(0x00ff00);
                    scr_printf("PlayStation\n");
                    scr_setfontcolor(0xffffff);
                    ValidDiscInserted = 1;
                    break;

                case SCECdPS2CD:
                case SCECdPS2CDDA:
                case SCECdPS2DVD:
                    scr_setfontcolor(0x00ff00);
                    scr_printf("PlayStation 2\n");
                    scr_setfontcolor(0xffffff);
                    ValidDiscInserted = 1;
                    break;

                case SCECdCDDA:
                    scr_setfontcolor(0xffff00);
                    scr_printf("Audio Disc (not supported by this program)\n");
                    scr_setfontcolor(0xffffff);
                    break;

                case SCECdDVDV:
                    scr_setfontcolor(0x00ff00);
                    scr_printf("DVD Video\n");
                    scr_setfontcolor(0xffffff);
                    ValidDiscInserted = 1;
                    break;
                default:
                    scr_setfontcolor(0x0000ff);
                    scr_printf("Unknown (%d)\n", DiscType);
                    scr_setfontcolor(0xffffff);
            }
        }

        // Avoid spamming the IOP with sceCdGetDiskType(), or there may be a deadlock.
        // The NTSC/PAL H-sync is approximately 16kHz. Hence approximately 16 ticks will pass every millisecond.
        SetAlarm(1000 * 16, &AlarmCallback, (void *)GetThreadId());
        SleepThread();
    }

    // Now that a valid disc is inserted, do something.
    // CleanUp() will be called, to deinitialize RPCs. SIFRPC will be deinitialized by the respective disc-handlers.
    switch (DiscType) {
        case SCECdPSCD:
        case SCECdPSCDDA:
            // Boot PlayStation disc
            PS1DRVBoot();
            break;

        case SCECdPS2CD:
        case SCECdPS2CDDA:
        case SCECdPS2DVD:
            // Boot PlayStation 2 disc
            PS2DiscBoot(GLOBCFG.SKIPLOGO);
            break;

        case SCECdDVDV:
            /*  If the user chose to disable the DVD Player progressive scan setting,
                it is disabled here because Sony probably wanted the setting to only bind if the user played a DVD.
                The original did the updating of the EEPROM in the background, but I want to keep this demo simple.
                The browser only allowed this setting to be disabled, by only showing the menu option for it if it was enabled by the DVD Player. */
            /* OSDConfigSetDVDPProgressive(0);
            OSDConfigApply(); */

            /*  Boot DVD Player. If one is stored on the memory card and is newer, it is booted instead of the one from ROM.
                Play history is automatically updated. */
            DVDPlayerBoot();
            break;
    }
    return 0;
}

void ResetIOP(void)
{
    SifInitRpc(0); // Initialize SIFCMD & SIFRPC
#ifndef PSX
    while (!SifIopReset("", 0)) {};
#else
    /* sp193: We need some of the PSX's CDVDMAN facilities, but we do not want to use its (too-)new FILEIO module.
       This special IOPRP image contains a IOPBTCONF list that lists PCDVDMAN instead of CDVDMAN.
       PCDVDMAN is the board-specific CDVDMAN module on all PSX, which can be used to switch the CD/DVD drive operating mode.
       Usually, I would discourage people from using board-specific modules, but I do not have a proper replacement for this. */
    while (!SifIopRebootBuffer(psx_ioprp, size_psx_ioprp)) {};
#endif
    while (!SifIopSync()) {};

#ifdef PSX
    InitPSX();
#endif
}

#ifdef PSX
static void InitPSX()
{
    int result, STAT;

    SifInitRpc(0);
    sceCdInit(SCECdINoD);

    // No need to perform boot certification because rom0:OSDSYS does it.
    while (custom_sceCdChgSys(sceDVRTrayModePS2) != sceDVRTrayModePS2) {}; // Switch the drive into PS2 mode.

    do {
        result = custom_sceCdNoticeGameStart(1, &STAT);
    } while ((result == 0) || (STAT & 0x80));

    // Reset the IOP again to get the standard PS2 default modules.
    while (!SifIopReset("", 0)) {};

    /*    Set the EE kernel into 32MB mode. Let's do this, while the IOP is being reboot.
        The memory will be limited with the TLB. The remap can be triggered by calling the _InitTLB syscall
        or with ExecPS2().
        WARNING! If the stack pointer resides above the 32MB offset at the point of remap, a TLB exception will occur.
        This example has the stack pointer configured to be within the 32MB limit. */

    /// WARNING: until further investigation, the memory mode should remain on 64mb. changing it to 32 breaks SDK ELF Loader
    /// SetMemoryMode(1);
    ///_InitTLB();

    while (!SifIopSync()) {};
}
#endif

#ifndef PSX
void CDVDBootCertify(u8 romver[16])
{
    u8 RomName[4];
    /*  Perform boot certification to enable the CD/DVD drive.
        This is not required for the PSX, as its OSDSYS will do it before booting the update. */
    if (romver != NULL) {
        // e.g. 0160HC = 1,60,'H','C'
        RomName[0] = (romver[0] - '0') * 10 + (romver[1] - '0');
        RomName[1] = (romver[2] - '0') * 10 + (romver[3] - '0');
        RomName[2] = romver[4];
        RomName[3] = romver[5];

        // Do not check for success/failure. Early consoles do not support (and do not require) boot-certification.
        sceCdBootCertify(RomName);
#ifdef REPORT_FATAL_ERRORS
    } else {
        scr_setfontcolor(0x0000ff);
        scr_printf("\tERROR: Could not certify CDVD Boot. ROMVER was NULL\n");
        scr_setfontcolor(0xffffff);
#endif
    }

    // This disables DVD Video Disc playback. This functionality is restored by loading a DVD Player KELF.
    /*    Hmm. What should the check for STAT be? In v1.xx, it seems to be a check against 0x08. In v2.20, it checks against 0x80.
          The HDD Browser does not call this function, but I guess it would check against 0x08. */
    /*  do
     {
         sceCdForbidDVDP(&STAT);
     } while (STAT & 0x08); */
}
#endif

static void AlarmCallback(s32 alarm_id, u16 time, void *common)
{
    iWakeupThread((int)common);
}

void CleanUp(void)
{
    sceCdInit(SCECdEXIT);
    // Keep config_buf alive so argv pointers remain valid during ELF load.
    PadDeinitPads();
}

void credits(void)
{
    scr_clear();
    scr_printf("\n\n");
    scr_printf("%s%s", BANNER, BANNER_FOOTER);
    scr_printf("\n"
               "\n"
               "\tBased on SP193 OSD Init samples.\n"
               "\t\tall credits go to him\n"
               "\tThanks to: fjtrujy, uyjulian, asmblur and AKuHAK\n"
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

    );
    while (1) {};
}

void runOSDNoUpdate(void)
{
    char *args[3] = {"SkipHdd", "BootBrowser", "SkipMc"};
    CleanUp();
    SifExitCmd();
    ExecOSD(3, args);
}

#ifndef NO_TEMP_DISP
void PrintTemperature()
{
    // Based on PS2Ident libxcdvd from SP193
    unsigned char in_buffer[1], out_buffer[16];
    int stat = 0;

    memset(&out_buffer, 0, 16);

    in_buffer[0] = 0xEF;
    if (sceCdApplySCmd(0x03, in_buffer, sizeof(in_buffer), out_buffer /*, sizeof(out_buffer)*/) != 0) {
        stat = out_buffer[0];
    }

    if (!stat) {
        unsigned short temp = out_buffer[1] * 256 + out_buffer[2];
        scr_printf("  Temp: %02d.%02dC\n", (temp - (temp % 128)) / 128, (temp % 128));
    } else {
        DPRINTF("Failed 0x03 0xEF command. stat=%x \n", stat);
    }
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
