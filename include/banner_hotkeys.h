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
    "  PSX Basic BootLoader v" VERSION "-" SUBVERSION "-" PATCHLEVEL " - " STATUS "- By Matias Israelson AKA: EL_isra \n"
#else
    "  PS2 Basic BootLoader v" VERSION "-" SUBVERSION "-" PATCHLEVEL " - " STATUS "- By Matias Israelson AKA: EL_isra \n"
#endif
    "\n\n";
const char *BANNER_HOTKEYS_NAMES =
#ifdef PSX
    "  __________  ____________  ___ \n"
    "  \\______   \\/   _____/\\  \\/  / AUTO:     " NAME_AUTO "\n"
    "  |     ___/\\_____  \\  \\     /   TRIANGLE: " NAME_TRIANGLE "\n"
    "  |    |    /        \\ /     \\   CIRCLE:   " NAME_CIRCLE "\n"
    "  |____|   /_______  //___/\\  \\  CROSS:    " NAME_CROSS "\n"
    "                   \\/       \\_/  SQUARE:   " NAME_SQUARE "\n"
#else
    "  _________  _________________  \n"
    "  \\______  \\/   _____/\\_____  \\  AUTO:     " NAME_AUTO "\n"
    "  |     ___/\\_____  \\  /  ____/  TRIANGLE: " NAME_TRIANGLE "\n"
    "  |    |    /        \\/       \\  CIRCLE:   " NAME_CIRCLE "\n"
    "  |____|   /_______  /\\_______ \\ CROSS:    " NAME_CROSS "\n"
    "                   \\/         \\/ SQUARE:   " NAME_SQUARE "\n"
#endif
    " ____________________._____      UP:       " NAME_UP "\n"
    "  \\______   \\______   \\    |     DOWN:     " NAME_DOWN "\n"
    "  |    |  _/|    |  _/     |     LEFT:     " NAME_LEFT "\n"
    "  |    |   \\|    |   \\     |___  RIGHT:    " NAME_RIGHT "\n"
    "  |______  /|______  /________ \\ L1:       " NAME_L1 "\n"
    "         \\/        \\/         \\/ L2:       " NAME_L2 "\n"
    "                                 L3:       " NAME_L3 "\n"
    "                                 R1:       " NAME_R1 "\n"
    "                                 R2:       " NAME_R2 "\n"
    "                                 R3:       " NAME_R3 "\n"
    "                                 START:    " NAME_START "\n"
    "                                 SELECT:   " NAME_SELECT "\n"

#ifdef DEBUG
    " - DEBUG"
#endif
    "";

#endif
