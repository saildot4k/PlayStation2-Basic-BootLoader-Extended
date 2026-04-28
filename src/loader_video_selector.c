// Interactive emergency video-mode selector UI and config save flow.
#include "main.h"
#include "console_info.h"
#include "loader_path.h"
#include "loader_video.h"
#include "loader_video_selector.h"
#include "splash_render.h"
#include "splash_screen.h"

#define VIDEO_SELECTOR_TEXT_COLOR 0xffffff
#define VIDEO_SELECTOR_HIGHLIGHT_COLOR 0x15d670
#define VIDEO_SELECTOR_SELECT_COLOR 0xffff00
#define VIDEO_SELECTOR_BOX_BG_COLOR 0x606060
#define VIDEO_SELECTOR_BOX_BG_OPACITY_PERCENT 80
#define VIDEO_SELECTOR_BOX_TEXT_PAD_X 10
#define VIDEO_SELECTOR_BOX_TEXT_PAD_Y 20
#define VIDEO_SELECTOR_LINE_SPACING 22
#define VIDEO_SELECTOR_TEXT_HEIGHT 7
#define VIDEO_SELECTOR_BOX_GAP_FROM_LOGO 14
#define VIDEO_SELECTOR_BOX_SCREEN_MARGIN 8
#define VIDEO_SELECTOR_SAVE_FEEDBACK_MS 3000
#define PAD_MASK_RIGHT 0x0020
#define PAD_MASK_LEFT 0x0080
#define PAD_MASK_SELECT 0x0001

void LoaderRunEmergencyVideoModeSelector(int *hotkey_launches_enabled,
                                         int *block_hotkeys_until_release,
                                         int is_psx_desr,
                                         int native_video_mode,
                                         const u8 *romver,
                                         size_t romver_size,
                                         char *config_path_in_use,
                                         size_t config_path_in_use_size)
{
    int selected_mode = GLOBCFG.VIDEO_MODE;
    int applied_effective_mode;
    int prev_pad;
    int config_source = LoaderGetConfigSource();
    ConsoleInfo console_info;
    const char *model;
    const char *ps1ver;
    const char *dvdver;
    const char *source;
    const char *hotkey_lines[KEY_COUNT] = {
        GLOBCFG.KEYNAMES[AUTO],
        GLOBCFG.KEYNAMES[TRIANGLE],
        GLOBCFG.KEYNAMES[CIRCLE],
        GLOBCFG.KEYNAMES[CROSS],
        GLOBCFG.KEYNAMES[SQUARE],
        GLOBCFG.KEYNAMES[UP],
        GLOBCFG.KEYNAMES[DOWN],
        GLOBCFG.KEYNAMES[LEFT],
        GLOBCFG.KEYNAMES[RIGHT],
        GLOBCFG.KEYNAMES[L1],
        GLOBCFG.KEYNAMES[L2],
        GLOBCFG.KEYNAMES[L3],
        GLOBCFG.KEYNAMES[R1],
        GLOBCFG.KEYNAMES[R2],
        GLOBCFG.KEYNAMES[R3],
        GLOBCFG.KEYNAMES[SELECT],
        GLOBCFG.KEYNAMES[START],
    };
    u64 save_feedback_until = 0;
    int save_feedback_ok = 0;
    char save_feedback_path[128];

    if (hotkey_launches_enabled == NULL || block_hotkeys_until_release == NULL ||
        romver == NULL || romver_size == 0 || config_path_in_use == NULL || config_path_in_use_size == 0)
        return;

    if (selected_mode < CFG_VIDEO_MODE_AUTO || selected_mode > CFG_VIDEO_MODE_480P)
        selected_mode = CFG_VIDEO_MODE_AUTO;
    applied_effective_mode = LoaderResolveEffectiveVideoMode(selected_mode);
    *hotkey_launches_enabled = 0;

    GLOBCFG.LOGO_DISP = 3;
    GLOBCFG.HOTKEY_DISPLAY = logo_to_hotkey_display(GLOBCFG.LOGO_DISP);
    LoaderApplyDisplayNameMode(GLOBCFG.HOTKEY_DISPLAY);

    ConsoleInfoCapture(&console_info, config_source, romver, romver_size);
    model = console_info.model;
    ps1ver = console_info.ps1ver;
    dvdver = console_info.dvdver;
    source = console_info.source;
    ConsoleInfoRefreshTemperature(&console_info);

    SplashRenderSetVideoMode(selected_mode, native_video_mode);
    if (SplashRenderIsActive())
        SplashRenderEnd();
    SplashRenderTextBody(GLOBCFG.LOGO_DISP, is_psx_desr);
    save_feedback_path[0] = '\0';

    prev_pad = ReadCombinedPadStatus_raw();

    while (1) {
        u64 now = Timer();
        int pad = ReadCombinedPadStatus_raw();
        int pressed = pad & (~prev_pad);
        int mode_changed = 0;

        if (pressed & PAD_MASK_LEFT) {
            selected_mode = LoaderStepVideoMode(selected_mode, -1);
            mode_changed = 1;
        }
        if (pressed & PAD_MASK_RIGHT) {
            selected_mode = LoaderStepVideoMode(selected_mode, +1);
            mode_changed = 1;
        }

        if (mode_changed) {
            int new_effective_mode = LoaderResolveEffectiveVideoMode(selected_mode);

            if (new_effective_mode != applied_effective_mode) {
                LoaderApplyVideoMode(selected_mode);
                SplashRenderSetVideoMode(selected_mode, native_video_mode);
                if (SplashRenderIsActive())
                    SplashRenderEnd();
                SplashRenderTextBody(GLOBCFG.LOGO_DISP, is_psx_desr);
                applied_effective_mode = new_effective_mode;
            }
        }

        if (pressed & PAD_MASK_SELECT) {
            save_feedback_ok = LoaderSaveVideoModeToConfigFile(selected_mode,
                                                               config_source,
                                                               config_path_in_use,
                                                               config_path_in_use_size,
                                                               save_feedback_path,
                                                               sizeof(save_feedback_path));
            save_feedback_until = Timer() + VIDEO_SELECTOR_SAVE_FEEDBACK_MS;
        }

        if (SplashRenderIsActive()) {
            char line_mode[96];
            char line_save_status[96];
            char save_path_display[56];
            const char *line_start_prefix = "PRESS ";
            const char *line_start_word = "START";
            const char *line_start_suffix = " TO RETRY LAUNCH KEYS";
            const char *line_change = "PRESS LEFT/RIGHT TO CHANGE MODES";
            const char *line_select_prefix = "PRESS ";
            const char *line_select_word = "SELECT";
            const char *line_select_suffix = " TO SAVE TO CONFIG";
            const char *status_path = "(unknown)";
            const char *selected_label = LoaderVideoModeLabel(selected_mode);
            const char *native_label = LoaderVideoModeLabel(native_video_mode);
            int screen_w = SplashRenderGetScreenWidth();
            int screen_h = SplashRenderGetScreenHeight();
            int logo_x = SplashRenderGetLogoX();
            int logo_y = SplashRenderGetLogoY();
            int logo_w = SplashRenderGetLogoWidth();
            int logo_h = SplashRenderGetLogoHeight();
            int anchor_center_x = screen_w / 2;
            int box_x;
            int box_y;
            int box_w;
            int box_h;
            int text_block_h;
            int text_base_y;
            int line_mode_y;
            int line_change_y;
            int line_start_y;
            int line_select_y;
            int line_save_status_y;
            int line_mode_x;
            int line_change_x;
            int line_start_x;
            int line_select_x;
            int line_save_status_x;
            int line_mode_w;
            int line_change_w;
            int line_start_prefix_w;
            int line_start_word_w;
            int line_start_suffix_w;
            int line_start_w;
            int line_select_prefix_w;
            int line_select_word_w;
            int line_select_suffix_w;
            int line_select_w;
            int line_save_status_w;
            int line_start_prefix_x;
            int line_start_word_x;
            int line_start_suffix_x;
            int line_select_prefix_x;
            int line_select_word_x;
            int line_select_suffix_x;
            int max_line_w;
            int line_count = 4;
            int show_save_status = (save_feedback_until > now);
            const char *render_temp;
            u32 save_status_color = VIDEO_SELECTOR_TEXT_COLOR;

            if (logo_x >= 0 && logo_y >= 0 && logo_w > 0 && logo_h > 0) {
                anchor_center_x = logo_x + (logo_w / 2);
                box_y = logo_y + logo_h + VIDEO_SELECTOR_BOX_GAP_FROM_LOGO;
            } else
                box_y = ((screen_h * 55) + 50) / 100;

            render_temp = ConsoleInfoRefreshTemperature(&console_info);

            snprintf(line_mode, sizeof(line_mode), "VIDEO_MODE = %s [NATIVE %s]", selected_label, native_label);
            if (show_save_status) {
                if (save_feedback_path[0] != '\0') {
                    size_t path_len = strlen(save_feedback_path);
                    if (path_len < sizeof(save_path_display)) {
                        memcpy(save_path_display, save_feedback_path, path_len + 1);
                    } else {
                        size_t keep_len = sizeof(save_path_display) - 4;
                        memcpy(save_path_display, save_feedback_path, keep_len);
                        save_path_display[keep_len] = '.';
                        save_path_display[keep_len + 1] = '.';
                        save_path_display[keep_len + 2] = '.';
                        save_path_display[keep_len + 3] = '\0';
                    }
                    status_path = save_path_display;
                }

                snprintf(line_save_status,
                         sizeof(line_save_status),
                         save_feedback_ok ? "SAVED TO %s" : "SAVE FAILED %s",
                         status_path);
                line_count = 5;
                save_status_color = save_feedback_ok ? VIDEO_SELECTOR_HIGHLIGHT_COLOR : 0xff4040;
            }
            line_mode_w = (int)strlen(line_mode) * 6;
            line_change_w = (int)strlen(line_change) * 6;
            line_start_prefix_w = (int)strlen(line_start_prefix) * 6;
            line_start_word_w = (int)strlen(line_start_word) * 6;
            line_start_suffix_w = (int)strlen(line_start_suffix) * 6;
            line_start_w = line_start_prefix_w + line_start_word_w + line_start_suffix_w;
            line_select_prefix_w = (int)strlen(line_select_prefix) * 6;
            line_select_word_w = (int)strlen(line_select_word) * 6;
            line_select_suffix_w = (int)strlen(line_select_suffix) * 6;
            line_select_w = line_select_prefix_w + line_select_word_w + line_select_suffix_w;
            line_save_status_w = show_save_status ? (int)strlen(line_save_status) * 6 : 0;

            text_block_h = ((line_count - 1) * VIDEO_SELECTOR_LINE_SPACING) + VIDEO_SELECTOR_TEXT_HEIGHT;

            max_line_w = line_mode_w;
            if (line_change_w > max_line_w)
                max_line_w = line_change_w;
            if (line_start_w > max_line_w)
                max_line_w = line_start_w;
            if (line_select_w > max_line_w)
                max_line_w = line_select_w;
            if (line_save_status_w > max_line_w)
                max_line_w = line_save_status_w;

            box_w = max_line_w + (VIDEO_SELECTOR_BOX_TEXT_PAD_X * 2);
            box_h = text_block_h + (VIDEO_SELECTOR_BOX_TEXT_PAD_Y * 2);
            box_x = anchor_center_x - (box_w / 2);

            if (box_x < VIDEO_SELECTOR_BOX_SCREEN_MARGIN)
                box_x = VIDEO_SELECTOR_BOX_SCREEN_MARGIN;
            if (box_x + box_w > screen_w - VIDEO_SELECTOR_BOX_SCREEN_MARGIN)
                box_x = screen_w - box_w - VIDEO_SELECTOR_BOX_SCREEN_MARGIN;
            if (box_y < VIDEO_SELECTOR_BOX_SCREEN_MARGIN)
                box_y = VIDEO_SELECTOR_BOX_SCREEN_MARGIN;
            if (box_y + box_h > screen_h - VIDEO_SELECTOR_BOX_SCREEN_MARGIN)
                box_y = screen_h - box_h - VIDEO_SELECTOR_BOX_SCREEN_MARGIN;

            text_base_y = box_y + ((box_h - text_block_h) / 2);
            line_mode_y = text_base_y;
            line_change_y = text_base_y + VIDEO_SELECTOR_LINE_SPACING;
            line_start_y = text_base_y + (VIDEO_SELECTOR_LINE_SPACING * 2);
            line_select_y = text_base_y + (VIDEO_SELECTOR_LINE_SPACING * 3);
            line_save_status_y = text_base_y + (VIDEO_SELECTOR_LINE_SPACING * 4);

            line_mode_x = box_x + (box_w - line_mode_w) / 2;
            line_change_x = box_x + (box_w - line_change_w) / 2;
            line_start_x = box_x + (box_w - line_start_w) / 2;
            line_select_x = box_x + (box_w - line_select_w) / 2;
            line_save_status_x = box_x + (box_w - line_save_status_w) / 2;
            line_start_prefix_x = line_start_x;
            line_start_word_x = line_start_prefix_x + line_start_prefix_w;
            line_start_suffix_x = line_start_word_x + line_start_word_w;
            line_select_prefix_x = line_select_x;
            line_select_word_x = line_select_prefix_x + line_select_prefix_w;
            line_select_suffix_x = line_select_word_x + line_select_word_w;

            SplashRenderSetHotkeysVisible(1);
            SplashRenderBeginFrame();
            SplashRenderHotkeyLines(GLOBCFG.LOGO_DISP, hotkey_lines);
            SplashRenderConsoleInfoLine(GLOBCFG.LOGO_DISP,
                                        model,
                                        console_info.rom_fmt,
                                        dvdver,
                                        ps1ver,
                                        render_temp,
                                        "",
                                        source);
            SplashRenderHotkeyClockDate(GLOBCFG.LOGO_DISP, now);
            SplashRenderDrawRoundedRect(box_x,
                                        box_y,
                                        box_w,
                                        box_h,
                                        0,
                                        VIDEO_SELECTOR_BOX_BG_COLOR,
                                        VIDEO_SELECTOR_BOX_BG_OPACITY_PERCENT);
            SplashRenderDrawTextPxScaled(line_mode_x,
                                         line_mode_y,
                                         VIDEO_SELECTOR_HIGHLIGHT_COLOR,
                                         line_mode,
                                         1);
            SplashRenderDrawTextPxScaled(line_change_x,
                                         line_change_y,
                                         VIDEO_SELECTOR_TEXT_COLOR,
                                         line_change,
                                         1);
            SplashRenderDrawTextPxScaled(line_start_prefix_x,
                                         line_start_y,
                                         VIDEO_SELECTOR_TEXT_COLOR,
                                         line_start_prefix,
                                         1);
            SplashRenderDrawTextPxScaled(line_start_word_x,
                                         line_start_y,
                                         VIDEO_SELECTOR_HIGHLIGHT_COLOR,
                                         line_start_word,
                                         1);
            SplashRenderDrawTextPxScaled(line_start_suffix_x,
                                         line_start_y,
                                         VIDEO_SELECTOR_TEXT_COLOR,
                                         line_start_suffix,
                                         1);
            SplashRenderDrawTextPxScaled(line_select_prefix_x,
                                         line_select_y,
                                         VIDEO_SELECTOR_TEXT_COLOR,
                                         line_select_prefix,
                                         1);
            SplashRenderDrawTextPxScaled(line_select_word_x,
                                         line_select_y,
                                         VIDEO_SELECTOR_SELECT_COLOR,
                                         line_select_word,
                                         1);
            SplashRenderDrawTextPxScaled(line_select_suffix_x,
                                         line_select_y,
                                         VIDEO_SELECTOR_TEXT_COLOR,
                                         line_select_suffix,
                                         1);
            if (show_save_status) {
                SplashRenderDrawTextPxScaled(line_save_status_x,
                                             line_save_status_y,
                                             save_status_color,
                                             line_save_status,
                                             1);
            }
            SplashRenderPresent();
        }

        if (pressed & PAD_START) {
            *hotkey_launches_enabled = 1;
            break;
        }

        prev_pad = pad;
    }

    GLOBCFG.VIDEO_MODE = selected_mode;
    SplashRenderSetVideoMode(GLOBCFG.VIDEO_MODE, native_video_mode);
    *block_hotkeys_until_release = 1;
    if (SplashRenderIsActive())
        SplashRenderEnd();

    while (ReadCombinedPadStatus_raw() & PAD_START) {
    }
}
