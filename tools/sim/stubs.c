// stubs.c - fake hardware for the simulator: battery, Wi-Fi, OTA, microSD and media are
// plain variables the scenes in sim_main.c can set.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "board.h"
#include "display.h"
#include "input.h"
#include "media.h"
#include "ota.h"
#include "sd.h"
#include "settings.h"
#include "ui.h"
#include "web.h"
#include "wifi_mgr.h"

int64_t sim_now_us;
int sim_battery = 78, sim_charging, sim_boot_held, sim_pwr_held;
wifi_state_t sim_wifi = WIFI_CONNECTED;
ota_state_t sim_ota = OTA_IDLE;
int sim_ota_progress;
bool sim_sd = true, sim_ap;
int sim_media = 7;
ev_handler_t sim_handler;

/* ---- esp ---- */
void esp_restart(void) { printf("esp_restart\n"); }
int64_t esp_timer_get_time(void) { return sim_now_us; }
uint32_t esp_random(void) { static uint32_t x = 12345; x = x * 1664525u + 1013904223u; return x; }

/* ---- board ---- */
float board_battery_volts(void) { return 3.95f; }
int board_battery_percent(void) { return sim_battery; }
bool board_charging(void) { return sim_charging; }
bool board_usb_power(void) { return sim_charging; }
void board_power_off(void) {}
bool board_imu_ok(void) { return true; }
bool board_imu_accel(vec3_t *g) { g->x = 0; g->y = 0; g->z = 1; return true; }
void board_gps_enable(bool on) {}

/* ---- display ---- */
static int rot;
static bool dark;
void display_set_brightness(int p) { dark = p == 0; }
bool display_is_dark(void) { return dark; }
void display_lock(void) {}
void display_unlock(void) {}
void display_set_rotation(int r) { rot = r; }
int display_rotation(void) { return rot; }
bool display_direct_on(void) { return false; }
void display_direct_begin(void) {}
void display_direct_end(void) {}

/* ---- input ---- */
void input_init(ev_handler_t h) { sim_handler = h; }
void input_set_touch(bool on) {}
bool input_touch_enabled(void) { return true; }
void input_set_double_tap(bool w) {}
uint32_t input_idle_ms(void) { return 0; }
void input_mark_activity(void) {}
int input_boot_held_ms(void) { return sim_boot_held; }
int input_pwr_held_ms(void) { return sim_pwr_held; }

/* ---- settings ---- */
settings_t g_set;
void settings_load(void) {}
void settings_save(void) {}
void settings_mark(void) {}
void settings_flush(void) {}
void settings_reset(void) {}

/* ---- wifi ---- */
wifi_state_t wifi_mgr_state(void) { return sim_wifi; }
bool wifi_mgr_conn_failed(void) { return false; }
const char *wifi_mgr_ip(void) { return sim_wifi == WIFI_CONNECTED ? "192.168.1.42" : ""; }
int wifi_mgr_rssi(void) { return -58; }
void wifi_mgr_apply(void) {}
bool wifi_mgr_connect(const char *s, const char *p) { return true; }
void wifi_mgr_forget(void) {}
void wifi_mgr_reconnect(void) {}
void wifi_mgr_scan_start(void) {}
bool wifi_mgr_scan_busy(void) { return false; }
uint32_t wifi_mgr_scan_gen(void) { static uint32_t n; return ++n; }   // "a scan finished" at every look
int wifi_mgr_scan_results(wifi_ap_t *out, int max)
{
    static const wifi_ap_t a[] = {{"Casa", -48, 3}, {"Casa-5G", -60, 3}, {"Vodafone-A1B2", -71, 3}, {"Bar Centrale", -80, 0}};
    int n = 4 < max ? 4 : max;
    memcpy(out, a, n * sizeof(wifi_ap_t));
    return n;
}
void wifi_mgr_ap_start(void) { sim_ap = true; }
void wifi_mgr_ap_stop(void) { sim_ap = false; }
bool wifi_mgr_ap_on(void) { return sim_ap; }
const char *wifi_mgr_ap_ssid(void) { return "AmuletOS-3F2A"; }
int wifi_mgr_ap_clients(void) { return 0; }
bool wifi_mgr_time_synced(void) { return true; }

/* ---- ota ---- */
void ota_mark_valid(void) {}
void ota_check(void) {}
void ota_install(void) {}
void ota_auto_start(void) {}
ota_state_t ota_state(void) { return sim_ota; }
int ota_progress(void) { return sim_ota_progress; }
const char *ota_current(void) { return "0.1.12"; }
const char *ota_latest(void) { return "0.1.15"; }
const char *ota_error(void) { return "GitHub non raggiungibile"; }

/* ---- sd / web ---- */
bool sd_mount(void) { return sim_sd; }
bool sd_ok(void) { return sim_sd; }
bool sd_space(uint64_t *t, uint64_t *f) { if (!sim_sd) return false; *t = 31900000000ULL; *f = 29400000000ULL; return true; }
static web_mode_t wmode;
void web_start(web_mode_t m) { wmode = m; }
void web_stop(void) { wmode = WEB_OFF; }
web_mode_t web_mode(void) { return wmode; }
const char *web_pin(void) { return "4821"; }
int web_uploads(void) { return 2; }
int web_progress(void) { return -1; }
const char *web_current(void) { return ""; }
bool web_client_seen(void) { return true; }
bool web_take_wifi(char *s, int sn, char *p, int pn) { return false; }

/* ---- media: a few fake items with generated previews ---- */
static media_item_t items[8] = {
    {"Tramonto_a_Positano.jpg", MEDIA_IMAGE}, {"Gatto_che_balla.amj", MEDIA_ANIM}, {"Nonna_e_io.jpg", MEDIA_IMAGE},
    {"Aurora.amj", MEDIA_ANIM}, {"Mare.jpg", MEDIA_IMAGE}, {"Fuochi.amj", MEDIA_ANIM}, {"Cuore.jpg", MEDIA_IMAGE},
};
int media_scan(void) { return sim_media; }
int media_count(void) { return sim_sd ? sim_media : 0; }
const media_item_t *media_get(int i) { return i >= 0 && i < media_count() ? &items[i] : NULL; }
int media_find(const char *f) { return 0; }
bool media_delete(int i) { return true; }
void media_display_name(const media_item_t *m, char *out, int n)
{
    strncpy(out, m->file, n - 1);
    out[n - 1] = 0;
    char *d = strrchr(out, '.');
    if (d) *d = 0;
    for (char *p = out; *p; p++) if (*p == '_') *p = ' ';
}
uint32_t media_generation(void) { return 1; }
void media_touch(void) {}
uint16_t *media_frame_alloc(int w, int h) { return calloc((size_t)w * h, 2); }
static uint16_t rgb(int r, int g, int b) { return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); }
bool media_thumb(int i, uint16_t *out)
{
    // soft gradients with a sun / moon: something photo-like to judge the layout
    static const int pal[7][6] = {{255, 140, 60, 90, 30, 110}, {40, 40, 60, 230, 200, 120}, {200, 170, 150, 80, 60, 70},
                                  {20, 60, 70, 90, 230, 160}, {30, 110, 190, 230, 240, 255}, {10, 10, 30, 255, 180, 60},
                                  {240, 80, 120, 255, 200, 210}};
    const int *p = pal[i % 7];
    for (int y = 0; y < THUMB_SIZE; y++)
        for (int x = 0; x < THUMB_SIZE; x++) {
            float t = y / (float)THUMB_SIZE;
            int r = p[0] + (p[3] - p[0]) * t, g = p[1] + (p[4] - p[1]) * t, b = p[2] + (p[5] - p[2]) * t;
            float dx = x - 150 + i * 9, dy = y - 90, d = sqrtf(dx * dx + dy * dy);
            if (d < 38) { r = 255; g = 236; b = 200; }
            else if (d < 60) { float k = (60 - d) / 22 * 0.5f; r += (255 - r) * k; g += (236 - g) * k; b += (200 - b) * k; }
            out[y * THUMB_SIZE + x] = rgb(r > 255 ? 255 : r, g > 255 ? 255 : g, b > 255 ? 255 : b);
        }
    return true;
}

/* ---- pendant: a frame on screen (the real player needs FreeRTOS and the JPEG decoder) ---- */
static uint16_t *pframe;
static lv_image_dsc_t pdsc;
static void p_enter(lv_obj_t *root, void *arg)
{
    if (!pframe) {
        pframe = calloc(466 * 466, 2);
        uint16_t *t = calloc(THUMB_SIZE * THUMB_SIZE, 2);
        media_thumb(0, t);
        for (int y = 0; y < 466; y++)
            for (int x = 0; x < 466; x++) pframe[y * 466 + x] = t[(y * THUMB_SIZE / 466) * THUMB_SIZE + x * THUMB_SIZE / 466];
        free(t);
        pdsc = (lv_image_dsc_t){.header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565, .w = 466, .h = 466, .stride = 932},
                                .data_size = 466 * 466 * 2, .data = (const uint8_t *)pframe};
    }
    lv_obj_t *im = lv_image_create(root);
    lv_image_set_src(im, &pdsc);
    lv_obj_center(im);
}
static bool p_event(ev_t e, int x, int y) { if (e == EV_BOOT_LONG) { ui_pop(); } return true; }
const screen_t scr_pendant = {"pendant", p_enter, NULL, p_event, NULL, SCR_NO_STATUS | SCR_NO_SLEEP | SCR_NO_TOUCH | SCR_NO_PULL};
void pendant_start(int index, bool slideshow) { static pendant_arg_t a; a.start = index; a.slideshow = slideshow; ui_push(&scr_pendant, &a); }
void pendant_resume(void) { pendant_start(0, true); }
