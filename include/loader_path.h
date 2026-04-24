#ifndef LOADER_PATH_H
#define LOADER_PATH_H

#include <stddef.h>

#define LOADER_DEVICE_COUNT 8

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

void LoaderPathSetModuleStates(int usb_ready, int mx4sio_ready, int mmce_ready, int hdd_ready);
void LoaderPathSetPendingCommandArgs(int argc, char *argv[]);
int LoaderPathConsumeCdvdCancelled(void);
LoaderPathFamily LoaderPathFamilyFromPath(const char *path);

void LoaderBuildDeviceAvailableCache(int dev_ok[LOADER_DEVICE_COUNT]);
int LoaderDeviceAvailableForPathCached(const char *path, const int dev_ok[LOADER_DEVICE_COUNT]);
int LoaderAllowVirtualPatinfoEntry(int key_idx, int entry_idx, const char *path);
int LoaderPathCanAttemptNow(const char *path);
void ValidateKeypathsAndSetNames(int display_mode, int scan_paths);

char *CheckPath(const char *path);

#endif
