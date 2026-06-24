// HDD module loading, partition mounting, and utility checks.
#include "main.h"
#include "splash_render.h"

#include <stdarg.h>

#ifdef HDD
char PART[128] = "\0";
int HDD_USABLE = 0;

#define HDD_CHECKER_POLL_INTERVAL_MS 50u
#define HDD_CHECKER_MAX_LINES 20
#define HDD_CHECKER_LINE_LEN 96
#define HDD_CHECKER_LINE_SPACING 16
#define HDD_CHECKER_GLYPH_ADVANCE 6
#define HDD_CHECKER_GLYPH_HEIGHT 7
#define HDD_CHECKER_MARGIN 8
#define HDD_CHECKER_PROMPT_PREFIX "PRESS "
#define HDD_CHECKER_PROMPT_WORD "START"
#define HDD_CHECKER_PROMPT_SUFFIX " TO RETURN TO LAUNCH KEYS"
#define HDD_CHECKER_COLOR_TITLE 0x00ffff
#define HDD_CHECKER_COLOR_OK 0x00ff00
#define HDD_CHECKER_COLOR_WARN 0xffff00
#define HDD_CHECKER_COLOR_BAD 0xff0000
#define HDD_CHECKER_COLOR_INFO 0x00ffff
#define HDD_CHECKER_COLOR_TEXT 0xffffff
#define HDD_CHECKER_COLOR_PROMPT 0x15d670

typedef struct
{
    char text[HDD_CHECKER_LINE_LEN];
    u32 color;
} HDDCheckerLine;

static int hdd_unit_from_path(const char *path)
{
    if (path == NULL || !ci_starts_with(path, "hdd"))
        return -1;
    if (path[3] >= '0' && path[3] <= '1' && path[4] == ':')
        return path[3] - '0';
    return -1;
}

static void hdd_unit_root(int unit, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0)
        return;
    snprintf(out, out_size, "hdd%d:", unit);
}

static int CheckHDDUnit(int unit)
{
    char hdd_root[] = "hdd0:";
    int ret;

    if (unit < 0 || unit > 1)
        return -1;

    hdd_unit_root(unit, hdd_root, sizeof(hdd_root));
    ret = fileXioDevctl(hdd_root, HDIOC_STATUS, NULL, 0, NULL, 0);
    /* 0 = HDD connected and formatted, 1 = not formatted, 2 = HDD not usable, 3 = HDD not connected. */
    DPRINTF("%s: %s status is %d\n", __func__, hdd_root, ret);
    if ((ret >= 3) || (ret < 0))
        return -1;
    return ret;
}

int LoadHDDIRX(const char *path_hint)
{
    int ID, RET, HDDSTAT;
    int hdd_unit = hdd_unit_from_path(path_hint);
    static const char hddarg[] = "-o"
                                 "\0"
                                 "4"
                                 "\0"
                                 "-n"
                                 "\0"
                                 "20";
    //static const char pfsarg[] = "-n\0" "24\0" "-o\0" "8";

    if (hdd_unit < 0) {
        DPRINTF(" [HDD]: explicit hdd0:/hdd1: path required, got '%s'\n",
                (path_hint != NULL) ? path_hint : "<null>");
        return -1;
    }

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

    HDDSTAT = CheckHDDUnit(hdd_unit);
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
    BUF = strdup(path); //use strdup, otherwise, path will become `hddN:`
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

static void hdd_checker_add_line(HDDCheckerLine lines[HDD_CHECKER_MAX_LINES],
                                 int *line_count,
                                 u32 color,
                                 const char *fmt,
                                 ...)
{
    va_list args;

    if (lines == NULL || line_count == NULL || fmt == NULL)
        return;
    if (*line_count < 0 || *line_count >= HDD_CHECKER_MAX_LINES)
        return;

    va_start(args, fmt);
    vsnprintf(lines[*line_count].text, sizeof(lines[*line_count].text), fmt, args);
    va_end(args);
    lines[*line_count].color = color;
    (*line_count)++;
}

static u32 hdd_checker_connection_color(int status)
{
    if (status == 0)
        return HDD_CHECKER_COLOR_OK;
    return HDD_CHECKER_COLOR_BAD;
}

static const char *hdd_checker_connection_description(int status)
{
    if (status < 0)
        return "DEVCTL ERROR";

    switch (status) {
        case 0:
            return "OK - APA FORMATTED";
        case 1:
            return "NOT APA FORMATTED";
        case 2:
            return "NOT USABLE";
        case 3:
            return "NOT CONNECTED";
        default:
            return "UNKNOWN STATUS";
    }
}

static const char *hdd_checker_smart_description(int status)
{
    if (status < 0)
        return "DEVCTL ERROR";

    switch (status) {
        case 0:
            return "OK";
        case 1:
            return "THRESHOLD EXCEEDED";
        default:
            return "ATA ERROR";
    }
}

static u32 hdd_checker_result_color(int result)
{
    return (result == 0) ? HDD_CHECKER_COLOR_OK : HDD_CHECKER_COLOR_BAD;
}

static void hdd_checker_collect_unit_lines(HDDCheckerLine lines[HDD_CHECKER_MAX_LINES],
                                           int *line_count,
                                           int unit)
{
    char hdd_root[] = "hdd0:";
    char ErrorPartName[64];
    int ret;

    hdd_unit_root(unit, hdd_root, sizeof(hdd_root));
    hdd_checker_add_line(lines, line_count, HDD_CHECKER_COLOR_TITLE, "%s DIAGNOSTICS", hdd_root);

    ret = fileXioDevctl(hdd_root, HDIOC_STATUS, NULL, 0, NULL, 0);
    if (ret < 0) {
        hdd_checker_add_line(lines,
                             line_count,
                             hdd_checker_connection_color(ret),
                             "  CONNECTION STATUS: %d - %s",
                             ret,
                             hdd_checker_connection_description(ret));
        return;
    }
    if (ret == 3) {
        hdd_checker_add_line(lines,
                             line_count,
                             hdd_checker_connection_color(ret),
                             "  CONNECTION STATUS: %d - %s",
                             ret,
                             hdd_checker_connection_description(ret));
        return;
    }

    hdd_checker_add_line(lines,
                         line_count,
                         hdd_checker_connection_color(ret),
                         "  CONNECTION STATUS: %d - %s",
                         ret,
                         hdd_checker_connection_description(ret));

    ret = fileXioDevctl(hdd_root, HDIOC_SMARTSTAT, NULL, 0, NULL, 0);
    hdd_checker_add_line(lines,
                         line_count,
                         hdd_checker_result_color(ret),
                         "  S.M.A.R.T STATUS: %d - %s",
                         ret,
                         hdd_checker_smart_description(ret));

    ret = fileXioDevctl(hdd_root, HDIOC_GETSECTORERROR, NULL, 0, NULL, 0);
    hdd_checker_add_line(lines,
                         line_count,
                         hdd_checker_result_color(ret),
                         "  SECTOR ERRORS: %d",
                         ret);

    memset(ErrorPartName, 0, sizeof(ErrorPartName));
    ret = fileXioDevctl(hdd_root, HDIOC_GETERRORPARTNAME, NULL, 0, ErrorPartName, sizeof(ErrorPartName));
    hdd_checker_add_line(lines,
                         line_count,
                         hdd_checker_result_color(ret),
                         "  CORRUPTED PARTITIONS: %d",
                         ret);
    if (ret != 0 && ErrorPartName[0] != '\0')
        hdd_checker_add_line(lines, line_count, HDD_CHECKER_COLOR_BAD, "  PARTITION: %s", ErrorPartName);
}

static int hdd_checker_collect_lines(HDDCheckerLine lines[HDD_CHECKER_MAX_LINES])
{
    int line_count = 0;
    int unit;

    hdd_checker_add_line(lines, &line_count, HDD_CHECKER_COLOR_TITLE, "HDD DIAGNOSIS ROUTINE");
    hdd_checker_add_line(lines, &line_count, HDD_CHECKER_COLOR_WARN, "CHECKS APA FORMATTED DRIVES ONLY!");
    hdd_checker_add_line(lines, &line_count, HDD_CHECKER_COLOR_TEXT, "");
    for (unit = 0; unit <= 1; unit++) {
        hdd_checker_collect_unit_lines(lines, &line_count, unit);
        if (unit == 0)
            hdd_checker_add_line(lines, &line_count, HDD_CHECKER_COLOR_TEXT, "");
    }
    return line_count;
}

static void hdd_checker_draw_centered_line(int screen_w,
                                           int anchor_center_x,
                                           int y,
                                           u32 color,
                                           const char *text)
{
    int line_w;
    int x;

    if (text == NULL)
        return;

    line_w = (int)strlen(text) * HDD_CHECKER_GLYPH_ADVANCE;
    x = anchor_center_x - (line_w / 2);

    if (x < HDD_CHECKER_MARGIN)
        x = HDD_CHECKER_MARGIN;
    if (x + line_w > screen_w - HDD_CHECKER_MARGIN)
        x = screen_w - line_w - HDD_CHECKER_MARGIN;
    if (x < HDD_CHECKER_MARGIN)
        x = HDD_CHECKER_MARGIN;

    SplashRenderDrawTextPxScaled(x, y, color, text, 1);
}

static void hdd_checker_draw_prompt_line(int screen_w, int anchor_center_x, int y)
{
    int prefix_w = (int)strlen(HDD_CHECKER_PROMPT_PREFIX) * HDD_CHECKER_GLYPH_ADVANCE;
    int word_w = (int)strlen(HDD_CHECKER_PROMPT_WORD) * HDD_CHECKER_GLYPH_ADVANCE;
    int suffix_w = (int)strlen(HDD_CHECKER_PROMPT_SUFFIX) * HDD_CHECKER_GLYPH_ADVANCE;
    int line_w = prefix_w + word_w + suffix_w;
    int x = anchor_center_x - (line_w / 2);

    if (x < HDD_CHECKER_MARGIN)
        x = HDD_CHECKER_MARGIN;
    if (x + line_w > screen_w - HDD_CHECKER_MARGIN)
        x = screen_w - line_w - HDD_CHECKER_MARGIN;
    if (x < HDD_CHECKER_MARGIN)
        x = HDD_CHECKER_MARGIN;

    SplashRenderDrawTextPxScaled(x, y, HDD_CHECKER_COLOR_TEXT, HDD_CHECKER_PROMPT_PREFIX, 1);
    SplashRenderDrawTextPxScaled(x + prefix_w, y, HDD_CHECKER_COLOR_PROMPT, HDD_CHECKER_PROMPT_WORD, 1);
    SplashRenderDrawTextPxScaled(x + prefix_w + word_w, y, HDD_CHECKER_COLOR_TEXT, HDD_CHECKER_PROMPT_SUFFIX, 1);
}

static void hdd_checker_draw_splash_frame(const HDDCheckerLine lines[HDD_CHECKER_MAX_LINES],
                                          int line_count)
{
    int screen_w;
    int screen_h;
    int anchor_center_x;
    int y;
    int max_y;
    int total_height;
    int prompt_y;
    int i;

    if (lines == NULL || line_count <= 0 || !SplashRenderIsActive())
        return;

    screen_w = SplashRenderGetScreenWidth();
    screen_h = SplashRenderGetScreenHeight();
    anchor_center_x = screen_w / 2;
    total_height = (line_count * HDD_CHECKER_LINE_SPACING) + HDD_CHECKER_GLYPH_HEIGHT;
    y = (screen_h - total_height) / 2;

    max_y = screen_h - total_height - HDD_CHECKER_MARGIN;
    if (y > max_y)
        y = max_y;
    if (y < HDD_CHECKER_MARGIN)
        y = HDD_CHECKER_MARGIN;
    prompt_y = y + (line_count * HDD_CHECKER_LINE_SPACING);

    SplashRenderSetHotkeysVisible(0);
    SplashRenderBeginFrame();
    for (i = 0; i < line_count; i++) {
        hdd_checker_draw_centered_line(screen_w,
                                       anchor_center_x,
                                       y + (i * HDD_CHECKER_LINE_SPACING),
                                       lines[i].color,
                                       lines[i].text);
    }
    hdd_checker_draw_prompt_line(screen_w, anchor_center_x, prompt_y);
    SplashRenderPresent();
}

static void hdd_checker_draw_console(const HDDCheckerLine lines[HDD_CHECKER_MAX_LINES],
                                     int line_count)
{
    int i;

    scr_clear();
    for (i = 0; i < line_count; i++) {
        scr_setfontcolor(lines[i].color);
        if (lines[i].text[0] == '\0')
            scr_printf("\n");
        else
            scr_printf("\t%s\n", lines[i].text);
    }
    scr_setfontcolor(HDD_CHECKER_COLOR_TEXT);
    scr_printf("\t%s", HDD_CHECKER_PROMPT_PREFIX);
    scr_setfontcolor(HDD_CHECKER_COLOR_PROMPT);
    scr_printf("%s", HDD_CHECKER_PROMPT_WORD);
    scr_setfontcolor(HDD_CHECKER_COLOR_TEXT);
    scr_printf("%s\n", HDD_CHECKER_PROMPT_SUFFIX);
    scr_setfontcolor(0xffffff);
}

static void hdd_checker_wait_for_start(const HDDCheckerLine lines[HDD_CHECKER_MAX_LINES],
                                       int line_count,
                                       int use_splash)
{
    int prev_pad = ReadCombinedPadStatus_raw();

    while (1) {
        int pad = ReadCombinedPadStatus_raw();

        if (use_splash)
            hdd_checker_draw_splash_frame(lines, line_count);
        if (!(prev_pad & PAD_START) && (pad & PAD_START))
            break;

        prev_pad = pad;
        delay_ms(HDD_CHECKER_POLL_INTERVAL_MS);
    }
}

void HDDChecker(void)
{
    HDDCheckerLine lines[HDD_CHECKER_MAX_LINES];
    int line_count = hdd_checker_collect_lines(lines);
    int use_splash = SplashRenderIsActive();
    int previous_logo_visible = 0;

    if (use_splash) {
        previous_logo_visible = SplashRenderGetLogoVisible();
        SplashRenderSetLogoVisible(0);
        hdd_checker_draw_splash_frame(lines, line_count);
    } else {
        hdd_checker_draw_console(lines, line_count);
    }

    hdd_checker_wait_for_start(lines, line_count, use_splash);
    if (use_splash)
        SplashRenderSetLogoVisible(previous_logo_visible);
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
