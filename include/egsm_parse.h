#ifndef EGSM_PARSE_H
#define EGSM_PARSE_H

#include <stdint.h>

// Compatibility flags shared by the main launcher and the embedded stage2 loader.
#define EGSM_FLAG_VMODE_FP1 (1 << 0)      // Force 240p/480p
#define EGSM_FLAG_VMODE_FP2 (1 << 1)      // Force 480p/576p
#define EGSM_FLAG_VMODE_1080I_X1 (1 << 2) // Force 1080i, width/height x1
#define EGSM_FLAG_VMODE_1080I_X2 (1 << 3) // Force 1080i, width/height x2
#define EGSM_FLAG_VMODE_1080I_X3 (1 << 4) // Force 1080i, width/height x3
#define EGSM_FLAG_COMP_1 (1 << 5)         // Enable field flipping type 1
#define EGSM_FLAG_COMP_2 (1 << 6)         // Enable field flipping type 2
#define EGSM_FLAG_COMP_3 (1 << 7)         // Enable field flipping type 3
#define EGSM_FLAG_NO_576P (1 << 8)        // Disable 576p (not supported on pre-Deckard consoles)

uint32_t parse_egsm_flags_common(const char *gsm_arg);

#endif
