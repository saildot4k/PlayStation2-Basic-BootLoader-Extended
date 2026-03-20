
#include <stdint.h>

/**
 * @brief Hint used for OSDMenu-like OSDGSM.CNF lookup order.
 */
enum
{
    PS2_DISC_HINT_MC0 = 0,
    PS2_DISC_HINT_MC1,
    PS2_DISC_HINT_HDD
};

/**
 * @brief Sets config-source hint used by PS2 disc helpers.
 * @param hint one of PS2_DISC_HINT_*
 */
void PS2DiscSetConfigHint(int hint);

/**
 * @brief  Boots the inserted PlayStation 2 game disc
 * @param skip_PS2LOGO wheter to load the game main executable via rom0:PS2LOGO or run it directly
 * @param egsm_override_flags optional explicit eGSM flags from a command entry (-gsm=...)
 * @param egsm_override_arg original override string for logging/debugging
 * @returns 0 on success.
*/
int PS2DiscBoot(int skip_PS2LOGO, uint32_t egsm_override_flags, const char *egsm_override_arg);

/**
 * @brief Function that reboots the browser with the "BootError" argument.
 * @note You can use this if an unexpected error occurs while booting the software that the user wants to use.
*/
void BootError(void);
