// ELF/KELF launch pipeline, argv construction, and optional stage2 handoff.
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

typedef struct
{
    const char *launch_filename;
    char *title_override;
    int force_appid;
    int use_patinfo_path;
    int dev9_mode;
    uint32_t gsm_flags;
    const char *gsm_arg;
} LaunchIntent;

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

static void LaunchIntentInit(LaunchIntent *intent, const char *default_launch)
{
    if (intent == NULL)
        return;

    intent->launch_filename = default_launch;
    intent->title_override = NULL;
    intent->force_appid = 0;
    intent->use_patinfo_path = 0;
    intent->dev9_mode = DEV9_DEFAULT;
    intent->gsm_flags = 0;
    intent->gsm_arg = NULL;
}

static void LaunchIntentRelease(LaunchIntent *intent)
{
    if (intent == NULL)
        return;

    if (intent->title_override != NULL) {
        free(intent->title_override);
        intent->title_override = NULL;
    }
}

static void ParseTitleOverrideValue(LaunchIntent *intent, const char *value)
{
    int tlen = 0;

    if (intent == NULL || value == NULL || *value == '\0')
        return;
    if (intent->title_override != NULL)
        return;

    intent->title_override = malloc(12);
    if (intent->title_override == NULL)
        return;

    while (tlen < 11 && value[tlen] != '\0' && !isspace((unsigned char)value[tlen])) {
        intent->title_override[tlen] = value[tlen];
        tlen++;
    }
    intent->title_override[tlen] = '\0';
}

// Parse loader-control arguments and remove them from app argv.
static int ParseLaunchControlArgs(LaunchIntent *intent, int argc, char *argv[])
{
    int out_argc = 0;
    int i;

    if (intent == NULL || argc <= 0 || argv == NULL)
        return argc;

    for (i = 0; i < argc; i++) {
        const char *val = NULL;

        if (argv[i] == NULL) {
            argv[out_argc++] = argv[i];
            continue;
        }

        if (argv[i][0] != '-' && argv[i][0] != '+') {
            argv[out_argc++] = argv[i];
            continue;
        }

        if (arg_eq_ci(argv[i], "-appid")) {
            intent->force_appid = 1;
            continue;
        }
        if (arg_eq_ci(argv[i], "-patinfo")) {
            intent->use_patinfo_path = 1;
            continue;
        }
        if (arg_prefix_ci(argv[i], "-titleid=", &val)) {
            while (*val == ' ' || *val == '\t')
                val++;
            ParseTitleOverrideValue(intent, val);
            continue;
        }
        if (arg_prefix_ci(argv[i], "-gsm=", &val)) {
            uint32_t parsed_flags;

            while (*val == ' ' || *val == '\t')
                val++;
            parsed_flags = parse_egsm_flags_common(val);
            if (parsed_flags == 0)
                DPRINTF("Ignoring invalid -gsm value: '%s'\n", val);
            else {
                intent->gsm_flags = parsed_flags;
                DPRINTF("Parsed -gsm value '%s' as flags 0x%08x\n", val, (unsigned int)intent->gsm_flags);
                intent->gsm_arg = val;
            }
            continue;
        }
        if (arg_prefix_ci(argv[i], "-dev9=", &val)) {
            while (*val == ' ' || *val == '\t')
                val++;
            if (arg_eq_ci(val, "NIC")) {
                intent->dev9_mode = DEV9_NIC;
            } else if (arg_eq_ci(val, "NICHDD")) {
                intent->dev9_mode = DEV9_NICHDD;
            } else {
                DPRINTF("Ignoring unknown -dev9 value: '%s'\n", val);
            }
            continue;
        }
        if (arg_prefix_ci(argv[i], "-la=", &val)) {
            (void)val;
            DPRINTF("Ignoring reserved internal loader flag override '%s'\n", argv[i]);
            continue;
        }

        argv[out_argc++] = argv[i];
    }

    return out_argc;
}

// Apply -patinfo target override from the first remaining app arg when path includes :PATINFO.
static void ApplyPatinfoLaunchOverride(LaunchIntent *intent,
                                       const char *original_filename,
                                       const char *party,
                                       int *argc_inout,
                                       char *argv[],
                                       char *patinfo_path,
                                       size_t patinfo_path_size)
{
    int argc;

    if (intent == NULL || argc_inout == NULL || argv == NULL || patinfo_path == NULL || patinfo_path_size == 0)
        return;

    if (!intent->use_patinfo_path || !path_has_patinfo_token(original_filename))
        return;

    argc = *argc_inout;
    if (argc > 0 && argv[0] != NULL && argv[0][0] != '\0') {
        intent->launch_filename = argv[0];
        if (party != NULL && strchr(intent->launch_filename, ':') == NULL) {
            snprintf(patinfo_path, patinfo_path_size, "pfs:%s%s",
                     (intent->launch_filename[0] == '/') ? "" : "/",
                     intent->launch_filename);
            intent->launch_filename = patinfo_path;
        }

        argc--;
        if (argc > 0)
            memmove(argv, &argv[1], sizeof(char *) * argc);
        *argc_inout = argc;
    } else {
        DPRINTF("Ignoring -patinfo for '%s': missing target ELF path argument\n", original_filename);
    }
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

static void DebugPrintStage2Argv(const char *label, int argc, char *argv[])
{
    int i;

    DPRINTF("%s: stage2 handoff argc=%d\n", label, argc);
    for (i = 0; i < argc; i++)
        DPRINTF("%s: stage2 argv[%d] = %s\n",
                label,
                i,
                (argv[i] != NULL) ? argv[i] : "<null>");
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
    char loader_args[16] = "-la=G";
    char **stage2_argv;
    char *owned_launch = NULL;
    char *owned_gsm = NULL;
    const char *stage2_launch = launch_filename;
    int stage2_argc;
    int i;

    if (launch_filename == NULL || *launch_filename == '\0' || gsm_arg == NULL || *gsm_arg == '\0')
        return -1;
    if (size_ps2_stage2_loader_elf < sizeof(embedded_elf_header_t))
        return -1;

    if (party != NULL && *party != '\0') {
        if (path_is_pfs_prefix(launch_filename)) {
            owned_launch = malloc(MAX_PATH);
            if (owned_launch == NULL)
                return -1;
            snprintf(owned_launch, MAX_PATH, "%s%s", party, launch_filename);
            stage2_launch = owned_launch;
        } else if (strchr(launch_filename, ':') == NULL) {
            owned_launch = malloc(MAX_PATH);
            if (owned_launch == NULL)
                return -1;
            snprintf(owned_launch, MAX_PATH, "%spfs:%s%s",
                     party,
                     (launch_filename[0] == '/') ? "" : "/",
                     launch_filename);
            stage2_launch = owned_launch;
        }
    }

    i = (int)strlen(loader_args);
    if (dev9_mode == DEV9_NIC) {
        loader_args[i++] = 'N';
    } else if (dev9_mode == DEV9_NICHDD) {
        loader_args[i++] = 'D';
    }
    loader_args[i] = '\0';

    owned_gsm = malloc(strlen(gsm_arg) + 1);
    if (owned_gsm == NULL) {
        if (owned_launch != NULL)
            free(owned_launch);
        return -1;
    }
    strcpy(owned_gsm, gsm_arg);

    stage2_argc = argc + 3;
    stage2_argv = malloc(sizeof(char *) * stage2_argc);
    if (stage2_argv == NULL) {
        free(owned_gsm);
        if (owned_launch != NULL)
            free(owned_launch);
        return -1;
    }

    stage2_argv[0] = (char *)stage2_launch;
    for (i = 0; i < argc; i++)
        stage2_argv[i + 1] = argv[i];
    stage2_argv[argc + 1] = owned_gsm;
    stage2_argv[argc + 2] = loader_args;
    DebugPrintStage2Argv(__func__, stage2_argc, stage2_argv);

    if (ExecEmbeddedStage2(ps2_stage2_loader_elf, size_ps2_stage2_loader_elf, stage2_argc, stage2_argv) != 0) {
        free(stage2_argv);
        free(owned_gsm);
        if (owned_launch != NULL)
            free(owned_launch);
        return -1;
    }

    free(stage2_argv);
    free(owned_gsm);
    if (owned_launch != NULL)
        free(owned_launch);
    return 0;
}
#endif

void RunLoaderElf(const char *filename, const char *party, int argc, char *argv[])
{
    DPRINTF("%s\n", __FUNCTION__);
    char patinfo_path[MAX_PATH];
    LaunchIntent intent;
    int show_app_id;

    LaunchIntentInit(&intent, filename);
    patinfo_path[0] = '\0';

    argc = ParseLaunchControlArgs(&intent, argc, argv);
    ApplyPatinfoLaunchOverride(&intent,
                               filename,
                               party,
                               &argc,
                               argv,
                               patinfo_path,
                               sizeof(patinfo_path));

    show_app_id = GameIDAppEnabled();
    if (intent.force_appid || intent.title_override != NULL)
        show_app_id = 1;

    if (show_app_id) {
        char *title_id = (intent.title_override != NULL) ? intent.title_override : generateTitleID(intent.launch_filename);
        if (title_id) {
            GameIDHandleApp(title_id, show_app_id);
            if (intent.title_override == NULL)
                free(title_id);
        }
    }

    if (intent.gsm_flags != 0) {
        if (path_is_rom_binary(intent.launch_filename)) {
            DPRINTF("Ignoring -gsm for ROM path '%s'\n", intent.launch_filename);
        } else {
#if EGSM_BUILD
            if (RunLoaderElfViaStage2(intent.launch_filename, party, argc, argv, intent.gsm_arg, intent.dev9_mode) == 0) {
                LaunchIntentRelease(&intent);
                return;
            }
            DPRINTF("Unable to hand off -gsm launch to embedded stage2; continuing without eGSM\n");
#else
            DPRINTF("Ignoring -gsm (eGSM build disabled): flags 0x%08x\n", (unsigned int)intent.gsm_flags);
#endif
        }
    }

    if (intent.dev9_mode == DEV9_NIC) {
        DPRINTF("Applying -dev9=NIC (idle HDD, keep DEV9 on)\n");
    } else if (intent.dev9_mode == DEV9_NICHDD) {
        DPRINTF("Applying -dev9=NICHDD (keep HDD and DEV9 on)\n");
    }
    apply_dev9_policy(intent.dev9_mode);

    if (party == NULL) {
        DPRINTF("LoadELFFromFile(%s, %d, %p)\n", intent.launch_filename, argc, argv);
        DBGWAIT(2);
        LoadELFFromFile(intent.launch_filename, argc, argv);
    } else {
        DPRINTF("LoadELFFromFileWithPartition(%s, %s, %d, %p);\n", intent.launch_filename, party, argc, argv);
        DBGWAIT(2);
        LoadELFFromFileWithPartition(intent.launch_filename, party, argc, argv);
    }

    LaunchIntentRelease(&intent);
}
