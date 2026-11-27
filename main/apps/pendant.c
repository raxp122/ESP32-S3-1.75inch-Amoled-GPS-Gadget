// pendant.c - pendant (display) mode: one media in a loop or a slideshow, full screen.
// The content setting limits it to images, or to GIFs and videos, each with its own interval.
// Touch is off. BOOT: next media. BOOT held 2 s: back to the menus. PWR: screen off + lock.
//
// A player task decodes JPEG frames (esp_new_jpeg) into two full-screen buffers and pushes
// them straight to the panel (display_direct_*), LVGL is paused meanwhile. When a button is
// held the ring overlay needs LVGL: direct mode stops, the LVGL screen shows the last frame
// through an image that points at the same buffer, and the player waits.
#include "ui.h"
#include "media.h"
#include "power.h"
#include "settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "pendant";

static pendant_arg_t start_arg;
static uint16_t *frame[2];
static volatile int shown = -1;          // buffer on the panel (and in the LVGL image)
static volatile int cur;                 // media index
static volatile bool want_next, quit, running;
static SemaphoreHandle_t done_sem;
static lv_obj_t *img;
static lv_image_dsc_t dsc;
static uint8_t *inbuf;
static int inbuf_len;
static uint32_t seed;
static int64_t grace_until;
static bool was_off, dirty_last;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static bool ensure_in(int n)
{
    if (n <= inbuf_len) return true;
    heap_caps_free(inbuf);
    inbuf = heap_caps_malloc(n + 16 * 1024, MALLOC_CAP_SPIRAM);
    inbuf_len = inbuf ? n + 16 * 1024 : 0;
    return inbuf != NULL;
}

// may the player draw now? (not while the ring overlay or a dark screen is up)
static bool can_draw(void) { return display_direct_on() && !display_is_dark(); }

static void show_frame(int k)
{
    display_direct_push(frame[k]);
    shown = k;
    dsc.data = (const uint8_t *)frame[k];
}

// waits until t (ms), false if the media must change or the player stops
static bool wait_until(int64_t t)
{
    while (!quit && !want_next) {
        int64_t d = t - now_ms();
        if (d <= 0) return true;
        vTaskDelay(pdMS_TO_TICKS(d > 20 ? 20 : d));
    }
    return false;
}

// screen off (or a ring overlay): nothing is read or decoded meanwhile. With the panel dark
// the check is slow, so the chip spends the time in light sleep.
static void wait_drawable(void)
{
    while (!quit && !want_next && !can_draw()) vTaskDelay(pdMS_TO_TICKS(display_is_dark() ? 150 : 30));
}

// decodes jpg into the free buffer and shows it
static bool decode_show(const uint8_t *jpg, int len)
{
    int k = shown == 0 ? 1 : 0;
    if (!media_decode(jpg, len, frame[k], SCR_W, SCR_H)) return false;
    wait_drawable();
    if (quit) return false;
    show_frame(k);
    return true;
}

static void play_image(const char *path, int64_t until)
{
    int len = 0;
    power_boost_begin();   // full speed to read and decode, then the chip may sleep: the
    uint8_t *j = media_read_file(path, &len, 4 * 1024 * 1024);   // AMOLED keeps the picture
    bool ok = j && decode_show(j, len);
    free(j);
    power_boost_end();
    if (!ok) { ESP_LOGW(TAG, "cannot show %s", path); vTaskDelay(pdMS_TO_TICKS(500)); return; }
    // a still image: nothing to do until the slideshow moves on (or the screen comes back)
    while (!quit && !want_next && (!until || now_ms() < until)) {
        if (!can_draw()) { wait_drawable(); if (can_draw() && shown >= 0) display_direct_push(frame[shown]); }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void play_anim(const char *path, uint32_t size, int64_t until)
{
    FILE *f = fopen(path, "rb");
    if (!f) { vTaskDelay(pdMS_TO_TICKS(500)); return; }
    uint8_t hdr[AMJ_HEADER];
    amj_info_t info;
    uint8_t *index = NULL;
    if (fread(hdr, 1, AMJ_HEADER, f) != AMJ_HEADER || !amj_parse_header(hdr, AMJ_HEADER, size, &info) ||
        !(index = heap_caps_malloc(info.frames * AMJ_ENTRY, MALLOC_CAP_SPIRAM)) ||
        fread(index, 1, info.frames * AMJ_ENTRY, f) != info.frames * AMJ_ENTRY) {
        ESP_LOGW(TAG, "%s: not a valid AMJ", path);
        heap_caps_free(index);
        fclose(f);
        vTaskDelay(pdMS_TO_TICKS(500));
        return;
    }
    bool loop_done = false;
    int64_t due = now_ms();
    for (uint32_t i = 0; !quit && !want_next; i = (i + 1) % info.frames) {
        if (i == 0 && loop_done && until && now_ms() >= until) break;   // slideshow: at least one full loop
        if (!can_draw()) {   // screen off / ring overlay: stop here, read and decode nothing
            wait_drawable();
            if (quit || want_next) break;
            due = now_ms();
        }
        // full speed only to read and decode the frame; the wait until it is due runs at the
        // low clock (and in light sleep)
        power_boost_begin();
        amj_frame_t fr;
        if (!amj_entry(index, i, size, &fr) || !ensure_in(fr.size) || fseek(f, fr.offset, SEEK_SET) ||
            fread(inbuf, 1, fr.size, f) != fr.size) {
            power_boost_end();
            ESP_LOGW(TAG, "%s: frame %u unreadable", path, (unsigned)i);
            vTaskDelay(pdMS_TO_TICKS(200));
            break;
        }
        int k = shown == 0 ? 1 : 0;
        bool ok = media_decode(inbuf, fr.size, frame[k], SCR_W, SCR_H);
        power_boost_end();
        if (!ok) continue;
        if (!can_draw()) { wait_drawable(); due = now_ms(); }
        if (!wait_until(due)) break;
        show_frame(k);
        // late (slow card, big frame): do not try to catch up, just keep the rhythm
        due += fr.delay_ms;
        if (due < now_ms() - 200) due = now_ms();
        if (i == info.frames - 1) loop_done = true;
    }
    heap_caps_free(index);
    fclose(f);
}

// kinds of the media on the card, for the content filter (images / GIFs and videos / both)
static uint8_t *kinds;
static int kinds_len;
static bool filter_warned;

static bool fill_kinds(int n)
{
    if (n > kinds_len) {
        free(kinds);
        kinds = malloc(n);
        kinds_len = kinds ? n : 0;
        if (!kinds) return false;
    }
    for (int i = 0; i < n; i++) {
        const media_item_t *m = media_get(i);
        kinds[i] = m ? m->kind : MEDIA_NONE;
    }
    return true;
}

// the filter of the settings, or "everything" when no media passes it
static int active_filter(int n)
{
    int f = g_set.disp_filter % MEDIA_FILTERS;
    if (f != MEDIA_FILTER_ALL && media_filter_count(kinds, n, f) == 0) {
        if (!filter_warned) { filter_warned = true; ui_toast_async(T(S_NO_MEDIA_FILTER)); }
        f = MEDIA_FILTER_ALL;
    }
    return f;
}

static void player_task(void *arg)
{
    bool first = true;
    while (!quit) {
        int n = media_count();
        if (!n || !fill_kinds(n)) { vTaskDelay(pdMS_TO_TICKS(300)); continue; }
        if (cur >= n) cur = 0;
        int filter = active_filter(n);
        // a slideshow starts from the first media of the chosen kind; a single media chosen in
        // the gallery is shown as it is
        if (first && start_arg.slideshow) {
            int c = media_pick_first(kinds, n, cur, filter);
            if (c >= 0) cur = c;
        }
        first = false;
        const media_item_t *m = media_get(cur);
        char path[96];
        snprintf(path, sizeof(path), MEDIA_DIR "/%s", m->file);
        int kind = m->kind;
        uint32_t size = m->size;
        if (strcmp(g_set.disp_last, m->file)) { strlcpy(g_set.disp_last, m->file, sizeof(g_set.disp_last)); dirty_last = true; }
        int64_t until = start_arg.slideshow ? now_ms() + *settings_interval(filter) * 1000LL : 0;
        want_next = false;
        if (kind == MEDIA_ANIM) play_anim(path, size, until);
        else play_image(path, until);
        if (quit) break;
        // next media: BOOT, or the slideshow timer
        bool by_button = want_next;
        want_next = false;
        if (!start_arg.slideshow && !by_button) continue;   // single media: loop again
        n = media_count();
        if (!n || !fill_kinds(n)) continue;
        filter = active_filter(n);
        int next = media_pick_next(kinds, n, cur, filter, start_arg.slideshow && g_set.disp_shuffle, &seed);
        cur = next >= 0 ? next : (cur + 1) % n;
    }
    running = false;
    xSemaphoreGive(done_sem);
    vTaskDelete(NULL);
}

/* ---------------- screen ---------------- */

static void enter(lv_obj_t *root, void *arg)
{
    if (arg) start_arg = *(pendant_arg_t *)arg;
    if (!done_sem) done_sem = xSemaphoreCreateBinary();
    for (int k = 0; k < 2; k++)
        if (!frame[k]) frame[k] = media_frame_alloc(SCR_W, SCR_H);
    if (!frame[0] || !frame[1]) { ui_toast(T(S_ERR_MEMORY)); return; }
    dsc = (lv_image_dsc_t){
        .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565, .w = SCR_W, .h = SCR_H, .stride = SCR_W * 2},
        .data_size = SCR_W * SCR_H * 2,
        .data = (const uint8_t *)frame[shown >= 0 ? shown : 0],
    };
    img = lv_image_create(root);
    lv_image_set_src(img, &dsc);
    lv_obj_center(img);

    cur = start_arg.start;
    seed = esp_random();
    quit = want_next = false;
    filter_warned = false;
    grace_until = 0;
    was_off = false;
    power_motion_reset();
    ui_brightness_override(g_set.disp_brightness);
    display_direct_begin();
    running = true;
    if (xTaskCreatePinnedToCore(player_task, "player", 6144, NULL, 3, NULL, 0) != pdPASS) {
        running = false;
        display_direct_end();
    }
}

static void leave(void)
{
    if (running) {
        quit = true;
        xSemaphoreTake(done_sem, pdMS_TO_TICKS(3000));
    }
    display_direct_end();
    ui_brightness_override(-1);
    if (dirty_last) { settings_save(); dirty_last = false; }
}

static bool event(ev_t e, int x, int y)
{
    switch (e) {
    case EV_BOOT_CLICK: want_next = true; return true;
    case EV_BOOT_LONG: ui_pop(); return true;
    default: return true;   // nothing else does anything here
    }
}

static void tick(void)
{
    if (!running) return;
    // the ring overlay of a held button has gone: back to direct mode
    if (!display_direct_on() && input_boot_held_ms() == 0 && input_pwr_held_ms() == 0) display_direct_begin();

    int64_t now = now_ms();
    if (was_off && !ui_is_off()) grace_until = now + 60000;   // woken by hand: stay on a while
    bool night = power_night();
    bool night_off = night && g_set.night_mode == 1 && now > grace_until;
    bool timeout = false;
    if (g_set.disp_off_min) {
        uint32_t idle = input_idle_ms();
        if (g_set.motion_wake && power_motion_idle_ms() < idle) idle = power_motion_idle_ms();
        timeout = idle > g_set.disp_off_min * 60000u;
    }
    if (!ui_is_off() && (night_off || timeout)) {
        ui_screen_off(false);
    } else if (ui_is_off() && !ui_is_locked() && !night_off && g_set.motion_wake && power_motion_idle_ms() < 800) {
        ui_wake();
    }
    was_off = ui_is_off();
    int b = g_set.disp_brightness;
    if (night && g_set.night_mode == 0) b = b / 4 < 5 ? 5 : b / 4;
    ui_brightness_override(b);
}

const screen_t scr_pendant = {"pendant", enter, leave, event, tick, SCR_NO_STATUS | SCR_NO_SLEEP | SCR_NO_TOUCH | SCR_NO_PULL};

void pendant_start(int index, bool slideshow)
{
    if (!media_count()) { ui_toast(T(S_NO_MEDIA)); return; }
    static pendant_arg_t a;
    a.start = index;
    a.slideshow = slideshow;
    if (g_set.disp_slideshow != slideshow) { g_set.disp_slideshow = slideshow; settings_save(); }
    ui_push(&scr_pendant, &a);
}

void pendant_resume(void)
{
    if (!media_count()) media_scan();
    int i = media_find(g_set.disp_last);
    pendant_start(i < 0 ? 0 : i, g_set.disp_slideshow);
}
