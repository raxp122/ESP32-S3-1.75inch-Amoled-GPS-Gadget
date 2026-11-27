// input.h - touch gestures for the round screen and the two buttons.
// Events are delivered from the LVGL task (the LVGL lock is held).
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    EV_NONE = 0,
    EV_TOUCH_DOWN,     // first contact (wakes the screen)
    EV_TAP,
    EV_DOUBLE_TAP,
    EV_LONG_PRESS,     // finger still for ~0.6 s
    EV_SWIPE_UP,       // finger moves up
    EV_SWIPE_DOWN,
    EV_SWIPE_LEFT,
    EV_SWIPE_RIGHT,
    EV_PULL_DOWN,      // swipe down that starts at the top edge: quick panel
    EV_DIAL_CW,        // finger slides clockwise along the edge (virtual bezel), one event per step
    EV_DIAL_CCW,
    EV_BOOT_CLICK,
    EV_BOOT_LONG,      // BOOT held 2 s
    EV_PWR_CLICK,
    EV_PWR_LONG,       // PWR held 5 s: power off
} ev_t;

#define BOOT_LONG_MS     2000
#define PWR_LONG_MS      5000
#define HOLD_SHOW_MS     600    // a hold longer than this shows its progress ring

typedef void (*ev_handler_t)(ev_t ev, int x, int y);

void input_init(ev_handler_t handler);    // with the LVGL lock held
// touch off: no gesture at all (display mode, locked screen). The buttons keep working.
void input_set_touch(bool enabled);
bool input_touch_enabled(void);
// screens that use the double tap: single taps are delayed until the double-tap window ends
void input_set_double_tap(bool wanted);
// polling period: 12 ms with the touch in use, slower with only the buttons to watch
// (pendant mode, screen off): every wake-up costs battery
void input_set_poll_ms(int ms);
uint32_t input_idle_ms(void);             // since the last touch or button
void input_mark_activity(void);
int  input_boot_held_ms(void);            // 0 when not pressed (or already fired)
int  input_pwr_held_ms(void);
const char *ev_name(ev_t e);
