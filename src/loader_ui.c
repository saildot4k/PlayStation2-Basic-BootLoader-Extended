// Splash/console UI prompts and status rendering helpers for interactive flows.
#include "main.h"
#include "splash_screen.h"
#include "splash_render.h"

extern int g_is_psx_desr;
extern int g_native_video_mode;

#define MISSING_PATH_RESCUE_CHORD_WINDOW_MS 250
#define VIDEO_SELECTOR_HIGHLIGHT_COLOR 0x15d670
#define PAD_MASK_ANY 0xffff

#ifndef NO_TEMP_DISP
// Query CDVD thermal sensor and return formatted Celsius string when supported.
int QueryTemperatureCelsius(char *temp_buf, size_t temp_buf_size)
{
    unsigned char in_buffer[1];
    unsigned char out_buffer[16];
    unsigned short temp;
    int whole;
    int tenths;
    int stat = 0;

    if (temp_buf == NULL || temp_buf_size == 0)
        return 0;

    temp_buf[0] = '\0';
    memset(out_buffer, 0, sizeof(out_buffer));

    in_buffer[0] = 0xEF;
    if (sceCdApplySCmd(0x03, in_buffer, sizeof(in_buffer), out_buffer) != 0)
        stat = out_buffer[0];

    if (stat != 0)
        return 0;

    temp = (unsigned short)(out_buffer[1] * 256 + out_buffer[2]);
    whole = (temp - (temp % 128)) / 128;
    tenths = (int)(((temp % 128) * 10 + 64) / 128);
    if (tenths >= 10) {
        whole++;
        tenths -= 10;
    }
    snprintf(temp_buf,
             temp_buf_size,
             "%02d.%dC",
             whole,
             tenths);
    return 1;
}
#endif

static int IsCdvdCommandToken(const char *path)
{
    if (path == NULL)
        return 0;
    return (!strcmp(path, "$CDVD") || !strcmp(path, "$CDVD_NO_PS2LOGO"));
}

static void SplashDrawStatusBelowLogo(const char *text, u32 color)
{
    int line_width;
    int x;
    int y;
    int screen_w;
    int screen_h;
    int anchor_center_x;
    int logo_x;
    int logo_y;
    int logo_w;
    int logo_h;

    if (text == NULL || !SplashRenderIsActive())
        return;

    screen_w = SplashRenderGetScreenWidth();
    screen_h = SplashRenderGetScreenHeight();
    logo_x = SplashRenderGetLogoX();
    logo_y = SplashRenderGetLogoY();
    logo_w = SplashRenderGetLogoWidth();
    logo_h = SplashRenderGetLogoHeight();
    anchor_center_x = logo_x + (logo_w / 2);
    y = logo_y + logo_h + 6;

    if (y > screen_h - 20)
        y = screen_h - 20;
    if (y < 0)
        y = 0;

    line_width = (int)strlen(text) * 6;
    x = anchor_center_x - (line_width / 2);
    if (x < 8)
        x = 8;
    if (x + line_width > screen_w - 8)
        x = screen_w - line_width - 8;
    if (x < 8)
        x = 8;

    SplashRenderSetHotkeysVisible(0);
    SplashRenderBeginFrame();
    SplashRenderDrawTextPxScaled(x, y, color, text, 1);
    SplashRenderPresent();
}

static void SplashDrawStatusForLaunch(int logo_disp, const char *text, u32 color)
{
    if (text == NULL || !SplashRenderIsActive())
        return;

    if (logo_disp >= 2)
        SplashDrawStatusBelowLogo(text, color);
    else
        SplashDrawCenteredStatusWithInfo(text, color, "", "", "", "", NULL, "");
}

void SplashDrawLoadingStatus(int logo_disp)
{
    const char *splash_hotkey_lines[KEY_COUNT] = {
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
    char loading_line[16];
    int dots;
    int loading_max_w;
    int i;
    int loading_x;
    int loading_y;
    int screen_w;
    int screen_h;
    int anchor_center_x;
    int logo_x;
    int logo_y;
    int logo_w;
    int logo_h;
    u64 now_ms;
    static int s_loading_phase = 0;
    static u64 s_loading_phase_tick_ms = 0;

    if (!SplashRenderIsActive())
        return;

    now_ms = Timer();
    if (s_loading_phase_tick_ms == 0)
        s_loading_phase_tick_ms = now_ms;
    if (now_ms >= s_loading_phase_tick_ms + 500u) {
        s_loading_phase = (s_loading_phase + 1) % 3;
        s_loading_phase_tick_ms = now_ms;
    }
    dots = s_loading_phase + 1;
    strcpy(loading_line, "Loading");
    for (i = 0; i < dots; i++)
        loading_line[7 + i] = '.';
    loading_line[7 + dots] = '\0';

    screen_w = SplashRenderGetScreenWidth();
    screen_h = SplashRenderGetScreenHeight();
    anchor_center_x = SplashRenderGetScreenCenterX();
    logo_x = SplashRenderGetLogoX();
    logo_y = SplashRenderGetLogoY();
    logo_w = SplashRenderGetLogoWidth();
    logo_h = SplashRenderGetLogoHeight();

    if (logo_x >= 0 && logo_y >= 0 && logo_w > 0 && logo_h > 0) {
        anchor_center_x = logo_x + (logo_w / 2);
        loading_y = logo_y + logo_h + 6;
    } else {
        loading_y = SplashRenderGetScreenCenterY() + 10;
    }

    if (loading_y > screen_h - 20)
        loading_y = screen_h - 20;
    if (loading_y < 0)
        loading_y = 0;

    // Keep the loading label anchored so it does not shift as dots change.
    loading_max_w = (int)strlen("Loading...") * 6;
    loading_x = anchor_center_x - (loading_max_w / 2);
    if (loading_x < 8)
        loading_x = 8;
    if (loading_x + loading_max_w > screen_w - 8)
        loading_x = screen_w - loading_max_w - 8;

    SplashRenderSetHotkeysVisible(logo_disp >= 3);
    SplashRenderBeginFrame();
    if (logo_disp >= 3)
        SplashRenderHotkeyLines(logo_disp, splash_hotkey_lines);
    SplashRenderDrawTextPxScaled(loading_x, loading_y, 0x404040, loading_line, 1);
    SplashRenderPresent();
}

void SplashDrawCenteredStatusWithInfo(const char *text,
                                             u32 color,
                                             const char *model,
                                             const char *rom_fmt,
                                             const char *dvdver,
                                             const char *ps1ver,
                                             const char *temp_celsius,
                                             const char *source)
{
    int line_width;
    int x;
    int y;
    int screen_w;
    int screen_h;
    int anchor_center_x;
    int logo_x;
    int logo_y;
    int logo_w;
    int logo_h;

    if (text == NULL || !SplashRenderIsActive())
        return;

    SplashRenderSetHotkeysVisible(0);
    SplashRenderBeginFrame();
    SplashRenderConsoleInfoLine(GLOBCFG.LOGO_DISP,
                                model,
                                rom_fmt,
                                dvdver,
                                ps1ver,
                                temp_celsius,
                                "",
                                source);
    screen_w = SplashRenderGetScreenWidth();
    screen_h = SplashRenderGetScreenHeight();
    line_width = (int)strlen(text) * 6;
    if (GLOBCFG.LOGO_DISP >= 2) {
        logo_x = SplashRenderGetLogoX();
        logo_y = SplashRenderGetLogoY();
        logo_w = SplashRenderGetLogoWidth();
        logo_h = SplashRenderGetLogoHeight();
        anchor_center_x = logo_x + (logo_w / 2);
        y = logo_y + logo_h + 6;
        if (y > screen_h - 20)
            y = screen_h - 20;
        if (y < 0)
            y = 0;
        x = anchor_center_x - (line_width / 2);
        if (x < 8)
            x = 8;
        if (x + line_width > screen_w - 8)
            x = screen_w - line_width - 8;
    } else {
        x = SplashRenderGetScreenCenterX() - (line_width / 2);
        y = SplashRenderGetScreenCenterY() - 4;
    }
    if (x < 8)
        x = 8;
    SplashRenderDrawTextPxScaled(x, y, color, text, 1);
    SplashRenderPresent();
}

void SplashDrawRetryPromptWithInfo(const char *line1,
                                          u32 line1_color,
                                          int dots,
                                          const char *model,
                                          const char *rom_fmt,
                                          const char *dvdver,
                                          const char *ps1ver,
                                          const char *temp_celsius,
                                          const char *source)
{
    const char *line2_prefix = "INSERT DISC OR PRESS ";
    const char *line2_word = "START";
    const char *line2_suffix = " TO RETRY HOTKEYS";
    char dots_buf[4];
    int i, dot_count;
    int line1_w;
    int line2_prefix_w;
    int line2_word_w;
    int line2_suffix_w;
    int line2_w;
    int x1;
    int x2;
    int x2_word;
    int x2_suffix;
    int dot_x;
    int y1;
    int y2;
    int screen_w;
    int screen_h;
    int anchor_center_x;
    int logo_x;
    int logo_y;
    int logo_w;
    int logo_h;

    if (!SplashRenderIsActive())
        return;
    if (line1 == NULL)
        return;

    if (dots < 0)
        dots = 0;
    if (dots > 3)
        dots = 3;

    dot_count = dots;
    if (dot_count < 0)
        dot_count = 0;
    if (dot_count > 3)
        dot_count = 3;
    for (i = 0; i < dot_count; i++)
        dots_buf[i] = '.';
    dots_buf[dot_count] = '\0';

    SplashRenderSetHotkeysVisible(0);
    SplashRenderBeginFrame();
    SplashRenderConsoleInfoLine(GLOBCFG.LOGO_DISP,
                                model,
                                rom_fmt,
                                dvdver,
                                ps1ver,
                                temp_celsius,
                                "",
                                source);

    screen_w = SplashRenderGetScreenWidth();
    screen_h = SplashRenderGetScreenHeight();
    line1_w = (int)strlen(line1) * 6;
    line2_prefix_w = (int)strlen(line2_prefix) * 6;
    line2_word_w = (int)strlen(line2_word) * 6;
    line2_suffix_w = (int)strlen(line2_suffix) * 6;
    line2_w = line2_prefix_w + line2_word_w + line2_suffix_w;
    if (GLOBCFG.LOGO_DISP >= 2) {
        logo_x = SplashRenderGetLogoX();
        logo_y = SplashRenderGetLogoY();
        logo_w = SplashRenderGetLogoWidth();
        logo_h = SplashRenderGetLogoHeight();
        anchor_center_x = logo_x + (logo_w / 2);
        y1 = logo_y + logo_h + 2;
        if (y1 > screen_h - 28)
            y1 = screen_h - 28;
        if (y1 < 0)
            y1 = 0;
        y2 = y1 + 18;
        x1 = anchor_center_x - (line1_w / 2);
        x2 = anchor_center_x - (line2_w / 2);
        if (x1 + line1_w > screen_w - 8)
            x1 = screen_w - line1_w - 8;
        if (x2 + line2_w > screen_w - 8)
            x2 = screen_w - line2_w - 8;
    } else {
        x1 = SplashRenderGetScreenCenterX() - (line1_w / 2);
        x2 = SplashRenderGetScreenCenterX() - (line2_w / 2);
        y1 = SplashRenderGetScreenCenterY() - 16;
        y2 = SplashRenderGetScreenCenterY() + 2;
    }
    if (x1 < 8)
        x1 = 8;
    if (x2 < 8)
        x2 = 8;
    x2_word = x2 + line2_prefix_w;
    x2_suffix = x2_word + line2_word_w;
    dot_x = x2 + line2_w;

    SplashRenderDrawTextPxScaled(x1, y1, line1_color, line1, 1);
    SplashRenderDrawTextPxScaled(x2, y2, 0xffffff, line2_prefix, 1);
    SplashRenderDrawTextPxScaled(x2_word, y2, VIDEO_SELECTOR_HIGHLIGHT_COLOR, line2_word, 1);
    SplashRenderDrawTextPxScaled(x2_suffix, y2, 0xffffff, line2_suffix, 1);
    if (dots_buf[0] != '\0')
        SplashRenderDrawTextPxScaled(dot_x, y2, 0xffffff, dots_buf, 1);
    SplashRenderPresent();
}

static void SplashDrawMissingPathPromptWithInfo(const char *button_name,
                                                const char *model,
                                                const char *rom_fmt,
                                                const char *dvdver,
                                                const char *ps1ver,
                                                const char *temp_celsius,
                                                const char *source)
{
    char line1[96];
    const char *safe_button = (button_name != NULL && *button_name != '\0') ? button_name : "BUTTON";
    const char *line2_prefix = "PRESS ";
    const char *line2_word = "START";
    const char *line2_suffix = " TO RETRY LAUNCH KEYS";
    const char *line3_prefix = "PRESS ";
    const char *line3_word1 = "R1";
    const char *line3_plus = "+";
    const char *line3_word2 = "START";
    const char *line3_suffix = " FOR RESCUE MODE";
    int line1_w;
    int line2_prefix_w;
    int line2_word_w;
    int line2_suffix_w;
    int line2_w;
    int line3_prefix_w;
    int line3_word1_w;
    int line3_plus_w;
    int line3_word2_w;
    int line3_suffix_w;
    int line3_w;
    int x1;
    int x2;
    int x2_word;
    int x2_suffix;
    int x3;
    int x3_word1;
    int x3_plus;
    int x3_word2;
    int x3_suffix;
    int y1;
    int y2;
    int y3;
    int screen_w;
    int screen_h;
    int anchor_center_x;
    int logo_x;
    int logo_y;
    int logo_w;
    int logo_h;

    if (!SplashRenderIsActive())
        return;

    snprintf(line1, sizeof(line1), "NO VALID PATH FOUND FOR %s", safe_button);

    SplashRenderSetHotkeysVisible(0);
    SplashRenderBeginFrame();
    SplashRenderConsoleInfoLine(GLOBCFG.LOGO_DISP,
                                model,
                                rom_fmt,
                                dvdver,
                                ps1ver,
                                temp_celsius,
                                "",
                                source);

    screen_w = SplashRenderGetScreenWidth();
    screen_h = SplashRenderGetScreenHeight();
    line1_w = (int)strlen(line1) * 6;
    line2_prefix_w = (int)strlen(line2_prefix) * 6;
    line2_word_w = (int)strlen(line2_word) * 6;
    line2_suffix_w = (int)strlen(line2_suffix) * 6;
    line2_w = line2_prefix_w + line2_word_w + line2_suffix_w;
    line3_prefix_w = (int)strlen(line3_prefix) * 6;
    line3_word1_w = (int)strlen(line3_word1) * 6;
    line3_plus_w = (int)strlen(line3_plus) * 6;
    line3_word2_w = (int)strlen(line3_word2) * 6;
    line3_suffix_w = (int)strlen(line3_suffix) * 6;
    line3_w = line3_prefix_w + line3_word1_w + line3_plus_w + line3_word2_w + line3_suffix_w;

    if (GLOBCFG.LOGO_DISP >= 2) {
        logo_x = SplashRenderGetLogoX();
        logo_y = SplashRenderGetLogoY();
        logo_w = SplashRenderGetLogoWidth();
        logo_h = SplashRenderGetLogoHeight();
        anchor_center_x = logo_x + (logo_w / 2);
        y1 = logo_y + logo_h + 2;
        if (y1 > screen_h - 46)
            y1 = screen_h - 46;
        if (y1 < 0)
            y1 = 0;
    } else {
        anchor_center_x = SplashRenderGetScreenCenterX();
        y1 = SplashRenderGetScreenCenterY() - 20;
    }

    y2 = y1 + 18;
    y3 = y2 + 18;
    x1 = anchor_center_x - (line1_w / 2);
    x2 = anchor_center_x - (line2_w / 2);
    x3 = anchor_center_x - (line3_w / 2);

    if (x1 < 8)
        x1 = 8;
    if (x2 < 8)
        x2 = 8;
    if (x3 < 8)
        x3 = 8;
    if (x1 + line1_w > screen_w - 8)
        x1 = screen_w - line1_w - 8;
    if (x2 + line2_w > screen_w - 8)
        x2 = screen_w - line2_w - 8;
    if (x3 + line3_w > screen_w - 8)
        x3 = screen_w - line3_w - 8;
    if (x1 < 8)
        x1 = 8;
    if (x2 < 8)
        x2 = 8;
    if (x3 < 8)
        x3 = 8;

    x2_word = x2 + line2_prefix_w;
    x2_suffix = x2_word + line2_word_w;
    x3_word1 = x3 + line3_prefix_w;
    x3_plus = x3_word1 + line3_word1_w;
    x3_word2 = x3_plus + line3_plus_w;
    x3_suffix = x3_word2 + line3_word2_w;

    SplashRenderDrawTextPxScaled(x1, y1, 0xffff00, line1, 1);
    SplashRenderDrawTextPxScaled(x2, y2, 0xffffff, line2_prefix, 1);
    SplashRenderDrawTextPxScaled(x2_word, y2, VIDEO_SELECTOR_HIGHLIGHT_COLOR, line2_word, 1);
    SplashRenderDrawTextPxScaled(x2_suffix, y2, 0xffffff, line2_suffix, 1);
    SplashRenderDrawTextPxScaled(x3, y3, 0xffffff, line3_prefix, 1);
    SplashRenderDrawTextPxScaled(x3_word1, y3, VIDEO_SELECTOR_HIGHLIGHT_COLOR, line3_word1, 1);
    SplashRenderDrawTextPxScaled(x3_plus, y3, 0xffffff, line3_plus, 1);
    SplashRenderDrawTextPxScaled(x3_word2, y3, VIDEO_SELECTOR_HIGHLIGHT_COLOR, line3_word2, 1);
    SplashRenderDrawTextPxScaled(x3_suffix, y3, 0xffffff, line3_suffix, 1);
    SplashRenderPresent();
}

static void DrawConsoleMissingPathPrompt(const char *button_name)
{
    const char *safe_button = (button_name != NULL && *button_name != '\0') ? button_name : "BUTTON";

    scr_clear();
    scr_setfontcolor(0xffff00);
    scr_printf("\n\n\t\tNO VALID PATH FOUND FOR %s\n\n", safe_button);
    scr_setfontcolor(0xffffff);
    scr_printf("\t\tPRESS ");
    scr_setfontcolor(VIDEO_SELECTOR_HIGHLIGHT_COLOR);
    scr_printf("START");
    scr_setfontcolor(0xffffff);
    scr_printf(" TO RETRY LAUNCH KEYS\n");
    scr_printf("\t\tPRESS ");
    scr_setfontcolor(VIDEO_SELECTOR_HIGHLIGHT_COLOR);
    scr_printf("R1");
    scr_setfontcolor(0xffffff);
    scr_printf("+");
    scr_setfontcolor(VIDEO_SELECTOR_HIGHLIGHT_COLOR);
    scr_printf("START");
    scr_setfontcolor(0xffffff);
    scr_printf(" FOR RESCUE MODE\n");
}

int WaitForMissingPathAction(const char *button_name,
                             const char *model,
                             const char *rom_fmt,
                             const char *dvdver,
                             const char *ps1ver,
                             const char *temp_celsius,
                             const char *source)
{
    int prev_pad = 0;
    u64 start_retry_deadline = 0;

    if (!SplashRenderIsActive() && GLOBCFG.LOGO_DISP > 0) {
        int missing_path_logo_disp = (GLOBCFG.LOGO_DISP >= 1) ? GLOBCFG.LOGO_DISP : 1;

        SplashRenderSetVideoMode(GLOBCFG.VIDEO_MODE, g_native_video_mode);
        SplashRenderTextBody(missing_path_logo_disp, g_is_psx_desr);
    }

    if (SplashRenderIsActive())
        SplashDrawMissingPathPromptWithInfo(button_name, model, rom_fmt, dvdver, ps1ver, temp_celsius, source);
    else
        DrawConsoleMissingPathPrompt(button_name);

    while (ReadCombinedPadStatus_raw() & PAD_MASK_ANY) {
    }

    while (1) {
        int pad = ReadCombinedPadStatus_raw();

        if ((pad & PAD_R1) && (pad & PAD_START))
            EMERGENCY();

        if (!(prev_pad & PAD_START) && (pad & PAD_START) && start_retry_deadline == 0)
            start_retry_deadline = Timer() + MISSING_PATH_RESCUE_CHORD_WINDOW_MS;

        if (start_retry_deadline != 0 && Timer() >= start_retry_deadline) {
            while (ReadCombinedPadStatus_raw() & PAD_MASK_ANY) {
            }
            return 1;
        }

        prev_pad = pad;
    }
}

void SplashDrawEmergencyModeStatus(const char *reason)
{
    const char *line1 = "USB EMERGENCY MODE!";
    const char *line2 = (reason != NULL && *reason != '\0') ? reason : NULL;
    const char *line3 = "Searching for mass:/RESCUE.ELF";
    int line1_w;
    int line2_w;
    int line3_w;
    int x1;
    int x2 = 0;
    int x3;
    int y1;
    int y2 = 0;
    int y3;
    int screen_w;
    int screen_h;
    int anchor_center_x;
    int logo_x;
    int logo_y;
    int logo_w;
    int logo_h;

    if (!SplashRenderIsActive())
        return;

    SplashRenderSetHotkeysVisible(0);
    SplashRenderBeginFrame();

    screen_w = SplashRenderGetScreenWidth();
    screen_h = SplashRenderGetScreenHeight();
    line1_w = (int)strlen(line1) * 6;
    line2_w = (line2 != NULL) ? (int)strlen(line2) * 6 : 0;
    line3_w = (int)strlen(line3) * 6;

    if (GLOBCFG.LOGO_DISP >= 2) {
        logo_x = SplashRenderGetLogoX();
        logo_y = SplashRenderGetLogoY();
        logo_w = SplashRenderGetLogoWidth();
        logo_h = SplashRenderGetLogoHeight();
        anchor_center_x = logo_x + (logo_w / 2);
        y1 = logo_y + logo_h + 2;
        if (line2 != NULL) {
            if (y1 > screen_h - 46)
                y1 = screen_h - 46;
        } else if (y1 > screen_h - 28)
            y1 = screen_h - 28;
        if (y1 < 0)
            y1 = 0;
    } else {
        anchor_center_x = SplashRenderGetScreenCenterX();
        y1 = (line2 != NULL) ? (SplashRenderGetScreenCenterY() - 20) : (SplashRenderGetScreenCenterY() - 10);
    }

    if (line2 != NULL) {
        y2 = y1 + 18;
        y3 = y2 + 18;
    } else
        y3 = y1 + 18;

    x1 = anchor_center_x - (line1_w / 2);
    if (line2 != NULL)
        x2 = anchor_center_x - (line2_w / 2);
    x3 = anchor_center_x - (line3_w / 2);
    if (x1 < 8)
        x1 = 8;
    if (line2 != NULL && x2 < 8)
        x2 = 8;
    if (x3 < 8)
        x3 = 8;
    if (x1 + line1_w > screen_w - 8)
        x1 = screen_w - line1_w - 8;
    if (line2 != NULL && x2 + line2_w > screen_w - 8)
        x2 = screen_w - line2_w - 8;
    if (x3 + line3_w > screen_w - 8)
        x3 = screen_w - line3_w - 8;
    if (x1 < 8)
        x1 = 8;
    if (line2 != NULL && x2 < 8)
        x2 = 8;
    if (x3 < 8)
        x3 = 8;

    SplashRenderDrawTextPxScaled(x1, y1, 0xff0000, line1, 1);
    if (line2 != NULL)
        SplashRenderDrawTextPxScaled(x2, y2, 0xffff00, line2, 1);
    SplashRenderDrawTextPxScaled(x3, y3, 0x00ffff, line3, 1);
    SplashRenderPresent();
}

void RestoreSplashInteractiveUi(int logo_disp,
                                const char *const hotkey_lines[KEY_COUNT],
                                const char *model,
                                const char *rom_fmt,
                                const char *dvdver,
                                const char *ps1ver,
                                const char *temp_celsius,
                                const char *source)
{
    int pass;

    if (!SplashRenderIsActive())
        return;

    SplashRenderSetHotkeysVisible(logo_disp >= 3);
    for (pass = 0; pass < 2; pass++) {
        SplashRenderBeginFrame();
        SplashRenderHotkeyLines(logo_disp, hotkey_lines);
        SplashRenderConsoleInfoLine(logo_disp,
                                    model,
                                    rom_fmt,
                                    dvdver,
                                    ps1ver,
                                    temp_celsius,
                                    "",
                                    source);
        SplashRenderHotkeyClockDate(logo_disp, 0);
        SplashRenderPresent();
    }
}

void ShowLaunchStatus(const char *path)
{
    char loading_line[160];
    const char *safe_path = (path != NULL) ? path : "";
    int is_cdvd = IsCdvdCommandToken(safe_path);

    if (!is_cdvd) {
        scr_setfontcolor(0x00ff00);
        scr_printf("  Loading %s\n", safe_path);
        return;
    }

    if (!SplashRenderIsActive()) {
        int launch_status_logo_disp = (GLOBCFG.LOGO_DISP >= 1) ? GLOBCFG.LOGO_DISP : 1;

        SplashRenderSetVideoMode(GLOBCFG.VIDEO_MODE, g_native_video_mode);
        SplashRenderTextBody(launch_status_logo_disp, g_is_psx_desr);
    }
    if (!SplashRenderIsActive())
        return;

    snprintf(loading_line, sizeof(loading_line), "Loading %s", safe_path);
    SplashDrawStatusForLaunch(GLOBCFG.LOGO_DISP, loading_line, 0x00ff00);
}
