#define NEWLIB_PORT_AWARE
#include <ctype.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "debugprintf.h"
#include "egsm_api.h"
#include "egsm_parse.h"

static int egsm_starts_with_ci(const char *value, const char *prefix)
{
    while (*prefix != '\0') {
        if (*value == '\0')
            return 0;
        if (tolower((unsigned char)*value) != tolower((unsigned char)*prefix))
            return 0;
        value++;
        prefix++;
    }

    return 1;
}

uint32_t parse_egsm_flags_common(const char *gsm_arg)
{
    uint32_t flags = 0;
    const char *p = gsm_arg;
    int fd;
    int nread;
    char romver[4] = {0};

    if (p == NULL || *p == '\0')
        return 0;

    if (egsm_starts_with_ci(p, "fp")) {
        switch (p[2]) {
            case '1':
                flags |= EGSM_FLAG_VMODE_FP1;
                break;
            case '2':
                flags |= EGSM_FLAG_VMODE_FP2;
                break;
            default:
                return 0;
        }
        p += 3;
    } else if (egsm_starts_with_ci(p, "1080ix")) {
        switch (p[6]) {
            case '1':
                flags |= EGSM_FLAG_VMODE_1080I_X1;
                break;
            case '2':
                flags |= EGSM_FLAG_VMODE_1080I_X2;
                break;
            case '3':
                flags |= EGSM_FLAG_VMODE_1080I_X3;
                break;
            default:
                return 0;
        }
        p += 7;
    } else if (egsm_starts_with_ci(p, "1080x")) {
        switch (p[5]) {
            case '1':
                flags |= EGSM_FLAG_VMODE_1080I_X1;
                break;
            case '2':
                flags |= EGSM_FLAG_VMODE_1080I_X2;
                break;
            case '3':
                flags |= EGSM_FLAG_VMODE_1080I_X3;
                break;
            default:
                return 0;
        }
        p += 6;
    } else {
        return 0;
    }

    if (*p == ':') {
        p++;
        switch (*p) {
            case '1':
                flags |= EGSM_FLAG_COMP_1;
                break;
            case '2':
                flags |= EGSM_FLAG_COMP_2;
                break;
            case '3':
                flags |= EGSM_FLAG_COMP_3;
                break;
            default:
                break;
        }
    }

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
