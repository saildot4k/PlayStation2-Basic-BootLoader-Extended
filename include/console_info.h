#ifndef CONSOLE_INFO_H
#define CONSOLE_INFO_H

#include <stddef.h>
#include <tamtypes.h>

typedef struct
{
    char model[64];
    char ps1ver[64];
    char dvdver[64];
    char source[32];
    char rom_fmt[8];
#ifndef NO_TEMP_DISP
    char temp_celsius[16];
    int temp_checked;
    int temp_supported;
#endif
} ConsoleInfo;

int ConsoleInfoInit(void);
void ConsoleInfoCapture(ConsoleInfo *info, int config_source, const u8 *romver, size_t romver_len);
const char *ConsoleInfoRefreshTemperature(ConsoleInfo *info);

#endif
