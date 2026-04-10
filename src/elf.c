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
#ifdef HDD
#include <fileio.h>
#include <io_common.h>
#endif
#define MAX_PATH 1025
#define PATINFO_MAX_CNF 4096
#define PATINFO_ELF_MEM_ADDR 0x01000000
#define PATINFO_IOPRP_MEM_ADDR 0x01F00000
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

static int arg_eq_ci(const char *a, const char *b);

typedef struct
{
    const char *launch_filename;
    char *title_override;
    int force_appid;
    int use_patinfo_path;
    int skip_argv0;
    int dev9_mode;
    uint32_t gsm_flags;
    const char *gsm_arg;
} LaunchIntent;

#ifdef HDD
typedef struct
{
    char *boot_path;
    char *ioprp_path;
    char *title_id;
    char **args;
    int arg_count;
    int arg_capacity;
    int no_history;
    int skip_argv0;
    int dev9_mode;
} PatinfoCnfOptions;

typedef struct
{
    uint32_t offset;
    uint32_t size;
} PatinfoAttrFile;

typedef struct
{
    char magic[9];
    uint8_t unused[3];
    uint32_t version;
    PatinfoAttrFile system_cnf;
    PatinfoAttrFile icon_sys;
    PatinfoAttrFile list_icon;
    PatinfoAttrFile delete_icon;
    PatinfoAttrFile elf;
    PatinfoAttrFile ioprp;
} PatinfoAttrHeader;

static void PatinfoOptionsInit(PatinfoCnfOptions *opts)
{
    if (opts == NULL)
        return;

    opts->boot_path = NULL;
    opts->ioprp_path = NULL;
    opts->title_id = NULL;
    opts->args = NULL;
    opts->arg_count = 0;
    opts->arg_capacity = 0;
    opts->no_history = 0;
    opts->skip_argv0 = 0;
    opts->dev9_mode = DEV9_DEFAULT;
}

static void PatinfoOptionsRelease(PatinfoCnfOptions *opts)
{
    int i;

    if (opts == NULL)
        return;

    if (opts->boot_path != NULL)
        free(opts->boot_path);
    if (opts->ioprp_path != NULL)
        free(opts->ioprp_path);
    if (opts->title_id != NULL)
        free(opts->title_id);

    for (i = 0; i < opts->arg_count; i++) {
        if (opts->args[i] != NULL)
            free(opts->args[i]);
    }

    if (opts->args != NULL)
        free(opts->args);

    PatinfoOptionsInit(opts);
}

static int PatinfoOptionsAppendArg(PatinfoCnfOptions *opts, const char *value)
{
    char *dup;
    char **new_args;
    int new_capacity;

    if (opts == NULL || value == NULL)
        return -1;

    if (opts->arg_count >= opts->arg_capacity) {
        new_capacity = (opts->arg_capacity == 0) ? 8 : (opts->arg_capacity * 2);
        new_args = realloc(opts->args, sizeof(char *) * (size_t)new_capacity);
        if (new_args == NULL)
            return -1;
        opts->args = new_args;
        opts->arg_capacity = new_capacity;
    }

    dup = strdup(value);
    if (dup == NULL)
        return -1;

    opts->args[opts->arg_count] = dup;

    opts->arg_count++;
    return 0;
}

static int is_space_char(char c)
{
    return (c == ' ' || c == '\t' || c == '\r' || c == '\n');
}

static char *trim_span(char *s)
{
    char *e;

    if (s == NULL)
        return NULL;

    while (*s != '\0' && is_space_char(*s))
        s++;

    if (*s == '\0')
        return s;

    e = s + strlen(s) - 1;
    while (e >= s && is_space_char(*e)) {
        *e = '\0';
        e--;
    }

    return s;
}

static int parse_patinfo_cnf_text(char *cnf_text, PatinfoCnfOptions *opts)
{
    char *line;
    char *cursor;

    if (cnf_text == NULL || opts == NULL)
        return -1;

    cursor = cnf_text;
    while (cursor != NULL && *cursor != '\0') {
        char *eq;
        char *name;
        char *value;
        char *next = strchr(cursor, '\n');

        if (next != NULL) {
            *next = '\0';
            line = cursor;
            cursor = next + 1;
        } else {
            line = cursor;
            cursor = NULL;
        }

        line = trim_span(line);
        if (*line == '\0')
            continue;

        eq = strchr(line, '=');
        if (eq == NULL)
            continue;

        *eq = '\0';
        name = trim_span(line);
        value = trim_span(eq + 1);
        if (name == NULL || value == NULL || *name == '\0')
            continue;

        if (arg_eq_ci(name, "BOOT2") || arg_eq_ci(name, "BOOT") || arg_eq_ci(name, "path")) {
            if (opts->boot_path != NULL)
                free(opts->boot_path);
            opts->boot_path = strdup(value);
            continue;
        }

        if (arg_eq_ci(name, "titleid")) {
            if (opts->title_id != NULL)
                free(opts->title_id);
            opts->title_id = strdup(value);
            continue;
        }

        if (arg_eq_ci(name, "IOPRP")) {
            if (opts->ioprp_path != NULL)
                free(opts->ioprp_path);
            opts->ioprp_path = strdup(value);
            continue;
        }

        if (arg_eq_ci(name, "skip_argv0")) {
            opts->skip_argv0 = atoi(value) ? 1 : 0;
            continue;
        }

        if (arg_eq_ci(name, "nohistory")) {
            opts->no_history = atoi(value) ? 1 : 0;
            continue;
        }

        if (arg_eq_ci(name, "HDDUNITPOWER")) {
            if (arg_eq_ci(value, "NIC"))
                opts->dev9_mode = DEV9_NIC;
            else if (arg_eq_ci(value, "NICHDD"))
                opts->dev9_mode = DEV9_NICHDD;
            continue;
        }

        if ((name[0] == 'a' || name[0] == 'A') &&
            (name[1] == 'r' || name[1] == 'R') &&
            (name[2] == 'g' || name[2] == 'G')) {
            if (PatinfoOptionsAppendArg(opts, value) != 0) {
                DPRINTF("PATINFO: failed to append arg entry due to memory exhaustion\n");
                return -1;
            }
            continue;
        }
    }

    return 0;
}

static int read_patinfo_system_cnf(const char *launch_path, PatinfoCnfOptions *opts)
{
    char partition_path[128];
    uint8_t header_buf[512] = {0};
    PatinfoAttrHeader header;
    int fd;
    int seek_res;
    int read_res;
    int cnf_size;
    char *cnf_text;
    char *sep;

    if (launch_path == NULL || opts == NULL)
        return -1;

    snprintf(partition_path, sizeof(partition_path), "%s", launch_path);
    if (strlen(partition_path) > 5) {
        sep = strchr(&partition_path[5], ':');
        if (sep != NULL)
            *sep = '\0';
    }

    fd = fioOpen(partition_path, FIO_O_RDONLY);
    if (fd < 0) {
        DPRINTF("PATINFO: failed to open partition '%s': %d\n", partition_path, fd);
        return -1;
    }

    read_res = fioRead(fd, header_buf, sizeof(header_buf));
    if (read_res < (int)sizeof(PatinfoAttrHeader)) {
        DPRINTF("PATINFO: failed to read attribute header: %d\n", read_res);
        fioClose(fd);
        return -1;
    }

    memcpy(&header, header_buf, sizeof(header));
    if (memcmp(header.magic, "PS2ICON3D", 9) != 0) {
        DPRINTF("PATINFO: invalid attribute area magic\n");
        fioClose(fd);
        return -1;
    }

    if (header.system_cnf.size == 0 || header.system_cnf.offset == 0) {
        DPRINTF("PATINFO: SYSTEM.CNF entry missing in attribute area\n");
        fioClose(fd);
        return -1;
    }

    seek_res = fioLseek(fd, header.system_cnf.offset, FIO_SEEK_SET);
    if (seek_res < 0) {
        DPRINTF("PATINFO: failed to seek SYSTEM.CNF: %d\n", seek_res);
        fioClose(fd);
        return -1;
    }

    cnf_size = (int)header.system_cnf.size;
    if (cnf_size > PATINFO_MAX_CNF)
        cnf_size = PATINFO_MAX_CNF;

    cnf_text = malloc((size_t)cnf_size + 1);
    if (cnf_text == NULL) {
        fioClose(fd);
        return -1;
    }

    read_res = fioRead(fd, cnf_text, cnf_size);
    fioClose(fd);
    if (read_res <= 0) {
        DPRINTF("PATINFO: failed to read SYSTEM.CNF data: %d\n", read_res);
        free(cnf_text);
        return -1;
    }
    cnf_text[read_res] = '\0';

    if (parse_patinfo_cnf_text(cnf_text, opts) != 0) {
        free(cnf_text);
        return -1;
    }
    free(cnf_text);

    if (opts->boot_path == NULL || opts->boot_path[0] == '\0') {
        DPRINTF("PATINFO: SYSTEM.CNF did not contain BOOT/path\n");
        return -1;
    }

    // Match OSDMenu PATINFO behavior: when titleid is missing, derive from
    // partition name and keep only valid Title IDs.
    if (opts->title_id == NULL) {
        opts->title_id = generateTitleID(launch_path);
        if (opts->title_id != NULL && !validateTitleID(opts->title_id)) {
            free(opts->title_id);
            opts->title_id = NULL;
        }
    }

    DPRINTF("PATINFO: parsed BOOT/path='%s', args=%d, skip_argv0=%d\n",
            opts->boot_path,
            opts->arg_count,
            opts->skip_argv0);
    if (opts->ioprp_path != NULL)
        DPRINTF("PATINFO: parsed IOPRP='%s'\n", opts->ioprp_path);
    return 0;
}

static int read_patinfo_payload(const char *launch_path, int read_ioprp, void *dst_mem, uint32_t *size_out)
{
    char partition_path[128];
    uint8_t header_buf[512] = {0};
    PatinfoAttrHeader header;
    PatinfoAttrFile payload;
    int fd;
    int seek_res;
    int read_res;
    char *sep;

    if (launch_path == NULL || dst_mem == NULL || size_out == NULL)
        return -1;

    snprintf(partition_path, sizeof(partition_path), "%s", launch_path);
    if (strlen(partition_path) > 5) {
        sep = strchr(&partition_path[5], ':');
        if (sep != NULL)
            *sep = '\0';
    }

    fd = fioOpen(partition_path, FIO_O_RDONLY);
    if (fd < 0) {
        DPRINTF("PATINFO: failed to open partition '%s' for payload: %d\n", partition_path, fd);
        return -1;
    }

    read_res = fioRead(fd, header_buf, sizeof(header_buf));
    if (read_res < (int)sizeof(PatinfoAttrHeader)) {
        DPRINTF("PATINFO: failed to read attribute header for payload: %d\n", read_res);
        fioClose(fd);
        return -1;
    }

    memcpy(&header, header_buf, sizeof(header));
    if (memcmp(header.magic, "PS2ICON3D", 9) != 0) {
        DPRINTF("PATINFO: invalid attribute area magic for payload\n");
        fioClose(fd);
        return -1;
    }

    payload = read_ioprp ? header.ioprp : header.elf;
    if (payload.size == 0 || payload.offset == 0) {
        DPRINTF("PATINFO: missing %s entry in attribute area\n", read_ioprp ? "IOPRP" : "ELF");
        fioClose(fd);
        return -1;
    }

    seek_res = fioLseek(fd, payload.offset, FIO_SEEK_SET);
    if (seek_res < 0) {
        DPRINTF("PATINFO: failed to seek payload: %d\n", seek_res);
        fioClose(fd);
        return -1;
    }

    read_res = fioRead(fd, dst_mem, (int)payload.size);
    fioClose(fd);
    if (read_res < (int)payload.size) {
        DPRINTF("PATINFO: failed to read %s payload (%d/%u)\n",
                read_ioprp ? "IOPRP" : "ELF",
                read_res,
                payload.size);
        return -1;
    }

    *size_out = payload.size;
    DPRINTF("PATINFO: loaded %s payload @ %p size=0x%08x\n",
            read_ioprp ? "IOPRP" : "ELF",
            dst_mem,
            (unsigned int)payload.size);
    return 0;
}
#endif

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
    intent->skip_argv0 = 0;
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

// Parse trailing loader-control arguments from app argv.
// Matches OSDMenu launcher behavior: scan right-to-left and stop at first non-control arg.
static int ParseLaunchControlArgs(LaunchIntent *intent, int argc, char *argv[])
{
    int i;
    int min_index;

    if (intent == NULL || argc <= 0 || argv == NULL)
        return argc;

    // Config ARG_* entries are passed without argv0, so argv[0] can be a
    // control switch (e.g. -gsm=...). If argv[0] looks like a normal program
    // path, preserve legacy behavior and never consume it as a control arg.
    min_index = 0;
    if (argv[0] != NULL && argv[0][0] != '\0' && argv[0][0] != '-' && argv[0][0] != '+')
        min_index = 1;

    for (i = argc - 1; i >= min_index; i--) {
        const char *val = NULL;

        if (argv[i] == NULL)
            break;

        if (argv[i][0] != '-' && argv[i][0] != '+')
            break;

        if (arg_eq_ci(argv[i], "-appid")) {
            intent->force_appid = 1;
            argc--;
            continue;
        }
        if (arg_eq_ci(argv[i], "-patinfo")) {
            intent->use_patinfo_path = 1;
            argc--;
            continue;
        }
        if (arg_prefix_ci(argv[i], "-titleid=", &val)) {
            while (*val == ' ' || *val == '\t')
                val++;
            ParseTitleOverrideValue(intent, val);
            argc--;
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
            argc--;
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
            argc--;
            continue;
        }
        if (arg_prefix_ci(argv[i], "-la=", &val)) {
            (void)val;
            DPRINTF("Ignoring reserved internal loader flag override '%s'\n", argv[i]);
            argc--;
            continue;
        }

        break;
    }

    return argc;
}

// Apply -patinfo target override from the first remaining app arg when path includes :PATINFO.
// Returns 1 if override applied, 0 if not requested/not applicable, -1 if requested but missing target arg.
static int ApplyPatinfoLaunchOverride(LaunchIntent *intent,
                                      const char *original_filename,
                                      const char *party,
                                      int *argc_inout,
                                      char *argv[],
                                      char *patinfo_path,
                                      size_t patinfo_path_size)
{
    int argc;

    if (intent == NULL || argc_inout == NULL || argv == NULL || patinfo_path == NULL || patinfo_path_size == 0)
        return 0;

    if (!intent->use_patinfo_path || !path_has_patinfo_token(original_filename))
        return 0;

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
        return 1;
    } else {
        DPRINTF("Ignoring -patinfo for '%s': missing target ELF path argument\n", original_filename);
        return -1;
    }

    return 0;
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

static int RunLoaderElfViaStage2(const char *launch_filename,
                                 const char *party,
                                 int argc,
                                 char *argv[],
                                 const char *gsm_arg,
                                 const char *ioprp_arg,
                                 const char *elf_mem_arg,
                                 int dev9_mode,
                                 int skip_argv0)
{
    char loader_args[16] = "-la=";
    char **stage2_argv;
    char *owned_launch = NULL;
    char *owned_gsm = NULL;
    char *owned_ioprp = NULL;
    char *owned_elf = NULL;
    const char *stage2_launch = launch_filename;
    int stage2_argc_base;
    int stage2_argc;
    int extra_count = 0;
    int use_gsm = 0;
    int use_ioprp = 0;
    int use_elf = 0;
    int i;

    if (launch_filename == NULL || *launch_filename == '\0')
        return -1;
    if (size_ps2_stage2_loader_elf < sizeof(embedded_elf_header_t))
        return -1;

    use_gsm = (gsm_arg != NULL && *gsm_arg != '\0');
    use_ioprp = (ioprp_arg != NULL && *ioprp_arg != '\0');
    use_elf = (elf_mem_arg != NULL && *elf_mem_arg != '\0');

    if (party != NULL && *party != '\0') {
        if (path_is_pfs_prefix(launch_filename)) {
            size_t needed = strlen(party) + strlen(launch_filename) + 1;
            owned_launch = malloc(needed);
            if (owned_launch == NULL)
                return -1;
            snprintf(owned_launch, needed, "%s%s", party, launch_filename);
            stage2_launch = owned_launch;
        } else if (strchr(launch_filename, ':') == NULL) {
            size_t needed = strlen(party) + 4 + ((launch_filename[0] == '/') ? 0 : 1) + strlen(launch_filename) + 1;
            owned_launch = malloc(needed);
            if (owned_launch == NULL)
                return -1;
            snprintf(owned_launch, needed, "%spfs:%s%s",
                     party,
                     (launch_filename[0] == '/') ? "" : "/",
                     launch_filename);
            stage2_launch = owned_launch;
        }
    }

    i = (int)strlen(loader_args);
    if (use_gsm)
        loader_args[i++] = 'G';
    if (use_ioprp)
        loader_args[i++] = 'I';
    if (use_elf)
        loader_args[i++] = 'E';
    // Keep stage2 DEV9 handling aligned with direct launch behavior.
    // Non-HDD builds should never request stage2 DEV9/fileXio policy.
#if defined(HDD) && defined(FILEXIO)
    if (dev9_mode == DEV9_NIC) {
        loader_args[i++] = 'N';
    } else
#endif
    {
        // Keep stage2 behavior aligned with direct launch:
        // default launch policy should not shut down DEV9/HDD.
        loader_args[i++] = 'D';
    }
    if (skip_argv0)
        loader_args[i++] = 'A';
    loader_args[i] = '\0';

    if (use_elf) {
        owned_elf = malloc(strlen(elf_mem_arg) + 1);
        if (owned_elf == NULL) {
            if (owned_launch != NULL)
                free(owned_launch);
            return -1;
        }
        strcpy(owned_elf, elf_mem_arg);
        extra_count++;
    }
    if (use_ioprp) {
        owned_ioprp = malloc(strlen(ioprp_arg) + 1);
        if (owned_ioprp == NULL) {
            if (owned_elf != NULL)
                free(owned_elf);
            if (owned_launch != NULL)
                free(owned_launch);
            return -1;
        }
        strcpy(owned_ioprp, ioprp_arg);
        extra_count++;
    }
    if (use_gsm) {
        owned_gsm = malloc(strlen(gsm_arg) + 1);
        if (owned_gsm == NULL) {
            if (owned_ioprp != NULL)
                free(owned_ioprp);
            if (owned_elf != NULL)
                free(owned_elf);
            if (owned_launch != NULL)
                free(owned_launch);
            return -1;
        }
        strcpy(owned_gsm, gsm_arg);
        extra_count++;
    }

    stage2_argc_base = argc + 2;
    stage2_argc = stage2_argc_base + extra_count;
    stage2_argv = malloc(sizeof(char *) * stage2_argc);
    if (stage2_argv == NULL) {
        if (owned_gsm != NULL)
            free(owned_gsm);
        if (owned_ioprp != NULL)
            free(owned_ioprp);
        if (owned_elf != NULL)
            free(owned_elf);
        if (owned_launch != NULL)
            free(owned_launch);
        return -1;
    }

    stage2_argv[0] = (char *)stage2_launch;
    for (i = 0; i < argc; i++)
        stage2_argv[i + 1] = argv[i];

    i = argc + 1;
    if (owned_elf != NULL)
        stage2_argv[i++] = owned_elf;
    if (owned_ioprp != NULL)
        stage2_argv[i++] = owned_ioprp;
    if (owned_gsm != NULL)
        stage2_argv[i++] = owned_gsm;
    stage2_argv[i++] = loader_args;

    DebugPrintStage2Argv(__func__, stage2_argc, stage2_argv);

    if (ExecEmbeddedStage2(ps2_stage2_loader_elf, size_ps2_stage2_loader_elf, stage2_argc, stage2_argv) != 0) {
        free(stage2_argv);
        if (owned_gsm != NULL)
            free(owned_gsm);
        if (owned_ioprp != NULL)
            free(owned_ioprp);
        if (owned_elf != NULL)
            free(owned_elf);
        if (owned_launch != NULL)
            free(owned_launch);
        return -1;
    }

    free(stage2_argv);
    if (owned_gsm != NULL)
        free(owned_gsm);
    if (owned_ioprp != NULL)
        free(owned_ioprp);
    if (owned_elf != NULL)
        free(owned_elf);
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
    int launch_argc;
    char **launch_argv;
    char **launch_argv_owned = NULL;
    int launch_argv_capacity;
    char *stage2_ioprp_arg = NULL;
    char *stage2_elf_arg = NULL;
    int force_stage2 = 0;
    int force_stage2_without_gsm = 0;
    char patinfo_mem_elf[MAX_PATH];
    char patinfo_mem_ioprp[MAX_PATH];
    int i;
    int show_app_id;
    int patinfo_no_history = 0;
    int patinfo_override_used = 0;
#ifdef HDD
    PatinfoCnfOptions patinfo_opts;
    int patinfo_cnf_ok = 0;
#endif

    LaunchIntentInit(&intent, filename);
    patinfo_path[0] = '\0';
    patinfo_mem_elf[0] = '\0';
    patinfo_mem_ioprp[0] = '\0';
#if !EGSM_BUILD
    (void)patinfo_mem_elf;
    (void)patinfo_mem_ioprp;
#endif
#ifdef HDD
    PatinfoOptionsInit(&patinfo_opts);
#endif

#ifdef HDD
    if (path_has_patinfo_token(filename)) {
        if (read_patinfo_system_cnf(filename, &patinfo_opts) == 0)
            patinfo_cnf_ok = 1;
        else
            DPRINTF("PATINFO: falling back to existing launch behavior for '%s'\n", filename);
    }
#endif

#ifdef HDD
    if (patinfo_cnf_ok) {
        patinfo_no_history = patinfo_opts.no_history;
        if (intent.dev9_mode == DEV9_DEFAULT)
            intent.dev9_mode = patinfo_opts.dev9_mode;
        if (intent.title_override == NULL && patinfo_opts.title_id != NULL)
            intent.title_override = strdup(patinfo_opts.title_id);
    }
#endif

    launch_argc = 0;
    launch_argv_capacity = argc;

#ifdef HDD
    if (patinfo_cnf_ok)
        launch_argv_capacity += patinfo_opts.arg_count;
#endif

    if (launch_argv_capacity < 1)
        launch_argv_capacity = 1;

    launch_argv_owned = malloc(sizeof(char *) * (size_t)launch_argv_capacity);
    if (launch_argv_owned == NULL) {
        DPRINTF("Launch argv allocation failed for %d entries\n", launch_argv_capacity);
        LaunchIntentRelease(&intent);
#ifdef HDD
        PatinfoOptionsRelease(&patinfo_opts);
#endif
        return;
    }

    launch_argv = launch_argv_owned;
    for (i = 0; i < argc; i++)
        launch_argv[launch_argc++] = argv[i];

#ifdef HDD
    if (patinfo_cnf_ok) {
        for (i = 0; i < patinfo_opts.arg_count; i++) {
            if (patinfo_opts.args[i] != NULL && patinfo_opts.args[i][0] != '\0')
                launch_argv[launch_argc++] = patinfo_opts.args[i];
        }
    }
#endif

    // Match OSDMenu behavior: consume loader-control args from the merged argv
    // (caller-provided args plus SYSTEM.CNF args) before launch handling.
    launch_argc = ParseLaunchControlArgs(&intent, launch_argc, launch_argv);

    patinfo_override_used = ApplyPatinfoLaunchOverride(&intent,
                                                       filename,
                                                       party,
                                                       &launch_argc,
                                                       launch_argv,
                                                       patinfo_path,
                                                       sizeof(patinfo_path));

    if (patinfo_override_used < 0) {
        DPRINTF("PATINFO: -patinfo requested without target argument, aborting launch for '%s'\n", filename);
        free(launch_argv_owned);
        LaunchIntentRelease(&intent);
#ifdef HDD
        PatinfoOptionsRelease(&patinfo_opts);
#endif
        return;
    }

#ifdef HDD
    // Match OSDMenu PATINFO override semantics: when -patinfo is requested,
    // ignore BOOT/path and IOPRP from SYSTEM.CNF and launch the explicit target.
    if (patinfo_cnf_ok && !patinfo_override_used && patinfo_opts.boot_path != NULL) {
        if (arg_eq_ci(patinfo_opts.boot_path, "PATINFO")) {
            uint32_t elf_size = 0;

            if (read_patinfo_payload(filename, 0, (void *)PATINFO_ELF_MEM_ADDR, &elf_size) == 0) {
                snprintf(patinfo_mem_elf,
                         sizeof(patinfo_mem_elf),
                         "mem:%08X:%08X",
                         (unsigned int)PATINFO_ELF_MEM_ADDR,
                         (unsigned int)elf_size);
                intent.launch_filename = patinfo_mem_elf;
                stage2_elf_arg = patinfo_mem_elf;
                force_stage2 = 1;
                force_stage2_without_gsm = 1;
            } else {
                DPRINTF("PATINFO: failed to load embedded ELF payload, keeping CNF path as-is\n");
                intent.launch_filename = patinfo_opts.boot_path;
            }
        } else {
            intent.launch_filename = patinfo_opts.boot_path;
        }

        if (patinfo_opts.ioprp_path != NULL && patinfo_opts.ioprp_path[0] != '\0') {
            if (arg_eq_ci(patinfo_opts.ioprp_path, "PATINFO")) {
                uint32_t ioprp_size = 0;

                if (read_patinfo_payload(filename, 1, (void *)PATINFO_IOPRP_MEM_ADDR, &ioprp_size) == 0) {
                    snprintf(patinfo_mem_ioprp,
                             sizeof(patinfo_mem_ioprp),
                             "mem:%08X:%08X",
                             (unsigned int)PATINFO_IOPRP_MEM_ADDR,
                             (unsigned int)ioprp_size);
                    stage2_ioprp_arg = patinfo_mem_ioprp;
                    force_stage2 = 1;
                    force_stage2_without_gsm = 1;
                } else {
                    DPRINTF("PATINFO: failed to load embedded IOPRP payload\n");
                }
            } else {
                stage2_ioprp_arg = patinfo_opts.ioprp_path;
                force_stage2 = 1;
                force_stage2_without_gsm = 1;
            }
        }

        if (patinfo_opts.skip_argv0)
            intent.skip_argv0 = 1;

        if (intent.skip_argv0) {
            force_stage2 = 1;
            force_stage2_without_gsm = 1;
        }
    }
#endif

    show_app_id = GameIDAppEnabled();
    if (intent.force_appid || intent.title_override != NULL)
        show_app_id = 1;

    if (show_app_id) {
        char *title_id = (intent.title_override != NULL) ? intent.title_override : generateTitleID(intent.launch_filename);
        if (title_id) {
            if (patinfo_no_history)
                gsDisplayGameID(title_id);
            else
                GameIDHandleApp(title_id, show_app_id);
            if (intent.title_override == NULL)
                free(title_id);
        }
    }

#if EGSM_BUILD
    if (!force_stage2_without_gsm &&
        intent.gsm_flags == 0 &&
        !path_is_rom_binary(intent.launch_filename)) {
        if (RunLoaderElfViaStage2(intent.launch_filename,
                                  party,
                                  launch_argc,
                                  launch_argv,
                                  NULL,
                                  NULL,
                                  NULL,
                                  intent.dev9_mode,
                                  intent.skip_argv0) == 0) {
            free(launch_argv_owned);
            LaunchIntentRelease(&intent);
#ifdef HDD
            PatinfoOptionsRelease(&patinfo_opts);
#endif
            return;
        }
        DPRINTF("Unable to hand off plain launch to embedded stage2; falling back to direct launch\n");
    }
#endif

    if (intent.gsm_flags != 0) {
        if (path_is_rom_binary(intent.launch_filename)) {
            DPRINTF("Ignoring -gsm for ROM path '%s'\n", intent.launch_filename);
        } else {
#if EGSM_BUILD
            if (RunLoaderElfViaStage2(intent.launch_filename,
                                      party,
                                      launch_argc,
                                      launch_argv,
                                      intent.gsm_arg,
                                      stage2_ioprp_arg,
                                      stage2_elf_arg,
                                      intent.dev9_mode,
                                      intent.skip_argv0) == 0) {
                free(launch_argv_owned);
                LaunchIntentRelease(&intent);
#ifdef HDD
                PatinfoOptionsRelease(&patinfo_opts);
#endif
                return;
            }
            DPRINTF("Unable to hand off -gsm launch to embedded stage2; continuing without eGSM\n");
#else
            DPRINTF("Ignoring -gsm (eGSM build disabled): flags 0x%08x\n", (unsigned int)intent.gsm_flags);
#endif
        }
    }

    if (force_stage2_without_gsm) {
#if EGSM_BUILD
        if (RunLoaderElfViaStage2(intent.launch_filename,
                                  party,
                                  launch_argc,
                                  launch_argv,
                                  NULL,
                                  stage2_ioprp_arg,
                                  stage2_elf_arg,
                                  intent.dev9_mode,
                                  intent.skip_argv0) == 0) {
            free(launch_argv_owned);
            LaunchIntentRelease(&intent);
#ifdef HDD
            PatinfoOptionsRelease(&patinfo_opts);
#endif
            return;
        }
        DPRINTF("PATINFO: unable to hand off launch to embedded stage2; continuing with direct launch\n");
#else
        DPRINTF("PATINFO: stage2 handoff required but EGSM_BUILD is disabled\n");
#endif
    }

    if (intent.dev9_mode == DEV9_NIC) {
        DPRINTF("Applying -dev9=NIC (idle HDD, keep DEV9 on)\n");
    } else if (intent.dev9_mode == DEV9_NICHDD) {
        DPRINTF("Applying -dev9=NICHDD (keep HDD and DEV9 on)\n");
    }
    apply_dev9_policy(intent.dev9_mode);

    if (intent.skip_argv0 && launch_argc > 0 && !force_stage2) {
        launch_argc--;
        launch_argv = &launch_argv[1];
    }

    if (party == NULL) {
        DPRINTF("LoadELFFromFile(%s, %d, %p)\n", intent.launch_filename, launch_argc, launch_argv);
        DBGWAIT(2);
        LoadELFFromFile(intent.launch_filename, launch_argc, launch_argv);
    } else {
        DPRINTF("LoadELFFromFileWithPartition(%s, %s, %d, %p);\n", intent.launch_filename, party, launch_argc, launch_argv);
        DBGWAIT(2);
        LoadELFFromFileWithPartition(intent.launch_filename, party, launch_argc, launch_argv);
    }

    free(launch_argv_owned);
    LaunchIntentRelease(&intent);
#ifdef HDD
    PatinfoOptionsRelease(&patinfo_opts);
#endif
}
