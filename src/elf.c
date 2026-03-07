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
#include "egsm_api.h"
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

static uint32_t parse_gsm_flags(const char *gsm_arg)
{
    uint32_t flags = 0;
    const char *p = gsm_arg;
    int fd;
    int nread;
    char romver[4] = {0};

    if (p == NULL || *p == '\0')
        return 0;

    DPRINTF("%s: parsing -gsm value '%s'\n", __func__, gsm_arg);

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
            romver[1] < '2' || (romver[1] == '2' && romver[2] < '2'))
            flags |= EGSM_FLAG_NO_576P;
    }

    DPRINTF("%s: parsed -gsm flags=0x%08x\n", __func__, flags);
    return flags;
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

void RunLoaderElf(const char *filename, const char *party, int argc, char *argv[])
{
    enum {
        DEV9_DEFAULT = 0,
        DEV9_NIC,
        DEV9_NICHDD
    };

    DPRINTF("%s\n", __FUNCTION__);
    const char *launch_filename = filename;
    char patinfo_path[MAX_PATH];
    char *title_override = NULL;
    int force_appid = 0;
    int use_patinfo_path = 0;
    int dev9_mode = DEV9_DEFAULT;
    uint32_t gsm_flags = 0;

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
                        gsm_flags = parse_gsm_flags(val);
                        if (gsm_flags == 0)
                            DPRINTF("Ignoring invalid -gsm value: '%s'\n", val);
                        else
                            DPRINTF("Parsed -gsm value '%s' as flags 0x%08x\n", val, gsm_flags);
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
            DPRINTF("Applying -gsm flags 0x%08x before launch\n", gsm_flags);
            enableGSM(gsm_flags);
#else
            DPRINTF("Ignoring -gsm (eGSM build disabled): flags 0x%08x\n", gsm_flags);
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
