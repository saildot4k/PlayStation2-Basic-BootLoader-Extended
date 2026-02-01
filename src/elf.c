#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <tamtypes.h>
#include <kernel.h>
#include <elf-loader.h>
#include <ctype.h>
#include "debugprintf.h"
#include "game_id.h"
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

void RunLoaderElf(const char *filename, const char *party, int argc, char *argv[])
{
    DPRINTF("%s\n", __FUNCTION__);
    char *title_override = NULL;
    int force_appid = 0;
    if (argc > 0 && argv != NULL) {
        int out_argc = 0;
        int i;
        for (i = 0; i < argc; i++) {
            if (argv[i] != NULL && (argv[i][0] == '-' || argv[i][0] == '+')) {
                if (arg_eq_ci(argv[i], "-appid")) {
                    force_appid = 1;
                    continue;
                }
                const char *p = argv[i];
                const char *prefix = "-titleid=";
                int match = 1;
                int k;
                for (k = 0; prefix[k] != '\0'; k++) {
                    char c = p[k];
                    if (c == '\0') {
                        match = 0;
                        break;
                    }
                    if (tolower((unsigned char)c) != prefix[k]) {
                        match = 0;
                        break;
                    }
                }
                if (match) {
                    const char *val = p + k;
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
            }
            argv[out_argc++] = argv[i];
        }
        argc = out_argc;
    }

    int show_app_id = GameIDAppEnabled();
    if (force_appid || title_override)
        show_app_id = 1;

    if (show_app_id) {
        char *title_id = title_override ? title_override : generateTitleID(filename);
        if (title_id) {
            GameIDHandleApp(title_id, show_app_id);
            if (!title_override)
                free(title_id);
        }
    }

    if (title_override)
        free(title_override);

    if (party == NULL) {
        DPRINTF("LoadELFFromFile(%s, %d, %p)\n", filename, argc, argv);
        DBGWAIT(2);
        LoadELFFromFile(filename, argc, argv);
    } else {
        DPRINTF("LoadELFFromFileWithPartition(%s, %s, %d, %p);\n", filename, party, argc, argv);
        DBGWAIT(2);
        LoadELFFromFileWithPartition(filename, party, argc, argv);
    }
}
