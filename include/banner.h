#ifndef BANNER_H
#define BANNER_H

#if defined(PSX)
const char *BANNER_PSX =
    "\t\t__________  _____________  _______________________.____\n"
    "\t\t\\______   \\/   _____/\\   \\/  /\\______   \\______   \\    |\n"
    "\t\t |     ___/\\_____  \\  \\     /  |    |  _/|    |  _/    |\n"
    "\t\t |    |    /        \\ /     \\  |    |   \\|    |   \\    |___\n"
    "\t\t |____|   /_______  //___/\\  \\ |______  /|______  /_______ \\\n"
    "\t\t                  \\/       \\_/        \\/        \\/        \\/\n"
    "\t\t\tv" VERSION "-" SUBVERSION "-" PATCHLEVEL " - " STATUS
#ifdef DEBUG
    " - DEBUG"
#endif
    "\n"
    "\n";
#endif

const char *BANNER_PS2 =
    "\t\t__________  _________________   ____________________.____\n"
    "\t\t\\______   \\/   _____/\\_____  \\  \\______   \\______   \\    |\n"
    "\t\t |     ___/\\_____  \\  /  ____/   |    |  _/|    |  _/    |\n"
    "\t\t |    |    /        \\/       \\   |    |   \\|    |   \\    |___\n"
    "\t\t |____|   /_______  /\\_______ \\  |______  /|______  /_______ \\\n"
    "\t\t                  \\/         \\/         \\/        \\/        \\/\n"
    "\t\t\tv" VERSION "-" SUBVERSION "-" PATCHLEVEL " - " STATUS
#ifdef DEBUG
    " - DEBUG"
#endif
    "\n"
    "\n";

#if defined(PSX)
#define BANNER BANNER_PSX
#else
#define BANNER BANNER_PS2
#endif

#define BANNER_FOOTER                                                   \
    "\t\t		PlayStation2 Basic BootLoader - By Matias Israelson\n"  \
    "\t\t                                             (AKA: El_isra)\n" \
    "\t\t       https://ps2homebrewstore.com - Modified - 9 Paths\n"    \
    "\n" \
    "\n" \

#endif
