// home.c - the launcher: apps on a ring along the edge, the selected one at 12 o'clock.
// Turning the virtual bezel (or swiping) rotates the ring; tap opens. BOOT = pendant mode.
#include "ui.h"
#include "media.h"
#include "sd.h"
#include "settings.h"
#include "wifi_mgr.h"
#include "board.h"
#include <math.h>
#include <stdio.h>
#include <time.h>

typedef struct { const char *icon; int name; const screen_t *scr; } app_t;
static const app_t apps[] = {
    {IC_GEM, S_APP_PENDANT, NULL},
    {IC_IMAGE, S_APP_GALLERY, &scr_gallery},
    {IC_UPLOAD, S_APP_UPLOAD, &scr_upload},
    {IC_CLOCK, S_APP_CLOCK, &scr_clock},
    {IC_COG, S_APP_SETTINGS, &scr_settings},
    {IC_INFO, S_APP_INFO, &scr_info},
};
#define N (int)(sizeof(apps) / sizeof(apps[0]))
#define RING_R 166
#define DISC   112
#define STEP   (360.0f / N)

static int sel;            // kept across visits
static float pos;          // animated position (index units)
static lv_obj_t *disc[N], *ico[N], *time_l, *name_l, *sub_l;

static float wrapd(float d)   // shortest distance on the ring, in items
{
    while (d > N / 2.0f) d -= N;
    while (d < -N / 2.0f) d += N;
    return d;
}

static void layout(void)
{
    for (int i = 0; i < N; i++) {
        float d = wrapd(i - pos);
        float a = (-90.0f - d * STEP) * 0.0174533f;
        int x = SCR_C + (int)lroundf(RING_R * cosf(a)) - DISC / 2;
        int y = SCR_C + (int)lroundf(RING_R * sinf(a)) - DISC / 2;
        float t = 1.0f - fabsf(d);
        if (t < 0) t = 0;
        lv_obj_set_pos(disc[i], x, y);
        lv_obj_set_style_transform_scale(disc[i], (int)(256 * (0.58f + 0.42f * t)), 0);
        lv_obj_set_style_border_opa(disc[i], (lv_opa_t)(255 * t), 0);
        lv_obj_set_style_bg_color(disc[i], lv_color_mix(ui_accent_dark(), C_CARD, (uint8_t)(255 * t)), 0);
        lv_obj_set_style_text_color(ico[i], lv_color_mix(ui_accent(), C_DIM, (uint8_t)(255 * t)), 0);
    }
}

static void anim_pos(void *v, int32_t x) { pos = x / 256.0f; layout(); }

static void sub_text(char *b, int n)
{
    b[0] = 0;
    switch (sel) {
    case 0:
        if (!sd_ok()) snprintf(b, n, "%s", T(S_NO_SD));
        else if (!media_count()) snprintf(b, n, "%s", T(S_NO_MEDIA_SHORT));
        else snprintf(b, n, "%s", T(g_set.disp_slideshow ? S_SLIDESHOW : S_SINGLE_MEDIA));
        break;
    case 1:
        if (!sd_ok()) snprintf(b, n, "%s", T(S_NO_SD));
        else snprintf(b, n, T(S_N_MEDIA), media_count());
        break;
    case 2:
        if (wifi_mgr_state() == WIFI_CONNECTED) snprintf(b, n, IC_WIFI "  %s", g_set.wifi_ssid);
        else snprintf(b, n, "%s", T(S_VIA_HOTSPOT));
        break;
    case 3: {
        time_t t = time(NULL);
        struct tm lt;
        localtime_r(&t, &lt);
        if (t > 1735689600) strftime(b, n, "%d.%m.%Y", &lt);
        break;
    }
    case 5: ui_battery_icon(b, n); break;
    default: break;
    }
}

static void refresh_text(void)
{
    char t[16], s[64];
    ui_time_str(t, sizeof(t));
    ui_set_text(time_l, t);
    ui_set_text(name_l, T(apps[sel].name));
    sub_text(s, sizeof(s));
    ui_set_text(sub_l, s);
}

static void go(int d)
{
    float from = pos;
    sel = ((sel + d) % N + N) % N;
    // animate along the shortest way
    float to = from + wrapd(sel - from);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &pos);
    lv_anim_set_values(&a, (int32_t)(from * 256), (int32_t)(to * 256));
    lv_anim_set_duration(&a, 220);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, anim_pos);
    lv_anim_start(&a);
    refresh_text();
}

static void open_app(void)
{
    if (!apps[sel].scr) { pendant_resume(); return; }
    ui_push(apps[sel].scr, NULL);
}

static void enter(lv_obj_t *root, void *arg)
{
    pos = sel;
    for (int i = 0; i < N; i++) {
        disc[i] = ui_circle(root, DISC, C_CARD);
        lv_obj_set_style_border_color(disc[i], ui_accent(), 0);
        lv_obj_set_style_border_width(disc[i], 3, 0);
        lv_obj_set_style_transform_pivot_x(disc[i], DISC / 2, 0);
        lv_obj_set_style_transform_pivot_y(disc[i], DISC / 2, 0);
        ico[i] = ui_label(disc[i], &font_l, C_DIM, apps[i].icon);
        lv_obj_center(ico[i]);
    }
    time_l = ui_label(root, &font_m, C_DIM, "");
    lv_obj_align(time_l, LV_ALIGN_CENTER, 0, -52);
    name_l = ui_label(root, &font_l, C_TEXT, "");
    lv_obj_align(name_l, LV_ALIGN_CENTER, 0, 4);
    sub_l = ui_label(root, &font_s, ui_accent(), "");
    lv_obj_set_width(sub_l, 230);
    lv_label_set_long_mode(sub_l, LV_LABEL_LONG_DOT);
    lv_obj_align(sub_l, LV_ALIGN_CENTER, 0, 56);
    layout();
    refresh_text();
}

static bool event(ev_t e, int x, int y)
{
    switch (e) {
    case EV_DIAL_CW: case EV_SWIPE_LEFT: case EV_SWIPE_UP: go(1); return true;
    case EV_DIAL_CCW: case EV_SWIPE_RIGHT: case EV_SWIPE_DOWN: go(-1); return true;
    case EV_TAP: {
        int dx = x - SCR_C, dy = y - SCR_C;
        if (dx * dx + dy * dy < 120 * 120) { open_app(); return true; }
        // tap on a disc of the ring: bring it to the top (or open it if it is there)
        for (int i = 0; i < N; i++) {
            lv_area_t a;
            lv_obj_get_coords(disc[i], &a);
            if (x >= a.x1 && x <= a.x2 && y >= a.y1 && y <= a.y2) {
                if (i == sel) open_app();
                else go((int)wrapd(i - sel));
                return true;
            }
        }
        return true;
    }
    case EV_BOOT_CLICK: pendant_resume(); return true;
    case EV_BOOT_LONG: return true;
    default: return false;
    }
}

static void tick(void) { refresh_text(); }

static void leave(void)
{
    lv_anim_delete(&pos, NULL);   // the animation would move deleted objects
    pos = sel;
}

const screen_t scr_home = {"home", enter, leave, event, tick, SCR_NO_STATUS};
