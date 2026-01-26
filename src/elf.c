#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <tamtypes.h>
#include <kernel.h>
#include <elf-loader.h>
#include "debugprintf.h"
#define MAX_PATH 1025
#ifdef DEBUG
#define DBGWAIT(T) sleep(T)
#else
#define DBGWAIT(T)
#endif


void RunLoaderElf(const char *filename, const char *party, int argc, char *argv[])
{
    DPRINTF("%s\n", __FUNCTION__);
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
