// sim_main.c - renders the real AmuletOS screens on the PC (fake hardware in stubs.c) and
// writes out/<scene>.ppm plus out/layout-report.md (text that leaves the circle).
// Run with tools/sim/run.sh after one `idf.py build` (it needs build/config/sdkconfig.h).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"
#include "ota.h"
#include "settings.h"
#include "wifi_mgr.h"

extern int64_t sim_now_us;
extern int sim_battery, sim_charging, sim_boot_held, sim_pwr_held, sim_media, sim_ota_progress;
extern bool sim_sd;
extern wifi_state_t sim_wifi;
extern ota_state_t sim_ota;
extern ev_handler_t sim_handler;

static uint16_t *fb;
static FILE *report;
static int problems;
static char scene[64];

static uint32_t tick(void) { return (uint32_t)(sim_now_us / 1000); }

static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    for (int y = a->y1; y <= a->y2; y++)
        memcpy(&fb[y * SCR_W + a->x1], px + (y * SCR_W + a->x1) * 2, (a->x2 - a->x1 + 1) * 2);
    lv_display_flush_ready(d);
}

static void run(int ms)
{
    for (int t = 0; t < ms; t += 5) {
        sim_now_us += 5000;
        lv_timer_handler();
    }
}

static void ev(ev_t e, int x, int y)
{
    sim_handler(e, x, y);
    run(350);
}

// home ring: the order of main/apps/home.c
enum { A_PENDANT, A_GALLERY, A_UPLOAD, A_DICE, A_CLOCK, A_SETTINGS, A_INFO, A_CALENDAR, A_COUNT };
static int home_sel;
static void open_app(int a)
{
    ev(EV_BOOT_LONG, 0, 0);   // home (from pendant mode: back to the menus first)
    ev(EV_BOOT_LONG, 0, 0);
    while (home_sel != a) { ev(EV_DIAL_CW, 400, 200); home_sel = (home_sel + 1) % A_COUNT; }
    ev(EV_TAP, SCR_C, SCR_C);
}

// labels whose text pokes out of the round screen
static void check_obj(lv_obj_t *o)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
    if (lv_obj_check_type(o, &lv_label_class) && lv_obj_get_style_opa(o, 0) > 10) {
        const char *t = lv_label_get_text(o);
        if (t && t[0]) {
            lv_area_t a;
            lv_obj_get_coords(o, &a);
            const lv_font_t *f = lv_obj_get_style_text_font(o, 0);
            lv_point_t sz;
            lv_text_get_size(&sz, t, f, lv_obj_get_style_text_letter_space(o, 0), lv_obj_get_style_text_line_space(o, 0),
                             lv_area_get_width(&a), LV_TEXT_FLAG_NONE);
            int cx = (a.x1 + a.x2) / 2;
            int x1 = cx - sz.x / 2, x2 = cx + sz.x / 2, y1 = a.y1, y2 = a.y1 + sz.y;
            // glyph boxes have some empty space: allow a few pixels
            int px[4] = {x1, x2, x1, x2}, py[4] = {y1 + 4, y1 + 4, y2 - 4, y2 - 4};
            for (int i = 0; i < 4; i++) {
                float d = hypotf(px[i] - SCR_C, py[i] - SCR_C);
                if (d > SCR_C + 1) {
                    fprintf(report, "- **%s**: \"%.40s\" esce dal cerchio di %.0f px\n", scene, t, d - SCR_C);
                    problems++;
                    break;
                }
            }
        }
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) check_obj(lv_obj_get_child(o, i));
}

static void shot(const char *name)
{
    run(400);
    lv_refr_now(NULL);
    snprintf(scene, sizeof(scene), "%s", name);
    check_obj(lv_screen_active());
    check_obj(lv_layer_top());
    char p[128];
    snprintf(p, sizeof(p), "out/%s.ppm", name);
    FILE *f = fopen(p, "wb");
    fprintf(f, "P6\n%d %d\n255\n", SCR_W, SCR_H);
    for (int i = 0; i < SCR_W * SCR_H; i++) {
        uint16_t c = fb[i];
        unsigned char rgb[3] = {(c >> 11) << 3, ((c >> 5) & 63) << 2, (c & 31) << 3};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("  %s\n", name);
}

int main(int argc, char **argv)
{
    g_set.brightness = 70;
    g_set.lang = LANG_IT;
    g_set.wifi_on = 1;
    g_set.ota_auto = 1;
    g_set.disp_brightness = 70;
    g_set.disp_interval_s = 30;
    g_set.disp_int_all_s = 10;
    g_set.disp_int_img_s = 60;
    g_set.disp_int_anim_s = 20;
    g_set.motion_wake = 1;
    g_set.night_from_h = 23;
    g_set.night_to_h = 7;
    g_set.disp_slideshow = 1;
    g_set.sleep_s = 0;
    strcpy(g_set.wifi_ssid, "Casa");
    g_set.dice = (dice_spec_t){1, 20, 0, 0, 0};
    g_set.dice_shake = 1;
    g_set.home_loop = 1;
    g_set.battery_mah = 500;
    g_set.home_shortcut = HOME_SHORTCUT_DEFAULT;
    report = fopen("out/layout-report.md", "w");
    fprintf(report, "# Layout report\n\nText outside the round screen (466 px circle):\n\n");

    lv_init();
    lv_tick_set_cb(tick);
    lv_display_t *d = lv_display_create(SCR_W, SCR_H);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
    fb = calloc(SCR_W * SCR_H, 2);
    lv_display_set_buffers(d, calloc(SCR_W * SCR_H, 2), NULL, SCR_W * SCR_H * 2, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(d, flush);
    ui_init();

    shot("01_home");
    sim_handler(EV_DIAL_CW, 400, 200);
    run(90);
    shot("02_home_turning");
    run(400);
    g_set.home_loop = 0;
    for (int i = 0; i < A_COUNT; i++) ev(EV_DIAL_CCW, 400, 200);   // stops at the first app
    home_sel = 0;
    shot("02b_home_stop_mode");
    g_set.home_loop = 1;
    open_app(A_GALLERY);
    shot("03_gallery");
    ev(EV_LONG_PRESS, SCR_C, SCR_C);
    shot("04_gallery_actions");
    ev(EV_BOOT_CLICK, 0, 0);
    ev(EV_TAP, SCR_C, SCR_C);
    shot("05_pendant");
    sim_boot_held = 1400;
    shot("06_pendant_exit_ring");
    sim_boot_held = 0;
    ev(EV_BOOT_LONG, 0, 0);
    open_app(A_UPLOAD);
    shot("07_upload");
    sim_wifi = WIFI_NO_NETWORK;
    open_app(A_UPLOAD);
    shot("08_upload_hotspot");
    sim_wifi = WIFI_CONNECTED;
    open_app(A_CLOCK);
    shot("09_clock");
    sim_battery = 0;   // a fuel gauge that has not learned the battery: "~" percentage
    run(5500);
    open_app(A_CLOCK);   // the face redraws once a second of real time: open it again
    shot("09b_clock_battery_estimated");
    sim_battery = 78;
    run(5500);
    open_app(A_SETTINGS);
    shot("10_settings");
    ev(EV_DIAL_CW, 400, 200);
    ev(EV_TAP, SCR_C, SCR_C);
    shot("11_settings_screen");
    ev(EV_TAP, SCR_C, SCR_C);
    shot("12_settings_brightness_edit");
    ev(EV_TAP, SCR_C, SCR_C);
    for (int i = 0; i < 5; i++) ev(EV_DIAL_CW, 400, 200);   // Shortcut at the bottom (long name)
    shot("12b_settings_long_name_dots");
    run(2500);
    shot("12c_settings_long_name_scrolling");
    for (int i = 0; i < 5; i++) ev(EV_DIAL_CCW, 400, 200);
    ev(EV_TAP, SCR_C, SCR_C);
    ev(EV_TAP, SCR_C, SCR_C);
    ev(EV_BOOT_CLICK, 0, 0);
    ev(EV_DIAL_CCW, 400, 200);
    ev(EV_TAP, SCR_C, SCR_C);
    shot("13_wifi");
    ev(EV_DIAL_CW, 400, 200);
    ev(EV_TAP, SCR_C, SCR_C);
    run(400);
    shot("14_wifi_networks");
    ev(EV_TAP, SCR_C, SCR_C);
    shot("15_keyboard");
    ev(EV_BOOT_CLICK, 0, 0);
    ev(EV_BOOT_CLICK, 0, 0);
    ev(EV_DIAL_CW, 400, 200);
    ev(EV_TAP, SCR_C, SCR_C);
    shot("16_wifi_phone");
    ev(EV_BOOT_LONG, 0, 0);
    ev(EV_PULL_DOWN, SCR_C, 30);
    shot("17_quick_panel");
    ev(EV_SWIPE_UP, SCR_C, 300);
    open_app(A_INFO);
    shot("18_info");
    sim_ota = OTA_AVAILABLE;
    open_app(A_SETTINGS);
    for (int i = 0; i < 5; i++) ev(EV_DIAL_CW, 400, 200);
    ev(EV_TAP, SCR_C, SCR_C);   // updates
    ev(EV_TAP, SCR_C, SCR_C);   // firmware
    shot("19_ota_available");
    sim_ota = OTA_DOWNLOADING;
    sim_ota_progress = 64;
    shot("20_ota_downloading");
    sim_ota = OTA_IDLE;
    ev(EV_BOOT_LONG, 0, 0);
    sim_pwr_held = 3200;
    shot("21_power_off_ring");
    sim_pwr_held = 0;
    run(100);
    sim_media = 0;
    open_app(A_GALLERY);
    shot("22_gallery_empty");
    sim_media = 7;

    // dice
    open_app(A_DICE);
    shot("30_dice_start");
    ev(EV_TAP, SCR_C, SCR_C);
    run(900);
    shot("31_dice_d20");
    ev(EV_SWIPE_UP, SCR_C, 300);       // advantage
    ev(EV_TAP, SCR_C, SCR_C);
    run(900);
    shot("32_dice_advantage");
    ev(EV_SWIPE_DOWN, SCR_C, 300);     // normal again
    for (int i = 0; i < 4; i++) ev(EV_SWIPE_RIGHT, SCR_C, 200);   // d20 -> d6
    for (int i = 0; i < 7; i++) ev(EV_DIAL_CW, 400, 200);         // 8d6 (fireball)
    ev(EV_TAP, SCR_C, SCR_C);
    run(900);
    shot("33_dice_fireball");
    ev(EV_LONG_PRESS, SCR_C, SCR_C);
    shot("34_dice_options");
    for (int i = 0; i < 5; i++) ev(EV_DIAL_CW, 400, 200);
    ev(EV_TAP, SCR_C, SCR_C);
    shot("35_dice_stats");
    ev(EV_BOOT_CLICK, 0, 0);
    ev(EV_DIAL_CW, 400, 200);
    ev(EV_TAP, SCR_C, SCR_C);
    shot("36_dice_history");
    ev(EV_BOOT_CLICK, 0, 0);
    ev(EV_BOOT_CLICK, 0, 0);
    for (int i = 0; i < 3; i++) ev(EV_SWIPE_UP, SCR_C, 300);      // Daggerheart
    shot("38_dice_daggerheart_start");
    ev(EV_DIAL_CW, 400, 200);                                     // +1
    ev(EV_TAP, SCR_C, SCR_C);
    run(900);
    shot("39_dice_daggerheart");
    ev(EV_SWIPE_UP, SCR_C, 300);                                  // + d6
    ev(EV_TAP, SCR_C, SCR_C);
    run(900);
    shot("40_dice_daggerheart_adv");
    ev(EV_SWIPE_LEFT, SCR_C, 200);                                // back to a normal die
    for (int i = 0; i < 6; i++) ev(EV_SWIPE_LEFT, SCR_C, 200);
    ev(EV_TAP, SCR_C, SCR_C);
    run(900);
    shot("37_dice_d100");
    ev(EV_BOOT_CLICK, 0, 0);

    open_app(A_CALENDAR);
    run(400);
    shot("41_calendar");
    for (int i = 0; i < 5; i++) ev(EV_DIAL_CW, 400, 200);   // across the end of the month
    run(400);
    shot("42_calendar_next_month");
    ev(EV_SWIPE_DOWN, SCR_C, 200);                           // the bezel turns the months
    ev(EV_DIAL_CW, 400, 200);
    run(400);
    shot("43_calendar_months");
    ev(EV_TAP, SCR_C, SCR_C);                                // the day page
    shot("44_calendar_day");
    ev(EV_TAP, SCR_C, SCR_C);
    ev(EV_LONG_PRESS, SCR_C, SCR_C);                         // back to today
    ev(EV_BOOT_CLICK, 0, 0);

    g_set.lang = LANG_EN;
    g_set.accent = 2;
    ui_rebuild();
    shot("23_home_english_lavender");

    if (!problems) fprintf(report, "Nothing: every text stays inside the circle.\n");
    fclose(report);
    printf("%d layout problems (out/layout-report.md)\n", problems);
    return 0;
}
