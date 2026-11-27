// ui.h - AmuletOS interface: a stack of screens, global gestures, overlays and the round menu.
#pragma once
#include <stdbool.h>
#include "lvgl.h"
#include "input.h"
#include "display.h"
#include "i18n.h"

/* ---- fonts (tools/gen_fonts.py): Poppins + Font Awesome icons ---- */
extern const lv_font_t font_xs;     // 17 px, text only (small labels)
extern const lv_font_t font_s;      // 22 px, text + icons
extern const lv_font_t font_m;      // 30 px, text + icons
extern const lv_font_t font_l;      // 44 px, text + icons
extern const lv_font_t font_xl;     // 112 px, clock digits only
extern const lv_font_t font_icon;   // 64 px, icons only

/* ---- icons (must be listed in tools/gen_fonts.py) ---- */
#define IC_CHECK      "\xEF\x80\x8C"
#define IC_CLOSE      "\xEF\x80\x8D"
#define IC_POWER      "\xEF\x80\x91"
#define IC_COG        "\xEF\x80\x93"
#define IC_HOME       "\xEF\x80\x95"
#define IC_CLOCK      "\xEF\x80\x97"
#define IC_DOWNLOAD   "\xEF\x80\x99"
#define IC_ROTATE     "\xEF\x80\x9E"
#define IC_LOCK       "\xEF\x80\xA3"
#define IC_QR         "\xEF\x80\xA9"
#define IC_IMAGE      "\xEF\x80\xBE"
#define IC_FILM       "\xEF\x80\x88"
#define IC_PLAY       "\xEF\x81\x8B"
#define IC_PAUSE      "\xEF\x81\x8C"
#define IC_LEFT       "\xEF\x81\x93"
#define IC_RIGHT      "\xEF\x81\x94"
#define IC_EYE        "\xEF\x81\xAE"
#define IC_SHUFFLE    "\xEF\x81\xB4"
#define IC_UPLOAD     "\xEF\x82\x93"
#define IC_GLOBE      "\xEF\x82\xAC"
#define IC_MAGIC      "\xEF\x83\x90"
#define IC_BOLT       "\xEF\x83\xA7"
#define IC_INFO       "\xEF\x84\xA9"
#define IC_KEYBOARD   "\xEF\x84\x9C"
#define IC_SUN        "\xEF\x86\x85"
#define IC_MOON       "\xEF\x86\x86"
#define IC_LANG       "\xEF\x86\xAB"
#define IC_SLIDERS    "\xEF\x87\x9E"
#define IC_WIFI       "\xEF\x87\xAB"
#define IC_BAT_FULL   "\xEF\x89\x80"
#define IC_BAT_3      "\xEF\x89\x81"
#define IC_BAT_2      "\xEF\x89\x82"
#define IC_BAT_1      "\xEF\x89\x83"
#define IC_BAT_0      "\xEF\x89\x84"
#define IC_HOURGLASS  "\xEF\x89\x94"
#define IC_TRASH      "\xEF\x8B\xAD"
#define IC_SYNC       "\xEF\x8B\xB1"
#define IC_GEM        "\xEF\x8E\xA5"
#define IC_PIN        "\xEF\x8F\x85"
#define IC_MOBILE     "\xEF\x8F\x8D"
#define IC_PALETTE    "\xEF\x94\xBF"
#define IC_WALK       "\xEF\x95\x94"
#define IC_SATELLITE  "\xEF\x9F\x80"
#define IC_HEART      "\xEF\x80\x84"
#define IC_STAR       "\xEF\x80\x85"
#define IC_WARN       "\xEF\x81\xB1"
#define IC_GIF        "\xEF\x87\x88"
#define IC_SD         "\xEF\x9E\xA0"
#define IC_CHIP       "\xEF\x8B\x9B"
#define IC_REFRESH    "\xEF\x80\xA1"
#define IC_DICE_D20   "\xEF\x9B\x8F"
#define IC_DICE       "\xEF\x94\xA2"
#define IC_HISTORY    "\xEF\x87\x9A"
#define IC_SAVE       "\xEF\x83\x87"
#define IC_PLUS       "\xEF\x81\xA7"
#define IC_MINUS      "\xEF\x81\xA8"
#define IC_CALENDAR   "\xEF\x81\xB3"

/* ---- colours: pure black (AMOLED pixels off) + warm whites + one accent ---- */
#define C_BG     lv_color_hex(0x000000)
#define C_TEXT   lv_color_hex(0xF4F1EA)
#define C_DIM    lv_color_hex(0x8C877D)
#define C_FAINT  lv_color_hex(0x3A3732)
#define C_CARD   lv_color_hex(0x151412)
#define C_WARN   lv_color_hex(0xFF6B57)
#define C_OK     lv_color_hex(0x6BE39A)

/* ---- screens ---- */
enum {
    SCR_NO_STATUS = 1 << 0,   // no clock at the top
    SCR_NO_SLEEP  = 1 << 1,   // the menu auto-off timer does not apply
    SCR_DTAP      = 1 << 2,   // uses the double tap (single taps arrive ~0.3 s later)
    SCR_NO_TOUCH  = 1 << 3,   // touch off while this screen is on top (pendant mode)
    SCR_NO_PULL   = 1 << 4,   // no quick panel
};

typedef struct screen_s {
    const char *name;
    void (*enter)(lv_obj_t *root, void *arg);   // builds the UI in root (called again after a pop)
    void (*leave)(void);                        // frees resources; LVGL objects are deleted for it
    bool (*event)(ev_t ev, int x, int y);       // true = handled
    void (*tick)(void);                         // every 200 ms while on top
    uint8_t flags;
} screen_t;

void ui_init(void);
void ui_push(const screen_t *s, void *arg);
void ui_pop(void);
void ui_home(void);
void ui_rebuild(void);                // rebuilds the current screen (language, accent...)
bool ui_closing(void);                // inside leave(): the screen goes away (not covered)
const screen_t *ui_current(void);
void ui_toast(const char *msg);       // LVGL lock held
void ui_toast_async(const char *msg); // from any task

/* ---- screen power ---- */
void ui_screen_off(bool lock);        // lock: touch and BOOT ignored until PWR
void ui_wake(void);
bool ui_is_off(void);
bool ui_is_locked(void);
void ui_brightness_override(int pct); // -1 = menu brightness from the settings
// the clock app keeps the screen on: true when it should not turn off now
bool ui_clock_keeps_on(void);
// ... and has not been touched for the auto-off time: dimmed, seconds stopped, Wi-Fi asleep
bool ui_clock_ambient(void);
void ui_apply_brightness(void);
void ui_power_off(void);

/* ---- look ---- */
lv_color_t ui_accent(void);
lv_color_t ui_accent_dark(void);      // the accent at ~25 %, for rings and cards
int  ui_accent_count(void);
const char *ui_accent_name(int i);
int  ui_tz_count(void);
const char *ui_tz_name(int i);
void ui_apply_tz(void);

lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *f, lv_color_t c, const char *text);
lv_obj_t *ui_circle(lv_obj_t *parent, int d, lv_color_t bg);
bool ui_set_text(lv_obj_t *label, const char *text);   // only redraws when it changes
void ui_one_line(lv_obj_t *label, int width);          // fixed width, one line, "..." at the end
void ui_battery_icon(char *buf, int n);                // icon + percentage
void ui_time_str(char *buf, int n);                    // "14:32" or "--:--"

/* ---- round menu ----
 * The current item in the middle (icon, name, value), the neighbours above and below,
 * position on an arc at the right edge. Bezel / swipe up-down move, tap selects. Items with
 * adjust(): tap enters edit mode, then bezel / swipe change the value, tap leaves. */
typedef struct {
    const char *icon;
    int str;                                // S_* label (or -1 and text)
    const char *text;
    void (*value)(char *buf, int n);        // secondary line (state / value), optional
    void (*select)(void);                   // tap
    void (*adjust)(int dir);                // makes the item adjustable
    const screen_t *open;                   // tap opens this screen
    void *arg;
    bool confirm;                           // a second tap is needed
} menu_item_t;

typedef struct {
    int title;                              // S_*
    const menu_item_t *items;
    int count;
    int sel;
} menu_t;

extern const screen_t scr_menu;             // arg = menu_t*

// A screen that is just a fixed menu (no arg needed), e.g. MENU_SCREEN(scr_info, "info", &info_menu)
#define MENU_SCREEN(var, name, menu_ptr)                                                 \
    static void var##_enter(lv_obj_t *r, void *a) { scr_menu.enter(r, (menu_ptr)); }     \
    static void var##_leave(void) { scr_menu.leave(); }                                  \
    static bool var##_event(ev_t e, int x, int y) { return scr_menu.event(e, x, y); }    \
    static void var##_tick(void) { scr_menu.tick(); }                                    \
    const screen_t var = {name, var##_enter, var##_leave, var##_event, var##_tick, 0}

/* ---- the screens (apps/) ---- */
extern const screen_t scr_home, scr_gallery, scr_pendant, scr_upload, scr_clock, scr_info,
                      scr_quick, scr_settings, scr_wifi, scr_wifi_scan, scr_wifi_portal, scr_ota,
                      scr_keyboard, scr_dice, scr_dice_favs, scr_dice_hist, scr_dice_stats,
                      scr_calendar, scr_calendar_day, scr_battery_off;
void settings_open_battery(void);   // Settings › Battery, on the power-off threshold

// the launcher's apps (for the shortcut setting)
int  home_app_count(void);
const char *home_app_name(int i);   // i: index in the app table (what the shortcut stores)
int  home_app_at(int pos);           // the app at a position of the (alphabetical) ring
int  home_app_pos(int app);
#define HOME_SHORTCUT_DEFAULT 5   // Settings

// pendant mode: start = index in the media list, slideshow or a single media
typedef struct { int start; bool slideshow; } pendant_arg_t;
void pendant_start(int index, bool slideshow);
void pendant_resume(void);                  // the last pendant setup (BOOT on the home screen)
