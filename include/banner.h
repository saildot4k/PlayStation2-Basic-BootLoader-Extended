#ifndef BANNER_H
#define BANNER_H


const char *BANNER =
// change the banner text depending on system type, leave versioning and credits the same
    "\tPS2 Basic BootLoader - By Matias Israelson AKA: EL_isra \n"
    "\tv" VERSION "-" SUBVERSION "-" PATCHLEVEL " - " STATUS
    "\n"
    "\thttps://ps2store.com - Modified - 9 Paths               \n"
    "\n"
#ifdef PSX
    "\t\t__________  _____________  _______________________.____\n"
    "\t\t\\______   \\/   _____/\\   \\/  /\\______   \\______   \\    |\n"
    "\t\t |     ___/\\_____  \\  \\     /  |    |  _/|    |  _/    |\n"
    "\t\t |    |    /        \\ /     \\  |    |   \\|    |   \\    |___\n"
    "\t\t |____|   /_______  //___/\\  \\ |______  /|______  /_______ \\\n"
    "\t\t                  \\/       \\_/        \\/        \\/        \\/\n"
#else
    "\t       L1: PSBBN-FORWARDER               R1: NHDDL                \n"
    "\t       L2: OSDSYS                        R2: OPL                  \n"
    "\t                                                                  \n"
    "\t       /\\: OSD-XMB                      /\\:WLE-ISR              \n"
    "\t<: USER    >: RETROLauncher  []: POPSLoader  O: WLE-ISR (REVERSE) \n"
    "\t       \\/: XEB+                         X: DKWDRV                \n"
    "\t     SELECT: DISC SKIP LOGO             START: DISC               \n"
#endif
    
#ifdef DEBUG
    " - DEBUG"
#endif
    "\n";
#define BANNER_FOOTER                                                   \
    "\n"

#endif
