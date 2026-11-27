// settings.h - persistent settings (NVS namespace "amulet", one blob).
// New fields go at the END of the struct: an older blob is read over the defaults, so the
// fields it does not have keep their default values (no migration needed).
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "core.h"

#define DICE_FAVS 6

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
    // dice
    dice_spec_t dice;           // the current roll
    uint8_t  dice_shake;        // shake the pendant to roll
    dice_spec_t dice_fav[DICE_FAVS];   // saved rolls (count 0 = empty)
    // power (0.1.7)
    uint8_t  home_loop;         // home list: 1 goes round, 0 stops at the ends
    uint8_t  power_save;        // CPU frequency scaling + light sleep (not on USB power)
    uint8_t  wifi_save;         // Wi-Fi off in pendant mode and with the screen off
    uint16_t battery_mah;       // battery capacity: charge current, warnings
    uint8_t  battery_care;      // charge to 4.1 V (~90 %) instead of 4.2 V
    // 0.1.16
    uint8_t  home_shortcut;     // app on the fixed button at the bottom of the home ring (home.c order)
    // 0.1.21
    uint8_t  clock_on;          // the clock app keeps the screen on (dimmed when not touched)
    // 0.1.3x: what pendant mode plays and a slideshow interval for each choice
    // (disp_interval_s above is no longer used)
    uint8_t  disp_filter;       // MEDIA_FILTER_ALL / _IMAGES / _ANIM
    uint16_t disp_int_all_s;    // slideshow interval: everything
    uint16_t disp_int_img_s;    //                     images only
    uint16_t disp_int_anim_s;   //                     GIFs and videos only
} settings_t;

extern settings_t g_set;

void settings_load(void);
void settings_save(void);
void settings_mark(void);    // changed: saved at the next settings_flush() (menus leaving)
void settings_flush(void);
uint16_t *settings_interval(int filter);   // the slideshow interval (s) for a media filter
void settings_reset(void);
