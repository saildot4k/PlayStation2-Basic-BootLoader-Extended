#ifndef LOADER_PATH_H
#define LOADER_PATH_H

#include <stddef.h>

typedef enum
{
    LOADER_PATH_FAMILY_NONE = 0,
    LOADER_PATH_FAMILY_MC,
    LOADER_PATH_FAMILY_BDM,
    LOADER_PATH_FAMILY_MX4SIO,
    LOADER_PATH_FAMILY_MMCE,
    LOADER_PATH_FAMILY_HDD_APA,
    LOADER_PATH_FAMILY_XFROM,
} LoaderPathFamily;

void LoaderPathSetModuleStates(int bdm_ready, int mx4sio_ready, int mmce_ready, int hdd_ready);
void LoaderPathSetPendingCommandArgs(int argc, char *argv[]);
void LoaderPathSetPendingCommandAutoMode(int auto_mode);
int LoaderPathConsumeCdvdCancelled(void);
int LoaderPathIsCommandToken(const char *path);
LoaderPathFamily LoaderPathFamilyFromPath(const char *path);

int LoaderAllowVirtualPatinfoEntry(int key_idx, int entry_idx, const char *path);
int LoaderPathCanAttemptNow(const char *path);
void LoaderApplyDisplayNameMode(int display_mode);

char *CheckPath(const char *path);

#endif
