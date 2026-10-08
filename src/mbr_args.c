// HDD MBR argv compatibility for HDD-OSD/PSBBN style boot requests.
#include "main.h"
#include "dvdplayer.h"
#include "mbr_crypto.h"

#ifdef PS2BBL_MBR

#define DNASLOAD_PATH "hdd0:__system:pfs:/dnas100/dnasload.elf"

static int mbr_boot_source_is_supported(const char *arg)
{
    return ci_eq(arg, "rom0:HDDBOOT") ||
           ci_eq(arg, "rom0:MBRBOOT") ||
           ci_eq(arg, "xfrom:XFROMBOOT");
}

static int mbr_arg_is_osd_passthrough(const char *arg)
{
    return ci_eq(arg, "BootError") ||
           ci_eq(arg, "BootClock") ||
           ci_eq(arg, "BootBrowser") ||
           ci_eq(arg, "BootCdPlayer") ||
           ci_eq(arg, "BootOpening") ||
           ci_eq(arg, "BootWarning") ||
           ci_eq(arg, "BootIllegal") ||
           ci_eq(arg, "Initialize") ||
           ci_starts_with(arg, "Skip");
}

static void mbr_exec_osd_args(int argc, char *argv[])
{
    CleanUp();
    SifExitCmd();
    ExecOSD(argc, argv);
}

static int mbr_shift_past_opt_arg(int argc, char *argv[])
{
    if (argc > 2 && argv[1] != NULL && ci_starts_with(argv[1], "Opt")) {
        DPRINTF("MBR: ignoring PSBBN option argument '%s'\n", argv[1]);
        return 2;
    }

    return 1;
}

int LoaderMbrHandleArgs(int argc, char *argv[])
{
    int cmd_index;
    const char *cmd;

    if (argc <= 1 || argv == NULL || argv[0] == NULL)
        return 0;
    if (!mbr_boot_source_is_supported(argv[0]))
        return 0;

    if (ci_eq(argv[0], "rom0:MBRBOOT")) {
        DPRINTF("MBR: decrypting PSBBN rom0:MBRBOOT arguments\n");
        argv = decryptMBRBOOTArgs(&argc, argv);
    }

    cmd_index = mbr_shift_past_opt_arg(argc, argv);
    if (cmd_index >= argc || argv[cmd_index] == NULL || argv[cmd_index][0] == '\0')
        return 0;

    cmd = argv[cmd_index];
    DPRINTF("MBR: handling argv command '%s'\n", cmd);

    if (mbr_arg_is_osd_passthrough(cmd)) {
        mbr_exec_osd_args(argc - cmd_index, &argv[cmd_index]);
        return 1;
    }

    if (ci_eq(cmd, "BootPs1Cd") ||
        ci_eq(cmd, "BootPs2Cd") ||
        ci_eq(cmd, "BootPs2Dvd")) {
        dischandler(0, argc - cmd_index - 1, &argv[cmd_index + 1], 0);
        return 1;
    }

    if (ci_eq(cmd, "BootDvdVideo")) {
        DVDPlayerBoot();
        return 1;
    }

    if (ci_eq(cmd, "BootHddApp")) {
        if (cmd_index + 1 >= argc || argv[cmd_index + 1] == NULL || argv[cmd_index + 1][0] == '\0') {
            char *args[] = {"BootError"};
            DPRINTF("MBR: BootHddApp missing target path\n");
            mbr_exec_osd_args(1, args);
            return 1;
        }

        CleanUp();
        RunLoaderElf(argv[cmd_index + 1], NULL, argc - cmd_index - 2, &argv[cmd_index + 2]);
        return 1;
    }

    if (ci_eq(cmd, "DnasPs1Emu") ||
        ci_eq(cmd, "DnasPs2Native") ||
        ci_eq(cmd, "DnasPs2Hdd")) {
        int dnas_argc = argc - cmd_index - 2;

        if (dnas_argc < 0)
            dnas_argc = 0;

        CleanUp();
        RunLoaderElf(DNASLOAD_PATH,
                     NULL,
                     dnas_argc,
                     (dnas_argc > 0) ? &argv[cmd_index + 2] : NULL);
        return 1;
    }

    DPRINTF("MBR: unknown argv command '%s', continuing normal PS2BBL flow\n", cmd);
    return 0;
}

#endif
