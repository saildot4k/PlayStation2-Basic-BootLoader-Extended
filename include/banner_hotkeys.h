#ifndef BANNER_HOTKEYS_H
#define BANNER_HOTKEYS_H

// Hotkey name tokens (use these inside BANNER_HOTKEYS_NAMES).
#define NAME_AUTO "{NAME_AUTO}"
#define NAME_SELECT "{NAME_SELECT}"
#define NAME_L3 "{NAME_L3}"
#define NAME_R3 "{NAME_R3}"
#define NAME_START "{NAME_START}"
#define NAME_UP "{NAME_UP}"
#define NAME_RIGHT "{NAME_RIGHT}"
#define NAME_DOWN "{NAME_DOWN}"
#define NAME_LEFT "{NAME_LEFT}"
#define NAME_L2 "{NAME_L2}"
#define NAME_R2 "{NAME_R2}"
#define NAME_L1 "{NAME_L1}"
#define NAME_R1 "{NAME_R1}"
#define NAME_TRIANGLE "{NAME_TRIANGLE}"
#define NAME_CIRCLE "{NAME_CIRCLE}"
#define NAME_CROSS "{NAME_CROSS}"
#define NAME_SQUARE "{NAME_SQUARE}"

const char *BANNER_HOTKEYS =
#ifdef PSX
    "\tPSX Basic BootLoader v" VERSION "-" SUBVERSION "-" PATCHLEVEL " - " STATUS "- By Matias Israelson AKA: EL_isra \n"
#else
    "\tPS2 Basic BootLoader v" VERSION "-" SUBVERSION "-" PATCHLEVEL " - " STATUS "- By Matias Israelson AKA: EL_isra \n"
#endif
    "\thttps://ps2homebrewstore.com - Modified - 9 Paths                    \n"
    "\n"
const char *BANNER_HOTKEYS_NAMES =
    "\tAUTO:     " NAME_AUTO "\n"
    "\tTRIANGLE: " NAME_TRIANGLE "\n"
    "\tCIRCLE:   " NAME_CIRCLE "\n"
    "\tCROSS:    " NAME_CROSS "\n"
    "\tSQUARE:   " NAME_SQUARE "\n"
    "\tUP:       " NAME_UP "\n"
    "\tDOWN:     " NAME_DOWN "\n"
    "\tLEFT:     " NAME_LEFT "\n"
    "\tRIGHT:    " NAME_RIGHT "\n"
    "\tL1:       " NAME_L1 "\n"
    "\tL2:       " NAME_L2 "\n"
    "\tL3:       " NAME_L3 "\n"
    "\tR1:       " NAME_R1 "\n"
    "\tR2:       " NAME_R2 "\n"
    "\tR3:       " NAME_R3 "\n"
    "\tSTART:    " NAME_START "\n"
    "\tSELECT:   " NAME_SELECT "\n"
    
#ifdef DEBUG
    " - DEBUG"
#endif
    "\n"

#endif
