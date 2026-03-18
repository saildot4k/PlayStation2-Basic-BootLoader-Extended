#ifndef LOADER_VIDEO_H
#define LOADER_VIDEO_H

#include <stddef.h>

int LoaderParseVideoModeValue(const char *value, int *out_mode);
int LoaderDetectNativeVideoMode(void);
void LoaderApplyVideoMode(int cfg_mode);
int LoaderResolveEffectiveVideoMode(int cfg_mode);
const char *LoaderVideoModeLabel(int cfg_mode);
int LoaderStepVideoMode(int current_mode, int direction);

int LoaderSaveVideoModeToConfigFile(int cfg_mode,
                                    int config_source,
                                    char *config_path_in_use,
                                    size_t config_path_in_use_size,
                                    char *saved_path_out,
                                    size_t saved_path_out_size);

#endif
