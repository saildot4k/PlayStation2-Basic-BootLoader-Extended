#define NEWLIB_PORT_AWARE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <tamtypes.h>
#include <kernel.h>
#include <elf-loader.h>
#include <ctype.h>
#include "debugprintf.h"
#include "game_id.h"
#include "egsm_parse.h"
#if defined(HDD) && defined(FILEXIO)
#include <fileXio_rpc.h>
#include <hdd-ioctl.h>
#endif
#define MAX_PATH 1025
#ifdef DEBUG
#define DBGWAIT(T) sleep(T)
#else
#define DBGWAIT(T)
#endif

enum {
    DEV9_DEFAULT = 0,
    DEV9_NIC,
    DEV9_NICHDD
};

static int arg_eq_ci(const char *a, const char *b)
{
    unsigned char ca, cb;
    if (a == NULL || b == NULL)
        return 0;
    while (*a != '\0' && *b != '\0') {
        ca = (unsigned char)*a;
        cb = (unsigned char)*b;
        if (ca >= 'a' && ca <= 'z')
            ca -= ('a' - 'A');
        if (cb >= 'a' && cb <= 'z')
            cb -= ('a' - 'A');
        if (ca != cb)
            return 0;
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0');
}

static int arg_prefix_ci(const char *arg, const char *prefix, const char **value_out)
{
    size_t i;

    if (arg == NULL || prefix == NULL)
        return 0;

    for (i = 0; prefix[i] != '\0'; i++) {
        unsigned char a = (unsigned char)arg[i];
        unsigned char b = (unsigned char)prefix[i];

        if (a == '\0')
            return 0;
        if (a >= 'a' && a <= 'z')
            a -= ('a' - 'A');
        if (b >= 'a' && b <= 'z')
            b -= ('a' - 'A');
        if (a != b)
            return 0;
    }

    if (value_out != NULL)
        *value_out = arg + i;

    return 1;
}

static int path_has_patinfo_token(const char *path)
{
    static const char token[] = ":PATINFO";
    size_t token_len = sizeof(token) - 1;
    const char *p;

    if (path == NULL)
        return 0;

    for (p = path; *p != '\0'; p++) {
        size_t k;
        for (k = 0; k < token_len && p[k] != '\0'; k++) {
            unsigned char a = (unsigned char)p[k];
            unsigned char b = (unsigned char)token[k];

            if (a >= 'a' && a <= 'z')
                a -= ('a' - 'A');
            if (b >= 'a' && b <= 'z')
                b -= ('a' - 'A');
            if (a != b)
                break;
        }
        if (k == token_len)
            return 1;
    }

    return 0;
}

static int path_is_rom_binary(const char *path)
{
    if (path == NULL)
        return 0;

    return ((path[0] == 'r' || path[0] == 'R') &&
            (path[1] == 'o' || path[1] == 'O') &&
            (path[2] == 'm' || path[2] == 'M') &&
            (path[3] >= '0' && path[3] <= '9') &&
            (path[4] == ':'));
}

static void apply_dev9_policy(int dev9_mode)
{
#if defined(HDD) && defined(FILEXIO)
    if (dev9_mode == 1) { // NIC
        fileXioUmount("pfs0:");
        fileXioDevctl("hdd0:", HDIOC_IDLEIMM, NULL, 0, NULL, 0);
        fileXioDevctl("hdd1:", HDIOC_IDLEIMM, NULL, 0, NULL, 0);
    }
#else
    (void)dev9_mode;
#endif
}

#if EGSM_BUILD
extern unsigned char ps2_stage2_loader_elf[];
extern unsigned int size_ps2_stage2_loader_elf;

#define ELF_PT_LOAD 1

typedef struct
{
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint32_t entry;
    uint32_t phoff;
    uint32_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} embedded_elf_header_t;

typedef struct
{
    uint32_t type;
    uint32_t offset;
    void *vaddr;
    uint32_t paddr;
    uint32_t filesz;
    uint32_t memsz;
    uint32_t flags;
    uint32_t align;
} embedded_elf_pheader_t;

static int path_is_pfs_prefix(const char *path)
{
    return (path != NULL &&
            (path[0] == 'p' || path[0] == 'P') &&
            (path[1] == 'f' || path[1] == 'F') &&
            (path[2] == 's' || path[2] == 'S') &&
            path[3] == ':');
}

static int ExecEmbeddedStage2(unsigned char *elf, unsigned int elf_size, int argc, char *argv[])
{
    embedded_elf_header_t *eh;
    embedded_elf_pheader_t *eph;
    int i;

    if (elf == NULL || elf_size < sizeof(embedded_elf_header_t))
        return -1;

    eh = (embedded_elf_header_t *)elf;
    if (memcmp(eh->ident, "\x7f""ELF", 4) != 0)
        return -1;
    if ((uint32_t)eh->phoff + ((uint32_t)eh->phnum * (uint32_t)sizeof(embedded_elf_pheader_t)) > elf_size)
        return -1;

    eph = (embedded_elf_pheader_t *)(elf + eh->phoff);
    for (i = 0; i < eh->phnum; i++) {
        void *pdata;

        if (eph[i].type != ELF_PT_LOAD)
            continue;
        if ((uint32_t)eph[i].offset > elf_size || (uint32_t)eph[i].filesz > (elf_size - (uint32_t)eph[i].offset))
            return -1;

        pdata = (void *)(elf + eph[i].offset);
        memcpy(eph[i].vaddr, pdata, eph[i].filesz);
        if (eph[i].memsz > eph[i].filesz)
            memset((uint8_t *)eph[i].vaddr + eph[i].filesz, 0, eph[i].memsz - eph[i].filesz);
    }

    FlushCache(0);
    FlushCache(2);
    ExecPS2((void *)eh->entry, NULL, argc, argv);
    return 0;
}

static int RunLoaderElfViaStage2(const char *launch_filename, const char *party, int argc, char *argv[], const char *gsm_arg, int dev9_mode)
{
    char full_launch_path[MAX_PATH];
    char loader_args[16] = "-la=G";
    char **stage2_argv;
    const char *stage2_launch = launch_filename;
    int i;
    int stage2_argc;

    if (launch_filename == NULL || *launch_filename == '\0' || gsm_arg == NULL || *gsm_arg == '\0')
        return -1;
    if (size_ps2_stage2_loader_elf < sizeof(embedded_elf_header_t))
        return -1;

    if (party != NULL && *party != '\0') {
        if (path_is_pfs_prefix(launch_filename)) {
            snprintf(full_launch_path, sizeof(full_launch_path), "%s%s", party, launch_filename);
            stage2_launch = full_launch_path;
        } else if (strchr(launch_filename, ':') == NULL) {
            snprintf(full_launch_path, sizeof(full_launch_path), "%spfs:%s%s",
                     party,
                     (launch_filename[0] == '/') ? "" : "/",
                     launch_filename);
            stage2_launch = full_launch_path;
        }
    }

    i = (int)strlen(loader_args);
    if (dev9_mode == DEV9_NIC) {
        loader_args[i++] = 'N';
    } else if (dev9_mode == DEV9_NICHDD) {
        loader_args[i++] = 'D';
    }
    loader_args[i] = '\0';

    stage2_argc = argc + 3;
    stage2_argv = malloc(sizeof(char *) * stage2_argc);
    if (stage2_argv == NULL)
        return -1;

    stage2_argv[0] = (char *)stage2_launch;
    for (i = 0; i < argc; i++)
        stage2_argv[i + 1] = argv[i];
    stage2_argv[argc + 1] = (char *)gsm_arg;
    stage2_argv[argc + 2] = loader_args;

    if (ExecEmbeddedStage2(ps2_stage2_loader_elf, size_ps2_stage2_loader_elf, stage2_argc, stage2_argv) != 0) {
        free(stage2_argv);
        return -1;
    }

    free(stage2_argv);
    return 0;
}
#endif

void RunLoaderElf(const char *filename, const char *party, int argc, char *argv[])
{
    DPRINTF("%s\n", __FUNCTION__);
    const char *launch_filename = filename;
    char patinfo_path[MAX_PATH];
    char *title_override = NULL;
    int force_appid = 0;
    int use_patinfo_path = 0;
    int dev9_mode = DEV9_DEFAULT;
    uint32_t gsm_flags = 0;
    const char *gsm_arg = NULL;

    if (argc > 0 && argv != NULL) {
        int out_argc = 0;
        int i;
        for (i = 0; i < argc; i++) {
            if (argv[i] != NULL && (argv[i][0] == '-' || argv[i][0] == '+')) {
                if (arg_eq_ci(argv[i], "-appid")) {
                    force_appid = 1;
                    continue;
                }
                if (arg_eq_ci(argv[i], "-patinfo")) {
                    use_patinfo_path = 1;
                    continue;
                }
                {
                    const char *val = NULL;
                    if (arg_prefix_ci(argv[i], "-titleid=", &val)) {
                        while (*val == ' ' || *val == '\t')
                            val++;
                        if (*val != '\0' && title_override == NULL) {
                            int tlen = 0;
                            title_override = malloc(12);
                            if (title_override) {
                                while (tlen < 11 && val[tlen] != '\0' && !isspace((unsigned char)val[tlen])) {
                                    title_override[tlen] = val[tlen];
                                    tlen++;
                                }
                                title_override[tlen] = '\0';
                            }
                        }
                        continue;
                    }
                    if (arg_prefix_ci(argv[i], "-gsm=", &val)) {
                        while (*val == ' ' || *val == '\t')
                            val++;
                        gsm_flags = parse_egsm_flags_common(val);
                        if (gsm_flags == 0)
                            DPRINTF("Ignoring invalid -gsm value: '%s'\n", val);
                        else {
                            DPRINTF("Parsed -gsm value '%s' as flags 0x%08x\n", val, (unsigned int)gsm_flags);
                            gsm_arg = val;
                        }
                        continue;
                    }
                    if (arg_prefix_ci(argv[i], "-dev9=", &val)) {
                        while (*val == ' ' || *val == '\t')
                            val++;
                        if (arg_eq_ci(val, "NIC")) {
                            dev9_mode = DEV9_NIC;
                        } else if (arg_eq_ci(val, "NICHDD")) {
                            dev9_mode = DEV9_NICHDD;
                        } else {
                            DPRINTF("Ignoring unknown -dev9 value: '%s'\n", val);
                        }
                        continue;
                    }
                }
            }
            argv[out_argc++] = argv[i];
        }
        argc = out_argc;
    }

    if (use_patinfo_path && path_has_patinfo_token(filename)) {
        if (argc > 0 && argv[0] != NULL && argv[0][0] != '\0') {
            launch_filename = argv[0];
            if (party != NULL && strchr(launch_filename, ':') == NULL) {
                snprintf(patinfo_path, sizeof(patinfo_path), "pfs:%s%s",
                         (launch_filename[0] == '/') ? "" : "/",
                         launch_filename);
                launch_filename = patinfo_path;
            }

            argc--;
            if (argc > 0)
                memmove(argv, &argv[1], sizeof(char *) * argc);
        } else {
            DPRINTF("Ignoring -patinfo for '%s': missing target ELF path argument\n", filename);
        }
    }

    int show_app_id = GameIDAppEnabled();
    if (force_appid || title_override)
        show_app_id = 1;

    if (show_app_id) {
        char *title_id = title_override ? title_override : generateTitleID(launch_filename);
        if (title_id) {
            GameIDHandleApp(title_id, show_app_id);
            if (!title_override)
                free(title_id);
        }
    }

    if (title_override)
        free(title_override);

    if (gsm_flags != 0) {
        if (path_is_rom_binary(launch_filename)) {
            DPRINTF("Ignoring -gsm for ROM path '%s'\n", launch_filename);
        } else {
#if EGSM_BUILD
            if (RunLoaderElfViaStage2(launch_filename, party, argc, argv, gsm_arg, dev9_mode) == 0)
                return;
            DPRINTF("Unable to hand off -gsm launch to embedded stage2; continuing without eGSM\n");
#else
            DPRINTF("Ignoring -gsm (eGSM build disabled): flags 0x%08x\n", (unsigned int)gsm_flags);
#endif
        }
    }

    if (dev9_mode == DEV9_NIC) {
        DPRINTF("Applying -dev9=NIC (idle HDD, keep DEV9 on)\n");
    } else if (dev9_mode == DEV9_NICHDD) {
        DPRINTF("Applying -dev9=NICHDD (keep HDD and DEV9 on)\n");
    }
    apply_dev9_policy(dev9_mode);

    if (party == NULL) {
        DPRINTF("LoadELFFromFile(%s, %d, %p)\n", launch_filename, argc, argv);
        DBGWAIT(2);
        LoadELFFromFile(launch_filename, argc, argv);
    } else {
        DPRINTF("LoadELFFromFileWithPartition(%s, %s, %d, %p);\n", launch_filename, party, argc, argv);
        DBGWAIT(2);
        LoadELFFromFileWithPartition(launch_filename, party, argc, argv);
    }
}
