#ifndef LOADER_PATH_H
#define LOADER_PATH_H

#include <stddef.h>

#define LOADER_DEVICE_COUNT 8

void LoaderPathSetModuleStates(int usb_ready, int mx4sio_ready, int mmce_ready, int hdd_ready);
void LoaderPathSetPendingCommandArgs(int argc, char *argv[]);
int LoaderPathConsumeCdvdCancelled(void);

void LoaderBuildDeviceAvailableCache(int dev_ok[LOADER_DEVICE_COUNT]);
int LoaderDeviceAvailableForPathCached(const char *path, const int dev_ok[LOADER_DEVICE_COUNT]);
int LoaderAllowVirtualPatinfoEntry(int key_idx, int entry_idx, const char *path);
void ValidateKeypathsAndSetNames(int display_mode, int scan_paths);

char *CheckPath(const char *path);

#endif
