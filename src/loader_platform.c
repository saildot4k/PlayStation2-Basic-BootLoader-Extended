// Platform-specific low-level boot helpers (IOP reset, PSX paths, boot certify).
#include "main.h"
#include "ee_asm.h"

extern int g_is_psx_desr;

void LoaderPlatformClearStaleEEDebugState(void)
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

#ifdef PSX
static void InitPSX(void)
{
    int result;
    u32 STAT;

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

void CDVDBootCertify(u8 romver[16])
{
    u8 RomName[4];
    int romver_valid = 0;

    if (romver != NULL &&
        romver[0] >= '0' && romver[0] <= '9' &&
        romver[1] >= '0' && romver[1] <= '9' &&
        romver[2] >= '0' && romver[2] <= '9' &&
        romver[3] >= '0' && romver[3] <= '9' &&
        romver[4] != '\0' &&
        romver[5] != '\0')
        romver_valid = 1;

    /*  Perform boot certification to enable the CD/DVD drive.
        This is not required for the PSX, as its OSDSYS will do it before booting the update. */
    if (romver_valid) {
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
        scr_printf("\tERROR: Could not certify CDVD Boot. ROMVER unavailable/invalid\n");
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
