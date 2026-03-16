#define NEWLIB_PORT_AWARE
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "debugprintf.h"
#include "egsm_api.h"
#include "egsm_parse.h"

uint32_t parse_egsm_flags_common(const char *gsm_arg)
{
    uint32_t flags = 0;
    const char *p = gsm_arg;
    int fd;
    int nread;
    char romver[4] = {0};

    if (p == NULL || *p == '\0')
        return 0;

    if (*p != '\0' && *p != ':') {
        if (strncmp(p, "fp1", 3) == 0) {
            flags |= EGSM_FLAG_VMODE_FP1;
            p += 3;
        } else if (strncmp(p, "fp2", 3) == 0) {
            flags |= EGSM_FLAG_VMODE_FP2;
            p += 3;
        } else if (strncmp(p, "1080ix1", 7) == 0) {
            flags |= EGSM_FLAG_VMODE_1080I_X1;
            p += 7;
        } else if (strncmp(p, "1080ix2", 7) == 0) {
            flags |= EGSM_FLAG_VMODE_1080I_X2;
            p += 7;
        } else if (strncmp(p, "1080ix3", 7) == 0) {
            flags |= EGSM_FLAG_VMODE_1080I_X3;
            p += 7;
        } else {
            return 0;
        }
    }

    if (*p == ':') {
        p++;
        switch (*p) {
            case '\0':
                break;
            case '1':
                flags |= EGSM_FLAG_COMP_1;
                p++;
                break;
            case '2':
                flags |= EGSM_FLAG_COMP_2;
                p++;
                break;
            case '3':
                flags |= EGSM_FLAG_COMP_3;
                p++;
                break;
            default:
                return 0;
        }
    }

    if (*p != '\0')
        return 0;

    if (flags == 0)
        return 0;

    fd = open("rom0:ROMVER", O_RDONLY);
    if (fd < 0) {
        flags |= EGSM_FLAG_NO_576P;
    } else {
        nread = read(fd, romver, sizeof(romver));
        close(fd);
        if (nread < (int)sizeof(romver) ||
            romver[1] < '0' || romver[1] > '9' ||
            romver[2] < '0' || romver[2] > '9' ||
            romver[1] < '2' || (romver[1] == '2' && romver[2] < '2'))
            flags |= EGSM_FLAG_NO_576P;
    }

    DPRINTF("%s: parsed flags=0x%08x from '%s'\n", __func__, flags, gsm_arg);
    return flags;
}
