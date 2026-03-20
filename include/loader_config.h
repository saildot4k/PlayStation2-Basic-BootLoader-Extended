#ifndef LOADER_CONFIG_H
#define LOADER_CONFIG_H

#include <stdio.h>
#include <stddef.h>
#include <tamtypes.h>

typedef struct
{
    int read_success;
    int has_launch_key_entries;
    int video_mode_applied;
} LoaderConfigParseResult;

typedef void (*LoaderEmergencyPollFn)(u64 *window_deadline_ms);
typedef int (*LoaderParseVideoModeFn)(const char *value, int *out_mode);
typedef void (*LoaderApplyVideoModeFn)(int cfg_mode);

int LoaderFindConfigFile(FILE **fp_out,
                         char *path_out,
                         size_t path_out_size,
                         u64 *rescue_combo_deadline,
                         LoaderEmergencyPollFn poll_fn);

int LoaderParseConfigFile(FILE *fp,
                          LoaderConfigParseResult *result,
                          LoaderParseVideoModeFn parse_video_mode_fn,
                          LoaderApplyVideoModeFn apply_video_mode_fn,
                          u64 *rescue_combo_deadline,
                          LoaderEmergencyPollFn poll_fn);

int LoaderBootstrapConfigAndSplash(int *pre_scanned_out,
                                   int *splash_early_presented_out,
                                   char *config_path_in_use,
                                   size_t config_path_in_use_size,
                                   int usb_modules_loaded,
                                   int mx4sio_modules_loaded,
                                   int mmce_modules_loaded,
                                   int hdd_modules_loaded,
                                   int native_video_mode,
                                   int is_psx_desr,
                                   u64 *rescue_combo_deadline,
                                   LoaderEmergencyPollFn poll_fn,
                                   LoaderParseVideoModeFn parse_video_mode_fn,
                                   LoaderApplyVideoModeFn apply_video_mode_fn);

#endif
