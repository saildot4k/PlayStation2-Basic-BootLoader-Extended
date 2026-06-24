// HDD module loading, partition mounting, and utility checks.
#include "main.h"

#ifdef HDD
char PART[128] = "\0";
int HDD_USABLE = 0;

#define HDD_CHECKER_WAIT_MS 10000u

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

void HDDChecker(void)
{
    char ErrorPartName[64];
    const char *HEADING = "HDD Diagnosis routine";
    int ret = -1;
    int unit;
    scr_clear();
    scr_printf("\n\n%*s%s\n", ((80 - strlen(HEADING)) / 2), "", HEADING);
    for (unit = 0; unit <= 1; unit++) {
        char hdd_root[] = "hdd0:";

        hdd_unit_root(unit, hdd_root, sizeof(hdd_root));
        scr_setfontcolor(0x0000FF);
        ret = fileXioDevctl(hdd_root, HDIOC_STATUS, NULL, 0, NULL, 0);
        if (ret == 0 || ret == 1)
            scr_setfontcolor(0x00FF00);
        if (ret != 3) {
            scr_printf("\t\t - %s CONNECTION STATUS: %d\n", hdd_root, ret);
            /* Check ATA device S.M.A.R.T. status. */
            ret = fileXioDevctl(hdd_root, HDIOC_SMARTSTAT, NULL, 0, NULL, 0);
            if (ret != 0)
                scr_setfontcolor(0x0000ff);
            else
                scr_setfontcolor(0x00FF00);
            scr_printf("\t\t - %s S.M.A.R.T STATUS: %d\n", hdd_root, ret);
            /* Check for unrecoverable I/O errors on sectors. */
            ret = fileXioDevctl(hdd_root, HDIOC_GETSECTORERROR, NULL, 0, NULL, 0);
            if (ret != 0)
                scr_setfontcolor(0x0000ff);
            else
                scr_setfontcolor(0x00FF00);
            scr_printf("\t\t - %s SECTOR ERRORS: %d\n", hdd_root, ret);
            /* Check for partitions that have errors. */
            ret = fileXioDevctl(hdd_root, HDIOC_GETERRORPARTNAME, NULL, 0, ErrorPartName, sizeof(ErrorPartName));
            if (ret != 0)
                scr_setfontcolor(0x0000ff);
            else
                scr_setfontcolor(0x00FF00);
            scr_printf("\t\t - %s CORRUPTED PARTITIONS: %d\n", hdd_root, ret);
            if (ret != 0) {
                scr_printf("\t\tpartition: %s\n", ErrorPartName);
            }
        } else {
            scr_setfontcolor(0x00FFFF);
            scr_printf("\t\t - %s not connected\n", hdd_root);
        }
    }
    scr_setfontcolor(0xFFFFFF);
    scr_printf("\t\tWaiting for 10 seconds...\n");
    delay_ms(HDD_CHECKER_WAIT_MS);
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
