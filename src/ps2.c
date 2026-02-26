#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <kernel.h>
#include <sifcmd.h>
#include <sifrpc.h>
#include <iopcontrol.h>
#include <libcdvd.h>
#include "libcdvd_add.h"
#include <fcntl.h>
#include <debug.h>
#include <ctype.h>
#include <unistd.h>
#include <loadfile.h>
//#include "main.h"
#include "ps2.h"
#include "OSDInit.h"
#include "OSDHistory.h"
#include "game_id.h"
#include "egsm_api.h"
#include "debugprintf.h"

void CleanUp(void);
void BootError(void)
{
    char *args[1];

    args[0] = "BootBrowser";
    CleanUp();
    SifExitCmd();
    ExecOSD(1, args);
}

static int g_ps2logo_is_pal = 0;
static int g_ps2disc_cfg_hint = PS2_DISC_HINT_MC0;

static int PS2LOGOGetDiscRegion(void)
{
    return g_ps2logo_is_pal ? 2 : 0;
}

static int PS2LOGOPatchWithOffsets(uint32_t get_region_loc, uint32_t checksum_loc)
{
    if ((_lw(get_region_loc) != 0x27bdfff0) || ((_lw(get_region_loc + 8) & 0xff000000) != 0x0c000000))
        return -1;
    if ((_lw(checksum_loc) & 0xffff0000) != 0x8f820000)
        return -1;

    // Patch region getter call to force the disc region and bypass checksum validation.
    _sw((0x0c000000 | ((uint32_t)PS2LOGOGetDiscRegion >> 2)), get_region_loc + 8);
    _sw(0x24020000, checksum_loc);

    FlushCache(0);
    FlushCache(2);
    return 0;
}

static void ApplyPS2LOGOPatch(void)
{
    // ROM 1.10
    if (!PS2LOGOPatchWithOffsets(0x100178, 0x100278))
        return;
    // ROM 1.20-1.70
    if (!PS2LOGOPatchWithOffsets(0x102078, 0x102178))
        return;
    // ROM 1.80-2.10
    if (!PS2LOGOPatchWithOffsets(0x102018, 0x102118))
        return;
    // ROM 2.20+
    PS2LOGOPatchWithOffsets(0x102018, 0x102264);
}

static void PatchedExecPS2(void *entry, void *gp, int argc, char *argv[])
{
    ApplyPS2LOGOPatch();
    ExecPS2(entry, gp, argc, argv);
}

static void PatchPS2LOGO(uint32_t epc, int is_pal)
{
    g_ps2logo_is_pal = (is_pal != 0);

    if (epc > 0x1000000) {
        // Packed PS2LOGO.
        if ((_lw(0x1000200) & 0xff000000) == 0x08000000) {
            // ROM 2.20+: replace unpacker jump target with our patch routine.
            _sw((0x08000000 | ((uint32_t)ApplyPS2LOGOPatch >> 2)), (uint32_t)0x1000200);
        } else if ((_lw(0x100011c) & 0xff000000) == 0x0c000000) {
            // ROM 2.00: hijack unpacker's ExecPS2 call.
            _sw((0x0c000000 | ((uint32_t)PatchedExecPS2 >> 2)), (uint32_t)0x100011c);
        }

        FlushCache(0);
        FlushCache(2);
        return;
    }

    // Ignore protokernels.
    if (epc > 0x200000)
        return;

    // Unpacked PS2LOGO.
    ApplyPS2LOGOPatch();
}

static void ResetIOPForExec(void)
{
    while (!SifIopReset("", 0)) {
    };
    while (!SifIopSync()) {
    };
    SifInitRpc(0);
}

void PS2DiscSetConfigHint(int hint)
{
    switch (hint) {
        case PS2_DISC_HINT_MC0:
        case PS2_DISC_HINT_MC1:
        case PS2_DISC_HINT_HDD:
            g_ps2disc_cfg_hint = hint;
            break;
        default:
            g_ps2disc_cfg_hint = PS2_DISC_HINT_MC0;
    }
}

#define OSDGSM_MC_PATH       "mc0:/SYS-CONF/OSDGSM.CNF"
#define OSDGSM_HDD_PFS_PATH  "pfs0:/osdmenu/OSDGSM.CNF"

static char *PS2TrimLeft(char *s)
{
    while (s != NULL && isspace((unsigned char)*s))
        s++;
    return s;
}

static void PS2TrimRight(char *s)
{
    size_t len;
    if (s == NULL)
        return;
    len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[len - 1] = '\0';
        len--;
    }
}

static char *PS2DupString(const char *s)
{
    size_t len;
    char *out;

    if (s == NULL)
        return NULL;

    len = strlen(s) + 1;
    out = malloc(len);
    if (out == NULL)
        return NULL;

    memcpy(out, s, len);
    return out;
}

static uint32_t PS2ParseOSDGSMFlags(const char *gsm_arg)
{
    uint32_t flags = 0;
    const char *p = gsm_arg;
    int fd;
    int nread;
    char romver[4] = {0};

    if (p == NULL || *p == '\0')
        return 0;

    /*
     * Match OSDMenu loader format:
     *   fp1
     *   fp2
     *   1080ix1|2|3
     * optional:
     *   :1|2|3
     */
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
        p++;
    }

    if (flags == 0)
        return 0;

    /*
     * Keep PS2BBL behavior:
     * disable 576p on ROMs older than 2.20 or when ROMVER cannot be read.
     */
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

    return flags;
}

static void PS2ApplyEGSMIfNeeded(uint32_t flags)
{
    if (flags == 0)
        return;

    DPRINTF("%s: applying eGSM flags 0x%08x\n", __func__, flags);
    enableGSM(flags);
}

static FILE *PS2OpenOSDGSMConfig(void)
{
    FILE *gsm_conf = NULL;

#ifdef HDD
    if (g_ps2disc_cfg_hint == PS2_DISC_HINT_HDD) {
        DPRINTF("%s: trying %s\n", __func__, OSDGSM_HDD_PFS_PATH);
        gsm_conf = fopen(OSDGSM_HDD_PFS_PATH, "r");
    }
#endif

    if (gsm_conf == NULL) {
        char cnf_path[] = OSDGSM_MC_PATH;
        int preferred = (g_ps2disc_cfg_hint == PS2_DISC_HINT_MC1) ? 1 : 0;

        cnf_path[2] = (char)('0' + preferred);
        DPRINTF("%s: trying %s\n", __func__, cnf_path);
        gsm_conf = fopen(cnf_path, "r");
        if (gsm_conf == NULL) {
            cnf_path[2] = (preferred == 1) ? '0' : '1';
            DPRINTF("%s: trying %s\n", __func__, cnf_path);
            gsm_conf = fopen(cnf_path, "r");
        }
    }

    return gsm_conf;
}

static char *PS2GetOSDGSMArgument(const char *title_id, uint32_t *flags_out)
{
    FILE *gsm_conf;
    char *default_arg = NULL;
    char *title_arg = NULL;
    char *selected_arg = NULL;
    char line_buffer[64];

    if (flags_out != NULL)
        *flags_out = 0;

    if (title_id == NULL || title_id[0] == '\0')
        return NULL;

    DPRINTF("%s: trying to load OSDGSM.CNF\n", __func__);
    gsm_conf = PS2OpenOSDGSMConfig();
    if (gsm_conf == NULL)
        return NULL;

    while (fgets(line_buffer, sizeof(line_buffer), gsm_conf) != NULL) {
        char *key;
        char *value;
        char *sep = strchr(line_buffer, '=');

        if (sep == NULL)
            continue;

        *sep = '\0';
        key = PS2TrimLeft(line_buffer);
        value = PS2TrimLeft(sep + 1);
        value[strcspn(value, "\r\n")] = '\0';
        PS2TrimRight(key);
        PS2TrimRight(value);

        if (*key == '\0' || *value == '\0')
            continue;

        if (!strncmp(key, title_id, 11)) {
            DPRINTF("%s: OSDGSM.CNF title-specific match for %s\n", __func__, title_id);
            title_arg = PS2DupString(value);
            break;
        }

        if (!strncmp(key, "default", 7)) {
            char *tmp = PS2DupString(value);
            if (tmp != NULL) {
                if (default_arg != NULL)
                    free(default_arg);
                default_arg = tmp;
            }
        }
    }

    fclose(gsm_conf);

    if (title_arg != NULL) {
        if (default_arg != NULL)
            free(default_arg);
        selected_arg = title_arg;
    } else {
        selected_arg = default_arg;
    }

    if (selected_arg != NULL) {
        uint32_t flags = PS2ParseOSDGSMFlags(selected_arg);
        if (flags == 0) {
            DPRINTF("%s: ignoring invalid OSDGSM value '%s' for title %s\n", __func__, selected_arg, title_id);
            free(selected_arg);
            return NULL;
        }

        DPRINTF("%s: OSDGSM value '%s' parsed as flags 0x%08x\n", __func__, selected_arg, flags);
        if (flags_out != NULL)
            *flags_out = flags;
    }

    return selected_arg;
}

#define CNF_PATH_LEN_MAX 64
#define CNF_LEN_MAX      1024

static const unsigned char *CNFGetToken(const unsigned char *cnf, const char *key)
{
    for (; isspace(*cnf); cnf++) {
    }

    for (; *key != '\0'; key++, cnf++) {
        // End of file
        if (*cnf == '\0')
            return (const unsigned char *)-1;

        if (*cnf != *key)
            return NULL; // Non-match
    }

    return cnf;
}

static const char *CNFAdvanceLine(const char *start, const char *end)
{
    for (; start <= end; start++) {
        if (*start == '\n')
            return start;
    }

    BootError();

    return NULL;
}

#define CNF_PATH_LEN_MAX 64

static const unsigned char *CNFGetKey(const unsigned char *line, char *key)
{
    int i;

    // Skip leading whitespace
    for (; isspace((unsigned char)*line); line++) {
    }

    if (*line == '\0') { // Unexpected end of file
        return (const unsigned char *)-1;
    }

    for (i = 0; i < CNF_PATH_LEN_MAX && *line != '\0'; i++) {
        if (isgraph((unsigned char)*line)) {
            *key = *line;
            line++;
            key++;
        } else if (isspace((unsigned char)*line)) {
            *key = '\0';
            break;
        } else if (*line == '\0') { // Unexpected end of file. This check exists, along with the other similar check above.
            return (const unsigned char *)-1;
        }
    }

    return line;
}

static int CNFCheckBootFile(const char *value, const char *key)
{
    int i;

    for (; *key != ':'; key++) {
        if (*key == '\0')
            return 0;
    }

    ++key;
    if (*key == '\\')
        key++;

    for (i = 0; i < 10; i++) {
        if (value[i] != key[i])
            return 0;
    }

    return 1;
}

static void CNFExtractDiscIDFromBootPath(const char *boot_path, char *title_id, size_t title_id_len)
{
    const char *p;
    size_t i;

    if (title_id == NULL || title_id_len == 0)
        return;
    title_id[0] = '\0';

    if (boot_path == NULL)
        return;

    p = strchr(boot_path, ':');
    if (p != NULL)
        boot_path = p + 1;

    while (*boot_path == '\\' || *boot_path == '/')
        boot_path++;

    for (i = 0; i + 1 < title_id_len; i++) {
        unsigned char c = (unsigned char)boot_path[i];
        if (c == '\0' || c == ';' || isspace(c))
            break;
        title_id[i] = (char)c;
    }
    title_id[i] = '\0';
}

// The TRUE way (but not the only way!) to get the boot file. Read the comment above PS2DiscBoot().
static int PS2GetBootFile(char *boot)
{
    DPRINTF("%s: start\n", __func__);
    u32 k32;
    u8 key1[16];
#ifdef XCDVD_READKEY
    u8 key2[16];
#endif

    if (sceCdReadKey(0, 0, 0x004B, key1) == 0)
        return 2;

    switch (sceCdGetError()) {
        case SCECdErREAD:
            return 3;
        case 0x37:
            return 3;
        case 0: // Success condition
            break;
        default:
            return 2;
    }

#ifdef XCDVD_READKEY
    if (sceCdReadKey(0, 0, 0x0C03, key2) == 0)
        return 2;

    switch (sceCdGetError()) {
        case SCECdErREAD:
            return 3;
        case 0x37:
            return 3;
        case 0: // Success condition
            break;
        default:
            return 2;
    }

    if (OSDGetConsoleRegion() != CONSOLE_REGION_CHINA) { // The rest of the world
        if (key1[0] == 0 && key1[1] == 0 && key1[2] == 0 && key1[3] == 0 && key1[4] == 0)
            return 3;

        if ((key2[0] != key1[0]) || (key2[1] != key1[1]) || (key2[2] != key1[2]) || (key2[3] != key1[3]) || (key2[4] != key1[4]))
            return 3;

        if ((key1[15] & 5) != 5)
            return 3;

        if ((key2[15] & 1) == 0)
            return 3;
#endif

        boot[11] = '\0';

        k32 = (key1[4] >> 3) | (key1[14] >> 3 << 5) | ((key1[0] & 0x7F) << 10);
        boot[10] = '0' + (k32 % 10);
        boot[9] = '0' + (k32 / 10 % 10);
        boot[8] = '.';
        boot[7] = '0' + (k32 / 10 / 10 % 10);
        boot[6] = '0' + (k32 / 10 / 10 / 10 % 10);
        boot[5] = '0' + (k32 / 10 / 10 / 10 / 10 % 10);
        boot[4] = '_';
        boot[3] = (key1[0] >> 7) | ((key1[1] & 0x3F) << 1);
        boot[2] = (key1[1] >> 6) | ((key1[2] & 0x1F) << 2);
        boot[1] = (key1[2] >> 5) | ((key1[3] & 0xF) << 3);
        boot[0] = ((key1[4] & 0x7) << 4) | (key1[3] >> 4);

        return 0;
#ifdef XCDVD_READKEY
    } else { // China
        if (key1[0] == 0 && key1[1] == 0 && key1[2] == 0 && key1[3] == 0 && key1[4] == 0)
            return 3;

        if (key1[5] == 0) {
            if (key1[6] == 0 && key1[7] == 0 && key1[8] == 0 && key1[9] == 0)
                return 3;
        }

        if ((key2[0] != key1[0]) || (key2[1] != key1[1]) || (key2[2] != key1[2]) || (key2[3] != key1[3]) || (key2[4] != key1[4]) || (key2[5] != key1[5]) || (key2[6] != key1[6]) || (key2[7] != key1[7]) || (key2[8] != key1[8]) || (key2[9] != key1[9]))
            return 3;

        if ((key1[5] != key1[0]) || (key1[6] != key1[1]) || (key1[7] != key1[2]) || (key1[8] != key1[3]) || (key1[9] != key1[4]))
            return 3;

        if ((key1[15] & 7) != 7)
            return 3;

        if ((key2[15] & 3) != 3)
            return 3;

        boot[11] = '\0';

        k32 = (key1[4] >> 3 << 5) | (key1[9] >> 3) | ((key1[5] & 0x7F) << 10);
        boot[10] = '0' + (k32 % 10);
        boot[9] = '0' + (k32 / 10 % 10);
        boot[8] = key1[3];
        boot[7] = '0' + (k32 / 10 / 10 % 10);
        boot[6] = '0' + (k32 / 10 / 10 / 10 % 10);
        boot[5] = '0' + (k32 / 10 / 10 / 10 / 10 % 10);
        boot[4] = key1[5];
        boot[3] = ((key1[6] & 0x3F) << 1) | (key1[5] >> 7);
        boot[2] = ((key1[7] & 0x1F) << 2) | (key1[6] >> 6);
        boot[1] = ((key1[8] & 0xF) << 3) | (key1[7] >> 5);
        boot[0] = ((key1[9] & 0x7) << 4) | (key1[8] >> 4);

        return 0;
    }
#endif
}

/*  While this function uses sceCdReadKey() to obtain the filename,
    it is possible to actually parse SYSTEM.CNF and get the boot filename from BOOT2.
    Lots of homebrew software do that. */
int PS2DiscBoot(int skip_PS2LOGO)
{
    DPRINTF("%s: start\n skip_ps2_logo=%d\n", __func__, skip_PS2LOGO);
    char ps2disc_boot[CNF_PATH_LEN_MAX] = "";             // This was originally static/global.
    char system_cnf[CNF_LEN_MAX], line[CNF_PATH_LEN_MAX]; // These were originally globals/static.
    int is_pal_vmode = 0;
    int bootfile_status = 0;
    char *args[1];
    char *osdgsm_arg = NULL;
    uint32_t osdgsm_flags = 0;
    const unsigned char *pChar;
    const char *cnf_start, *cnf_end;
    int fd, size, size_remaining, size_read;

    bootfile_status = PS2GetBootFile(ps2disc_boot);
    if (bootfile_status != 0) {
        DPRINTF("%s: PS2GetBootFile returned %d. Continuing with BOOT2 from SYSTEM.CNF\n", __func__, bootfile_status);
        ps2disc_boot[0] = '\0';
    } else {
        DPRINTF("%s: PS2GetBootFile returned %s\n", __func__, ps2disc_boot);
    }

    // The browser uses open mode 5 when a specific thread is created, otherwise mode 4.
    if ((fd = open("cdrom0:\\SYSTEM.CNF;1", O_RDONLY)) < 0) {
        scr_setfontcolor(0x0000ff);
        scr_printf("%s: Can't open SYSTEM.CNF\n", __func__);
        sleep(3);
        scr_clear();
        BootError();
    }

    size = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);

    if (size >= CNF_LEN_MAX)
        size = CNF_LEN_MAX - 1;

    for (size_remaining = size; size_remaining > 0; size_remaining -= size_read) {
        if ((size_read = read(fd, system_cnf, size_remaining)) <= 0) {
            scr_setfontcolor(0x0000ff);
            scr_printf("%s: Can't read SYSTEM.CNF\n", __func__);
            sleep(3);
            scr_clear();
            BootError();
        }
    }
    close(fd);
    DPRINTF("%s: readed SYSTEM.CNF. size was %d\n", __func__, size);

    system_cnf[size] = '\0';
    {
        char *vmode = strstr(system_cnf, "VMODE");
        if (vmode != NULL && strstr(vmode, "PAL") != NULL)
            is_pal_vmode = 1;
    }
    cnf_end = &system_cnf[size];

    // Parse SYSTEM.CNF
    cnf_start = system_cnf;
    while ((pChar = CNFGetToken((const unsigned char *)cnf_start, "BOOT2")) == NULL)
        cnf_start = CNFAdvanceLine(cnf_start, cnf_end);

    if (pChar == (const unsigned char *)-1) { // Unexpected EOF
        BootError();
    }

    if ((pChar = CNFGetToken(pChar, "=")) == (const unsigned char *)-1) { // Unexpected EOF
        BootError();
    }

    CNFGetKey(pChar, line);
    DPRINTF("%s line: [%s]\n", __func__, line);
    if (ps2disc_boot[0] != '\0') {
        DPRINTF("%s ELF:  [%s]\n", __func__, ps2disc_boot);
        if (CNFCheckBootFile(ps2disc_boot, line) == 0) {
            DPRINTF("%s: CNFCheckBootFile mismatch. Continuing with BOOT2 from SYSTEM.CNF\n", __func__);
            ps2disc_boot[0] = '\0';
        }
    }
    if (ps2disc_boot[0] == '\0')
        CNFExtractDiscIDFromBootPath(line, ps2disc_boot, sizeof(ps2disc_boot));

    args[0] = line;


    DPRINTF("%s updating play history\n", __func__);
    DPRINTF("%s:\n\tline:[%s]\n\tps2discboot:[%s]\n", __func__, line, ps2disc_boot);
    GameIDHandleDisc(ps2disc_boot, GameIDDiscEnabled());
    osdgsm_arg = PS2GetOSDGSMArgument(ps2disc_boot, &osdgsm_flags);
    if (osdgsm_arg != NULL)
        DPRINTF("%s: discovered OSDGSM setting '%s' for %s\n", __func__, osdgsm_arg, ps2disc_boot);

    CleanUp();
    if (skip_PS2LOGO) {
        SifExitCmd();
        PS2ApplyEGSMIfNeeded(osdgsm_flags);
        if (osdgsm_arg != NULL)
            free(osdgsm_arg);
        LoadExecPS2(line, 0, NULL);
    } else {
        int ret;
        t_ExecData elfdata = {0};

        SifLoadFileInit();
        ret = SifLoadElf("rom0:PS2LOGO", &elfdata);
        if (ret && (ret = SifLoadElfEncrypted("rom0:PS2LOGO", &elfdata))) {
            SifLoadFileExit();
            DPRINTF("%s: failed to load rom0:PS2LOGO for patching (%d); falling back to LoadExecPS2\n", __func__, ret);
            SifExitCmd();
            PS2ApplyEGSMIfNeeded(osdgsm_flags);
            if (osdgsm_arg != NULL)
                free(osdgsm_arg);
            LoadExecPS2("rom0:PS2LOGO", 1, args);
            return 0;
        }
        SifLoadFileExit();

        if (elfdata.epc == 0) {
            DPRINTF("%s: PS2LOGO load returned empty entrypoint; falling back to LoadExecPS2\n", __func__);
            SifExitCmd();
            PS2ApplyEGSMIfNeeded(osdgsm_flags);
            if (osdgsm_arg != NULL)
                free(osdgsm_arg);
            LoadExecPS2("rom0:PS2LOGO", 1, args);
            return 0;
        }

        PatchPS2LOGO(elfdata.epc, is_pal_vmode);
        ResetIOPForExec();
        SifExitCmd();
        PS2ApplyEGSMIfNeeded(osdgsm_flags);
        if (osdgsm_arg != NULL)
            free(osdgsm_arg);
        ExecPS2((void *)elfdata.epc, (void *)elfdata.gp, 1, args);
    }
    return 0;
}
