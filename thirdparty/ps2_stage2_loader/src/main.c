#include <fcntl.h>
#include <iopcontrol.h>
#include <kernel.h>
#include <libcdvd.h>
#include <loadfile.h>
#include <ps2sdkapi.h>
#include <sifrpc.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "egsm_api.h"

static uint8_t g_is_pal = 0;

static int getDiscRegion(void)
{
    return g_is_pal ? 2 : 0;
}

static int parseGSMFlags(const char *gsm_arg)
{
    uint32_t flags = 0;
    const char *p = gsm_arg;
    int fd;
    int nread;
    char romver[4] = {0};

    if (p == NULL || *p == '\0')
        return 0;

    if (!strncmp(p, "fp", 2)) {
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
    } else if (!strncmp(p, "1080ix", 6)) {
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
            romver[1] < '2' || (romver[1] == '2' && romver[2] < '2')) {
            flags |= EGSM_FLAG_NO_576P;
        }
    }

    return flags;
}

static int patchWithOffsets(uint32_t get_region_loc, uint32_t checksum_loc)
{
    if ((_lw(get_region_loc) != 0x27bdfff0) || ((_lw(get_region_loc + 8) & 0xff000000) != 0x0c000000))
        return -1;
    if ((_lw(checksum_loc) & 0xffff0000) != 0x8f820000)
        return -1;

    _sw((0x0c000000 | ((uint32_t)getDiscRegion >> 2)), get_region_loc + 8);
    _sw(0x24020000, checksum_loc);

    FlushCache(0);
    FlushCache(2);
    return 0;
}

static void patchPS2LOGOUnpacked(void)
{
    if (!patchWithOffsets(0x100178, 0x100278))
        return;
    if (!patchWithOffsets(0x102078, 0x102178))
        return;
    if (!patchWithOffsets(0x102018, 0x102118))
        return;
    if (!patchWithOffsets(0x102018, 0x102264))
        asm volatile("j 0x100000");
}

static void patchedExecPS2(void *entry, void *gp, int argc, char *argv[])
{
    patchPS2LOGOUnpacked();
    ExecPS2(entry, gp, argc, argv);
}

static void patchPS2LOGO(uint32_t epc)
{
    char syscnf[128];
    int fd;
    int nread;
    char *vmode;

    fd = open("cdrom0:\\SYSTEM.CNF;1", O_RDONLY);
    if (fd >= 0) {
        nread = read(fd, syscnf, sizeof(syscnf) - 1);
        close(fd);
        if (nread > 0) {
            syscnf[nread] = '\0';
            vmode = strstr(syscnf, "VMODE");
            if (vmode != NULL && strstr(vmode, "PAL") != NULL)
                g_is_pal = 1;
            else
                g_is_pal = 0;
        }
    }

    if (epc > 0x1000000) {
        if ((_lw(0x1000200) & 0xff000000) == 0x08000000) {
            _sw((0x08000000 | ((uint32_t)patchPS2LOGOUnpacked >> 2)), (uint32_t)0x1000200);
        } else if ((_lw(0x100011c) & 0xff000000) == 0x0c000000) {
            _sw((0x0c000000 | ((uint32_t)patchedExecPS2 >> 2)), (uint32_t)0x100011c);
        }
        return;
    }

    if (epc > 0x200000)
        return;

    patchPS2LOGOUnpacked();
}

static void resetIOP(void)
{
    while (!SifIopReset("", 0)) {
    }
    while (!SifIopSync()) {
    }
    SifInitRpc(0);
}

int main(int argc, char *argv[])
{
    t_ExecData elfdata = {0};
    uint32_t gsm_flags = 0;
    int i;
    int out_argc = 0;
    int ret;
    int is_logo_launch;

    if (argc < 1 || argv[0] == NULL)
        return -1;

    for (i = 0; i < argc; i++) {
        if (argv[i] != NULL && !strncmp(argv[i], "-gsm=", 5)) {
            gsm_flags = (uint32_t)parseGSMFlags(argv[i] + 5);
            continue;
        }
        argv[out_argc++] = argv[i];
    }
    argc = out_argc;
    if (argc < 1 || argv[0] == NULL)
        return -1;

    if (!strncmp(argv[0], "cdrom", 5) || !strcmp(argv[0], "rom0:PS2LOGO"))
        sceCdInit(SCECdINIT);

    SifLoadFileInit();
    ret = SifLoadElf(argv[0], &elfdata);
    if (ret && (ret = SifLoadElfEncrypted(argv[0], &elfdata))) {
        SifLoadFileExit();
        return ret;
    }
    SifLoadFileExit();

    is_logo_launch = !strcmp(argv[0], "rom0:PS2LOGO");
    if (is_logo_launch) {
        patchPS2LOGO(elfdata.epc);
        sceCdInit(SCECdEXIT);
        resetIOP();
    } else if (!strncmp(argv[0], "cdrom", 5)) {
        sceCdInit(SCECdEXIT);
    }

    SifExitRpc();
    SifExitCmd();

    if (gsm_flags != 0)
        enableGSM(gsm_flags);

    return ExecPS2((void *)elfdata.epc, (void *)elfdata.gp, argc, argv);
}
