#include <debug.h>
#include <stdio.h>
#include <string.h>
#include <libcdvd.h>
#include "OSDConfig.h"
#include "splash_render.h"
#include "splash_screen.h"

// Console info text anchor for LOGO_DISPLAY = 2-5.
#define INFO_CENTER_ADJUST_X 0
#define INFO_BOTTOM_MARGIN_PERCENT 10
#define INFO_Y_ADJUST 0
#define INFO_TEXT_COLOR 0x707070
#define INFO_AUTOBOOT_COLOR 0xffff00
#define GLYPH_ADVANCE_PX 6
#define GLYPH_HEIGHT_PX 7
#define GLYPH_SHADOW_PAD_X 1
#define GLYPH_SHADOW_PAD_Y 1
#define AUTOBOOT_PREFIX "  AUTO: "
#define AUTOBOOT_VALUE_DEFAULT_WIDTH 5
#define TEMP_TAG " TEMP: "
#define TEMP_VALUE_WIDTH_CHARS 5
#define HOTKEY_CLOCK_DATE_LINE_SPACING HOTKEY_TEXT_LINE_SPACING
#define HOTKEY_CLOCK_DATE_YEAR_BASE 2000
#define HOTKEY_CLOCK_PS2_RTC_BASE_OFFSET_MINUTES 540
#define HOTKEY_CLOCK_TIME_MAX_CHARS 11
#define HOTKEY_CLOCK_DATE_MAX_CHARS 10

static int g_countdown_x = 0;
static int g_countdown_y = 0;
static int g_countdown_visible = 0;
static int g_last_countdown_chars = 0;
static int g_temp_x = 0;
static int g_temp_y = 0;
static int g_temp_visible = 0;
static int g_hotkey_time_x = 0;
static int g_hotkey_time_y = 0;
static int g_hotkey_time_width = 0;
static int g_hotkey_date_x = 0;
static int g_hotkey_date_y = 0;
static int g_hotkey_date_width = 0;
static int g_hotkey_clock_visible = 0;
static int g_hotkey_clock_initialized = 0;
static int g_hotkey_clock_year = HOTKEY_CLOCK_DATE_YEAR_BASE;
static int g_hotkey_clock_month = 1;
static int g_hotkey_clock_day = 1;
static int g_hotkey_clock_hour = 0;
static int g_hotkey_clock_minute = 0;
static int g_hotkey_clock_second = 0;
static int g_hotkey_clock_use_12h = 0;
static int g_hotkey_clock_date_format = 0;
static u64 g_hotkey_clock_last_tick_ms = 0;
static int g_hotkey_clock_last_right_anchor = -1;

// Hotkey text layout for LOGO_DISPLAY = 3-5.
// AUTO line anchors from the hotkeys image top-left.
#define HOTKEY_TEXT_X_FROM_HOTKEYS_LEFT 50
#define HOTKEY_TEXT_Y_FROM_HOTKEYS_TOP 6
#define HOTKEY_TEXT_LINE_SPACING 21
#define HK_MAX_CHARS 70

static void copy_clamped(char *dst, size_t dst_size, const char *src, int max_chars)
{
    size_t i = 0;

    if (dst == NULL || dst_size == 0)
        return;

    if (src == NULL)
        src = "";

    while (src[i] != '\0' && (int)i < max_chars && (i + 1) < dst_size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void clear_hotkey_clock_date(void)
{
    if (g_hotkey_clock_visible && SplashRenderIsActive()) {
        if (g_hotkey_time_width > 0) {
            SplashRenderRestoreBackgroundRect(g_hotkey_time_x,
                                              g_hotkey_time_y,
                                              g_hotkey_time_width + GLYPH_SHADOW_PAD_X,
                                              GLYPH_HEIGHT_PX + GLYPH_SHADOW_PAD_Y);
        }

        if (g_hotkey_date_width > 0) {
            SplashRenderRestoreBackgroundRect(g_hotkey_date_x,
                                              g_hotkey_date_y,
                                              g_hotkey_date_width + GLYPH_SHADOW_PAD_X,
                                              GLYPH_HEIGHT_PX + GLYPH_SHADOW_PAD_Y);
        }
    }

    g_hotkey_clock_visible = 0;
    g_hotkey_time_width = 0;
    g_hotkey_date_width = 0;
    g_hotkey_clock_last_right_anchor = -1;
}

static void format_clock_time(char *dst, size_t dst_size, int hour, int minute, int second, int use_12h)
{
    if (use_12h) {
        const char *suffix = (hour >= 12) ? "PM" : "AM";
        int hour12 = hour % 12;
        if (hour12 == 0)
            hour12 = 12;
        snprintf(dst, dst_size, "%02d:%02d:%02d %s", hour12, minute, second, suffix);
    } else {
        snprintf(dst, dst_size, "%02d:%02d:%02d", hour, minute, second);
    }
}

static void format_clock_date(char *dst, size_t dst_size, int year, int month, int day, int date_format)
{
    switch (date_format) {
        case 1:
            snprintf(dst, dst_size, "%02d/%02d/%04d", month, day, year);
            break;
        case 2:
            snprintf(dst, dst_size, "%02d/%02d/%04d", day, month, year);
            break;
        default:
            snprintf(dst, dst_size, "%04d/%02d/%02d", year, month, day);
            break;
    }
}

static int is_leap_year(int year)
{
    if ((year % 400) == 0)
        return 1;
    if ((year % 100) == 0)
        return 0;
    return ((year % 4) == 0);
}

static int days_in_month(int year, int month)
{
    static const int month_days[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    if (month < 1 || month > 12)
        return 31;

    if (month == 2 && is_leap_year(year))
        return 29;

    return month_days[month - 1];
}

static void normalize_hotkey_clock_date(void)
{
    int dim;

    if (g_hotkey_clock_month < 1)
        g_hotkey_clock_month = 1;
    if (g_hotkey_clock_month > 12)
        g_hotkey_clock_month = 12;
    if (g_hotkey_clock_day < 1)
        g_hotkey_clock_day = 1;

    dim = days_in_month(g_hotkey_clock_year, g_hotkey_clock_month);
    if (g_hotkey_clock_day > dim)
        g_hotkey_clock_day = dim;
}

static void advance_hotkey_clock_seconds(u64 seconds)
{
    while (seconds > 0) {
        int dim;

        g_hotkey_clock_second++;
        if (g_hotkey_clock_second < 60) {
            seconds--;
            continue;
        }

        g_hotkey_clock_second = 0;
        g_hotkey_clock_minute++;
        if (g_hotkey_clock_minute < 60) {
            seconds--;
            continue;
        }

        g_hotkey_clock_minute = 0;
        g_hotkey_clock_hour++;
        if (g_hotkey_clock_hour < 24) {
            seconds--;
            continue;
        }

        g_hotkey_clock_hour = 0;
        g_hotkey_clock_day++;
        dim = days_in_month(g_hotkey_clock_year, g_hotkey_clock_month);
        if (g_hotkey_clock_day <= dim) {
            seconds--;
            continue;
        }

        g_hotkey_clock_day = 1;
        g_hotkey_clock_month++;
        if (g_hotkey_clock_month <= 12) {
            seconds--;
            continue;
        }

        g_hotkey_clock_month = 1;
        g_hotkey_clock_year++;
        seconds--;
    }
}

static int decode_timezone_offset_minutes(int raw_offset)
{
    int value = raw_offset & 0x7FF;

    // The OSD stores timezoneOffset in an 11-bit signed field.
    if (value & 0x400)
        value -= 0x800;

    return value;
}

static void shift_hotkey_clock_minutes(int delta_minutes)
{
    while (delta_minutes > 0) {
        int dim;

        g_hotkey_clock_minute++;
        if (g_hotkey_clock_minute < 60) {
            delta_minutes--;
            continue;
        }

        g_hotkey_clock_minute = 0;
        g_hotkey_clock_hour++;
        if (g_hotkey_clock_hour < 24) {
            delta_minutes--;
            continue;
        }

        g_hotkey_clock_hour = 0;
        g_hotkey_clock_day++;
        dim = days_in_month(g_hotkey_clock_year, g_hotkey_clock_month);
        if (g_hotkey_clock_day <= dim) {
            delta_minutes--;
            continue;
        }

        g_hotkey_clock_day = 1;
        g_hotkey_clock_month++;
        if (g_hotkey_clock_month <= 12) {
            delta_minutes--;
            continue;
        }

        g_hotkey_clock_month = 1;
        g_hotkey_clock_year++;
        delta_minutes--;
    }

    while (delta_minutes < 0) {
        g_hotkey_clock_minute--;
        if (g_hotkey_clock_minute >= 0) {
            delta_minutes++;
            continue;
        }

        g_hotkey_clock_minute = 59;
        g_hotkey_clock_hour--;
        if (g_hotkey_clock_hour >= 0) {
            delta_minutes++;
            continue;
        }

        g_hotkey_clock_hour = 23;
        g_hotkey_clock_day--;
        if (g_hotkey_clock_day >= 1) {
            delta_minutes++;
            continue;
        }

        g_hotkey_clock_month--;
        if (g_hotkey_clock_month < 1) {
            g_hotkey_clock_month = 12;
            g_hotkey_clock_year--;
        }
        g_hotkey_clock_day = days_in_month(g_hotkey_clock_year, g_hotkey_clock_month);
        delta_minutes++;
    }
}

static int seed_hotkey_clock_from_ps2(u64 tick_ms)
{
    sceCdCLOCK clock_data;
    int local_offset_minutes;
    int delta_minutes;

    if (!sceCdReadClock(&clock_data))
        return 0;

    g_hotkey_clock_year = HOTKEY_CLOCK_DATE_YEAR_BASE + btoi(clock_data.year);
    g_hotkey_clock_month = btoi(clock_data.month & 0x7F);
    g_hotkey_clock_day = btoi(clock_data.day);
    g_hotkey_clock_hour = btoi(clock_data.hour);
    g_hotkey_clock_minute = btoi(clock_data.minute);
    g_hotkey_clock_second = btoi(clock_data.second);
    g_hotkey_clock_use_12h = (OSDConfigGetTimeFormat() != 0);
    g_hotkey_clock_date_format = OSDConfigGetDateFormat();
    normalize_hotkey_clock_date();

    local_offset_minutes = decode_timezone_offset_minutes(OSDConfigGetTimezoneOffset());
    if (OSDConfigGetDaylightSaving() != 0)
        local_offset_minutes += 60;
    delta_minutes = local_offset_minutes - HOTKEY_CLOCK_PS2_RTC_BASE_OFFSET_MINUTES;
    shift_hotkey_clock_minutes(delta_minutes);

    g_hotkey_clock_initialized = 1;
    g_hotkey_clock_last_tick_ms = tick_ms;

    return 1;
}

void SplashRenderTextBody(int logo_disp,
                          int is_psx_desr)
{
    if (logo_disp < 2)
        return;

    if (!SplashRenderBegin(logo_disp, is_psx_desr))
        return;
}

void SplashRenderHotkeyLines(int logo_disp,
                             const char *const hotkey_lines[17])
{
    int i;
    int x;
    int y;
    int hotkeys_x;
    int hotkeys_y;

    if (logo_disp < 3 || hotkey_lines == NULL)
        return;
    if (!SplashRenderIsActive())
        return;

    hotkeys_x = SplashRenderGetHotkeysX();
    hotkeys_y = SplashRenderGetHotkeysY();
    if (hotkeys_x < 0 || hotkeys_y < 0)
        return;

    x = hotkeys_x + HOTKEY_TEXT_X_FROM_HOTKEYS_LEFT;
    y = hotkeys_y + HOTKEY_TEXT_Y_FROM_HOTKEYS_TOP;
    for (i = 0; i < 17; i++) {
        char clamped[HK_MAX_CHARS + 1];
        copy_clamped(clamped, sizeof(clamped), hotkey_lines[i], HK_MAX_CHARS);
        if (clamped[0] == '\0')
            continue;
        SplashRenderDrawTextPxScaled(x, y + (i * HOTKEY_TEXT_LINE_SPACING), 0xffffff, clamped, 1);
    }
}

void SplashRenderHotkeyClockDate(int logo_disp, u64 tick_ms)
{
    char time_text[24];
    char date_text[24];
    int hotkeys_x;
    int hotkeys_y;
    int screen_w;
    int screen_h;
    int left_anchor_x;
    int right_anchor_x;
    int countdown_chars;
    int time_y;
    int date_y;
    int time_width;
    int date_width;
    int time_x;
    int date_x;
    int clear_right_x;
    int clear_left_anchor_x;
    int clear_time_x;
    int clear_date_x;
    int clear_time_w;
    int clear_date_w;
    u64 elapsed_ms;
    u64 elapsed_seconds;

    if (!SplashRenderIsActive() || logo_disp < 3) {
        clear_hotkey_clock_date();
        g_hotkey_clock_initialized = 0;
        g_hotkey_clock_last_tick_ms = 0;
        return;
    }

    hotkeys_x = SplashRenderGetHotkeysX();
    hotkeys_y = SplashRenderGetHotkeysY();
    if (hotkeys_x < 0 || hotkeys_y < 0) {
        clear_hotkey_clock_date();
        return;
    }

    if (!g_hotkey_clock_initialized) {
        if (!seed_hotkey_clock_from_ps2(tick_ms))
            return;
    } else {
        if (tick_ms < g_hotkey_clock_last_tick_ms) {
            if (!seed_hotkey_clock_from_ps2(tick_ms))
                return;
        } else {
            elapsed_ms = tick_ms - g_hotkey_clock_last_tick_ms;
            elapsed_seconds = elapsed_ms / 1000u;
            if (elapsed_seconds > 0) {
                advance_hotkey_clock_seconds(elapsed_seconds);
                g_hotkey_clock_last_tick_ms += elapsed_seconds * 1000u;
            }
        }
    }

    format_clock_time(time_text,
                      sizeof(time_text),
                      g_hotkey_clock_hour,
                      g_hotkey_clock_minute,
                      g_hotkey_clock_second,
                      g_hotkey_clock_use_12h);
    format_clock_date(date_text,
                      sizeof(date_text),
                      g_hotkey_clock_year,
                      g_hotkey_clock_month,
                      g_hotkey_clock_day,
                      g_hotkey_clock_date_format);

    screen_w = SplashRenderGetScreenWidth();
    screen_h = SplashRenderGetScreenHeight();
    if (g_countdown_visible) {
        countdown_chars = g_last_countdown_chars;
        if (countdown_chars < AUTOBOOT_VALUE_DEFAULT_WIDTH)
            countdown_chars = AUTOBOOT_VALUE_DEFAULT_WIDTH;
        right_anchor_x = g_countdown_x + (countdown_chars * GLYPH_ADVANCE_PX);
    } else {
        left_anchor_x = hotkeys_x + HOTKEY_TEXT_X_FROM_HOTKEYS_LEFT;
        right_anchor_x = screen_w - left_anchor_x;
    }

    if (right_anchor_x > screen_w)
        right_anchor_x = screen_w;
    if (right_anchor_x < 0)
        right_anchor_x = 0;

    time_width = (int)strlen(time_text) * GLYPH_ADVANCE_PX;
    date_width = (int)strlen(date_text) * GLYPH_ADVANCE_PX;
    time_x = right_anchor_x - time_width;
    if (time_x < 0)
        time_x = 0;
    date_x = right_anchor_x - date_width;
    if (date_x < 0)
        date_x = 0;

    time_y = hotkeys_y + HOTKEY_TEXT_Y_FROM_HOTKEYS_TOP;
    date_y = time_y + HOTKEY_CLOCK_DATE_LINE_SPACING;
    if (time_y < 0)
        time_y = 0;
    if (date_y < 0)
        date_y = 0;
    if (time_y > screen_h - GLYPH_HEIGHT_PX)
        time_y = screen_h - GLYPH_HEIGHT_PX;
    if (date_y > screen_h - GLYPH_HEIGHT_PX)
        date_y = screen_h - GLYPH_HEIGHT_PX;

    clear_right_x = right_anchor_x;
    clear_left_anchor_x = right_anchor_x;
    if (g_hotkey_clock_last_right_anchor >= 0) {
        if (g_hotkey_clock_last_right_anchor > clear_right_x)
            clear_right_x = g_hotkey_clock_last_right_anchor;
        if (g_hotkey_clock_last_right_anchor < clear_left_anchor_x)
            clear_left_anchor_x = g_hotkey_clock_last_right_anchor;
    }

    clear_time_x = clear_left_anchor_x - (HOTKEY_CLOCK_TIME_MAX_CHARS * GLYPH_ADVANCE_PX);
    if (clear_time_x < 0)
        clear_time_x = 0;
    clear_date_x = clear_left_anchor_x - (HOTKEY_CLOCK_DATE_MAX_CHARS * GLYPH_ADVANCE_PX);
    if (clear_date_x < 0)
        clear_date_x = 0;

    clear_time_w = clear_right_x - clear_time_x;
    clear_date_w = clear_right_x - clear_date_x;
    if (clear_time_w > 0) {
        SplashRenderRestoreBackgroundRect(clear_time_x,
                                          time_y,
                                          clear_time_w + GLYPH_SHADOW_PAD_X,
                                          GLYPH_HEIGHT_PX + GLYPH_SHADOW_PAD_Y);
    }
    if (clear_date_w > 0) {
        SplashRenderRestoreBackgroundRect(clear_date_x,
                                          date_y,
                                          clear_date_w + GLYPH_SHADOW_PAD_X,
                                          GLYPH_HEIGHT_PX + GLYPH_SHADOW_PAD_Y);
    }

    SplashRenderDrawTextPxScaled(time_x, time_y, INFO_TEXT_COLOR, time_text, 1);
    SplashRenderDrawTextPxScaled(date_x, date_y, INFO_TEXT_COLOR, date_text, 1);

    g_hotkey_time_x = time_x;
    g_hotkey_time_y = time_y;
    g_hotkey_time_width = time_width;
    g_hotkey_date_x = date_x;
    g_hotkey_date_y = date_y;
    g_hotkey_date_width = date_width;
    g_hotkey_clock_last_right_anchor = right_anchor_x;
    g_hotkey_clock_visible = 1;
}

void SplashRenderConsoleInfoLine(int logo_disp,
                                 const char *model,
                                 const char *rom_fmt,
                                 const char *dvdver,
                                 const char *ps1ver,
                                 const char *temp_celsius,
                                 const char *autoboot_countdown,
                                 const char *config_source_name)
{
    char info_line[320];
    char temp_padded[TEMP_VALUE_WIDTH_CHARS + 1];

    if (logo_disp <= 0)
        return;

    if (temp_celsius != NULL && temp_celsius[0] != '\0') {
        snprintf(temp_padded, sizeof(temp_padded), "%-*s", TEMP_VALUE_WIDTH_CHARS, temp_celsius);
        snprintf(info_line,
                 sizeof(info_line),
                 "MODEL: %s  ROM: %s  DVD: %s  PS1DRV: %s" TEMP_TAG "%s  CFG SRC: %s",
                 model,
                 rom_fmt,
                 dvdver,
                 ps1ver,
                 temp_padded,
                 config_source_name);
    } else {
        snprintf(info_line,
                 sizeof(info_line),
                 "MODEL: %s  ROM: %s  DVD: %s  PS1DRV: %s  CFG SRC: %s",
                 model,
                 rom_fmt,
                 dvdver,
                 ps1ver,
                 config_source_name);
    }

    if (logo_disp == 1) {
        g_countdown_visible = 0;
        g_last_countdown_chars = 0;
        g_temp_visible = 0;
        if (autoboot_countdown != NULL && autoboot_countdown[0] != '\0')
            scr_printf("\n%s%s", info_line, autoboot_countdown);
        else
            scr_printf("\n%s", info_line);
        return;
    }

    if (SplashRenderIsActive()) {
        int line_chars;
        int countdown_layout_chars;
        int line_width_px;
        int suffix_x;
        int prefix_width_px;
        int x;
        int screen_h = SplashRenderGetScreenHeight();
        int bottom_margin_px = ((screen_h * INFO_BOTTOM_MARGIN_PERCENT) + 50) / 100;
        int y = screen_h - bottom_margin_px - GLYPH_HEIGHT_PX + INFO_Y_ADJUST;

        countdown_layout_chars = AUTOBOOT_VALUE_DEFAULT_WIDTH;
        if (autoboot_countdown != NULL && autoboot_countdown[0] != '\0') {
            int provided_chars = (int)strlen(autoboot_countdown);
            if (provided_chars > countdown_layout_chars)
                countdown_layout_chars = provided_chars;
        }

        line_chars = (int)strlen(info_line) + (int)strlen(AUTOBOOT_PREFIX) + countdown_layout_chars;
        line_width_px = line_chars * GLYPH_ADVANCE_PX;
        x = SplashRenderGetScreenCenterX() - (line_width_px / 2) + INFO_CENTER_ADJUST_X;

        SplashRenderDrawTextPxScaled(x, y, INFO_TEXT_COLOR, info_line, 1);

        suffix_x = x + ((int)strlen(info_line) * GLYPH_ADVANCE_PX);
        SplashRenderDrawTextPxScaled(suffix_x, y, INFO_AUTOBOOT_COLOR, AUTOBOOT_PREFIX, 1);
        prefix_width_px = (int)strlen(AUTOBOOT_PREFIX) * GLYPH_ADVANCE_PX;
        g_countdown_x = suffix_x + prefix_width_px;
        g_countdown_y = y;
        g_countdown_visible = 1;
        g_last_countdown_chars = 0;

        if (temp_celsius != NULL && temp_celsius[0] != '\0') {
            const char *temp_tag = strstr(info_line, TEMP_TAG);
            if (temp_tag != NULL) {
                int temp_prefix_chars = (int)(temp_tag - info_line) + (int)strlen(TEMP_TAG);
                g_temp_x = x + (temp_prefix_chars * GLYPH_ADVANCE_PX);
                g_temp_y = y;
                g_temp_visible = 1;
            } else {
                g_temp_visible = 0;
            }
        } else {
            g_temp_visible = 0;
        }

        if (autoboot_countdown == NULL || autoboot_countdown[0] == '\0')
            return;

        SplashRenderDrawTextPxScaled(g_countdown_x, y, INFO_AUTOBOOT_COLOR, autoboot_countdown, 1);
        g_last_countdown_chars = (int)strlen(autoboot_countdown);
    }
}

void SplashRenderConsoleInfoCountdownOnly(const char *autoboot_countdown)
{
    int countdown_chars;
    int clear_chars;

    if (!g_countdown_visible || !SplashRenderIsActive())
        return;

    if (autoboot_countdown == NULL)
        autoboot_countdown = "";

    countdown_chars = (int)strlen(autoboot_countdown);
    clear_chars = (countdown_chars > g_last_countdown_chars) ? countdown_chars : g_last_countdown_chars;
    if (clear_chars > 0) {
        SplashRenderRestoreBackgroundRect(g_countdown_x,
                                          g_countdown_y,
                                          clear_chars * GLYPH_ADVANCE_PX + GLYPH_SHADOW_PAD_X,
                                          GLYPH_HEIGHT_PX + GLYPH_SHADOW_PAD_Y);
    }

    if (countdown_chars > 0)
        SplashRenderDrawTextPxScaled(g_countdown_x, g_countdown_y, INFO_AUTOBOOT_COLOR, autoboot_countdown, 1);

    g_last_countdown_chars = countdown_chars;
}

void SplashRenderConsoleInfoTemperatureOnly(const char *temp_celsius)
{
    char temp_padded[TEMP_VALUE_WIDTH_CHARS + 1];

    if (!g_temp_visible || !SplashRenderIsActive())
        return;

    if (temp_celsius == NULL)
        temp_celsius = "";

    snprintf(temp_padded, sizeof(temp_padded), "%-*s", TEMP_VALUE_WIDTH_CHARS, temp_celsius);
    SplashRenderRestoreBackgroundRect(g_temp_x,
                                      g_temp_y,
                                      TEMP_VALUE_WIDTH_CHARS * GLYPH_ADVANCE_PX + GLYPH_SHADOW_PAD_X,
                                      GLYPH_HEIGHT_PX + GLYPH_SHADOW_PAD_Y);
    SplashRenderDrawTextPxScaled(g_temp_x, g_temp_y, INFO_TEXT_COLOR, temp_padded, 1);
}
