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
#include "irx_import.h"

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

/*
 * PS2LOGO packed-path callbacks cannot reliably execute from PS2BBL text
 * because rom0:PS2LOGO load may overwrite user RAM where PS2BBL resides.
 * Keep tiny trampolines in reserved kernel memory (OSDMenu-style).
 */
#define PS2LOGO_KERNEL_UNSCRAMBLE_ADDR 0x00081400
#define PS2LOGO_KERNEL_PATCHJUMP_ADDR  0x00081480
#define PS2LOGO_KERNEL_PATCHEXEC_ADDR  0x000814C0

#define MIPS_REG_ZERO 0
#define MIPS_REG_V0   2
#define MIPS_REG_T0   8
#define MIPS_REG_T1   9
#define MIPS_REG_T2   10
#define MIPS_REG_T3   11
#define MIPS_REG_T4   12
#define MIPS_REG_T5   13
#define MIPS_REG_T6   14
#define MIPS_REG_S1   17
#define MIPS_REG_RA   31

#define MIPS_NOP                  0x00000000u
#define MIPS_LUI(rt, imm)         (0x3c000000u | ((uint32_t)(rt) << 16) | ((uint32_t)(imm) & 0xffffu))
#define MIPS_ORI(rt, rs, imm)     (0x34000000u | ((uint32_t)(rs) << 21) | ((uint32_t)(rt) << 16) | ((uint32_t)(imm) & 0xffffu))
#define MIPS_ADDIU(rt, rs, imm)   (0x24000000u | ((uint32_t)(rs) << 21) | ((uint32_t)(rt) << 16) | ((uint32_t)(imm) & 0xffffu))
#define MIPS_LBU(rt, off, base)   (0x90000000u | ((uint32_t)(base) << 21) | ((uint32_t)(rt) << 16) | ((uint32_t)(off) & 0xffffu))
#define MIPS_SB(rt, off, base)    (0xa0000000u | ((uint32_t)(base) << 21) | ((uint32_t)(rt) << 16) | ((uint32_t)(off) & 0xffffu))
#define MIPS_SW(rt, off, base)    (0xac000000u | ((uint32_t)(base) << 21) | ((uint32_t)(rt) << 16) | ((uint32_t)(off) & 0xffffu))
#define MIPS_SLL(rd, rt, sa)      (0x00000000u | ((uint32_t)(rt) << 16) | ((uint32_t)(rd) << 11) | ((uint32_t)(sa) << 6))
#define MIPS_SRL(rd, rt, sa)      (0x00000002u | ((uint32_t)(rt) << 16) | ((uint32_t)(rd) << 11) | ((uint32_t)(sa) << 6))
#define MIPS_XOR(rd, rs, rt)      (0x00000026u | ((uint32_t)(rs) << 21) | ((uint32_t)(rt) << 16) | ((uint32_t)(rd) << 11))
#define MIPS_OR(rd, rs, rt)       (0x00000025u | ((uint32_t)(rs) << 21) | ((uint32_t)(rt) << 16) | ((uint32_t)(rd) << 11))
#define MIPS_SLTI(rt, rs, imm)    (0x28000000u | ((uint32_t)(rs) << 21) | ((uint32_t)(rt) << 16) | ((uint32_t)(imm) & 0xffffu))
#define MIPS_BNE(rs, rt, off)     (0x14000000u | ((uint32_t)(rs) << 21) | ((uint32_t)(rt) << 16) | ((uint32_t)((off) & 0xffff)))
#define MIPS_J(addr)              (0x08000000u | (((uint32_t)(addr) >> 2) & 0x03ffffffu))
#define MIPS_JAL(addr)            (0x0c000000u | (((uint32_t)(addr) >> 2) & 0x03ffffffu))
#define MIPS_JR(rs)               (0x00000008u | ((uint32_t)(rs) << 21))
#define MIPS_HI16(v)              (((uint32_t)(v) >> 16) & 0xffffu)
#define MIPS_LO16(v)              ((uint32_t)(v) & 0xffffu)

static void PS2LOGOWriteWords(uint32_t dst, const uint32_t *words, size_t word_count)
{
    size_t i;

    for (i = 0; i < word_count; i++)
        _sw(words[i], dst + ((uint32_t)i * 4));
}

static void PS2LOGOInstallUnscrambleStub(void)
{
    static const uint32_t code[] = {
        /* t0 = s1 (logo buffer) */
        MIPS_OR(MIPS_REG_T0, MIPS_REG_S1, MIPS_REG_ZERO),
        /* t1 = key = logo[0], t2 = i = 0 */
        MIPS_LBU(MIPS_REG_T1, 0, MIPS_REG_T0),
        MIPS_ADDIU(MIPS_REG_T2, MIPS_REG_ZERO, 0),
        /* loop: t3 = logo[i] */
        MIPS_LBU(MIPS_REG_T3, 0, MIPS_REG_T0),
        MIPS_XOR(MIPS_REG_T3, MIPS_REG_T3, MIPS_REG_T1),
        MIPS_SLL(MIPS_REG_T4, MIPS_REG_T3, 3),
        MIPS_SRL(MIPS_REG_T5, MIPS_REG_T3, 5),
        MIPS_OR(MIPS_REG_T3, MIPS_REG_T4, MIPS_REG_T5),
        MIPS_SB(MIPS_REG_T3, 0, MIPS_REG_T0),
        MIPS_ADDIU(MIPS_REG_T0, MIPS_REG_T0, 1),
        MIPS_ADDIU(MIPS_REG_T2, MIPS_REG_T2, 1),
        MIPS_SLTI(MIPS_REG_T6, MIPS_REG_T2, 0x6000),
        MIPS_BNE(MIPS_REG_T6, MIPS_REG_ZERO, -10),
        MIPS_NOP,
        MIPS_ADDIU(MIPS_REG_V0, MIPS_REG_ZERO, 0),
        MIPS_JR(MIPS_REG_RA),
        MIPS_NOP
    };

    PS2LOGOWriteWords(PS2LOGO_KERNEL_UNSCRAMBLE_ADDR, code, sizeof(code) / sizeof(code[0]));
}

static void PS2LOGOEmitPatchStub(uint32_t dst, uint32_t region_instr, uint32_t jal_unscramble_instr, uint32_t tail_jump)
{
    uint32_t code[] = {
        MIPS_LUI(MIPS_REG_T0, MIPS_HI16(0x00102020)),
        MIPS_ORI(MIPS_REG_T0, MIPS_REG_T0, MIPS_LO16(0x00102020)),
        MIPS_LUI(MIPS_REG_T1, MIPS_HI16(region_instr)),
        MIPS_ORI(MIPS_REG_T1, MIPS_REG_T1, MIPS_LO16(region_instr)),
        MIPS_SW(MIPS_REG_T1, 0, MIPS_REG_T0),

        MIPS_LUI(MIPS_REG_T0, MIPS_HI16(0x00101578)),
        MIPS_ORI(MIPS_REG_T0, MIPS_REG_T0, MIPS_LO16(0x00101578)),
        MIPS_SW(MIPS_REG_ZERO, 0, MIPS_REG_T0),

        MIPS_LUI(MIPS_REG_T0, MIPS_HI16(0x00101608)),
        MIPS_ORI(MIPS_REG_T0, MIPS_REG_T0, MIPS_LO16(0x00101608)),
        MIPS_LUI(MIPS_REG_T1, MIPS_HI16(jal_unscramble_instr)),
        MIPS_ORI(MIPS_REG_T1, MIPS_REG_T1, MIPS_LO16(jal_unscramble_instr)),
        MIPS_SW(MIPS_REG_T1, 0, MIPS_REG_T0),

        MIPS_J(tail_jump),
        MIPS_NOP
    };

    PS2LOGOWriteWords(dst, code, sizeof(code) / sizeof(code[0]));
}

static uint32_t PS2LOGOGetUnpackerExecTarget(void)
{
    uint32_t instr = _lw(0x100011c);

    if ((instr & 0xff000000u) != 0x0c000000u)
        return 0;

    return ((instr & 0x03ffffffu) << 2);
}

static void PS2LOGOInstallKernelStubs(void)
{
    uint32_t region_instr = (0x24020000u | (g_ps2logo_is_pal ? 2u : 0u));
    uint32_t jal_unscramble_instr = MIPS_JAL(PS2LOGO_KERNEL_UNSCRAMBLE_ADDR);
    uint32_t unpacker_exec_target = PS2LOGOGetUnpackerExecTarget();

    PS2LOGOInstallUnscrambleStub();
    PS2LOGOEmitPatchStub(PS2LOGO_KERNEL_PATCHJUMP_ADDR, region_instr, jal_unscramble_instr, 0x00100000);
    if (unpacker_exec_target != 0)
        PS2LOGOEmitPatchStub(PS2LOGO_KERNEL_PATCHEXEC_ADDR, region_instr, jal_unscramble_instr, unpacker_exec_target);

    FlushCache(0);
    FlushCache(2);
}

static int PS2LOGOPatchWithOffsets(uint32_t get_region_loc, uint32_t cd_dec_loc)
{
    if ((_lw(get_region_loc) != 0x27bdfff0) || ((_lw(get_region_loc + 8) & 0xff000000) != 0x0c000000))
        return -1;
    if (((_lw(cd_dec_loc) & 0xff000000) != 0x0c000000) || (_lw(cd_dec_loc) != _lw(cd_dec_loc + 0x90)))
        return -1;

    // Force logo region from disc VMODE (PAL/NTSC) directly.
    _sw((0x24020000 | (g_ps2logo_is_pal ? 2 : 0)), get_region_loc + 8);

    // Disable DSP XORing call and replace post-read reset call with kernel descramble stub.
    _sw(0x00000000, cd_dec_loc);
    _sw(MIPS_JAL(PS2LOGO_KERNEL_UNSCRAMBLE_ADDR), cd_dec_loc + 0x90);

    FlushCache(0);
    FlushCache(2);
    return 0;
}

static int ApplyPS2LOGOPatch(void)
{
    // ROM 1.10
    if (!PS2LOGOPatchWithOffsets(0x100178, 0x1069e8))
        return 0;
    // ROM 1.20-1.70
    if (!PS2LOGOPatchWithOffsets(0x102078, 0x1015b0))
        return 0;
    // ROM 1.80+
    if (!PS2LOGOPatchWithOffsets(0x102018, 0x101578))
        return 0;

    return -1;
}

static void PatchPS2LOGO(uint32_t epc, int is_pal)
{
    g_ps2logo_is_pal = (is_pal != 0);
    PS2LOGOInstallKernelStubs();

    // Attempt immediate in-memory patch first (works for unpacked PS2LOGO and
    // for packed variants where target code is already present).
    if (ApplyPS2LOGOPatch() == 0)
        return;

    if (epc > 0x1000000) {
        // Packed PS2LOGO fallback: hook unpacker path so patch is applied after unpack.
        if ((_lw(0x1000200) & 0xff000000) == 0x08000000) {
            // ROM 2.20+: replace unpacker jump target with kernel patch+jump wrapper.
            _sw(MIPS_J(PS2LOGO_KERNEL_PATCHJUMP_ADDR), (uint32_t)0x1000200);
        } else if ((_lw(0x100011c) & 0xff000000) == 0x0c000000) {
            // ROM 2.00: hijack unpacker's ExecPS2 call through kernel patch+exec wrapper.
            _sw(MIPS_JAL(PS2LOGO_KERNEL_PATCHEXEC_ADDR), (uint32_t)0x100011c);
        }

        FlushCache(0);
        FlushCache(2);
        return;
    }

    // Ignore protokernels.
    if (epc > 0x200000)
        return;
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

#if EGSM_BUILD
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

    DPRINTF("%s: parsing OSDGSM value '%s'\n", __func__, gsm_arg);

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

    DPRINTF("%s: parsed flags=0x%08x\n", __func__, flags);
    return flags;
}

static void PS2ApplyEGSMIfNeeded(uint32_t flags)
{
    if (flags == 0) {
        DPRINTF("%s: no eGSM flags to apply\n", __func__);
        return;
    }

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
    if (gsm_conf == NULL) {
        DPRINTF("%s: no OSDGSM.CNF found\n", __func__);
        return NULL;
    }

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
        DPRINTF("%s: using title-specific OSDGSM value\n", __func__);
    } else {
        selected_arg = default_arg;
        if (selected_arg != NULL)
            DPRINTF("%s: using default OSDGSM value\n", __func__);
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
#endif

#if !EGSM_BUILD
static void PS2ApplyEGSMIfNeeded(uint32_t flags)
{
    (void)flags;
}
#endif

static void PS2ApplyDeckardXParamIfNeeded(const char *title_id)
{
    int mod_ret = 0;
    int ret;

    if (title_id == NULL || title_id[0] == '\0') {
        DPRINTF("%s: no title id available, skipping XPARAM\n", __func__);
        return;
    }

    /*
     * OSDMenu behavior: load PS2SDK xparam.irx with the current title ID.
     * On non-Deckard consoles this module exits without applying flags.
     */
    ret = SifExecModuleBuffer(xparam_irx, size_xparam_irx, (int)strlen(title_id) + 1, title_id, &mod_ret);
    DPRINTF("%s: title_id=%s ret=%d mod_ret=%d\n", __func__, title_id, ret, mod_ret);
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
    char *args[2];
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

    args[0] = "rom0:PS2LOGO";
    args[1] = line;


    DPRINTF("%s updating play history\n", __func__);
    DPRINTF("%s:\n\tline:[%s]\n\tps2discboot:[%s]\n", __func__, line, ps2disc_boot);
    GameIDHandleDisc(ps2disc_boot, GameIDDiscEnabled());
#if EGSM_BUILD
    osdgsm_arg = PS2GetOSDGSMArgument(ps2disc_boot, &osdgsm_flags);
    if (osdgsm_arg != NULL)
        DPRINTF("%s: discovered OSDGSM setting '%s' for %s\n", __func__, osdgsm_arg, ps2disc_boot);
#else
    DPRINTF("%s: eGSM build disabled, skipping OSDGSM.CNF lookup\n", __func__);
#endif

    CleanUp();
    if (skip_PS2LOGO) {
        PS2ApplyDeckardXParamIfNeeded(ps2disc_boot);
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
            LoadExecPS2("rom0:PS2LOGO", 2, args);
            return 0;
        }
        SifLoadFileExit();

        if (elfdata.epc == 0) {
            DPRINTF("%s: PS2LOGO load returned empty entrypoint; falling back to LoadExecPS2\n", __func__);
            SifExitCmd();
            PS2ApplyEGSMIfNeeded(osdgsm_flags);
            if (osdgsm_arg != NULL)
                free(osdgsm_arg);
            LoadExecPS2("rom0:PS2LOGO", 2, args);
            return 0;
        }

        PatchPS2LOGO(elfdata.epc, is_pal_vmode);
        ResetIOPForExec();
        SifExitCmd();
        PS2ApplyEGSMIfNeeded(osdgsm_flags);
        if (osdgsm_arg != NULL)
            free(osdgsm_arg);
        ExecPS2((void *)elfdata.epc, (void *)elfdata.gp, 2, args);
    }
    return 0;
}
