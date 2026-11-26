// settings.h - persistent settings (NVS namespace "amulet", one blob).
// New fields go at the END of the struct: an older blob is read over the defaults, so the
// fields it does not have keep their default values (no migration needed).
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    // screen
    uint8_t  brightness;        // 5-100, menus
    uint8_t  rotation;          // 0-3: 0/90/180/270 degrees, how the board sits in the case
    uint8_t  accent;            // index in the accent palette (ui.c)
    uint8_t  lang;              // lang_t (i18n.h)
    uint16_t sleep_s;           // menus: screen off after this idle time (0 = never)
    // wi-fi
    uint8_t  wifi_on;
    char     wifi_ssid[33];
    char     wifi_pass[65];
    uint8_t  ota_auto;          // check for a new release when internet is available
    uint8_t  tz;                // index in the time-zone table (ui.c)
    // pendant (display mode)
    uint8_t  disp_brightness;   // 5-100
    uint16_t disp_interval_s;   // slideshow: time per media
    uint8_t  disp_shuffle;
    uint16_t disp_off_min;      // screen off after this many minutes (0 = always on)
    uint8_t  motion_wake;       // the timer counts from the last movement; moving wakes the screen
    uint8_t  night_on;
    uint8_t  night_from_h;      // 0-23
    uint8_t  night_to_h;        // 0-23
    uint8_t  night_mode;        // 0 = dim, 1 = screen off
    uint8_t  disp_slideshow;    // last display mode: 1 slideshow, 0 single media
    char     disp_last[48];     // last media shown (file name)
    uint8_t  gps_on;            // -G version: GPS powered (unused for now)
} settings_t;

extern settings_t g_set;

void settings_load(void);
void settings_save(void);
void settings_mark(void);    // changed: saved at the next settings_flush() (menus leaving)
void settings_flush(void);
void settings_reset(void);
