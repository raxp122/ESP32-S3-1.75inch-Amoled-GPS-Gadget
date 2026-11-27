// input.c - CST9217 polling, gesture recognition and buttons.
//
// Gestures, decided from where the finger lands and how it moves:
//   - lands in the outer ring and moves along the edge  -> virtual bezel (EV_DIAL_*, one per step)
//   - lands at the top edge and moves down              -> EV_PULL_DOWN (quick panel)
//   - otherwise a quick move                            -> EV_SWIPE_*
//   - no movement: tap, double tap, long press
#include "input.h"
#include "board.h"
#include "display.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "input";

#define POLL_MS         12
#define TAP_MAX_MOVE    26
#define SWIPE_MIN       48
#define LONG_MS         600
#define DTAP_MS         330     // second tap within this time (after the first release)
#define DTAP_DIST       60
#define RELEASE_POLLS   6       // finger lifted only after ~70 ms without contact (slow swipes)
#define DIAL_R_MIN      150     // the bezel ring starts this far from the centre
#define DIAL_STEP_DEG   20.0f
#define PULL_ZONE_Y     80

static ev_handler_t handler;
static lv_timer_t *poll_timer;
static int64_t last_activity_us;
static volatile bool touch_on = true, dtap_wanted;

static struct {
    bool down, moved, long_fired, dial;
    int sx, sy, lx, ly;
    int64_t t0;
    int release_cnt;
    float last_ang, acc_ang;
} g;

static struct { int64_t t; int x, y; bool pending; } tap;   // single tap waiting for a possible second
static struct { bool down, fired; int64_t t0; } boot, pwr;
static int touch_err_run;
static int64_t touch_recover_us;

/* ---------------- CST9217 ---------------- */

static bool touch_read(int *x, int *y)
{
    static const uint8_t rd[2] = {0xD0, 0x00}, ack[3] = {0xD0, 0x00, 0xAB};
    uint8_t b[15] = {0};
    if (i2c_master_transmit_receive(board_touch_dev(), rd, 2, b, sizeof(b), 20) != ESP_OK) {
        touch_err_run++;
        return false;
    }
    touch_err_run = 0;
    i2c_master_transmit(board_touch_dev(), ack, 3, 20);
    if (b[6] != 0xAB) return false;
    int n = b[5] & 0x7F;
    if (n == 0 || n > 2 || (b[0] & 0x0F) != 0x06) return false;
    int rx = (b[1] << 4) | (b[3] >> 4);
    int ry = (b[2] << 4) | (b[3] & 0x0F);
    if (rx > LCD_W - 1) rx = LCD_W - 1;
    if (ry > LCD_H - 1) ry = LCD_H - 1;
    // the panel is mounted rotated: both axes mirrored (as in the Waveshare BSP)
    *x = LCD_W - 1 - rx;
    *y = LCD_H - 1 - ry;
    display_map_touch(x, y);
    return true;
}

/* ---------------- events ---------------- */

uint32_t input_idle_ms(void) { return (uint32_t)((esp_timer_get_time() - last_activity_us) / 1000); }
void input_mark_activity(void) { last_activity_us = esp_timer_get_time(); }

static void emit(ev_t e, int x, int y)
{
    input_mark_activity();
    ESP_LOGD(TAG, "%s (%d,%d)", ev_name(e), x, y);
    if (handler) handler(e, x, y);
}

void input_set_touch(bool on)
{
    touch_on = on;
    if (!on) { g.down = false; tap.pending = false; }
}
bool input_touch_enabled(void) { return touch_on; }
void input_set_poll_ms(int ms)
{
    static int cur = POLL_MS;
    if (poll_timer && cur != ms) { cur = ms; lv_timer_set_period(poll_timer, ms); }
}

void input_set_double_tap(bool w) { dtap_wanted = w; if (!w) tap.pending = false; }

static float angle_of(int x, int y) { return atan2f((float)(y - SCR_C), (float)(x - SCR_C)) * 57.29578f; }
static int radius_of(int x, int y) { int dx = x - SCR_C, dy = y - SCR_C; return (int)sqrtf((float)(dx * dx + dy * dy)); }

static void on_release(int64_t now)
{
    if (g.long_fired || g.dial) return;
    int dx = g.lx - g.sx, dy = g.ly - g.sy;
    if (!g.moved) {
        // tap or double tap
        if (tap.pending && (now - tap.t) / 1000 < DTAP_MS && abs(g.lx - tap.x) < DTAP_DIST && abs(g.ly - tap.y) < DTAP_DIST) {
            tap.pending = false;
            emit(EV_DOUBLE_TAP, g.lx, g.ly);
            return;
        }
        if (dtap_wanted) {   // wait: it may become a double tap
            tap = (typeof(tap)){.t = now, .x = g.lx, .y = g.ly, .pending = true};
            return;
        }
        emit(EV_TAP, g.lx, g.ly);
        return;
    }
    tap.pending = false;
    if (abs(dy) > abs(dx)) {
        if (abs(dy) < SWIPE_MIN) return;
        if (dy > 0 && g.sy < PULL_ZONE_Y) emit(EV_PULL_DOWN, g.sx, g.sy);
        else emit(dy < 0 ? EV_SWIPE_UP : EV_SWIPE_DOWN, g.sx, g.sy);
    } else {
        if (abs(dx) < SWIPE_MIN) return;
        emit(dx < 0 ? EV_SWIPE_LEFT : EV_SWIPE_RIGHT, g.sx, g.sy);
    }
}

static void process_touch(int64_t now)
{
    // a pending single tap becomes a tap once the double-tap window has passed
    if (tap.pending && !g.down && (now - tap.t) / 1000 >= DTAP_MS) {
        tap.pending = false;
        emit(EV_TAP, tap.x, tap.y);
    }
    if (!touch_on) return;
    int x, y;
    bool pressed = touch_read(&x, &y);
    if (pressed) {
        g.release_cnt = 0;
        if (!g.down) {
            g = (typeof(g)){.down = true, .sx = x, .sy = y, .lx = x, .ly = y, .t0 = now, .last_ang = angle_of(x, y)};
            emit(EV_TOUCH_DOWN, x, y);
            return;
        }
        g.lx = x;
        g.ly = y;
        if (!g.moved && (abs(x - g.sx) > TAP_MAX_MOVE || abs(y - g.sy) > TAP_MAX_MOVE)) {
            g.moved = true;
            // started in the outer ring and moving along the edge rather than across it?
            if (radius_of(g.sx, g.sy) >= DIAL_R_MIN) {
                float rx = g.sx - SCR_C, ry = g.sy - SCR_C, len = sqrtf(rx * rx + ry * ry);
                float mx = x - g.sx, my = y - g.sy;
                float radial = fabsf((mx * rx + my * ry) / len);
                float tangential = fabsf((mx * -ry + my * rx) / len);
                g.dial = tangential > radial * 1.3f;
            }
        }
        if (g.dial) {
            float a = angle_of(x, y), d = a - g.last_ang;
            if (d > 180) d -= 360;
            if (d < -180) d += 360;
            g.last_ang = a;
            g.acc_ang += d;
            while (g.acc_ang >= DIAL_STEP_DEG) { g.acc_ang -= DIAL_STEP_DEG; emit(EV_DIAL_CW, x, y); }
            while (g.acc_ang <= -DIAL_STEP_DEG) { g.acc_ang += DIAL_STEP_DEG; emit(EV_DIAL_CCW, x, y); }
        } else if (!g.moved && !g.long_fired && (now - g.t0) / 1000 > LONG_MS) {
            g.long_fired = true;
            tap.pending = false;
            emit(EV_LONG_PRESS, x, y);
        }
        return;
    }
    if (!g.down) return;
    if (++g.release_cnt < RELEASE_POLLS) return;
    g.down = false;
    on_release(now);
}

static void process_btn(bool pressed, int64_t now, typeof(boot) *b, ev_t click, ev_t lng, int long_ms)
{
    if (pressed && !b->down) { b->down = true; b->t0 = now; b->fired = false; input_mark_activity(); return; }
    if (pressed && b->down && !b->fired && (now - b->t0) / 1000 >= long_ms) {
        b->fired = true;
        emit(lng, 0, 0);
        return;
    }
    if (!pressed && b->down) {
        b->down = false;
        // a hold that was released before the long time is not a click either
        if (!b->fired && (now - b->t0) / 1000 > 30 && (now - b->t0) / 1000 < HOLD_SHOW_MS) emit(click, 0, 0);
        b->fired = false;
    }
}

int input_boot_held_ms(void) { return boot.down && !boot.fired ? (int)((esp_timer_get_time() - boot.t0) / 1000) : 0; }
int input_pwr_held_ms(void) { return pwr.down && !pwr.fired ? (int)((esp_timer_get_time() - pwr.t0) / 1000) : 0; }

static void poll_cb(lv_timer_t *t)
{
    int64_t now = esp_timer_get_time();
    if (touch_err_run >= 25 && now - touch_recover_us > 3000000) {
        touch_recover_us = now;
        touch_err_run = 0;
        board_touch_recover();
    }
    process_touch(now);
    process_btn(board_btn_boot(), now, &boot, EV_BOOT_CLICK, EV_BOOT_LONG, BOOT_LONG_MS);
    process_btn(board_btn_pwr(), now, &pwr, EV_PWR_CLICK, EV_PWR_LONG, PWR_LONG_MS);
}

void input_init(ev_handler_t h)
{
    handler = h;
    input_mark_activity();
    // a PWR press that powered the board on is not a click
    pwr.down = board_btn_pwr();
    pwr.fired = true;
    poll_timer = lv_timer_create(poll_cb, POLL_MS, NULL);
}

const char *ev_name(ev_t e)
{
    static const char *n[] = {"none", "down", "tap", "double-tap", "long-press", "swipe-up", "swipe-down",
                              "swipe-left", "swipe-right", "pull-down", "dial-cw", "dial-ccw",
                              "boot", "boot-long", "pwr", "pwr-long"};
    return e < sizeof(n) / sizeof(n[0]) ? n[e] : "?";
}
