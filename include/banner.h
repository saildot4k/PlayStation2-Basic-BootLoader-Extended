#ifndef BANNER_H
#define BANNER_H


const char *BANNER =
// change the banner text depending on system type, leave versioning and credits the same
    "\tPS2 Basic BootLoader v" VERSION "-" SUBVERSION "-" PATCHLEVEL " - " STATUS  "- By Matias Israelson AKA: EL_isra \n"
    "\thttps://ps2homebrewstore.com - Modified - 9 Paths                    \n"
    "\n"
#ifdef PSX
    "\t       L1: PSBBN-FORWARDER                  R1: NHDDL                \n"
    "\t       L2: RESCUE.ELF                       R2: OPL                  \n"
    "\t                                                                     \n"
    "\t       /\\: OSD-XMB                         /\\:WLE-ISR              \n"
    "\t<: USER    >: RETROLauncher     []: POPSLoader  O: WLE-ISR (REVERSE) \n"
    "\t       \\/: XEB+                            X: DKWDRV                \n"
    "\t                                                                     \n"
    "\t     SELECT: DISC SKIP LOGO               START: DISC                \n"
#else
    "\t       L1: PSBBN-FORWARDER                  R1: NHDDL                \n"
    "\t       L2: OSDSYS                           R2: OPL                  \n"
    "\t                                                                     \n"
    "\t       /\\: OSD-XMB                         /\\:WLE-ISR              \n"
    "\t<: USER    >: RETROLauncher     []: POPSLoader  O: WLE-ISR (REVERSE) \n"
    "\t       \\/: XEB+                            X: DKWDRV                \n"
    "\t                                                                     \n"
    "\t     SELECT: DISC SKIP LOGO               START: DISC                \n"
#endif
    
#ifdef DEBUG
    " - DEBUG"
#endif
    "\n";
#define BANNER_FOOTER                                                   \
    "\n"

#endif
