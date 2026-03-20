// IOP module loading pipeline (pads, MC, USB, MMCE/MX4SIO, HDD, DEV9).
#include "main.h"

void LoaderLoadSystemModules(int *usb_modules_loaded,
                             int *mx4sio_modules_loaded,
                             int *mmce_modules_loaded,
                             int *hdd_modules_loaded)
{
    int j, x;

    if (usb_modules_loaded != NULL)
        *usb_modules_loaded = 0;
    if (mx4sio_modules_loaded != NULL)
        *mx4sio_modules_loaded = 0;
    if (mmce_modules_loaded != NULL)
        *mmce_modules_loaded = 0;
    if (hdd_modules_loaded != NULL)
        *hdd_modules_loaded = 0;

#ifdef HDD
    int filexio_ok = 1;
#endif

#ifdef PPCTTY
    // no error handling bc nothing to do in this case
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
    if (usb_modules_loaded != NULL)
        *usb_modules_loaded = (j == 0);
    if (j != 0) {
        scr_setfontcolor(0x0000ff);
        scr_printf("ERROR: could not load USB modules (%d)\n", j);
        scr_setfontcolor(0xffffff);
#ifdef HAS_EMBEDDED_IRX // we have embedded IRX... something bad is going on if this condition executes. add a wait time for user to know something is wrong
        sleep(1);
#endif
    }

#ifdef FILEXIO
    if (LoadFIO() < 0) {
        scr_setbgcolor(0xff0000);
        scr_clear();
        sleep(4);
#ifdef HDD
        filexio_ok = 0;
#endif
    }
#endif

#ifdef MMCE
    j = SifExecModuleBuffer(mmceman_irx, size_mmceman_irx, 0, NULL, &x);
    DPRINTF(" [MMCEMAN]: ID=%d, ret=%d\n", j, x);
    if (mmce_modules_loaded != NULL)
        *mmce_modules_loaded = (j >= 0);
#endif

#ifdef MX4SIO
    j = SifExecModuleBuffer(mx4sio_bd_irx, size_mx4sio_bd_irx, 0, NULL, &x);
    DPRINTF(" [MX4SIO_BD]: ID=%d, ret=%d\n", j, x);
    if (mx4sio_modules_loaded != NULL)
        *mx4sio_modules_loaded = (j >= 0);
#endif

#ifdef HDD
    if (filexio_ok) {
        int hdd_ret = LoadHDDIRX(); // only load HDD crap if filexio and iomanx are up and running

        if (hdd_ret < 0) {
            scr_setbgcolor(0x0000ff);
            scr_clear();
            sleep(4);
        } else if (hdd_modules_loaded != NULL) {
            *hdd_modules_loaded = 1;
        }
    }
#endif

    j = SifLoadModule("rom0:ADDDRV", 0, NULL); // Load ADDDRV. The OSD has it listed in rom0:OSDCNF/IOPBTCONF, but it is otherwise not loaded automatically.
    DPRINTF(" [ADDDRV]: %d\n", j);
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
static int dev9_loaded = 0;

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
void loadUDPTTY(void)
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
