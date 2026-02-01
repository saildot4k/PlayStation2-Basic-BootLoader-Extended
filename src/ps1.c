#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <kernel.h>
#include <sifcmd.h>
#include <libmc.h>
#include <libcdvd.h>
#include "libcdvd_add.h"
#include <unistd.h>

#include "ps1.h"
#include "OSDInit.h"
#include "OSDConfig.h"
#include "OSDHistory.h"
#include "game_id.h"
#ifndef EMBED_PS1VN
#include "util.h"
extern char *CheckPath(char *path);
#endif

#define PS1DRV_CFG_FAST 0x01
#define PS1DRV_CFG_SMOOTH 0x10

void CleanUp(void);

#ifdef EMBED_PS1VN
extern unsigned char ps1vn_elf[];
extern unsigned int size_ps1vn_elf;

#define ELF_MAGIC 0x464c457f
#define ELF_PT_LOAD 1

typedef struct {
    u8 ident[16];
    u16 type;
    u16 machine;
    u32 version;
    u32 entry;
    u32 phoff;
    u32 shoff;
    u32 flags;
    u16 ehsize;
    u16 phentsize;
    u16 phnum;
    u16 shentsize;
    u16 shnum;
    u16 shstrndx;
} elf_header_t;

typedef struct {
    u32 type;
    u32 offset;
    void *vaddr;
    u32 paddr;
    u32 filesz;
    u32 memsz;
    u32 flags;
    u32 align;
} elf_pheader_t;

static int load_elf_from_buffer(const void *elf, void **entry_out)
{
    const elf_header_t *eh;
    const elf_pheader_t *ph;
    int i;

    if (elf == NULL || entry_out == NULL || size_ps1vn_elf < sizeof(elf_header_t))
        return -1;

    eh = (const elf_header_t *)elf;
    if (*(const u32 *)eh->ident != ELF_MAGIC)
        return -1;

    ph = (const elf_pheader_t *)((const u8 *)elf + eh->phoff);
    for (i = 0; i < eh->phnum; i++) {
        if (ph[i].type != ELF_PT_LOAD)
            continue;
        memcpy(ph[i].vaddr, (const u8 *)elf + ph[i].offset, ph[i].filesz);
        if (ph[i].memsz > ph[i].filesz) {
            memset((u8 *)ph[i].vaddr + ph[i].filesz, 0, ph[i].memsz - ph[i].filesz);
        }
    }

    FlushCache(0);
    FlushCache(2);

    *entry_out = (void *)eh->entry;
    return 0;
}
#endif

struct PS1DRV
{
    char ver[32];
    int major;
    int minor;
    int region;
};

static struct PS1DRV ps1drv = {"", 0, 0, 0};
static char ps1ver_uni[] = "rom0:PS1VER?";
static char ps1drv_boot[64] = "";
static char ps1drv_ver[64] = "";
static int g_ps1drv_enable_fast = 0;
static int g_ps1drv_enable_smooth = 0;
static int g_ps1drv_use_ps1vn = 0;

void PS1DRVSetOptions(int enable_fast, int enable_smooth, int use_ps1vn)
{
    g_ps1drv_enable_fast = (enable_fast != 0);
    g_ps1drv_enable_smooth = (enable_smooth != 0);
    g_ps1drv_use_ps1vn = (use_ps1vn != 0);
}

#ifndef EMBED_PS1VN
static const char *find_ps1vn_path(void)
{
    static const char *candidates[] = {
        "mc?:/BOOT/PS1VN.ELF",
        "mc?:/PS2BBL/PS1VN.ELF",
        "mass:/PS2BBL/PS1VN.ELF",
        "massX:/PS2BBL/PS1VN.ELF",
        "mmce?:/PS2BBL/PS1VN.ELF",
        "hdd0:__sysconf:pfs:/BOOT/PS1VN.ELF",
        NULL
    };
    static char path_buf[128];
    int i;

    for (i = 0; candidates[i] != NULL; i++) {
        strncpy(path_buf, candidates[i], sizeof(path_buf) - 1);
        path_buf[sizeof(path_buf) - 1] = '\0';
        char *resolved = CheckPath(path_buf);
        if (resolved != NULL && exist(resolved))
            return resolved;
    }

    return NULL;
}
#endif

int PS1DRVInit(void)
{
    const char *pChar;
    int fd, result;

    ps1drv.ver[0] = '\0';

    fd = open("rom0:PS1ID", O_RDONLY);

    if (fd < 0)
        return -1;

    read(fd, ps1drv.ver, sizeof(ps1drv.ver));
    close(fd);

    pChar = ps1drv.ver;
    ps1drv.major = atoi(pChar);

    for (pChar = ps1drv.ver; *pChar >= '0' && *pChar <= '9'; pChar++) {
    };

    if (*pChar == '.') {
        ps1drv.minor = atoi(pChar + 1);

        for (; *pChar >= '0' && *pChar <= '9'; pChar++) {
        };

        result = 0;
        ps1drv.region = *pChar;
    } else { // Missing dot
        ps1drv.region = 0;
        ps1drv.major = 0;
        ps1drv.minor = 0;
        result = -1;
    }

    if (result == 0) {
        if (OSDGetPS1DRVRegion(&ps1ver_uni[11]) == 0 || (fd = open(ps1ver_uni, O_RDONLY)) < 0)
            fd = open("rom0:PS1VER", O_RDONLY);

        if (fd >= 0) {
            result = read(fd, ps1drv.ver, sizeof(ps1drv.ver) - 1);
            close(fd);

            // NULL-terminate, only if non-error
            ps1drv.ver[result >= 0 ? result : 0] = '\0';
        }
    }

    // The original always returned -1. But we'll return something more meaningful.
    return result;
}

const char *PS1DRVGetVersion(void)
{
    if (ps1drv.ver[0] == '\0') { // rom0:PS1VER is missing (not present in the earliest ROMs)
        return (OSDGetConsoleRegion() == CONSOLE_REGION_JAPAN ? "1.01" : "1.10");
    }

    return ps1drv.ver;
}

static void CNFGetKey(unsigned char *cnf, char *line, const char *key)
{
    int len;

    if ((len = strlen(key)) != 0) {
        do {
            if (strncmp((const char *)cnf, key, len) == 0) {           // Key located.
                for (cnf += len; isspace(*cnf); cnf++) { // Eliminate leading whitespaces.
                    if (*cnf == '\n' || *cnf == '\0')
                        return;
                }

                if (*cnf == '=') { // Equals sign found
                    const char *pKey;
                    ++cnf;

                    for (; isspace(*cnf); cnf++) { // Eliminate whitespaces after the equals sign.
                        if (*cnf == '\n' || *cnf == '\0')
                            return;
                    }

                    pKey = (const char *)cnf;

                    // Carry on, until the end of the line.
                    for (;; cnf++) { // NULL-terminate at the newline character.
                        if (*cnf == '\n' || *cnf == '\r') {
                            *cnf = '\0';
                            break;
                        }
                    }

                    strcpy(line, pKey);
                }

                return;
            }

            for (; *cnf != '\n'; cnf++) { // Skip uptil the newline character
                if (*cnf == '\0')         // Parse error
                    return;
            }
            ++cnf; // Skip the newline character.
        } while (*cnf != '\0');
    }
}

#define CNF_PATH_LEN_MAX 64

static int ParseBootCNF(void)
{
    u32 stat;
    int fd;

    strcpy(ps1drv_ver, "???");
    strcpy(ps1drv_boot, "???");

    if (OSDGetConsoleRegion() == CONSOLE_REGION_CHINA) { // China
        /*  I do not know why Sony did this for the Chinese console (SCPH-50009).
            Perhaps it as an act to strengthen their DRM for that release,
            since PlayStation 2 game booting is also slightly different.

            It is normally possible to actually parse SYSTEM.CNF and get the boot filename from BOOT.
            Lots of homebrew software do that, and so does Sony (for all other regions).
            But I do not know for sure whether that can be done for Chinese PlayStation discs. */
        custom_sceCdReadPS1BootParam(ps1drv_boot, &stat);

        if (stat & 0x180) { // Command unsupported or failed for some reason.
            return 0;
        }

        if (ps1drv_boot[4] == '-')
            ps1drv_boot[4] = '_';

        ps1drv_boot[11] = '\0';
        ps1drv_boot[10] = ps1drv_boot[9];
        ps1drv_boot[9] = ps1drv_boot[8];
        ps1drv_boot[8] = '.';

        return 1;
    }

    if ((fd = open("cdrom0:\\SYSTEM.CNF;1", O_RDWR)) >= 0) {
        char system_cnf[2048], line[128];
        const char *pChar;
        int size, i, len;

        size = read(fd, system_cnf, sizeof(system_cnf));
        close(fd);

        system_cnf[size] = '\0';
        line[0] = '\0';

        // Parse SYSTEM.CNF
        CNFGetKey((unsigned char *)system_cnf, line, "BOOT");
        pChar = line;

        // Locate the end of the line.
        for (i = 0; *pChar != ';' && *pChar != '\n' && *pChar != '\0'; pChar++, i++) {
            if (i + 1 == CNF_PATH_LEN_MAX + 1) {
                strcpy(ps1drv_boot, "???");
                break;
            }
        }

        // Now we have reached the end of the line. Go back to the start of the filename and count the length of the filename.
        for (i = 0, len = 0; *pChar != '\\' && *pChar != ':'; --pChar, i++, len++) {
            if (i + 1 == CNF_PATH_LEN_MAX + 1) {
                strcpy(ps1drv_boot, "???");
                break;
            }
        }

        // Copy the filename
        --len;
        strncpy(ps1drv_boot, pChar + 1, len);
        ps1drv_boot[len] = '\0';

        // Get the version number
        ps1drv_ver[0] = '\0';
        CNFGetKey((unsigned char *)ps1drv_boot, ps1drv_ver, "VER");
        if (ps1drv_ver[0] == '\0')
            strcpy(ps1drv_ver, "???");

        return 1;
    } else { // Odd PlayStation title
        if ((fd = open("cdrom0:\\PSXMYST\\MYST.CCS;1", O_RDWR)) >= 0) {
            close(fd);
            strcpy(ps1drv_boot, "SLPS_000.24");
            return 1;
        } else if ((fd = open("cdrom0:\\CDROM\\LASTPHOT\\ALL_C.NBN;1", O_RDWR)) >= 0) {
            close(fd);
            strcpy(ps1drv_boot, "SLPS_000.65");
            return 1;
        } else if ((fd = open("cdrom0:\\PSX.EXE;1", O_RDWR)) >= 0) { // Generic PlayStation game, with no ID information
            close(fd);
            strcpy(ps1drv_boot, "???");
            return 1;
        } else
            return 0;
    }
}

int PS1DRVBoot(void)
{
    char *args[2];
#ifndef EMBED_PS1VN
    const char *ps1vn_exec = NULL;
#endif

    if (ParseBootCNF() == 0)
        return 4;

    if (!strncmp(ps1drv_boot, "???", 3)) {
        const char *gid = getPS1GenericTitleID();
        if (gid) {
            strncpy(ps1drv_boot, gid, sizeof(ps1drv_boot) - 1);
            ps1drv_boot[sizeof(ps1drv_boot) - 1] = '\0';
        }
    }

    args[0] = ps1drv_boot;
    args[1] = ps1drv_ver;

#ifndef EMBED_PS1VN
    if (g_ps1drv_use_ps1vn)
        ps1vn_exec = find_ps1vn_path();
#endif

    GameIDHandleDisc(ps1drv_boot, GameIDDiscEnabled());

    if (g_ps1drv_enable_fast || g_ps1drv_enable_smooth) {
        int cfg = OSDConfigGetPSConfig();
        if (g_ps1drv_enable_fast)
            cfg |= PS1DRV_CFG_FAST;
        if (g_ps1drv_enable_smooth)
            cfg |= PS1DRV_CFG_SMOOTH;
        OSDConfigSetPSConfig(cfg);
        OSDConfigApply();
    }
    CleanUp();

    SifExitCmd();

#ifdef EMBED_PS1VN
    if (g_ps1drv_use_ps1vn) {
        void *entry = NULL;
        if (load_elf_from_buffer(ps1vn_elf, &entry) == 0 && entry != NULL) {
            ExecPS2(entry, NULL, 2, args);
            return 0;
        }
    }
#else
    if (ps1vn_exec != NULL) {
        LoadExecPS2(ps1vn_exec, 2, args);
        return 0;
    }
#endif

    LoadExecPS2("rom0:PS1DRV", 2, args);

    return 0;
}
