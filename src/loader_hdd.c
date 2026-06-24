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
#define HDD_CHECKER_MARGIN 8
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
    if (status == 0 || status == 1)
        return HDD_CHECKER_COLOR_OK;
    if (status == 2)
        return HDD_CHECKER_COLOR_WARN;
    if (status == 3)
        return HDD_CHECKER_COLOR_INFO;
    return HDD_CHECKER_COLOR_BAD;
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
        hdd_checker_add_line(lines, line_count, HDD_CHECKER_COLOR_BAD, "  STATUS ERROR: %d", ret);
        return;
    }
    if (ret == 3) {
        hdd_checker_add_line(lines, line_count, HDD_CHECKER_COLOR_INFO, "  NOT CONNECTED");
        return;
    }

    hdd_checker_add_line(lines,
                         line_count,
                         hdd_checker_connection_color(ret),
                         "  CONNECTION STATUS: %d",
                         ret);

    ret = fileXioDevctl(hdd_root, HDIOC_SMARTSTAT, NULL, 0, NULL, 0);
    hdd_checker_add_line(lines,
                         line_count,
                         hdd_checker_result_color(ret),
                         "  S.M.A.R.T STATUS: %d",
                         ret);

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
    hdd_checker_add_line(lines, &line_count, HDD_CHECKER_COLOR_TEXT, "");
    for (unit = 0; unit <= 1; unit++) {
        hdd_checker_collect_unit_lines(lines, &line_count, unit);
        if (unit == 0)
            hdd_checker_add_line(lines, &line_count, HDD_CHECKER_COLOR_TEXT, "");
    }
    hdd_checker_add_line(lines, &line_count, HDD_CHECKER_COLOR_TEXT, "");
    hdd_checker_add_line(lines, &line_count, HDD_CHECKER_COLOR_PROMPT, "PRESS START TO RETURN TO LAUNCH KEYS");
    return line_count;
}

static void hdd_checker_draw_splash_frame(const HDDCheckerLine lines[HDD_CHECKER_MAX_LINES],
                                          int line_count)
{
    int screen_w;
    int screen_h;
    int anchor_center_x;
    int y;
    int total_height;
    int logo_x;
    int logo_y;
    int logo_w;
    int logo_h;
    int i;

    if (lines == NULL || line_count <= 0 || !SplashRenderIsActive())
        return;

    screen_w = SplashRenderGetScreenWidth();
    screen_h = SplashRenderGetScreenHeight();
    anchor_center_x = screen_w / 2;
    total_height = line_count * HDD_CHECKER_LINE_SPACING;
    y = (screen_h - total_height) / 2;

    logo_x = SplashRenderGetLogoX();
    logo_y = SplashRenderGetLogoY();
    logo_w = SplashRenderGetLogoWidth();
    logo_h = SplashRenderGetLogoHeight();
    if (logo_x >= 0 && logo_y >= 0 && logo_w > 0 && logo_h > 0) {
        anchor_center_x = logo_x + (logo_w / 2);
        y = logo_y + logo_h + 6;
    }

    if (y + total_height > screen_h - HDD_CHECKER_MARGIN)
        y = screen_h - total_height - HDD_CHECKER_MARGIN;
    if (y < HDD_CHECKER_MARGIN)
        y = HDD_CHECKER_MARGIN;

    SplashRenderSetHotkeysVisible(0);
    SplashRenderBeginFrame();
    for (i = 0; i < line_count; i++) {
        int line_w = (int)strlen(lines[i].text) * HDD_CHECKER_GLYPH_ADVANCE;
        int x = anchor_center_x - (line_w / 2);

        if (x < HDD_CHECKER_MARGIN)
            x = HDD_CHECKER_MARGIN;
        if (x + line_w > screen_w - HDD_CHECKER_MARGIN)
            x = screen_w - line_w - HDD_CHECKER_MARGIN;
        if (x < HDD_CHECKER_MARGIN)
            x = HDD_CHECKER_MARGIN;

        SplashRenderDrawTextPxScaled(x, y + (i * HDD_CHECKER_LINE_SPACING), lines[i].color, lines[i].text, 1);
    }
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

    if (use_splash)
        hdd_checker_draw_splash_frame(lines, line_count);
    else
        hdd_checker_draw_console(lines, line_count);

    hdd_checker_wait_for_start(lines, line_count, use_splash);
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
