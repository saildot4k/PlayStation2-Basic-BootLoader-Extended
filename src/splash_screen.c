// Splash layout composition for hotkeys, clock/date, and console info text.
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include <libcdvd.h>
#include "OSDConfig.h"
#include "splash_render.h"
#include "splash_screen.h"

// Console info text anchor for LOGO_DISPLAY = 1-5.
#define INFO_CENTER_ADJUST_X 0
#define INFO_BOTTOM_MARGIN_PERCENT 10
#define INFO_Y_ADJUST 0
#define INFO_TEXT_COLOR 0x707070
#define INFO_COUNTDOWN_COLOR 0x15d670
#define GLYPH_ADVANCE_PX 6
#define GLYPH_HEIGHT_PX 7
#define GLYPH_SHADOW_PAD_X 1
#define GLYPH_SHADOW_PAD_Y 1
#define TEMP_TAG " TEMP: "
#define TEMP_VALUE_WIDTH_CHARS 5
#define HOTKEY_CLOCK_DATE_LINE_SPACING HOTKEY_TEXT_LINE_SPACING
#define HOTKEY_CLOCK_DATE_YEAR_BASE 2000
#define HOTKEY_CLOCK_PS2_RTC_BASE_OFFSET_MINUTES 540
#define HOTKEY_CLOCK_COUNTDOWN_MAX_CHARS 16
#define HOTKEY_CLOCK_TIME_MAX_CHARS 11
#define HOTKEY_CLOCK_DATE_MAX_CHARS 10
#define HOTKEY_CLOCK_BLOCK_MAX_CHARS HOTKEY_CLOCK_COUNTDOWN_MAX_CHARS
#define HOTKEY_CLOCK_CLEAR_EXTRA_CHARS_LEFT 1
#define HOTKEY_CLOCK_TOP_MARGIN_PERCENT 10

typedef struct
{
    int visible;
    int last_chars;
    int right_anchor_x;
    char text[HOTKEY_CLOCK_COUNTDOWN_MAX_CHARS + 1];
} CountdownState;

typedef struct
{
    int x;
    int y;
    int visible;
} TempState;

typedef struct
{
    int countdown_x;
    int countdown_y;
    int countdown_width;
    int time_x;
    int time_y;
    int time_width;
    int date_x;
    int date_y;
    int date_width;
    int visible;
    int initialized;
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int use_12h;
    int date_format;
    u64 last_tick_ms;
    int last_right_anchor[2];
    int anchor_slot;
} HotkeyClockState;

static CountdownState g_countdown = {
    .right_anchor_x = -1,
};
static TempState g_temp = {0};
static HotkeyClockState g_hotkey_clock = {
    .year = HOTKEY_CLOCK_DATE_YEAR_BASE,
    .month = 1,
    .day = 1,
    .last_right_anchor = {-1, -1},
};

// Hotkey text layout for LOGO_DISPLAY = 3-5.
// AUTO line anchors from the hotkeys image top-left.
#define HOTKEY_TEXT_LOGO_DISPLAY_MIN 3
#define HOTKEY_TEXT_LINE_COUNT KEY_COUNT
#define HOTKEY_TEXT_X_FROM_HOTKEYS_LEFT 50
#define HOTKEY_TEXT_Y_FROM_HOTKEYS_TOP 6
#define HOTKEY_TEXT_LINE_SPACING 21
#define HOTKEY_TEXT_START_LINE_INDEX (HOTKEY_TEXT_LINE_COUNT - 1)
#define HK_MAX_CHARS 70

static int clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value)
        return min_value;
    if (value > max_value)
        return max_value;
    return value;
}

static int has_text(const char *text)
{
    return (text != NULL && text[0] != '\0');
}

static int get_text_bottom_y_limit(int screen_h)
{
    int max_y = screen_h - GLYPH_HEIGHT_PX;

    if (max_y < 0)
        return 0;
    return max_y;
}

static int get_hotkey_start_line_y(void)
{
    int hotkeys_y = SplashRenderGetHotkeysY();

    if (hotkeys_y < 0)
        return -1;

    return hotkeys_y + HOTKEY_TEXT_Y_FROM_HOTKEYS_TOP + (HOTKEY_TEXT_START_LINE_INDEX * HOTKEY_TEXT_LINE_SPACING);
}

static int compute_console_info_y(int logo_disp, int screen_h)
{
    int bottom_margin_px = ((screen_h * INFO_BOTTOM_MARGIN_PERCENT) + 50) / 100;
    int y = screen_h - bottom_margin_px - GLYPH_HEIGHT_PX + INFO_Y_ADJUST;

    if (logo_disp >= HOTKEY_TEXT_LOGO_DISPLAY_MIN) {
        int start_hotkey_y = get_hotkey_start_line_y();
        if (start_hotkey_y >= 0) {
            int min_info_y = start_hotkey_y + HOTKEY_TEXT_LINE_SPACING;
            if (y < min_info_y)
                y = min_info_y;
        }
    }

    return clamp_int(y, 0, get_text_bottom_y_limit(screen_h));
}

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
    if (g_hotkey_clock.visible && SplashRenderIsActive()) {
        if (g_hotkey_clock.countdown_width > 0) {
            SplashRenderRestoreBackgroundRect(g_hotkey_clock.countdown_x,
                                              g_hotkey_clock.countdown_y,
                                              g_hotkey_clock.countdown_width + GLYPH_SHADOW_PAD_X,
                                              GLYPH_HEIGHT_PX + GLYPH_SHADOW_PAD_Y);
        }

        if (g_hotkey_clock.time_width > 0) {
            SplashRenderRestoreBackgroundRect(g_hotkey_clock.time_x,
                                              g_hotkey_clock.time_y,
                                              g_hotkey_clock.time_width + GLYPH_SHADOW_PAD_X,
                                              GLYPH_HEIGHT_PX + GLYPH_SHADOW_PAD_Y);
        }

        if (g_hotkey_clock.date_width > 0) {
            SplashRenderRestoreBackgroundRect(g_hotkey_clock.date_x,
                                              g_hotkey_clock.date_y,
                                              g_hotkey_clock.date_width + GLYPH_SHADOW_PAD_X,
                                              GLYPH_HEIGHT_PX + GLYPH_SHADOW_PAD_Y);
        }
    }

    g_hotkey_clock.visible = 0;
    g_hotkey_clock.countdown_width = 0;
    g_hotkey_clock.time_width = 0;
    g_hotkey_clock.date_width = 0;
    g_hotkey_clock.last_right_anchor[0] = -1;
    g_hotkey_clock.last_right_anchor[1] = -1;
    g_hotkey_clock.anchor_slot = 0;
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

    if (g_hotkey_clock.month < 1)
        g_hotkey_clock.month = 1;
    if (g_hotkey_clock.month > 12)
        g_hotkey_clock.month = 12;
    if (g_hotkey_clock.day < 1)
        g_hotkey_clock.day = 1;

    dim = days_in_month(g_hotkey_clock.year, g_hotkey_clock.month);
    if (g_hotkey_clock.day > dim)
        g_hotkey_clock.day = dim;
}

static void advance_hotkey_clock_seconds(u64 seconds)
{
    while (seconds > 0) {
        int dim;

        g_hotkey_clock.second++;
        if (g_hotkey_clock.second < 60) {
            seconds--;
            continue;
        }

        g_hotkey_clock.second = 0;
        g_hotkey_clock.minute++;
        if (g_hotkey_clock.minute < 60) {
            seconds--;
            continue;
        }

        g_hotkey_clock.minute = 0;
        g_hotkey_clock.hour++;
        if (g_hotkey_clock.hour < 24) {
            seconds--;
            continue;
        }

        g_hotkey_clock.hour = 0;
        g_hotkey_clock.day++;
        dim = days_in_month(g_hotkey_clock.year, g_hotkey_clock.month);
        if (g_hotkey_clock.day <= dim) {
            seconds--;
            continue;
        }

        g_hotkey_clock.day = 1;
        g_hotkey_clock.month++;
        if (g_hotkey_clock.month <= 12) {
            seconds--;
            continue;
        }

        g_hotkey_clock.month = 1;
        g_hotkey_clock.year++;
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

        g_hotkey_clock.minute++;
        if (g_hotkey_clock.minute < 60) {
            delta_minutes--;
            continue;
        }

        g_hotkey_clock.minute = 0;
        g_hotkey_clock.hour++;
        if (g_hotkey_clock.hour < 24) {
            delta_minutes--;
            continue;
        }

        g_hotkey_clock.hour = 0;
        g_hotkey_clock.day++;
        dim = days_in_month(g_hotkey_clock.year, g_hotkey_clock.month);
        if (g_hotkey_clock.day <= dim) {
            delta_minutes--;
            continue;
        }

        g_hotkey_clock.day = 1;
        g_hotkey_clock.month++;
        if (g_hotkey_clock.month <= 12) {
            delta_minutes--;
            continue;
        }

        g_hotkey_clock.month = 1;
        g_hotkey_clock.year++;
        delta_minutes--;
    }

    while (delta_minutes < 0) {
        g_hotkey_clock.minute--;
        if (g_hotkey_clock.minute >= 0) {
            delta_minutes++;
            continue;
        }

        g_hotkey_clock.minute = 59;
        g_hotkey_clock.hour--;
        if (g_hotkey_clock.hour >= 0) {
            delta_minutes++;
            continue;
        }

        g_hotkey_clock.hour = 23;
        g_hotkey_clock.day--;
        if (g_hotkey_clock.day >= 1) {
            delta_minutes++;
            continue;
        }

        g_hotkey_clock.month--;
        if (g_hotkey_clock.month < 1) {
            g_hotkey_clock.month = 12;
            g_hotkey_clock.year--;
        }
        g_hotkey_clock.day = days_in_month(g_hotkey_clock.year, g_hotkey_clock.month);
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

    g_hotkey_clock.year = HOTKEY_CLOCK_DATE_YEAR_BASE + btoi(clock_data.year);
    g_hotkey_clock.month = btoi(clock_data.month & 0x7F);
    g_hotkey_clock.day = btoi(clock_data.day);
    g_hotkey_clock.hour = btoi(clock_data.hour);
    g_hotkey_clock.minute = btoi(clock_data.minute);
    g_hotkey_clock.second = btoi(clock_data.second);
    g_hotkey_clock.use_12h = (OSDConfigGetTimeFormat() != 0);
    g_hotkey_clock.date_format = OSDConfigGetDateFormat();
    normalize_hotkey_clock_date();

    local_offset_minutes = decode_timezone_offset_minutes(OSDConfigGetTimezoneOffset());
    if (OSDConfigGetDaylightSaving() != 0)
        local_offset_minutes += 60;
    delta_minutes = local_offset_minutes - HOTKEY_CLOCK_PS2_RTC_BASE_OFFSET_MINUTES;
    shift_hotkey_clock_minutes(delta_minutes);

    g_hotkey_clock.initialized = 1;
    g_hotkey_clock.last_tick_ms = tick_ms;

    return 1;
}

void SplashRenderTextBody(int logo_disp,
                          int is_psx_desr)
{
    if (logo_disp < 1)
        return;

    (void)SplashRenderBegin(logo_disp, is_psx_desr);
}

void SplashRenderHotkeyLines(int logo_disp,
                             const char *const hotkey_lines[KEY_COUNT])
{
    int i;
    int x;
    int y;
    int hotkeys_x;
    int hotkeys_y;

    if (logo_disp < HOTKEY_TEXT_LOGO_DISPLAY_MIN || hotkey_lines == NULL)
        return;
    if (!SplashRenderIsActive())
        return;

    hotkeys_x = SplashRenderGetHotkeysX();
    hotkeys_y = SplashRenderGetHotkeysY();
    if (hotkeys_x < 0 || hotkeys_y < 0)
        return;

    x = hotkeys_x + HOTKEY_TEXT_X_FROM_HOTKEYS_LEFT;
    y = hotkeys_y + HOTKEY_TEXT_Y_FROM_HOTKEYS_TOP;
    for (i = 0; i < HOTKEY_TEXT_LINE_COUNT; i++) {
        char clamped[HK_MAX_CHARS + 1];
        copy_clamped(clamped, sizeof(clamped), hotkey_lines[i], HK_MAX_CHARS);
        if (clamped[0] == '\0')
            continue;
        SplashRenderDrawTextPxScaled(x, y + (i * HOTKEY_TEXT_LINE_SPACING), 0xffffff, clamped, 1);
    }
}

void SplashRenderHotkeyClockDate(int logo_disp, u64 tick_ms)
{
    char countdown_text[HOTKEY_CLOCK_COUNTDOWN_MAX_CHARS + 1];
    char time_text[24];
    char date_text[24];
    int hotkeys_x;
    int hotkeys_y;
    int screen_w;
    int screen_h;
    int left_anchor_x;
    int right_anchor_x;
    int max_block_width;
    int block_x;
    int has_countdown;
    int countdown_y;
    int time_y;
    int date_y;
    int countdown_width;
    int time_width;
    int date_width;
    int countdown_x;
    int time_x;
    int date_x;
    int clear_right_x;
    int clear_left_anchor_x;
    int clear_block_x;
    int clear_block_w;
    int text_bottom_y;
    int top_margin_y;
    int i;
    u64 elapsed_ms;
    u64 elapsed_seconds;

    if (!SplashRenderIsActive() || logo_disp < 1) {
        clear_hotkey_clock_date();
        g_hotkey_clock.initialized = 0;
        g_hotkey_clock.last_tick_ms = 0;
        return;
    }

    hotkeys_x = SplashRenderGetHotkeysX();
    hotkeys_y = SplashRenderGetHotkeysY();
    if (logo_disp >= HOTKEY_TEXT_LOGO_DISPLAY_MIN) {
        if (hotkeys_x < 0 || hotkeys_y < 0) {
            clear_hotkey_clock_date();
            return;
        }
    }

    if (!g_hotkey_clock.initialized) {
        if (!seed_hotkey_clock_from_ps2(tick_ms))
            return;
    } else {
        if (tick_ms < g_hotkey_clock.last_tick_ms) {
            if (!seed_hotkey_clock_from_ps2(tick_ms))
                return;
        } else {
            elapsed_ms = tick_ms - g_hotkey_clock.last_tick_ms;
            elapsed_seconds = elapsed_ms / 1000u;
            if (elapsed_seconds > 0) {
                advance_hotkey_clock_seconds(elapsed_seconds);
                g_hotkey_clock.last_tick_ms += elapsed_seconds * 1000u;
            }
        }
    }

    format_clock_time(time_text,
                      sizeof(time_text),
                      g_hotkey_clock.hour,
                      g_hotkey_clock.minute,
                      g_hotkey_clock.second,
                      g_hotkey_clock.use_12h);
    format_clock_date(date_text,
                      sizeof(date_text),
                      g_hotkey_clock.year,
                      g_hotkey_clock.month,
                      g_hotkey_clock.day,
                      g_hotkey_clock.date_format);
    copy_clamped(countdown_text, sizeof(countdown_text), g_countdown.text, HOTKEY_CLOCK_COUNTDOWN_MAX_CHARS);
    has_countdown = has_text(countdown_text);

    screen_w = SplashRenderGetScreenWidth();
    screen_h = SplashRenderGetScreenHeight();
    if (logo_disp >= HOTKEY_TEXT_LOGO_DISPLAY_MIN)
        left_anchor_x = hotkeys_x + HOTKEY_TEXT_X_FROM_HOTKEYS_LEFT;
    else
        left_anchor_x = ((screen_w * HOTKEY_CLOCK_TOP_MARGIN_PERCENT) + 50) / 100;
    right_anchor_x = screen_w - left_anchor_x;
    if (g_countdown.right_anchor_x >= 0)
        right_anchor_x = g_countdown.right_anchor_x;

    right_anchor_x = clamp_int(right_anchor_x, 0, screen_w);

    countdown_width = (int)strlen(countdown_text) * GLYPH_ADVANCE_PX;
    time_width = (int)strlen(time_text) * GLYPH_ADVANCE_PX;
    date_width = (int)strlen(date_text) * GLYPH_ADVANCE_PX;
    max_block_width = countdown_width;
    if (time_width > max_block_width)
        max_block_width = time_width;
    if (date_width > max_block_width)
        max_block_width = date_width;
    if (right_anchor_x < max_block_width)
        right_anchor_x = max_block_width;
    if (right_anchor_x > screen_w)
        right_anchor_x = screen_w;
    block_x = right_anchor_x - max_block_width;
    countdown_x = block_x;
    time_x = block_x;
    date_x = block_x;

    top_margin_y = ((screen_h * HOTKEY_CLOCK_TOP_MARGIN_PERCENT) + 50) / 100;
    if (logo_disp >= HOTKEY_TEXT_LOGO_DISPLAY_MIN)
        countdown_y = hotkeys_y + HOTKEY_TEXT_Y_FROM_HOTKEYS_TOP;
    else
        countdown_y = top_margin_y;
    time_y = countdown_y + HOTKEY_CLOCK_DATE_LINE_SPACING;
    date_y = time_y + HOTKEY_CLOCK_DATE_LINE_SPACING;
    text_bottom_y = get_text_bottom_y_limit(screen_h);
    countdown_y = clamp_int(countdown_y, 0, text_bottom_y);
    time_y = clamp_int(time_y, 0, text_bottom_y);
    date_y = clamp_int(date_y, 0, text_bottom_y);

    clear_right_x = right_anchor_x;
    clear_left_anchor_x = right_anchor_x;
    for (i = 0; i < 2; i++) {
        if (g_hotkey_clock.last_right_anchor[i] >= 0) {
            if (g_hotkey_clock.last_right_anchor[i] > clear_right_x)
                clear_right_x = g_hotkey_clock.last_right_anchor[i];
            if (g_hotkey_clock.last_right_anchor[i] < clear_left_anchor_x)
                clear_left_anchor_x = g_hotkey_clock.last_right_anchor[i];
        }
    }

    clear_block_x = clear_left_anchor_x - ((HOTKEY_CLOCK_BLOCK_MAX_CHARS + HOTKEY_CLOCK_CLEAR_EXTRA_CHARS_LEFT) * GLYPH_ADVANCE_PX);
    if (clear_block_x < 0)
        clear_block_x = 0;
    clear_block_w = clear_right_x - clear_block_x;
    if (clear_block_w > 0) {
        SplashRenderRestoreBackgroundRect(clear_block_x,
                                          countdown_y,
                                          clear_block_w + GLYPH_SHADOW_PAD_X,
                                          GLYPH_HEIGHT_PX + GLYPH_SHADOW_PAD_Y);
        SplashRenderRestoreBackgroundRect(clear_block_x,
                                          time_y,
                                          clear_block_w + GLYPH_SHADOW_PAD_X,
                                          GLYPH_HEIGHT_PX + GLYPH_SHADOW_PAD_Y);
        SplashRenderRestoreBackgroundRect(clear_block_x,
                                          date_y,
                                          clear_block_w + GLYPH_SHADOW_PAD_X,
                                          GLYPH_HEIGHT_PX + GLYPH_SHADOW_PAD_Y);
    }

    if (has_countdown)
        SplashRenderDrawTextPxScaled(countdown_x, countdown_y, INFO_COUNTDOWN_COLOR, countdown_text, 1);
    SplashRenderDrawTextPxScaled(time_x, time_y, INFO_TEXT_COLOR, time_text, 1);
    SplashRenderDrawTextPxScaled(date_x, date_y, INFO_TEXT_COLOR, date_text, 1);

    g_hotkey_clock.countdown_x = countdown_x;
    g_hotkey_clock.countdown_y = countdown_y;
    g_hotkey_clock.countdown_width = countdown_width;
    g_hotkey_clock.time_x = time_x;
    g_hotkey_clock.time_y = time_y;
    g_hotkey_clock.time_width = time_width;
    g_hotkey_clock.date_x = date_x;
    g_hotkey_clock.date_y = date_y;
    g_hotkey_clock.date_width = date_width;
    g_hotkey_clock.last_right_anchor[g_hotkey_clock.anchor_slot] = right_anchor_x;
    g_hotkey_clock.anchor_slot ^= 1;
    g_hotkey_clock.visible = 1;
}

static void build_console_info_line_text(char *info_line,
                                         size_t info_line_size,
                                         const char *model,
                                         const char *rom_fmt,
                                         const char *dvdver,
                                         const char *ps1ver,
                                         const char *temp_celsius,
                                         const char *config_source_name,
                                         int has_temp)
{
    if (has_temp) {
        char temp_padded[TEMP_VALUE_WIDTH_CHARS + 1];

        snprintf(temp_padded, sizeof(temp_padded), "%-*s", TEMP_VALUE_WIDTH_CHARS, temp_celsius);
        snprintf(info_line,
                 info_line_size,
                 "MODEL: %s  ROM: %s  DVD: %s  PS1DRV: %s" TEMP_TAG "%s  CFG SRC: %s",
                 model,
                 rom_fmt,
                 dvdver,
                 ps1ver,
                 temp_padded,
                 config_source_name);
    } else {
        snprintf(info_line,
                 info_line_size,
                 "MODEL: %s  ROM: %s  DVD: %s  PS1DRV: %s  CFG SRC: %s",
                 model,
                 rom_fmt,
                 dvdver,
                 ps1ver,
                 config_source_name);
    }
}

static void update_temp_overlay_anchor(const char *info_line, int x, int y, int has_temp)
{
    if (has_temp) {
        const char *temp_tag = strstr(info_line, TEMP_TAG);
        if (temp_tag != NULL) {
            int temp_prefix_chars = (int)(temp_tag - info_line) + (int)strlen(TEMP_TAG);
            g_temp.x = x + (temp_prefix_chars * GLYPH_ADVANCE_PX);
            g_temp.y = y;
            g_temp.visible = 1;
            return;
        }
    }

    g_temp.visible = 0;
}

static void draw_console_info_overlay(const char *info_line,
                                      const char *autoboot_countdown,
                                      int has_temp,
                                      int has_autoboot,
                                      int logo_disp)
{
    int line_chars;
    int line_width_px;
    int x;
    int screen_h = SplashRenderGetScreenHeight();
    int y = compute_console_info_y(logo_disp, screen_h);

    line_chars = (int)strlen(info_line);
    line_width_px = line_chars * GLYPH_ADVANCE_PX;
    x = SplashRenderGetScreenCenterX() - (line_width_px / 2) + INFO_CENTER_ADJUST_X;

    SplashRenderDrawTextPxScaled(x, y, INFO_TEXT_COLOR, info_line, 1);
    g_countdown.right_anchor_x = x + line_width_px;

    update_temp_overlay_anchor(info_line, x, y, has_temp);

    if (logo_disp >= 1 && has_autoboot)
        copy_clamped(g_countdown.text, sizeof(g_countdown.text), autoboot_countdown, HOTKEY_CLOCK_COUNTDOWN_MAX_CHARS);
    else
        g_countdown.text[0] = '\0';
    g_countdown.last_chars = (int)strlen(g_countdown.text);
    g_countdown.visible = (g_countdown.last_chars > 0);
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
    int has_temp = has_text(temp_celsius);
    int has_autoboot = has_text(autoboot_countdown);

    if (logo_disp <= 0)
        return;

    build_console_info_line_text(info_line,
                                 sizeof(info_line),
                                 model,
                                 rom_fmt,
                                 dvdver,
                                 ps1ver,
                                 temp_celsius,
                                 config_source_name,
                                 has_temp);

    if (SplashRenderIsActive())
        draw_console_info_overlay(info_line, autoboot_countdown, has_temp, has_autoboot, logo_disp);
}

void SplashRenderConsoleInfoCountdownOnly(const char *autoboot_countdown)
{
    if (autoboot_countdown == NULL)
        autoboot_countdown = "";
    copy_clamped(g_countdown.text, sizeof(g_countdown.text), autoboot_countdown, HOTKEY_CLOCK_COUNTDOWN_MAX_CHARS);
    g_countdown.last_chars = (int)strlen(g_countdown.text);
    g_countdown.visible = (g_countdown.last_chars > 0);
}

void SplashRenderConsoleInfoTemperatureOnly(const char *temp_celsius)
{
    char temp_padded[TEMP_VALUE_WIDTH_CHARS + 1];

    if (!g_temp.visible || !SplashRenderIsActive())
        return;

    if (temp_celsius == NULL)
        temp_celsius = "";

    snprintf(temp_padded, sizeof(temp_padded), "%-*s", TEMP_VALUE_WIDTH_CHARS, temp_celsius);
    SplashRenderRestoreBackgroundRect(g_temp.x,
                                      g_temp.y,
                                      TEMP_VALUE_WIDTH_CHARS * GLYPH_ADVANCE_PX + GLYPH_SHADOW_PAD_X,
                                      GLYPH_HEIGHT_PX + GLYPH_SHADOW_PAD_Y);
    SplashRenderDrawTextPxScaled(g_temp.x, g_temp.y, INFO_TEXT_COLOR, temp_padded, 1);
}
