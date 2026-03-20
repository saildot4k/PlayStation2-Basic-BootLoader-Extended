#ifndef LOADER_VIDEO_SELECTOR_H
#define LOADER_VIDEO_SELECTOR_H

#include <stddef.h>
#include <tamtypes.h>

void LoaderRunEmergencyVideoModeSelector(int *pre_scanned,
                                         int *hotkey_launches_enabled,
                                         int *block_hotkeys_until_release,
                                         int is_psx_desr,
                                         int native_video_mode,
                                         const u8 *romver,
                                         size_t romver_size,
                                         char *config_path_in_use,
                                         size_t config_path_in_use_size);

#endif
