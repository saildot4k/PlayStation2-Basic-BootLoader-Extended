#include <stdint.h>

#include "main.h"
#include "game_id.h"
#include "egsm_parse.h"
#include "splash_screen.h"
#include "splash_render.h"
#include "ee_asm.h"

static int g_pending_command_argc;
static char **g_pending_command_argv;
static int g_is_psx_desr = 0;

#define VIDEO_SELECTOR_TEXT_COLOR 0xffffff
#define VIDEO_SELECTOR_HIGHLIGHT_COLOR 0x15d670
#define VIDEO_SELECTOR_SELECT_COLOR 0xffff00
#define MISSING_PATH_RESCUE_CHORD_WINDOW_MS 250
#define PAD_MASK_RIGHT 0x0020
#define PAD_MASK_LEFT 0x0080
#define PAD_MASK_SELECT 0x0001
#define PAD_MASK_TRIANGLE 0x1000
#define PAD_MASK_CROSS 0x4000
#define PAD_MASK_ANY 0xffff

static void SplashDrawCenteredStatusWithInfo(const char *text,
                                             u32 color,
                                             const char *model,
                                             const char *rom_fmt,
                                             const char *dvdver,
                                             const char *ps1ver,
                                             const char *temp_celsius,
                                             const char *source);
static void RunEmergencyMode(const char *reason);

static void ClearStaleEEDebugState(void)
{
    // Emulator and some warm-boot paths can preserve BPC/watch registers.
    // Clear them before any GS traffic to prevent immediate EL2 traps.
    _ee_disable_bpc();
    _ee_mtiab(0);
    _ee_mtiabm(0);
    _ee_mtdab(0);
    _ee_mtdabm(0);
    _ee_mtdvb(0);
    _ee_mtdvbm(0);
    _ee_sync_p();
}

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

enum {
    CFG_VIDEO_MODE_AUTO = 0,
    CFG_VIDEO_MODE_NTSC,
    CFG_VIDEO_MODE_PAL,
    CFG_VIDEO_MODE_480P
};

enum {
    GS_VIDEO_MODE_NTSC = 2,
    GS_VIDEO_MODE_PAL = 3,
    GS_VIDEO_MODE_480P = 0x50
};

static int g_native_video_mode = CFG_VIDEO_MODE_NTSC;

static int parse_video_mode_value(const char *value, int *out_mode)
{
    int parsed_mode;

    if (value == NULL)
        return 0;

    if (ci_eq(value, "AUTO"))
        parsed_mode = CFG_VIDEO_MODE_AUTO;
    else if (ci_eq(value, "NTSC"))
        parsed_mode = CFG_VIDEO_MODE_NTSC;
    else if (ci_eq(value, "PAL"))
        parsed_mode = CFG_VIDEO_MODE_PAL;
    else if (ci_eq(value, "480P"))
        parsed_mode = CFG_VIDEO_MODE_480P;
    else
        return 0;

    if (out_mode != NULL)
        *out_mode = parsed_mode;

    return 1;
}

static int detect_native_video_mode(void)
{
    return (OSDGetVideoMode() != 0) ? CFG_VIDEO_MODE_PAL : CFG_VIDEO_MODE_NTSC;
}

static void apply_loader_video_mode(int cfg_mode)
{
    int effective_mode = cfg_mode;
    short interlace = 1;
    short ffmd = 1;
    short gs_mode = GS_VIDEO_MODE_NTSC;

    if (effective_mode == CFG_VIDEO_MODE_AUTO)
        effective_mode = g_native_video_mode;

    switch (effective_mode) {
        case CFG_VIDEO_MODE_PAL:
            gs_mode = GS_VIDEO_MODE_PAL;
            break;
        case CFG_VIDEO_MODE_480P:
            interlace = 0;
            gs_mode = GS_VIDEO_MODE_480P;
            break;
        case CFG_VIDEO_MODE_NTSC:
        default:
            gs_mode = GS_VIDEO_MODE_NTSC;
            break;
    }

    SetGsCrt(interlace, gs_mode, ffmd);
}

static int resolve_effective_video_mode(int cfg_mode)
{
    if (cfg_mode == CFG_VIDEO_MODE_AUTO)
        return g_native_video_mode;

    switch (cfg_mode) {
        case CFG_VIDEO_MODE_PAL:
            return CFG_VIDEO_MODE_PAL;
        case CFG_VIDEO_MODE_480P:
            return CFG_VIDEO_MODE_480P;
        case CFG_VIDEO_MODE_NTSC:
        default:
            return CFG_VIDEO_MODE_NTSC;
    }
}

static const char *video_mode_label(int cfg_mode)
{
    switch (cfg_mode) {
        case CFG_VIDEO_MODE_NTSC:
            return "NTSC";
        case CFG_VIDEO_MODE_PAL:
            return "PAL";
        case CFG_VIDEO_MODE_480P:
            return "480P";
        case CFG_VIDEO_MODE_AUTO:
        default:
            return "AUTO";
    }
}

static int step_video_mode(int current_mode, int direction)
{
    static const int mode_order[] = {
        CFG_VIDEO_MODE_AUTO,
        CFG_VIDEO_MODE_NTSC,
        CFG_VIDEO_MODE_PAL,
        CFG_VIDEO_MODE_480P
    };
    int i;
    int index = 0;
    int count = (int)(sizeof(mode_order) / sizeof(mode_order[0]));

    for (i = 0; i < count; i++) {
        if (mode_order[i] == current_mode) {
            index = i;
            break;
        }
    }

    if (direction > 0)
        index = (index + 1) % count;
    else
        index = (index + count - 1) % count;

    return mode_order[index];
}

#ifndef NO_TEMP_DISP
// Query CDVD thermal sensor and return formatted Celsius string when supported.
static int QueryTemperatureCelsius(char *temp_buf, size_t temp_buf_size)
{
    unsigned char in_buffer[1];
    unsigned char out_buffer[16];
    unsigned short temp;
    int whole;
    int tenths;
    int stat = 0;

    if (temp_buf == NULL || temp_buf_size == 0)
        return 0;

    temp_buf[0] = '\0';
    memset(out_buffer, 0, sizeof(out_buffer));

    in_buffer[0] = 0xEF;
    if (sceCdApplySCmd(0x03, in_buffer, sizeof(in_buffer), out_buffer) != 0)
        stat = out_buffer[0];

    if (stat != 0)
        return 0;

    temp = (unsigned short)(out_buffer[1] * 256 + out_buffer[2]);
    whole = (temp - (temp % 128)) / 128;
    tenths = (int)(((temp % 128) * 10 + 64) / 128);
    if (tenths >= 10) {
        whole++;
        tenths -= 10;
    }
    snprintf(temp_buf,
             temp_buf_size,
             "%02d.%dC",
             whole,
             tenths);
    return 1;
}
#endif

// --------------- glob stuff --------------- //
typedef struct
{
    char *KEYPATHS[KEY_COUNT][CONFIG_KEY_INDEXES];
    char *KEYARGS[KEY_COUNT][CONFIG_KEY_INDEXES][MAX_ARGS_PER_ENTRY];
    int KEYARGC[KEY_COUNT][CONFIG_KEY_INDEXES];
    const char *KEYNAMES[KEY_COUNT];
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
    int VIDEO_MODE;
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

static int get_disc_config_hint(void)
{
#ifdef HDD
    if (config_source == SOURCE_HDD)
        return PS2_DISC_HINT_HDD;
#endif
#ifdef MMCE
    if (config_source == SOURCE_MMCE1)
        return PS2_DISC_HINT_MC1;
#endif
    if (config_source == SOURCE_MC1)
        return PS2_DISC_HINT_MC1;
    return PS2_DISC_HINT_MC0;
}

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

static int IsCdvdCommandToken(const char *path)
{
    if (path == NULL)
        return 0;
    return (!strcmp(path, "$CDVD") || !strcmp(path, "$CDVD_NO_PS2LOGO"));
}

static void SplashDrawStatusBelowLogo(const char *text, u32 color)
{
    int line_width;
    int x;
    int y;
    int screen_w;
    int screen_h;
    int anchor_center_x;
    int logo_x;
    int logo_y;
    int logo_w;
    int logo_h;

    if (text == NULL || !SplashRenderIsActive())
        return;

    screen_w = SplashRenderGetScreenWidth();
    screen_h = SplashRenderGetScreenHeight();
    logo_x = SplashRenderGetLogoX();
    logo_y = SplashRenderGetLogoY();
    logo_w = SplashRenderGetLogoWidth();
    logo_h = SplashRenderGetLogoHeight();
    anchor_center_x = logo_x + (logo_w / 2);
    y = logo_y + logo_h + 6;

    if (y > screen_h - 20)
        y = screen_h - 20;
    if (y < 0)
        y = 0;

    line_width = (int)strlen(text) * 6;
    x = anchor_center_x - (line_width / 2);
    if (x < 8)
        x = 8;
    if (x + line_width > screen_w - 8)
        x = screen_w - line_width - 8;
    if (x < 8)
        x = 8;

    SplashRenderSetHotkeysVisible(0);
    SplashRenderBeginFrame();
    SplashRenderDrawTextPxScaled(x, y, color, text, 1);
    SplashRenderPresent();
}

static void SplashDrawStatusForLaunch(int logo_disp, const char *text, u32 color)
{
    if (text == NULL || !SplashRenderIsActive())
        return;

    if (logo_disp >= 2)
        SplashDrawStatusBelowLogo(text, color);
    else
        SplashDrawCenteredStatusWithInfo(text, color, "", "", "", "", NULL, "");
}

static void SplashDrawLoadingStatus(int logo_disp)
{
    const char *scan_hotkey_lines[KEY_COUNT] = {
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
    char loading_line[16];
    int dots;
    int loading_max_w;
    int i;
    int loading_x;
    int loading_y;
    int screen_w;
    int screen_h;
    int anchor_center_x;
    int logo_x;
    int logo_y;
    int logo_w;
    int logo_h;
    u64 now_ms;
    static int s_loading_phase = 0;
    static u64 s_loading_phase_tick_ms = 0;

    if (!SplashRenderIsActive())
        return;

    now_ms = Timer();
    if (s_loading_phase_tick_ms == 0)
        s_loading_phase_tick_ms = now_ms;
    if (now_ms >= s_loading_phase_tick_ms + 500u) {
        s_loading_phase = (s_loading_phase + 1) % 3;
        s_loading_phase_tick_ms = now_ms;
    }
    dots = s_loading_phase + 1;
    strcpy(loading_line, "Loading");
    for (i = 0; i < dots; i++)
        loading_line[7 + i] = '.';
    loading_line[7 + dots] = '\0';

    screen_w = SplashRenderGetScreenWidth();
    screen_h = SplashRenderGetScreenHeight();
    anchor_center_x = SplashRenderGetScreenCenterX();
    logo_x = SplashRenderGetLogoX();
    logo_y = SplashRenderGetLogoY();
    logo_w = SplashRenderGetLogoWidth();
    logo_h = SplashRenderGetLogoHeight();

    if (logo_x >= 0 && logo_y >= 0 && logo_w > 0 && logo_h > 0) {
        anchor_center_x = logo_x + (logo_w / 2);
        loading_y = logo_y + logo_h + 6;
    } else {
        loading_y = SplashRenderGetScreenCenterY() + 10;
    }

    if (loading_y > screen_h - 20)
        loading_y = screen_h - 20;
    if (loading_y < 0)
        loading_y = 0;

    // Keep the loading label anchored so it does not shift as dots change.
    loading_max_w = (int)strlen("Loading...") * 6;
    loading_x = anchor_center_x - (loading_max_w / 2);
    if (loading_x < 8)
        loading_x = 8;
    if (loading_x + loading_max_w > screen_w - 8)
        loading_x = screen_w - loading_max_w - 8;

    SplashRenderSetHotkeysVisible(logo_disp >= 3);
    SplashRenderBeginFrame();
    if (logo_disp >= 3)
        SplashRenderHotkeyLines(logo_disp, scan_hotkey_lines);
    SplashRenderDrawTextPxScaled(loading_x, loading_y, 0x404040, loading_line, 1);
    SplashRenderPresent();
}

static void SplashDrawCenteredStatusWithInfo(const char *text,
                                             u32 color,
                                             const char *model,
                                             const char *rom_fmt,
                                             const char *dvdver,
                                             const char *ps1ver,
                                             const char *temp_celsius,
                                             const char *source)
{
    int line_width;
    int x;
    int y;
    int screen_w;
    int screen_h;
    int anchor_center_x;
    int logo_x;
    int logo_y;
    int logo_w;
    int logo_h;

    if (text == NULL || !SplashRenderIsActive())
        return;

    SplashRenderSetHotkeysVisible(0);
    SplashRenderBeginFrame();
    SplashRenderConsoleInfoLine(GLOBCFG.LOGO_DISP,
                                model,
                                rom_fmt,
                                dvdver,
                                ps1ver,
                                temp_celsius,
                                "",
                                source);
    screen_w = SplashRenderGetScreenWidth();
    screen_h = SplashRenderGetScreenHeight();
    line_width = (int)strlen(text) * 6;
    if (GLOBCFG.LOGO_DISP >= 2) {
        logo_x = SplashRenderGetLogoX();
        logo_y = SplashRenderGetLogoY();
        logo_w = SplashRenderGetLogoWidth();
        logo_h = SplashRenderGetLogoHeight();
        anchor_center_x = logo_x + (logo_w / 2);
        y = logo_y + logo_h + 6;
        if (y > screen_h - 20)
            y = screen_h - 20;
        if (y < 0)
            y = 0;
        x = anchor_center_x - (line_width / 2);
        if (x < 8)
            x = 8;
        if (x + line_width > screen_w - 8)
            x = screen_w - line_width - 8;
    } else {
        x = SplashRenderGetScreenCenterX() - (line_width / 2);
        y = SplashRenderGetScreenCenterY() - 4;
    }
    if (x < 8)
        x = 8;
    SplashRenderDrawTextPxScaled(x, y, color, text, 1);
    SplashRenderPresent();
}

static void SplashDrawRetryPromptWithInfo(const char *line1,
                                          u32 line1_color,
                                          int dots,
                                          const char *model,
                                          const char *rom_fmt,
                                          const char *dvdver,
                                          const char *ps1ver,
                                          const char *temp_celsius,
                                          const char *source)
{
    const char *line2_prefix = "INSERT DISC OR PRESS ";
    const char *line2_word = "START";
    const char *line2_suffix = " TO RETRY HOTKEYS";
    char dots_buf[4];
    int i, dot_count;
    int line1_w;
    int line2_prefix_w;
    int line2_word_w;
    int line2_suffix_w;
    int line2_w;
    int x1;
    int x2;
    int x2_word;
    int x2_suffix;
    int dot_x;
    int y1;
    int y2;
    int screen_w;
    int screen_h;
    int anchor_center_x;
    int logo_x;
    int logo_y;
    int logo_w;
    int logo_h;

    if (!SplashRenderIsActive())
        return;
    if (line1 == NULL)
        return;

    if (dots < 0)
        dots = 0;
    if (dots > 3)
        dots = 3;

    dot_count = dots;
    if (dot_count < 0)
        dot_count = 0;
    if (dot_count > 3)
        dot_count = 3;
    for (i = 0; i < dot_count; i++)
        dots_buf[i] = '.';
    dots_buf[dot_count] = '\0';

    SplashRenderSetHotkeysVisible(0);
    SplashRenderBeginFrame();
    SplashRenderConsoleInfoLine(GLOBCFG.LOGO_DISP,
                                model,
                                rom_fmt,
                                dvdver,
                                ps1ver,
                                temp_celsius,
                                "",
                                source);

    screen_w = SplashRenderGetScreenWidth();
    screen_h = SplashRenderGetScreenHeight();
    line1_w = (int)strlen(line1) * 6;
    line2_prefix_w = (int)strlen(line2_prefix) * 6;
    line2_word_w = (int)strlen(line2_word) * 6;
    line2_suffix_w = (int)strlen(line2_suffix) * 6;
    line2_w = line2_prefix_w + line2_word_w + line2_suffix_w;
    if (GLOBCFG.LOGO_DISP >= 2) {
        logo_x = SplashRenderGetLogoX();
        logo_y = SplashRenderGetLogoY();
        logo_w = SplashRenderGetLogoWidth();
        logo_h = SplashRenderGetLogoHeight();
        anchor_center_x = logo_x + (logo_w / 2);
        y1 = logo_y + logo_h + 2;
        if (y1 > screen_h - 28)
            y1 = screen_h - 28;
        if (y1 < 0)
            y1 = 0;
        y2 = y1 + 18;
        x1 = anchor_center_x - (line1_w / 2);
        x2 = anchor_center_x - (line2_w / 2);
        if (x1 + line1_w > screen_w - 8)
            x1 = screen_w - line1_w - 8;
        if (x2 + line2_w > screen_w - 8)
            x2 = screen_w - line2_w - 8;
    } else {
        x1 = SplashRenderGetScreenCenterX() - (line1_w / 2);
        x2 = SplashRenderGetScreenCenterX() - (line2_w / 2);
        y1 = SplashRenderGetScreenCenterY() - 16;
        y2 = SplashRenderGetScreenCenterY() + 2;
    }
    if (x1 < 8)
        x1 = 8;
    if (x2 < 8)
        x2 = 8;
    x2_word = x2 + line2_prefix_w;
    x2_suffix = x2_word + line2_word_w;
    dot_x = x2 + line2_w;

    SplashRenderDrawTextPxScaled(x1, y1, line1_color, line1, 1);
    SplashRenderDrawTextPxScaled(x2, y2, 0xffffff, line2_prefix, 1);
    SplashRenderDrawTextPxScaled(x2_word, y2, VIDEO_SELECTOR_HIGHLIGHT_COLOR, line2_word, 1);
    SplashRenderDrawTextPxScaled(x2_suffix, y2, 0xffffff, line2_suffix, 1);
    if (dots_buf[0] != '\0')
        SplashRenderDrawTextPxScaled(dot_x, y2, 0xffffff, dots_buf, 1);
    SplashRenderPresent();
}

static void SplashDrawMissingPathPromptWithInfo(const char *button_name,
                                                const char *model,
                                                const char *rom_fmt,
                                                const char *dvdver,
                                                const char *ps1ver,
                                                const char *temp_celsius,
                                                const char *source)
{
    char line1[96];
    const char *safe_button = (button_name != NULL && *button_name != '\0') ? button_name : "BUTTON";
    const char *line2_prefix = "PRESS ";
    const char *line2_word = "START";
    const char *line2_suffix = " TO RETRY LAUNCH KEYS";
    const char *line3_prefix = "PRESS ";
    const char *line3_word1 = "R1";
    const char *line3_plus = "+";
    const char *line3_word2 = "START";
    const char *line3_suffix = " FOR RESCUE MODE";
    int line1_w;
    int line2_prefix_w;
    int line2_word_w;
    int line2_suffix_w;
    int line2_w;
    int line3_prefix_w;
    int line3_word1_w;
    int line3_plus_w;
    int line3_word2_w;
    int line3_suffix_w;
    int line3_w;
    int x1;
    int x2;
    int x2_word;
    int x2_suffix;
    int x3;
    int x3_word1;
    int x3_plus;
    int x3_word2;
    int x3_suffix;
    int y1;
    int y2;
    int y3;
    int screen_w;
    int screen_h;
    int anchor_center_x;
    int logo_x;
    int logo_y;
    int logo_w;
    int logo_h;

    if (!SplashRenderIsActive())
        return;

    snprintf(line1, sizeof(line1), "NO VALID PATH FOUND FOR %s", safe_button);

    SplashRenderSetHotkeysVisible(0);
    SplashRenderBeginFrame();
    SplashRenderConsoleInfoLine(GLOBCFG.LOGO_DISP,
                                model,
                                rom_fmt,
                                dvdver,
                                ps1ver,
                                temp_celsius,
                                "",
                                source);

    screen_w = SplashRenderGetScreenWidth();
    screen_h = SplashRenderGetScreenHeight();
    line1_w = (int)strlen(line1) * 6;
    line2_prefix_w = (int)strlen(line2_prefix) * 6;
    line2_word_w = (int)strlen(line2_word) * 6;
    line2_suffix_w = (int)strlen(line2_suffix) * 6;
    line2_w = line2_prefix_w + line2_word_w + line2_suffix_w;
    line3_prefix_w = (int)strlen(line3_prefix) * 6;
    line3_word1_w = (int)strlen(line3_word1) * 6;
    line3_plus_w = (int)strlen(line3_plus) * 6;
    line3_word2_w = (int)strlen(line3_word2) * 6;
    line3_suffix_w = (int)strlen(line3_suffix) * 6;
    line3_w = line3_prefix_w + line3_word1_w + line3_plus_w + line3_word2_w + line3_suffix_w;

    if (GLOBCFG.LOGO_DISP >= 2) {
        logo_x = SplashRenderGetLogoX();
        logo_y = SplashRenderGetLogoY();
        logo_w = SplashRenderGetLogoWidth();
        logo_h = SplashRenderGetLogoHeight();
        anchor_center_x = logo_x + (logo_w / 2);
        y1 = logo_y + logo_h + 2;
        if (y1 > screen_h - 46)
            y1 = screen_h - 46;
        if (y1 < 0)
            y1 = 0;
    } else {
        anchor_center_x = SplashRenderGetScreenCenterX();
        y1 = SplashRenderGetScreenCenterY() - 20;
    }

    y2 = y1 + 18;
    y3 = y2 + 18;
    x1 = anchor_center_x - (line1_w / 2);
    x2 = anchor_center_x - (line2_w / 2);
    x3 = anchor_center_x - (line3_w / 2);

    if (x1 < 8)
        x1 = 8;
    if (x2 < 8)
        x2 = 8;
    if (x3 < 8)
        x3 = 8;
    if (x1 + line1_w > screen_w - 8)
        x1 = screen_w - line1_w - 8;
    if (x2 + line2_w > screen_w - 8)
        x2 = screen_w - line2_w - 8;
    if (x3 + line3_w > screen_w - 8)
        x3 = screen_w - line3_w - 8;
    if (x1 < 8)
        x1 = 8;
    if (x2 < 8)
        x2 = 8;
    if (x3 < 8)
        x3 = 8;

    x2_word = x2 + line2_prefix_w;
    x2_suffix = x2_word + line2_word_w;
    x3_word1 = x3 + line3_prefix_w;
    x3_plus = x3_word1 + line3_word1_w;
    x3_word2 = x3_plus + line3_plus_w;
    x3_suffix = x3_word2 + line3_word2_w;

    SplashRenderDrawTextPxScaled(x1, y1, 0xffff00, line1, 1);
    SplashRenderDrawTextPxScaled(x2, y2, 0xffffff, line2_prefix, 1);
    SplashRenderDrawTextPxScaled(x2_word, y2, VIDEO_SELECTOR_HIGHLIGHT_COLOR, line2_word, 1);
    SplashRenderDrawTextPxScaled(x2_suffix, y2, 0xffffff, line2_suffix, 1);
    SplashRenderDrawTextPxScaled(x3, y3, 0xffffff, line3_prefix, 1);
    SplashRenderDrawTextPxScaled(x3_word1, y3, VIDEO_SELECTOR_HIGHLIGHT_COLOR, line3_word1, 1);
    SplashRenderDrawTextPxScaled(x3_plus, y3, 0xffffff, line3_plus, 1);
    SplashRenderDrawTextPxScaled(x3_word2, y3, VIDEO_SELECTOR_HIGHLIGHT_COLOR, line3_word2, 1);
    SplashRenderDrawTextPxScaled(x3_suffix, y3, 0xffffff, line3_suffix, 1);
    SplashRenderPresent();
}

static void DrawConsoleMissingPathPrompt(const char *button_name)
{
    const char *safe_button = (button_name != NULL && *button_name != '\0') ? button_name : "BUTTON";

    scr_clear();
    scr_setfontcolor(0xffff00);
    scr_printf("\n\n\t\tNO VALID PATH FOUND FOR %s\n\n", safe_button);
    scr_setfontcolor(0xffffff);
    scr_printf("\t\tPRESS ");
    scr_setfontcolor(VIDEO_SELECTOR_HIGHLIGHT_COLOR);
    scr_printf("START");
    scr_setfontcolor(0xffffff);
    scr_printf(" TO RETRY LAUNCH KEYS\n");
    scr_printf("\t\tPRESS ");
    scr_setfontcolor(VIDEO_SELECTOR_HIGHLIGHT_COLOR);
    scr_printf("R1");
    scr_setfontcolor(0xffffff);
    scr_printf("+");
    scr_setfontcolor(VIDEO_SELECTOR_HIGHLIGHT_COLOR);
    scr_printf("START");
    scr_setfontcolor(0xffffff);
    scr_printf(" FOR RESCUE MODE\n");
}

static int WaitForMissingPathAction(const char *button_name,
                                    const char *model,
                                    const char *rom_fmt,
                                    const char *dvdver,
                                    const char *ps1ver,
                                    const char *temp_celsius,
                                    const char *source)
{
    int prev_pad = 0;
    u64 start_retry_deadline = 0;

    if (SplashRenderIsActive())
        SplashDrawMissingPathPromptWithInfo(button_name, model, rom_fmt, dvdver, ps1ver, temp_celsius, source);
    else
        DrawConsoleMissingPathPrompt(button_name);

    while (ReadCombinedPadStatus_raw() & PAD_MASK_ANY) {
    }

    while (1) {
        int pad = ReadCombinedPadStatus_raw();

        if ((pad & PAD_R1) && (pad & PAD_START))
            EMERGENCY();

        if (!(prev_pad & PAD_START) && (pad & PAD_START) && start_retry_deadline == 0)
            start_retry_deadline = Timer() + MISSING_PATH_RESCUE_CHORD_WINDOW_MS;

        if (start_retry_deadline != 0 && Timer() >= start_retry_deadline) {
            while (ReadCombinedPadStatus_raw() & PAD_MASK_ANY) {
            }
            return 1;
        }

        prev_pad = pad;
    }
}

static void SplashDrawEmergencyModeStatus(const char *reason, int dots)
{
    const char *line1 = "USB EMERGENCY MODE!";
    const char *line2 = (reason != NULL && *reason != '\0') ? reason : NULL;
    const char *line3 = "Searching for mass:/RESCUE.ELF";
    char dots_buf[4];
    int line1_w;
    int line2_w;
    int line3_w;
    int x1;
    int x2 = 0;
    int x3;
    int dot_x;
    int y1;
    int y2 = 0;
    int y3;
    int screen_w;
    int screen_h;
    int anchor_center_x;
    int logo_x;
    int logo_y;
    int logo_w;
    int logo_h;
    int i;

    if (!SplashRenderIsActive())
        return;

    if (dots < 0)
        dots = 0;
    if (dots > 3)
        dots = 3;
    for (i = 0; i < dots; i++)
        dots_buf[i] = '.';
    dots_buf[dots] = '\0';

    SplashRenderSetHotkeysVisible(0);
    SplashRenderBeginFrame();

    screen_w = SplashRenderGetScreenWidth();
    screen_h = SplashRenderGetScreenHeight();
    line1_w = (int)strlen(line1) * 6;
    line2_w = (line2 != NULL) ? (int)strlen(line2) * 6 : 0;
    line3_w = (int)strlen(line3) * 6;

    if (GLOBCFG.LOGO_DISP >= 2) {
        logo_x = SplashRenderGetLogoX();
        logo_y = SplashRenderGetLogoY();
        logo_w = SplashRenderGetLogoWidth();
        logo_h = SplashRenderGetLogoHeight();
        anchor_center_x = logo_x + (logo_w / 2);
        y1 = logo_y + logo_h + 2;
        if (line2 != NULL) {
            if (y1 > screen_h - 46)
                y1 = screen_h - 46;
        } else if (y1 > screen_h - 28)
            y1 = screen_h - 28;
        if (y1 < 0)
            y1 = 0;
    } else {
        anchor_center_x = SplashRenderGetScreenCenterX();
        y1 = (line2 != NULL) ? (SplashRenderGetScreenCenterY() - 20) : (SplashRenderGetScreenCenterY() - 10);
    }

    if (line2 != NULL) {
        y2 = y1 + 18;
        y3 = y2 + 18;
    } else
        y3 = y1 + 18;

    x1 = anchor_center_x - (line1_w / 2);
    if (line2 != NULL)
        x2 = anchor_center_x - (line2_w / 2);
    x3 = anchor_center_x - (line3_w / 2);
    if (x1 < 8)
        x1 = 8;
    if (line2 != NULL && x2 < 8)
        x2 = 8;
    if (x3 < 8)
        x3 = 8;
    if (x1 + line1_w > screen_w - 8)
        x1 = screen_w - line1_w - 8;
    if (line2 != NULL && x2 + line2_w > screen_w - 8)
        x2 = screen_w - line2_w - 8;
    if (x3 + line3_w > screen_w - 8)
        x3 = screen_w - line3_w - 8;
    if (x1 < 8)
        x1 = 8;
    if (line2 != NULL && x2 < 8)
        x2 = 8;
    if (x3 < 8)
        x3 = 8;
    dot_x = x3 + line3_w;

    SplashRenderDrawTextPxScaled(x1, y1, 0x0000ff, line1, 1);
    if (line2 != NULL)
        SplashRenderDrawTextPxScaled(x2, y2, 0xffff00, line2, 1);
    SplashRenderDrawTextPxScaled(x3, y3, 0x00ffff, line3, 1);
    if (dots_buf[0] != '\0')
        SplashRenderDrawTextPxScaled(dot_x, y3, 0xffffff, dots_buf, 1);
    SplashRenderPresent();
}

static void RestoreSplashInteractiveUi(int logo_disp,
                                       const char *const hotkey_lines[KEY_COUNT],
                                       const char *model,
                                       const char *rom_fmt,
                                       const char *dvdver,
                                       const char *ps1ver,
                                       const char *temp_celsius,
                                       const char *source)
{
    int pass;

    if (!SplashRenderIsActive())
        return;

    SplashRenderSetHotkeysVisible(logo_disp >= 3);
    for (pass = 0; pass < 2; pass++) {
        SplashRenderBeginFrame();
        SplashRenderHotkeyLines(logo_disp, hotkey_lines);
        SplashRenderConsoleInfoLine(logo_disp,
                                    model,
                                    rom_fmt,
                                    dvdver,
                                    ps1ver,
                                    temp_celsius,
                                    "",
                                    source);
        SplashRenderHotkeyClockDate(logo_disp, 0);
        SplashRenderPresent();
    }
}

static void ShowLaunchStatus(const char *path)
{
    char loading_line[160];
    const char *safe_path = (path != NULL) ? path : "";
    int is_cdvd = IsCdvdCommandToken(safe_path);

    if (!is_cdvd) {
        scr_setfontcolor(0x00ff00);
        scr_printf("  Loading %s\n", safe_path);
        return;
    }

    if (!SplashRenderIsActive()) {
        int launch_status_logo_disp = (GLOBCFG.LOGO_DISP >= 1) ? GLOBCFG.LOGO_DISP : 1;

        SplashRenderSetVideoMode(GLOBCFG.VIDEO_MODE, g_native_video_mode);
        SplashRenderTextBody(launch_status_logo_disp, g_is_psx_desr);
    }
    if (!SplashRenderIsActive())
        return;

    snprintf(loading_line, sizeof(loading_line), "Loading %s", safe_path);
    SplashDrawStatusForLaunch(GLOBCFG.LOGO_DISP, loading_line, 0x00ff00);
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

#ifdef HDD
static int path_has_patinfo_token(const char *path)
{
    static const char token[] = ":PATINFO";
    size_t token_len = sizeof(token) - 1;
    const char *p;

    if (path == NULL)
        return 0;

    for (p = path; *p != '\0'; p++) {
        size_t k;
        for (k = 0; k < token_len && p[k] != '\0'; k++) {
            unsigned char a = (unsigned char)p[k];
            unsigned char b = (unsigned char)token[k];

            if (a >= 'a' && a <= 'z')
                a -= ('a' - 'A');
            if (b >= 'a' && b <= 'z')
                b -= ('a' - 'A');
            if (a != b)
                break;
        }

        if (k == token_len)
            return 1;
    }

    return 0;
}

static int entry_has_arg_ci(int key_idx, int entry_idx, const char *arg)
{
    int i;

    if (arg == NULL)
        return 0;
    if (key_idx < 0 || key_idx >= KEY_COUNT)
        return 0;
    if (entry_idx < 0 || entry_idx >= CONFIG_KEY_INDEXES)
        return 0;

    for (i = 0; i < GLOBCFG.KEYARGC[key_idx][entry_idx] && i < MAX_ARGS_PER_ENTRY; i++) {
        const char *entry_arg = GLOBCFG.KEYARGS[key_idx][entry_idx][i];
        if (entry_arg == NULL)
            continue;

        while (*entry_arg == ' ' || *entry_arg == '\t')
            entry_arg++;

        if (ci_eq(entry_arg, arg))
            return 1;
    }

    return 0;
}

static int allow_virtual_patinfo_entry(int key_idx, int entry_idx, const char *path)
{
    return path_has_patinfo_token(path) && entry_has_arg_ci(key_idx, entry_idx, "-patinfo");
}
#else
static int allow_virtual_patinfo_entry(int key_idx, int entry_idx, const char *path)
{
    (void)key_idx;
    (void)entry_idx;
    (void)path;
    return 0;
}
#endif

static void set_pending_command_args(int argc, char *argv[])
{
    if (argc > 0 && argv != NULL) {
        g_pending_command_argc = argc;
        g_pending_command_argv = argv;
    } else {
        g_pending_command_argc = 0;
        g_pending_command_argv = NULL;
    }
}

static void parse_disc_egsm_override(int argc, char *argv[], uint32_t *flags_out, const char **arg_out)
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

        if (argv[i] == NULL || !ci_starts_with(argv[i], "-gsm="))
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
    static char name_buf[KEY_COUNT][MAX_LEN];
    int dev_ok[DEV_COUNT];
    const char *first_valid[KEY_COUNT];
    int logo_disp = GLOBCFG.LOGO_DISP;
    u64 next_loading_refresh_ms = 0;
    int i, j;

    for (i = 0; i < KEY_COUNT; i++)
        first_valid[i] = NULL;

    if (scan_paths) {
        build_device_available_cache(dev_ok, DEV_COUNT);
        if (logo_disp > 0)
            next_loading_refresh_ms = Timer() + 500u;
        for (i = 0; i < KEY_COUNT; i++) {
            int found = 0;

            for (j = 0; j < CONFIG_KEY_INDEXES; j++) {
                char *path = GLOBCFG.KEYPATHS[i][j];

                if (logo_disp > 0) {
                    u64 now_ms = Timer();
                    if (now_ms >= next_loading_refresh_ms) {
                        SplashDrawLoadingStatus(logo_disp);
                        next_loading_refresh_ms = now_ms + 500u;
                    }
                }

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
                if (allow_virtual_patinfo_entry(i, j, path) || exist(path)) {
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
        for (i = 0; i < KEY_COUNT; i++)
            GLOBCFG.KEYNAMES[i] = "";
        return;
    }
    if (display_mode == 1)
        return; // keep user-defined names

    for (i = 0; i < KEY_COUNT; i++) {
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
static int g_cdvd_cancelled = 0;
static int g_pending_command_argc = 0;
static char **g_pending_command_argv = NULL;
#define ROMVER_MODEL_PREFIX_LEN 5
static const char *const g_psx_desr_rom_prefixes[] = {
    "0180J",
    "0210J",
};
int PAD = 0;
unsigned char *config_buf = NULL; // pointer to allocated config file
static int g_video_mode_selector_requested = 0;
static int g_block_hotkeys_until_release = 0;
static int g_hotkey_launches_enabled = 1;
static char g_config_path_in_use[256] = "";

#define RESCUE_COMBO_WINDOW_MS 2000
#define VIDEO_SELECTOR_BOX_BG_COLOR 0x606060
#define VIDEO_SELECTOR_BOX_BG_OPACITY_PERCENT 80
#define VIDEO_SELECTOR_BOX_TEXT_PAD_X 10
#define VIDEO_SELECTOR_BOX_TEXT_PAD_Y 20
#define VIDEO_SELECTOR_LINE_SPACING 22
#define VIDEO_SELECTOR_TEXT_HEIGHT 7
#define VIDEO_SELECTOR_BOX_GAP_FROM_LOGO 14
#define VIDEO_SELECTOR_BOX_SCREEN_MARGIN 8
#define VIDEO_SELECTOR_SAVE_FEEDBACK_MS 3000

static int is_space_or_tab(char c)
{
    return (c == ' ' || c == '\t');
}

static int ci_starts_with_n(const char *s, size_t s_len, const char *prefix)
{
    size_t i;

    if (s == NULL || prefix == NULL)
        return 0;

    for (i = 0; prefix[i] != '\0'; i++) {
        unsigned char cs;
        unsigned char cp;

        if (i >= s_len)
            return 0;

        cs = (unsigned char)s[i];
        cp = (unsigned char)prefix[i];
        if (cs >= 'a' && cs <= 'z')
            cs -= ('a' - 'A');
        if (cp >= 'a' && cp <= 'z')
            cp -= ('a' - 'A');
        if (cs != cp)
            return 0;
    }

    return 1;
}

static int classify_video_mode_config_line(const char *line, size_t len)
{
    size_t i = 0;

    while (i < len && is_space_or_tab(line[i]))
        i++;
    if (i >= len)
        return 0;

    if (line[i] == '#' || line[i] == ';')
        return 0;

    if (!ci_starts_with_n(line + i, len - i, "VIDEO_MODE"))
        return 0;
    i += strlen("VIDEO_MODE");
    while (i < len && is_space_or_tab(line[i]))
        i++;
    if (i < len && line[i] == '=')
        return 1;

    return 0;
}

static int SaveVideoModeToConfigFile(int cfg_mode, char *saved_path_out, size_t saved_path_out_size)
{
    FILE *fp = NULL;
    char *in_buf = NULL;
    char *out_buf = NULL;
    char mode_line[32];
    char *resolved_path;
    const char *path = g_config_path_in_use;
    size_t in_size;
    size_t out_cap;
    size_t out_size;
    long file_size;
    int use_crlf = 0;
    int replaced_video_mode_line = 0;
    int success = 0;
    const char *cursor;
    const char *end;
    char *out;

    if (saved_path_out != NULL && saved_path_out_size > 0)
        saved_path_out[0] = '\0';

    if (path[0] == '\0' && !(config_source >= SOURCE_MC0 && config_source < SOURCE_COUNT))
        return 0;

    snprintf(mode_line, sizeof(mode_line), "VIDEO_MODE = %s", video_mode_label(cfg_mode));

    fp = fopen(path, "rb");
    if (fp == NULL && config_source >= SOURCE_MC0 && config_source < SOURCE_COUNT) {
        resolved_path = CheckPath(CONFIG_PATHS[config_source]);
        if (resolved_path != NULL && *resolved_path != '\0') {
            snprintf(g_config_path_in_use, sizeof(g_config_path_in_use), "%s", resolved_path);
            path = g_config_path_in_use;
            fp = fopen(path, "rb");
        }
    }
    if (fp == NULL)
        goto cleanup;

    if (fseek(fp, 0, SEEK_END) != 0)
        goto cleanup;
    file_size = ftell(fp);
    if (file_size < 0)
        goto cleanup;
    if (fseek(fp, 0, SEEK_SET) != 0)
        goto cleanup;

    in_size = (size_t)file_size;
    in_buf = (char *)malloc(in_size + 1);
    if (in_buf == NULL)
        goto cleanup;
    if (in_size > 0 && fread(in_buf, 1, in_size, fp) != in_size)
        goto cleanup;
    fclose(fp);
    fp = NULL;
    in_buf[in_size] = '\0';

    {
        size_t i;
        for (i = 0; i + 1 < in_size; i++) {
            if (in_buf[i] == '\r' && in_buf[i + 1] == '\n') {
                use_crlf = 1;
                break;
            }
        }
    }

    out_cap = (in_size * 2) + 128;
    out_buf = (char *)malloc(out_cap);
    if (out_buf == NULL)
        goto cleanup;

    out = out_buf;
    cursor = in_buf;
    end = in_buf + in_size;
    while (cursor < end) {
        const char *line_end = cursor;
        const char *newline_ptr = NULL;
        size_t line_content_len;
        size_t newline_len = 0;
        int kind;

        while (line_end < end && *line_end != '\n')
            line_end++;

        if (line_end < end) {
            if (line_end > cursor && line_end[-1] == '\r') {
                line_content_len = (size_t)((line_end - 1) - cursor);
                newline_ptr = line_end - 1;
                newline_len = 2;
            } else {
                line_content_len = (size_t)(line_end - cursor);
                newline_ptr = line_end;
                newline_len = 1;
            }
        } else {
            line_content_len = (size_t)(end - cursor);
        }

        kind = classify_video_mode_config_line(cursor, line_content_len);
        if (kind == 1) {
            size_t mode_line_len = strlen(mode_line);

            memcpy(out, mode_line, mode_line_len);
            out += mode_line_len;

            if (newline_len > 0) {
                memcpy(out, newline_ptr, newline_len);
                out += newline_len;
            }
            replaced_video_mode_line = 1;
        } else {
            size_t original_len = line_content_len + newline_len;
            memcpy(out, cursor, original_len);
            out += original_len;
        }

        cursor = (line_end < end) ? (line_end + 1) : end;
    }

    if (!replaced_video_mode_line) {
        size_t mode_line_len = strlen(mode_line);
        size_t newline_len = use_crlf ? 2 : 1;
        size_t prefix_len = mode_line_len + newline_len;
        size_t existing_len = (size_t)(out - out_buf);

        if (existing_len + prefix_len > out_cap)
            goto cleanup;

        memmove(out_buf + prefix_len, out_buf, existing_len);
        memcpy(out_buf, mode_line, mode_line_len);
        if (use_crlf) {
            out_buf[mode_line_len] = '\r';
            out_buf[mode_line_len + 1] = '\n';
        } else {
            out_buf[mode_line_len] = '\n';
        }
        out = out_buf + prefix_len + existing_len;
    }

    out_size = (size_t)(out - out_buf);
    fp = fopen(path, "wb");
    if (fp == NULL)
        goto cleanup;
    if (out_size > 0 && fwrite(out_buf, 1, out_size, fp) != out_size)
        goto cleanup;

    success = 1;
    if (saved_path_out != NULL && saved_path_out_size > 0)
        snprintf(saved_path_out, saved_path_out_size, "%s", path);

cleanup:
    if (!success && saved_path_out != NULL && saved_path_out_size > 0 && path != NULL && *path != '\0')
        snprintf(saved_path_out, saved_path_out_size, "%s", path);
    if (fp != NULL)
        fclose(fp);
    if (in_buf != NULL)
        free(in_buf);
    if (out_buf != NULL)
        free(out_buf);
    return success;
}

static void RunEmergencyVideoModeSelector(void)
{
    int selected_mode = GLOBCFG.VIDEO_MODE;
    int applied_effective_mode;
    int prev_pad;
    char model_buf[64];
    char ps1_buf[64];
    char dvd_buf[64];
    char src_buf[32];
    char rom_raw[ROMVER_MAX_LEN + 1];
    char rom_buf[32];
    char rom_fmt[8];
    const char *model = "";
    const char *ps1ver = "";
    const char *dvdver = "";
    const char *source = "";
    const char *temp_celsius = NULL;
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
#ifndef NO_TEMP_DISP
    char temp_query_buf[16];
    char temp_render_buf[16];
    int temp_supported = 0;
#endif
    u64 save_feedback_until = 0;
    int save_feedback_ok = 0;
    char save_feedback_path[128];

    if (selected_mode < CFG_VIDEO_MODE_AUTO || selected_mode > CFG_VIDEO_MODE_480P)
        selected_mode = CFG_VIDEO_MODE_AUTO;
    applied_effective_mode = resolve_effective_video_mode(selected_mode);
    g_hotkey_launches_enabled = 0;

    GLOBCFG.LOGO_DISP = 5;
    GLOBCFG.HOTKEY_DISPLAY = logo_to_hotkey_display(GLOBCFG.LOGO_DISP);
    g_pre_scanned = (GLOBCFG.HOTKEY_DISPLAY == 2 || GLOBCFG.HOTKEY_DISPLAY == 3);
    ValidateKeypathsAndSetNames(GLOBCFG.HOTKEY_DISPLAY, g_pre_scanned);

    model = strip_crlf_copy(ModelNameGet(), model_buf, sizeof(model_buf));
    ps1ver = strip_crlf_copy(PS1DRVGetVersion(), ps1_buf, sizeof(ps1_buf));
    dvdver = strip_crlf_copy(DVDPlayerGetVersion(), dvd_buf, sizeof(dvd_buf));
    source = strip_crlf_copy(SOURCES[config_source], src_buf, sizeof(src_buf));
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
#ifndef NO_TEMP_DISP
    if (QueryTemperatureCelsius(temp_query_buf, sizeof(temp_query_buf))) {
        strncpy(temp_render_buf, temp_query_buf, sizeof(temp_render_buf));
        temp_render_buf[sizeof(temp_render_buf) - 1] = '\0';
        temp_celsius = temp_render_buf;
        temp_supported = 1;
    }
#endif

    SplashRenderSetVideoMode(selected_mode, g_native_video_mode);
    if (SplashRenderIsActive())
        SplashRenderEnd();
    SplashRenderTextBody(GLOBCFG.LOGO_DISP, g_is_psx_desr);
    save_feedback_path[0] = '\0';

    prev_pad = ReadCombinedPadStatus_raw();

    while (1) {
        u64 now = Timer();
        int pad = ReadCombinedPadStatus_raw();
        int pressed = pad & (~prev_pad);
        int mode_changed = 0;

        if (pressed & PAD_MASK_LEFT) {
            selected_mode = step_video_mode(selected_mode, -1);
            mode_changed = 1;
        }
        if (pressed & PAD_MASK_RIGHT) {
            selected_mode = step_video_mode(selected_mode, +1);
            mode_changed = 1;
        }

        if (mode_changed) {
            int new_effective_mode = resolve_effective_video_mode(selected_mode);

            if (new_effective_mode != applied_effective_mode) {
                apply_loader_video_mode(selected_mode);
                SplashRenderSetVideoMode(selected_mode, g_native_video_mode);
                if (SplashRenderIsActive())
                    SplashRenderEnd();
                SplashRenderTextBody(GLOBCFG.LOGO_DISP, g_is_psx_desr);
                applied_effective_mode = new_effective_mode;
            }
        }

        if (pressed & PAD_MASK_SELECT) {
            save_feedback_ok = SaveVideoModeToConfigFile(selected_mode,
                                                         save_feedback_path,
                                                         sizeof(save_feedback_path));
            save_feedback_until = Timer() + VIDEO_SELECTOR_SAVE_FEEDBACK_MS;
        }

        if (SplashRenderIsActive()) {
            char line_mode[96];
            char line_save_status[96];
            char save_path_display[56];
            const char *line_start_prefix = "PRESS ";
            const char *line_start_word = "START";
            const char *line_start_suffix = " TO RETRY LAUNCH KEYS";
            const char *line_change = "PRESS LEFT/RIGHT TO CHANGE MODES";
            const char *line_select_prefix = "PRESS ";
            const char *line_select_word = "SELECT";
            const char *line_select_suffix = " TO SAVE TO CONFIG";
            const char *status_path = "(unknown)";
            const char *selected_label = video_mode_label(selected_mode);
            const char *native_label = video_mode_label(g_native_video_mode);
            int screen_w = SplashRenderGetScreenWidth();
            int screen_h = SplashRenderGetScreenHeight();
            int logo_x = SplashRenderGetLogoX();
            int logo_y = SplashRenderGetLogoY();
            int logo_w = SplashRenderGetLogoWidth();
            int logo_h = SplashRenderGetLogoHeight();
            int anchor_center_x = screen_w / 2;
            int box_x;
            int box_y;
            int box_w;
            int box_h;
            int text_block_h;
            int text_base_y;
            int line_mode_y;
            int line_change_y;
            int line_start_y;
            int line_select_y;
            int line_save_status_y;
            int line_mode_x;
            int line_change_x;
            int line_start_x;
            int line_select_x;
            int line_save_status_x;
            int line_mode_w;
            int line_change_w;
            int line_start_prefix_w;
            int line_start_word_w;
            int line_start_suffix_w;
            int line_start_w;
            int line_select_prefix_w;
            int line_select_word_w;
            int line_select_suffix_w;
            int line_select_w;
            int line_save_status_w;
            int line_start_prefix_x;
            int line_start_word_x;
            int line_start_suffix_x;
            int line_select_prefix_x;
            int line_select_word_x;
            int line_select_suffix_x;
            int max_line_w;
            int line_count = 4;
            int show_save_status = (save_feedback_until > now);
            const char *render_temp = temp_celsius;
            u32 save_status_color = VIDEO_SELECTOR_TEXT_COLOR;

            if (logo_x >= 0 && logo_y >= 0 && logo_w > 0 && logo_h > 0) {
                anchor_center_x = logo_x + (logo_w / 2);
                box_y = logo_y + logo_h + VIDEO_SELECTOR_BOX_GAP_FROM_LOGO;
            } else
                box_y = ((screen_h * 55) + 50) / 100;

#ifndef NO_TEMP_DISP
            if (temp_supported) {
                if (QueryTemperatureCelsius(temp_query_buf, sizeof(temp_query_buf))) {
                    strncpy(temp_render_buf, temp_query_buf, sizeof(temp_render_buf));
                    temp_render_buf[sizeof(temp_render_buf) - 1] = '\0';
                }
                render_temp = temp_render_buf;
            }
#endif

            snprintf(line_mode, sizeof(line_mode), "VIDEO_MODE = %s [NATIVE %s]", selected_label, native_label);
            if (show_save_status) {
                if (save_feedback_path[0] != '\0') {
                    size_t path_len = strlen(save_feedback_path);
                    if (path_len < sizeof(save_path_display)) {
                        snprintf(save_path_display, sizeof(save_path_display), "%s", save_feedback_path);
                    } else {
                        size_t keep_len = sizeof(save_path_display) - 4;
                        memcpy(save_path_display, save_feedback_path, keep_len);
                        save_path_display[keep_len] = '.';
                        save_path_display[keep_len + 1] = '.';
                        save_path_display[keep_len + 2] = '.';
                        save_path_display[keep_len + 3] = '\0';
                    }
                    status_path = save_path_display;
                }

                snprintf(line_save_status,
                         sizeof(line_save_status),
                         save_feedback_ok ? "SAVED TO %s" : "SAVE FAILED %s",
                         status_path);
                line_count = 5;
                save_status_color = save_feedback_ok ? VIDEO_SELECTOR_HIGHLIGHT_COLOR : 0xff4040;
            }
            line_mode_w = (int)strlen(line_mode) * 6;
            line_change_w = (int)strlen(line_change) * 6;
            line_start_prefix_w = (int)strlen(line_start_prefix) * 6;
            line_start_word_w = (int)strlen(line_start_word) * 6;
            line_start_suffix_w = (int)strlen(line_start_suffix) * 6;
            line_start_w = line_start_prefix_w + line_start_word_w + line_start_suffix_w;
            line_select_prefix_w = (int)strlen(line_select_prefix) * 6;
            line_select_word_w = (int)strlen(line_select_word) * 6;
            line_select_suffix_w = (int)strlen(line_select_suffix) * 6;
            line_select_w = line_select_prefix_w + line_select_word_w + line_select_suffix_w;
            line_save_status_w = show_save_status ? (int)strlen(line_save_status) * 6 : 0;

            text_block_h = ((line_count - 1) * VIDEO_SELECTOR_LINE_SPACING) + VIDEO_SELECTOR_TEXT_HEIGHT;

            max_line_w = line_mode_w;
            if (line_change_w > max_line_w)
                max_line_w = line_change_w;
            if (line_start_w > max_line_w)
                max_line_w = line_start_w;
            if (line_select_w > max_line_w)
                max_line_w = line_select_w;
            if (line_save_status_w > max_line_w)
                max_line_w = line_save_status_w;

            box_w = max_line_w + (VIDEO_SELECTOR_BOX_TEXT_PAD_X * 2);
            box_h = text_block_h + (VIDEO_SELECTOR_BOX_TEXT_PAD_Y * 2);
            box_x = anchor_center_x - (box_w / 2);

            if (box_x < VIDEO_SELECTOR_BOX_SCREEN_MARGIN)
                box_x = VIDEO_SELECTOR_BOX_SCREEN_MARGIN;
            if (box_x + box_w > screen_w - VIDEO_SELECTOR_BOX_SCREEN_MARGIN)
                box_x = screen_w - box_w - VIDEO_SELECTOR_BOX_SCREEN_MARGIN;
            if (box_y < VIDEO_SELECTOR_BOX_SCREEN_MARGIN)
                box_y = VIDEO_SELECTOR_BOX_SCREEN_MARGIN;
            if (box_y + box_h > screen_h - VIDEO_SELECTOR_BOX_SCREEN_MARGIN)
                box_y = screen_h - box_h - VIDEO_SELECTOR_BOX_SCREEN_MARGIN;

            text_base_y = box_y + ((box_h - text_block_h) / 2);
            line_mode_y = text_base_y;
            line_change_y = text_base_y + VIDEO_SELECTOR_LINE_SPACING;
            line_start_y = text_base_y + (VIDEO_SELECTOR_LINE_SPACING * 2);
            line_select_y = text_base_y + (VIDEO_SELECTOR_LINE_SPACING * 3);
            line_save_status_y = text_base_y + (VIDEO_SELECTOR_LINE_SPACING * 4);

            line_mode_x = box_x + (box_w - line_mode_w) / 2;
            line_change_x = box_x + (box_w - line_change_w) / 2;
            line_start_x = box_x + (box_w - line_start_w) / 2;
            line_select_x = box_x + (box_w - line_select_w) / 2;
            line_save_status_x = box_x + (box_w - line_save_status_w) / 2;
            line_start_prefix_x = line_start_x;
            line_start_word_x = line_start_prefix_x + line_start_prefix_w;
            line_start_suffix_x = line_start_word_x + line_start_word_w;
            line_select_prefix_x = line_select_x;
            line_select_word_x = line_select_prefix_x + line_select_prefix_w;
            line_select_suffix_x = line_select_word_x + line_select_word_w;

            SplashRenderSetHotkeysVisible(1);
            SplashRenderBeginFrame();
            SplashRenderHotkeyLines(GLOBCFG.LOGO_DISP, hotkey_lines);
            SplashRenderConsoleInfoLine(GLOBCFG.LOGO_DISP,
                                        model,
                                        rom_fmt,
                                        dvdver,
                                        ps1ver,
                                        render_temp,
                                        "",
                                        source);
            SplashRenderHotkeyClockDate(GLOBCFG.LOGO_DISP, now);
            SplashRenderDrawRoundedRect(box_x,
                                        box_y,
                                        box_w,
                                        box_h,
                                        0,
                                        VIDEO_SELECTOR_BOX_BG_COLOR,
                                        VIDEO_SELECTOR_BOX_BG_OPACITY_PERCENT);
            SplashRenderDrawTextPxScaled(line_mode_x,
                                         line_mode_y,
                                         VIDEO_SELECTOR_HIGHLIGHT_COLOR,
                                         line_mode,
                                         1);
            SplashRenderDrawTextPxScaled(line_change_x,
                                         line_change_y,
                                         VIDEO_SELECTOR_TEXT_COLOR,
                                         line_change,
                                         1);
            SplashRenderDrawTextPxScaled(line_start_prefix_x,
                                         line_start_y,
                                         VIDEO_SELECTOR_TEXT_COLOR,
                                         line_start_prefix,
                                         1);
            SplashRenderDrawTextPxScaled(line_start_word_x,
                                         line_start_y,
                                         VIDEO_SELECTOR_HIGHLIGHT_COLOR,
                                         line_start_word,
                                         1);
            SplashRenderDrawTextPxScaled(line_start_suffix_x,
                                         line_start_y,
                                         VIDEO_SELECTOR_TEXT_COLOR,
                                         line_start_suffix,
                                         1);
            SplashRenderDrawTextPxScaled(line_select_prefix_x,
                                         line_select_y,
                                         VIDEO_SELECTOR_TEXT_COLOR,
                                         line_select_prefix,
                                         1);
            SplashRenderDrawTextPxScaled(line_select_word_x,
                                         line_select_y,
                                         VIDEO_SELECTOR_SELECT_COLOR,
                                         line_select_word,
                                         1);
            SplashRenderDrawTextPxScaled(line_select_suffix_x,
                                         line_select_y,
                                         VIDEO_SELECTOR_TEXT_COLOR,
                                         line_select_suffix,
                                         1);
            if (show_save_status) {
                SplashRenderDrawTextPxScaled(line_save_status_x,
                                             line_save_status_y,
                                             save_status_color,
                                             line_save_status,
                                             1);
            }
            SplashRenderPresent();
        }

        if (pressed & PAD_START) {
            g_hotkey_launches_enabled = 1;
            break;
        }

        prev_pad = pad;
    }

    GLOBCFG.VIDEO_MODE = selected_mode;
    SplashRenderSetVideoMode(GLOBCFG.VIDEO_MODE, g_native_video_mode);
    g_block_hotkeys_until_release = 1;
    if (SplashRenderIsActive())
        SplashRenderEnd();

    while (ReadCombinedPadStatus_raw() & PAD_START) {
    }
}

static void PollEmergencyComboWindow(u64 *window_deadline_ms)
{
    int pad_state;

    if (window_deadline_ms == NULL || *window_deadline_ms == 0)
        return;

    if (Timer() > *window_deadline_ms) {
        *window_deadline_ms = 0;
        return;
    }

    pad_state = ReadCombinedPadStatus_raw();
    if ((pad_state & PAD_R1) && (pad_state & PAD_START))
        EMERGENCY();
    if ((pad_state & PAD_MASK_TRIANGLE) && (pad_state & PAD_MASK_CROSS))
        g_video_mode_selector_requested = 1;
}

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

static void ReadROMVEROnce(void)
{
    int fd;

    memset(ROMVER, 0, sizeof(ROMVER));
    if ((fd = open("rom0:ROMVER", O_RDONLY)) >= 0) {
        read(fd, ROMVER, sizeof(ROMVER));
        close(fd);
    }

    g_is_psx_desr = IsPSXDESRROMVER(ROMVER);
}

static const char *GetRuntimeBanner(void)
{
#if defined(PSX)
    return g_is_psx_desr ? BANNER_PSX : BANNER_PS2;
#else
    return BANNER_PS2;
#endif
}

static void LogDetectedPlatform(void)
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

int main(int argc, char *argv[])
{
    u32 STAT;
    u64 tstart;
    u64 rescue_combo_deadline = 0;
    int button, x, j, cnf_size, result;
    int splash_early_presented = 0;
    int video_mode_applied = 0;
    int config_has_launch_key_entries = 0;
    int config_read_success = 0;
    static int num_buttons = 16, pad_button = 0x0001; // Scan all 16 buttons
    char *CNFBUFF, *name, *value;

    ClearStaleEEDebugState();
    ReadROMVEROnce();
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
    LogDetectedPlatform();
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

    j = SifLoadModule("rom0:ADDDRV", 0, NULL); // Load ADDDRV. The OSD has it listed in rom0:OSDCNF/IOPBTCONF, but it is otherwise not loaded automatically.
    DPRINTF(" [ADDDRV]: %d\n", j);

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
    g_native_video_mode = detect_native_video_mode();
    DPRINTF("Init pads\n");
    PadInitPads();
    DPRINTF("Init timer and start non-blocking rescue key window\n");
    TimerInit();
    rescue_combo_deadline = Timer() + RESCUE_COMBO_WINDOW_MS;
    PollEmergencyComboWindow(&rescue_combo_deadline);
    DPRINTF("load default settings\n");
    SetDefaultSettings();
    g_config_path_in_use[0] = '\0';
    FILE *fp;
    for (x = SOURCE_CWD; x >= SOURCE_MC0; x--) {
        PollEmergencyComboWindow(&rescue_combo_deadline);
#if defined(PSX)
        if (!g_is_psx_desr && x == SOURCE_XCONFIG)
            continue;
#endif
        char *T = CheckPath(CONFIG_PATHS[x]);
        fp = fopen(T, "r");
        if (fp != NULL) {
            config_source = x;
            snprintf(g_config_path_in_use, sizeof(g_config_path_in_use), "%s", T);
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
                config_read_success = 1;
                int var_cnt = 0;
                char TMP[64];
                for (var_cnt = 0; get_CNF_string(&CNFBUFF, &name, &value); var_cnt++) {
                    PollEmergencyComboWindow(&rescue_combo_deadline);
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
                    if (ci_eq(name, "VIDEO_MODE")) {
                        int parsed_video_mode;
                        if (parse_video_mode_value(value, &parsed_video_mode)) {
                            GLOBCFG.VIDEO_MODE = parsed_video_mode;
                            // Apply video mode as soon as it is known so scalers/displays
                            // can start re-syncing while the rest of config parsing continues.
                            apply_loader_video_mode(parsed_video_mode);
                            SplashRenderSetVideoMode(parsed_video_mode, g_native_video_mode);
                            video_mode_applied = 1;
                        } else
                            DPRINTF("Ignoring invalid VIDEO_MODE value '%s'\n", value);
                        continue;
                    }
                    if (ci_starts_with(name, "NAME_")) {
                        for (x = 0; x < KEY_COUNT; x++) {
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
                        for (x = 0; x < KEY_COUNT; x++) {
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
                        for (x = 0; x < KEY_COUNT; x++) {
                            for (j = 0; j < CONFIG_KEY_INDEXES; j++) {
                                sprintf(TMP, "LK_%s_E%d", KEYS_ID[x], j + 1);
                                if (ci_eq(name, TMP)) {
                                    // Empty string means: skip this slot (try next E# when executing)
                                    if (value == NULL || *value == '\0')
                                        GLOBCFG.KEYPATHS[x][j] = NULL;
                                    else {
                                        GLOBCFG.KEYPATHS[x][j] = value;
                                        config_has_launch_key_entries = 1;
                                    }
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
        // If config parsing did not produce a valid VIDEO_MODE, fall back to
        // the default AUTO/native mode now.
        if (!video_mode_applied)
            apply_loader_video_mode(GLOBCFG.VIDEO_MODE);
        if (config_read_success && !config_has_launch_key_entries)
            RunEmergencyMode("CONFIG FILE HAS NO LAUNCH KEY ENTRIES");

        // Show splash immediately after video mode is known so users can read it
        // while path validation runs. For LOGO_DISPLAY=3, skip the transient
        // Loading... overlay so the first visible hotkey frame is the final
        // NAME_* splash/countdown render.
        if (GLOBCFG.LOGO_DISP > 0) {
            int show_loading_overlay = (GLOBCFG.HOTKEY_DISPLAY != 1);

            if (GLOBCFG.HOTKEY_DISPLAY == 2 || GLOBCFG.HOTKEY_DISPLAY == 3) {
                for (x = 0; x < KEY_COUNT; x++)
                    GLOBCFG.KEYNAMES[x] = "";
            }
            SplashRenderSetVideoMode(GLOBCFG.VIDEO_MODE, g_native_video_mode);
            SplashRenderTextBody(GLOBCFG.LOGO_DISP, g_is_psx_desr);
            if (show_loading_overlay) {
                SplashDrawLoadingStatus(GLOBCFG.LOGO_DISP);
                splash_early_presented = 1;
            }
        }

        ValidateKeypathsAndSetNames(GLOBCFG.HOTKEY_DISPLAY, g_pre_scanned);
    } else {
        scr_printf("Can't find config, loading hardcoded paths\n");
        char **default_paths = DEFPATH;
#if defined(PSX)
        if (!g_is_psx_desr)
            default_paths = DEFPATH_PS2;
#endif
        for (x = 0; x < KEY_COUNT; x++)
            for (j = 0; j < CONFIG_KEY_INDEXES; j++)
                GLOBCFG.KEYPATHS[x][j] = default_paths[CONFIG_KEY_INDEXES * x + j];
        GLOBCFG.LOGO_DISP = normalize_logo_display(LOGO_DISPLAY_DEFAULT);
        GLOBCFG.HOTKEY_DISPLAY = logo_to_hotkey_display(GLOBCFG.LOGO_DISP);
        g_pre_scanned = (GLOBCFG.HOTKEY_DISPLAY == 2 || GLOBCFG.HOTKEY_DISPLAY == 3);
        // No config means no valid VIDEO_MODE was parsed, so apply the default
        // AUTO/native mode now.
        if (!video_mode_applied)
            apply_loader_video_mode(GLOBCFG.VIDEO_MODE);

        // Keep fallback path consistent: show a quick loading overlay once
        // video mode is selected (AUTO/native by default). For LOGO_DISPLAY=3,
        // skip the transient Loading... overlay so the first visible hotkey
        // frame is the final NAME_* splash/countdown render.
        if (GLOBCFG.LOGO_DISP > 0) {
            int show_loading_overlay = (GLOBCFG.HOTKEY_DISPLAY != 1);

            if (GLOBCFG.HOTKEY_DISPLAY == 2 || GLOBCFG.HOTKEY_DISPLAY == 3) {
                for (x = 0; x < KEY_COUNT; x++)
                    GLOBCFG.KEYNAMES[x] = "";
            }
            SplashRenderSetVideoMode(GLOBCFG.VIDEO_MODE, g_native_video_mode);
            SplashRenderTextBody(GLOBCFG.LOGO_DISP, g_is_psx_desr);
            if (show_loading_overlay) {
                SplashDrawLoadingStatus(GLOBCFG.LOGO_DISP);
                splash_early_presented = 1;
            }
        }

        ValidateKeypathsAndSetNames(GLOBCFG.HOTKEY_DISPLAY, g_pre_scanned);
    }

    if (g_video_mode_selector_requested) {
        RunEmergencyVideoModeSelector();
        g_video_mode_selector_requested = 0;
    }

    GameIDSetConfig(GLOBCFG.APP_GAMEID, GLOBCFG.CDROM_DISABLE_GAMEID);
    PS1DRVSetOptions(GLOBCFG.PS1DRV_ENABLE_FAST, GLOBCFG.PS1DRV_ENABLE_SMOOTH, GLOBCFG.PS1DRV_USE_PS1VN);
    SplashRenderSetVideoMode(GLOBCFG.VIDEO_MODE, g_native_video_mode);
    int dev_ok[DEV_COUNT];

    // Stores last key during DELAY msec
    {
        char model_buf[64];
        char ps1_buf[64];
        char dvd_buf[64];
        char src_buf[32];
        char rom_raw[ROMVER_MAX_LEN + 1];
        char rom_buf[32];
        char rom_fmt[8];
        char autoboot_text[48];
        const char *model = "";
        const char *ps1ver = "";
        const char *dvdver = "";
        const char *source = "";
        const char *temp_celsius = NULL;
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
        u64 deadline;
#ifndef NO_TEMP_DISP
        char temp_query_buf[16];
        char temp_render_buf[16];
        int temp_supported = 0;
#endif

        if (!SplashRenderIsActive())
            SplashRenderTextBody(GLOBCFG.LOGO_DISP, g_is_psx_desr);

        if (GLOBCFG.LOGO_DISP > 0) {
            model = strip_crlf_copy(ModelNameGet(), model_buf, sizeof(model_buf));
            ps1ver = strip_crlf_copy(PS1DRVGetVersion(), ps1_buf, sizeof(ps1_buf));
            dvdver = strip_crlf_copy(DVDPlayerGetVersion(), dvd_buf, sizeof(dvd_buf));
            source = strip_crlf_copy(SOURCES[config_source], src_buf, sizeof(src_buf));

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

#ifndef NO_TEMP_DISP
            if (QueryTemperatureCelsius(temp_query_buf, sizeof(temp_query_buf))) {
                strncpy(temp_render_buf, temp_query_buf, sizeof(temp_render_buf));
                temp_render_buf[sizeof(temp_render_buf) - 1] = '\0';
                temp_celsius = temp_render_buf;
                temp_supported = 1;
            }
#endif

            if (GLOBCFG.LOGO_DISP == 1) {
                SplashRenderConsoleInfoLine(GLOBCFG.LOGO_DISP,
                                            model,
                                            rom_fmt,
                                            dvdver,
                                            ps1ver,
                                            temp_celsius,
                                            "",
                                            source);
            }
        } else {
            rom_fmt[0] = '\0';
        }

        if (SplashRenderIsActive()) {
            int pass_count = splash_early_presented ? 1 : 2;
            int pass;
            for (pass = 0; pass < pass_count; pass++) {
                SplashRenderBeginFrame();
                SplashRenderHotkeyLines(GLOBCFG.LOGO_DISP, hotkey_lines);
                SplashRenderConsoleInfoLine(GLOBCFG.LOGO_DISP,
                                            model,
                                            rom_fmt,
                                            dvdver,
                                            ps1ver,
                                            temp_celsius,
                                            "",
                                            source);
                SplashRenderHotkeyClockDate(GLOBCFG.LOGO_DISP, 0);
                SplashRenderPresent();
            }
        }

        DPRINTF("Timer starts!\n");
        PollEmergencyComboWindow(&rescue_combo_deadline);
        tstart = Timer();
        deadline = tstart + GLOBCFG.DELAY;
        build_device_available_cache(dev_ok, DEV_COUNT);
        while (Timer() <= deadline) {
            u64 now = Timer();
            PollEmergencyComboWindow(&rescue_combo_deadline);

            if (SplashRenderIsActive()) {
                const char *render_temp = temp_celsius;
#ifndef NO_TEMP_DISP
                if (temp_supported) {
                    if (QueryTemperatureCelsius(temp_query_buf, sizeof(temp_query_buf))) {
                        strncpy(temp_render_buf, temp_query_buf, sizeof(temp_render_buf));
                        temp_render_buf[sizeof(temp_render_buf) - 1] = '\0';
                    }
                    render_temp = temp_render_buf;
                }
#endif
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
                                            rom_fmt,
                                            dvdver,
                                            ps1ver,
                                            render_temp,
                                            autoboot_text,
                                            source);
                SplashRenderHotkeyClockDate(GLOBCFG.LOGO_DISP, now);
                SplashRenderPresent();
            }

        button = pad_button; // reset the value so we can iterate (bit-shift) again
        PAD = ReadCombinedPadStatus_raw();
        if (!g_hotkey_launches_enabled)
            continue;
        if (g_block_hotkeys_until_release) {
            if (PAD & PAD_MASK_ANY)
                continue;
            g_block_hotkeys_until_release = 0;
        }
        for (x = 0; x < num_buttons; x++) { // check all pad buttons
            if (PAD & button) {
                int command_cancelled = 0;
                int retry_requested = 0;
                const char *button_name = KEYS_ID[x + 1];
                DPRINTF("PAD detected\n");
                // if button detected, copy path to corresponding index
                for (j = 0; j < CONFIG_KEY_INDEXES; j++) {
                    // Skip empty/unset entries (common when config has blank LK_* values)
                    if (GLOBCFG.KEYPATHS[x + 1][j] == NULL || *GLOBCFG.KEYPATHS[x + 1][j] == '\0')
                        continue;
                    if (g_pre_scanned && !is_command_token(GLOBCFG.KEYPATHS[x + 1][j])) {
                        ShowLaunchStatus(GLOBCFG.KEYPATHS[x + 1][j]);
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
                        if (is_command_token(GLOBCFG.KEYPATHS[x + 1][j]))
                            ShowLaunchStatus(GLOBCFG.KEYPATHS[x + 1][j]);
                        if (is_command_token(GLOBCFG.KEYPATHS[x + 1][j]))
                            set_pending_command_args(GLOBCFG.KEYARGC[x + 1][j], GLOBCFG.KEYARGS[x + 1][j]);
                        EXECPATHS[j] = CheckPath(GLOBCFG.KEYPATHS[x + 1][j]);
                        if (is_command_token(GLOBCFG.KEYPATHS[x + 1][j]))
                            set_pending_command_args(0, NULL);
                        if (is_command_token(GLOBCFG.KEYPATHS[x + 1][j]) && g_cdvd_cancelled) {
                            g_cdvd_cancelled = 0;
                            command_cancelled = 1;
                            RestoreSplashInteractiveUi(GLOBCFG.LOGO_DISP,
                                                       hotkey_lines,
                                                       model,
                                                       rom_fmt,
                                                       dvdver,
                                                       ps1ver,
                                                       temp_celsius,
                                                       source);
                            break;
                        }
                        if (!allow_virtual_patinfo_entry(x + 1, j, EXECPATHS[j]) && !exist(EXECPATHS[j])) {
                            scr_setfontcolor(0x00ffff);
                            DPRINTF("%s not found\n", EXECPATHS[j]);
                            scr_setfontcolor(0xffffff);
                            continue;
                        }
                    }
                    if (EXECPATHS[j] != NULL && *EXECPATHS[j] != '\0') {
                        if (!is_command_token(GLOBCFG.KEYPATHS[x + 1][j]))
                            ShowLaunchStatus(EXECPATHS[j]);
                        CleanUp();
                        RunLoaderElf(EXECPATHS[j], MPART, GLOBCFG.KEYARGC[x + 1][j], GLOBCFG.KEYARGS[x + 1][j]);
                    }
                }
                if (!command_cancelled) {
                    retry_requested = WaitForMissingPathAction(button_name,
                                                               model,
                                                               rom_fmt,
                                                               dvdver,
                                                               ps1ver,
                                                               temp_celsius,
                                                               source);
                    if (retry_requested) {
                        if (SplashRenderIsActive())
                            RestoreSplashInteractiveUi(GLOBCFG.LOGO_DISP,
                                                       hotkey_lines,
                                                       model,
                                                       rom_fmt,
                                                       dvdver,
                                                       ps1ver,
                                                       temp_celsius,
                                                       source);
                        else
                            scr_clear();
                        g_block_hotkeys_until_release = 1;
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
        rescue_combo_deadline = 0;
        TimerEnd();
        build_device_available_cache(dev_ok, DEV_COUNT);
    }
    for (j = 0; j < CONFIG_KEY_INDEXES; j++) {
        // Skip empty/unset AUTO entries too
        if (GLOBCFG.KEYPATHS[0][j] == NULL || *GLOBCFG.KEYPATHS[0][j] == '\0')
            continue;
        if (is_command_token(GLOBCFG.KEYPATHS[0][j]))
            continue; // Don't execute commands without a key press.
        if (g_pre_scanned && !is_command_token(GLOBCFG.KEYPATHS[0][j])) {
            ShowLaunchStatus(GLOBCFG.KEYPATHS[0][j]);
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
            if (!allow_virtual_patinfo_entry(0, j, EXECPATHS[j]) && !exist(EXECPATHS[j])) {
                scr_printf("%s %-15s\r", EXECPATHS[j], "not found");
                continue;
            }
        }
        if (EXECPATHS[j] != NULL && *EXECPATHS[j] != '\0') {
            ShowLaunchStatus(EXECPATHS[j]);
            CleanUp();
            RunLoaderElf(EXECPATHS[j], MPART, GLOBCFG.KEYARGC[0][j], GLOBCFG.KEYARGS[0][j]);
        }
    }

    RunEmergencyMode("COULD NOT FIND ANY DEFAULT APPLICATIONS");

    return 0;
}

static void RunEmergencyMode(const char *reason)
{
    int dot_count = 1;
    int dot_tick = 0;

    if (!SplashRenderIsActive()) {
        int emergency_logo_disp = normalize_logo_display(GLOBCFG.LOGO_DISP);

        if (emergency_logo_disp < 1)
            emergency_logo_disp = 1;
        SplashRenderSetVideoMode(GLOBCFG.VIDEO_MODE, g_native_video_mode);
        SplashRenderTextBody(emergency_logo_disp, g_is_psx_desr);
    }

    SplashDrawEmergencyModeStatus(reason, dot_count);
    while (1) {
        usleep(100000);
        dot_tick++;
        if (dot_tick >= 5) {
            dot_tick = 0;
            dot_count = (dot_count % 3) + 1;
            SplashDrawEmergencyModeStatus(reason, dot_count);
        }
        if (exist("mass:/RESCUE.ELF")) {
            if (SplashRenderIsActive())
                SplashRenderEnd();
            CleanUp();
            RunLoaderElf("mass:/RESCUE.ELF", NULL, 0, NULL);
        }
    }
}

void EMERGENCY(void)
{
    RunEmergencyMode(NULL);
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
        g_cdvd_cancelled = 0;
        if (!strcmp("$CDVD", path))
            g_cdvd_cancelled = (dischandler(0, g_pending_command_argc, g_pending_command_argv) < 0);
        if (!strcmp("$CDVD_NO_PS2LOGO", path))
            g_cdvd_cancelled = (dischandler(1, g_pending_command_argc, g_pending_command_argv) < 0);
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
            char *pfs_path = strstr(path, "pfs:");
            DPRINTF("--{%s}--{%s}\n", path, (pfs_path != NULL) ? pfs_path : "<none>");
            return (pfs_path != NULL) ? pfs_path : path;
        } // leave path as pfs:/blabla
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
int dischandler(int skip_ps2logo, int argc, char *argv[])
{
    int OldDiscType, DiscType, ValidDiscInserted, result, first_run = 1;
    int cancel_requested = 0;
    int start_was_down;
    u64 start_hold_deadline = 0;
    int prompt_dots = 0;
    u64 prompt_next_tick = 0;
    int use_splash_ui;
    char model_buf[64];
    char ps1_buf[64];
    char dvd_buf[64];
    char src_buf[32];
    char rom_raw[ROMVER_MAX_LEN + 1];
    char rom_buf[32];
    char rom_fmt[8];
#ifndef NO_TEMP_DISP
    char temp_buf[16];
#endif
    const char *model = "";
    const char *ps1ver = "";
    const char *dvdver = "";
    const char *source = "";
    const char *temp_celsius = NULL;
    const char *egsm_override_arg = NULL;
    char disc_status[64];
    uint32_t egsm_override_flags = 0;
    u32 STAT;

    parse_disc_egsm_override(argc, argv, &egsm_override_flags, &egsm_override_arg);
    if (egsm_override_flags != 0)
        DPRINTF("%s: using command -gsm override '%s' flags=0x%08x\n",
                __func__, egsm_override_arg, (unsigned int)egsm_override_flags);

    use_splash_ui = SplashRenderIsActive();

    if (use_splash_ui) {
        model = strip_crlf_copy(ModelNameGet(), model_buf, sizeof(model_buf));
        ps1ver = strip_crlf_copy(PS1DRVGetVersion(), ps1_buf, sizeof(ps1_buf));
        dvdver = strip_crlf_copy(DVDPlayerGetVersion(), dvd_buf, sizeof(dvd_buf));
        source = strip_crlf_copy(SOURCES[config_source], src_buf, sizeof(src_buf));
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
#ifndef NO_TEMP_DISP
        if (QueryTemperatureCelsius(temp_buf, sizeof(temp_buf)))
            temp_celsius = temp_buf;
#endif
        SplashDrawCenteredStatusWithInfo("CDVD: Waiting for disc (START=Back)",
                                         0x00ffff,
                                         model,
                                         rom_fmt,
                                         dvdver,
                                         ps1ver,
                                         temp_celsius,
                                         source);
    } else {
        scr_clear();
        scr_printf("\n\t%s: Activated\n", __func__);
        scr_printf("\t\tEnabling Diagnosis...\n");
    }

    do { // 0 = enable, 1 = disable.
        result = sceCdAutoAdjustCtrl(0, &STAT);
    } while ((STAT & 0x08) || (result == 0));

    // For this demo, wait for a valid disc to be inserted.
    if (!use_splash_ui)
        scr_printf("\tWaiting for disc to be inserted...\n\n");

    ValidDiscInserted = 0;
    OldDiscType = -1;
    start_was_down = (ReadCombinedPadStatus_raw() & PAD_START) ? 1 : 0;
    if (start_was_down)
        start_hold_deadline = Timer() + 150;
    while (!ValidDiscInserted) {
        PAD = ReadCombinedPadStatus_raw();
        if (PAD & PAD_START) {
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

        DiscType = sceCdGetDiskType();
        if (DiscType != OldDiscType) {
            OldDiscType = DiscType;

            switch (DiscType) {
                case SCECdNODISC:
                    if (first_run) {
                        if (GLOBCFG.TRAYEJECT) // if tray eject is allowed on empty tray...
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
                                                      rom_fmt,
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
                                                         rom_fmt,
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
                                                         rom_fmt,
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
                    ValidDiscInserted = 1;
                    break;

                case SCECdPS2CD:
                case SCECdPS2CDDA:
                case SCECdPS2DVD:
                    prompt_next_tick = 0;
                    if (use_splash_ui)
                        SplashDrawCenteredStatusWithInfo("PlayStation 2 Disc",
                                                         0x00ff00,
                                                         model,
                                                         rom_fmt,
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
                    ValidDiscInserted = 1;
                    break;

                case SCECdCDDA:
                    if (use_splash_ui) {
                        prompt_dots = 0;
                        prompt_next_tick = Timer() + 1000;
                        SplashDrawRetryPromptWithInfo("Audio Disc Not Supported",
                                                      0xffff00,
                                                      prompt_dots,
                                                      model,
                                                      rom_fmt,
                                                      dvdver,
                                                      ps1ver,
                                                      temp_celsius,
                                                      source);
                    }
                    else {
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
                                                         rom_fmt,
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
                    ValidDiscInserted = 1;
                    break;
                default:
                    if (use_splash_ui) {
                        snprintf(disc_status, sizeof(disc_status), "Unknown Disc (%d)", DiscType);
                        prompt_dots = 0;
                        prompt_next_tick = Timer() + 1000;
                        SplashDrawRetryPromptWithInfo(disc_status,
                                                      0x8080ff,
                                                      prompt_dots,
                                                      model,
                                                      rom_fmt,
                                                      dvdver,
                                                      ps1ver,
                                                      temp_celsius,
                                                      source);
                    } else {
                        scr_printf("\tNew Disc:\t");
                        scr_setfontcolor(0x0000ff);
                        scr_printf("Unknown (%d)\n", DiscType);
                        scr_setfontcolor(0xffffff);
                    }
            }
        }

        if (use_splash_ui && prompt_next_tick != 0 && Timer() >= prompt_next_tick) {
            switch (DiscType) {
                case SCECdNODISC:
                    prompt_dots = (prompt_dots + 1) % 4;
                    prompt_next_tick = Timer() + 1000;
                    SplashDrawRetryPromptWithInfo("NO DISC FOUND!",
                                                  0xffff00,
                                                  prompt_dots,
                                                  model,
                                                  rom_fmt,
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
                                                  rom_fmt,
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
                    snprintf(disc_status, sizeof(disc_status), "Unknown Disc (%d)", DiscType);
                    SplashDrawRetryPromptWithInfo(disc_status,
                                                  0x8080ff,
                                                  prompt_dots,
                                                  model,
                                                  rom_fmt,
                                                  dvdver,
                                                  ps1ver,
                                                  temp_celsius,
                                                  source);
                    break;
            }
        }

        // Avoid spamming the IOP with sceCdGetDiskType(), or there may be a deadlock.
        // The NTSC/PAL H-sync is approximately 16kHz. Hence approximately 16 ticks will pass every millisecond.
        SetAlarm(1000 * 16, &AlarmCallback, (void *)GetThreadId());
        SleepThread();
    }

    if (cancel_requested) {
        if (use_splash_ui)
            SplashDrawCenteredStatusWithInfo("CDVD Canceled",
                                             0xffff00,
                                             model,
                                             rom_fmt,
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
        switch (DiscType) {
            case SCECdPSCD:
            case SCECdPSCDDA:
                SplashDrawCenteredStatusWithInfo("Booting PlayStation Disc...",
                                                 0x00ff00,
                                                 model,
                                                 rom_fmt,
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
                                                 rom_fmt,
                                                 dvdver,
                                                 ps1ver,
                                                 temp_celsius,
                                                 source);
                break;
            case SCECdDVDV:
                SplashDrawCenteredStatusWithInfo("Booting DVD Video...",
                                                 0x00ff00,
                                                 model,
                                                 rom_fmt,
                                                 dvdver,
                                                 ps1ver,
                                                 temp_celsius,
                                                 source);
                break;
        }
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
            PS2DiscSetConfigHint(get_disc_config_hint());
            PS2DiscBoot(skip_ps2logo, egsm_override_flags, egsm_override_arg);
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
#if defined(PSX)
    if (g_is_psx_desr) {
        /* sp193: We need some of the PSX's CDVDMAN facilities, but we do not want to use its (too-)new FILEIO module.
           This special IOPRP image contains a IOPBTCONF list that lists PCDVDMAN instead of CDVDMAN.
           PCDVDMAN is the board-specific CDVDMAN module on all PSX, which can be used to switch the CD/DVD drive operating mode.
           Usually, I would discourage people from using board-specific modules, but I do not have a proper replacement for this. */
        while (!SifIopRebootBuffer(psx_ioprp, size_psx_ioprp)) {};
    } else
#endif
    {
        while (!SifIopReset("", 0)) {};
    }
    while (!SifIopSync()) {};

#if defined(PSX)
    if (g_is_psx_desr)
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

static void AlarmCallback(s32 alarm_id, u16 time, void *common)
{
    iWakeupThread((int)common);
}

void CleanUp(void)
{
    if (SplashRenderIsActive())
        SplashRenderEnd();

    sceCdInit(SCECdEXIT);
    // Keep config_buf alive so argv pointers remain valid during ELF load.
    PadDeinitPads();
}

void credits(void)
{
    scr_clear();
    scr_printf("\n\n");
    scr_printf("%s%s", GetRuntimeBanner(), BANNER_FOOTER);
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
