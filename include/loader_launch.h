#ifndef LOADER_LAUNCH_H
#define LOADER_LAUNCH_H

#include <stddef.h>
#include <tamtypes.h>

typedef void (*LoaderPollEmergencyComboWindowFn)(u64 *window_deadline_ms);

int LoaderRunLaunchWorkflow(int splash_early_presented,
                            int pre_scanned,
                            int *hotkey_launches_enabled,
                            int *block_hotkeys_until_release,
                            int pad_button,
                            int num_buttons,
                            int is_psx_desr,
                            int config_source,
                            int native_video_mode,
                            const u8 *romver,
                            size_t romver_size,
                            char **execpaths,
                            u64 *rescue_combo_deadline,
                            LoaderPollEmergencyComboWindowFn poll_emergency_combo_window);

#endif
